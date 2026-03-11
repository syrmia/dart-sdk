// Copyright (c) 2025, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.



#include "vm/compiler/runtime_api.h"
#include "vm/globals.h"
#include "vm/compiler/backend/il.h"

#define SHOULD_NOT_INCLUDE_RUNTIME

#include "vm/compiler/stub_code_compiler.h"

#if defined(TARGET_ARCH_MIPS)

#include "vm/class_id.h"
#include "vm/code_entry_kind.h"
#include "vm/compiler/api/type_check_mode.h"
#include "vm/compiler/assembler/assembler.h"
#include "vm/compiler/backend/locations.h"
#include "vm/constants.h"
#include "vm/ffi_callback_metadata.h"
#include "vm/instructions.h"
#include "vm/static_type_exactness_state.h"
#include "vm/tags.h"

#define __ assembler->

namespace dart {
namespace compiler {

// Call a native function within a safepoint.
//
// On entry:
//   Stack: set up for call
//   T0: target to call
//
// On exit:
//   Stack: preserved
//   NOTFP, S2: clobbered, although normally callee-saved
void StubCodeCompiler::GenerateCallNativeThroughSafepointStub() {
  COMPILE_ASSERT(IsAbiPreservedRegister(S2));

  __ mov(S2, RA);

  __ LoadImmediate(T1, target::Thread::exit_through_ffi());
  __ TransitionGeneratedToNative(T0, FPREG, T1 /*volatile*/, T2,
                                 /*enter_safepoint=*/true);

#if defined(DEBUG)
  // Check SP alignment.
  __ AndImmediate(T2 /*volatile*/, SP, ~(OS::ActivationFrameAlignment() - 1));
  Label done;
  __ beq(T2, SP, &done);
  __ Breakpoint();
  __ Bind(&done);
#endif
  __ mov(T9, T0);
  __ jalr(T9);

  __ TransitionNativeToGenerated(T1 /*volatile*/, T2,
                                 /*exit_safepoint=*/true);

  __ jr(S2);
}

void StubCodeCompiler::GenerateLoadFfiCallbackMetadataRuntimeFunction(
    uword function_index,
    Register dst) {
  // Keep in sync with FfiCallbackMetadata::EnsureFirstTrampolinePageLocked.
  // Note: If the stub was aligned, this could be a single PC relative load.

  // Load a pointer to the beginning of the stub into dst.
  const intptr_t code_size = __ CodeSize();

  __ Push(RA);
  compiler::Label label_for_getting_pc;
  __ bal(&label_for_getting_pc);
  __ Bind(&label_for_getting_pc);
  //push = 2 instr, bal = 2 instr 
  __ AddImmediate(dst, RA, -4 * compiler::target::kWordSize - code_size);
  __ Pop(RA);

  // Round dst down to the page size.
  __ AndImmediate(dst, dst, FfiCallbackMetadata::kPageMask);

  // Load the function from the function table.
  __ LoadFromOffset(dst, dst,
                    FfiCallbackMetadata::RuntimeFunctionOffset(function_index));
}

void StubCodeCompiler::GenerateFfiCallbackTrampolineStub() {
#if defined(USING_SIMULATOR) && !defined(DART_PRECOMPILER)
  // TODO(37299): FFI is not supported in SIMMIPS.
  __ Breakpoint();
#else
  Label body;

  // T1 is volatile and not used for passing any arguments.
  COMPILE_ASSERT(!IsCalleeSavedRegister(T1) && !IsArgumentRegister(T1));
  for (intptr_t i = 0; i < FfiCallbackMetadata::NumCallbackTrampolinesPerPage();
       ++i) {
    // The FfiCallbackMetadata table is keyed by the trampoline entry point. So
    // look up the current PC, then jump to the shared section.
    __ Push(RA);
    compiler::Label label_for_getting_pc;
    __ bal(&label_for_getting_pc);
    __ Bind(&label_for_getting_pc);
    //push = 2 instr, bal = 2 instr 
    __ AddImmediate(T1, RA, -4 * compiler::target::kWordSize);
    __ Pop(RA);
    __ b(&body);
  }

  ASSERT_EQUAL(__ CodeSize(),
         FfiCallbackMetadata::kNativeCallbackTrampolineSize *
             FfiCallbackMetadata::NumCallbackTrampolinesPerPage());

  __ Bind(&body);

  const intptr_t shared_stub_start = __ CodeSize();

  // Save THR (callee-saved), and RA.
  COMPILE_ASSERT(FfiCallbackMetadata::kNativeCallbackTrampolineStackDelta == 2);
  
  __ PushRegisters(RegisterSet((1 << RA) | (1 << THR), 0));

  COMPILE_ASSERT(!IsArgumentRegister(THR));
  __ Push(T1);
  RegisterSet argument_registers;
  argument_registers.AddAllArgumentRegisters();
  __ PushRegisters(argument_registers);

  // Load the thread, verify the callback ID and exit the safepoint.
  //
  // We exit the safepoint inside DLRT_GetFfiCallbackMetadata in order to save
  // code size on this shared stub.
  {
    __ EnterFrame(0);    
    __ ReserveAlignedFrameSpace(4 * target::kWordSize);

    __ mov(A0, T1);                          // trampoline
    __ mov(A1, SPREG);                       // out_entry_point
    __ addi(A2, SPREG, Immediate(target::kWordSize));  // out_trampoline_type

    GenerateLoadFfiCallbackMetadataRuntimeFunction(
        FfiCallbackMetadata::kGetFfiCallbackMetadata, T1);

    __ mov(T9, T1);
    __ jalr(T9);
    __ mov(THR, V0);

    __ lw(T2, Address(SPREG, 0));                  // entry_point
    __ lw(T3, Address(SPREG, target::kWordSize));  // trampoline_type

    __ LeaveFrame();
  }

  __ PopRegisters(argument_registers);
  __ Pop(T1);

  COMPILE_ASSERT(!IsCalleeSavedRegister(T2) && !IsArgumentRegister(T2));
  COMPILE_ASSERT(!IsCalleeSavedRegister(T3) && !IsArgumentRegister(T3));

  Label async_callback;
  Label done;

  // If GetFfiCallbackMetadata returned a null thread, it means that the async
  // callback was invoked after it was deleted. In this case, do nothing.
  __ beq(THR, ZR, &done);

  // Check the trampoline type to see how the callback should be invoked.
  COMPILE_ASSERT(
      static_cast<uword>(FfiCallbackMetadata::TrampolineType::kSync) == 0);
  __ bne(T3, ZR, &async_callback);

  // Sync callback. The entry point contains the target function, so just call
  // it. DLRT_GetThreadForNativeCallbackTrampoline exited the safepoint, so
  // re-enter it afterwards.

  // On entry to the function, there will be four extra slots on the stack:
  // saved THR, R4, R5 and the return address. The target will know to skip
  // them.
  __ mov(T9, T2);
  __ jalr(T9);

  // Clobbers TMP, T1 and T4 -- all volatile and not holding return values.
  __ EnterFullSafepoint(T1, T4);

  __ b(&done);
  __ Bind(&async_callback);

  // Async callback. The entrypoint marshals the arguments into a message and
  // sends it over the send port. DLRT_GetThreadForNativeCallbackTrampoline
  // entered a temporary isolate, so exit it afterwards.

  // Clobbers all volatile registers, including the callback ID in T1.
  __ mov(T9, T2);
  __ jalr(T9);

  // Exit the temporary isolate.
  {
    const RegisterSet return_registers(
        (1 << CallingConventions::kReturnReg) |
            (1 << CallingConventions::kSecondReturnReg),
        1 << CallingConventions::kReturnFpuReg);
    __ PushRegisters(return_registers);

    __ EnterFrame(0);
    __ ReserveAlignedFrameSpace(4 * target::kWordSize);

    GenerateLoadFfiCallbackMetadataRuntimeFunction(
        FfiCallbackMetadata::kExitTemporaryIsolate, T1);
        
    __ mov(T9, T1);
    __ jalr(T9);

    __ LeaveFrame();
    __ PopRegisters(return_registers);
  }

  __ Bind(&done);

  // Returns.
  __ PopRegisters(RegisterSet((1 << RA) | (1 << THR), 0));
  __ Ret();

  ASSERT_EQUAL(__ CodeSize() - shared_stub_start,
                       FfiCallbackMetadata::kNativeCallbackSharedStubSize);
  ASSERT_LESS_OR_EQUAL(__ CodeSize(), FfiCallbackMetadata::kPageSize);

#if defined(DEBUG)
  while (__ CodeSize() < FfiCallbackMetadata::kPageSize) {
    __ Breakpoint();
  }
#endif
#endif
}

// Called when invoking Dart code from C++ (VM code).
// Input parameters:
//   RA : points to return address.
//   A0 : target code or entry point (in AOT mode).
//   A1 : arguments descriptor array.
//   A2 : arguments array (address of first argument).
//   A3 : current thread.
void StubCodeCompiler::GenerateInvokeDartCodeStub() {
  // Save frame pointer coming in.
  __ Comment("InvokeDartCodeStub");
  __ EnterFrame();

  // Push code object to PC marker slot.
  __ lw(TMP, Address(A3, target::Thread::invoke_dart_code_stub_offset()));
  __ Push(TMP);

  // Save new context and C++ ABI callee-saved registers.

  // The saved vm tag, top resource, and top exit frame info.
  const intptr_t kPreservedSlots = 4;
  const intptr_t kPreservedRegSpace =
     target::kWordSize *
      (kAbiPreservedCpuRegCount + kAbiPreservedFRegCount + kPreservedSlots);

  __ addiu(SP, SP, Immediate(-kPreservedRegSpace));
  for (int i = S0; i <= S7; i++) {
    Register r = static_cast<Register>(i);
    const intptr_t slot = i - S0 + kPreservedSlots;
    __ sw(r, Address(SP, slot *target::kWordSize));
  }

  for (intptr_t i = kAbiFirstPreservedFReg; i <= kAbiLastPreservedFReg;
       i++) {
    FRegister r = static_cast<FRegister>(i);
    const intptr_t slot = kAbiPreservedCpuRegCount + kPreservedSlots + i -
                          kAbiFirstPreservedFReg;
    __ swc1(r, Address(SP, slot *target::kWordSize));
  }

  // Set up THR, which caches the current thread in Dart code.
  if (THR != A3) {
    __ mov(THR, A3);
  }

  // Save the current VMTag on the stack.
  __ lw(T1, Assembler::VMTagAddress());
  __ sw(T1, Address(SP, 3 *target::kWordSize));


  // Save top resource and top exit frame info. Use T0 as a temporary register.
  // StackFrameIterator reads the top exit frame info saved in this frame.
  __ lw(T0, Address(THR, target::Thread::top_resource_offset()));
  __ sw(ZR, Address(THR, target::Thread::top_resource_offset()));
  __ sw(T0, Address(SP, 2 *target::kWordSize));
  __ lw(T0, Address(THR, target::Thread::exit_through_ffi_offset()));
  __ sw(ZR, Address(THR, target::Thread::exit_through_ffi_offset()));
  __ sw(T0, Address(SP, 1 *target::kWordSize));
  __ lw(T0, Address(THR, target::Thread::top_exit_frame_info_offset()));
  __ sw(ZR, Address(THR, target::Thread::top_exit_frame_info_offset()));
  __ sw(T0, Address(SP, 0 *target::kWordSize));
  
  // target::frame_layout.exit_link_slot_from_entry_fp must be kept in sync
  // with the code below.
  ASSERT(target::frame_layout.exit_link_slot_from_entry_fp == -25);

  // In debug mode, verify that we've pushed the top exit frame info at the
  // correct offset from FP.
  __ EmitEntryFrameVerification(T0);

  // Mark that the thread is executing Dart code.
  __ LoadImmediate(T0, VMTag::kDartTagId);
  __ sw(T0, Assembler::VMTagAddress());

  // Load arguments descriptor array, which is passed to Dart code.
  __ mov(ARGS_DESC_REG, A1);

  // Load number of arguments into T1 and adjust count for type arguments.
  __ LoadFieldFromOffset(T1, ARGS_DESC_REG,
                         target::ArgumentsDescriptor::count_offset());
  __ LoadFieldFromOffset(T3, ARGS_DESC_REG,
                         target::ArgumentsDescriptor::type_args_len_offset());
  __ SmiUntag(T1);

  // Include the type arguments.
  Label isZero;
  __ beq(T3, ZR, &isZero);
  __ LoadImmediate(T3, 1);
  __ addu(T1, T1, T3);
  __ Bind(&isZero);

  // Compute address of 'arguments array' data area into A2.
 __ AddImmediate(A2, A2, target::Array::data_offset() - kHeapObjectTag);

  // Set up arguments for the Dart call.
  Label push_arguments;
  Label done_push_arguments;
  __ beq(T1, ZR, &done_push_arguments);  // check if there are arguments.
  __ mov(A1, ZR);
  __ Bind(&push_arguments);
  __ lw(A3, Address(A2));
  __ Push(A3);
  __ addiu(A2, A2, Immediate(target::kWordSize));
  __ addiu(A1, A1, Immediate(1));
  __ BranchSignedLess(A1, T1, &push_arguments);
  __ Bind(&done_push_arguments);

  if(FLAG_precompiled_mode) {
    __ LoadImmediate(CODE_REG, 0);  // GC safe value into CODE_REG.
    __ lw(PP, Address(THR, target::Thread::global_object_pool_offset()));
  } else {
    // We now load the pool pointer(PP) with a GC safe value as we are about to
    // invoke dart code. We don't need a real object pool here.
    __ LoadImmediate(PP, 0);  // GC safe value into PP.
    __ mov(CODE_REG, A0);
    __ lw(A0, FieldAddress(CODE_REG, target::Code::entry_point_offset()));
  }

  // Call the Dart code entrypoint.
  // We are calling into Dart code, here, so there is no need to call through
  // T9 to match the ABI.
  __ mov(T9, A0);
  __ jalr(T9);  // S4 is the arguments descriptor array.
  __ Comment("InvokeDartCodeStub return");

  // Get rid of arguments pushed on the stack.
  __ AddImmediate(SP, FP, target::frame_layout.exit_link_slot_from_entry_fp * target::kWordSize);


  // Restore the current VMTag, the saved top exit frame info and top resource
  // back into the Thread structure.
  __ lw(TMP, Address(SP, 0 * target::kWordSize));
  __ sw(TMP, Address(THR, target::Thread::top_exit_frame_info_offset()));
  __ lw(TMP, Address(SP, 1 * target::kWordSize));
  __ sw(TMP, Address(THR, target::Thread::exit_through_ffi_offset()));
  __ lw(TMP, Address(SP, 2 * target::kWordSize));
  __ sw(TMP, Address(THR, target::Thread::top_resource_offset()));
  __ lw(TMP, Address(SP, 3 * target::kWordSize));
  __ sw(TMP, Address(THR, target::Thread::vm_tag_offset()));

  // Restore C++ ABI callee-saved registers.
  for (int i = S0; i <= S7; i++) {
    Register r = static_cast<Register>(i);
    const intptr_t slot = i - S0 + kPreservedSlots;
    __ lw(r, Address(SP, slot *target::kWordSize));
  }

  for (intptr_t i = kAbiFirstPreservedFReg; i <= kAbiLastPreservedFReg;
       i++) {
    FRegister r = static_cast<FRegister>(i);
    const intptr_t slot = kAbiPreservedCpuRegCount + kPreservedSlots + i -
                          kAbiFirstPreservedFReg;
    __ lwc1(r, Address(SP, slot *target::kWordSize));
  }

  __ addiu(SP, SP, Immediate(kPreservedRegSpace));

  __ set_constant_pool_allowed(false);


  // Restore the frame pointer and return.
  __ LeaveFrameAndReturn();
}

// S5: Contains an ICData.
void StubCodeCompiler::GenerateICCallBreakpointStub() {
#if defined(PRODUCT)
  __ Stop("No debugging in PRODUCT mode");
#else
  __ Comment("ICCallBreakpoint stub");
  __ EnterStubFrame();
  __ addiu(SP, SP, Immediate(-3 *target::kWordSize));
  __ sw(A0, Address(SP, 2 *target::kWordSize));
  __ sw(S5, Address(SP, 1 *target::kWordSize));
  __ sw(ZR, Address(SP, 0 *target::kWordSize));

  __ CallRuntime(kBreakpointRuntimeHandlerRuntimeEntry, 0);

  __ lw(A0, Address(SP, 2 *target::kWordSize));
  __ lw(S5, Address(SP, 1 *target::kWordSize));
  __ lw(CODE_REG, Address(SP, 0 *target::kWordSize));
  __ addiu(SP, SP, Immediate(3 *target::kWordSize));
  __ LeaveStubFrame();
  __ lw(TMP, FieldAddress(CODE_REG, Code::entry_point_offset()));
  __ jr(TMP);
#endif  // defined(PRODUCT)
}

// S5: ICData
void StubCodeCompiler::GenerateUnoptStaticCallBreakpointStub() {
#if defined(PRODUCT)
  __ Stop("No debugging in PRODUCT mode");
#else
  __ EnterStubFrame();
  __ AddImmediate(SP, SP, -2 * target::kWordSize);
  __ sw(S5, Address(SP, 1 * target::kWordSize));  // Preserve IC data.
  __ sw(ZR, Address(SP, 0 * target::kWordSize));  // Space for result.
  __ CallRuntime(kBreakpointRuntimeHandlerRuntimeEntry, 0);
  __ lw(CODE_REG, Address(SP, 0 * target::kWordSize));  // Original stub.
  __ lw(S5, Address(SP, 1 * target::kWordSize));        // Restore IC data.
  __ AddImmediate(SP, SP, 2 * target::kWordSize);
  __ LeaveStubFrame();
  __ LoadFieldFromOffset(TMP, CODE_REG, target::Code::entry_point_offset());
  __ jr(TMP);
#endif  // defined(PRODUCT)
}

void StubCodeCompiler::GenerateRuntimeCallBreakpointStub() {
#if defined(PRODUCT)
  __ Stop("No debugging in PRODUCT mode");
#else
  __ Comment("RuntimeCallBreakpoint stub");
  __ EnterStubFrame();
  __ addiu(SP, SP, Immediate(-1 *target::kWordSize));
  __ sw(ZR, Address(SP, 0 *target::kWordSize));

  __ CallRuntime(kBreakpointRuntimeHandlerRuntimeEntry, 0);

  __ lw(CODE_REG, Address(SP, 0 *target::kWordSize));
  __ addiu(SP, SP, Immediate(1 *target::kWordSize));
  __ LeaveStubFrame();
  __ lw(TMP, FieldAddress(CODE_REG, Code::entry_point_offset()));
  __ jr(TMP);
#endif  // defined(PRODUCT)
}

}  // namespace compiler
}  // namespace dart

#endif  // defined TARGET_ARCH_MIPS

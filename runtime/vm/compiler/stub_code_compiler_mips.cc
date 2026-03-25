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

// Ensures that [V0] is a new object, if not it will be added to the remembered
// set via a leaf runtime call.
//
// WARNING: This might clobber all registers except for [V0], [THR] and [FP].
// The caller should simply call LeaveStubFrame() and return.
void StubCodeCompiler::EnsureIsNewOrRemembered() {
  // If the object is not in an active TLAB, we call a leaf-runtime to add it to
  // the remembered set and/or deferred marking worklist. This test assumes a
  // Page's TLAB use is always ascending.
  Label done;
  __ AndImmediate(TMP, V0, target::Page::kPageMask);
  __ LoadFromOffset(TMP, TMP, target::Page::original_top_offset());
  __ BranchUnsignedGreaterEqual(V0, TMP, &done);

  {
    LeafRuntimeScope rt(assembler, /*frame_size=*/0,
                        /*preserve_registers=*/false);

    __ mov(A0, V0);
    __ mov(A1, THR);
    rt.Call(kEnsureRememberedAndMarkingDeferredRuntimeEntry,
            /*argument_count=*/2);
  }

  __ Bind(&done);
}

// Input parameters:
//   RA : return address.
//   SP : address of last argument in argument array.
//   SP + 4*S4 - 4 : address of first argument in argument array.
//   SP + 4*S4 : address of return value.
//   S5 : address of the runtime function to call.
//   S4 : number of arguments to the call.
void StubCodeCompiler::GenerateCallToRuntimeStub() {
  const intptr_t thread_offset = target::NativeArguments::thread_offset();
  const intptr_t argc_tag_offset = target::NativeArguments::argc_tag_offset();
  const intptr_t argv_offset = target::NativeArguments::argv_offset();
  const intptr_t retval_offset = target::NativeArguments::retval_offset();

  __ lw(CODE_REG, Address(THR, target::Thread::call_to_runtime_stub_offset()));
  __ Comment("CallToRuntimeStub");
  __ EnterStubFrame();

  // Save exit frame information to enable stack walking as we are about
  // to transition to Dart VM C++ code.
  __ sw(FP, Address(THR, Thread::top_exit_frame_info_offset()));

  // Mark that the thread exited generated code through a runtime call.
  __ LoadImmediate(T0, target::Thread::exit_through_runtime_call());
  __ StoreToOffset(T0, THR, target::Thread::exit_through_ffi_offset());

#if defined(DEBUG)
  {
    Label ok;
    // Check that we are always entering from Dart code.
    __ LoadFromOffset(T0, THR, target::Thread::vm_tag_offset());
    __ BranchEqual(T0, Immediate(VMTag::kDartTagId), &ok);
    __ Stop("Not coming from Dart code.");
    __ Bind(&ok);
  }
#endif

  // Mark that the thread is executing VM code.
  __ sw(S5, Assembler::VMTagAddress());

  // Reserve space for arguments and align frame before entering C++ world.
  // NativeArguments are passed in registers.
  ASSERT(target::NativeArguments::StructSize() == 4 * target::kWordSize);
  __ ReserveAlignedFrameSpace(target::NativeArguments::StructSize());  // Reserve space for arguments.

  // Pass NativeArguments structure by value and call runtime.
  // Registers A0, A1, A2, and A3 are used.

  ASSERT(thread_offset == 0 *target::kWordSize);
  // Set thread in NativeArgs.
  __ mov(A0, THR);

  // There are no runtime calls to closures, so we do not need to set the tag
  // bits kClosureFunctionBit and kInstanceFunctionBit in argc_tag_.
  ASSERT(argc_tag_offset == 1 *target::kWordSize);
  __ mov(A1, S4);  // Set argc in NativeArguments.

  ASSERT(argv_offset == 2 *target::kWordSize);
  __ sll(A2, S4, 2);
  __ addu(A2, FP, A2);  // Compute argv.
  // Set argv in NativeArguments.
  __ addiu(A2, A2,
        Immediate(target::frame_layout.param_end_from_fp *target::kWordSize));

  ASSERT(retval_offset == 3 *target::kWordSize);
  // Retval is next to 1st argument.
  __ addiu(A3, A2, Immediate(target::kWordSize));

  // Call runtime or redirection via simulator.
  // We defensively always jalr through T9 because it is sometimes required by
  // the MIPS ABI.
  __ mov(T9, S5);
  __ jalr(T9);

  __ Comment("CallToRuntimeStub return");

  // Mark that the thread is executing Dart code.
  __ LoadImmediate(A2, VMTag::kDartTagId);
  __ sw(A2, Assembler::VMTagAddress());

  // Reset exit frame information in Isolate structure.
  __ sw(ZR, Address(THR, Thread::exit_through_ffi_offset()));
  __ sw(ZR, Address(THR, Thread::top_exit_frame_info_offset()));

  // Restore the global object pool after returning from runtime (old space is
  // moving, so the GOP could have been relocated).
  if (FLAG_precompiled_mode) {
    __ lw(PP, Address(THR, target::Thread::global_object_pool_offset()));
  }

  __ LeaveStubFrame();

  // The following return can jump to a lazy-deopt stub, which assumes V0
  // contains a return value and will save it in a GC-visible way.  We therefore
  // have to ensure V0 does not contain any garbage value left from the C
  // function we called (which has return type "void").
  // (See GenerateDeoptimizationSequence::saved_result_slot_from_fp.)
  __ LoadImmediate(V0, 0);

  __ Ret();
}

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
#if defined(DART_INCLUDE_SIMULATOR) && !defined(DART_PRECOMPILER)
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

void StubCodeCompiler::GenerateSharedStubGeneric(
    bool save_fpu_registers,
    intptr_t self_code_stub_offset_from_thread,
    bool allow_return,
    std::function<void()> perform_runtime_call) {
  // We want the saved registers to appear like part of the caller's frame, so
  // we push them before calling EnterStubFrame.
  RegisterSet all_registers;
  all_registers.AddAllNonReservedRegisters(save_fpu_registers);

  // To make the stack map calculation architecture independent we do the same
  // as on intel.
  __ PushRegister(RA);
  __ PushRegisters(all_registers);
  __ lw(CODE_REG, Address(THR, self_code_stub_offset_from_thread));
  __ EnterStubFrame();
  perform_runtime_call();
  if (!allow_return) {
    __ Breakpoint();
    return;
  }
  __ LeaveStubFrame();
  __ PopRegisters(all_registers);
  __ Drop(1);  // We use the LR restored via LeaveStubFrame.
  __ Ret();
}

void StubCodeCompiler::GenerateSharedStub(
    bool save_fpu_registers,
    const RuntimeEntry* target,
    intptr_t self_code_stub_offset_from_thread,
    bool allow_return,
    bool store_runtime_result_in_result_register) {
  ASSERT(!store_runtime_result_in_result_register || allow_return);
  auto perform_runtime_call = [&]() {
    if (store_runtime_result_in_result_register) {
      __ LoadObject(T7, NullObject());
      __ PushRegister(T7);
    }
    __ CallRuntime(*target, /*argument_count=*/0);
    if (store_runtime_result_in_result_register) {
      __ PopRegister(V0);
      __ sw(V0, Address(FP, target::kWordSize *
                                StubCodeCompiler::WordOffsetFromFpToCpuRegister(
                                    SharedSlowPathStubABI::kResultReg)));
    }
  };
  GenerateSharedStubGeneric(save_fpu_registers,
                            self_code_stub_offset_from_thread, allow_return,
                            perform_runtime_call);
}

// Input parameters:
//   RA : return address.
//   SP : address of return value.
//   T5 : address of the native function to call.
//   A2 : address of first argument in argument array.
//   A1 : argc_tag including number of arguments and function kind.
static void GenerateCallNativeWithWrapperStub(Assembler* assembler,
                                              Address wrapper) {
  const intptr_t thread_offset = NativeArguments::thread_offset();
  const intptr_t argc_tag_offset = NativeArguments::argc_tag_offset();
  const intptr_t argv_offset = NativeArguments::argv_offset();
  const intptr_t retval_offset = NativeArguments::retval_offset();

  __ SetPrologueOffset();
  __ Comment("CallNativeCFunctionStub");
  __ EnterStubFrame();

  // Save exit frame information to enable stack walking as we are about
  // to transition to native code.
  __ sw(FP, Address(THR, Thread::top_exit_frame_info_offset()));

  // Mark that the thread exited generated code through a runtime call.
  __ LoadImmediate(T0, target::Thread::exit_through_runtime_call());
  __ StoreToOffset(T0, THR, target::Thread::exit_through_ffi_offset());

#if defined(DEBUG)
  {
    Label ok;
    // Check that we are always entering from Dart code.
    __ LoadFromOffset(T0, THR, target::Thread::vm_tag_offset());
    __ BranchEqual(T0, Immediate(VMTag::kDartTagId), &ok);
    __ Stop("Not coming from Dart code.");
    __ Bind(&ok);
  }
#endif

  // Mark that the thread is executing native code.
  __ sw(T5, Assembler::VMTagAddress());

  // Reserve space for the native arguments structure passed on the stack (the
  // outgoing pointer parameter to the native arguments structure is passed in
  // A0) and align frame before entering the C++ world.
  __ ReserveAlignedFrameSpace(target::NativeArguments::StructSize());

  // Initialize target::NativeArguments structure and call native function.
  // Registers A0, A1, A2, and A3 are used.

  ASSERT(thread_offset == 0 *target::kWordSize);
  // Set thread in NativeArgs.
  __ mov(A0, THR);

  // There are no native calls to closures, so we do not need to set the tag
  // bits kClosureFunctionBit and kInstanceFunctionBit in argc_tag_.
  ASSERT(argc_tag_offset == 1 *target::kWordSize);
  // Set argc in NativeArguments: A1 already contains argc.

  ASSERT(argv_offset == 2 *target::kWordSize);
  // Set argv in NativeArguments: A2 already contains argv.

  ASSERT(retval_offset == 3 *target::kWordSize);
  // Set retval in NativeArgs.
  __ AddImmediate(
      A3, FP, (target::frame_layout.param_end_from_fp + 1) * target::kWordSize);

  // Passing the structure by value as in runtime calls would require changing
  // Dart API for native functions.
  // For now, space is reserved on the stack and we pass a pointer to it.
  __ sw(A3, Address(SP, 3 *target::kWordSize));
  __ sw(A2, Address(SP, 2 *target::kWordSize));
  __ sw(A1, Address(SP, 1 *target::kWordSize));
  __ sw(A0, Address(SP, 0 *target::kWordSize));
  __ mov(A0, SP);  // Pass the pointer to the NativeArguments.
  __ mov(A1, T5);  // Pass the function entrypoint.

  // Call native wrapper function or redirection via simulator.
  ASSERT(IsAbiPreservedRegister(THR));
  __ lw(T9, wrapper);
  __ jalr(T9);
  __ Comment("CallNativeCFunctionStub return");

  // Mark that the thread is executing Dart code.
  __ LoadImmediate(A2, VMTag::kDartTagId);
  __ StoreToOffset(A2, THR, target::Thread::vm_tag_offset());

  // Mark that the thread has not exited generated Dart code.
  __ sw(ZR, Address(THR, Thread::exit_through_ffi_offset()));

  // Reset exit frame information in Isolate structure.
  __ sw(ZR, Address(THR, Thread::top_exit_frame_info_offset()));

  if (FLAG_precompiled_mode) {
    __ lw(PP, Address(THR, target::Thread::global_object_pool_offset()));
  }

  __ LeaveStubFrame();

  __ Ret();
}

void StubCodeCompiler::GenerateCallNoScopeNativeStub() {
  GenerateCallNativeWithWrapperStub(
      assembler,
      Address(THR, Thread::no_scope_native_wrapper_entry_point_offset()));
}

void StubCodeCompiler::GenerateCallAutoScopeNativeStub() {
  GenerateCallNativeWithWrapperStub(
      assembler,
      Address(THR, Thread::auto_scope_native_wrapper_entry_point_offset()));
}

// Input parameters:
//   RA : return address.
//   SP : address of return value.
//   T5 : address of the native function to call.
//   A2 : address of first argument in argument array.
//   A1 : argc_tag including number of arguments and function kind.
void StubCodeCompiler::GenerateCallBootstrapNativeStub() {
  GenerateCallNativeWithWrapperStub(
    assembler,
    Address(THR,
            target::Thread::bootstrap_native_wrapper_entry_point_offset()));
}

// Input parameters:
//   S4: arguments descriptor array.
void StubCodeCompiler::GenerateCallStaticFunctionStub() {
  __ Comment("CallStaticFunctionStub");
  __ EnterStubFrame();
  // Setup space on stack for return value and preserve arguments descriptor.

  __ addiu(SP, SP, Immediate(-2 *target::kWordSize));
  __ sw(S4, Address(SP, 1 *target::kWordSize));
  __ sw(ZR, Address(SP, 0 *target::kWordSize));

  __ CallRuntime(kPatchStaticCallRuntimeEntry, 0);
  __ Comment("CallStaticFunctionStub return");

  // Get Code object result and restore arguments descriptor array.
  __ lw(CODE_REG, Address(SP, 0 *target::kWordSize));
  __ lw(S4, Address(SP, 1 *target::kWordSize));
  __ addiu(SP, SP, Immediate(2 *target::kWordSize));

  __ lw(T0, FieldAddress(CODE_REG, Code::entry_point_offset()));

  // Remove the stub frame as we are about to jump to the dart function.
  __ LeaveStubFrameAndReturn(T0);
}

// Called from a static call only when an invalid code has been entered
// (invalid because its function was optimized or deoptimized).
// S4: arguments descriptor array.
void StubCodeCompiler::GenerateFixCallersTargetStub() {
  Label monomorphic;
  __ BranchOnMonomorphicCheckedEntryJIT(&monomorphic);

  // Load code pointer to this stub from the thread:
  // The one that is passed in, is not correct - it points to the code object
  // that needs to be replaced.
  __ lw(CODE_REG,
         Address(THR, target::Thread::fix_callers_target_code_offset()));
  // Create a stub frame as we are pushing some objects on the stack before
  // calling into the runtime.
  __ EnterStubFrame();
  // Setup space on stack for return value and preserve arguments descriptor.
  __ PushRegistersInOrder({ARGS_DESC_REG, ZR});
  __ CallRuntime(kFixCallersTargetRuntimeEntry, 0);
  // Get Code object result and restore arguments descriptor array.
  __ PopRegister(CODE_REG);
  __ PopRegister(ARGS_DESC_REG);
  // Remove the stub frame.
  __ LeaveStubFrame();
  // Jump to the dart function.
  __ LoadFieldFromOffset(TMP, CODE_REG, target::Code::entry_point_offset());
  __ jr(TMP);

  __ Bind(&monomorphic);
  // Load code pointer to this stub from the thread:
  // The one that is passed in, is not correct - it points to the code object
  // that needs to be replaced.
  __ lw(CODE_REG,
         Address(THR, target::Thread::fix_callers_target_code_offset()));
  // Create a stub frame as we are pushing some objects on the stack before
  // calling into the runtime.
  __ EnterStubFrame();
  // Setup result slot, preserve receiver and
  // push old cache value (also 2nd return value).
  __ PushRegistersInOrder({ZR, A0, S5});
  __ CallRuntime(kFixCallersTargetMonomorphicRuntimeEntry, 2);
  __ PopRegister(S5);        // Get target cache object.
  __ PopRegister(A0);        // Restore receiver.
  __ PopRegister(CODE_REG);  // Get target Code object.
  // Remove the stub frame.
  __ LeaveStubFrame();
  // Jump to the dart function.
  __ LoadFieldFromOffset(
      T9, CODE_REG,
      target::Code::entry_point_offset(CodeEntryKind::kMonomorphic));
__ jr(T9);
}

// Used by eager and lazy deoptimization. Preserve result in V0 if necessary.
// This stub translates optimized frame into unoptimized frame. The optimized
// frame can contain values in registers and on stack, the unoptimized
// frame contains all values on stack.
// Deoptimization occurs in following steps:
// - Push all registers that can contain values.
// - Call C routine to copy the stack and saved registers into temporary buffer.
// - Adjust caller's frame to correct unoptimized frame size.
// - Fill the unoptimized frame.
// - Materialize objects that require allocation (e.g. Double instances).
// GC can occur only after frame is fully rewritten.
// Stack after EnterFrame(...) below:
//   +------------------+
//   | Saved PP         | <- TOS
//   +------------------+
//   | Saved CODE_REG   |
//   +------------------+
//   | Saved FP         | <- FP of stub
//   +------------------+
//   | Saved LR         |  (deoptimization point)
//   +------------------+
//   | Saved CODE_REG   |
//   +------------------+
//   | ...              | <- SP of optimized frame
//
// Parts of the code cannot GC, part of the code can GC.
static void GenerateDeoptimizationSequence(Assembler* assembler,
                                           DeoptStubKind kind) {
  const intptr_t kPushedRegistersSize =
      kNumberOfCpuRegisters *target::kWordSize + kNumberOfFRegisters *target::kWordSize;

  __ SetPrologueOffset();
  __ Comment("GenerateDeoptimizationSequence");
  // DeoptimizeCopyFrame expects a Dart frame.
  __ EnterDartFrame(kPushedRegistersSize);

  // The code in this frame may not cause GC. kDeoptimizeCopyFrameRuntimeEntry
  // and kDeoptimizeFillFrameRuntimeEntry are leaf runtime calls.
  const intptr_t saved_result_slot_from_fp =
      kFirstLocalSlotFromFp + 1 - (kNumberOfCpuRegisters - V0);
  const intptr_t saved_exception_slot_from_fp =
      kFirstLocalSlotFromFp + 1 - (kNumberOfCpuRegisters - V0);
  const intptr_t saved_stacktrace_slot_from_fp =
      kFirstLocalSlotFromFp + 1 - (kNumberOfCpuRegisters - V1);
  // Result in V0 is preserved as part of pushing all registers below.

  // Push registers in their enumeration order: lowest register number at
  // lowest address.
  for (int i = 0; i < kNumberOfCpuRegisters; i++) {
    const int slot = kNumberOfCpuRegisters - i;
    Register reg = static_cast<Register>(i);
    if (reg == CODE_REG) {
      // Save the original value of CODE_REG pushed before invoking this stub
      // instead of the value used to call this stub.
      COMPILE_ASSERT(TMP < CODE_REG);  // Assert TMP is pushed first.
      __ lw(TMP, Address(FP, kCallerSpSlotFromFp *target::kWordSize));
      __ sw(TMP, Address(SP, kPushedRegistersSize - slot *target::kWordSize));
    } else {
      __ sw(reg, Address(SP, kPushedRegistersSize - slot *target::kWordSize));
    }
  }
  for (int i = 0; i < kNumberOfFRegisters; i++) {
    // These go below the CPU registers.
    const int slot = static_cast<int>(kNumberOfCpuRegisters) + static_cast<int>(kNumberOfFRegisters) - i;
    FRegister reg = static_cast<FRegister>(i);
    __ swc1(reg, Address(SP, kPushedRegistersSize - slot *target::kWordSize));
  }

  {
    __ mov(A0, SP);  // Pass address of saved registers block.
    LeafRuntimeScope rt(assembler,
                        /*frame_size=*/0,
                        /*preserve_registers=*/false);
    bool is_lazy =
        (kind == kLazyDeoptFromReturn) || (kind == kLazyDeoptFromThrow);
    __ LoadImmediate(A1, is_lazy ? 1 : 0);
    rt.Call(kDeoptimizeCopyFrameRuntimeEntry, 2);
    // Result (V0) is stack-size (FP - SP) in bytes.
  }

  if (kind == kLazyDeoptFromReturn) {
    // Restore result into T1 temporarily.
    __ lw(T1, Address(FP, saved_result_slot_from_fp *target::kWordSize));
  } else if (kind == kLazyDeoptFromThrow) {
    // Restore result into T1 temporarily.
    __ lw(T1, Address(FP, saved_exception_slot_from_fp *target::kWordSize));
    __ lw(T2, Address(FP, saved_stacktrace_slot_from_fp *target::kWordSize));
  }

  __ RestoreCodePointer();
  __ LeaveDartFrame();
  __ subu(SP, FP, V0);

  // DeoptimizeFillFrame expects a Dart frame, i.e. EnterDartFrame(0), but there
  // is no need to set the correct PC marker or load PP, since they get patched.
  __ EnterStubFrame();

  if (kind == kLazyDeoptFromReturn) {
    __ Push(T1);  // Preserve result as first local.
  } else if (kind == kLazyDeoptFromThrow) {
    __ Push(T1);  // Preserve exception as first local.
    __ Push(T2);  // Preserve stacktrace as second local.
  }
  {
    __ mov(A0, FP);  // Get last FP address.
    LeafRuntimeScope rt(assembler,
                        /*frame_size=*/0,
                        /*preserve_registers=*/false);
    rt.Call(kDeoptimizeFillFrameRuntimeEntry, 1);
  }
  if (kind == kLazyDeoptFromReturn) {
    // Restore result into T1.
    __ lw(T1, Address(FP, kFirstLocalSlotFromFp *target::kWordSize));
  } else if (kind == kLazyDeoptFromThrow) {
    // Restore result into T1.
    __ lw(T1, Address(FP, kFirstLocalSlotFromFp *target::kWordSize));
    __ lw(T2, Address(FP, (kFirstLocalSlotFromFp - 1) *target::kWordSize));
  }
  // Code above cannot cause GC.
  __ RestoreCodePointer();
  __ LeaveStubFrame();

  // Frame is fully rewritten at this point and it is safe to perform a GC.
  // Materialize any objects that were deferred by FillFrame because they
  // require allocation.
  // Enter stub frame with loading PP. The caller's PP is not materialized yet.
  __ EnterStubFrame();
  if (kind == kLazyDeoptFromReturn) {
    __ Push(T1);  // Preserve result, it will be GC-d here.
  } else if (kind == kLazyDeoptFromThrow) {
    __ PushRegister(CODE_REG);
    __ Push(T1);  // Preserve exception, it will be GC-d here.
    __ Push(T2);  // Preserve stacktrace, it will be GC-d here.
  }
  __ PushRegister(ZR);  // Space for the result.
  __ CallRuntime(kDeoptimizeMaterializeRuntimeEntry, 0);
  // Result tells stub how many bytes to remove from the expression stack
  // of the bottom-most frame. They were used as materialization arguments.
  __ Pop(T1);
  if (kind == kLazyDeoptFromReturn) {
    __ Pop(V0);  // Restore result.
  } else if (kind == kLazyDeoptFromThrow) {
    __ Pop(V1);  // Restore stacktrace.
    __ Pop(V0);  // Restore exception.
    __ Pop(CODE_REG);
  }
  __ LeaveStubFrame();
  // Remove materialization arguments.
  __ SmiUntag(T1);
  __ addu(SP, SP, T1);
  // The caller is responsible for emitting the return instruction.

  if (kind == kLazyDeoptFromThrow) {
    // Unoptimized frame is now ready to accept the exception. Rethrow it to
    // find the right handler. Ask rethrow machinery to bypass debugger it
    // was already notified about this exception.
    __ EnterStubFrame();
    __ Push(ZR);  // Space for the return value (unused).
    __ Push(V0);               // Exception
    __ Push(V1);               // Stacktrace
    __ PushImmediate(target::ToRawSmi(1));  // Bypass debugger
    __ CallRuntime(kReThrowRuntimeEntry, 3);
    __ LeaveStubFrame();
  }
}

// Called for inline allocation of arrays.
// Input parameters:
//   RA: return address.
//   AllocateArrayABI::kLengthReg: Array length as Smi (must be preserved).
//   AllocateArrayABI::kTypeArgumentsReg: array element type (either NULL or an instantiated type).
// NOTE: AllocateArrayABI::kLengthReg cannot be clobbered here as the caller relies on it being saved.
// The newly allocated object is returned in AllocateArrayABI::kResultReg.
void StubCodeCompiler::GenerateAllocateArrayStub() {
  if (!FLAG_use_slow_path && FLAG_inline_alloc) {
    __ Comment("AllocateArrayStub");
    Label slow_case;
    // Compute the size to be allocated, it is based on the array length
    // and is computed as:
    // RoundedAllocationSize((array_length *target::kWordSize) + sizeof(RawArray)).
    __ mov(T3, AllocateArrayABI::kLengthReg);  // Array length.

    // Check that length is a positive Smi.
    __ BranchIfNotSmi(T3, &slow_case);

    // Check for maximum allowed length.
    const intptr_t max_len =
        target::ToRawSmi(target::Array::kMaxNewSpaceElements);
    __ BranchUnsignedGreater(T3, Immediate(max_len), &slow_case);

    const intptr_t cid = kArrayCid;
    NOT_IN_PRODUCT(__ MaybeTraceAllocation(cid, &slow_case, T4));

    const intptr_t fixed_size_plus_alignment_padding =
        target::Array::header_size() +
        target::ObjectAlignment::kObjectAlignment - 1;
    __ LoadImmediate(T2, fixed_size_plus_alignment_padding);
    __ sll(T3, T3, 1);  // T3 is  a Smi.
    __ addu(T2, T2, T3);
    ASSERT(kSmiTagShift == 1);
    __ AndImmediate(T2, T2, ~(target::ObjectAlignment::kObjectAlignment - 1));

    // T2: Allocation size.

    // Potential new object start.
    __ lw(AllocateArrayABI::kResultReg, Address(THR, target::Thread::top_offset()));

    __ addu(T1, AllocateArrayABI::kResultReg, T2);                        // Potential next object start.
    __ BranchUnsignedLess(T1, AllocateArrayABI::kResultReg, &slow_case);  // Branch on unsigned overflow.

    // Check if the allocation fits into the remaining space.
    // AllocateArrayABI::kResultReg: potential new object start.
    // T1: potential next object start.
    // T2: allocation size.
    __ lw(T4, Address(THR, target::Thread::end_offset()));
    __ BranchUnsignedGreaterEqual(T1, T4, &slow_case);
    __ CheckAllocationCanary(AllocateArrayABI::kResultReg);

    // Successfully allocated the object(s), now update top to point to
    // next object start and initialize the object.
    __ sw(T1, Address(THR, target::Thread::top_offset()));
    __ addiu(AllocateArrayABI::kResultReg, AllocateArrayABI::kResultReg, Immediate(kHeapObjectTag));

    // Initialize the tags.
    // AllocateArrayABI::kResultReg: new object start as a tagged pointer.
    // T1: new object end address.
    // T2: allocation size.
    {
      compiler::Label skip_shift;
      __ BranchSignedLessEqual(
        T2, Immediate(target::UntaggedObject::kSizeTagMaxSizeTag),
        &skip_shift);
      __ LoadImmediate(T5, 0);  // overflow: tag == 0
      compiler::Label done;
      __ b(&done);

      __ Bind(&skip_shift);
      __ sll(T5, T2, target::UntaggedObject::kSizeTagPos -
                              target::ObjectAlignment::kObjectAlignmentLog2);
      __ Bind(&done);

      ASSERT(cid != kIllegalCid);

      // Get the class index and insert it into the tags.
      // T2: size and bit tags.
      const uword tags =
            target::MakeTagWordForNewSpaceObject(cid, /*instance_size=*/0);
      __ LoadImmediate(TMP, tags);
      __ or_(T5, T5, TMP);
      __ InitializeHeader(T5, AllocateArrayABI::kResultReg);
    }

    // AllocateArrayABI::kResultReg: new object start as a tagged pointer.
    // T1: new object end address.
    // Store the type argument field.
    __ StoreIntoObjectNoBarrier(
      AllocateArrayABI::kResultReg, FieldAddress(AllocateArrayABI::kResultReg, Array::type_arguments_offset()), AllocateArrayABI::kTypeArgumentsReg);

    // Set the length field.
    __ StoreIntoObjectNoBarrier(AllocateArrayABI::kResultReg, FieldAddress(AllocateArrayABI::kResultReg, Array::length_offset()), AllocateArrayABI::kLengthReg);

    __ LoadObject(T7, Object::null_object());
    // Initialize all array elements to raw_null.
    // AllocateArrayABI::kResultReg: new object start as a tagged pointer.
    // T1: new object end address.
    // T2: iterator which initially points to the start of the variable
    // data area to be initialized.
    // T7: null.
    __ AddImmediate(T2, AllocateArrayABI::kResultReg, target::Array::header_size() - kHeapObjectTag);

    Label done;
    Label init_loop;
    __ Bind(&init_loop);
    __ BranchUnsignedGreaterEqual(T2, T1, &done);
    __ sw(T7, Address(T2, 0));
    __ addiu(T2, T2, Immediate(target::kWordSize));
    __ b(&init_loop);
    __ Bind(&done);

    // Safe to only check every kObjectAlignment bytes instead of each word.
    ASSERT(kAllocationRedZoneSize >= target::kObjectAlignment);
    __ AddImmediate(T2, T2, target::kObjectAlignment);
    __ BranchSignedLess(T2, T1, &init_loop);
    __ WriteAllocationCanary(T1);  // Fix overshoot.

    __ Ret();

    // Unable to allocate the array using the fast inline code, just call
    // into the runtime.
    __ Bind(&slow_case);
  }
  // Create a stub frame as we are pushing some objects on the stack before
  // calling into the runtime.
  __ EnterStubFrame();
  // Setup space on stack for return value.
  // Push array length as Smi and element type.
  __ AddImmediate(SP, SP, -3 *target::kWordSize);
  __ sw(ZR, Address(SP, 2 *target::kWordSize));
  __ sw(AllocateArrayABI::kLengthReg, Address(SP, 1 *target::kWordSize));
  __ sw(AllocateArrayABI::kTypeArgumentsReg, Address(SP, 0 *target::kWordSize));
  __ CallRuntime(kAllocateArrayRuntimeEntry, 2);

  // Write-barrier elimination might be enabled for this array (depending on the
  // array length). To be sure we will check if the allocated object is in old
  // space and if so call a leaf runtime to add it to the remembered set.
  __ lw(AllocateArrayABI::kResultReg, Address(SP, 2 * target::kWordSize));
  EnsureIsNewOrRemembered();

  __ Comment("AllocateArrayStub return");
  ASSERT(AllocateArrayABI::kResultReg == V0);
  __ lw(AllocateArrayABI::kResultReg, Address(SP, 2 *target::kWordSize));
  __ lw(AllocateArrayABI::kLengthReg, Address(SP, 1 *target::kWordSize));
  __ lw(AllocateArrayABI::kTypeArgumentsReg, Address(SP, 0 *target::kWordSize));
  __ addiu(SP, SP, Immediate(3 *target::kWordSize));

  __ LeaveStubFrameAndReturn();
}

// Called for allocation of Mint.
void StubCodeCompiler::GenerateAllocateMintSharedWithFPURegsStub() {
  // For test purpose call allocation stub without inline allocation attempt.
  if (!FLAG_use_slow_path && FLAG_inline_alloc) {
    Label slow_case;
    __ TryAllocate(compiler::MintClass(), &slow_case, Assembler::kNearJump,
                   AllocateMintABI::kResultReg, AllocateMintABI::kTempReg);
    __ Ret();

    __ Bind(&slow_case);
  }
  COMPILE_ASSERT(AllocateMintABI::kResultReg ==
                 SharedSlowPathStubABI::kResultReg);
  GenerateSharedStub(/*save_fpu_registers=*/true, &kAllocateMintRuntimeEntry,
                     target::Thread::allocate_mint_with_fpu_regs_stub_offset(),
                     /*allow_return=*/true,
                     /*store_runtime_result_in_result_register=*/true);
}

// Called for allocation of Mint.
void StubCodeCompiler::GenerateAllocateMintSharedWithoutFPURegsStub() {
  // For test purpose call allocation stub without inline allocation attempt.
  if (!FLAG_use_slow_path && FLAG_inline_alloc) {
    Label slow_case;
    __ TryAllocate(compiler::MintClass(), &slow_case, Assembler::kNearJump,
                   AllocateMintABI::kResultReg, AllocateMintABI::kTempReg);
    __ Ret();

    __ Bind(&slow_case);
  }
  COMPILE_ASSERT(AllocateMintABI::kResultReg ==
                 SharedSlowPathStubABI::kResultReg);
  GenerateSharedStub(
      /*save_fpu_registers=*/false, &kAllocateMintRuntimeEntry,
      target::Thread::allocate_mint_without_fpu_regs_stub_offset(),
      /*allow_return=*/true,
      /*store_runtime_result_in_result_register=*/true);
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

// Called when invoking compiled Dart code from interpreted Dart code.
// Input parameters:
//   RA : points to return address.
//   A0 : target code or entry point (in AOT mode).
//   A1 : arguments descriptor array.
//   A2 : address of first argument.
//   A3 : current thread.
void StubCodeCompiler::GenerateInvokeDartCodeFromBytecodeStub() {
  __ Stop("Not implemented on MIPS.");
}

// Called for inline allocation of objects.
// Input parameters:
//   RA : return address.
//   SP + 0 : type arguments object (only if class is parameterized).
void StubCodeCompiler::GenerateAllocationStubForClass(
    UnresolvedPcRelativeCalls* unresolved_calls,
    const Class& cls,
    const Code& allocate_object,
    const Code& allocat_object_parametrized) {
  __ Comment("AllocationStubForClass");

  classid_t cls_id = target::Class::GetId(cls);
  ASSERT(cls_id != kIllegalCid);

  // The generated code is different if the class is parameterized.
  const bool is_cls_parameterized = target::Class::NumTypeArguments(cls) > 0;
  ASSERT(!is_cls_parameterized ||
         (target::Class::TypeArgumentsFieldOffset(cls) !=
          target::Class::kNoTypeArguments));
  const intptr_t instance_size = target::Class::GetInstanceSize(cls);
  ASSERT(instance_size > 0);

  const uword tags =
      target::MakeTagWordForNewSpaceObject(cls_id, instance_size);

  const Register kTagsReg = AllocateObjectABI::kTagsReg;

  __ LoadImmediate(kTagsReg, tags);

  if (!FLAG_use_slow_path && FLAG_inline_alloc &&
      !target::Class::TraceAllocation(cls) &&
      target::SizeFitsInSizeTag(instance_size)) {
    RELEASE_ASSERT(AllocateObjectInstr::WillAllocateNewOrRemembered(cls));
    RELEASE_ASSERT(target::Heap::IsAllocatableInNewSpace(instance_size));

    if (is_cls_parameterized) {
      if (!IsSameObject(NullObject(),
                        CastHandle<Object>(allocat_object_parametrized))) {
        __ GenerateUnRelocatedPcRelativeTailCall();
        unresolved_calls->Add(new UnresolvedPcRelativeCall(
            __ CodeSize(), allocat_object_parametrized, /*is_tail_call=*/true));
      } else {
        __ lw(T9,
              Address(THR,
                      target::Thread::
                          allocate_object_parameterized_entry_point_offset()));
        __ jr(T9);
      }
    } else {
      if (!IsSameObject(NullObject(), CastHandle<Object>(allocate_object))) {
        __ GenerateUnRelocatedPcRelativeTailCall();
        unresolved_calls->Add(new UnresolvedPcRelativeCall(
            __ CodeSize(), allocate_object, /*is_tail_call=*/true));
      } else {
        __ lw(
            T9,
            Address(THR, target::Thread::allocate_object_entry_point_offset()));
        __ jr(T9);
      }
    }
  } else {
    if (!is_cls_parameterized) {
      __ LoadObject(AllocateObjectABI::kTypeArgumentsReg, NullObject());
    }
    __ lw(T9,
          Address(THR,
                  target::Thread::allocate_object_slow_entry_point_offset()));
    __ jr(T9);
  }
}

// Helper to generate space allocation of context stub.
// This does not initialise the fields of the context.
// Input:
//   T1: number of context variables.
// Output:
//   V0: new allocated Context object.
// Clobbered:
//   T2, T3, T4
static void GenerateAllocateContext(Assembler* assembler, Label* slow_case) {
  // First compute the rounded instance size.
  // T1: number of context variables.
  const intptr_t fixed_size_plus_alignment_padding =
      target::Context::header_size() +
      target::ObjectAlignment::kObjectAlignment - 1;

  __ sll(T2, T1, 2);
  ASSERT(kSmiTagShift == 1);
  __ AddImmediate(T2, fixed_size_plus_alignment_padding);
  __ AndImmediate(T2, T2, ~(target::ObjectAlignment::kObjectAlignment - 1));

  NOT_IN_PRODUCT(__ MaybeTraceAllocation(kContextCid, slow_case, T4));
  // Now allocate the object.
  // T1: number of context variables.
  // T2: object size.
  __ lw(V0, Address(THR, target::Thread::top_offset()));
  __ addu(T3, T2, V0);
  // Check if the allocation fits into the remaining space.
  // V0: potential new object.
  // T1: number of context variables.
  // T2: object size.
  // T3: potential next object start.
  __ lw(T4, Address(THR, target::Thread::end_offset()));
  __ BranchUnsignedGreaterEqual(T3, T4, slow_case);
  __ CheckAllocationCanary(V0);

  // Successfully allocated the object, now update top to point to
  // next object start and initialize the object.
  // V0: new object start (untagged).
  // T1: number of context variables.
  // T2: object size.
  // T3: next object start.
  __ sw(T3, Address(THR, target::Thread::top_offset()));
  __ AddImmediate(V0, V0, kHeapObjectTag);

  // Calculate the size tag.
  // V0: new object (tagged).
  // T1: number of context variables.
  // T2: object size.
  // T3: next object start.
  const intptr_t shift = target::UntaggedObject::kSizeTagPos -
                         target::ObjectAlignment::kObjectAlignmentLog2;
  __ LoadImmediate(T3, 0);
  // If no size tag overflow, shift T2 left, else set T2 to zero.
  Label zero_tag;
  __ BranchSignedGreater(
      T2, compiler::Immediate(target::UntaggedObject::kSizeTagMaxSizeTag),
      &zero_tag);
  __ sll(T3, T2, shift);
  __ Bind(&zero_tag);

  // Get the class index and insert it into the tags.
  // T3: size and bit tags.
  const uword tags =
      target::MakeTagWordForNewSpaceObject(kContextCid, /*instance_size=*/0);

  __ OrImmediate(T3, T3, tags);
  __ InitializeHeader(T3, V0);

  // Setup up number of context variables field.
  // V0: new object.
  // T1: number of context variables as integer value (not object).
  // T2: object size.
  // T3: next object start.
  __ sw(T1, FieldAddress(V0, target::Context::num_variables_offset()));
}

// Called for inline allocation of contexts.
// Input:
//   T1: number of context variables.
// Output:
//   V0: new allocated RawContext object.
void StubCodeCompiler::GenerateAllocateContextStub() {
  if (!FLAG_use_slow_path && FLAG_inline_alloc) {
    Label slow_case;

    GenerateAllocateContext(assembler, &slow_case);

    // Setup the parent field.
    // V0: new object.
    // T1: number of context variables.
    __ LoadObject(T7, NullObject());
    __ StoreIntoObjectOffset(V0, target::Context::parent_offset(),
                                       T7);

    // Initialize the context variables.
    // V0: new object.
    // T1: number of context variables.
    {
      Label loop, done;
      __ AddImmediate(T3, V0,
                      target::Context::variable_offset(0) - kHeapObjectTag);
      __ Bind(&loop);
      __ AddImmediate(T1, T1, -1);
      __ bltz(T1, &done);
      __ LoadObject(TMP, NullObject());
      __ sw(TMP, Address(T3, 0));
      __ AddImmediate(T3, T3, target::kWordSize);
      __ b(&loop);
      __ Bind(&done);
    }

    // Done allocating and initializing the context.
    // V0: new object.
    __ Ret();

    __ Bind(&slow_case);
  }

  // Create a stub frame as we are pushing some objects on the stack before
  // calling into the runtime.
  __ EnterStubFrame();
  // Setup space on stack for return value.
  __ SmiTag(T1);
  __ PushObject(NullObject());
  __ PushRegister(T1);
  __ CallRuntime(kAllocateContextRuntimeEntry, 1);  // Allocate context.
  __ Drop(1);          // Pop number of context variables argument.
  __ PopRegister(V0);  // Pop the new context object.

  // Write-barrier elimination might be enabled for this context (depending on
  // the size). To be sure we will check if the allocated object is in old
  // space and if so call a leaf runtime to add it to the remembered set.
  EnsureIsNewOrRemembered();

  // V0: new object
  // Restore the frame pointer.
  __ LeaveStubFrame();
  __ Ret();
}

// Called for clone of contexts.
// Input:
//   T5: context variable to clone.
// Output:
//   V0: new allocated Context object.
// Clobbered:
//   Potentially any since it can go to runtime.
void StubCodeCompiler::GenerateCloneContextStub() {
  if (!FLAG_use_slow_path && FLAG_inline_alloc) {
    Label slow_case;

    // Load num. variable in the existing context.
    __ lw(T1, FieldAddress(T5, target::Context::num_variables_offset()));

    GenerateAllocateContext(assembler, &slow_case);

    // Load parent in the existing context.
    __ lw(T2, FieldAddress(T5, target::Context::parent_offset()));
    // Setup the parent field.
    // V0: new object.
    __ StoreIntoObjectNoBarrier(
        V0, FieldAddress(V0, target::Context::parent_offset()), T2);

    // Clone the context variables.
    // V0: new object.
    // T1: number of context variables.
    {
      Label loop, done;
      __ AddImmediate(T2, V0,
                      target::Context::variable_offset(0) - kHeapObjectTag);
      __ AddImmediate(T3, T5,
                      target::Context::variable_offset(0) - kHeapObjectTag);

      __ Bind(&loop);
      __ AddImmediate(T1, T1, -1);
      __ BranchSignedLess(T1, ZR, &done);

      __ lw(T5, Address(T3, 0));
      __ addi(T3, T3, Immediate(target::kWordSize));
      __ sw(T5, Address(T2, 0));
      __ addi(T2, T2, Immediate(target::kWordSize));
      __ b(&loop);

      __ Bind(&done);
    }

    // Done allocating and initializing the context.
    // V0: new object.
    __ Ret();

    __ Bind(&slow_case);
  }

  // Create a stub frame as we are pushing some objects on the stack before
  // calling into the runtime.
  __ EnterStubFrame();
  // Setup space on stack for return value.
  __ PushRegisterPair(T5, ZR);
  __ CallRuntime(kCloneContextRuntimeEntry, 1);  // Clone context.
  // T5: Pop number of context variables argument.
  // V0: Pop the new context object.
  __ PopRegisterPair(T5, V0);

  // Write-barrier elimination might be enabled for this context (depending on
  // the size). To be sure we will check if the allocated object is in old
  // space and if so call a leaf runtime to add it to the remembered set.
  EnsureIsNewOrRemembered();

  // V0: new object
  // Restore the frame pointer.
  __ LeaveStubFrame();
  __ Ret();
}

void StubCodeCompiler::GenerateWriteBarrierWrappersStub() {
  for (intptr_t i = 0; i < kNumberOfCpuRegisters; ++i) {
    if ((kDartAvailableCpuRegs & (1 << i)) == 0) continue;

    Register reg = static_cast<Register>(i);
    intptr_t start = __ CodeSize();
    __ AddImmediate(SP, SP, -2 * target::kWordSize);
    __ sw(RA, Address(SP, 1 * target::kWordSize));
    __ sw(kWriteBarrierObjectReg, Address(SP, 0 * target::kWordSize));
    __ mov(kWriteBarrierObjectReg, reg);
    __ Call(Address(THR, target::Thread::write_barrier_entry_point_offset()));
    __ lw(kWriteBarrierObjectReg, Address(SP, 0 * target::kWordSize));
    __ lw(RA, Address(SP, 1 * target::kWordSize));
    __ AddImmediate(SP, SP, 2 * target::kWordSize);
    __ Ret();  // Return.
    intptr_t end = __ CodeSize();
    ASSERT_EQUAL(end - start, kStoreBufferWrapperSize);
  }
}

// Helper stub to implement Assembler::StoreIntoObject/Array.
// Input parameters:
//   A0: Object (old)
//   A1: Value (old or new)
//   S5: Slot
// If A1 is new, add A0 to the store buffer. Otherwise A1 is old, mark A1
// and add it to the mark list.
COMPILE_ASSERT(kWriteBarrierObjectReg == A0);
COMPILE_ASSERT(kWriteBarrierValueReg == A1);
COMPILE_ASSERT(kWriteBarrierSlotReg == S5);
static void GenerateWriteBarrierStubHelper(Assembler* assembler, bool cards) {
  RegisterSet spill_set((1 << T2) | (1 << T3) | (1 << T4), 0);

  Label skip_marking;
  __ PushRegister(T9);
  __ lbu(TMP, FieldAddress(A1, target::Object::tags_offset()));
  __ lbu(T9, Address(THR, target::Thread::write_barrier_mask_offset()));
  __ and_(TMP, TMP, T9);
  __ PopRegister(T9);
  __ andi(TMP, TMP,
          compiler::Immediate(target::UntaggedObject::kIncrementalBarrierMask));
  __ beq(TMP, ZR, &skip_marking);

  {
    // Atomically clear kNotMarkedBit.
    Label retry, is_new, done;
    __ PushRegisters(spill_set);
    __ AddImmediate(T3, A1, target::Object::tags_offset() - kHeapObjectTag);
    // T3: Untagged address of header word.
    __ Bind(&retry);
    __ ll(T2, Address(T3, 0));
    __ AndImmediate(T4, T2, 1 << target::UntaggedObject::kNotMarkedBit);
    __ BranchEqual(T4, ZR, &done);  // Marked by another thread.
    __ AndImmediate(T2, T2, ~(1 << target::UntaggedObject::kNotMarkedBit));
    __ sc(T2, Address(T3, 0));
    // T2 = 1 on success, 0 on failure.
    __ beq(T2, ZR, &retry);

    __ AndImmediate(T2, A1,
                    1 << target::ObjectAlignment::kNewObjectBitPosition);
    __ bne(T2, ZR, &is_new);
    auto mark_stack_push = [&](intptr_t offset, const RuntimeEntry& entry) {
      __ lw(T4, Address(THR, offset));
      __ lw(T2, Address(T4, target::MarkingStackBlock::top_offset()));
      __ sll(T3, T2, target::kWordSizeLog2);
      __ addu(T3, T4, T3);
      __ sw(A1, Address(T3, target::MarkingStackBlock::pointers_offset()));
      __ AddImmediate(T2, T2, 1);
      __ sw(T2, Address(T4, target::MarkingStackBlock::top_offset()));
      __ BranchNotEqual(
          T2, compiler::Immediate(target::MarkingStackBlock::kSize), &done);

      {
        LeafRuntimeScope rt(assembler, /*frame_size=*/0,
                            /*preserve_registers=*/true);
        __ mov(A0, THR);
        rt.Call(entry, /*argument_count=*/1);
      }
    };
    mark_stack_push(target::Thread::old_marking_stack_block_offset(),
                    kOldMarkingStackBlockProcessRuntimeEntry);
    __ b(&done);

    __ Bind(&is_new);
    mark_stack_push(target::Thread::new_marking_stack_block_offset(),
                    kNewMarkingStackBlockProcessRuntimeEntry);

    __ Bind(&done);
    __ PopRegisters(spill_set);
  }

  Label add_to_remembered_set, remember_card;
  __ Bind(&skip_marking);
  __ PushRegister(T9);
  __ lbu(TMP, FieldAddress(A0, target::Object::tags_offset()));
  __ lbu(T9, FieldAddress(A1, target::Object::tags_offset()));
  __ srl(TMP, TMP, target::UntaggedObject::kBarrierOverlapShift);
  __ and_(TMP, T9, TMP);
  __ LoadImmediate(T9, target::UntaggedObject::kGenerationalBarrierMask);
  __ and_(TMP, TMP, T9);
  __ PopRegister(T9);
  __ bne(TMP, ZR, &add_to_remembered_set);
  __ Ret();

  __ Bind(&add_to_remembered_set);
  if (cards) {
    __ lbu(TMP, FieldAddress(A0, target::Object::tags_offset()));
    __ AndImmediate(TMP, TMP, 1 << target::UntaggedObject::kCardRememberedBit);
    __ bne(TMP, ZR, &remember_card);
  } else {
#if defined(DEBUG)
    Label ok;
    __ lbu(TMP, FieldAddress(A0, target::Object::tags_offset()));
    __ AndImmediate(TMP, TMP, 1 << target::UntaggedObject::kCardRememberedBit);
    __ beq(TMP, ZR, &ok);
    __ Stop("Wrong barrier!");
    __ Bind(&ok);
#endif
  }
  {
    // Atomically clear kOldAndNotRememberedBit.
    Label retry, done;
    __ PushRegisters(spill_set);
    __ AddImmediate(T3, A0, target::Object::tags_offset() - kHeapObjectTag);
    // T3: Untagged address of header word.
    __ Bind(&retry);
    __ ll(T2, Address(T3, 0));
    __ AndImmediate(T4, T2,
                    1 << target::UntaggedObject::kOldAndNotRememberedBit);
    __ BranchEqual(T4, ZR, &done);  // Marked by another thread.
    __ AndImmediate(T2, T2,
                    ~(1 << target::UntaggedObject::kOldAndNotRememberedBit));
    __ sc(T2, Address(T3, 0));
    // T2 = 1 on success, 0 on failure.
    __ beq(T2, ZR, &retry);

    // Load the StoreBuffer block out of the thread. Then load top_ out of the
    // StoreBufferBlock and add the address to the pointers_.
    __ lw(T4, Address(THR, target::Thread::store_buffer_block_offset()));
    __ lw(T2, Address(T4, target::StoreBufferBlock::top_offset()));
    __ sll(T3, T2, target::kWordSizeLog2);
    __ addu(T3, T4, T3);
    __ sw(A0, Address(T3, target::StoreBufferBlock::pointers_offset()));

    // Increment top_ and check for overflow.
    // T2: top_.
    // T4: StoreBufferBlock.
    __ AddImmediate(T2, T2, 1);
    __ sw(T2, Address(T4, target::StoreBufferBlock::top_offset()));
    __ BranchNotEqual(T2, compiler::Immediate(target::StoreBufferBlock::kSize),
                      &done);

    {
      LeafRuntimeScope rt(assembler, /*frame_size=*/0,
                          /*preserve_registers=*/true);
      __ mov(A0, THR);
      rt.Call(kStoreBufferBlockProcessRuntimeEntry, /*argument_count=*/1);
    }

    __ Bind(&done);
    __ PopRegisters(spill_set);
    __ Ret();
  }
  if (cards) {
    RegisterSet spill_set2((1 << A0) | (1 << A1) | (1 << A2), 0);
    Label retry;

    // Get card table.
    __ Bind(&remember_card);
    __ AndImmediate(TMP, A0, target::Page::kPageMask);                 // Page.
    __ lw(TMP, Address(TMP, target::Page::card_table_offset()));  // Card table.

    // Atomically dirty the card.     // Page.
    __ PushRegisters(spill_set2);
    __ AndImmediate(TMP, A0, target::Page::kPageMask);  // Page.
    __ subu(S5, S5, TMP);                              // Offset in page.
    __ srl(S5, S5, target::Page::kBytesPerCardLog2);  // Card index.
    __ andi(A0, S5, Immediate(target::kBitsPerWord - 1));
    __ LoadImmediate(A1, 1);
    __ sllv(A1, A1, A0);
    __ lw(TMP, Address(TMP, target::Page::card_table_offset()));  // Card table.
    __ srl(S5, S5, target::kBitsPerWordLog2);  // Word index.
    __ sll(A2, S5, target::kWordSizeLog2);
    __ addu(TMP, TMP, A2);  // Word address.
    __ Bind(&retry);
    __ ll(A0, Address(TMP, 0));
    __ or_(A0, A0, A1);
    __ sc(A0, Address(TMP, 0));
    __ beq(A0, ZR, &retry);
    __ PopRegisters(spill_set2);
    __ Ret();
  }
}

void StubCodeCompiler::GenerateWriteBarrierStub() {
  GenerateWriteBarrierStubHelper(assembler, false);
}

void StubCodeCompiler::GenerateArrayWriteBarrierStub() {
  GenerateWriteBarrierStubHelper(assembler, true);
}

static void GenerateAllocateObjectHelper(Assembler* assembler,
                                         bool is_cls_parameterized) {
  const Register kTagsReg = AllocateObjectABI::kTagsReg;

  {
    Label slow_case;

#if !defined(PRODUCT)
    {
      const Register kCidRegister = T6;
      __ ExtractClassIdFromTags(kCidRegister, AllocateObjectABI::kTagsReg);
      __ MaybeTraceAllocation(kCidRegister, &slow_case, TMP);
    }
#endif

    const Register kNewTopReg = T3;

    // Bump allocation.
    {
      const Register kInstanceSizeReg = T4;
      const Register kEndReg = T5;

      __ ExtractInstanceSizeFromTags(kInstanceSizeReg, kTagsReg);

      // Load two words from Thread::top: top and end.
      // AllocateObjectABI::kResultReg: potential next object start.
      __ lw(AllocateObjectABI::kResultReg,
            Address(THR, target::Thread::top_offset()));
      __ lw(kEndReg, Address(THR, target::Thread::end_offset()));

      __ addu(kNewTopReg, AllocateObjectABI::kResultReg, kInstanceSizeReg);
      __ BranchUnsignedLessEqual(kEndReg, kNewTopReg, &slow_case);
      __ CheckAllocationCanary(AllocateObjectABI::kResultReg);

      // Successfully allocated the object, now update top to point to
      // next object start and store the class in the class field of object.
      __ sw(kNewTopReg, Address(THR, target::Thread::top_offset()));
    }  //  kEndReg = T5, kInstanceSizeReg = T4

    // Tags.
    __ InitializeHeaderUntagged(kTagsReg, AllocateObjectABI::kResultReg);

    // Initialize the remaining words of the object.
    {
      const Register kFieldReg = T4;
      const Register kNullReg = T7;

      __ LoadObject(kNullReg, NullObject());

      __ AddImmediate(kFieldReg, AllocateObjectABI::kResultReg,
                      target::Instance::first_field_offset());
      Label loop;
      __ Bind(&loop);
      for (intptr_t offset = 0; offset < target::kObjectAlignment;
            offset += target::kCompressedWordSize) {
        __ StoreCompressedIntoObjectNoBarrier(AllocateObjectABI::kResultReg,
                                              Address(kFieldReg, offset),
                                              T7);
      }
      // Safe to only check every kObjectAlignment bytes instead of each word.
      ASSERT(kAllocationRedZoneSize >= target::kObjectAlignment);
      __ AddImmediate(kFieldReg, kFieldReg, target::kObjectAlignment);
      __ BranchUnsignedLess(kFieldReg, kNewTopReg, &loop);
      __ WriteAllocationCanary(kNewTopReg);  // Fix overshoot.
    }  // kFieldReg = T4, kNullReg = T7

    __ AddImmediate(AllocateObjectABI::kResultReg,
                    AllocateObjectABI::kResultReg, kHeapObjectTag);

    // Store parameterized type.
    if (is_cls_parameterized) {
      Label not_parameterized_case;

      const Register kClsIdReg = T4;
      const Register kTypeOffsetReg = T5;

      __ ExtractClassIdFromTags(kClsIdReg, kTagsReg);

      // Load class' type_arguments_field offset in words.
      __ LoadClassById(kTypeOffsetReg, kClsIdReg);
      __ lw(
          kTypeOffsetReg,
          FieldAddress(kTypeOffsetReg,
                       target::Class::
                           host_type_arguments_field_offset_in_words_offset()));

      // Set the type arguments in the new object.
      __ AddShifted(kTypeOffsetReg, AllocateObjectABI::kResultReg,
                    kTypeOffsetReg, target::kWordSizeLog2);
      __ StoreIntoObjectNoBarrier(AllocateObjectABI::kResultReg,
                                  FieldAddress(kTypeOffsetReg, 0),
                                  AllocateObjectABI::kTypeArgumentsReg);

      __ Bind(&not_parameterized_case);
    }  // kClsIdReg = T4, kTypeOffsetReg = T5

    __ Ret();

    __ Bind(&slow_case);
  }  // kNewTopReg = T3

  // Fall back on slow case:
  {
    const Register kStubReg = T9;

    if (!is_cls_parameterized) {
      __ LoadObject(AllocateObjectABI::kTypeArgumentsReg, NullObject());
    }

    // Tail call to generic allocation stub.
    __ lw(kStubReg,
           Address(THR,
                   target::Thread::allocate_object_slow_entry_point_offset()));
    __ jr(kStubReg);
  }  // kStubReg = T9
}

// Called for inline allocation of objects (any class).
void StubCodeCompiler::GenerateAllocateObjectStub() {
  GenerateAllocateObjectHelper(assembler, /*is_cls_parameterized=*/false);
}

void StubCodeCompiler::GenerateAllocateObjectParameterizedStub() {
  GenerateAllocateObjectHelper(assembler, /*is_cls_parameterized=*/true);
}

void StubCodeCompiler::GenerateAllocateObjectSlowStub() {
  const Register kClsReg = A0;

  if (!FLAG_precompiled_mode) {
    __ lw(CODE_REG,
           Address(THR, target::Thread::call_to_runtime_stub_offset()));
  }

  // Create a stub frame as we are pushing some objects on the stack before
  // calling into the runtime.
  __ EnterStubFrame();

  __ ExtractClassIdFromTags(AllocateObjectABI::kResultReg,
                            AllocateObjectABI::kTagsReg);
  __ LoadClassById(kClsReg, AllocateObjectABI::kResultReg);

  __ LoadObject(AllocateObjectABI::kResultReg, NullObject());

  __ addiu(SP, SP, Immediate(-3 * target::kWordSize));
  __ sw(AllocateObjectABI::kResultReg, Address(SP, 2 * target::kWordSize));       // Result slot.
  __ sw(kClsReg, Address(SP, 1 * target::kWordSize));  // Arg0: Class object.
  __ sw(AllocateObjectABI::kTypeArgumentsReg,
        Address(SP, 0 * target::kWordSize));  // Arg1: Type args or null.
  __ CallRuntime(kAllocateObjectRuntimeEntry, 2);

  // Load result off the stack into result register.
  __ lw(AllocateObjectABI::kResultReg, Address(SP, 2 * target::kWordSize));
  __ addiu(SP, SP, Immediate(3 * target::kWordSize));

  // Write-barrier elimination is enabled for [cls] and we therefore need to
  // ensure that the object is in new-space or has remembered bit set.
  EnsureIsNewOrRemembered();

  __ LeaveDartFrameAndReturn();
}

// Stub for compiling a function and jumping to the compiled code.
// S5/ICREG: IC-Data (for methods).
// S4/ARGS_DESC_REG: Arguments descriptor.
// T0/FUNCTION_REG: Function.
void StubCodeCompiler::GenerateLazyCompileStub() {
  __ EnterStubFrame();
  __ addiu(SP, SP, Immediate(-3 *target::kWordSize));
  __ sw(ICREG, Address(SP, 2 *target::kWordSize));  // Preserve IC data object.
  __ sw(ARGS_DESC_REG, Address(SP, 1 *target::kWordSize));  // Preserve args descriptor array.
  __ sw(FUNCTION_REG, Address(SP, 0 *target::kWordSize));  // Pass function.
  __ CallRuntime(kCompileFunctionRuntimeEntry, 1);
  __ lw(FUNCTION_REG, Address(SP, 0 *target::kWordSize));  // Restore function.
  __ lw(ARGS_DESC_REG, Address(SP, 1 *target::kWordSize));  // Restore args descriptor array.
  __ lw(ICREG, Address(SP, 2 *target::kWordSize));  // Restore IC data array.
  __ addiu(SP, SP, Immediate(3 *target::kWordSize));
  __ LeaveStubFrame();

  __ lw(CODE_REG, FieldAddress(FUNCTION_REG, target::Function::code_offset()));
  __ lw(T2, FieldAddress(FUNCTION_REG, target::Function::entry_point_offset()));
  __ jr(T2);
}

// Stub for interpreting a function call.
// S4/ARGS_DESC_REG: Arguments descriptor.
// T0/FUNCTION_REG: Function.
void StubCodeCompiler::GenerateInterpretCallStub() {
#if defined(DART_DYNAMIC_MODULES)

  __ EnterStubFrame();

#if defined(DEBUG)
  {
    Label ok;
    // Check that we are always entering from Dart code.
    __ LoadFromOffset(CMPRES1, THR, target::Thread::vm_tag_offset());
    __ BranchEqual(CMPRES1, compiler::Immediate(VMTag::kDartTagId), &ok);
    __ Stop("Not coming from Dart code.");
    __ Bind(&ok);
  }
#endif

  // Adjust arguments count for type arguments vector.
  __ LoadFieldFromOffset(A2, ARGS_DESC_REG, target::ArgumentsDescriptor::count_offset());
  __ SmiUntag(A2);
  __ LoadFieldFromOffset(T1, ARGS_DESC_REG, target::ArgumentsDescriptor::type_args_len_offset());
  Label args_count_ok;
  __ BranchEqual(T1, Immediate(0), &args_count_ok);
  __ AddImmediate(A2, A2, 1);  // Include the type arguments.
  __ Bind(&args_count_ok);

  // Compute argv.
  __ sll(A3, A2, 2);
  __ addu(A3, A3, FP);
  __ AddImmediate(A3, A3, target::frame_layout.param_end_from_fp * target::kWordSize);

  // Indicate decreasing memory addresses of arguments with negative argc.
  __ subu(A2, ZR, A2);

  // Align frame before entering C++ world. Fifth argument passed on the stack.
  __ ReserveAlignedFrameSpace(1 * target::kWordSize);

  // Pass arguments in registers.
  __ mov(A0, FUNCTION_REG);   // T0/FUNCTION_REG: Function.
  __ mov(A1, ARGS_DESC_REG);  // Arguments descriptor.
  // A2: Negative argc.
  // A3: Argv.
  __ sw(THR, Address(SP, 0));  // Fifth argument: Thread.

  // Save exit frame information to enable stack walking as we are about
  // to transition to Dart VM C++ code.
  __ StoreToOffset(FP, THR,
                   target::Thread::top_exit_frame_info_offset());

  // Mark that the thread exited generated code through a runtime call.
  __ LoadImmediate(T5, target::Thread::exit_through_runtime_call());
  __ StoreToOffset(T5, THR, target::Thread::exit_through_ffi_offset());

  // Mark that the thread is executing VM code.
  __ LoadFromOffset(T5, THR,
                    target::Thread::interpret_call_entry_point_offset());
  __ StoreToOffset(T5, THR, target::Thread::vm_tag_offset());

  __ mov(T9, T5);
  __ Call(T9);

  // Mark that the thread is executing Dart code.
  __ LoadImmediate(T2, VMTag::kDartTagId);
  __ StoreToOffset(T2, THR, target::Thread::vm_tag_offset());

  // Mark that the thread has not exited generated Dart code.
  __ LoadImmediate(T2, 0);
  __ StoreToOffset(T2, THR, target::Thread::exit_through_ffi_offset());

  // Reset exit frame information in Isolate's mutator thread structure.
  __ StoreToOffset(T2, THR,
                   target::Thread::top_exit_frame_info_offset());

  __ LeaveStubFrame();
  __ Ret();

#else
  __ Stop("Not using Dart dynamic modules");
#endif  // defined(DART_DYNAMIC_MODULES)
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

// Return the current stack pointer address, used to stack alignment
// checks.
void StubCodeCompiler::GenerateGetCStackPointerStub() {
  __ Ret();
  __ delay_slot()->mov(V0, SP);
}

// Jump to the exception or error handler.
// RA: return address.
// A0: program_counter.
// A1: stack_pointer.
// A2: frame_pointer.
// A3: thread.
// Does not return.
void StubCodeCompiler::GenerateJumpToFrameStub() {
  ASSERT(kExceptionObjectReg == V0);
  ASSERT(kStackTraceObjectReg == V1);
  COMPILE_ASSERT(IsAbiPreservedRegister(CALLEE_SAVED_TEMP));
  COMPILE_ASSERT(IsAbiPreservedRegister(THR));
  __ mov (CALLEE_SAVED_TEMP, A0);
  __ mov(SP, A1);    // Stack pointer.
  __ mov(FP, A2);   // Frame_pointer.
  __ mov(THR, A3);  // Thread.
#if defined(DART_TARGET_OS_FUCHSIA) || defined(DART_TARGET_OS_ANDROID)
#error Unimplmented
#elif defined(USING_SHADOW_CALL_STACK)
#error Unimplemented
#endif

  Label exit_through_non_ffi;
  Register tmp1 = A0, tmp2 = A1;
  // Check if we exited generated from FFI. If so do transition - this is needed
  // because normally runtime calls transition back to generated via destructor
  // of TransitionGeneratedToVM/Native that is part of runtime boilerplate
  // code (see DEFINE_RUNTIME_ENTRY_IMPL in runtime_entry.h). Ffi calls don't
  // have this boilerplate, don't have this stack resource, have to transition
  // explicitly.
  __ LoadFromOffset(tmp1, THR,
      compiler::target::Thread::exit_through_ffi_offset());
  __ LoadImmediate(tmp2, target::Thread::exit_through_ffi());
  __ bne(tmp1, tmp2, &exit_through_non_ffi);
  __ TransitionNativeToGenerated(tmp1, tmp2,
                  /*leave_safepoint=*/true,
                  /*ignore_unwind_in_progress=*/true);
  __ Bind(&exit_through_non_ffi);

  // Set tag.
  __ LoadImmediate(TMP, VMTag::kDartTagId);
  __ sw(TMP, Assembler::VMTagAddress());
  // Clear top exit frame.
  __ sw(ZR, Address(THR, Thread::top_exit_frame_info_offset()));
  // Restore pool pointer.
  __ RestoreCodePointer();
  if (FLAG_precompiled_mode) {
    __ lw(PP, Address(THR, target::Thread::global_object_pool_offset()));
    __ set_constant_pool_allowed(true);
  } else {
    __ LoadPoolPointer();
  }
  __ jr(CALLEE_SAVED_TEMP);                     // Jump to the program counter.
}

// Run an exception handler.  Execution comes from JumpToFrame
// stub or from the simulator.
// The arguments are stored in the Thread object.
// Does not return.
void StubCodeCompiler::GenerateRunExceptionHandlerStub() {
  __ lw(A0, Address(THR, Thread::resume_pc_offset()));

  word offset_from_thread = 0;
  bool ok = target::CanLoadFromThread(NullObject(), &offset_from_thread);
  ASSERT(ok);
  __ LoadFromOffset(A2, THR, offset_from_thread);

  ASSERT(kExceptionObjectReg == V0);
  // Load the exception from the current thread.
  Address exception_addr(THR, Thread::active_exception_offset());
  __ lw(V0, exception_addr);
  __ sw(A2, exception_addr);

  ASSERT(kStackTraceObjectReg == V1);
  // Load the stacktrace from the current thread.
  Address stacktrace_addr(THR, Thread::active_stacktrace_offset());
  __ lw(V1, stacktrace_addr);

  __ jr(A0);  // Jump to continuation point.
  __ delay_slot()->sw(A2, stacktrace_addr);
}

// Deoptimize a frame on the call stack before rewinding.
// The arguments are stored in the Thread object.
// No result.
void StubCodeCompiler::GenerateDeoptForRewindStub() {
  // Push zap value instead of CODE_REG.
  __ LoadImmediate(TMP, kZapCodeReg);
  __ Push(TMP);

  // Load the deopt pc into RA.
  __ lw(RA, Address(THR, Thread::resume_pc_offset()));
  GenerateDeoptimizationSequence(assembler, kEagerDeopt);

  // After we have deoptimized, jump to the correct frame.
  __ EnterStubFrame();
  __ CallRuntime(kRewindPostDeoptRuntimeEntry, 0);
  __ LeaveStubFrame();
  __ break_(0);
}

}  // namespace compiler
}  // namespace dart

#endif  // defined TARGET_ARCH_MIPS

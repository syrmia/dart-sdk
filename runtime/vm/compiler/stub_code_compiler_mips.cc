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
    __ EnterFrame(0);
    __ ReserveAlignedFrameSpace(4 * target::kWordSize);

    GenerateLoadFfiCallbackMetadataRuntimeFunction(
        FfiCallbackMetadata::kExitTemporaryIsolate, T1);
        
    __ mov(T9, T1);
    __ jalr(T9);

    __ LeaveFrame();
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

}  // namespace compiler
}  // namespace dart

#endif  // defined TARGET_ARCH_MIPS

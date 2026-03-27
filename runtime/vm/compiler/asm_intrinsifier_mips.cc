// Copyright (c) 2026, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"  // Needed here to get TARGET_ARCH_MIPS.
#if defined(TARGET_ARCH_MIPS)

#define SHOULD_NOT_INCLUDE_RUNTIME

#include "vm/class_id.h"
#include "vm/compiler/asm_intrinsifier.h"
#include "vm/compiler/assembler/assembler.h"

namespace dart {
namespace compiler{

#define __ assembler->

void AsmIntrinsifier::WriteIntoOneByteString(Assembler* assembler,
                                             Label* normal_ir_body) {
  __ lw(T2, Address(SP, 0 * target::kWordSize));  // Value.
  __ lw(T1, Address(SP, 1 * target::kWordSize));  // Index.
  __ lw(T0, Address(SP, 2 * target::kWordSize));  // OneByteString.
  __ SmiUntag(T1);
  __ SmiUntag(T2);
  __ addu(T3, T0, T1);
  __ Ret();
  __ delay_slot()->sb(T2, FieldAddress(T3, target::OneByteString::data_offset()));
}

void AsmIntrinsifier::WriteIntoTwoByteString(Assembler* assembler,
                                             Label* normal_ir_body) {
  __ lw(T2, Address(SP, 0 * target::kWordSize));  // Value.
  __ lw(T1, Address(SP, 1 * target::kWordSize));  // Index.
  __ lw(T0, Address(SP, 2 * target::kWordSize));  // TwoByteString.
  // Untag index and multiply by element size -> no-op.
  __ SmiUntag(T2);
  __ addu(T3, T0, T1);
  __ Ret();
  __ delay_slot() -> sh(T2,
                        FieldAddress(T3, target::TwoByteString::data_offset()));
}

}  // namespace compiler
}  // namespace dart

#endif  // defined TARGET_ARCH_MIPS

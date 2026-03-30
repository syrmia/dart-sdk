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

// Allocates one-byte string of length 'end - start'. The content is not
// initialized.
// 'length-reg' (T2) contains tagged length.
// Returns new string as tagged pointer in V0.
static void TryAllocateString(Assembler* assembler,
                              classid_t cid,
                              intptr_t max_elements,
                              Label* ok,
                              Label* failure) {
  ASSERT(cid == kOneByteStringCid || cid == kTwoByteStringCid);
  const Register length_reg = T2;
  // _Mint length: call to runtime to produce error.
  __ BranchIfNotSmi(length_reg, failure);
  // Negative length: call to runtime to produce error.
  // Too big: call to runtime to allocate old.
  __ BranchUnsignedGreater(length_reg,
                           compiler::Immediate(target::ToRawSmi(max_elements)),
                           failure);
  NOT_IN_PRODUCT(__ MaybeTraceAllocation(cid, failure, V0));
  __ mov(T6, length_reg);  // Save the length register.

  if (cid == kOneByteStringCid) {
    __ SmiUntag(length_reg);
  } else {
    // Untag length and multiply by element size -> no-op.
  }
  const intptr_t fixed_size_plus_alignment_padding =
      target::String::InstanceSize() +
      target::ObjectAlignment::kObjectAlignment - 1;
  __ AddImmediate(length_reg, fixed_size_plus_alignment_padding);
  __ LoadImmediate(TMP, ~(target::ObjectAlignment::kObjectAlignment - 1));
  __ and_(length_reg, length_reg, TMP);

  __ lw(V0, Address(THR, target::Thread::top_offset()));

  // length_reg: allocation size.
  __ addu(T1, V0, length_reg);
  __ BranchUnsignedLess(T1, V0, failure);  // Fail on unsigned overflow.

  // Check if the allocation fits into the remaining space.
  // V0: potential new object start.
  // T1: potential next object start.
  // T2: allocation size.
  __ lw(T4, Address(THR, target::Thread::end_offset()));
  __ BranchUnsignedGreaterEqual(T1, T4, failure);
  __ CheckAllocationCanary(V0);

  // Successfully allocated the object(s), now update top to point to
  // next object start and initialize the object.
  __ sw(T1, Address(THR, target::Thread::top_offset()));
  __ AddImmediate(V0, kHeapObjectTag);

  // Clear last double word to ensure string comparison doesn't need to
  // specially handle remainder of strings with lengths not factors of double
  // offsets.
  __ sw(ZR, Address(T1, -1 * target::kWordSize));
  __ sw(ZR, Address(T1, -2 * target::kWordSize));


  // Initialize the tags.
  // V0: new object start as a tagged pointer.
  // T1: new object end address.
  // T2: allocation size.
  {
    Label overflow, done;
    const intptr_t shift = target::UntaggedObject::kSizeTagPos -
                           target::ObjectAlignment::kObjectAlignmentLog2;
    __ BranchUnsignedGreater(T2, Immediate(target::UntaggedObject::kSizeTagMaxSizeTag),
                             &overflow);
    __ b(&done);
    __ delay_slot()->sll(T2, T2, shift);
    __ Bind(&overflow);
    __ mov(T2, ZR);
    __ Bind(&done);

    // Get the class index and insert it into the tags.
    // T2: size and bit tags.
    const uword tags =
        target::MakeTagWordForNewSpaceObject(cid, /*instance_size=*/0);
    __ LoadImmediate(TMP, tags);
    __ or_(T2, T2, TMP);
    __ sw(T2, FieldAddress(V0, target::Object::tags_offset()));  // Store tags.
  }

  // Set the length field using the saved length (T6).
  __ StoreIntoObjectNoBarrier(V0, FieldAddress(V0, target::String::length_offset()),
                              T6);
  // Clear hash.
  __ StoreIntoObjectNoBarrier(
      V0, FieldAddress(V0, target::String::hash_offset()), ZR);

  __ b(ok);
}

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

void AsmIntrinsifier::AllocateOneByteString(Assembler* assembler,
                                             Label* normal_ir_body) {
  Label ok;

  __ lw(T2, Address(SP, 0 * target::kWordSize));  // Length.
  TryAllocateString(assembler, kOneByteStringCid,
                    target::OneByteString::kMaxNewSpaceElements, &ok, normal_ir_body);
  __ Bind(&ok);
  __ Ret();
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::AllocateTwoByteString(Assembler* assembler,
                                            Label* normal_ir_body) {
  Label ok;

  __ lw(T2, Address(SP, 0 * target::kWordSize));  // Length.
  TryAllocateString(assembler, kTwoByteStringCid,
                    target::TwoByteString::kMaxNewSpaceElements, &ok,
                    normal_ir_body);
  __ Bind(&ok);
  __ Ret();
  __ Bind(normal_ir_body);
}

}  // namespace compiler
}  // namespace dart

#endif  // defined TARGET_ARCH_MIPS

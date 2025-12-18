// Copyright (c) 2025, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#if defined(TARGET_ARCH_MIPS)

#include "vm/compiler/assembler/assembler.h"
#include "vm/compiler/backend/locations.h"

namespace dart {
namespace compiler{

void Assembler::EmitBranch(Opcode b, Register rs, Register rt, Label* label) {
  UNIMPLEMENTED();
}  

void Assembler::EmitRegImmBranch(RtRegImm b, Register rs, Label* label) {
  UNIMPLEMENTED();
}

void Assembler::EmitFpuBranch(bool kind, Label* label) {
  UNIMPLEMENTED();
}

void Assembler::PushRegisters(const RegisterSet& registers) {
  UNIMPLEMENTED();
}

void Assembler::PopRegisters(const RegisterSet& registers) {
  UNIMPLEMENTED();
}

void Assembler::PushRegistersInOrder(std::initializer_list<Register> regs) {
  for (Register reg : regs) {
    PushRegister(reg);
  }
}

void Assembler::PushRegisterPair(Register r0, Register r1){
  ASSERT(r0 != SP);
  ASSERT(r1 != SP);

  addiu(SP, SP, Immediate(-2 * target::kWordSize));
  sw(r1, Address(SP, target::kWordSize));
  sw(r0, Address(SP, 0));
}

void Assembler::PopRegisterPair(Register r0, Register r1){
  ASSERT(r0 != SP);
  ASSERT(r1 != SP);

  lw(r1, Address(SP, target::kWordSize));
  lw(r0, Address(SP, 0));
  addiu(SP, SP, Immediate(2 * target::kWordSize));
}

void Assembler::PushImmediate(int64_t immediate){
  LoadImmediate(TMP, immediate);
  Push(TMP);
}

void Assembler::PushValueAtOffset(Register base, int32_t offset){
  addiu(SP, SP, Immediate(-target::kWordSize));
  lw(TMP, Address(base, offset));
  sw(TMP, Address(SP, 0));
}

void Assembler::CompareRegisters(Register rn, Register rm) {
  ASSERT(deferred_compare_ == kNone);
  deferred_compare_ = kCompareReg;
  deferred_left_ = rn;
  deferred_reg_ = rm;
}

void Assembler::TestRegisters(Register rn, Register rm) {
  ASSERT(deferred_compare_ == kNone);
  deferred_compare_ = kTestReg;
  deferred_left_ = rn;
  deferred_reg_ = rm;
}

void Assembler::CompareImmediate(Register rn, int32_t imm, OperandSize sz) {
  ASSERT(deferred_compare_ == kNone);
  deferred_compare_ = kCompareImm;
  deferred_left_ = rn;
  deferred_imm_ = imm;
}

void Assembler::TestImmediate(Register rn, int32_t imm, OperandSize sz) {
  ASSERT(deferred_compare_ == kNone);
  deferred_compare_ = kTestImm;
  deferred_left_ = rn;
  deferred_imm_ = imm;
}

void Assembler::Bind(Label* label) {
  UNIMPLEMENTED();
}

void Assembler::LoadClassId(Register result, Register object) {
  UNIMPLEMENTED();
}

void Assembler::LoadClassById(Register result, Register class_id) {
  UNIMPLEMENTED();
}

void Assembler::CompareClassId(Register object,
                               intptr_t class_id,
                               Register scratch) {
  UNIMPLEMENTED();
}

void Assembler::LoadClassIdMayBeSmi(Register result, Register object) {
  UNIMPLEMENTED();
}

void Assembler::LoadTaggedClassIdMayBeSmi(Register result, Register object) {
  UNIMPLEMENTED();
}

void Assembler::EnterFrame(intptr_t frame_size) {
  ASSERT(!in_delay_slot_);
  addiu(SP, SP, Immediate(-2 * target::kWordSize));
  sw(RA, Address(SP, 1 * target::kWordSize));
  sw(FP, Address(SP, 0 * target::kWordSize));
  mov(FP, SP);
  if (frame_size != 0) {
    addiu(SP, SP, Immediate(-frame_size));
  }
}

void Assembler::LeaveFrame() {
  ASSERT(!in_delay_slot_);
  mov(SP, FP);
  lw(RA, Address(SP, 1 * target::kWordSize));
  lw(FP, Address(SP, 0 * target::kWordSize));
  addiu(SP, SP, Immediate(2 * target::kWordSize));
}

void Assembler::LeaveFrameAndReturn() {
  ASSERT(!in_delay_slot_);
  mov(SP, FP);
  lw(RA, Address(SP, 1 * target::kWordSize));
  lw(FP, Address(SP, 0 * target::kWordSize));
  Ret();
  delay_slot()->addiu(SP, SP, Immediate(2 * target::kWordSize));
}

void Assembler::EnterStubFrame(intptr_t frame_size) {
  EnterDartFrame(frame_size);
}

void Assembler::LeaveStubFrame() {
  LeaveDartFrame();
}

void Assembler::LeaveStubFrameAndReturn(Register ra) {
  LeaveDartFrameAndReturn(ra);
}

void Assembler::EnterDartFrame(intptr_t frame_size, bool load_pool_pointer) {
  ASSERT(!in_delay_slot_);

  SetPrologueOffset();

  if (FLAG_precompiled_mode) {
    addiu(SP, SP, Immediate(-2 * target::kWordSize));
    sw(RA, Address(SP, 1 * target::kWordSize));
    sw(FP, Address(SP, 0 * target::kWordSize));
    mov(FP, SP);
  } else {
    addiu(SP, SP, Immediate(-4 * target::kWordSize));
    sw(RA, Address(SP, 3 * target::kWordSize));
    sw(FP, Address(SP, 2 * target::kWordSize));
    sw(CODE_REG, Address(SP, 1 * target::kWordSize));
    sw(PP, Address(SP, 0 * target::kWordSize));

    // Set FP to the saved previous FP.
    addiu(FP, SP, Immediate(2 * target::kWordSize));

    if (load_pool_pointer) LoadPoolPointer();
  }
  set_constant_pool_allowed(true);

  // Reserve space for locals.
  AddImmediate(SP, -frame_size);
}

void Assembler::LeaveDartFrame(RestorePP restore_pp) {
  ASSERT(!in_delay_slot_);
  addiu(SP, FP, Immediate(-2 * target::kWordSize));

  lw(RA, Address(SP, 3 * target::kWordSize));
  lw(FP, Address(SP, 2 * target::kWordSize));
  if (restore_pp == kRestoreCallerPP && !FLAG_precompiled_mode) {
    lw(PP, Address(SP, 0 * target::kWordSize));
  }
  set_constant_pool_allowed(false);

  // Adjust SP for PC, RA, FP, PP pushed in EnterDartFrame.
  addiu(SP, SP, Immediate(4 * target::kWordSize));
}

void Assembler::LeaveDartFrameAndReturn(Register ra) {
  ASSERT(!in_delay_slot_);
  addiu(SP, FP, Immediate(-2 * target::kWordSize));

  lw(RA, Address(SP, 3 * target::kWordSize));
  lw(FP, Address(SP, 2 * target::kWordSize));
  if (!FLAG_precompiled_mode) {
    lw(PP, Address(SP, 0 * target::kWordSize));
  }
  set_constant_pool_allowed(false);

  // Adjust SP for PC, RA, FP, PP pushed in EnterDartFrame, and return.
  addiu(SP, SP, Immediate(4 * target::kWordSize));
  jr(ra);
}

// A0 receiver, S5 ICData entries array
void Assembler::MonomorphicCheckedEntryJIT() {
  has_monomorphic_entry_ = true;
  bool saved_use_far_branches = use_far_branches();
  set_use_far_branches(false);

  const intptr_t start = CodeSize();

  Label miss;
  Bind(&miss);
  lw(T9, Address(THR, target::Thread::switchable_call_miss_entry_offset()));
  jr(T9);

  Comment("MonomorphicCheckedEntry");
  ASSERT_EQUAL(CodeSize() - start, target::Instructions::kMonomorphicEntryOffsetJIT);

  const intptr_t cid_offset = target::Array::element_offset(0);
  const intptr_t count_offset = target::Array::element_offset(1);

  lw(T1, FieldAddress(S5, cid_offset));
  LoadTaggedClassIdMayBeSmi(A1, A0);
  bne(T1, A1, &miss);

  lw(T1, FieldAddress(S5, count_offset));
  AddImmediate(T1, T1, target::ToRawSmi(1));
  sw(T1, FieldAddress(S5, count_offset));

  LoadImmediate(ARGS_DESC_REG, 0);  // GC-safe for OptimizeInvokedFunction

  // Fall through to unchecked entry.
  ASSERT_EQUAL(CodeSize() - start, target::Instructions::kPolymorphicEntryOffsetJIT);

  set_use_far_branches(saved_use_far_branches);
}

// A0 receiver, S5 guarded cid as Smi
void Assembler::MonomorphicCheckedEntryAOT() {
  has_monomorphic_entry_ = true;
  bool saved_use_far_branches = use_far_branches();
  set_use_far_branches(false);

  const intptr_t start = CodeSize();

  Label miss;
  Bind(&miss);
  lw(T9, Address(THR, target::Thread::switchable_call_miss_entry_offset()));
  jr(T9);

  Comment("MonomorphicCheckedEntry");
  ASSERT_EQUAL(CodeSize() - start, target::Instructions::kMonomorphicEntryOffsetAOT);
  LoadClassId(TMP, A0);
  SmiTag(TMP);
  bne(S5, TMP, &miss);

  // Fall through to unchecked entry.
  ASSERT_EQUAL(CodeSize() - start, target::Instructions::kPolymorphicEntryOffsetAOT);

  set_use_far_branches(saved_use_far_branches);
}

}  // namespace compiler
}  // namespace dart

#endif  // defined TARGET_ARCH_MIPS

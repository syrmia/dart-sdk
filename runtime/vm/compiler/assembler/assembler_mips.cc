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

void PushRegisterPair(Register r0, Register r1){
  ASSERT(r0 != SP);
  ASSERT(r1 != SP);

  addiu(SP, SP, Immediate(-2 * target::kWordSize));
  sw(r1, Address(SP, target::kWordSize));
  sw(r0, Address(SP, 0));
}

void PopRegisterPair(Register r0, Register r1){
  ASSERT(r0 != SP);
  ASSERT(r1 != SP);

  lw(r1, Address(SP, target::kWordSize));
  lw(r0, Address(SP, 0));
  addiu(SP, SP, Immediate(2 * target::kWordSize));
}

void PushImmediate(int64_t immediate){
  LoadImmediate(TMP, immediate);
  Push(TMP);
}

void PushValueAtOffset(Register base, int32_t offset){
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

}  // namespace compiler
}  // namespace dart

#endif  // defined TARGET_ARCH_MIPS

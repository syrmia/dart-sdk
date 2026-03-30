// Copyright (c) 2025, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#if defined(TARGET_ARCH_MIPS)

#define SHOULD_NOT_INCLUDE_RUNTIME

#include "vm/compiler/assembler/assembler.h"
#include "vm/compiler/backend/locations.h"

namespace dart {

DECLARE_FLAG(bool, check_code_pointer);

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

void Assembler::CompareObjectRegisters(Register rn, Register rm) {
  CompareRegisters(rn, rm);
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

void Assembler::ArithmeticShiftRightImmediate(Register dst,
                                              Register src,
                                              int32_t shift,
                                              OperandSize sz) {

  ASSERT(IsSignedOperand(sz));
  ASSERT((shift >= 0) && (shift < OperandSizeInBits(sz)));
  if (shift == 0) {
    return ExtendValue(dst, src, sz);
  }
  if (shift != 0) {
    sra(dst, src, shift);
  }
}

void Assembler::CompareWords(Register reg1,
                             Register reg2,
                             intptr_t offset,
                             Register count,
                             Register temp,
                             Label* equals) {
  ASSERT(reg1 != TMP);
  ASSERT(reg2 != TMP);
  ASSERT(count != TMP);
  ASSERT(temp != TMP);
  Label loop;
  Bind(&loop);
  blez(count, equals);
  AddImmediate(count, count, -1);
  lw(temp, FieldAddress(reg1, offset));
  lw(TMP, FieldAddress(reg2, offset));
  AddImmediate(reg1, reg1, target::kWordSize);
  AddImmediate(reg2, reg2, target::kWordSize);
  beq(temp, TMP, &loop);
}

void Assembler::AddShifted(Register dest,
                           Register base,
                           Register index,
                           int32_t shift) {
  if (shift == 0) {
    addu(dest, index, base);
  } else if (shift < 0) {
    if (base != dest) {
      sra(dest, index, -shift);
      addu(dest, dest, base);
    } else {
      ASSERT(TMP != dest);
      Push(TMP);
      sra(TMP, index, -shift);
      addu(dest, TMP, base);
      Pop(TMP);
    }
  } else {
    if (base != dest) {
      sll(dest, index, shift);
      addu(dest, dest, base);
    } else {
      ASSERT(TMP != dest);
      Push(TMP);
      sll(TMP, index, shift);
      addu(dest, TMP, base);
      Pop(TMP);
    }
  }
}

void Assembler::AddScaled(Register dest,
                          Register base,
                          Register index,
                          ScaleFactor scale,
                          int32_t disp) {
    if (base == kNoRegister || base == ZR) {
      if (scale == TIMES_1) {
        AddImmediate(dest, index, disp);
      } else {
        sll(dest, index, scale);
        AddImmediate(dest, disp);
      }
    } else {
      AddShifted(dest, base, index, scale);
      AddImmediate(dest, disp);
    }
  }

// Branch to label if condition is true.
void Assembler::BranchIf(Condition cond, Label* l, JumpDistance distance) {
  ASSERT(!in_delay_slot_);
  ASSERT(deferred_compare_ != kNone);

  if (deferred_compare_ == kCompareImm || deferred_compare_ == kCompareReg) {
    Register left = deferred_left_;
    Register right;
    if (deferred_compare_ == kCompareImm) {
      if (deferred_imm_ == 0) {
        right = ZR;
      } else {
        LoadImmediate(AT, deferred_imm_);
        right = AT;
      }
    } else {
      right = deferred_reg_;
    }
    switch (cond) {
      case NV: {
        deferred_compare_ = kNone; // Consumed.
        return;
      }
      case AL: {
        b(l);
        deferred_compare_ = kNone; // Consumed.
        return;
      }
      case EQ:{
        beq(left, right, l);
        break;
      }
      case NE:{
        bne(left, right, l);
        break;
      }
      case GT:{
        slt(AT, right, left);
        bne(AT, ZR, l);
        break;
      }
      case GE: {
        slt(AT, left, right);
        beq(AT, ZR, l);
        break;
      }
      case LT: {
        slt(AT, left, right);
        bne(AT, ZR, l);
        break;
      }
      case LE: {
        slt(AT, right, left);
        beq(AT, ZR, l);
        break;
      }
      case UGT: {
        sltu(AT, right, left);
        bne(AT, ZR, l);
        break;
      }
      case UGE: {
        sltu(AT, left, right);
        beq(AT, ZR, l);
        break;
      }
      case ULT: {
        sltu(AT, left, right);
        bne(AT, ZR, l);
        break;
      }
      case ULE: {
        sltu(AT, right, left);
        beq(AT, ZR, l);
        break;
      }
      default:
        UNREACHABLE();
    }
  } else if (deferred_compare_ == kTestImm || deferred_compare_ == kTestReg) {
    if (deferred_compare_ == kTestReg) {
      and_(CMPRES1, deferred_left_, deferred_reg_);
    } else {
      AndImmediate(CMPRES1, deferred_left_, deferred_imm_);
    }
    switch (cond) {
      case ZERO:
        beq(CMPRES1, ZR, l);
        break;
      case NOT_ZERO:
        bne(CMPRES1, ZR, l);
        break;
      default:
        UNREACHABLE();
    }
  } else {
    UNREACHABLE();
  }
  deferred_compare_ = kNone; // Consumed.
}

void Assembler::BranchIfZero(Register rn, Label* label, JumpDistance distance) {
  beq(rn, ZR, label);
}

void Assembler::BranchIfBit(Register rn,
                            intptr_t bit_number,
                            Condition condition,
                            Label* label,
                            JumpDistance distance) {
  ASSERT(rn != CMPRES1);
  andi(CMPRES1, rn, compiler::Immediate(1 << bit_number));
  if (condition == ZERO) {
    beq(CMPRES1, ZR, label);
  } else if (condition == NOT_ZERO) {
    bne(CMPRES1, ZR, label);
  } else {
    UNREACHABLE();
  }
}

void Assembler::SetIf(Condition condition, Register rd) {
  ASSERT(deferred_compare_ != kNone);

  Register left = deferred_left_;
  Register right;
  if (deferred_compare_ == kCompareImm || deferred_compare_ == kCompareReg) {
    if (deferred_compare_ == kCompareImm) {
      if (deferred_imm_ == 0) {
        right = ZR;
      } else {
        LoadImmediate(AT, deferred_imm_);
        right = AT;
      }
    } else {
      right = deferred_reg_;
    }

    switch (condition) {
      case AL:
      case NV:
        deferred_compare_ = kNone;
        return;  // Result holds true_value.
      case EQ:{
        xor_(rd, left, right);
        sltiu(rd, rd, compiler::Immediate(1));
        break;
      }
      case NE: {
        xor_(rd, left, right);
        break;
      }
      case GE:{
        slt(rd, left, right);
        xori(rd, rd, compiler::Immediate(1));
        break;
      }
      case LT: {
        slt(rd, left, right);
        break;
      }
      case LE:{
        slt(rd, right, left);
        xori(rd, rd, compiler::Immediate(1));
        break;
      }
      case GT: {
        slt(rd, right, left);
        break;
      }
      case UGE:{
        sltu(rd, left, right);
        xori(rd, rd, compiler::Immediate(1));
        break;
      }
      case ULT: {
        sltu(rd, left, right);
        break;
      }
      case ULE:{
        sltu(rd, right, left);
        xori(rd, rd, compiler::Immediate(1));
        break;
      }
      case UGT: {
        sltu(rd, right, left);
        break;
      }
      default:
        UNREACHABLE();
    }
  } else if (deferred_compare_ == kTestImm) {
    AndImmediate(rd, deferred_left_, deferred_imm_);
    switch (condition) {
      case ZERO:
        sltiu(rd, rd, compiler::Immediate(1));
        break;
      case NOT_ZERO:
        sltu(rd, ZR, rd);
        break;
      default:
        UNREACHABLE();
    }
  } else if (deferred_compare_ == kTestReg) {
    and_(rd, deferred_left_, deferred_reg_);
    switch (condition) {
      case ZERO:
        sltiu(rd, rd, compiler::Immediate(1));
        break;
      case NOT_ZERO:
        sltu(rd, ZR, rd);
        break;
      default:
        UNREACHABLE();
    }
  } else {
    UNREACHABLE();
  }

  deferred_compare_ = kNone;
}

void Assembler::BranchLink(
    const Code& target,
    ObjectPoolBuilderEntry::Patchability patchable,
    CodeEntryKind entry_kind,
    ObjectPoolBuilderEntry::SnapshotBehavior snapshot_behavior) {
  ASSERT(!in_delay_slot_);
  const intptr_t index = object_pool_builder().FindObject(
      ToObject(target), patchable, snapshot_behavior);
  // Avoid clobbering CODE_REG when invoking code in precompiled mode.
  // We don't actually use CODE_REG in the callee and caller might
  // be using CODE_REG for a live value (e.g. a value that is alive
  // across invocation of a shared stub like the one we use for
  // allocating Mint boxes).
  const Register code_reg = FLAG_precompiled_mode ? TMP : CODE_REG;
  LoadWordFromPoolIndex(code_reg, index);
  lw(T9, FieldAddress(code_reg, target::Code::entry_point_offset(entry_kind)));
  jalr(T9);
  if (patchable == ObjectPoolBuilderEntry::kPatchable) {
    delay_slot_available_ = false;  // CodePatcher expects a nop.
  }
}

void Assembler::BranchLinkPatchable(
    const Code& code,
    CodeEntryKind entry_kind,
    ObjectPoolBuilderEntry::SnapshotBehavior snapshot_behavior) {
  BranchLink(code, ObjectPoolBuilderEntry::kPatchable, entry_kind,
             snapshot_behavior);
}

void Assembler::BranchLinkWithEquivalence(const Code& target,
                                          const Object& equivalence,
                                          CodeEntryKind entry_kind) {
  ASSERT(!in_delay_slot_);
  const intptr_t index =
      object_pool_builder().FindObject(ToObject(target), equivalence);
  // Avoid clobbering CODE_REG when invoking code in precompiled mode.
  // We don't actually use CODE_REG in the callee and caller might
  // be using CODE_REG for a live value (e.g. a value that is alive
  // across invocation of a shared stub like the one we use for
  // allocating Mint boxes).
  const Register code_reg = FLAG_precompiled_mode ? TMP : CODE_REG;
  LoadWordFromPoolIndex(code_reg, index);
  lw(T9, FieldAddress(code_reg, target::Code::entry_point_offset(entry_kind)));
  jalr(T9);
  delay_slot_available_ = false;  // CodePatcher expects a nop.
}

void Assembler::AddBranchOverflow(Register rd,
                                  Register rs1,
                                  Register rs2,
                                  Label* overflow) {
  ASSERT(rd != CMPRES1);
  ASSERT(rd != CMPRES2);
  ASSERT(rs1 != CMPRES1);
  ASSERT(rs1 != CMPRES2);
  ASSERT(rs2 != CMPRES1);
  ASSERT(rs2 != CMPRES2);

  if ((rd == rs1) && (rd == rs2)) {
    ASSERT(rs1 == rs2);
    mov(CMPRES1, rs1);
    addu(rd, rs1, rs2);   // rs1, rs2 destroyed
    xor_(CMPRES1, CMPRES1, rd);  // CMPRES1 negative if sign changed
    bltz(CMPRES1, overflow);
  } else if (rs1 == rs2) {
    ASSERT(rd != rs1);
    ASSERT(rd != rs2);
    addu(rd, rs1, rs2);
    xor_(CMPRES1, rd, rs1);  // CMPRES1 negative if sign changed
    bltz(CMPRES1, overflow);
  } else if (rd == rs1) {
    ASSERT(rs1 != rs2);
    slti(CMPRES1, rs1, Immediate(0));
    addu(rd, rs1, rs2);  // rs1 destroyed
    slt(CMPRES2, rd, rs2);
    bne(CMPRES1, CMPRES2, overflow);
  } else if (rd == rs2) {
    ASSERT(rs1 != rs2);
    slti(CMPRES1, rs2, Immediate(0));
    addu(rd, rs1, rs2);  // rs2 destroyed
    slt(CMPRES2, rd, rs1);
    bne(CMPRES1, CMPRES2, overflow);
  } else {
    addu(rd, rs1, rs2);
    slti(CMPRES1, rs2, Immediate(0));
    slt(CMPRES2, rd, rs1);
    bne(CMPRES1, CMPRES2, overflow);
  }
}

void Assembler::Bind(Label* label) {
  UNIMPLEMENTED();
}

Address Assembler::PrepareLargeOffset(Register base, int32_t offset) {
  ASSERT(!in_delay_slot_);
  if (Utils::IsInt(kImmBits, offset)) {
    return Address(base, offset);
  } else {
    ASSERT(base != TMP);
    LoadImmediate(TMP, offset);
    addu(TMP, base, TMP);
    return Address(TMP, 0);
  }
}

void Assembler::LoadObjectHelper(Register rd,
                                 const Object& object,
                                 bool is_unique,
                                 ObjectPoolBuilderEntry::SnapshotBehavior snapshot_behavior) {
  ASSERT(IsOriginalObject(object));
  // `is_unique == true` effectively means object has to be patchable.
  // (even if the object is null)
  if(!is_unique){
    intptr_t offset = 0;
    if (target::CanLoadFromThread(object, &offset)) {
      // Load common VM constants from the thread. This works also in places where
      // no constant pool is set up (e.g. intrinsic code).
      lw(rd, Address(THR, offset));
      return;
    }
    if (target::IsSmi(object)) {
      // Relocation doesn't apply to Smis.
      LoadImmediate(rd, target::ToRawSmi(object));
      return;
    }
  }
  RELEASE_ASSERT(CanLoadFromObjectPool(object));
  // Make sure that class CallPattern is able to decode this load from the
  // object pool.
  const intptr_t index =
      is_unique ? object_pool_builder().AddObject(
                      object, ObjectPoolBuilderEntry::kPatchable, snapshot_behavior)
                : object_pool_builder().FindObject(
                      object, ObjectPoolBuilderEntry::kNotPatchable,
                      snapshot_behavior);
  LoadWordFromPoolIndex(rd, index);
}

void Assembler::LoadObject(Register rd, const Object& object) {
  LoadObjectHelper(rd, object, false);
}

void Assembler::LoadUniqueObject(Register rd, const Object& object,
                                ObjectPoolBuilderEntry::SnapshotBehavior snapshot_behavior) {
  LoadObjectHelper(rd, object, true, snapshot_behavior);
}

void Assembler::RangeCheck(Register value,
                           Register temp,
                           intptr_t low,
                           intptr_t high,
                           RangeCheckCondition condition,
                           Label* target) {
  Register to_check = temp != kNoRegister ? temp : value;
  AddImmediate(to_check, value, -low);
  if (condition == kIfInRange) {
    BranchUnsignedLessEqual(to_check, Immediate(high - low), target);
  } else {
    BranchUnsignedGreater(to_check, Immediate(high - low), target);
  }
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
  ASSERT(result != object);
  ASSERT(result != CMPRES1);
  ASSERT(object != CMPRES1);
  Label done;
  LoadImmediate(result, kSmiCid);
  BranchIfSmi(object, &done);
  LoadClassId(result, object);
  Bind(&done);
}

void Assembler::LoadTaggedClassIdMayBeSmi(Register result, Register object) {
  LoadClassIdMayBeSmi(result, object);
  SmiTag(result);
}

void Assembler::EnsureHasClassIdInDEBUG(intptr_t cid,
                                        Register src,
                                        Register scratch,
                                        bool can_be_null) {
#if defined(DEBUG)
  Comment("Check that object in register has cid %" Pd "", cid);
  Label matches;
  LoadClassIdMayBeSmi(scratch, src);
  BranchEqual(scratch, compiler::Immediate(cid), &matches);
  if (can_be_null) {
    BranchEqual(scratch, compiler::Immediate(kNullCid), &matches);
  }
  Breakpoint();
  Bind(&matches);
#endif
}

bool Assembler::CanLoadFromObjectPool(const Object& object) const {
  ASSERT(IsOriginalObject(object));
  if (!constant_pool_allowed()) {
    return false;
  }

#if defined(DEBUG)
  ASSERT(IsNotTemporaryScopedHandle(object));
#endif
  ASSERT(IsInOldSpace(object));
  return true;
}

void Assembler::Load(Register dest, const Address& address, OperandSize sz) {
  Address addr = PrepareLargeOffset(address.base(), address.offset());
  switch (sz) {
    case kByte:
      return lb(dest, addr);
    case kUnsignedByte:
      return lbu(dest, addr);
    case kTwoBytes:
      return lh(dest, addr);
    case kUnsignedTwoBytes:
      return lhu(dest, addr);
    case kUnsignedFourBytes:
    case kFourBytes:
      return lw(dest, addr);
    default:
      UNREACHABLE();
      break;
  }
}

void Assembler::LoadWordFromPoolIndex(Register rd,
                                      intptr_t index,
                                      Register pp) {
  ASSERT((pp != PP) || constant_pool_allowed());
  ASSERT(!in_delay_slot_);
  ASSERT(rd != pp);

  uint32_t offset = target::ObjectPool::element_offset(index) - kHeapObjectTag;

  if (Address::CanHoldOffset(offset)) {
    lw(rd, Address(pp, offset));
  } else {
    const int16_t offset_low = Utils::Low16Bits(offset);     // Signed.
    offset -= offset_low;
    const uint16_t offset_high = Utils::High16Bits(offset);  // Unsigned.
    if (offset_high != 0) {
      lui(rd, Immediate(offset_high));
      addu(rd, rd, pp);
      lw(rd, Address(rd, offset_low));
    } else {
      lw(rd, Address(pp, offset_low));
    }
  }
}

void Assembler::StoreWordToPoolIndex(Register rs,
                                     intptr_t index,
                                     Register pp) {
  ASSERT((pp != PP) || constant_pool_allowed());
  ASSERT(!in_delay_slot_);
  ASSERT(rs != pp);

  uint32_t offset =
      target::ObjectPool::element_offset(index) - kHeapObjectTag;

  if (Address::CanHoldOffset(offset)) {
    sw(rs, Address(pp, offset));
  } else {
    const int16_t offset_low = Utils::Low16Bits(offset);     // Signed.
    offset -= offset_low;
    const uint16_t offset_high = Utils::High16Bits(offset);  // Unsigned.
    if (offset_high != 0) {
      lui(TMP, Immediate(offset_high));
      addu(TMP, TMP, pp);
      sw(rs, Address(TMP, offset_low));
    } else {
      sw(rs, Address(pp, offset_low));
    }
  }
}

void Assembler::LoadFieldAddressForRegOffset(Register address,
                                             Register instance,
                                             Register offset_in_words_as_smi) {
  AddShifted(address, instance, offset_in_words_as_smi,
             target::kWordSizeLog2 - kSmiTagShift);
  AddImmediate(address, address, -kHeapObjectTag);
}

void Assembler::Store(Register reg, const Address& address, OperandSize sz) {
  Address addr = PrepareLargeOffset(address.base(), address.offset());
  switch (sz) {
    case kUnsignedFourBytes:
    case kFourBytes:
      return sw(reg, addr);
    case kUnsignedTwoBytes:
    case kTwoBytes:
      return sh(reg, addr);
    case kUnsignedByte:
    case kByte:
      return sb(reg, addr);
    default:
      UNREACHABLE();
  }
}

void Assembler::RestoreCodePointer() {
  lw(CODE_REG, Address(FP, target::frame_layout.code_from_fp * target::kWordSize));
  CheckCodePointer();
}

void Assembler::JumpAndLink(intptr_t target_code_pool_index,
                            CodeEntryKind entry_kind) {
  // Avoid clobbering CODE_REG when invoking code in precompiled mode.
  // We don't actually use CODE_REG in the callee and caller might
  // be using CODE_REG for a live value (e.g. a value that is alive
  // across invocation of a shared stub like the one we use for
  // allocating Mint boxes).
  const Register code_reg = FLAG_precompiled_mode ? TMP : CODE_REG;
  LoadWordFromPoolIndex(code_reg, target_code_pool_index);
  Call(FieldAddress(code_reg, target::Code::entry_point_offset(entry_kind)));
}

void Assembler::JumpAndLink(
    const Code& target,
    ObjectPoolBuilderEntry::Patchability patchable,
    CodeEntryKind entry_kind,
    ObjectPoolBuilderEntry::SnapshotBehavior snapshot_behavior) {
  const intptr_t index = object_pool_builder().FindObject(
      ToObject(target), patchable, snapshot_behavior);
  JumpAndLink(index, entry_kind);
}

// Generate code to call into the stub which will call the runtime
// function. Input for the stub is as follows:
//   SP : points to the arguments and return value array.
//   S5 : address of the runtime function to call.
//   S4 : number of arguments to the call.
void Assembler::CallRuntime(const RuntimeEntry& entry,
                            intptr_t argument_count,
                            bool tsan_enter_exit) {
  ASSERT(!entry.is_leaf());
  // Argument count is not checked here, but in the runtime entry for a more
  // informative error message.
  lw(S5, compiler::Address(THR, entry.OffsetFromThread()));
  LoadImmediate(S4, argument_count);
  Call(Address(THR, target::Thread::call_to_runtime_entry_point_offset()));
}

void Assembler::LoadPoolPointer(Register reg) {
  ASSERT(!in_delay_slot_);
  CheckCodePointer();
  lw(reg, FieldAddress(CODE_REG, target::Code::object_pool_offset()));
  set_constant_pool_allowed(reg == PP);
}

void Assembler::CheckCodePointer() {
#ifdef DEBUG
  if (!FLAG_check_code_pointer) {
    return;
  }
  Comment("CheckCodePointer");
  Label cid_ok, instructions_ok;
  Push(CMPRES1);
  Push(CMPRES2);
  LoadClassId(CMPRES1, CODE_REG);
  BranchEqual(CMPRES1, Immediate(kCodeCid), &cid_ok);
  break_(0);
  Bind(&cid_ok);
  GetNextPC(CMPRES1, TMP);
  const intptr_t entry_offset = CodeSize() - Instr::kInstrSize +
                                target::Instructions::HeaderSize() - kHeapObjectTag;
  AddImmediate(CMPRES1, CMPRES1, -entry_offset);
  lw(CMPRES2, FieldAddress(CODE_REG, target::Code::instructions_offset()));
  BranchEqual(CMPRES1, CMPRES2, &instructions_ok);
  break_(1);
  Bind(&instructions_ok);
  Pop(CMPRES2);
  Pop(CMPRES1);
#endif
}

void Assembler::GetNextPC(Register dest, Register temp) {
  if (temp != kNoRegister) {
    mov(temp, RA);
  }
  EmitRegImmType(REGIMM, R0, BGEZAL, 1);
  mov(dest, RA);
  if (temp != kNoRegister) {
    mov(RA, temp);
  }
}

void Assembler::LoadIsolate(Register result) {
  lw(result, Address(THR, target::Thread::isolate_offset()));
}

void Assembler::LoadIsolateGroup(Register rd) {
  lw(rd, Address(THR, target::Thread::isolate_group_offset()));
}

void Assembler::StoreObjectIntoObjectNoBarrier(Register object,
                                               const Address& address,
                                               const Object& value,
                                               MemoryOrder memory_order,
                                               OperandSize size) {
  ASSERT(IsOriginalObject(value));
  ASSERT(!in_delay_slot_);
  DEBUG_ASSERT(IsNotTemporaryScopedHandle(value));

  Register value_reg;
  if (IsSameObject(compiler::NullObject(), value)) {
    ASSERT(object != TMP);
    LoadObject(TMP, compiler::NullObject());
    value_reg = TMP;
  } else if (target::IsSmi(value) && (target::ToRawSmi(value) == 0)) {
    value_reg = ZR;
  } else {
    ASSERT(object != TMP);
    LoadObject(TMP, value);
    value_reg = TMP;
  }
  if (memory_order == kRelease) {
    sync(0);
  }
  Store(value_reg, address, size);
}

void Assembler::StoreBarrier(Register object,
                             Register value,
                             CanBeSmi can_value_be_smi,
                             Register scratch) {
  // x.slot = x. Barrier should have be removed at the IL level.
  ASSERT(object != value);
  ASSERT(object != scratch);
  ASSERT(value != scratch);
  ASSERT(object != RA);
  ASSERT(value != RA);
  ASSERT(scratch != RA);
  ASSERT(scratch != kNoRegister);

  Label done;
  if (can_value_be_smi == kValueCanBeSmi) {
    BranchIfSmi(value, &done, kNearJump);
  } else {
#if defined(DEBUG)
    Label passed_check;
    BranchIfNotSmi(value, &passed_check, kNearJump);
    Breakpoint();
    Bind(&passed_check);
#endif
  }
  Push(RA);
  lbu(scratch, FieldAddress(object, target::Object::tags_offset()));
  lbu(RA, FieldAddress(value, target::Object::tags_offset()));
  srl(scratch, scratch, target::UntaggedObject::kBarrierOverlapShift);
  and_(scratch, scratch, RA);
  lw(RA, Address(THR, target::Thread::write_barrier_mask_offset()));
  and_(RA, RA, scratch);

  Label restore_and_done;

  BranchEqual(RA, ZR, &restore_and_done);

  Register objectForCall = object;
  if (value != kWriteBarrierValueReg) {
    if (object != kWriteBarrierValueReg) {
      Push(kWriteBarrierValueReg);
    } else {
      COMPILE_ASSERT(S1 != kWriteBarrierValueReg);
      COMPILE_ASSERT(S2 != kWriteBarrierValueReg);
      objectForCall = (value == S1) ? S2 : S1;
      Push(kWriteBarrierValueReg);
      Push(objectForCall);
      mov(objectForCall, object);
    }
    mov(kWriteBarrierValueReg, value);
  }

  generate_invoke_write_barrier_wrapper_(objectForCall);

  if (value != kWriteBarrierValueReg) {
    if (object != kWriteBarrierValueReg) {
      Pop(kWriteBarrierValueReg);
    } else {
      Pop(objectForCall);
      Pop(kWriteBarrierValueReg);
    }
  }

  Bind(&restore_and_done);
  Pop(RA);
  Bind(&done);
}

void Assembler::ArrayStoreBarrier(Register object,
                                  Register slot,
                                  Register value,
                                  CanBeSmi can_value_be_smi,
                                  Register scratch) {
  ASSERT(object != slot);
  ASSERT(object != value);
  ASSERT(object != scratch);
  ASSERT(slot != value);
  ASSERT(slot != scratch);
  ASSERT(value != scratch);
  ASSERT(object != RA);
  ASSERT(slot != RA);
  ASSERT(value != RA);
  ASSERT(scratch != RA);
  ASSERT(scratch != kNoRegister);

  // In parallel, test whether
  //  - object is old and not remembered and value is new, or
  //  - object is old and value is old and not marked and concurrent marking is
  //    in progress
  // If so, call the WriteBarrier stub, which will either add object to the
  // store buffer (case 1) or add value to the marking stack (case 2).
  // Compare UntaggedObject::StorePointer.
  Label done;
  if (can_value_be_smi == kValueCanBeSmi) {
    BranchIfSmi(value, &done, kNearJump);
  } else {
#if defined(DEBUG)
    Label passed_check;
    BranchIfNotSmi(value, &passed_check, kNearJump);
    Breakpoint();
    Bind(&passed_check);
#endif
  }
  Push(RA);
  lbu(scratch, FieldAddress(object, target::Object::tags_offset()));
  lbu(RA, FieldAddress(value, target::Object::tags_offset()));
  srl(scratch, scratch, target::UntaggedObject::kBarrierOverlapShift);
  and_(scratch, scratch, RA);
  lw(RA, Address(THR, target::Thread::write_barrier_mask_offset()));
  and_(RA, RA, scratch);

  Label restore_and_done;

  BranchEqual(RA, ZR, &restore_and_done);

  if ((object != kWriteBarrierObjectReg) || (value != kWriteBarrierValueReg) ||
      (slot != kWriteBarrierSlotReg)) {
    // Spill and shuffle unimplemented. Currently StoreIntoArray is only used
    // from StoreIndexInstr, which gets these exact registers from the register
    // allocator.
    UNIMPLEMENTED();
  }
  generate_invoke_array_write_barrier_();

  Bind(&restore_and_done);
  Pop(RA);
  Bind(&done);
}

void Assembler::VerifyStoreNeedsNoWriteBarrier(Register object,
                                               Register value) {
  if (value == ZR) return;
  // We can't assert the incremental barrier is not needed here, only the
  // generational barrier. We sometimes omit the write barrier when 'value' is
  // a constant, but we don't eagerly mark 'value' and instead assume it is also
  // reachable via a constant pool, so it doesn't matter if it is not traced via
  // 'object'.
  Label done;
  BranchIfSmi(value, &done, kNearJump);
  lbu(CMPRES2, FieldAddress(value, target::Object::tags_offset()));
  andi(CMPRES2, CMPRES2, Immediate(1 << target::UntaggedObject::kNewOrEvacuationCandidateBit));
  BranchEqual(CMPRES2, ZR, &done);
  lbu(CMPRES2, FieldAddress(object, target::Object::tags_offset()));
  andi(CMPRES2, CMPRES2, Immediate(1 << target::UntaggedObject::kOldAndNotRememberedBit));
  BranchEqual(CMPRES2, ZR, &done);
  Stop("Write barrier is required");
  Bind(&done);
}

void Assembler::ExtendValue(Register dst, Register src, OperandSize sz) {
  switch (sz) {
    case kUnsignedFourBytes:
    case kFourBytes:
      if (dst == src) return;
      return mov(dst, src);
    case kUnsignedTwoBytes:
      return andi(dst, src, Immediate(0xFFFF));
    case kTwoBytes:
      if (dst != src) {
        mov(dst, src);
      }
      sll(dst, dst, 16);
      return sra(dst, dst, 16);
    case kUnsignedByte:
      return andi(dst, src, Immediate(0xFF));
    case kByte:
      if (dst != src) {
        mov(dst, src);
      }
      sll(dst, dst, 24);
      return sra(dst, dst, 24);
    default:
      UNIMPLEMENTED();
      break;
  }
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

void Assembler::EnterFullSafepoint(Register addr, Register state) {
  // We generate the same number of instructions whether or not the slow-path is
  // forced. This simplifies GenerateJitCallbackTrampolines.
  Label slow_path, done, retry;
  if (FLAG_use_slow_path) {
    b(&slow_path);
  }

  AddImmediate(addr, THR, target::Thread::safepoint_state_offset());
  Bind(&retry);
  ll(state, Address(addr, 0));
  ASSERT(TMP!=addr);
  ASSERT(TMP!=state);
  LoadImmediate(TMP, target::Thread::native_safepoint_state_unacquired());
  BranchNotEqual(state, TMP, &slow_path);

  LoadImmediate(state, target::Thread::native_safepoint_state_acquired());
  sc(state, Address(addr, 0));
  BranchEqual(state, compiler::Immediate(1),
              &done);  // 1 means sc was successful.

  if (!FLAG_use_slow_path) {
    b(&retry);
  }

  Bind(&slow_path);
  lw(TMP, Address(THR, target::Thread::enter_safepoint_stub_offset()));
  lw(TMP, FieldAddress(TMP, target::Code::entry_point_offset()));
  mov(T9, TMP);
  jalr(T9);

  Bind(&done);
}

void Assembler::ExitFullSafepoint(Register addr,
                                  Register state) {
  // We generate the same number of instructions whether or not the slow-path is
  // forced, for consistency with EnterFullSafepoint.
  Label slow_path, done, retry;
  if (FLAG_use_slow_path) {
    b(&slow_path);
  }

  AddImmediate(addr, THR, target::Thread::safepoint_state_offset());
  Bind(&retry);
  ll(state, Address(addr, 0));
  BranchNotEqual(
      state,
      compiler::Immediate(target::Thread::native_safepoint_state_acquired()),
      &slow_path);

  LoadImmediate(state, target::Thread::native_safepoint_state_unacquired());
  sc(state, Address(addr, 0));
  BranchEqual(state, compiler::Immediate(1),
              &done);  // 1 means sc was successful.

  if (!FLAG_use_slow_path) {
    b(&retry);
  }

  Bind(&slow_path);
  lw(TMP, Address(THR, target::Thread::exit_safepoint_stub_offset()));
  lw(T9, FieldAddress(TMP, target::Code::entry_point_offset()));
  jalr(T9);

  Bind(&done);
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

void Assembler::CombineHashes(Register dst, Register other) {
  // hash += other_hash
  addu(dst, dst, other);
  // hash += hash << 10
  sll(other, dst, 10);
  addu(dst, dst, other);
  // hash ^= hash >> 6
  srl(other, dst, 6);
  xor_(dst, dst, other);
}

void Assembler::FinalizeHashForSize(intptr_t bit_size,
                                    Register hash,
                                    Register scratch) {
  ASSERT(bit_size > 0);
  ASSERT(bit_size <= kBitsPerInt32);
  ASSERT(scratch != kNoRegister);
  // hash += hash << 3;
  sll(scratch, hash, 3);
  addu(hash, hash, scratch);
  // hash ^= hash >> 11;  // Logical shift, unsigned hash.
  srl(scratch, hash, 11);
  xor_(hash, hash, scratch);
  // hash += hash << 15;
  sll(scratch, hash, 15);
  addu(hash, hash, scratch);

  // Size to fit.
  if (bit_size < kBitsPerInt32) {
    AndImmediate(hash, hash, Utils::NBitMask(bit_size));
  }
  // return (hash == 0) ? 1 : hash;
  LoadImmediate(CMPRES1, 1);
  movz(hash, CMPRES1, hash);  // If hash is 0, set to 1.
}

void Assembler::BranchOnMonomorphicCheckedEntryJIT(Label* label) {
  has_monomorphic_entry_ = true;
  while (CodeSize() < target::Instructions::kMonomorphicEntryOffsetJIT) {
    break_(0);
  }
  b(label);
  while (CodeSize() < target::Instructions::kPolymorphicEntryOffsetJIT) {
    break_(0);
  }
}

#ifndef PRODUCT
void Assembler::MaybeTraceAllocation(intptr_t cid,
                                     Label* trace,
                                     Register temp_reg,
                                     JumpDistance distance) {
  ASSERT(cid > 0);
  ASSERT(!in_delay_slot_);
  ASSERT(temp_reg != kNoRegister);
  LoadIsolateGroup(temp_reg);
  lw(temp_reg, Address(temp_reg, target::IsolateGroup::class_table_offset()));
  lw(temp_reg, Address(temp_reg, target::ClassTable::allocation_tracing_state_table_offset()));
  LoadFromOffset(temp_reg, temp_reg, target::ClassTable::AllocationTracingStateSlotOffsetFor(cid), kUnsignedByte);
  bne(temp_reg, ZR, trace);
}

void Assembler::MaybeTraceAllocation(Register cid,
                                     Label* trace,
                                     Register temp_reg,
                                     JumpDistance distance) {
  ASSERT(temp_reg != cid);
  ASSERT(!in_delay_slot_);
  ASSERT(temp_reg != kNoRegister);
  LoadIsolateGroup(temp_reg);
  lw(temp_reg, Address(temp_reg, target::IsolateGroup::class_table_offset()));
  lw(temp_reg,
    Address(temp_reg,
            target::ClassTable::allocation_tracing_state_table_offset()));
  AddRegisters(temp_reg, cid);
  LoadFromOffset(temp_reg, temp_reg, target::ClassTable::AllocationTracingStateSlotOffsetFor(0), kUnsignedByte);
  bne(temp_reg, ZR, trace);
}
#endif  // !PRODUCT

void Assembler::TryAllocateObject(intptr_t cid,
                                  intptr_t instance_size,
                                  Label* failure,
                                  JumpDistance distance,
                                  Register instance_reg,
                                  Register temp_reg) {
  ASSERT(failure != nullptr);
  ASSERT(instance_reg != kNoRegister);
  ASSERT(instance_reg != temp_reg);
  ASSERT(temp_reg != kNoRegister);
  ASSERT(temp_reg != T8);
  ASSERT(instance_size != 0);
  ASSERT(Utils::IsAligned(instance_size,
                          target::ObjectAlignment::kObjectAlignment));
  if (FLAG_inline_alloc &&
      target::Heap::IsAllocatableInNewSpace(instance_size)) {
    lw(instance_reg, Address(THR, target::Thread::top_offset()));
    AddImmediate(instance_reg, instance_size);

    // instance_reg: potential next object start.
    lw(T8, Address(THR, target::Thread::end_offset()));
    // Fail if heap end unsigned less than or equal to instance_reg.
    BranchUnsignedLessEqual(T8, instance_reg, failure);
    CheckAllocationCanary(instance_reg, temp_reg);

    // If this allocation is traced, program will jump to failure path
    // (i.e. the allocation stub) which will allocate the object and trace the
    // allocation call site.
    NOT_IN_PRODUCT(MaybeTraceAllocation(cid, failure, temp_reg));

    // Successfully allocated the object, now update top to point to
    // next object start and store the class in the class field of object.
    sw(instance_reg, Address(THR, target::Thread::top_offset()));

    ASSERT(instance_size >= kHeapObjectTag);
    AddImmediate(instance_reg, -instance_size + kHeapObjectTag);

    const uword tags = target::MakeTagWordForNewSpaceObject(cid, instance_size);
    LoadImmediate(temp_reg, tags);
    InitializeHeader(temp_reg, instance_reg);
  } else {
    b(failure);
  }
}

void Assembler::TryAllocateArray(intptr_t cid,
                                 intptr_t instance_size,
                                 Label* failure,
                                 Register instance,
                                 Register end_address,
                                 Register temp1,
                                 Register temp2) {
  if (FLAG_inline_alloc &&
      target::Heap::IsAllocatableInNewSpace(instance_size)) {
    // If this allocation is traced, program will jump to failure path
    // (i.e. the allocation stub) which will allocate the object and trace the
    // allocation call site.
    NOT_IN_PRODUCT(MaybeTraceAllocation(cid, failure, temp1));
    // Potential new object start.
    lw(instance, Address(THR, target::Thread::top_offset()));
    // Potential next object start.
    AddImmediate(end_address, instance, instance_size);
    // Branch on unsigned overflow.
    BranchUnsignedLess(end_address, instance, failure);

    // Check if the allocation fits into the remaining space.
    // instance: potential new object start, /* inline_isolate = */ false.
    // end_address: potential next object start.
    lw(temp2, Address(THR, target::Thread::end_offset()));
    BranchUnsignedGreaterEqual(end_address, temp2, failure);
    CheckAllocationCanary(instance, temp2);

    // Successfully allocated the object(s), now update top to point to
    // next object start and initialize the object.
    sw(end_address, Address(THR, target::Thread::top_offset()));
    addiu(instance, instance, Immediate(kHeapObjectTag));
    LoadImmediate(temp1, instance_size);

    // Initialize the tags.
    // instance: new object start as a tagged pointer.
    const uword tags = target::MakeTagWordForNewSpaceObject(cid, instance_size);
    LoadImmediate(temp1, tags);
    InitializeHeader(temp1, instance);  // Store tags.
  } else {
    b(failure);
  }
}

void Assembler::CopyMemoryWords(Register src,
                                Register dst,
                                Register size,
                                Register temp) {
  Label loop, done;
  beq(size, ZR, &done);
  Bind(&loop);
  lw(temp, Address(src));
  AddImmediate(src, src, target::kWordSize);
  sw(temp, Address(dst));
  AddImmediate(dst, dst, target::kWordSize);
  AddImmediate(size, size, -target::kWordSize);
  bne(size, ZR, &loop);
  Bind(&done);
}

void Assembler::TransitionGeneratedToNative(Register destination_address,
                                            Register exit_frame_fp,
                                            Register exit_through_ffi,
                                            Register tmp1,
                                            bool enter_safepoint) {
  // Save exit frame information to enable stack walking.
  sw(exit_frame_fp,
     Address(THR, target::Thread::top_exit_frame_info_offset()));

  sw(exit_through_ffi,
     Address(THR, target::Thread::exit_through_ffi_offset()));
  Register tmp2 = exit_through_ffi;

  VerifyInGenerated(tmp1);
  // Mark that the thread is executing native code.
  sw(destination_address, Address(THR, target::Thread::vm_tag_offset()));
  LoadImmediate(tmp1, target::Thread::native_execution_state());
  sw(tmp1, Address(THR, target::Thread::execution_state_offset()));

  if (enter_safepoint) {
    EnterFullSafepoint(tmp1, tmp2);
  }
}

void Assembler::TransitionNativeToGenerated(Register addr,
                                            Register state,
                                            bool exit_safepoint,
                                            bool set_tag) {
  if (exit_safepoint) {
    ExitFullSafepoint(addr, state);
  } else {
#if defined(DEBUG)
    // Ensure we've already left the safepoint.
    ASSERT(target::Thread::native_safepoint_state_acquired() != 0);
    LoadImmediate(state, target::Thread::native_safepoint_state_acquired());
    lw(TMP, Address(THR, target::Thread::safepoint_state_offset()));
    and_(TMP, TMP, state);
    Label ok;
    BranchEqual(TMP, ZR, &ok);
    Breakpoint();
    Bind(&ok);
#endif
  }

  VerifyNotInGenerated(state);
  // Mark that the thread is executing Dart code.
  if (set_tag) {
    LoadImmediate(state, target::Thread::vm_tag_dart_id());
    sw(state, Address(THR, target::Thread::vm_tag_offset()));
  }
  LoadImmediate(state, target::Thread::generated_execution_state());
  sw(state, Address(THR, target::Thread::execution_state_offset()));

  // Reset exit frame information in Isolate's mutator thread structure.
  sw(ZR, Address(THR, target::Thread::top_exit_frame_info_offset()));
  sw(ZR, Address(THR, target::Thread::exit_through_ffi_offset()));
}

void Assembler::VerifyInGenerated(Register scratch) {
#if defined(DEBUG)
  // Verify the thread is in generated.
  Comment("VerifyInGenerated");
  lw(scratch, Address(THR, target::Thread::execution_state_offset()));
  Label ok;
  CompareImmediate(scratch, target::Thread::generated_execution_state());
  BranchIf(EQUAL, &ok);
  Breakpoint();
  Bind(&ok);
#endif
}

void Assembler::VerifyNotInGenerated(Register scratch) {
#if defined(DEBUG)
  // Verify the thread is in native or VM.
  Comment("VerifyNotInGenerated");
  lw(scratch, Address(THR, target::Thread::execution_state_offset()));
  CompareImmediate(scratch, target::Thread::generated_execution_state());
  Label ok;
  BranchIf(NOT_EQUAL, &ok, Assembler::kNearJump);
  Breakpoint();
  Bind(&ok);
#endif
}

void Assembler::ReserveAlignedFrameSpace(intptr_t frame_space) {
  ASSERT(!in_delay_slot_);
  // Reserve space for arguments and align frame before entering
  // the C++ world.
  AddImmediate(SP, -frame_space);
  if (OS::ActivationFrameAlignment() > 1) {
    LoadImmediate(TMP, ~(OS::ActivationFrameAlignment() - 1));
    and_(SP, SP, TMP);
  }
}

void Assembler::EmitEntryFrameVerification(Register scratch) {
#if defined(DEBUG)
  Label done;
  ASSERT(!constant_pool_allowed());
  LoadImmediate(scratch, target::frame_layout.exit_link_slot_from_entry_fp *
                             target::kWordSize);
  addu(scratch, scratch, FPREG);
  BranchEqual(scratch, SPREG, &done);

  Breakpoint();

  Bind(&done);
#endif
}

void Assembler::PushObject(const Object& object) {
  ASSERT(IsOriginalObject(object));
  ASSERT(!in_delay_slot_);
  LoadObject(TMP, object);
  Push(TMP);
}

void Assembler::CompareObject(Register reg, const Object& object) {
  ASSERT(IsOriginalObject(object));
  if (IsSameObject(compiler::NullObject(), object)) {
    LoadObject(TMP, compiler::NullObject());
    CompareObjectRegisters(reg, TMP);
  } else if (target::IsSmi(object)) {
    CompareImmediate(reg, target::ToRawSmi(object), kObjectBytes);
  } else {
    LoadObject(TMP, object);
    CompareObjectRegisters(reg, TMP);
  }
}

void Assembler::ExtractClassIdFromTags(Register result,
                                       Register tags) {
  ASSERT(target::UntaggedObject::kClassIdTagPos == 12);
  ASSERT(target::UntaggedObject::kClassIdTagSize == 20);
  srl(result, tags, target::UntaggedObject::kClassIdTagPos);
}

void Assembler::ExtractInstanceSizeFromTags(Register result, Register tags) {
  ASSERT(target::UntaggedObject::kSizeTagPos == 8);
  ASSERT(target::UntaggedObject::kSizeTagSize == 4);
  srl(result, tags, target::UntaggedObject::kSizeTagPos -
    target::ObjectAlignment::kObjectAlignmentLog2);
  andi(result, result, Immediate(Utils::NBitMask(target::UntaggedObject::kSizeTagSize)
          << target::ObjectAlignment::kObjectAlignmentLog2));
}

}  // namespace compiler
}  // namespace dart

#endif  // defined TARGET_ARCH_MIPS

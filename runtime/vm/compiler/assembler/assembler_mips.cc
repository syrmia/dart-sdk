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

static bool CanEncodeBranchOffset(int32_t offset) {
  ASSERT(Utils::IsAligned(offset, 4));
  return Utils::IsInt(18, offset);
}

int32_t Assembler::EncodeBranchOffset(int32_t offset, int32_t instr) {
  ASSERT(Utils::IsAligned(offset, 4));
  ASSERT(Utils::IsInt(18, offset));

  // Properly preserve only the bits supported in the instruction.
  offset >>= 2;
  offset &= kBranchOffsetMask;
  return (instr & ~kBranchOffsetMask) | offset;
}

int32_t Assembler::DecodeBranchOffset(int32_t instr) {
  // Sign-extend, left-shift by 2.
  return (((instr & kBranchOffsetMask) << 16) >> 14);
}

static int32_t DecodeLoadImmediate(int32_t ori_instr, int32_t lui_instr) {
  return (((lui_instr & kBranchOffsetMask) << 16) |
          (ori_instr & kBranchOffsetMask));
}

static int32_t EncodeLoadImmediate(int32_t dest, int32_t instr) {
  return ((instr & ~kBranchOffsetMask) | (dest & kBranchOffsetMask));
}

class PatchFarJump : public AssemblerFixup {
 public:
  PatchFarJump() {}

  void Process(const MemoryRegion& region, intptr_t position) {
    const int32_t high = region.Load<int32_t>(position);
    const int32_t low = region.Load<int32_t>(position + Instr::kInstrSize);
    const int32_t offset = DecodeLoadImmediate(low, high);
    const int32_t dest = region.start() + offset;

    if ((Instr::At(reinterpret_cast<uword>(&high))->OpcodeField() == LUI) &&
        (Instr::At(reinterpret_cast<uword>(&low))->OpcodeField() == ORI)) {
      // Change the offset to the absolute value.
      const int32_t encoded_low =
          EncodeLoadImmediate(dest & kBranchOffsetMask, low);
      const int32_t encoded_high = EncodeLoadImmediate(dest >> 16, high);

      region.Store<int32_t>(position, encoded_high);
      region.Store<int32_t>(position + Instr::kInstrSize, encoded_low);
      return;
    }
    // If the offset loading instructions aren't there, we must have replaced
    // the far branch with a near one, and so these instructions should be NOPs.
    ASSERT((high == Instr::kNopInstruction) && (low == Instr::kNopInstruction));
  }

  virtual bool IsPointerOffset() const { return false; }
};

void Assembler::EmitFarJump(int32_t offset, bool link) {
  ASSERT(!in_delay_slot_);
  ASSERT(use_far_branches());
  const uint16_t low = Utils::Low16Bits(offset);
  const uint16_t high = Utils::High16Bits(offset);
  buffer_.EmitFixup(new PatchFarJump());
  lui(T9, Immediate(high));
  ori(T9, T9, Immediate(low));
  if (link) {
    EmitRType(SPECIAL, T9, R0, RA, 0, JALR);
  } else {
    EmitRType(SPECIAL, T9, R0, R0, 0, JR);
  }
}

static Opcode OppositeBranchOpcode(Opcode b) {
  switch (b) {
    case BEQ:
      return BNE;
    case BNE:
      return BEQ;
    case BGTZ:
      return BLEZ;
    case BLEZ:
      return BGTZ;
    case BEQL:
      return BNEL;
    case BNEL:
      return BEQL;
    case BGTZL:
      return BLEZL;
    case BLEZL:
      return BGTZL;
    default:
      UNREACHABLE();
      break;
  }
  return BNE;
}

void Assembler::EmitFarBranch(Opcode b,
                              Register rs,
                              Register rt,
                              int32_t offset) {
  ASSERT(!in_delay_slot_);
  EmitIType(b, rs, rt, 4);
  nop();
  EmitFarJump(offset, false);
}

static RtRegImm OppositeBranchNoLink(RtRegImm b) {
  switch (b) {
    case BLTZ:
      return BGEZ;
    case BGEZ:
      return BLTZ;
    case BLTZAL:
      return BGEZ;
    case BGEZAL:
      return BLTZ;
    default:
      UNREACHABLE();
      break;
  }
  return BLTZ;
}

void Assembler::EmitFarRegImmBranch(RtRegImm b, Register rs, int32_t offset) {
  ASSERT(!in_delay_slot_);
  EmitRegImmType(REGIMM, rs, b, 4);
  nop();
  EmitFarJump(offset, (b == BLTZAL) || (b == BGEZAL));
}

void Assembler::EmitFarFpuBranch(bool kind, int32_t offset) {
  ASSERT(!in_delay_slot_);
  const uint32_t b16 = kind ? (1 << 16) : 0;
  Emit(COP1 << kOpcodeShift | COP1_BC << kCop1SubShift | b16 | 4);
  nop();
  EmitFarJump(offset, false);
}

void Assembler::EmitBranch(Opcode b, Register rs, Register rt, Label* label) {
  ASSERT(!in_delay_slot_);
  if (label->IsBound()) {
    // Relative destination from an instruction after the branch.
    const int32_t dest =
        label->Position() - (buffer_.Size() + Instr::kInstrSize);
    if (use_far_branches() && !CanEncodeBranchOffset(dest)) {
      EmitFarBranch(OppositeBranchOpcode(b), rs, rt, label->Position());
    } else {
      BailoutIfInvalidBranchOffset(dest);
      const uint16_t dest_off = EncodeBranchOffset(dest, 0);
      EmitIType(b, rs, rt, dest_off);
    }
  } else {
    const intptr_t position = buffer_.Size();
    if (use_far_branches()) {
      const uint32_t dest_off = label->position_;
      EmitFarBranch(b, rs, rt, dest_off);
    } else {
      BailoutIfInvalidBranchOffset(label->position_);
      const uint16_t dest_off = EncodeBranchOffset(label->position_, 0);
      EmitIType(b, rs, rt, dest_off);
    }
    label->LinkTo(position);
  }
}

void Assembler::BailoutIfInvalidBranchOffset(int32_t offset) {
  if (!CanEncodeBranchOffset(offset)) {
    ASSERT(!use_far_branches());
    BailoutWithBranchOffsetError();
  }
}

void Assembler::EmitRegImmBranch(RtRegImm b, Register rs, Label* label) {
  ASSERT(!in_delay_slot_);
  if (label->IsBound()) {
    // Relative destination from an instruction after the branch.
    const int32_t dest =
        label->Position() - (buffer_.Size() + Instr::kInstrSize);
    if (use_far_branches() && !CanEncodeBranchOffset(dest)) {
      EmitFarRegImmBranch(OppositeBranchNoLink(b), rs, label->Position());
    } else {
      BailoutIfInvalidBranchOffset(dest);
      const uint16_t dest_off = EncodeBranchOffset(dest, 0);
      EmitRegImmType(REGIMM, rs, b, dest_off);
    }
  } else {
    const intptr_t position = buffer_.Size();
    if (use_far_branches()) {
      const uint32_t dest_off = label->position_;
      EmitFarRegImmBranch(b, rs, dest_off);
    } else {
      BailoutIfInvalidBranchOffset(label->position_);
      const uint16_t dest_off = EncodeBranchOffset(label->position_, 0);
      EmitRegImmType(REGIMM, rs, b, dest_off);
    }
    label->LinkTo(position);
  }
}

void Assembler::EmitFpuBranch(bool kind, Label* label) {
  ASSERT(!in_delay_slot_);
  const int32_t b16 = kind ? (1 << 16) : 0;  // Bit 16 set for branch on true.
  if (label->IsBound()) {
    // Relative destination from an instruction after the branch.
    const int32_t dest =
        label->Position() - (buffer_.Size() + Instr::kInstrSize);
    if (use_far_branches() && !CanEncodeBranchOffset(dest)) {
      EmitFarFpuBranch(kind, label->Position());
    } else {
      BailoutIfInvalidBranchOffset(dest);
      const uint16_t dest_off = EncodeBranchOffset(dest, 0);
      Emit(COP1 << kOpcodeShift | COP1_BC << kCop1SubShift | b16 | dest_off);
    }
  } else {
    const intptr_t position = buffer_.Size();
    if (use_far_branches()) {
      const uint32_t dest_off = label->position_;
      EmitFarFpuBranch(kind, dest_off);
    } else {
      BailoutIfInvalidBranchOffset(label->position_);
      const uint16_t dest_off = EncodeBranchOffset(label->position_, 0);
      Emit(COP1 << kOpcodeShift | COP1_BC << kCop1SubShift | b16 | dest_off);
    }
    label->LinkTo(position);
  }
}

static int32_t FlipBranchInstruction(int32_t instr) {
  Instr* i = Instr::At(reinterpret_cast<uword>(&instr));
  if (i->OpcodeField() == REGIMM) {
    RtRegImm b = OppositeBranchNoLink(i->RegImmFnField());
    i->SetRegImmFnField(b);
    return i->InstructionBits();
  } else if (i->OpcodeField() == COP1) {
    return instr ^ (1 << 16);
  }
  Opcode b = OppositeBranchOpcode(i->OpcodeField());
  i->SetOpcodeField(b);
  return i->InstructionBits();
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

Address Assembler::ElementAddressForIntIndex(bool is_external,
                                            intptr_t cid,
                                            intptr_t index_scale,
                                            Register array,
                                            intptr_t index) const {
  const int64_t offset =
      static_cast<int64_t>(index) * index_scale +
      (is_external ? 0 : (target::Instance::DataOffsetFor(cid) - kHeapObjectTag));
  ASSERT(Utils::IsInt(32, offset));
  ASSERT(Address::CanHoldOffset(offset));
  return Address(array, static_cast<int32_t>(offset));
}

void Assembler::LoadElementAddressForIntIndex(Register address,
                                              bool is_external,
                                              intptr_t cid,
                                              intptr_t index_scale,
                                              Register array,
                                              intptr_t index) {
  const int64_t offset =
      static_cast<int64_t>(index) * index_scale +
      (is_external ? 0 : (target::Instance::DataOffsetFor(cid) - kHeapObjectTag));
  ASSERT(Utils::IsInt(32, offset));
  AddImmediate(address, array, offset);
}

Address Assembler::ElementAddressForRegIndex(bool is_load,
                                            bool is_external,
                                            intptr_t cid,
                                            intptr_t index_scale,
                                            bool index_unboxed,
                                            Register array,
                                            Register index) {
  // Note that index is expected smi-tagged, (i.e, LSL 1) for all arrays.
  const intptr_t boxing_shift = index_unboxed ? 0 : -kSmiTagShift;
  const intptr_t shift = Utils::ShiftForPowerOfTwo(index_scale) + boxing_shift;
  const int32_t offset =
      is_external ? 0 : (target::Instance::DataOffsetFor(cid) - kHeapObjectTag);
  ASSERT(array != TMP);
  ASSERT(index != TMP);
  const Register base = is_load ? TMP : index;
  if (shift < 0) {
    ASSERT(shift == -1);
    sra(TMP, index, 1);
    addu(base, array, TMP);
  } else if (shift == 0) {
    addu(base, array, index);
  } else {
    sll(TMP, index, shift);
    addu(base, array, TMP);
  }
  ASSERT(Address::CanHoldOffset(offset));
  return Address(base, offset);
}

void Assembler::LoadElementAddressForRegIndex(Register address,
                                              bool is_load,
                                              bool is_external,
                                              intptr_t cid,
                                              intptr_t index_scale,
                                              bool index_unboxed,
                                              Register array,
                                              Register index) {
  // Note that index is expected smi-tagged, (i.e, LSL 1) for all arrays.
  const intptr_t boxing_shift = index_unboxed ? 0 : -kSmiTagShift;
  const intptr_t shift = Utils::ShiftForPowerOfTwo(index_scale) + boxing_shift;
  const int32_t offset =
      is_external ? 0 : (target::Instance::DataOffsetFor(cid) - kHeapObjectTag);
  if (shift < 0) {
    ASSERT(shift == -1);
    sra(address, index, 1);
    addu(address, array, address);
  } else if (shift == 0) {
    addu(address, array, index);
  } else {
    sll(address, index, shift);
    addu(address, array, address);
  }
  if (offset != 0) {
    AddImmediate(address, offset);
  }
}

void Assembler::LoadHalfWordUnaligned(Register dst,
                                      Register addr,
                                      Register tmp) {
  ASSERT(dst != addr);
  lbu(dst, Address(addr, 0));
  lb(tmp, Address(addr, 1));
  sll(tmp, tmp, 8);
  or_(dst, dst, tmp);
}

void Assembler::LoadHalfWordUnsignedUnaligned(Register dst,
                                              Register addr,
                                              Register tmp) {
  ASSERT(dst != addr);
  lbu(dst, Address(addr, 0));
  lbu(tmp, Address(addr, 1));
  sll(tmp, tmp, 8);
  or_(dst, dst, tmp);
}

void Assembler::StoreHalfWordUnaligned(Register src,
                                       Register addr,
                                       Register tmp) {
  sb(src, Address(addr, 0));
  srl(tmp, src, 8);
  sb(tmp, Address(addr, 1));
}

void Assembler::LoadWordUnaligned(Register dst, Register addr, Register tmp) {
  ASSERT(dst != addr);
  lbu(dst, Address(addr, 0));
  lbu(tmp, Address(addr, 1));
  sll(tmp, tmp, 8);
  or_(dst, dst, tmp);
  lbu(tmp, Address(addr, 2));
  sll(tmp, tmp, 16);
  or_(dst, dst, tmp);
  lbu(tmp, Address(addr, 3));
  sll(tmp, tmp, 24);
  or_(dst, dst, tmp);
}

void Assembler::StoreWordUnaligned(Register src, Register addr, Register tmp) {
  sb(src, Address(addr, 0));
  srl(tmp, src, 8);
  sb(tmp, Address(addr, 1));
  srl(tmp, src, 16);
  sb(tmp, Address(addr, 2));
  srl(tmp, src, 24);
  sb(tmp, Address(addr, 3));
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

Register Assembler::LoadConditionOperand(Register rd,
                                const Object& operand,
                                int16_t* imm) {
  if (target::IsSmi(operand)) {
    const int32_t val = target::ToRawSmi(operand);
    if (val == 0) {
      return ZR;
    }
  }
  LoadObject(rd, operand);
  return rd;
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

void Assembler::SubtractBranchOverflow(Register rd,
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
    subu(rd, rs1, rs2);   // rs1, rs2 destroyed
    xor_(CMPRES1, CMPRES1, rd);  // CMPRES1 negative if sign changed
    bltz(CMPRES1, overflow);
  } else if (rs1 == rs2) {
    ASSERT(rd != rs1);
    ASSERT(rd != rs2);
    subu(rd, rs1, rs2);
    xor_(CMPRES1, rd, rs1);  // CMPRES1 negative if sign changed
    bltz(CMPRES1, overflow);
  } else if (rd == rs1) {
    ASSERT(rs1 != rs2);
    slti(CMPRES1, rs1, Immediate(0));
    subu(rd, rs1, rs2);  // rs1 destroyed
    slt(CMPRES2, rd, rs2);
    bne(CMPRES1, CMPRES2, overflow);
  } else if (rd == rs2) {
    ASSERT(rs1 != rs2);
    slti(CMPRES1, rs2, Immediate(0));
    subu(rd, rs1, rs2);  // rs2 destroyed
    slt(CMPRES2, rd, rs1);
    bne(CMPRES1, CMPRES2, overflow);
  } else {
    subu(rd, rs1, rs2);
    slti(CMPRES1, rs2, Immediate(0));
    slt(CMPRES2, rs1, rd);
    bne(CMPRES1, CMPRES2, overflow);
  }
}

bool Assembler::AddressCanHoldConstantIndex(const Object& constant,
                                            bool is_load,
                                            bool is_external,
                                            intptr_t cid,
                                            intptr_t index_scale,
                                            bool* needs_base) {
  if (!IsSafeSmi(constant)) {
    return false;
  }
  const int64_t index = target::SmiValue(constant);
  const intptr_t offset_base =
      (is_external ? 0
                   : (target::Instance::DataOffsetFor(cid) - kHeapObjectTag));
  const int64_t offset = index * index_scale + offset_base;
  if (!Utils::IsInt(32, offset)) {
    return false;
  }
  return Address::CanHoldOffset(static_cast<int32_t>(offset));
}

void Assembler::AddImmediateBranchOverflow(Register rd,
                                           Register rs1,
                                           int32_t imm,
                                           Label* overflow) {
  ASSERT(rd != CMPRES1);
  if (rd == rs1) {
    mov(CMPRES1, rs1);
    AddImmediate(rd, rs1, imm);
    if (imm > 0) {
      BranchSignedLess(rd, CMPRES1, overflow);
    } else if (imm < 0) {
      BranchSignedGreater(rd, CMPRES1, overflow);
    }
  } else {
    AddImmediate(rd, rs1, imm);
    if (imm > 0) {
      BranchSignedLess(rd, rs1, overflow);
    } else if (imm < 0) {
      BranchSignedGreater(rd, rs1, overflow);
    }
  }
}

void Assembler::SubtractImmediateBranchOverflow(Register rd,
                                                Register rs1,
                                                int32_t imm,
                                                Label* overflow) {
  AddImmediateBranchOverflow(rd, rs1, -imm, overflow);
}

void Assembler::Bind(Label* label) {
  ASSERT(!label->IsBound());
  intptr_t bound_pc = buffer_.Size();

  while (label->IsLinked()) {
    int32_t position = label->Position();
    int32_t dest = bound_pc - (position + Instr::kInstrSize);

    if (use_far_branches() && !CanEncodeBranchOffset(dest)) {
      // Far branches are enabled and we can't encode the branch offset.

      // Grab the branch instruction. We'll need to flip it later.
      const int32_t branch = buffer_.Load<int32_t>(position);

      // Grab instructions that load the offset.
      const int32_t high =
          buffer_.Load<int32_t>(position + 2 * Instr::kInstrSize);
      const int32_t low =
          buffer_.Load<int32_t>(position + 3 * Instr::kInstrSize);

      // Change from relative to the branch to relative to the assembler buffer.
      dest = buffer_.Size();
      const int32_t encoded_low =
          EncodeLoadImmediate(dest & kBranchOffsetMask, low);
      const int32_t encoded_high = EncodeLoadImmediate(dest >> 16, high);

      // Skip the unconditional far jump if the test fails by flipping the
      // sense of the branch instruction.
      buffer_.Store<int32_t>(position, FlipBranchInstruction(branch));
      buffer_.Store<int32_t>(position + 2 * Instr::kInstrSize, encoded_high);
      buffer_.Store<int32_t>(position + 3 * Instr::kInstrSize, encoded_low);
      label->position_ = DecodeLoadImmediate(low, high);
    } else if (use_far_branches() && CanEncodeBranchOffset(dest)) {
      // We assembled a far branch, but we don't need it. Replace with a near
      // branch.

      // Grab the link to the next branch.
      const int32_t high =
          buffer_.Load<int32_t>(position + 2 * Instr::kInstrSize);
      const int32_t low =
          buffer_.Load<int32_t>(position + 3 * Instr::kInstrSize);

      // Grab the original branch instruction.
      int32_t branch = buffer_.Load<int32_t>(position);

      // Clear out the old (far) branch.
      for (int i = 0; i < 5; i++) {
        buffer_.Store<int32_t>(position + i * Instr::kInstrSize,
                               Instr::kNopInstruction);
      }

      // Calculate the new offset.
      dest = dest - 4 * Instr::kInstrSize;
      BailoutIfInvalidBranchOffset(dest);
      const int32_t encoded = EncodeBranchOffset(dest, branch);
      buffer_.Store<int32_t>(position + 4 * Instr::kInstrSize, encoded);
      label->position_ = DecodeLoadImmediate(low, high);
    } else {
      const int32_t next = buffer_.Load<int32_t>(position);
      BailoutIfInvalidBranchOffset(dest);
      const int32_t encoded = EncodeBranchOffset(dest, next);
      buffer_.Store<int32_t>(position, encoded);
      label->position_ = DecodeBranchOffset(next);
    }
  }
  label->BindTo(bound_pc);
  delay_slot_available_ = false;
}

void Assembler::PushNativeCalleeSavedRegisters() {
  RegisterSet regs(kAbiPreservedCpuRegs, kAbiPreservedFpuRegs);
  intptr_t size = (regs.CpuRegisterCount() * target::kWordSize) +
                  (regs.FpuRegisterCount() * sizeof(double));
  AddImmediate(SP, SP, -size);
  intptr_t offset = 0;
  for (intptr_t i = 0; i < kNumberOfFpuRegisters; i++) {
    FpuRegister reg = static_cast<FpuRegister>(i);
    if (regs.ContainsFpuRegister(reg)) {
      sdc1(reg, Address(SP, offset));
      offset += sizeof(double);
    }
  }
  for (intptr_t i = 0; i < kNumberOfCpuRegisters; i++) {
    Register reg = static_cast<Register>(i);
    if (regs.ContainsRegister(reg)) {
      sw(reg, Address(SP, offset));
      offset += target::kWordSize;
    }
  }
  ASSERT(offset == size);
}

void Assembler::PopNativeCalleeSavedRegisters() {
  RegisterSet regs(kAbiPreservedCpuRegs, kAbiPreservedFpuRegs);
  intptr_t size = (regs.CpuRegisterCount() * target::kWordSize) +
                  (regs.FpuRegisterCount() * sizeof(double));
  intptr_t offset = 0;
  for (intptr_t i = 0; i < kNumberOfFpuRegisters; i++) {
    FpuRegister reg = static_cast<FpuRegister>(i);
    if (regs.ContainsFpuRegister(reg)) {
      ldc1(reg, Address(SP, offset));
      offset += sizeof(double);
    }
  }
  for (intptr_t i = 0; i < kNumberOfCpuRegisters; i++) {
    Register reg = static_cast<Register>(i);
    if (regs.ContainsRegister(reg)) {
      lw(reg, Address(SP, offset));
      offset += target::kWordSize;
    }
  }
  ASSERT(offset == size);
  AddImmediate(SP, SP, size);
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

void Assembler::LoadNativeEntry(Register rd,
                                const ExternalLabel* label,
                                ObjectPoolBuilderEntry::Patchability patchable) {
  const intptr_t index =
      object_pool_builder().FindNativeFunction(label, patchable);
  LoadWordFromPoolIndex(rd, index);
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
  ASSERT_EQUAL(target::UntaggedObject::kClassIdTagPos, 12);
  ASSERT_EQUAL(target::UntaggedObject::kClassIdTagSize, 20);
  lw(result, FieldAddress(object, target::Object::tags_offset()));
  ExtractClassIdFromTags(result, result);
}

void Assembler::LoadClassById(Register result, Register class_id) {
  ASSERT(!in_delay_slot_);
  ASSERT(result != class_id);
  LoadIsolateGroup(result);
  const intptr_t offset = target::IsolateGroup::cached_class_table_table_offset();
  LoadFromOffset(result, result, offset);
  sll(TMP, class_id, 2);
  addu(result, result, TMP);
  lw(result, Address(result));
}

void Assembler::LoadClass(Register result, Register object) {
  ASSERT(!in_delay_slot_);
  ASSERT(TMP != result);
  LoadClassId(TMP, object);
  LoadClassById(result, TMP);
}

void Assembler::CompareClassId(Register object,
                               intptr_t class_id,
                               Register scratch) {
  ASSERT(scratch != kNoRegister);
  LoadClassId(scratch, object);
  CompareImmediate(scratch, class_id);
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

void Assembler::LoadSImmediate(DRegister reg, float imms) {
  int32_t imm = bit_cast<int32_t, float>(imms);
  ASSERT(constant_pool_allowed());
  intptr_t index = object_pool_builder().FindImmediate(imm);
  intptr_t offset = target::ObjectPool::element_offset(index) - kHeapObjectTag;
  LoadSFromOffset(reg, PP, offset);
}

void Assembler::LoadDImmediate(DRegister reg, double immd, Register scratch) {
  ASSERT(scratch != TMP);
  int64_t imm = bit_cast<int64_t, double>(immd);
  if (constant_pool_allowed()) {
    intptr_t index = object_pool_builder().FindImmediate64(imm);
    intptr_t offset = target::ObjectPool::element_offset(index) - kHeapObjectTag;
    LoadDFromOffset(reg, PP, offset);
  } else {
    ASSERT(scratch != kNoRegister);
    LoadImmediate(TMP, Utils::Low32Bits(imm));
    LoadImmediate(scratch, Utils::High32Bits(imm));
    mtc1(TMP, static_cast<FRegister>(reg*2));
    mtc1(scratch, static_cast<FRegister>(reg*2 + 1));
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

void Assembler::AdduDetectOverflow(Register rd,
                                   Register rs,
                                   Register rt,
                                   Register ro,
                                   Register scratch) {
  ASSERT(!in_delay_slot_);
  ASSERT(rd != ro);
  ASSERT(rd != TMP);
  ASSERT(ro != TMP);
  ASSERT(ro != rs);
  ASSERT(ro != rt);

  if ((rs == rt) && (rd == rs)) {
    ASSERT(scratch != kNoRegister);
    ASSERT(scratch != TMP);
    ASSERT(rd != scratch);
    ASSERT(ro != scratch);
    ASSERT(rs != scratch);
    ASSERT(rt != scratch);
    mov(scratch, rt);
    rt = scratch;
  }

  if (rd == rs) {
    mov(TMP, rs);        // Preserve rs.
    addu(rd, rs, rt);    // rs is overwritten.
    xor_(TMP, rd, TMP);  // Original rs.
    xor_(ro, rd, rt);
    and_(ro, ro, TMP);
  } else if (rd == rt) {
    mov(TMP, rt);        // Preserve rt.
    addu(rd, rs, rt);    // rt is overwritten.
    xor_(TMP, rd, TMP);  // Original rt.
    xor_(ro, rd, rs);
    and_(ro, ro, TMP);
  } else {
    addu(rd, rs, rt);
    xor_(ro, rd, rs);
    xor_(TMP, rd, rt);
    and_(ro, TMP, ro);
  }
}

void Assembler::SubuDetectOverflow(Register rd,
                                   Register rs,
                                   Register rt,
                                   Register ro) {
  ASSERT(!in_delay_slot_);
  ASSERT(rd != ro);
  ASSERT(rd != TMP);
  ASSERT(ro != TMP);
  ASSERT(ro != rs);
  ASSERT(ro != rt);
  ASSERT(rs != TMP);
  ASSERT(rt != TMP);

  // This happens with some crankshaft code. Since Subu works fine if
  // left == right, let's not make that restriction here.
  if (rs == rt) {
    mov(rd, ZR);
    mov(ro, ZR);
    return;
  }

  if (rd == rs) {
    mov(TMP, rs);        // Preserve left.
    subu(rd, rs, rt);    // Left is overwritten.
    xor_(ro, rd, TMP);   // scratch is original left.
    xor_(TMP, TMP, rs);  // scratch is original left.
    and_(ro, TMP, ro);
  } else if (rd == rt) {
    mov(TMP, rt);      // Preserve right.
    subu(rd, rs, rt);  // Right is overwritten.
    xor_(ro, rd, rs);
    xor_(TMP, rs, TMP);  // Original right.
    and_(ro, TMP, ro);
  } else {
    subu(rd, rs, rt);
    xor_(ro, rd, rs);
    xor_(TMP, rs, rt);
    and_(ro, TMP, ro);
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

void Assembler::AndRegisters(Register dst, Register src1, Register src2) {
  ASSERT(src1 != src2);
  if (src2 == kNoRegister) {
    src2 = dst;
  }
  and_(dst, src2, src1);
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

// Stores a non-tagged value into a heap object.
void Assembler::StoreInternalPointer(Register object,
                                     const Address& dest,
                                     Register value) {
  sw(value, dest);
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

void Assembler::EnterCFrame(intptr_t frame_space) {
  // Already saved.
  COMPILE_ASSERT(IsCalleeSavedRegister(THR));
  COMPILE_ASSERT(IsCalleeSavedRegister(PP));

  EnterFrame();
  /*A caller reserves four words (16 bytes) at the end of its stack frame for the callee to store its
  arguments, even if the callee takes fewer than four arguments, even if the callee does not
  actually use this space. In other words, if you are a non-leaf function, then you must never
  address 0(sp), 4(sp), 8(sp), or 12(sp)! However, supposing your frame is 32 bytes, then you
  may use 32(sp), 36(sp), 40(sp), and 44(sp) for storing a0, a1, a2, and a3, respectively, even
  though this is in the frame of your caller!*/
  ReserveAlignedFrameSpace(frame_space + 4 * target::kWordSize);
}

void Assembler::LeaveCFrame() {
  LeaveFrame();
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

// On entry to a function compiled for OSR, the caller's frame pointer, the
// stack locals, and any copied parameters are already in place.  The frame
// pointer is already set up.  The PC marker is not correct for the
// optimized function and there may be extra space for spill slots to
// allocate. We must also set up the pool pointer for the function.
void Assembler::EnterOsrFrame(intptr_t extra_size) {
  ASSERT(!in_delay_slot_);
  ASSERT(!constant_pool_allowed());
  Comment("EnterOsrFrame");

  // Restore return address.
  lw(RA, Address(FP, 1 * target::kWordSize));

  // Load the pool pointer. offset has already been subtracted from temp.
  RestoreCodePointer();
  LoadPoolPointer();

  // Reserve space for locals.
  AddImmediate(SP, -extra_size);
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

// Preserves object and value registers.
void Assembler::StoreIntoObjectFilterNoSmi(Register object,
                                           Register value,
                                           Label* no_update) {
  ASSERT(!in_delay_slot_);
  COMPILE_ASSERT((target::ObjectAlignment::kNewObjectAlignmentOffset == target::kWordSize) &&
                 (target::ObjectAlignment::kOldObjectAlignmentOffset == 0));

  // Write-barrier triggers if the value is in the new space (has bit set) and
  // the object is in the old space (has bit cleared).
  // To check that, we compute value & ~object and skip the write barrier
  // if the bit is not set. We can't destroy the object.
  nor(TMP, ZR, object);
  and_(TMP, value, TMP);
  andi(CMPRES1, TMP, Immediate(target::ObjectAlignment::kNewObjectAlignmentOffset));
  beq(CMPRES1, ZR, no_update);
}

// Preserves object and value registers.
void Assembler::StoreIntoObjectFilter(Register object,
                                      Register value,
                                      Label* no_update) {
  ASSERT(!in_delay_slot_);
  // For the value we are only interested in the new/old bit and the tag bit.
  // And the new bit with the tag bit. The resulting bit will be 0 for a Smi.
  sll(TMP, value, target::ObjectAlignment::kObjectAlignmentLog2 - 1);
  and_(TMP, value, TMP);
  // And the result with the negated space bit of the object.
  nor(CMPRES1, ZR, object);
  and_(TMP, TMP, CMPRES1);
  andi(CMPRES1, TMP, Immediate(target::ObjectAlignment::kNewObjectAlignmentOffset));
  beq(CMPRES1, ZR, no_update);
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

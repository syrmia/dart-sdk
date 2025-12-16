// Copyright (c) 2025, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef RUNTIME_VM_ASSEMBLER_MIPS_H_
#define RUNTIME_VM_ASSEMBLER_MIPS_H_

#include "platform/assert.h"
#include "vm/compiler/assembler/assembler_base.h"
#include "vm/constants.h"

// References to documentation in this file refer to:
// "MIPS® Architecture For Programmers Volume I-A:
//   Introduction to the MIPS32® Architecture" in short "VolI-A"
//   available at:
//   https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00082-2B-MIPS32INT-AFP-06.01.pdf
// and
// "MIPS® Architecture For Programmers Volume II-A:
//   The MIPS32® Instruction Set" in short "VolII-A"
//   available at:
//   https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00086-2B-MIPS32BIS-AFP-6.06.pdf
namespace dart {
namespace compiler {

class RegisterSet;

class Immediate : public ValueObject {
 public:
  explicit Immediate(int32_t value) : value_(value) {}

  Immediate(const Immediate& other) : ValueObject(), value_(other.value_) {}
  Immediate& operator=(const Immediate& other) {
    value_ = other.value_;
    return *this;
  }

 private:
  int32_t value_;

  int32_t value() const { return value_; }

  friend class Assembler;
};

class Address : public ValueObject {
 public:
  explicit Address(Register base, int32_t offset = 0)
      : ValueObject(), base_(base), offset_(offset) {}

  // This addressing mode does not exist.
  // MIPS does not support two-register address calculation.
  Address(Register base, Register offset);

  Address(const Address& other)
      : ValueObject(), base_(other.base_), offset_(other.offset_) {}
  Address& operator=(const Address& other) {
    base_ = other.base_;
    offset_ = other.offset_;
    return *this;
  }

  uint32_t encoding() const {
    ASSERT(Utils::IsInt(kImmBits, offset_));
    uint16_t imm_value = static_cast<uint16_t>(offset_);
    return (base_ << kRsShift) | imm_value;
  }

  static bool CanHoldOffset(int32_t offset) {
    return Utils::IsInt(kImmBits, offset);
  }

  Register base() const { return base_; }
  int32_t offset() const { return offset_; }

 private:
  Register base_;
  int32_t offset_;
};

class FieldAddress : public Address {
 public:
  FieldAddress(Register base, int32_t disp)
      : Address(base, disp - kHeapObjectTag) {}

  FieldAddress(const FieldAddress& other) : Address(other) {}

  FieldAddress& operator=(const FieldAddress& other) {
    Address::operator=(other);
    return *this;
  }
};

class Assembler : public AssemblerBase {
 public:
  explicit Assembler(ObjectPoolBuilder* object_pool_builder,
                     intptr_t far_branch_level = 0)
      : AssemblerBase(object_pool_builder),
        use_far_branches_(far_branch_level != 0),
        delay_slot_available_(false),
        in_delay_slot_(false),
        constant_pool_allowed_(false) {
          UNIMPLEMENTED();
  }
  ~Assembler() {}

  void PushRegister(Register r) { Push(r); }
  void PopRegister(Register r) { Pop(r); }

  void PushRegisters(const RegisterSet& registers);
  void PopRegisters(const RegisterSet& registers);

  void PushRegistersInOrder(std::initializer_list<Register> regs);

  void PushRegisterPair(Register r0, Register r1);
  void PopRegisterPair(Register r0, Register r1);

  void PushImmediate(int64_t immediate);

  void PushValueAtOffset(Register base, int32_t offset);

  void Push(Register rt) {
    ASSERT(!in_delay_slot_);
    addiu(SP, SP, Immediate(-target::kWordSize));
    sw(rt, Address(SP));
  }

  void Pop(Register rt) {
    ASSERT(!in_delay_slot_);
    lw(rt, Address(SP));
    addiu(SP, SP, Immediate(target::kWordSize));
  }

  void CompareImmediate(Register rn, int32_t imm, OperandSize sz = kWordBytes) override;
  void TestImmediate(Register rn, int32_t imm, OperandSize sz = kWordBytes);

  void CompareRegisters(Register rn, Register rm);
  void TestRegisters(Register rn, Register rm);

  // A utility to be able to assemble an instruction into the delay slot.
  Assembler* delay_slot() {
    ASSERT(delay_slot_available_);
    ASSERT(buffer_.Load<int32_t>(buffer_.GetPosition() - sizeof(int32_t)) ==
           Instr::kNopInstruction);
    buffer_.Remit<int32_t>();
    delay_slot_available_ = false;
    in_delay_slot_ = true;
    return this;
  }

  // CPU instructions in alphabetical order.
  void addd(DRegister dd, DRegister ds, DRegister dt) {
    // DRegisters start at the even FRegisters.
    FRegister fd = static_cast<FRegister>(dd * 2);
    FRegister fs = static_cast<FRegister>(ds * 2);
    FRegister ft = static_cast<FRegister>(dt * 2);
    EmitFpuRType(COP1, FMT_D, ft, fs, fd, COP1_ADD);
  }

  void addiu(Register rt, Register rs, const Immediate& imm) {
    ASSERT(Utils::IsInt(kImmBits, imm.value()));
    const uint16_t imm_value = static_cast<uint16_t>(imm.value());
    EmitIType(ADDIU, rs, rt, imm_value);
  }

  void addu(Register rd, Register rs, Register rt) {
    EmitRType(SPECIAL, rs, rt, rd, 0, ADDU);
  }

  void add(Register rd, Register rs, Register rt) {
    EmitRType(SPECIAL, rs, rt, rd, 0, ADD);
  }

  void addi(Register rt, Register rs, const Immediate& imm) {
    ASSERT(Utils::IsInt(kImmBits, imm.value()));
    const uint16_t imm_value = static_cast<uint16_t>(imm.value());
    EmitIType(ADDI, rs, rt, imm_value);
  }

  void and_(Register rd, Register rs, Register rt) {
    EmitRType(SPECIAL, rs, rt, rd, 0, AND);
  }

  void andi(Register rt, Register rs, const Immediate& imm) {
    ASSERT(Utils::IsUint(kImmBits, imm.value()));
    const uint16_t imm_value = static_cast<uint16_t>(imm.value());
    EmitIType(ANDI, rs, rt, imm_value);
  }

  // Unconditional branch.
  void b(Label* l) { beq(R0, R0, l); }

  void bal(Label* l) {
    ASSERT(!in_delay_slot_);
    EmitRegImmBranch(BGEZAL, R0, l);
    EmitBranchDelayNop();
  }

  // Branch on floating point false.
  void bc1f(Label* l) {
    EmitFpuBranch(false, l);
    EmitBranchDelayNop();
  }

  // Branch on floating point true.
  void bc1t(Label* l) {
    EmitFpuBranch(true, l);
    EmitBranchDelayNop();
  }

  // Branch if equal.
  void beq(Register rs, Register rt, Label* l) {
    ASSERT(!in_delay_slot_);
    EmitBranch(BEQ, rs, rt, l);
    EmitBranchDelayNop();
  }

  // Branch if equal, likely taken.
  // Delay slot executed only when branch taken.
  void beql(Register rs, Register rt, Label* l) {
    ASSERT(!in_delay_slot_);
    EmitBranch(BEQL, rs, rt, l);
    EmitBranchDelayNop();
  }

  // Branch if rs >= 0.
  void bgez(Register rs, Label* l) {
    ASSERT(!in_delay_slot_);
    EmitRegImmBranch(BGEZ, rs, l);
    EmitBranchDelayNop();
  }

  // Branch if rs >= 0, likely taken.
  // Delay slot executed only when branch taken.
  void bgezl(Register rs, Label* l) {
    ASSERT(!in_delay_slot_);
    EmitRegImmBranch(BGEZL, rs, l);
    EmitBranchDelayNop();
  }

  // Branch if rs > 0.
  void bgtz(Register rs, Label* l) {
    ASSERT(!in_delay_slot_);
    EmitBranch(BGTZ, rs, R0, l);
    EmitBranchDelayNop();
  }

  // Branch if rs > 0, likely taken.
  // Delay slot executed only when branch taken.
  void bgtzl(Register rs, Label* l) {
    ASSERT(!in_delay_slot_);
    EmitBranch(BGTZL, rs, R0, l);
    EmitBranchDelayNop();
  }

  // Branch if rs <= 0.
  void blez(Register rs, Label* l) {
    ASSERT(!in_delay_slot_);
    EmitBranch(BLEZ, rs, R0, l);
    EmitBranchDelayNop();
  }

  // Branch if rs <= 0, likely taken.
  // Delay slot executed only when branch taken.
  void blezl(Register rs, Label* l) {
    ASSERT(!in_delay_slot_);
    EmitBranch(BLEZL, rs, R0, l);
    EmitBranchDelayNop();
  }

  // Branch if rs < 0.
  void bltz(Register rs, Label* l) {
    ASSERT(!in_delay_slot_);
    EmitRegImmBranch(BLTZ, rs, l);
    EmitBranchDelayNop();
  }

  // Branch if rs < 0, likely taken.
  // Delay slot executed only when branch taken.
  void bltzl(Register rs, Label* l) {
    ASSERT(!in_delay_slot_);
    EmitRegImmBranch(BLTZL, rs, l);
    EmitBranchDelayNop();
  }

  // Branch if not equal.
  void bne(Register rs, Register rt, Label* l) {
    ASSERT(!in_delay_slot_);  // Jump within a delay slot is not supported.
    EmitBranch(BNE, rs, rt, l);
    EmitBranchDelayNop();
  }

  // Branch if not equal, likely taken.
  // Delay slot executed only when branch taken.
  void bnel(Register rs, Register rt, Label* l) {
    ASSERT(!in_delay_slot_);  // Jump within a delay slot is not supported.
    EmitBranch(BNEL, rs, rt, l);
    EmitBranchDelayNop();
  }

  static int32_t BreakEncoding(int32_t code) {
    ASSERT(Utils::IsUint(20, code));
    return SPECIAL << kOpcodeShift | code << kBreakCodeShift |
           BREAK << kFunctionShift;
  }

  void break_(int32_t code) { Emit(BreakEncoding(code)); }

  // FPU compare, always false.
  void cfd(DRegister ds, DRegister dt) {
    FRegister fs = static_cast<FRegister>(ds * 2);
    FRegister ft = static_cast<FRegister>(dt * 2);
    EmitFpuRType(COP1, FMT_D, ft, fs, F0, COP1_C_F);
  }

  // FPU compare, true if unordered, i.e. one is NaN.
  void cund(DRegister ds, DRegister dt) {
    FRegister fs = static_cast<FRegister>(ds * 2);
    FRegister ft = static_cast<FRegister>(dt * 2);
    EmitFpuRType(COP1, FMT_D, ft, fs, F0, COP1_C_UN);
  }

  // FPU compare, true if equal.
  void ceqd(DRegister ds, DRegister dt) {
    FRegister fs = static_cast<FRegister>(ds * 2);
    FRegister ft = static_cast<FRegister>(dt * 2);
    EmitFpuRType(COP1, FMT_D, ft, fs, F0, COP1_C_EQ);
  }

  // FPU compare, true if unordered or equal.
  void cueqd(DRegister ds, DRegister dt) {
    FRegister fs = static_cast<FRegister>(ds * 2);
    FRegister ft = static_cast<FRegister>(dt * 2);
    EmitFpuRType(COP1, FMT_D, ft, fs, F0, COP1_C_UEQ);
  }

  // FPU compare, true if less than.
  void coltd(DRegister ds, DRegister dt) {
    FRegister fs = static_cast<FRegister>(ds * 2);
    FRegister ft = static_cast<FRegister>(dt * 2);
    EmitFpuRType(COP1, FMT_D, ft, fs, F0, COP1_C_OLT);
  }

  // FPU compare, true if unordered or less than.
  void cultd(DRegister ds, DRegister dt) {
    FRegister fs = static_cast<FRegister>(ds * 2);
    FRegister ft = static_cast<FRegister>(dt * 2);
    EmitFpuRType(COP1, FMT_D, ft, fs, F0, COP1_C_ULT);
  }

  // FPU compare, true if less or equal.
  void coled(DRegister ds, DRegister dt) {
    FRegister fs = static_cast<FRegister>(ds * 2);
    FRegister ft = static_cast<FRegister>(dt * 2);
    EmitFpuRType(COP1, FMT_D, ft, fs, F0, COP1_C_OLE);
  }

  // FPU compare, true if unordered or less or equal.
  void culed(DRegister ds, DRegister dt) {
    FRegister fs = static_cast<FRegister>(ds * 2);
    FRegister ft = static_cast<FRegister>(dt * 2);
    EmitFpuRType(COP1, FMT_D, ft, fs, F0, COP1_C_ULE);
  }

  void clo(Register rd, Register rs) {
    EmitRType(SPECIAL2, rs, rd, rd, 0, CLO);
  }

  void clz(Register rd, Register rs) {
    EmitRType(SPECIAL2, rs, rd, rd, 0, CLZ);
  }

  // Convert a double in ds to a 32-bit signed int in fd rounding towards 0.
  void truncwd(FRegister fd, DRegister ds) {
    FRegister fs = static_cast<FRegister>(ds * 2);
    EmitFpuRType(COP1, FMT_D, F0, fs, fd, COP1_TRUNC_W);
  }

  // Convert a 32-bit float in fs to a 64-bit double in dd.
  void cvtds(DRegister dd, FRegister fs) {
    FRegister fd = static_cast<FRegister>(dd * 2);
    EmitFpuRType(COP1, FMT_S, F0, fs, fd, COP1_CVT_D);
  }

  // Converts a 32-bit signed int in fs to a double in fd.
  void cvtdw(DRegister dd, FRegister fs) {
    FRegister fd = static_cast<FRegister>(dd * 2);
    EmitFpuRType(COP1, FMT_W, F0, fs, fd, COP1_CVT_D);
  }

  // Convert a 64-bit double in ds to a 32-bit float in fd.
  void cvtsd(FRegister fd, DRegister ds) {
    FRegister fs = static_cast<FRegister>(ds * 2);
    EmitFpuRType(COP1, FMT_D, F0, fs, fd, COP1_CVT_S);
  }

  void div(Register rs, Register rt) { EmitRType(SPECIAL, rs, rt, R0, 0, DIV); }

  void divd(DRegister dd, DRegister ds, DRegister dt) {
    FRegister fd = static_cast<FRegister>(dd * 2);
    FRegister fs = static_cast<FRegister>(ds * 2);
    FRegister ft = static_cast<FRegister>(dt * 2);
    EmitFpuRType(COP1, FMT_D, ft, fs, fd, COP1_DIV);
  }

  void divu(Register rs, Register rt) {
    EmitRType(SPECIAL, rs, rt, R0, 0, DIVU);
  }

  void jalr(Register rs, Register rd = RA) {
    ASSERT(rs != rd);
    ASSERT(!in_delay_slot_);  // Jump within a delay slot is not supported.
    EmitRType(SPECIAL, rs, R0, rd, 0, JALR);
    EmitBranchDelayNop();
  }

  void jr(Register rs) {
    ASSERT(!in_delay_slot_);  // Jump within a delay slot is not supported.
    EmitRType(SPECIAL, rs, R0, R0, 0, JR);
    EmitBranchDelayNop();
  }

  void jal(uint32_t address) {
    ASSERT(!in_delay_slot_);  // Jump within a delay slot is not supported.
    ASSERT(Utils::IsUint(26, address));
    Emit(JAL << kOpcodeShift | address << kFunctionShift);
    EmitBranchDelayNop();
  }

  void lb(Register rt, const Address& addr) { EmitLoadStore(LB, rt, addr); }

  void lbu(Register rt, const Address& addr) { EmitLoadStore(LBU, rt, addr); }

  void ldc1(DRegister dt, const Address& addr) {
    FRegister ft = static_cast<FRegister>(dt * 2);
    EmitFpuLoadStore(LDC1, ft, addr);
  }

  void lh(Register rt, const Address& addr) { EmitLoadStore(LH, rt, addr); }

  void lhu(Register rt, const Address& addr) { EmitLoadStore(LHU, rt, addr); }

  void ll(Register rt, const Address& addr) { EmitLoadStore(LL, rt, addr); }

  void lui(Register rt, const Immediate& imm) {
    ASSERT(Utils::IsUint(kImmBits, imm.value()));
    const uint16_t imm_value = static_cast<uint16_t>(imm.value());
    EmitIType(LUI, R0, rt, imm_value);
  }

  void lw(Register rt, const Address& addr) { EmitLoadStore(LW, rt, addr); }

  void lwc1(FRegister ft, const Address& addr) {
    EmitFpuLoadStore(LWC1, ft, addr);
  }

  void madd(Register rs, Register rt) {
    EmitRType(SPECIAL2, rs, rt, R0, 0, MADD);
  }

  void maddu(Register rs, Register rt) {
    EmitRType(SPECIAL2, rs, rt, R0, 0, MADDU);
  }

  void mfc1(Register rt, FRegister fs) {
    Emit(COP1 << kOpcodeShift | COP1_MF << kCop1SubShift | rt << kRtShift |
         fs << kFsShift);
  }

  void mfhi(Register rd) { EmitRType(SPECIAL, R0, R0, rd, 0, MFHI); }

  void mflo(Register rd) { EmitRType(SPECIAL, R0, R0, rd, 0, MFLO); }

  void mov(Register rd, Register rs) { or_(rd, rs, ZR); }

  void movd(DRegister dd, DRegister ds) {
    FRegister fd = static_cast<FRegister>(dd * 2);
    FRegister fs = static_cast<FRegister>(ds * 2);
    EmitFpuRType(COP1, FMT_D, F0, fs, fd, COP1_MOV);
  }

  // Move if floating point false.
  void movf(Register rd, Register rs) {
    EmitRType(SPECIAL, rs, R0, rd, 0, MOVCI);
  }

  void movn(Register rd, Register rs, Register rt) {
    EmitRType(SPECIAL, rs, rt, rd, 0, MOVN);
  }

  // Move if floating point true.
  void movt(Register rd, Register rs) {
    EmitRType(SPECIAL, rs, R1, rd, 0, MOVCI);
  }

  // rd <- (rt == 0) ? rs : rd;
  void movz(Register rd, Register rs, Register rt) {
    EmitRType(SPECIAL, rs, rt, rd, 0, MOVZ);
  }

  void movs(FRegister fd, FRegister fs) {
    EmitFpuRType(COP1, FMT_S, F0, fs, fd, COP1_MOV);
  }

  void mtc1(Register rt, FRegister fs) {
    Emit(COP1 << kOpcodeShift | COP1_MT << kCop1SubShift | rt << kRtShift |
         fs << kFsShift);
  }

  void mthi(Register rs) { EmitRType(SPECIAL, rs, R0, R0, 0, MTHI); }

  void mtlo(Register rs) { EmitRType(SPECIAL, rs, R0, R0, 0, MTLO); }

  void muld(DRegister dd, DRegister ds, DRegister dt) {
    FRegister fd = static_cast<FRegister>(dd * 2);
    FRegister fs = static_cast<FRegister>(ds * 2);
    FRegister ft = static_cast<FRegister>(dt * 2);
    EmitFpuRType(COP1, FMT_D, ft, fs, fd, COP1_MUL);
  }

  void mult(Register rs, Register rt) {
    EmitRType(SPECIAL, rs, rt, R0, 0, MULT);
  }

  void multu(Register rs, Register rt) {
    EmitRType(SPECIAL, rs, rt, R0, 0, MULTU);
  }

  void negd(DRegister dd, DRegister ds) {
    FRegister fd = static_cast<FRegister>(dd * 2);
    FRegister fs = static_cast<FRegister>(ds * 2);
    EmitFpuRType(COP1, FMT_D, F0, fs, fd, COP1_NEG);
  }

  void nop() { Emit(Instr::kNopInstruction); }

  void nor(Register rd, Register rs, Register rt) {
    EmitRType(SPECIAL, rs, rt, rd, 0, NOR);
  }

  void or_(Register rd, Register rs, Register rt) {
    EmitRType(SPECIAL, rs, rt, rd, 0, OR);
  }

  void ori(Register rt, Register rs, const Immediate& imm) {
    ASSERT(Utils::IsUint(kImmBits, imm.value()));
    const uint16_t imm_value = static_cast<uint16_t>(imm.value());
    EmitIType(ORI, rs, rt, imm_value);
  }

  void sb(Register rt, const Address& addr) { EmitLoadStore(SB, rt, addr); }

  // rt = 1 on success, 0 on failure.
  void sc(Register rt, const Address& addr) { EmitLoadStore(SC, rt, addr); }

  void sdc1(DRegister dt, const Address& addr) {
    FRegister ft = static_cast<FRegister>(dt * 2);
    EmitFpuLoadStore(SDC1, ft, addr);
  }

  void sh(Register rt, const Address& addr) { EmitLoadStore(SH, rt, addr); }

  void sll(Register rd, Register rt, int sa) {
    EmitRType(SPECIAL, R0, rt, rd, sa, SLL);
  }

  void sllv(Register rd, Register rt, Register rs) {
    EmitRType(SPECIAL, rs, rt, rd, 0, SLLV);
  }

  void slt(Register rd, Register rs, Register rt) {
    EmitRType(SPECIAL, rs, rt, rd, 0, SLT);
  }

  void slti(Register rt, Register rs, const Immediate& imm) {
    ASSERT(Utils::IsInt(kImmBits, imm.value()));
    const uint16_t imm_value = static_cast<uint16_t>(imm.value());
    EmitIType(SLTI, rs, rt, imm_value);
  }

  // Although imm argument is int32_t, it is interpreted as an uint32_t.
  // For example, -1 stands for 0xffffffffUL: it is encoded as 0xffff in the
  // instruction imm field and is then sign extended back to 0xffffffffUL.
  void sltiu(Register rt, Register rs, const Immediate& imm) {
    ASSERT(Utils::IsInt(kImmBits, imm.value()));
    const uint16_t imm_value = static_cast<uint16_t>(imm.value());
    EmitIType(SLTIU, rs, rt, imm_value);
  }

  void sltu(Register rd, Register rs, Register rt) {
    EmitRType(SPECIAL, rs, rt, rd, 0, SLTU);
  }

  void sqrtd(DRegister dd, DRegister ds) {
    FRegister fd = static_cast<FRegister>(dd * 2);
    FRegister fs = static_cast<FRegister>(ds * 2);
    EmitFpuRType(COP1, FMT_D, F0, fs, fd, COP1_SQRT);
  }

  void sra(Register rd, Register rt, int sa) {
    EmitRType(SPECIAL, R0, rt, rd, sa, SRA);
  }

  void srav(Register rd, Register rt, Register rs) {
    EmitRType(SPECIAL, rs, rt, rd, 0, SRAV);
  }

  void srl(Register rd, Register rt, int sa) {
    EmitRType(SPECIAL, R0, rt, rd, sa, SRL);
  }

  void srlv(Register rd, Register rt, Register rs) {
    EmitRType(SPECIAL, rs, rt, rd, 0, SRLV);
  }

  void subd(DRegister dd, DRegister ds, DRegister dt) {
    FRegister fd = static_cast<FRegister>(dd * 2);
    FRegister fs = static_cast<FRegister>(ds * 2);
    FRegister ft = static_cast<FRegister>(dt * 2);
    EmitFpuRType(COP1, FMT_D, ft, fs, fd, COP1_SUB);
  }

  void sub(Register rd, Register rs, Register rt) {
    EmitRType(SPECIAL, rs, rt, rd, 0, SUB);
  }

  void subu(Register rd, Register rs, Register rt) {
    EmitRType(SPECIAL, rs, rt, rd, 0, SUBU);
  }

  void sw(Register rt, const Address& addr) { EmitLoadStore(SW, rt, addr); }

  void swc1(FRegister ft, const Address& addr) {
    EmitFpuLoadStore(SWC1, ft, addr);
  }

  static int32_t SyncEncoding(int32_t stype) {
    ASSERT(Utils::IsUint(5, stype));
    return SPECIAL << kOpcodeShift | stype << kSyncCodeShift |
           SYNC << kFunctionShift;
  }

  void sync(int32_t code) { Emit(SyncEncoding(code)); }

  void xori(Register rt, Register rs, const Immediate& imm) {
    ASSERT(Utils::IsUint(kImmBits, imm.value()));
    const uint16_t imm_value = static_cast<uint16_t>(imm.value());
    EmitIType(XORI, rs, rt, imm_value);
  }

  void xor_(Register rd, Register rs, Register rt) {
    EmitRType(SPECIAL, rs, rt, rd, 0, XOR);
  }

  void LoadImmediate(Register rd, int32_t value) override{
    UNIMPLEMENTED();
  }

  void LoadImmediate(DRegister rd, double value) {
    UNIMPLEMENTED();
  }

  void LoadImmediate(FRegister rd, float value) {
    UNIMPLEMENTED();
  }

  void AddImmediate(Register rd, Register rs, int32_t value) {
    UNIMPLEMENTED();
  }

  void AddImmediate(Register rd, int32_t value) {
    UNIMPLEMENTED();
  }

  void AddRegisters(Register rd, Register rs) { addu(rd, rd, rs); }

  void AndImmediate(Register rd, 
                    Register rs, 
                    target::word imm, 
                    OperandSize sz = kWordBytes) override {
    UNIMPLEMENTED();
  }

  void AndImmediate(Register reg,
                    target::word imm, 
                    OperandSize sz = kWordBytes) override {
    UNIMPLEMENTED();
  }

  void OrImmediate(Register rd, Register rs, int32_t imm) {
    UNIMPLEMENTED();
  }

  void OrImmediate(Register rd, int32_t imm) { 
    UNIMPLEMENTED();
  }
  
  void AndRegisters(Register dst,
                    Register src1,
                    Register src2 = kNoRegister) override{
    UNIMPLEMENTED();                  
  }

  void XorImmediate(Register rd, Register rs, int32_t imm) {
    UNIMPLEMENTED();
  }

  void BranchEqual(Register rd, Register rn, Label* l) { beq(rd, rn, l); }

  void BranchEqual(Register rd, const Immediate& imm, Label* l) {
    UNIMPLEMENTED();
  }

  void BranchEqual(Register rd, const Object& object, Label* l) {
    UNIMPLEMENTED();
  }

  void BranchNotEqual(Register rd, Register rn, Label* l) { bne(rd, rn, l); }

  void BranchNotEqual(Register rd, const Immediate& imm, Label* l) {
    UNIMPLEMENTED();
  }

  void BranchNotEqual(Register rd, const Object& object, Label* l) {
    UNIMPLEMENTED();
  }

  void BranchSignedGreater(Register rd, Register rs, Label* l) {
    UNIMPLEMENTED();
  }

  void BranchSignedGreater(Register rd, const Immediate& imm, Label* l) {
    UNIMPLEMENTED();
  }

  void BranchUnsignedGreater(Register rd, Register rs, Label* l) {
    UNIMPLEMENTED();
  }

  void BranchUnsignedGreater(Register rd, const Immediate& imm, Label* l) {
    UNIMPLEMENTED();
  }

  void BranchSignedGreaterEqual(Register rd, Register rs, Label* l) {
    UNIMPLEMENTED();
  }

  void BranchSignedGreaterEqual(Register rd, const Immediate& imm, Label* l) {
    UNIMPLEMENTED();
  }

  void BranchUnsignedGreaterEqual(Register rd, Register rs, Label* l) {
    UNIMPLEMENTED();
  }

  void BranchUnsignedGreaterEqual(Register rd, const Immediate& imm, Label* l) {
    UNIMPLEMENTED();
  }

  void BranchSignedLess(Register rd, Register rs, Label* l) {
    UNIMPLEMENTED();
  }

  void BranchSignedLess(Register rd, const Immediate& imm, Label* l) {
    UNIMPLEMENTED();
  }

  void BranchUnsignedLess(Register rd, Register rs, Label* l) {
    UNIMPLEMENTED();
  }

  void BranchUnsignedLess(Register rd, const Immediate& imm, Label* l) {
    UNIMPLEMENTED();
  }

  void BranchSignedLessEqual(Register rd, Register rs, Label* l) {
    UNIMPLEMENTED();
  }

  void BranchSignedLessEqual(Register rd, const Immediate& imm, Label* l) {
    UNIMPLEMENTED();
  }

  void BranchUnsignedLessEqual(Register rd, Register rs, Label* l) {
    UNIMPLEMENTED();
  }

  void BranchUnsignedLessEqual(Register rd, const Immediate& imm, Label* l) {
    UNIMPLEMENTED();
  }

  void Bind(Label* label) override;

  void LoadClassId(Register result, Register object);
  void LoadClassById(Register result, Register class_id);
  void CompareClassId(Register object,
                    intptr_t class_id,
                    Register scratch = kNoRegister);
  void LoadClassIdMayBeSmi(Register result, Register object);
  void LoadTaggedClassIdMayBeSmi(Register result, Register object);

  bool constant_pool_allowed() const { return constant_pool_allowed_; }
  void set_constant_pool_allowed(bool b) { constant_pool_allowed_ = b; }

  bool use_far_branches() const {
    return FLAG_use_far_branches || use_far_branches_;
  }

  void set_use_far_branches(bool b) { use_far_branches_ = b; }

  void SetPrologueOffset() {
    if (prologue_offset_ == -1) {
      prologue_offset_ = CodeSize();
    }
  }

  void EnterFrame(intptr_t frame_size = 0);
  void LeaveFrame();
  void LeaveFrameAndReturn();

  // Set up a stub frame so that the stack traversal code can easily identify
  // a stub frame.
  void EnterStubFrame(intptr_t frame_size = 0);
  void LeaveStubFrame();
  // A separate macro for when a Ret immediately follows, so that we can use
  // the branch delay slot.
  void LeaveStubFrameAndReturn(Register ra = RA);

  // Set up a Dart frame on entry with a frame pointer and PC information to
  // enable easy access to the RawInstruction object of code corresponding
  // to this frame.
  enum RestorePP { kRestoreCallerPP, kKeepCalleePP };
  void EnterDartFrame(intptr_t frame_size, bool load_pool_pointer = true);
  void LeaveDartFrame(RestorePP restore_pp = kRestoreCallerPP);
  void LeaveDartFrameAndReturn(Register ra = RA);

  void MonomorphicCheckedEntryJIT();
  void MonomorphicCheckedEntryAOT();
  
 private:
  bool use_far_branches_;
  bool delay_slot_available_;
  bool in_delay_slot_;

  bool constant_pool_allowed_;

  enum DeferredCompareType {
    kNone,
    kCompareReg,
    kCompareImm,
    kTestReg,
    kTestImm,
  };
  
  DeferredCompareType deferred_compare_ = kNone;
  Register deferred_left_ = kNoRegister;
  Register deferred_reg_ = kNoRegister;
  intptr_t deferred_imm_ = 0;

  void Emit(int32_t value) {
    // Emitting an instruction clears the delay slot state.
    in_delay_slot_ = false;
    delay_slot_available_ = false;
    AssemblerBuffer::EnsureCapacity ensured(&buffer_);
    buffer_.Emit<int32_t>(value);
  }

// Encode CPU instructions according to the types specified in
// Figures 5‑1, 5‑3, and 5‑8 in
// MIPS® Architecture For Programmers Volume I‑A: Introduction to the MIPS32® Architecture, Revision 6.01.
// Available at:
// https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00082-2B-MIPS32INT-AFP-06.01.pdf
  void EmitIType(Opcode opcode, Register rs, Register rt, uint16_t imm) {
    Emit(opcode << kOpcodeShift | rs << kRsShift | rt << kRtShift | imm);
  }

  void EmitLoadStore(Opcode opcode, Register rt, const Address& addr) {
    Emit(opcode << kOpcodeShift | rt << kRtShift | addr.encoding());
  }

  void EmitFpuLoadStore(Opcode opcode, FRegister ft, const Address& addr) {
    Emit(opcode << kOpcodeShift | ft << kFtShift | addr.encoding());
  }

  void EmitRegImmType(Opcode opcode, Register rs, RtRegImm code, uint16_t imm) {
    Emit(opcode << kOpcodeShift | rs << kRsShift | code << kRtShift | imm);
  }

  void EmitJType(Opcode opcode, uint32_t destination) { UNIMPLEMENTED(); }

  void EmitRType(Opcode opcode,
                 Register rs,
                 Register rt,
                 Register rd,
                 int sa,
                 SpecialFunction func) {
    ASSERT(Utils::IsUint(5, sa));
    Emit(opcode << kOpcodeShift | rs << kRsShift | rt << kRtShift |
         rd << kRdShift | sa << kSaShift | func << kFunctionShift);
  }

  void EmitFpuRType(Opcode opcode,
                    Format fmt,
                    FRegister ft,
                    FRegister fs,
                    FRegister fd,
                    Cop1Function func) {
    Emit(opcode << kOpcodeShift | fmt << kFmtShift | ft << kFtShift |
         fs << kFsShift | fd << kFdShift | func << kCop1FnShift);
  }

  void EmitBranch(Opcode b, Register rs, Register rt, Label* label);
  void EmitRegImmBranch(RtRegImm b, Register rs, Label* label);
  void EmitFpuBranch(bool kind, Label* label);

  void EmitBranchDelayNop() {
    Emit(Instr::kNopInstruction);  // Branch delay NOP.
    delay_slot_available_ = true;
  }
};

}  // namespace compiler
}  // namespace dart

#endif  // RUNTIME_VM_ASSEMBLER_MIPS_H_

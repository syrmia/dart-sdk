// Copyright (c) 2025, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef RUNTIME_VM_ASSEMBLER_MIPS_H_
#define RUNTIME_VM_ASSEMBLER_MIPS_H_

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

  void CompareImmediate(Register rn, int32_t imm, OperandSize sz = kWordBytes) override;
  void TestImmediate(Register rn, int32_t imm, OperandSize sz = kWordBytes);

  void CompareRegisters(Register rn, Register rm);
  void TestRegisters(Register rn, Register rm);
  
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
};

}  // namespace compiler
}  // namespace dart

#endif  // RUNTIME_VM_ASSEMBLER_MIPS_H_

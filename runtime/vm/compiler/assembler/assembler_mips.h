// Copyright (c) 2025, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef RUNTIME_VM_ASSEMBLER_MIPS_H_
#define RUNTIME_VM_ASSEMBLER_MIPS_H_

#include "vm/constants.h"

// References to documentation in this file refer to:
// "MIPS® Architecture For Programmers Volume I-A:
//   Introduction to the MIPS32® Architecture" in short "VolI-A"
// and
// "MIPS® Architecture For Programmers Volume II-A:
//   The MIPS32® Instruction Set" in short "VolII-A"
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

}  // namespace compiler
}  // namespace dart

#endif  // RUNTIME_VM_ASSEMBLER_MIPS_H_

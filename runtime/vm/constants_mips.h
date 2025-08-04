// Copyright (c) 2025, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef RUNTIME_VM_CONSTANTS_MIPS_H_
#define RUNTIME_VM_CONSTANTS_MIPS_H_

namespace dart {

enum Register {
  R0 = 0,
  R1 = 1,  // AT aka TMP
  R2 = 2,
  R3 = 3,
  R4 = 4,
  R5 = 5,
  R6 = 6,
  R7 = 7,
  R8 = 8,
  R9 = 9,
  R10 = 10,
  R11 = 11,
  R12 = 12,
  R13 = 13,
  R14 = 14,
  R15 = 15,
  R16 = 16,
  R17 = 17,
  R18 = 18,
  R19 = 19,
  R20 = 20,
  R21 = 21,
  R22 = 22,
  R23 = 23,
  R24 = 24,
  R25 = 25,
  R26 = 26,  // K0 (reserved for OS kernel)
  R27 = 27,  // K1 (reserved for OS kernel)
  R28 = 28,
  R29 = 29,  // SP
  R30 = 30,  // FP
  R31 = 31,  // RA
  kNumberOfCpuRegisters = 32,
  IMM = 32,  // Positive value is easier to encode than kNoRegister in bitfield.
  kNoRegister = -1,  // Signals an illegal register.

  // Register aliases.
  ZR = R0,
  AT = R1,

  V0 = R2,
  V1 = R3,

  A0 = R4,
  A1 = R5,
  A2 = R6,
  A3 = R7,

  T0 = R8,
  T1 = R9,
  T2 = R10,
  T3 = R11,
  T4 = R12,
  T5 = R13,
  T6 = R14,
  T7 = R15,

  S0 = R16,
  S1 = R17,
  S2 = R18,
  S3 = R19,
  S4 = R20,
  S5 = R21,
  S6 = R22,
  S7 = R23,

  T8 = R24,
  T9 = R25,

  K0 = R26,
  K1 = R27,

  GP = R28,
  SP = R29,
  FP = R30,
  RA = R31,
};

// There is no dedicated status register on MIPS, but Condition values are used
// and passed around by the intermediate language, so we need a Condition type.
// We delay code generation of a comparison that would result in a traditional
// condition code in the status register by keeping both register operands and
// the relational operator between them as the Condition.

class Condition : public ValueObject {
 public:
  enum RelationOperator {
    kNoCondition = -1,
    AL = 0,    // always
    NV = 1,    // never
    EQ = 2,    // equal
    NE = 3,    // not equal
    GE = 4,    // greater equal
    LT = 5,    // less than
    GT = 6,    // greater than
    LE = 7,    // less equal
    UGT = 8,   // unsigned greater than
    ULE = 9,   // unsigned less equal
    UGE = 10,  // unsigned greater equal
    ULT = 11,  // unsigned less than
    kSpecialCondition = 12,
    kNumberOfConditions = 13,

    // Platform-independent variants declared for all platforms
    EQUAL = EQ,
    ZERO = EQUAL,
    NOT_EQUAL = NE,
    NOT_ZERO = NOT_EQUAL,
    LESS = LT,
    LESS_EQUAL = LE,
    GREATER_EQUAL = GE,
    GREATER = GT,
    UNSIGNED_LESS = ULT,
    UNSIGNED_LESS_EQUAL = ULE,
    UNSIGNED_GREATER = UGT,
    UNSIGNED_GREATER_EQUAL = UGE,

    INVALID_RELATION = 13,
    kInvalidCondition = INVALID_RELATION
  };
};   

}  // namespace dart

#endif  // RUNTIME_VM_CONSTANTS_MIPS_H_

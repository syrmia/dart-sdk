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
  R19 = 19,  // THR
  R20 = 20,
  R21 = 21,
  R22 = 22,  // CTX
  R23 = 23,  // PP
  R24 = 24,  // CMPRES1
  R25 = 25,  // CMPRES2
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

// Values for floating point registers.
// Double-precision values use register pairs.
enum FRegister {
  F0 = 0,
  F1 = 1,
  F2 = 2,
  F3 = 3,
  F4 = 4,
  F5 = 5,
  F6 = 6,
  F7 = 7,
  F8 = 8,
  F9 = 9,
  F10 = 10,
  F11 = 11,
  F12 = 12,
  F13 = 13,
  F14 = 14,
  F15 = 15,
  F16 = 16,
  F17 = 17,
  F18 = 18,
  F19 = 19,
  F20 = 20,
  F21 = 21,
  F22 = 22,
  F23 = 23,
  F24 = 24,
  F25 = 25,
  F26 = 26,
  F27 = 27,
  F28 = 28,
  F29 = 29,
  F30 = 30,
  F31 = 31,
  kNumberOfFRegisters = 32,
  kNoFRegister = -1,
};

const int kFpuRegisterSize = 8;
typedef double fpu_register_t;

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

// The double precision floating point registers are concatenated pairs of the
// single precision registers, e.g. D0 is F1:F0, D1 is F3:F2, etc.. We only
// tell the architecture generic code about the double precision registers, then
// convert to the single precision registers when needed in the mips-specific
// code.
enum DRegister {
  D0 = 0,    // Function return value 1.
  D1 = 1,    // Function return value 2.
  D2 = 2,    // Not preserved.
  D3 = 3,    // Not preserved.
  D4 = 4,    // Not preserved.
  D5 = 5,    // Not preserved.
  D6 = 6,    // Argument 1.
  D7 = 7,    // Argument 2.
  D8 = 8,    // Not preserved.
  D9 = 9,    // Not preserved.
  D10 = 10,  // Preserved.
  D11 = 11,  // Preserved.
  D12 = 12,  // Preserved.
  D13 = 13,  // Preserved.
  D14 = 14,  // Preserved.
  D15 = 15,  // Preserved.
  kNumberOfDRegisters = 16,
  kNoDRegister = -1,
};

const DRegister DTMP = D9;  // Double TMP
const FRegister STMP1 = F18;  // Single-precision TMP1
const FRegister STMP2 = F19;  // Single-precision TMP2

// Architecture independent aliases.
typedef DRegister FpuRegister;
const FpuRegister FpuTMP = DTMP;
const int kNumberOfFpuRegisters = kNumberOfDRegisters;
const FpuRegister kNoFpuRegister = kNoDRegister;

// Register aliases.
const Register TMP = AT;            // Used as scratch register by assembler.
const Register TMP2 = kNoRegister;  // No second assembler scratch register.
const Register CTX = S6;  // Location of current context at method entry.
const Register CODE_REG = S6;
const Register PP = S7;     // Caches object pool pointer in generated code.

const Register FUNCTION_REG = T0;
const Register SPREG = SP;  // Stack pointer register.
const Register FPREG = FP;  // Frame pointer register.
const Register LRREG = RA;  // Link register.
const Register ICREG = S5;  // IC data register.
const Register ARGS_DESC_REG = S4;
const Register THR = S3;  // Caches current thread in generated code.
const Register CALLEE_SAVED_TEMP = S5;

// The code that generates a comparison can be far away from the code that
// generates the branch that uses the result of that comparison. In this case,
// CMPRES1 and CMPRES2 are used for the results of the comparison. We need two
// since TMP is clobbered by a far branch.
const Register CMPRES1 = T8;
const Register CMPRES2 = T9;


extern const char* const cpu_reg_names[kNumberOfCpuRegisters];
extern const char* const cpu_reg_abi_names[kNumberOfCpuRegisters];
extern const char* const fpu_reg_names[kNumberOfFRegisters];

}  // namespace dart

#endif  // RUNTIME_VM_CONSTANTS_MIPS_H_

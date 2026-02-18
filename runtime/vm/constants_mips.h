// Copyright (c) 2025, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef RUNTIME_VM_CONSTANTS_MIPS_H_
#define RUNTIME_VM_CONSTANTS_MIPS_H_

#include "vm/allocation.h"
#include "vm/constants_base.h"

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

// Architecture-independent parts of the compiler should use
// compare-and-branch instead of condition codes.
enum Condition {
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
  VS = 12,   // overflow
  VC = 13,   // no overflow
  kSpecialCondition = 14,
  kNumberOfConditions = 15,

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
  OVERFLOW = VS,
  NO_OVERFLOW = VC,
  INVALID_RELATION = 16,
  kInvalidCondition = INVALID_RELATION
};

static inline Condition InvertCondition(Condition c) {
  COMPILE_ASSERT((EQ ^ NE) == 1);
  COMPILE_ASSERT((UGE ^ ULT) == 1);
  COMPILE_ASSERT((UGT ^ ULE) == 1);
  COMPILE_ASSERT((GE ^ LT) == 1);
  COMPILE_ASSERT((GT ^ LE) == 1);
  COMPILE_ASSERT((AL ^ NV) == 1);
  COMPILE_ASSERT((VS ^ VC) == 1);

  ASSERT(c != AL);
  ASSERT(c != kInvalidCondition);
  return static_cast<Condition>(c ^ 1);
}

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
struct InstantiationABI {
  static constexpr Register kUninstantiatedTypeArgumentsReg = T1;
  static constexpr Register kInstantiatorTypeArgumentsReg = T2;
  static constexpr Register kFunctionTypeArgumentsReg = T3;
  static constexpr Register kResultTypeArgumentsReg = V0;
  static constexpr Register kResultTypeReg = V0;
  static constexpr Register kScratchReg = T4;
};

typedef uint32_t RegList;
const RegList kAllCpuRegistersList = 0xFFFFFFFF;

const RegList kAbiArgumentCpuRegs =
    (1 << A0) | (1 << A1) | (1 << A2) | (1 << A3);
const RegList kAbiPreservedCpuRegs = (1 << S0) | (1 << S1) | (1 << S2) |
                                     (1 << S3) | (1 << S4) | (1 << S5) |
                                     (1 << S6) | (1 << S7);
const int kAbiPreservedCpuRegCount = 8;

// FRegister registers 20 - 31 are preserved across calls.
const FRegister kAbiFirstPreservedFReg = F20;
const FRegister kAbiLastPreservedFReg =
    static_cast<FRegister>(kNumberOfFRegisters - 1);
const int kAbiPreservedFRegCount = 12;

const RegList kAbiPreservedFRegs = (1 << F20) | (1 << F21) | (1 << F22) |
                                     (1 << F23) | (1 << F24) | (1 << F25) |
                                     (1 << F26) | (1 << F27) | (1 << F28) |
                                     (1 << F29) | (1 << F30)| (1 << F31);

// FpuRegister registers D10 - D15 are preserved across calls.
const int kAbiPreservedFpuRegCount = 6;
const RegList kAbiPreservedFpuRegs = (1 << D10) | (1 << D11) | (1 << D12) |
                                     (1 << D13) | (1 << D14) | (1 << D15);
const FpuRegister kAbiFirstPreservedFpuReg = D10;

const RegList kReservedCpuRegisters =
    (1 << SPREG) | (1 << FPREG) | (1 << TMP) | (1 << PP) | (1 << THR) |
    (1 << CTX) | (1 << ZR) | (1 << CMPRES1) | (1 << CMPRES2) | (1 << K0) |
    (1 << K1) | (1 << GP) | (1 << RA);
const int kNumberOfReservedCpuRegisters = 13;
// CPU registers available to Dart allocator.
const RegList kDartAvailableCpuRegs =
    kAllCpuRegistersList & ~kReservedCpuRegisters;
// Registers available to Dart that are not preserved by runtime calls.
const RegList kDartVolatileCpuRegs =
    kDartAvailableCpuRegs & ~kAbiPreservedCpuRegs;
const int kDartVolatileCpuRegCount = 14;
const Register kDartFirstVolatileCpuReg = R2;
const Register kDartLastVolatileCpuReg = R15;

// No reason to prefer certain registers on MIPS.
constexpr int kRegisterAllocationBias = 0;

// FPU registers 0 - 19 are not preserved across calls.
const FRegister kDartFirstVolatileFpuReg = F0;
const FRegister kDartLastVolatileFpuReg = F19;
const int kDartVolatileFRegCount = 20;
const int kDartVolatileFpuRegCount = 10;
constexpr RegList kAbiVolatileFpuRegs = 
    (1 << D0) | (1 << D1) | (1 << D2) | (1 << D3) | (1 << D4) |
    (1 << D5) | (1 << D6) | (1 << D7) | (1 << D8) | (1 << D9);
constexpr RegList kAbiVolatileFRegs = 
    (1 << F0) | (1 << F1) | (1 << F2) | (1 << F3) | (1 << F4) |
    (1 << F5) | (1 << F6) | (1 << F7) | (1 << F8) | (1 << F9) |
    (1 << F10) | (1 << F11) | (1 << F12) | (1 << F13) | (1 << F14) |
    (1 << F15) | (1 << F16) | (1 << F17) | (1 << F18) | (1 << F19);
constexpr RegList kAllFpuRegistersList = 
    (1 << D0) | (1 << D1) | (1 << D2) | (1 << D3) | (1 << D4) |
    (1 << D5) | (1 << D6) | (1 << D7) | (1 << D8) | (1 << D9) |
    (1 << D10) | (1 << D11) | (1 << D12) | (1 << D13) | (1 << D14) |
    (1 << D15);
constexpr RegList kAllFRegistersList = 
    (1 << F0) | (1 << F1) | (1 << F2) | (1 << F3) | (1 << F4) |
    (1 << F5) | (1 << F6) | (1 << F7) | (1 << F8) | (1 << F9) |
    (1 << F10) | (1 << F11) | (1 << F12) | (1 << F13) | (1 << F14) |
    (1 << F15) | (1 << F16) | (1 << F17) | (1 << F18) | (1 << F19) |
    (1 << F20) | (1 << F21) | (1 << F22) | (1 << F23) | (1 << F24) |
    (1 << F25) | (1 << F26) | (1 << F27) | (1 << F28) | (1 << F29) |
    (1 << F30) | (1 << F31);

constexpr int kNumberOfDartAvailableCpuRegs =
    kNumberOfCpuRegisters - kNumberOfReservedCpuRegisters;
const intptr_t kStoreBufferWrapperSize = 48;
const intptr_t kPreferredLoopAlignment = 1;

class CallingConventions {
 public:
  static const Register ArgumentRegisters[];
  static constexpr intptr_t kArgumentRegisters = kAbiArgumentCpuRegs;
  static constexpr intptr_t kNumArgRegs = 4;

  static constexpr intptr_t kFpuArgumentRegisters = (1 << D6) | (1 << D7);
  static const FpuRegister FpuArgumentRegisters[];
  static constexpr intptr_t kNumFpuArgRegs = 2;

  static constexpr intptr_t kCalleeSaveCpuRegisters = kAbiPreservedCpuRegs;

  static constexpr Register kReturnReg = V0;
  static constexpr Register kSecondReturnReg = V1;
  static constexpr Register kPointerToReturnStructRegisterReturn = kReturnReg;

  static constexpr FpuRegister kReturnFpuReg = D0;
  static constexpr FpuRegister kSecondReturnFpuReg = D1;

  static constexpr Register kFirstNonArgumentRegister = T0;
  static constexpr Register kSecondNonArgumentRegister = T1;
  static constexpr Register kStackPointerRegister = SPREG;

  // Whether larger than wordsize arguments are aligned to even registers.
  static constexpr AlignmentStrategy kArgumentRegisterAlignment =
      kAlignedToWordSizeAndValueSize;
  static constexpr AlignmentStrategy kArgumentRegisterAlignmentVarArgs =
      kArgumentRegisterAlignment;

  // How stack arguments are aligned.
  static constexpr AlignmentStrategy kArgumentStackAlignment =
      kAlignedToWordSizeAndValueSize;
  static constexpr AlignmentStrategy kArgumentStackAlignmentVarArgs =
      kArgumentStackAlignment;

  static constexpr AlignmentStrategy kFieldAlignment = kAlignedToValueSize;

  static constexpr ExtensionStrategy kReturnRegisterExtension = kExtendedTo4;
  static constexpr ExtensionStrategy kArgumentRegisterExtension = kExtendedTo4;
  static constexpr ExtensionStrategy kArgumentStackExtension = kExtendedTo4;
};

struct DartCallingConvention {
  static constexpr Register kCpuRegistersForArgs[] = {A0, A1, A2, A3};
  static constexpr FpuRegister kFpuRegistersForArgs[] = {D6, D7};
};

extern const char* const cpu_reg_names[kNumberOfCpuRegisters];
extern const char* const cpu_reg_abi_names[kNumberOfCpuRegisters];
extern const char* const fpu_reg_names[kNumberOfFRegisters];

// Registers in addition to those listed in TypeTestABI used inside the
// implementation of type testing stubs that are _not_ preserved.
struct TTSInternalRegs {
  static constexpr Register kInstanceTypeArgumentsReg = S1;
  static constexpr Register kScratchReg = S2;
  static constexpr Register kSubTypeArgumentReg = S4;
  static constexpr Register kSuperTypeArgumentReg = S5;

  // Must be pushed/popped whenever generic type arguments are being checked as
  // they overlap with registers in TypeTestABI.
  static constexpr intptr_t kSavedTypeArgumentRegisters = 0;

  static constexpr intptr_t kInternalRegisters =
      ((1 << kInstanceTypeArgumentsReg) | (1 << kScratchReg) |
       (1 << kSubTypeArgumentReg) | (1 << kSuperTypeArgumentReg)) &
      ~kSavedTypeArgumentRegisters;
};

// Registers in addition to those listed in TypeTestABI used inside the
// implementation of subtype test cache stubs that are _not_ preserved.
struct STCInternalRegs {
  static constexpr Register kInstanceCidOrSignatureReg = S1;

  static constexpr intptr_t kInternalRegisters =
      (1 << kInstanceCidOrSignatureReg);
};

// Calling convention when calling TypeTestingStub and SubtypeTestCacheStub.
struct TypeTestABI {
  static constexpr Register kInstanceReg = A0;
  static constexpr Register kDstTypeReg = A3;
  static constexpr Register kInstantiatorTypeArgumentsReg = A1;
  static constexpr Register kFunctionTypeArgumentsReg = A2;
  static constexpr Register kSubtypeTestCacheReg = V1;
  static constexpr Register kScratchReg = T2;

  // For calls to InstanceOfStub.
  static constexpr Register kInstanceOfResultReg = V0;
  // For calls to SubtypeNTestCacheStub. Must not be the same as any non-scratch
  // register above.
  static constexpr Register kSubtypeTestCacheResultReg = kScratchReg;

  static constexpr intptr_t kPreservedAbiRegisters =
      (1 << kInstanceReg) | (1 << kDstTypeReg) |
      (1 << kInstantiatorTypeArgumentsReg) | (1 << kFunctionTypeArgumentsReg);

  static constexpr intptr_t kNonPreservedAbiRegisters =
      TTSInternalRegs::kInternalRegisters |
      STCInternalRegs::kInternalRegisters | (1 << kSubtypeTestCacheReg) |
      (1 << kScratchReg) | (1 << kSubtypeTestCacheResultReg) | (1 << CODE_REG);

  static constexpr intptr_t kAbiRegisters =
      kPreservedAbiRegisters | kNonPreservedAbiRegisters;
};

// ABI for InitStaticFieldStub.
struct InitStaticFieldABI {
  static constexpr Register kFieldReg = T2;
  static constexpr Register kResultReg = V0;
};

struct AssertSubtypeABI {
  static constexpr Register kSubTypeReg = T1;
  static constexpr Register kSuperTypeReg = T2;
  static constexpr Register kInstantiatorTypeArgumentsReg = T3;
  static constexpr Register kFunctionTypeArgumentsReg = T4;
  static constexpr Register kDstNameReg = T5;

  static constexpr intptr_t kAbiRegisters =
      (1 << kSubTypeReg) | (1 << kSuperTypeReg) |
      (1 << kInstantiatorTypeArgumentsReg) | (1 << kFunctionTypeArgumentsReg) |
      (1 << kDstNameReg);

  // No result register, as AssertSubtype is only run for side effect
  // (throws if the subtype check fails).
};

// ABI for LateInitializationError stubs.
struct LateInitializationErrorABI {
  static constexpr Register kFieldReg = T2;
};

// ABI for ReThrowStub.
struct ReThrowABI {
  static constexpr Register kExceptionReg = V0;
  static constexpr Register kStackTraceReg = V1;
};

// ABI for AllocateObjectStub.
struct AllocateObjectABI {
  static constexpr Register kResultReg = V0;
  static constexpr Register kTypeArgumentsReg = A2;
  static constexpr Register kTagsReg = A1;
};

// ABI for AllocateClosureStub.
struct AllocateClosureABI {
  static constexpr Register kResultReg = AllocateObjectABI::kResultReg;
  static constexpr Register kFunctionReg = T1;
  static constexpr Register kContextReg = T2;
  static constexpr Register kInstantiatorTypeArgsReg = T3;
  static constexpr Register kScratchReg = T4;
};

// ABI for AllocateRecordStub.
struct AllocateRecordABI {
  static constexpr Register kResultReg = AllocateObjectABI::kResultReg;
  static constexpr Register kShapeReg = T1;
  static constexpr Register kTemp1Reg = T2;
  static constexpr Register kTemp2Reg = T3;
};

// ABI for AllocateSmallRecordStub (AllocateRecord2, AllocateRecord2Named,
// AllocateRecord3, AllocateRecord3Named).
struct AllocateSmallRecordABI {
  static constexpr Register kResultReg = AllocateObjectABI::kResultReg;
  static constexpr Register kShapeReg = T1;
  static constexpr Register kValue0Reg = T2;
  static constexpr Register kValue1Reg = T3;
  static constexpr Register kValue2Reg = T4;
  static constexpr Register kTempReg = TMP;
};

// ABI for BoxDoubleStub.
struct BoxDoubleStubABI {
  static constexpr FpuRegister kValueReg = D6;
  static constexpr Register kTempReg = T1;
  static constexpr Register kResultReg = V0;
};

// ABI for DoubleToIntegerStub.
struct DoubleToIntegerStubABI {
  static constexpr FpuRegister kInputReg = D6;
  static constexpr Register kRecognizedKindReg = V0;
  static constexpr Register kResultReg = V0;
};

// ABI for SuspendStub (AwaitStub, AwaitWithTypeCheckStub, YieldAsyncStarStub,
// SuspendSyncStarAtStartStub, SuspendSyncStarAtYieldStub).
struct SuspendStubABI {
  static constexpr Register kArgumentReg = A0;
  static constexpr Register kTypeArgsReg = T0;  // Can be the same as kTempReg
  static constexpr Register kTempReg = T0;
  static constexpr Register kFrameSizeReg = T1;
  static constexpr Register kSuspendStateReg = T2;
  static constexpr Register kFunctionDataReg = T3;
  static constexpr Register kSrcFrameReg = T4;
  static constexpr Register kDstFrameReg = T5;

  // Number of bytes to skip after
  // suspend stub return address in order to resume.
  static constexpr intptr_t kResumePcDistance = 0;
};

// ABI for InitSuspendableFunctionStub (InitAsyncStub, InitAsyncStarStub,
// InitSyncStarStub).
struct InitSuspendableFunctionStubABI {
  static constexpr Register kTypeArgsReg = A0;
};

// ABI for CloneSuspendStateStub.
struct CloneSuspendStateStubABI {
  static constexpr Register kSourceReg = A0;
  static constexpr Register kDestinationReg = A1;
  static constexpr Register kTempReg = T1;
  static constexpr Register kFrameSizeReg = T2;
  static constexpr Register kSrcFrameReg = T3;
  static constexpr Register kDstFrameReg = T4;
};

// ABI for FfiAsyncCallbackSendStub.
struct FfiAsyncCallbackSendStubABI {
  static constexpr Register kArgsReg = A0;
};

// ABI for DispatchTableNullErrorStub and consequently for all dispatch
// table calls (though normal functions will not expect or use this
// register). This ABI is added to distinguish memory corruption errors from
// null errors.
struct DispatchTableNullErrorABI {
  static constexpr Register kClassIdReg = V0;
};

enum ScaleFactor {
  TIMES_1 = 0,
  TIMES_2 = 1,
  TIMES_4 = 2,
  TIMES_8 = 3,
  TIMES_16 = 4,
// Don't use (dart::)kWordSizeLog2, as this needs to work for crossword as
// well. If this is included, we know the target is 32 bit.
#if defined(TARGET_ARCH_IS_32_BIT)
  // Used for Smi-boxed indices.
  TIMES_HALF_WORD_SIZE = kInt32SizeLog2 - 1,
  // Used for unboxed indices.
  TIMES_WORD_SIZE = kInt32SizeLog2,
#else
#error "Unexpected word size"
#endif
#if !defined(DART_COMPRESSED_POINTERS)
  TIMES_COMPRESSED_WORD_SIZE = TIMES_WORD_SIZE,
#else
#error Cannot compress MIPS32
#endif
  // Used for Smi-boxed indices.
  TIMES_COMPRESSED_HALF_WORD_SIZE = TIMES_COMPRESSED_WORD_SIZE - 1,
};

// Constants used for decoding or encoding the individual fields of
// instructions. Based on "Table 5.30 CPU Instruction Format Fields" in
// MIPS® Architecture For Programmers Volume I-A:
// Introduction to the MIPS32® Architecture, Revision 6.01.
// Available at:
// https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00082-2B-MIPS32INT-AFP-06.01.pdf
enum InstructionFields {
  kOpcodeShift = 26,
  kOpcodeBits = 6,
  kRsShift = 21,
  kRsBits = 5,
  kFmtShift = 21,
  kFmtBits = 5,
  kRtShift = 16,
  kRtBits = 5,
  kFtShift = 16,
  kFtBits = 5,
  kRdShift = 11,
  kRdBits = 5,
  kFsShift = 11,
  kFsBits = 5,
  kSaShift = 6,
  kSaBits = 5,
  kFdShift = 6,
  kFdBits = 5,
  kFunctionShift = 0,
  kFunctionBits = 6,
  kCop1FnShift = 0,
  kCop1FnBits = 6,
  kCop1SubShift = 21,
  kCop1SubBits = 5,
  kImmShift = 0,
  kImmBits = 16,
  kInstrShift = 0,
  kInstrBits = 26,
  kBreakCodeShift = 6,
  kBreakCodeBits = 20,
  kSyncCodeShift = 6,
  kFpuCCShift = 8,
  kFpuCCBits = 3,

  kBranchOffsetMask = 0x0000ffff,
};

enum Opcode {
  SPECIAL = 0,
  REGIMM = 1,
  J = 2,
  JAL = 3,
  BEQ = 4,
  BNE = 5,
  BLEZ = 6,
  BGTZ = 7,
  ADDI = 8,
  ADDIU = 9,
  SLTI = 10,
  SLTIU = 11,
  ANDI = 12,
  ORI = 13,
  XORI = 14,
  LUI = 15,
  CPO0 = 16,
  COP1 = 17,
  COP2 = 18,
  COP1X = 19,
  BEQL = 20,
  BNEL = 21,
  BLEZL = 22,
  BGTZL = 23,
  SPECIAL2 = 28,
  JALX = 29,
  SPECIAL3 = 31,
  LB = 32,
  LH = 33,
  LWL = 34,
  LW = 35,
  LBU = 36,
  LHU = 37,
  LWR = 38,
  SB = 40,
  SH = 41,
  SWL = 42,
  SW = 43,
  SWR = 46,
  CACHE = 47,
  LL = 48,
  LWC1 = 49,
  LWC2 = 50,
  PREF = 51,
  LDC1 = 53,
  LDC2 = 54,
  SC = 56,
  SWC1 = 57,
  SWC2 = 58,
  SDC1 = 61,
  SDC2 = 62,
};

enum SpecialFunction {
  // SPECIAL opcodes.
  SLL = 0,
  MOVCI = 1,
  SRL = 2,
  SRA = 3,
  SLLV = 4,
  SRLV = 6,
  SRAV = 7,
  JR = 8,
  JALR = 9,
  MOVZ = 10,
  MOVN = 11,
  SYSCALL = 12,
  BREAK = 13,
  SYNC = 15,
  MFHI = 16,
  MTHI = 17,
  MFLO = 18,
  MTLO = 19,
  MULT = 24,
  MULTU = 25,
  DIV = 26,
  DIVU = 27,
  ADD = 32,
  ADDU = 33,
  SUB = 34,
  SUBU = 35,
  AND = 36,
  OR = 37,
  XOR = 38,
  NOR = 39,
  SLT = 42,
  SLTU = 43,
  TGE = 48,
  TGEU = 49,
  TLT = 50,
  TLTU = 51,
  TEQ = 52,
  TNE = 54,

  // SPECIAL2 opcodes.
  MADD = 0,
  MADDU = 1,
  CLZ = 32,
  CLO = 33,
};

enum RtRegImm {
  BLTZ = 0,
  BGEZ = 1,
  BLTZL = 2,
  BGEZL = 3,
  TGEI = 8,
  TGEIU = 9,
  TLTI = 10,
  TLTIU = 11,
  TEQI = 12,
  TNEI = 14,
  BLTZAL = 16,
  BGEZAL = 17,
  BLTZALL = 18,
  BGEZALL = 19,
  SYNCI = 31,
};

enum Cop1Function {
  COP1_ADD = 0x00,
  COP1_SUB = 0x01,
  COP1_MUL = 0x02,
  COP1_DIV = 0x03,
  COP1_SQRT = 0x04,
  COP1_MOV = 0x06,
  COP1_NEG = 0x07,
  COP1_TRUNC_W = 0x0d,
  COP1_CVT_S = 0x20,
  COP1_CVT_D = 0x21,
  COP1_C_F = 0x30,
  COP1_C_UN = 0x31,
  COP1_C_EQ = 0x32,
  COP1_C_UEQ = 0x33,
  COP1_C_OLT = 0x34,
  COP1_C_ULT = 0x35,
  COP1_C_OLE = 0x36,
  COP1_C_ULE = 0x37,
};

enum Cop1Sub {
  COP1_MF = 0,
  COP1_MT = 4,
  COP1_BC = 8,
};

enum Format {
  FMT_S = 16,
  FMT_D = 17,
  FMT_W = 20,
  FMT_L = 21,
  FMT_PS = 22,
};

class Instr {
 public:
  enum {
    kInstrSize = 4,
  };

  static const int32_t kNopInstruction = 0;

  // Reserved break instruction codes.
  static const int32_t kBreakPointCode = 0xdeb0;      // For breakpoint.
  static const int32_t kStopMessageCode = 0xdeb1;     // For Stop(message).
  static const int32_t kSimulatorBreakCode = 0xdeb2;  // For breakpoint in sim.
  static const int32_t kSimulatorRedirectCode = 0xca11;  // For redirection.

  static const int32_t kBreakPointZeroInstruction =
      (SPECIAL << kOpcodeShift) | (BREAK << kFunctionShift);

  // Breakpoint instruction filling assembler code buffers in debug mode.
  static const int32_t kBreakPointInstruction =
      kBreakPointZeroInstruction | (kBreakPointCode << kBreakCodeShift);

  // Breakpoint instruction used by the simulator.
  // Should be distinct from kBreakPointInstruction and from a typical user
  // breakpoint inserted in generated code for debugging, e.g. break_(0).
  static const int32_t kSimulatorBreakpointInstruction =
      kBreakPointZeroInstruction | (kSimulatorBreakCode << kBreakCodeShift);

  // Runtime call redirection instruction used by the simulator.
  static const int32_t kSimulatorRedirectInstruction =
      kBreakPointZeroInstruction | (kSimulatorRedirectCode << kBreakCodeShift);  

  // Get the raw instruction bits.
  inline int32_t InstructionBits() const {
    return *reinterpret_cast<const int32_t*>(this);
  }

  // Set the raw instruction bits to value.
  inline void SetInstructionBits(int32_t value) {
    *reinterpret_cast<int32_t*>(this) = value;
  }

  inline void SetImmInstrBits(Opcode op,
                              Register rs,
                              Register rt,
                              uint16_t imm) {
    SetInstructionBits(op << kOpcodeShift | rs << kRsShift | rt << kRtShift |
                       imm << kImmShift);
  }

  inline void SetSpecialInstrBits(SpecialFunction f,
                                  Register rs,
                                  Register rt,
                                  Register rd) {
    SetInstructionBits(SPECIAL << kOpcodeShift | f << kFunctionShift |
                       rs << kRsShift | rt << kRtShift | rd << kRdShift);
  }

  // Read one particular bit out of the instruction bits.
  inline int32_t Bit(int nr) const { return (InstructionBits() >> nr) & 1; }

  // Read a bit field out of the instruction bits.
  inline int32_t Bits(int shift, int count) const {
    return (InstructionBits() >> shift) & ((1 << count) - 1);
  }

  // Accessors to the different named fields used in the MIPS encoding.
  inline Opcode OpcodeField() const {
    return static_cast<Opcode>(Bits(kOpcodeShift, kOpcodeBits));
  }

  inline void SetOpcodeField(Opcode b) {
    int32_t instr = InstructionBits();
    int32_t mask = ((1 << kOpcodeBits) - 1) << kOpcodeShift;
    SetInstructionBits((b << kOpcodeShift) | (instr & ~mask));
  }

  inline Register RsField() const {
    return static_cast<Register>(Bits(kRsShift, kRsBits));
  }

  inline Register RtField() const {
    return static_cast<Register>(Bits(kRtShift, kRtBits));
  }

  inline Register RdField() const {
    return static_cast<Register>(Bits(kRdShift, kRdBits));
  }

  inline FRegister FsField() const {
    return static_cast<FRegister>(Bits(kFsShift, kFsBits));
  }

  inline FRegister FtField() const {
    return static_cast<FRegister>(Bits(kFtShift, kFtBits));
  }

  inline FRegister FdField() const {
    return static_cast<FRegister>(Bits(kFdShift, kFdBits));
  }

  inline int SaField() const { return Bits(kSaShift, kSaBits); }

  inline int32_t UImmField() const { return Bits(kImmShift, kImmBits); }

  inline int32_t SImmField() const {
    // Sign-extend the imm field.
    return (Bits(kImmShift, kImmBits) << (32 - kImmBits)) >> (32 - kImmBits);
  }

  inline int32_t BreakCodeField() const {
    return Bits(kBreakCodeShift, kBreakCodeBits);
  }

  inline SpecialFunction FunctionField() const {
    return static_cast<SpecialFunction>(Bits(kFunctionShift, kFunctionBits));
  }

  inline RtRegImm RegImmFnField() const {
    return static_cast<RtRegImm>(Bits(kRtShift, kRtBits));
  }

  inline void SetRegImmFnField(RtRegImm b) {
    int32_t instr = InstructionBits();
    int32_t mask = ((1 << kRtBits) - 1) << kRtShift;
    SetInstructionBits((b << kRtShift) | (instr & ~mask));
  }

  inline bool IsBreakPoint() {
    return (OpcodeField() == SPECIAL) && (FunctionField() == BREAK);
  }

  inline Cop1Function Cop1FunctionField() const {
    return static_cast<Cop1Function>(Bits(kCop1FnShift, kCop1FnBits));
  }

  inline Cop1Sub Cop1SubField() const {
    return static_cast<Cop1Sub>(Bits(kCop1SubShift, kCop1SubBits));
  }

  inline bool HasFormat() const {
    return (OpcodeField() == COP1) && (Bit(25) == 1);
  }

  inline Format FormatField() const {
    return static_cast<Format>(Bits(kFmtShift, kFmtBits));
  }

  inline int32_t FpuCCField() const { return Bits(kFpuCCShift, kFpuCCBits); }

  // Instructions are read out of a code stream. The only way to get a
  // reference to an instruction is to convert a pc. There is no way
  // to allocate or create instances of class Instr.
  // Use the At(pc) function to create references to Instr.
  static Instr* At(uword pc) { return reinterpret_cast<Instr*>(pc); }

 private:
  DISALLOW_ALLOCATION();
  DISALLOW_IMPLICIT_CONSTRUCTORS(Instr);
};

}  // namespace dart

#endif  // RUNTIME_VM_CONSTANTS_MIPS_H_

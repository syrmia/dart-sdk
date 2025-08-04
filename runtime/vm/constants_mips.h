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
  R1 = 1,
  kNumberOfCpuRegisters = 2,
  kNoRegister = -1,
  FP = R0,
  RA = R1,
};

enum FRegister {
  F0 = 0,
  F1 = 1,
  kNumberOfFRegisters = 2,
};

const int kFpuRegisterSize = 0;
typedef double fpu_register_t;

class Condition : public ValueObject {
 public:
  enum RelationOperator {
    INVALID_RELATION = 13,
  };

  Condition() : ValueObject() { UNIMPLEMENTED(); }

  // Copy constructor.
  Condition(const Condition& other) : ValueObject() { UNIMPLEMENTED(); }

  // Copy assignment operator.
  Condition& operator=(const Condition& other) {
    UNIMPLEMENTED();
    return *this;
  }

};

static inline Condition InvertCondition(Condition c) {
  UNIMPLEMENTED();
  return c;
}

struct InstantiationABI {
  static constexpr Register kUninstantiatedTypeArgumentsReg = R0;
  static constexpr Register kInstantiatorTypeArgumentsReg = R0;
  static constexpr Register kFunctionTypeArgumentsReg = R0;
  static constexpr Register kResultTypeArgumentsReg = R0;
  static constexpr Register kResultTypeReg = R0;
  static constexpr Register kScratchReg = R0;
};

struct InstantiateTAVInternalRegs {
  static constexpr intptr_t kSavedRegisters = 0;
  static constexpr Register kEntryStartReg = R0;
  static constexpr Register kProbeMaskReg = R0;
  static constexpr Register kProbeDistanceReg = R0;
  static constexpr Register kCurrentEntryIndexReg = R0;
};

struct TTSInternalRegs {
  static constexpr Register kInstanceTypeArgumentsReg = R0;
  static constexpr Register kScratchReg = R0;
  static constexpr Register kSubTypeArgumentReg = R0;
  static constexpr Register kSuperTypeArgumentReg = R0;
  static constexpr intptr_t kSavedTypeArgumentRegisters = 0;
  static constexpr intptr_t kInternalRegisters = 0;
};

struct STCInternalRegs {
  static constexpr Register kInstanceCidOrSignatureReg = R0;
  static constexpr intptr_t kInternalRegisters = 0;
};

const Register kWriteBarrierValueReg = R0;

enum DRegister {
  D0 = 0,
  D1,
  kNumberOfDRegisters = 2,
};

// Architecture independent aliases.
typedef DRegister FpuRegister;
const FpuRegister FpuTMP = D1;
const int kNumberOfFpuRegisters = 0;
const FpuRegister kNoFpuRegister = D0;

// Register aliases.
const Register TMP = R0;
const Register TMP2 = R0;
const Register CODE_REG = R0;
const Register PP = R0;
const Register FUNCTION_REG = R0;
const Register SPREG = R0;
const Register FPREG = R0;
const Register ARGS_DESC_REG = R0;
const Register THR = R0;
const Register kExceptionObjectReg = R0;
const Register kStackTraceObjectReg = R0;

typedef uint32_t RegList;
const RegList kAllCpuRegistersList = 0x00000000;
const RegList kAbiPreservedCpuRegs = 0x00000000;
const RegList kReservedCpuRegisters = 0x00000000;
const RegList kDartAvailableCpuRegs = 0x00000000;
const RegList kDartVolatileCpuRegs = 0x00000000;
constexpr int kRegisterAllocationBias = 0;
constexpr RegList kAbiVolatileFpuRegs = 0x00000000;
constexpr RegList kAllFpuRegistersList = 0x00000000;
constexpr int kNumberOfDartAvailableCpuRegs = 0;
const intptr_t kStoreBufferWrapperSize = 0;
const intptr_t kPreferredLoopAlignment = 1;

struct AssertSubtypeABI {
  static constexpr Register kSubTypeReg = R0;
  static constexpr Register kSuperTypeReg = R0;
  static constexpr Register kInstantiatorTypeArgumentsReg = R0;
  static constexpr Register kFunctionTypeArgumentsReg = R0;
  static constexpr Register kDstNameReg = R0;
};

struct InitStaticFieldABI {
  static constexpr Register kFieldReg = R0;
  static constexpr Register kResultReg = R0;
};

struct InitLateStaticFieldInternalRegs {
  static constexpr Register kAddressReg = R0;
  static constexpr Register kScratchReg = R0;
};

struct InitInstanceFieldABI {
  static constexpr Register kInstanceReg = R0;
  static constexpr Register kFieldReg = R0;
  static constexpr Register kResultReg = R0;
};

struct InitLateInstanceFieldInternalRegs {
  static constexpr Register kAddressReg = R0;
  static constexpr Register kScratchReg = R0;
};

struct LateInitializationErrorABI {
  static constexpr Register kFieldReg = R0;
};

struct ThrowABI {
  static constexpr Register kExceptionReg = R0;
};

struct ReThrowABI {
  static constexpr Register kExceptionReg = R0;
  static constexpr Register kStackTraceReg = R0;
};

struct RangeErrorABI {
  static constexpr Register kLengthReg = R0;
  static constexpr Register kIndexReg = R0;
};

struct AllocateObjectABI {
  static constexpr Register kResultReg = R0;
  static constexpr Register kTypeArgumentsReg = R0;
  static constexpr Register kTagsReg = R0;
};

struct AllocateClosureABI {
  static constexpr Register kResultReg = R0;
  static constexpr Register kFunctionReg = R0;
  static constexpr Register kContextReg = R0;
  static constexpr Register kInstantiatorTypeArgsReg = R0;
  static constexpr Register kScratchReg = R0;
};

struct AllocateBoxABI {
  static constexpr Register kResultReg = R0;
  static constexpr Register kTempReg = R0;
};

struct AllocateRecordABI {
  static constexpr Register kResultReg = R0;
  static constexpr Register kShapeReg = R0;
  static constexpr Register kTemp1Reg = R0;
  static constexpr Register kTemp2Reg = R0;
};

struct BoxDoubleStubABI {
  static constexpr FpuRegister kValueReg = D0;
  static constexpr Register kTempReg = R0;
  static constexpr Register kResultReg = R0;
};

struct DoubleToIntegerStubABI {
  static constexpr FpuRegister kInputReg = D0;
  static constexpr Register kRecognizedKindReg = R0;
  static constexpr Register kResultReg = R0;
};

struct SuspendStubABI {
  static constexpr Register kArgumentReg = R0;
  static constexpr Register kTypeArgsReg = R0;
  static constexpr Register kTempReg = R0;
  static constexpr Register kFrameSizeReg = R0;
  static constexpr Register kSuspendStateReg = R0;
  static constexpr Register kFunctionDataReg = R0;
  static constexpr Register kSrcFrameReg = R0;
  static constexpr Register kDstFrameReg = R0;
  static constexpr intptr_t kResumePcDistance = 0;
};

struct InitSuspendableFunctionStubABI {
  static constexpr Register kTypeArgsReg = R0;
};

struct ResumeStubABI {
  static constexpr Register kSuspendStateReg = R0;
  static constexpr Register kTempReg = R0;
  static constexpr Register kFrameSizeReg = R0;
  static constexpr Register kSrcFrameReg = R0;
  static constexpr Register kDstFrameReg = R0;
  static constexpr Register kResumePcReg = R0;
  static constexpr Register kExceptionReg = R1;
  static constexpr Register kStackTraceReg = R1;
};

struct ReturnStubABI {
  static constexpr Register kSuspendStateReg = R0;
};

struct AsyncExceptionHandlerStubABI {
  static constexpr Register kSuspendStateReg = R0;
};

struct CloneSuspendStateStubABI {
  static constexpr Register kSourceReg = R0;
  static constexpr Register kDestinationReg = R0;
  static constexpr Register kTempReg = R0;
  static constexpr Register kFrameSizeReg = R0;
  static constexpr Register kSrcFrameReg = R0;
  static constexpr Register kDstFrameReg = R0;
};

struct FfiAsyncCallbackSendStubABI {
  static constexpr Register kArgsReg = R0;
};

struct DispatchTableNullErrorABI {
  static constexpr Register kClassIdReg = R0;
};

class CallingConventions {
 public:
  static const Register ArgumentRegisters[];
  static constexpr intptr_t kArgumentRegisters = 0;
  static constexpr intptr_t kNumArgRegs = 0;
  static constexpr Register kPointerToReturnStructRegisterCall = R0;


  static constexpr intptr_t kFpuArgumentRegisters = 0;
  static const FpuRegister FpuArgumentRegisters[];
  static constexpr intptr_t kNumFpuArgRegs = 0;

  static constexpr intptr_t kCalleeSaveCpuRegisters = 0;

  static constexpr bool kArgumentIntRegXorFpuReg = false;

  static constexpr Register kReturnReg = R0;
  static constexpr Register kSecondReturnReg = R0;
  static constexpr Register kPointerToReturnStructRegisterReturn = R0;

  static constexpr FpuRegister kReturnFpuReg = D0;
  static constexpr FpuRegister kSecondReturnFpuReg = D0;

  static constexpr Register kFfiAnyNonAbiRegister = R0;
  static constexpr Register kFirstNonArgumentRegister = R0;
  static constexpr Register kSecondNonArgumentRegister = R0;
  static constexpr Register kStackPointerRegister = R0;

  static constexpr AlignmentStrategy kArgumentRegisterAlignment =
      kAlignedToWordSize;
  static constexpr AlignmentStrategy kArgumentRegisterAlignmentVarArgs =
      kArgumentRegisterAlignment;

  static constexpr AlignmentStrategy kArgumentStackAlignment =
      kAlignedToWordSize;
  static constexpr AlignmentStrategy kArgumentStackAlignmentVarArgs =
      kArgumentStackAlignment;

  static constexpr AlignmentStrategy kFieldAlignment = kAlignedToValueSize;

  static constexpr ExtensionStrategy kReturnRegisterExtension = kExtendedTo4;
  static constexpr ExtensionStrategy kArgumentRegisterExtension = kExtendedTo4;
  static constexpr ExtensionStrategy kArgumentStackExtension = kExtendedTo4;

};

struct DartCallingConvention {
  static constexpr Register kCpuRegistersForArgs[] = {R0};
  static constexpr FpuRegister kFpuRegistersForArgs[] = {D0};
};

struct AllocateTypedDataArrayABI {
  static constexpr Register kResultReg = R0;
  static constexpr Register kLengthReg = R0;
};

struct AllocateSmallRecordABI {
  static constexpr Register kResultReg = R0;
  static constexpr Register kShapeReg = R0;
  static constexpr Register kValue0Reg = R0;
  static constexpr Register kValue1Reg = R0;
  static constexpr Register kValue2Reg = R0;
  static constexpr Register kTempReg = R0;
};

struct TypeTestABI {
  static constexpr Register kInstanceReg = R0;
  static constexpr Register kDstTypeReg = R0;
  static constexpr Register kInstantiatorTypeArgumentsReg = R0;
  static constexpr Register kFunctionTypeArgumentsReg = R0;
  static constexpr Register kSubtypeTestCacheReg = R1;
  static constexpr Register kScratchReg = R0;
  static constexpr Register kInstanceOfResultReg = R0;
  static constexpr Register kSubtypeTestCacheResultReg = R0;
  static constexpr intptr_t kPreservedAbiRegisters = 0;
  static constexpr intptr_t kNonPreservedAbiRegisters = 0;
  static constexpr intptr_t kAbiRegisters = 0;
};

struct AllocateArrayABI {
  static constexpr Register kResultReg = AllocateObjectABI::kResultReg;
  static constexpr Register kLengthReg = R0;
  static constexpr Register kTypeArgumentsReg = R0;
};

const char* const cpu_reg_names[1] = {};
const char* const cpu_reg_abi_names[1] = {};
const char* const fpu_reg_names[1] = {};

enum ScaleFactor {
  TIMES_1 = 0,
  TIMES_2 = 0,
  TIMES_4 = 0,
  TIMES_8 = 0,
  TIMES_16 = 0,
  TIMES_WORD_SIZE = 0,
  TIMES_COMPRESSED_HALF_WORD_SIZE = 0,
};

class Instr {
 public:
  enum {
    kInstrSize = 4,
  };

  static const int32_t kBreakPointInstruction = 0;
};

constexpr uword kBreakInstructionFiller = 0x0000000D;

inline Register ConcreteRegister(Register r) {
  UNIMPLEMENTED();
  return r;
}
#define LINK_REGISTER RA

}  // namespace dart

#endif  // RUNTIME_VM_CONSTANTS_MIPS_H_

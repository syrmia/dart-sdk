// Copyright (c) 2026, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.
// Classes that describe assembly patterns as used by inline caches.

#ifndef RUNTIME_VM_INSTRUCTIONS_MIPS_H_
#define RUNTIME_VM_INSTRUCTIONS_MIPS_H_

#ifndef RUNTIME_VM_INSTRUCTIONS_H_
#error Do not include instructions_mips.h directly; use instructions.h instead.
#endif

#include "vm/allocation.h"
#include "vm/constants.h"
#include "vm/native_function.h"
#include "vm/tagged_pointer.h"

#if !defined(DART_PRECOMPILED_RUNTIME)
#include "vm/compiler/assembler/assembler.h"
#endif  // !defined(DART_PRECOMPILED_RUNTIME)

namespace dart {

class ICData;
class Code;
class Object;
class ObjectPool;
class UntaggedCode;

class InstructionPattern : public AllStatic {
 public:
  // Decodes a load sequence ending at 'end' (the last instruction of the
  // load sequence is the instruction before the one at end).  Returns the
  // address of the first instruction in the sequence.  Returns the register
  // being loaded and the loaded immediate value in the output parameters
  // 'reg' and 'value' respectively.
  static uword DecodeLoadWordImmediate(uword end,
                                       Register* reg,
                                       intptr_t* value);

  // Encodes a load immediate sequence ending at 'end' (the last instruction of
  // the load sequence is the instruction before the one at end).
  //
  // Supports only a subset of [DecodeLoadWordImmediate], namely:
  //   lui reg, #imm_hi
  //   ori reg, reg, #imm_lo
  static void EncodeLoadWordImmediate(uword end, Register reg, intptr_t value);

  // Decodes a load sequence ending at 'end' (the last instruction of the
  // load sequence is the instruction before the one at end).  Returns the
  // address of the first instruction in the sequence.  Returns the register
  // being loaded and the index in the pool being read from in the output
  // parameters 'reg' and 'index' respectively.
  static uword DecodeLoadWordFromPool(uword end,
                                      Register* reg,
                                      intptr_t* index);
};

class CallPattern : public ValueObject {
 public:
  CallPattern(uword pc, const Code& code);

  CodePtr TargetCode() const;
  void SetTargetCode(const Code& target) const;

 private:
  const ObjectPool& object_pool_;
  intptr_t target_code_pool_index_;

  DISALLOW_COPY_AND_ASSIGN(CallPattern);
};

class ICCallPattern : public ValueObject {
 public:
  ICCallPattern(uword pc, const Code& code);

  ObjectPtr Data() const;
  void SetData(const Object& data) const;

  CodePtr TargetCode() const;
  void SetTargetCode(const Code& code) const;

 private:
  const ObjectPool& object_pool_;

  intptr_t target_pool_index_;
  intptr_t data_pool_index_;

  DISALLOW_COPY_AND_ASSIGN(ICCallPattern);
};

class NativeCallPattern : public ValueObject {
 public:
  NativeCallPattern(uword pc, const Code& code);

  CodePtr target() const;
  void set_target(const Code& target) const;

  NativeFunction native_function() const;
  void set_native_function(NativeFunction target) const;

 private:
  const ObjectPool& object_pool_;

  uword end_;
  intptr_t native_function_pool_index_;
  intptr_t target_code_pool_index_;

  DISALLOW_COPY_AND_ASSIGN(NativeCallPattern);
};

// Instance call that can switch between a direct monomorphic call, an IC call,
// and a megamorphic call.
//   load guarded cid            load ICData             load MegamorphicCache
//   load monomorphic target <-> load ICLookup stub  ->  load MMLookup stub
//   call target.entry           call stub.entry         call stub.entry
class SwitchableCallPatternBase : public ValueObject {
 public:
  explicit SwitchableCallPatternBase(const ObjectPool& object_pool);

  ObjectPtr data() const;
  void SetDataRelease(const Object& data) const;

 protected:
  const ObjectPool& object_pool_;
  intptr_t data_pool_index_;
  intptr_t target_pool_index_;

 private:
  DISALLOW_COPY_AND_ASSIGN(SwitchableCallPatternBase);
};

// See [SwitchableCallBase] for a switchable calls in general.
//
// The target slot is always a [Code] object: Either the code of the
// monomorphic function or a stub code.
class SwitchableCallPattern : public SwitchableCallPatternBase {
 public:
  SwitchableCallPattern(uword pc, const Code& code);

  ObjectPtr target() const;
  void SetTargetRelease(const Code& target) const;

 private:
  DISALLOW_COPY_AND_ASSIGN(SwitchableCallPattern);
};

// See [SwitchableCallBase] for a switchable calls in general.
//
// The target slot is always a direct entrypoint address: Either the entry point
// of the monomorphic function or a stub entry point.
class BareSwitchableCallPattern : public SwitchableCallPatternBase {
 public:
  explicit BareSwitchableCallPattern(uword pc);

  uword target_entry() const;
  void SetTargetRelease(const Code& target) const;

 private:
  DISALLOW_COPY_AND_ASSIGN(BareSwitchableCallPattern);
};

class ReturnPattern : public ValueObject {
 public:
  explicit ReturnPattern(uword pc);

  // jr(RA) = 1
  static const int kLengthInBytes = 1 * Instr::kInstrSize;

  int pattern_length_in_bytes() const { return kLengthInBytes; }

  bool IsValid() const;

 private:
  const uword pc_;
};

class PcRelativePatternBase : public ValueObject {
 public:
  // 16 bit signed integer which will get multiplied by 4.
  static constexpr intptr_t kLowerCallingRange = -(1 << 17) + Instr::kInstrSize;
  static constexpr intptr_t kUpperCallingRange = (1 << 17) - Instr::kInstrSize;

  explicit PcRelativePatternBase(uword pc) : pc_(pc) {}

  static constexpr int kLengthInBytes = 2 * Instr::kInstrSize;

  int32_t distance() {
    UNREACHABLE();
    return 0;
  }

  void set_distance(int32_t distance) { UNREACHABLE(); }

 protected:
  uword pc_;
};

class PcRelativeCallPattern : public PcRelativePatternBase {
 public:
  explicit PcRelativeCallPattern(uword pc) : PcRelativePatternBase(pc) {}

  bool IsValid() const;
};

class PcRelativeTailCallPattern : public PcRelativePatternBase {
 public:
  explicit PcRelativeTailCallPattern(uword pc) : PcRelativePatternBase(pc) {}

  bool IsValid() const;
};

class PcRelativeTrampolineJumpPattern : public ValueObject {
 public:
  explicit PcRelativeTrampolineJumpPattern(uword pattern_start)
      : pattern_start_(pattern_start) {
    USE(pattern_start_);
  }

  static constexpr int kLengthInBytes = 9 * Instr::kInstrSize;

  void Initialize();

  int32_t distance();
  void set_distance(int32_t distance);
  bool IsValid() const;

private:
  // This offset must be applied to account for the fact that
  // PC is read in 6th instruction
  static constexpr intptr_t kDistanceOffset = -5 * Instr::kInstrSize;

  //  sw RA, -4(SP)
  static constexpr uint32_t kStoreRA =
                    SW << kOpcodeShift | SP << kRsShift | RA << kRtShift | static_cast<uint16_t>(-4);

  //  bal(label)
  static constexpr uint32_t kBranchAndLinkEncoding =
                    REGIMM << kOpcodeShift | R0 << kRsShift | BGEZAL << kRtShift | 1;

  //  add TMP, RA, TMP
  static constexpr uint32_t kAddRaTmpEncoding =
                    SPECIAL << kOpcodeShift | RA << kRsShift | TMP << kRtShift |
                    TMP << kRdShift | 0 << kSaShift | ADD << kFunctionShift;

  // jr(TMP)
  static constexpr uint32_t kJumpRegisterEncoding =
                    SPECIAL << kOpcodeShift | TMP << kRsShift | R0 << kRtShift |
                    R0 << kRdShift | 0 << kSaShift | JR << kFunctionShift;

  //  lw RA, -4(SP)
  static constexpr uint32_t kLoadRA =
                    LW << kOpcodeShift | SP << kRsShift | RA << kRtShift | static_cast<uint16_t>(-4);

  uword pattern_start_;
};

}  // namespace dart

#endif  // RUNTIME_VM_INSTRUCTIONS_MIPS_H_

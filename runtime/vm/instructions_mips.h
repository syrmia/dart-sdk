// Copyright (c) 2025, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.
// Classes that describe assembly patterns as used by inline caches.

#ifndef RUNTIME_VM_INSTRUCTIONS_MIPS_H_
#define RUNTIME_VM_INSTRUCTIONS_MIPS_H_

#include "vm/allocation.h"

namespace dart {

class ReturnPattern : public ValueObject {
 public:
  explicit ReturnPattern(uword pc);
};

class PcRelativePatternBase : public ValueObject {
 public:
  static constexpr intptr_t kLowerCallingRange = 0;
  static constexpr intptr_t kUpperCallingRange = 0;

  explicit PcRelativePatternBase(uword pc) {
    UNIMPLEMENTED();
  }
  static constexpr int kLengthInBytes = 0;
  int32_t distance() {
    UNIMPLEMENTED();
  }
  void set_distance(int32_t distance) {
    UNIMPLEMENTED();
  }

};

class PcRelativeCallPattern : public PcRelativePatternBase {
 public:
  explicit PcRelativeCallPattern(uword pc) : PcRelativePatternBase(pc) {
    UNIMPLEMENTED();
  }
  bool IsValid() const;
};

class PcRelativeTailCallPattern : public PcRelativePatternBase {
 public:
  explicit PcRelativeTailCallPattern(uword pc) : PcRelativePatternBase(pc) {
    UNIMPLEMENTED();
  }
 bool IsValid() const;
};

class PcRelativeTrampolineJumpPattern : public ValueObject {
 public:
  explicit PcRelativeTrampolineJumpPattern(uword pattern_start) {
    UNIMPLEMENTED();
  }
  static constexpr int kLengthInBytes = 0;
  void Initialize();
  int32_t distance();
  void set_distance(int32_t distance);
  bool IsValid() const;

};

}  // namespace dart

#endif  // RUNTIME_VM_INSTRUCTIONS_MIPS_H_

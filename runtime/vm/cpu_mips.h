// Copyright (c) 2025, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef RUNTIME_VM_CPU_MIPS_H_
#define RUNTIME_VM_CPU_MIPS_H_

#include "vm/allocation.h"

namespace dart {


class HostCPUFeatures : public AllStatic {
 public:
  static const char* hardware() {
    UNIMPLEMENTED();
    return "";
  }
};

class TargetCPUFeatures : public AllStatic {
 public:
  static void Init() {
    UNIMPLEMENTED();
  }
  static void Cleanup() {
    UNIMPLEMENTED();
  }
  static bool double_truncate_round_supported() {
    UNIMPLEMENTED();
    return false;
  }
};

}  // namespace dart

#endif  // RUNTIME_VM_CPU_MIPS_H_

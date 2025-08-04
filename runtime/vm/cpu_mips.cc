// Copyright (c) 2025, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"
#if defined(TARGET_ARCH_MIPS)

#include "vm/cpu.h"
#include "vm/cpu_mips.h"


namespace dart {

void CPU::FlushICache(uword start, uword size) {
  UNIMPLEMENTED();
}

const char* CPU::Id() {
  UNIMPLEMENTED();
  return "";
}

}  // namespace dart

#endif  // defined TARGET_ARCH_MIPS

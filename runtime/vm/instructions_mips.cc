// Copyright (c) 2025, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"  // Needed here to get TARGET_ARCH_MIPS.
#if defined(TARGET_ARCH_MIPS)

#include "vm/instructions.h"
#include "vm/instructions_mips.h"


namespace dart {

intptr_t TypeTestingStubCallPattern::GetSubtypeTestCachePoolIndex() {
  UNIMPLEMENTED();
  return 0;
}

}  // namespace dart

#endif  // defined TARGET_ARCH_MIPS

// Copyright (c) 2025, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef RUNTIME_VM_STACK_FRAME_MIPS_H_
#define RUNTIME_VM_STACK_FRAME_MIPS_H_

#if !defined(RUNTIME_VM_STACK_FRAME_H_)
#error Do not include stack_frame_mips.h directly; use stack_frame.h instead.
#endif

namespace dart {

/* MIPS Dart Frame Layout

               |                    | <- TOS
Callee frame   | ...                |
               | current RA         |    (PC of current frame)
               | callee's PC marker |
               +--------------------+
Current frame  | ...               T| <- SP of current frame
               | first local       T|
               | caller's PP       T|
               | CODE_REG          T|    (current frame's code object)
               | caller's FP        | <- FP of current frame
               | caller's RA        |    (PC of caller frame)
               +--------------------+
Caller frame   | last parameter     | <- SP of caller frame
               |  ...               |

               T against a slot indicates it needs to be traversed during GC.
*/

static constexpr int kDartFrameFixedSize = 4;  // PP, FP, RA, PC marker.
static constexpr int kSavedPcSlotFromSp = -1;

static constexpr int kFirstObjectSlotFromFp = -1;  // Used by GC to traverse stack.
static constexpr int kLastFixedObjectSlotFromFp = -2;

static constexpr int kFirstLocalSlotFromFp = -3;
static constexpr int kSavedCallerPpSlotFromFp = -2;
static constexpr int kPcMarkerSlotFromFp = -1;
static constexpr int kSavedCallerFpSlotFromFp = 0;
static constexpr int kSavedCallerPcSlotFromFp = 1;
static constexpr int kParamEndSlotFromFp = 1;  // One slot past last parameter.
static constexpr int kCallerSpSlotFromFp = 2;
static constexpr int kLastParamSlotFromEntrySp = 0;

// Entry and exit frame layout.
static constexpr int kExitLinkSlotFromEntryFp = -25;
COMPILE_ASSERT(kAbiPreservedCpuRegCount == 8);
COMPILE_ASSERT(kAbiPreservedFRegCount == 12);

}  // namespace dart

#endif  // RUNTIME_VM_STACK_FRAME_MIPS_H_

// Copyright (c) 2026, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.


#ifndef RUNTIME_VM_SIMULATOR_MIPS_H_
#define RUNTIME_VM_SIMULATOR_MIPS_H_

#ifndef RUNTIME_VM_SIMULATOR_H_
#error Do not include simulator_mips.h directly; use simulator.h.
#endif

namespace dart {

class Simulator {
    // The files simulator_mips.h, simulator_mips.cc and the empty class Simulator
    // inside it were added only so that it would be possible to run the command
    // `./tools/run_offsets_extractor.dart`,
    // in order to obtain offsets for the MIPS architecture.
    // The implementation of this class will be added later.
};

}  // namespace dart

#endif  // RUNTIME_VM_SIMULATOR_MIPS_H_

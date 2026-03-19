// Copyright (c) 2026, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

// Declares a Simulator for MIPS instructions if we are not generating a native
// MIPS binary. This Simulator allows us to run and debug MIPS code generation
// on regular desktop machines.
// Dart calls into generated code by "calling" the InvokeDartCode stub,
// which will start execution in the Simulator or forwards to the real entry
// on a MIPS HW platform.

#ifndef RUNTIME_VM_SIMULATOR_MIPS_H_
#define RUNTIME_VM_SIMULATOR_MIPS_H_

#ifndef RUNTIME_VM_SIMULATOR_H_
#error Do not include simulator_mips.h directly; use simulator.h.
#endif

#include "vm/constants.h"
#include "vm/random.h"

namespace dart {

class Isolate;
class SimulatorSetjmpBuffer;
class Thread;

class Simulator {
 public:
  static constexpr uword kSimulatorStackUnderflowSize = 64;

  Simulator();
  ~Simulator();

  // The currently executing Simulator instance, which is associated to the
  // current isolate.
  static Simulator* Current();

  // Accessors for register state.
  void set_register(Register reg, int32_t value);
  int32_t get_register(Register reg) const;

  // Accessor for the pc.
  void set_pc(int32_t value) { pc_ = value; }

  // Call on program start.
  static void Init();

  void JumpToFrame(uword pc, uword sp, uword fp, Thread* thread);

 private:
  // Special registers for the results of div, divu.
  int32_t hi_reg_;
  int32_t lo_reg_;

  int32_t registers_[kNumberOfCpuRegisters];
  int32_t fregisters_[kNumberOfFRegisters];
  int32_t fcsr_;
  int32_t pc_;

  // Simulator support.
  char* stack_;
  uword stack_limit_;
  uword overflow_stack_limit_;
  uword stack_base_;
  uint64_t icount_;
  bool delay_slot_;
  SimulatorSetjmpBuffer* last_setjmp_buffer_;
  Random random_;

  // Registered breakpoints.
  Instr* break_pc_;
  int32_t break_instr_;

  // Exclusive access reservation.
  uword exclusive_access_addr_;
  uword exclusive_access_value_;

  // Longjmp support for exceptions.
  SimulatorSetjmpBuffer* last_setjmp_buffer() { return last_setjmp_buffer_; }
  void set_last_setjmp_buffer(SimulatorSetjmpBuffer* buffer) {
    last_setjmp_buffer_ = buffer;
  }

  friend class SimulatorSetjmpBuffer;
  DISALLOW_COPY_AND_ASSIGN(Simulator);
};

}  // namespace dart

#endif  // RUNTIME_VM_SIMULATOR_MIPS_H_

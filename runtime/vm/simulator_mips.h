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
  int32_t get_pc() const { return pc_; }

  // Accessors for floating point register state.
  void set_fregister(FRegister reg, int32_t value);
  void set_fregister_float(FRegister reg, float value);
  void set_fregister_double(FRegister reg, double value);
  void set_fregister_long(FRegister reg, int64_t value);

  int32_t get_fregister(FRegister reg) const;
  float get_fregister_float(FRegister reg) const;
  double get_fregister_double(FRegister reg) const;
  int64_t get_fregister_long(FRegister reg) const;

  void set_dregister_bits(DRegister reg, int64_t value);
  void set_dregister(DRegister reg, double value);
  int64_t get_dregister_bits(DRegister reg) const;
  double get_dregister(DRegister reg) const;

  // Call on program start.
  static void Init();

  // Dart generally calls into generated code with 4 parameters. This is a
  // convenience function, which sets up the simulator state and grabs the
  // result on return. When fp_return is true the return value is the D0
  // floating point register. Otherwise, the return value is V1:V0.
  int64_t Call(int32_t entry,
               int32_t parameter0,
               int32_t parameter1,
               int32_t parameter2,
               int32_t parameter3,
               bool fp_return = false,
               bool fp_args = false);

  // Runtime and native call support.
  enum CallKind {
    kRuntimeCall,
    kLeafRuntimeCall,
    kLeafFloatRuntimeCall,
    kNativeCallWrapper
  };

  static uword RedirectExternalReference(uword function,
                                         CallKind call_kind,
                                         int argument_count);

  static uword FunctionForRedirect(uword redirect);

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
  uword top_exit_frame_info_;

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

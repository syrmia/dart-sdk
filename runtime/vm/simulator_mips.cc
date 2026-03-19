// Copyright (c) 2026, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include <setjmp.h>  // NOLINT
#include <stdlib.h>

#include "vm/globals.h"
#if defined(TARGET_ARCH_MIPS)

// Only build the simulator if not compiling for real MIPS hardware.
#if defined(USING_SIMULATOR)

#include "vm/simulator.h"

#include "vm/constants.h"
#include "vm/isolate.h"
#include "vm/os_thread.h"
#include "vm/stack_frame.h"
#include "vm/stack_resource.h"

namespace dart {

// SimulatorSetjmpBuffer are linked together, and the last created one
// is referenced by the Simulator. When an exception is thrown, the exception
// runtime looks at where to jump and finds the corresponding
// SimulatorSetjmpBuffer based on the stack pointer of the exception handler.
// The runtime then does a Longjmp on that buffer to return to the simulator.
class SimulatorSetjmpBuffer {
 public:
  void Longjmp() {
    // "This" is now the last setjmp buffer.
    simulator_->set_last_setjmp_buffer(this);
    longjmp(buffer_, 1);
  }

  explicit SimulatorSetjmpBuffer(Simulator* sim) {
    simulator_ = sim;
    link_ = sim->last_setjmp_buffer();
    sim->set_last_setjmp_buffer(this);
    sp_ = static_cast<uword>(sim->get_register(SP));
  }

  ~SimulatorSetjmpBuffer() {
    ASSERT(simulator_->last_setjmp_buffer() == this);
    simulator_->set_last_setjmp_buffer(link_);
  }

  SimulatorSetjmpBuffer* link() { return link_; }

  uword sp() { return sp_; }

 private:
  uword sp_;
  Simulator* simulator_;
  SimulatorSetjmpBuffer* link_;
  jmp_buf buffer_;

  friend class Simulator;
};

void Simulator::Init() {}

Simulator::Simulator() {
  // Setup simulator support first. Some of this information is needed to
  // setup the architecture state.
  // We allocate the stack here, the size is computed as the sum of
  // the size specified by the user and the buffer space needed for
  // handling stack overflow exceptions. To be safe in potential
  // stack underflows we also add some underflow buffer space.
  stack_ =
      new char[(OSThread::GetSpecifiedStackSize() + OSThread::kStackSizeBufferMax +
                kSimulatorStackUnderflowSize)];
  // Low address.
  stack_limit_ = reinterpret_cast<uword>(stack_);
    // Limit for StackOverflowError.
  overflow_stack_limit_ = stack_limit_ + OSThread::kStackSizeBufferMax;
  // High address.
  stack_base_ = overflow_stack_limit_ + OSThread::GetSpecifiedStackSize();

  icount_ = 0;
  Random random_;
  delay_slot_ = false;
  break_pc_ = NULL;
  break_instr_ = 0;
  last_setjmp_buffer_ = NULL;
  top_exit_frame_info_ = 0;

  // Setup architecture state.
  // All registers are initialized to zero to start with.
  for (int i = 0; i < kNumberOfCpuRegisters; i++) {
    registers_[i] = 0;
  }
  pc_ = 0;
  // The sp is initialized to point to the bottom (high address) of the
  // allocated stack area.
  registers_[SP] = stack_base_;

  // All double-precision registers are initialized to zero.
  for (int i = 0; i < kNumberOfFRegisters; i++) {
    fregisters_[i] = 0.0;
  }
  fcsr_ = 0;
}

Simulator::~Simulator() {
  delete[] stack_;
  Isolate* isolate = Isolate::Current();
  if (isolate != NULL) {
    isolate->set_simulator(NULL);
  }
}

// Get the active Simulator for the current isolate.
Simulator* Simulator::Current() {
  Simulator* simulator = Isolate::Current()->simulator();
  if (simulator == NULL) {
    simulator = new Simulator();
    Isolate::Current()->set_simulator(simulator);
  }
  return simulator;
}

// Sets the register in the architecture state.
void Simulator::set_register(Register reg, int32_t value) {
  if (reg != R0) {
    registers_[reg] = value;
  }
}

// Get the register from the architecture state.
int32_t Simulator::get_register(Register reg) const {
  if (reg == R0) {
    return 0;
  }
  return registers_[reg];
}

void Simulator::JumpToFrame(uword pc, uword sp, uword fp, Thread* thread) {
  // Walk over all setjmp buffers (simulated --> C++ transitions)
  // and try to find the setjmp associated with the simulated stack pointer.
  SimulatorSetjmpBuffer* buf = last_setjmp_buffer();
  while (buf->link() != NULL && buf->link()->sp() <= sp) {
    buf = buf->link();
  }
  ASSERT(buf != NULL);

  // The C++ caller has not cleaned up the stack memory of C++ frames.
  // Prepare for unwinding frames by destroying all the stack resources
  // in the previous C++ frames.
  StackResource::Unwind(thread);

  // Unwind the C++ stack and continue simulation in the target frame.
  set_pc(static_cast<int32_t>(pc));
  set_register(SP, static_cast<int32_t>(sp));
  set_register(FP, static_cast<int32_t>(fp));
  set_register(THR, reinterpret_cast<int32_t>(thread));
  // Set the tag.
  thread->set_vm_tag(VMTag::kDartTagId);
  // Clear top exit frame.
  thread->set_top_exit_frame_info(0);
  // Restore pool pointer.
  int32_t code =
      *reinterpret_cast<int32_t*>(fp + kPcMarkerSlotFromFp * kWordSize);
  int32_t pp = *reinterpret_cast<int32_t*>(code + Code::object_pool_offset() -
                                           kHeapObjectTag);
  set_register(CODE_REG, code);
  set_register(PP, pp);
  buf->Longjmp();
}

}  // namespace dart

#endif  // defined(USING_SIMULATOR)

#endif  // defined(TARGET_ARCH_MIPS)

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
#include "vm/lockers.h"
#include "vm/os.h"
#include "vm/os_thread.h"
#include "vm/stack_frame.h"
#include "vm/stack_resource.h"

namespace dart {

DEFINE_FLAG(uint64_t,
            trace_sim_after,
            ULLONG_MAX,
            "Trace simulator execution after instruction count reached.");
DEFINE_FLAG(uint64_t,
            stop_sim_at,
            ULLONG_MAX,
            "Instruction address or instruction count to stop simulator at.");

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

// When the generated code calls an external reference we need to catch that in
// the simulator. The external reference will be a function compiled for the
// host architecture. We need to call that function instead of trying to
// execute it with the simulator. We do that by redirecting the external
// reference to a break instruction with a special code (kSimulatorRedirectCode)
// that is handled by the simulator. We write the original destination of the
// jump just at a known offset from the break instruction so the simulator
// knows what to call.
class Redirection {
 public:
  uword address_of_svc_instruction() {
    return reinterpret_cast<uword>(&svc_instruction_);
  }

  uword external_function() const { return external_function_; }

  Simulator::CallKind call_kind() const { return call_kind_; }

  int argument_count() const { return argument_count_; }

  static Redirection* Get(uword external_function,
                          Simulator::CallKind call_kind,
                          int argument_count) {
    MutexLocker ml(mutex_);

    for (Redirection* current = list_; current != NULL; current = current->next_) {
      if (current->external_function_ == external_function) return current;
    }

    Redirection* old_head = list_.load(std::memory_order_relaxed);
    Redirection* redirection =
        new Redirection(external_function, call_kind, argument_count);
    redirection->next_ = old_head;

    // Use a memory fence to ensure all pending writes are written at the time
    // of updating the list head, so the profiling thread always has a valid
    // list to look at.
    list_.store(redirection, std::memory_order_release);

    return redirection;
  }

  static Redirection* FromSvcInstruction(Instr* svc_instruction) {
    char* addr_of_svc = reinterpret_cast<char*>(svc_instruction);
    char* addr_of_redirection =
        addr_of_svc - OFFSET_OF(Redirection, svc_instruction_);
    return reinterpret_cast<Redirection*>(addr_of_redirection);
  }

  // Please note that this function is called by the signal handler of the
  // profiling thread. It can therefore run at any point in time and is not
  // allowed to hold any locks - which is precisely the reason why the list is
  // prepend-only and a memory fence is used when writing the list head [list_]!
  static uword FunctionForRedirect(uword address_of_svc) {
    for (Redirection* current = list_.load(std::memory_order_acquire);
         current != nullptr; current = current->next_) {
      if (current->address_of_svc_instruction() == address_of_svc) {
        return current->external_function_;
      }
    }
    return 0;
  }

 private:
  Redirection(uword external_function,
              Simulator::CallKind call_kind,
              int argument_count)
      : external_function_(external_function),
        call_kind_(call_kind),
        argument_count_(argument_count),
        svc_instruction_(Instr::kSimulatorRedirectInstruction),
        next_(nullptr) {}

  uword external_function_;
  Simulator::CallKind call_kind_;
  int argument_count_;
  uint32_t svc_instruction_;
  Redirection* next_;
  static std::atomic<Redirection*> list_;
  static Mutex* mutex_;
};

std::atomic<Redirection*> Redirection::list_ = {nullptr};
Mutex* Redirection::mutex_ = new Mutex();

uword Simulator::RedirectExternalReference(uword function,
                                           CallKind call_kind,
                                           int argument_count) {
  Redirection* redirection =
      Redirection::Get(function, call_kind, argument_count);
  return redirection->address_of_svc_instruction();
}

uword Simulator::FunctionForRedirect(uword redirect) {
  return Redirection::FunctionForRedirect(redirect);
}

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

void Simulator::set_fregister(FRegister reg, int32_t value) {
  ASSERT(reg >= 0);
  ASSERT(reg < kNumberOfFRegisters);
  fregisters_[reg] = value;
}

void Simulator::set_fregister_float(FRegister reg, float value) {
  ASSERT(reg >= 0);
  ASSERT(reg < kNumberOfFRegisters);
  fregisters_[reg] = bit_cast<int32_t, float>(value);
}

void Simulator::set_fregister_long(FRegister reg, int64_t value) {
  ASSERT(reg >= 0);
  ASSERT(reg < kNumberOfFRegisters);
  ASSERT((reg & 1) == 0);
  fregisters_[reg] = Utils::Low32Bits(value);
  fregisters_[reg + 1] = Utils::High32Bits(value);
}

void Simulator::set_fregister_double(FRegister reg, double value) {
  const int64_t ival = bit_cast<int64_t, double>(value);
  set_fregister_long(reg, ival);
}

void Simulator::set_dregister_bits(DRegister reg, int64_t value) {
  ASSERT(reg >= 0);
  ASSERT(reg < kNumberOfDRegisters);
  FRegister lo = static_cast<FRegister>(reg * 2);
  FRegister hi = static_cast<FRegister>((reg * 2) + 1);
  set_fregister(lo, Utils::Low32Bits(value));
  set_fregister(hi, Utils::High32Bits(value));
}

void Simulator::set_dregister(DRegister reg, double value) {
  ASSERT(reg >= 0);
  ASSERT(reg < kNumberOfDRegisters);
  set_dregister_bits(reg, bit_cast<int64_t, double>(value));
}

// Get the register from the architecture state.
int32_t Simulator::get_register(Register reg) const {
  if (reg == R0) {
    return 0;
  }
  return registers_[reg];
}

int32_t Simulator::get_fregister(FRegister reg) const {
  ASSERT((reg >= 0) && (reg < kNumberOfFRegisters));
  return fregisters_[reg];
}


float Simulator::get_fregister_float(FRegister reg) const {
  ASSERT(reg >= 0);
  ASSERT(reg < kNumberOfFRegisters);
  return bit_cast<float, int32_t>(fregisters_[reg]);
}


int64_t Simulator::get_fregister_long(FRegister reg) const {
  ASSERT(reg >= 0);
  ASSERT(reg < kNumberOfFRegisters);
  ASSERT((reg & 1) == 0);
  const int32_t low = fregisters_[reg];
  const int32_t high = fregisters_[reg + 1];
  return Utils::LowHighTo64Bits(low, high);
}

double Simulator::get_fregister_double(FRegister reg) const {
  ASSERT(reg >= 0);
  ASSERT(reg < kNumberOfFRegisters);
  ASSERT((reg & 1) == 0);
  const int64_t value = get_fregister_long(reg);
  return bit_cast<double, int64_t>(value);
}

int64_t Simulator::get_dregister_bits(DRegister reg) const {
  ASSERT(reg >= 0);
  ASSERT(reg < kNumberOfDRegisters);
  FRegister lo = static_cast<FRegister>(reg * 2);
  FRegister hi = static_cast<FRegister>((reg * 2) + 1);
  return Utils::LowHighTo64Bits(get_fregister(lo), get_fregister(hi));
}

double Simulator::get_dregister(DRegister reg) const {
  ASSERT(reg >= 0);
  ASSERT(reg < kNumberOfDRegisters);
  const int64_t value = get_dregister_bits(reg);
  return bit_cast<double, int64_t>(value);
}

void Simulator::UnimplementedInstruction(Instr* instr) {
  char buffer[64];
  snprintf(buffer, sizeof(buffer), "Unimplemented instruction: pc=%p\n", instr);
  FATAL("%s", buffer);
}

void Simulator::HandleIllegalAccess(uword addr, Instr* instr) {
  uword fault_pc = get_pc();
  char buffer[128];
  snprintf(buffer, sizeof(buffer),
           "illegal memory access at 0x%" Px ", pc=0x%" Px "\n", addr,
           fault_pc);
  FATAL("%s", buffer);
}

void Simulator::UnalignedAccess(const char* msg, uword addr, Instr* instr) {
  char buffer[128];
  snprintf(buffer, sizeof(buffer), "pc=%p, unaligned %s at 0x%" Px "\n", instr,
           msg, addr);
  FATAL("%s", buffer);
}

bool Simulator::IsTracingExecution() const {
  return icount_ > FLAG_trace_sim_after;
}

void Simulator::Format(Instr* instr, const char* format) {
  OS::PrintErr("Simulator - unknown instruction: %s\n", format);
  UNIMPLEMENTED();
}

int8_t Simulator::ReadB(uword addr) {
  int8_t* ptr = reinterpret_cast<int8_t*>(addr);
  return *ptr;
}

uint8_t Simulator::ReadBU(uword addr) {
  uint8_t* ptr = reinterpret_cast<uint8_t*>(addr);
  return *ptr;
}

int16_t Simulator::ReadH(uword addr, Instr* instr) {
  if ((addr & 1) == 0) {
    int16_t* ptr = reinterpret_cast<int16_t*>(addr);
    return *ptr;
  }
  UnalignedAccess("signed halfword read", addr, instr);
  return 0;
}

uint16_t Simulator::ReadHU(uword addr, Instr* instr) {
  if ((addr & 1) == 0) {
    uint16_t* ptr = reinterpret_cast<uint16_t*>(addr);
    return *ptr;
  }
  UnalignedAccess("unsigned halfword read", addr, instr);
  return 0;
}

intptr_t Simulator::ReadW(uword addr, Instr* instr) {
  if ((addr & 3) == 0) {
    intptr_t* ptr = reinterpret_cast<intptr_t*>(addr);
    return *ptr;
  }
  UnalignedAccess("read", addr, instr);
  return 0;
}

void Simulator::WriteB(uword addr, uint8_t value) {
  uint8_t* ptr = reinterpret_cast<uint8_t*>(addr);
  *ptr = value;
}

void Simulator::WriteH(uword addr, uint16_t value, Instr* instr) {
  if ((addr & 1) == 0) {
    uint16_t* ptr = reinterpret_cast<uint16_t*>(addr);
    *ptr = value;
    return;
  }
  UnalignedAccess("halfword write", addr, instr);
}

void Simulator::WriteW(uword addr, intptr_t value, Instr* instr) {
  if ((addr & 3) == 0) {
    intptr_t* ptr = reinterpret_cast<intptr_t*>(addr);
    *ptr = value;
    return;
  }
  UnalignedAccess("write", addr, instr);
}

double Simulator::ReadD(uword addr, Instr* instr) {
  if ((addr & 7) == 0) {
    double* ptr = reinterpret_cast<double*>(addr);
    return *ptr;
  }
  UnalignedAccess("double-precision floating point read", addr, instr);
  return 0.0;
}

void Simulator::WriteD(uword addr, double value, Instr* instr) {
  if ((addr & 7) == 0) {
    double* ptr = reinterpret_cast<double*>(addr);
    *ptr = value;
    return;
  }
  UnalignedAccess("double-precision floating point write", addr, instr);
}

void Simulator::ClearExclusive() {
  exclusive_access_addr_ = 0;
  exclusive_access_value_ = 0;
}

intptr_t Simulator::ReadExclusiveW(uword addr, Instr* instr) {
  exclusive_access_addr_ = addr;
  exclusive_access_value_ = ReadW(addr, instr);
  return exclusive_access_value_;
}

intptr_t Simulator::WriteExclusiveW(uword addr, intptr_t value, Instr* instr) {
  // In a well-formed code store-exclusive instruction should always follow
  // a corresponding load-exclusive instruction with the same address.
  ASSERT((exclusive_access_addr_ == 0) || (exclusive_access_addr_ == addr));
  if (exclusive_access_addr_ != addr) {
    return 0;  // Failure.
  }

  int32_t old_value = static_cast<uint32_t>(exclusive_access_value_);
  ClearExclusive();

  if ((random_.NextUInt32() % 16) == 0) {
    return 0;  // Spurious failure.
  }

  auto atomic_addr = reinterpret_cast<RelaxedAtomic<int32_t>*>(addr);
  if (atomic_addr->compare_exchange_weak(old_value, value)) {
    return 1;  // Success.
  }
  return 0;  // Failure.
}

void Simulator::Execute() {
  UNIMPLEMENTED();
}

int64_t Simulator::Call(int32_t entry,
                        int32_t parameter0,
                        int32_t parameter1,
                        int32_t parameter2,
                        int32_t parameter3,
                        bool fp_return,
                        bool fp_args) {
  // Save the SP register before the call so we can restore it.
  int32_t sp_before_call = get_register(SP);

  // Setup parameters.
  if (fp_args) {
    set_fregister(F0, parameter0);
    set_fregister(F1, parameter1);
    set_fregister(F2, parameter2);
    set_fregister(F3, parameter3);
  } else {
    set_register(A0, parameter0);
    set_register(A1, parameter1);
    set_register(A2, parameter2);
    set_register(A3, parameter3);
  }

  // Make sure the activation frames are properly aligned.
  int32_t stack_pointer = sp_before_call;
  if (OS::ActivationFrameAlignment() > 1) {
    stack_pointer =
        Utils::RoundDown(stack_pointer, OS::ActivationFrameAlignment());
  }
  set_register(SP, stack_pointer);

  // Prepare to execute the code at entry.
  set_pc(entry);
  // Put down marker for end of simulation. The simulator will stop simulation
  // when the PC reaches this value. By saving the "end simulation" value into
  // RA the simulation stops when returning to this call point.
  set_register(RA, kEndSimulatingPC);

  // Remember the values of callee-saved registers.
  // The code below assumes that r9 is not used as sb (static base) in
  // simulator code and therefore is regarded as a callee-saved register.
  int32_t r16_val = get_register(R16);
  int32_t r17_val = get_register(R17);
  int32_t r18_val = get_register(R18);
  int32_t r19_val = get_register(R19);
  int32_t r20_val = get_register(R20);
  int32_t r21_val = get_register(R21);
  int32_t r22_val = get_register(R22);
  int32_t r23_val = get_register(R23);

  double d10_val = get_dregister(D10);
  double d11_val = get_dregister(D11);
  double d12_val = get_dregister(D12);
  double d13_val = get_dregister(D13);
  double d14_val = get_dregister(D14);
  double d15_val = get_dregister(D15);

  // Setup the callee-saved registers with a known value. To be able to check
  // that they are preserved properly across dart execution.
  int32_t callee_saved_value = icount_;
  set_register(R16, callee_saved_value);
  set_register(R17, callee_saved_value);
  set_register(R18, callee_saved_value);
  set_register(R19, callee_saved_value);
  set_register(R20, callee_saved_value);
  set_register(R21, callee_saved_value);
  set_register(R22, callee_saved_value);
  set_register(R23, callee_saved_value);

  set_dregister_bits(D10, callee_saved_value);
  set_dregister_bits(D11, callee_saved_value);
  set_dregister_bits(D12, callee_saved_value);
  set_dregister_bits(D13, callee_saved_value);
  set_dregister_bits(D14, callee_saved_value);
  set_dregister_bits(D15, callee_saved_value);

  // Start the simulation
  Execute();

  // Check that the callee-saved registers have been preserved.
  ASSERT(callee_saved_value == get_register(R16));
  ASSERT(callee_saved_value == get_register(R17));
  ASSERT(callee_saved_value == get_register(R18));
  ASSERT(callee_saved_value == get_register(R19));
  ASSERT(callee_saved_value == get_register(R20));
  ASSERT(callee_saved_value == get_register(R21));
  ASSERT(callee_saved_value == get_register(R22));
  ASSERT(callee_saved_value == get_register(R23));

  ASSERT(callee_saved_value == get_dregister_bits(D10));
  ASSERT(callee_saved_value == get_dregister_bits(D11));
  ASSERT(callee_saved_value == get_dregister_bits(D12));
  ASSERT(callee_saved_value == get_dregister_bits(D13));
  ASSERT(callee_saved_value == get_dregister_bits(D14));
  ASSERT(callee_saved_value == get_dregister_bits(D15));

  // Restore callee-saved registers with the original value.
  set_register(R16, r16_val);
  set_register(R17, r17_val);
  set_register(R18, r18_val);
  set_register(R19, r19_val);
  set_register(R20, r20_val);
  set_register(R21, r21_val);
  set_register(R22, r22_val);
  set_register(R23, r23_val);

  set_dregister(D10, d10_val);
  set_dregister(D11, d11_val);
  set_dregister(D12, d12_val);
  set_dregister(D13, d13_val);
  set_dregister(D14, d14_val);
  set_dregister(D15, d15_val);

  // Restore the SP register and return V1:V0.
  set_register(SP, sp_before_call);
  int64_t return_value;
  if (fp_return) {
    return_value = Utils::LowHighTo64Bits(get_fregister(F0), get_fregister(F1));
  } else {
    return_value = Utils::LowHighTo64Bits(get_register(V0), get_register(V1));
  }
  return return_value;
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

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

#include "vm/compiler/assembler/disassembler.h"
#include "vm/constants.h"
#include "vm/isolate.h"
#include "vm/native_arguments.h"
#include "vm/lockers.h"
#include "vm/os.h"
#include "vm/os_thread.h"
#include "vm/stack_frame.h"
#include "vm/stack_resource.h"

namespace dart {

// This macro provides a platform independent use of sscanf. The reason for
// SScanF not being implemented in a platform independent way through
// OS in the same way as SNPrint is that the Windows C Run-Time
// Library does not provide vsscanf.
#define SScanF sscanf  // NOLINT

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

// The SimulatorDebugger class is used by the simulator while debugging
// simulated MIPS code.
class SimulatorDebugger {
 public:
  explicit SimulatorDebugger(Simulator* sim);
  ~SimulatorDebugger();

  void Stop(Instr* instr, const char* message);
  void Debug();
  char* ReadLine(const char* prompt);

 private:
  Simulator* sim_;

  bool GetValue(char* desc, uint32_t* value);
  bool GetFValue(char* desc, double* value);
  bool GetDValue(char* desc, double* value);

  static TokenPosition GetApproximateTokenIndex(const Code& code, uword pc);

  static void PrintDartFrame(uword pc,
                             uword fp,
                             uword sp,
                             const Function& function,
                             TokenPosition token_pos,
                             bool is_optimized,
                             bool is_inlined);
  void PrintBacktrace();

  // Set or delete a breakpoint. Returns true if successful.
  bool SetBreakpoint(Instr* breakpc);
  bool DeleteBreakpoint(Instr* breakpc);

  // Undo and redo all breakpoints. This is needed to bracket disassembly and
  // execution to skip past breakpoints when run from the debugger.
  void UndoBreakpoints();
  void RedoBreakpoints();
};

SimulatorDebugger::SimulatorDebugger(Simulator* sim) {
  sim_ = sim;
}

SimulatorDebugger::~SimulatorDebugger() {}

void SimulatorDebugger::Stop(Instr* instr, const char* message) {
  OS::Print("Simulator hit %s\n", message);
  Debug();
}

static Register LookupCpuRegisterByName(const char* name) {
  static const char* kNames[] = {
      "r0",  "r1",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",
      "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15",
      "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
      "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31",

      "zr",  "at",  "v0",  "v1",  "a0",  "a1",  "a2",  "a3",
      "t0",  "t1",  "t2",  "t3",  "t4",  "t5",  "t6",  "t7",
      "s0",  "s1",  "s2",  "s3",  "s4",  "s5",  "s6",  "s7",
      "t8",  "t9",  "k0",  "k1",  "gp",  "sp",  "fp",  "ra"};
  static const Register kRegisters[] = {
      R0,  R1,  R2,  R3,  R4,  R5,  R6,  R7,  R8,  R9,  R10,
      R11, R12, R13, R14, R15, R16, R17, R18, R19, R20, R21,
      R22, R23, R24, R25, R26, R27, R28, R29, R30, R31,

      ZR,  AT,  V0,  V1,  A0,  A1,  A2,  A3,  T0,  T1,  T2,
      T3,  T4,  T5,  T6,  T7,  S0,  S1,  S2,  S3,  S4,  S5,
      S6,  S7,  T8,  T9,  K0,  K1,  GP,  SP,  FP,  RA};
  ASSERT(ARRAY_SIZE(kNames) == ARRAY_SIZE(kRegisters));
  for (unsigned i = 0; i < ARRAY_SIZE(kNames); i++) {
    if (strcmp(kNames[i], name) == 0) {
      return kRegisters[i];
    }
  }
  return kNoRegister;
}

static FRegister LookupFRegisterByName(const char* name) {
  int reg_nr = -1;
  bool ok = SScanF(name, "f%d", &reg_nr);
  if (ok && (0 <= reg_nr) && (reg_nr < kNumberOfFRegisters)) {
    return static_cast<FRegister>(reg_nr);
  }
  return kNoFRegister;
}

bool SimulatorDebugger::GetValue(char* desc, uint32_t* value) {
  Register reg = LookupCpuRegisterByName(desc);
  if (reg != kNoRegister) {
    *value = sim_->get_register(reg);
    return true;
  }
  if (desc[0] == '*') {
    uint32_t addr;
    if (GetValue(desc + 1, &addr)) {
      if (Simulator::IsIllegalAddress(addr)) {
        return false;
      }
      *value = *(reinterpret_cast<uint32_t*>(addr));
      return true;
    }
  }
  if (strcmp("pc", desc) == 0) {
    *value = sim_->get_pc();
    return true;
  }
  bool retval = SScanF(desc, "0x%x", value) == 1;
  if (!retval) {
    retval = SScanF(desc, "%x", value) == 1;
  }
  return retval;
}

bool SimulatorDebugger::GetFValue(char* desc, double* value) {
  FRegister freg = LookupFRegisterByName(desc);
  if (freg != kNoFRegister) {
    *value = sim_->get_fregister(freg);
    return true;
  }
  if (desc[0] == '*') {
    uint32_t addr;
    if (GetValue(desc + 1, &addr)) {
      if (Simulator::IsIllegalAddress(addr)) {
        return false;
      }
      *value = *(reinterpret_cast<float*>(addr));
      return true;
    }
  }
  return false;
}

bool SimulatorDebugger::GetDValue(char* desc, double* value) {
  FRegister freg = LookupFRegisterByName(desc);
  if (freg != kNoFRegister) {
    *value = sim_->get_fregister_double(freg);
    return true;
  }
  if (desc[0] == '*') {
    uint32_t addr;
    if (GetValue(desc + 1, &addr)) {
      if (Simulator::IsIllegalAddress(addr)) {
        return false;
      }
      *value = *(reinterpret_cast<double*>(addr));
      return true;
    }
  }
  return false;
}

TokenPosition SimulatorDebugger::GetApproximateTokenIndex(const Code& code,
                                                          uword pc) {
  TokenPosition token_pos = TokenPosition::kNoSource;
  uword pc_offset = pc - code.PayloadStart();
  const PcDescriptors& descriptors =
      PcDescriptors::Handle(code.pc_descriptors());
  PcDescriptors::Iterator iter(descriptors, UntaggedPcDescriptors::kAnyKind);
  while (iter.MoveNext()) {
    if (iter.PcOffset() == pc_offset) {
      return iter.TokenPos();
    } else if (!token_pos.IsReal() && (iter.PcOffset() > pc_offset)) {
      token_pos = iter.TokenPos();
    }
  }
  return token_pos;
}

void SimulatorDebugger::PrintDartFrame(uword pc,
                                       uword fp,
                                       uword sp,
                                       const Function& function,
                                       TokenPosition token_pos,
                                       bool is_optimized,
                                       bool is_inlined) {
  const Script& script = Script::Handle(function.script());
  const String& func_name = String::Handle(function.QualifiedScrubbedName());
  const String& url = String::Handle(script.url());
  intptr_t line = -1;
  intptr_t column = -1;
  if (token_pos.IsReal()) {
    script.GetTokenLocation(token_pos, &line, &column);
  }
  OS::Print(
      "pc=0x%" Px " fp=0x%" Px " sp=0x%" Px " %s%s (%s:%" Pd ":%" Pd ")\n", pc,
      fp, sp, is_optimized ? (is_inlined ? "inlined " : "optimized ") : "",
      func_name.ToCString(), url.ToCString(), line, column);
}

void SimulatorDebugger::PrintBacktrace() {
  StackFrameIterator frames(
      sim_->get_register(FP), sim_->get_register(SP), sim_->get_pc(),
      ValidationPolicy::kDontValidateFrames, Thread::Current(),
      StackFrameIterator::kNoCrossThreadIteration);
  StackFrame* frame = frames.NextFrame();
  ASSERT(frame != NULL);
  Function& function = Function::Handle();
  Function& inlined_function = Function::Handle();
  Code& code = Code::Handle();
  Code& unoptimized_code = Code::Handle();
  while (frame != NULL) {
    if (frame->IsDartFrame()) {
      code = frame->LookupDartCode();
      function = code.function();
      if (code.is_optimized()) {
        // For optimized frames, extract all the inlined functions if any
        // into the stack trace.
        InlinedFunctionsIterator it(code, frame->pc());
        while (!it.Done()) {
          // Print each inlined frame with its pc in the corresponding
          // unoptimized frame.
          inlined_function = it.function();
          unoptimized_code = it.code();
          uword unoptimized_pc = it.pc();
          it.Advance();
          if (!it.Done()) {
            PrintDartFrame(
                unoptimized_pc, frame->fp(), frame->sp(), inlined_function,
                GetApproximateTokenIndex(unoptimized_code, unoptimized_pc),
                true, true);
          }
        }
        // Print the optimized inlining frame below.
      }
      PrintDartFrame(frame->pc(), frame->fp(), frame->sp(), function,
                     GetApproximateTokenIndex(code, frame->pc()),
                     code.is_optimized(), false);
    } else {
      OS::Print("pc=0x%" Px " fp=0x%" Px " sp=0x%" Px " %s frame\n",
                frame->pc(), frame->fp(), frame->sp(),
                frame->IsEntryFrame()
                    ? "entry"
                    : frame->IsExitFrame()
                          ? "exit"
                          : frame->IsStubFrame() ? "stub" : "invalid");
    }
    frame = frames.NextFrame();
  }
}

bool SimulatorDebugger::SetBreakpoint(Instr* breakpc) {
  // Check if a breakpoint can be set. If not return without any side-effects.
  if (sim_->break_pc_ != NULL) {
    return false;
  }

  // Set the breakpoint.
  sim_->break_pc_ = breakpc;
  sim_->break_instr_ = breakpc->InstructionBits();
  // Not setting the breakpoint instruction in the code itself. It will be set
  // when the debugger shell continues.
  return true;
}

bool SimulatorDebugger::DeleteBreakpoint(Instr* breakpc) {
  if (sim_->break_pc_ != NULL) {
    sim_->break_pc_->SetInstructionBits(sim_->break_instr_);
  }

  sim_->break_pc_ = NULL;
  sim_->break_instr_ = 0;
  return true;
}

void SimulatorDebugger::UndoBreakpoints() {
  if (sim_->break_pc_ != NULL) {
    sim_->break_pc_->SetInstructionBits(sim_->break_instr_);
  }
}

void SimulatorDebugger::RedoBreakpoints() {
  if (sim_->break_pc_ != NULL) {
    sim_->break_pc_->SetInstructionBits(Instr::kSimulatorBreakpointInstruction);
  }
}

void SimulatorDebugger::Debug() {
  intptr_t last_pc = -1;
  bool done = false;

#define COMMAND_SIZE 63
#define ARG_SIZE 255

#define STR(a) #a
#define XSTR(a) STR(a)

  char cmd[COMMAND_SIZE + 1];
  char arg1[ARG_SIZE + 1];
  char arg2[ARG_SIZE + 1];

  // make sure to have a proper terminating character if reaching the limit
  cmd[COMMAND_SIZE] = 0;
  arg1[ARG_SIZE] = 0;
  arg2[ARG_SIZE] = 0;

  // Undo all set breakpoints while running in the debugger shell. This will
  // make them invisible to all commands.
  UndoBreakpoints();

  while (!done) {
    if (last_pc != sim_->get_pc()) {
      last_pc = sim_->get_pc();
      if (Simulator::IsIllegalAddress(last_pc)) {
        OS::Print("pc is out of bounds: 0x%" Px "\n", last_pc);
      } else {
        if (FLAG_support_disassembler) {
          Disassembler::Disassemble(last_pc, last_pc + Instr::kInstrSize);
        } else {
          OS::Print("Disassembler not supported in this mode.\n");
        }
      }
    }
    char* line = ReadLine("sim> ");
    if (line == NULL) {
      FATAL("ReadLine failed");
    } else {
      // Use sscanf to parse the individual parts of the command line. At the
      // moment no command expects more than two parameters.
      int args = SScanF(line,
                        "%" XSTR(COMMAND_SIZE) "s "
                        "%" XSTR(ARG_SIZE) "s "
                        "%" XSTR(ARG_SIZE) "s",
                        cmd, arg1, arg2);
      if ((strcmp(cmd, "h") == 0) || (strcmp(cmd, "help") == 0)) {
        OS::Print(
            "c/cont -- continue execution\n"
            "disasm -- disassemble instrs at current pc location\n"
            "  other variants are:\n"
            "    disasm <address>\n"
            "    disasm <address> <number_of_instructions>\n"
            "  by default 10 instrs are disassembled\n"
            "del -- delete breakpoints\n"
            "gdb -- transfer control to gdb\n"
            "h/help -- print this help string\n"
            "break <address> -- set break point at specified address\n"
            "p/print <reg or icount or value or *addr> -- print integer\n"
            "pf/printfloat <freg or *addr> -- print float value\n"
            "po/printobject <*reg or *addr> -- print object\n"
            "si/stepi -- single step an instruction\n"
            "trace -- toggle execution tracing mode\n"
            "bt -- print backtrace\n"
            "unstop -- if current pc is a stop instr make it a nop\n"
            "q/quit -- Quit the debugger and exit the program\n");
      } else if ((strcmp(cmd, "quit") == 0) || (strcmp(cmd, "q") == 0)) {
        OS::Print("Quitting\n");
        OS::Exit(0);
      } else if ((strcmp(cmd, "si") == 0) || (strcmp(cmd, "stepi") == 0)) {
        sim_->InstructionDecode(reinterpret_cast<Instr*>(sim_->get_pc()));
      } else if ((strcmp(cmd, "c") == 0) || (strcmp(cmd, "cont") == 0)) {
        // Execute the one instruction we broke at with breakpoints disabled.
        sim_->InstructionDecode(reinterpret_cast<Instr*>(sim_->get_pc()));
        // Leave the debugger shell.
        done = true;
      } else if ((strcmp(cmd, "p") == 0) || (strcmp(cmd, "print") == 0)) {
        if (args == 2) {
          uint32_t value;
          if (strcmp(arg1, "icount") == 0) {
            const uint64_t icount = sim_->get_icount();
            OS::Print("icount: %" Pu64 " 0x%" Px64 "\n", icount, icount);
          } else if (GetValue(arg1, &value)) {
            OS::Print("%s: %u 0x%x\n", arg1, value, value);
          } else {
            OS::Print("%s unrecognized\n", arg1);
          }
        } else {
          OS::Print("print <reg or icount or value or *addr>\n");
        }
      } else if ((strcmp(cmd, "pf") == 0) || (strcmp(cmd, "printfloat") == 0)) {
        if (args == 2) {
          double dvalue;
          if (GetFValue(arg1, &dvalue)) {
            uint64_t long_value = bit_cast<uint64_t, double>(dvalue);
            OS::Print("%s: %llu 0x%llx %.8g\n", arg1, long_value, long_value,
                      dvalue);
          } else {
            OS::Print("%s unrecognized\n", arg1);
          }
        } else {
          OS::Print("printfloat <dreg or *addr>\n");
        }
      } else if ((strcmp(cmd, "pd") == 0) ||
                 (strcmp(cmd, "printdouble") == 0)) {
        if (args == 2) {
          double dvalue;
          if (GetDValue(arg1, &dvalue)) {
            uint64_t long_value = bit_cast<uint64_t, double>(dvalue);
            OS::Print("%s: %llu 0x%llx %.8g\n", arg1, long_value, long_value,
                      dvalue);
          } else {
            OS::Print("%s unrecognized\n", arg1);
          }
        } else {
          OS::Print("printfloat <dreg or *addr>\n");
        }
      } else if ((strcmp(cmd, "po") == 0) ||
                 (strcmp(cmd, "printobject") == 0)) {
        if (args == 2) {
          uint32_t value;
          // Make the dereferencing '*' optional.
          if (((arg1[0] == '*') && GetValue(arg1 + 1, &value)) ||
              GetValue(arg1, &value)) {
            if (IsolateGroup::Current()->heap()->Contains(value)) {
              OS::Print("%s: \n", arg1);
#if defined(DEBUG)
              const Object& obj = Object::Handle(static_cast<ObjectPtr>(value));
              obj.Print();
#endif  // defined(DEBUG)
            } else {
              OS::Print("0x%x is not an object reference\n", value);
            }
          } else {
            OS::Print("%s unrecognized\n", arg1);
          }
        } else {
          OS::Print("printobject <*reg or *addr>\n");
        }
      } else if (strcmp(cmd, "disasm") == 0) {
        uint32_t start = 0;
        uint32_t end = 0;
        if (args == 1) {
          start = sim_->get_pc();
          end = start + (10 * Instr::kInstrSize);
        } else if (args == 2) {
          if (GetValue(arg1, &start)) {
            // No length parameter passed, assume 10 instructions.
            if (Simulator::IsIllegalAddress(start)) {
              // If start isn't a valid address, warn and use PC instead.
              OS::Print("First argument yields invalid address: 0x%x\n", start);
              OS::Print("Using PC instead\n");
              start = sim_->get_pc();
            }
            end = start + (10 * Instr::kInstrSize);
          }
        } else {
          uint32_t length;
          if (GetValue(arg1, &start) && GetValue(arg2, &length)) {
            if (Simulator::IsIllegalAddress(start)) {
              // If start isn't a valid address, warn and use PC instead.
              OS::Print("First argument yields invalid address: 0x%x\n", start);
              OS::Print("Using PC instead\n");
              start = sim_->get_pc();
            }
            end = start + (length * Instr::kInstrSize);
          }
        }
        if ((start > 0) && (end > start)) {
          if (FLAG_support_disassembler) {
            Disassembler::Disassemble(start, end);
          } else {
            OS::Print("Disassembler not supported in this mode.\n");
          }
        } else {
          OS::Print("disasm [<address> [<number_of_instructions>]]\n");
        }
      } else if (strcmp(cmd, "gdb") == 0) {
        OS::Print("relinquishing control to gdb\n");
        OS::DebugBreak();
        OS::Print("regaining control from gdb\n");
      } else if (strcmp(cmd, "break") == 0) {
        if (args == 2) {
          uint32_t addr;
          if (GetValue(arg1, &addr)) {
            if (!SetBreakpoint(reinterpret_cast<Instr*>(addr))) {
              OS::Print("setting breakpoint failed\n");
            }
          } else {
            OS::Print("%s unrecognized\n", arg1);
          }
        } else {
          OS::Print("break <addr>\n");
        }
      } else if (strcmp(cmd, "del") == 0) {
        if (!DeleteBreakpoint(NULL)) {
          OS::Print("deleting breakpoint failed\n");
        }
      } else if (strcmp(cmd, "unstop") == 0) {
        intptr_t stop_pc = sim_->get_pc() - Instr::kInstrSize;
        Instr* stop_instr = reinterpret_cast<Instr*>(stop_pc);
        if (stop_instr->IsBreakPoint()) {
          stop_instr->SetInstructionBits(Instr::kNopInstruction);
        } else {
          OS::Print("Not at debugger stop.\n");
        }
      } else if (strcmp(cmd, "trace") == 0) {
        if (FLAG_trace_sim_after == ULLONG_MAX) {
          FLAG_trace_sim_after = sim_->get_icount();
          OS::Print("execution tracing on\n");
        } else {
          FLAG_trace_sim_after = ULLONG_MAX;
          OS::Print("execution tracing off\n");
        }
      } else if (strcmp(cmd, "bt") == 0) {
        Thread* thread = reinterpret_cast<Thread*>(sim_->get_register(S3));
        thread->set_execution_state(Thread::kThreadInVM);
        PrintBacktrace();
        thread->set_execution_state(Thread::kThreadInGenerated);
      } else {
        OS::Print("Unknown command: %s\n", cmd);
      }
    }
    delete[] line;
  }

  // Add all the breakpoints back to stop execution and enter the debugger
  // shell when hit.
  RedoBreakpoints();

#undef COMMAND_SIZE
#undef ARG_SIZE

#undef STR
#undef XSTR
}

char* SimulatorDebugger::ReadLine(const char* prompt) {
  char* result = NULL;
  char line_buf[256];
  intptr_t offset = 0;
  bool keep_going = true;
  OS::Print("%s", prompt);
  while (keep_going) {
    if (fgets(line_buf, sizeof(line_buf), stdin) == NULL) {
      // fgets got an error. Just give up.
      if (result != NULL) {
        delete[] result;
      }
      return NULL;
    }
    intptr_t len = strlen(line_buf);
    if (len > 1 && line_buf[len - 2] == '\\' && line_buf[len - 1] == '\n') {
      // When we read a line that ends with a "\" we remove the escape and
      // append the remainder.
      line_buf[len - 2] = '\n';
      line_buf[len - 1] = 0;
      len -= 1;
    } else if ((len > 0) && (line_buf[len - 1] == '\n')) {
      // Since we read a new line we are done reading the line. This
      // will exit the loop after copying this buffer into the result.
      keep_going = false;
    }
    if (result == NULL) {
      // Allocate the initial result and make room for the terminating '\0'
      result = new char[len + 1];
      if (result == NULL) {
        // OOM, so cannot readline anymore.
        return NULL;
      }
    } else {
      // Allocate a new result with enough room for the new addition.
      intptr_t new_len = offset + len + 1;
      char* new_result = new char[new_len];
      if (new_result == NULL) {
        // OOM, free the buffer allocated so far and return NULL.
        delete[] result;
        return NULL;
      } else {
        // Copy the existing input into the new array and set the new
        // array as the result.
        memmove(new_result, result, offset);
        delete[] result;
        result = new_result;
      }
    }
    // Copy the newly read line into the result.
    memmove(result + offset, line_buf, len);
    offset += len;
  }
  ASSERT(result != NULL);
  result[offset] = '\0';
  return result;
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
  SimulatorDebugger dbg(this);
  dbg.Stop(instr, buffer);
  FATAL("Cannot continue execution after unimplemented instruction.");
}

void Simulator::HandleIllegalAccess(uword addr, Instr* instr) {
  uword fault_pc = get_pc();
  // The debugger will not be able to single step past this instruction, but
  // it will be possible to disassemble the code and inspect registers.
  char buffer[128];
  snprintf(buffer, sizeof(buffer),
           "illegal memory access at 0x%" Px ", pc=0x%" Px "\n", addr,
           fault_pc);
  SimulatorDebugger dbg(this);
  dbg.Stop(instr, buffer);
  // The debugger will return control in non-interactive mode.
  FATAL("Cannot continue execution after illegal memory access.");
}

void Simulator::UnalignedAccess(const char* msg, uword addr, Instr* instr) {
  // The debugger will not be able to single step past this instruction, but
  // it will be possible to disassemble the code and inspect registers.
  char buffer[128];
  snprintf(buffer, sizeof(buffer), "pc=%p, unaligned %s at 0x%" Px "\n", instr,
           msg, addr);
  SimulatorDebugger dbg(this);
  dbg.Stop(instr, buffer);
  // The debugger will return control in non-interactive mode.
  FATAL("Cannot continue execution after unaligned access.");
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

// Calls to the Dart runtime are based on this interface.
typedef void (*SimulatorRuntimeCall)(NativeArguments arguments);

// Calls to leaf Dart runtime functions are based on this interface.
typedef int32_t (*SimulatorLeafRuntimeCall)(int32_t r0,
                                            int32_t r1,
                                            int32_t r2,
                                            int32_t r3);

// Calls to leaf float Dart runtime functions are based on this interface.
typedef double (*SimulatorLeafFloatRuntimeCall)(double d0, double d1);

// Calls to native Dart functions are based on this interface.
typedef void (*SimulatorNativeCallWrapper)(Dart_NativeArguments arguments,
                                           Dart_NativeFunction target);

void Simulator::DoBreak(Instr* instr) {
  ASSERT(instr->OpcodeField() == SPECIAL);
  ASSERT(instr->FunctionField() == BREAK);
  if (instr->BreakCodeField() == Instr::kStopMessageCode) {
    SimulatorDebugger dbg(this);
    const char* message = *reinterpret_cast<const char**>(
        reinterpret_cast<intptr_t>(instr) - Instr::kInstrSize);
    set_pc(get_pc() + Instr::kInstrSize);
    dbg.Stop(instr, message);
    // Adjust for extra pc increment.
    set_pc(get_pc() - Instr::kInstrSize);
  } else if (instr->BreakCodeField() == Instr::kSimulatorRedirectCode) {
    SimulatorSetjmpBuffer buffer(this);

    if (!setjmp(buffer.buffer_)) {
      int32_t saved_ra = get_register(RA);
      Redirection* redirection = Redirection::FromSvcInstruction(instr);
      uword external = redirection->external_function();
      if (IsTracingExecution()) {
        THR_Print("Call to host function at 0x%" Pd "\n", external);
      }

      if ((redirection->call_kind() == kRuntimeCall) ||
          (redirection->call_kind() == kNativeCallWrapper)) {
        // Set the top_exit_frame_info of this simulator to the native stack.
        set_top_exit_frame_info(OSThread::GetCurrentStackPointer());
      }
      if (redirection->call_kind() == kRuntimeCall) {
        NativeArguments arguments;
        ASSERT(sizeof(NativeArguments) == 4 * kWordSize);
        arguments.thread_ = reinterpret_cast<Thread*>(get_register(A0));
        arguments.argc_tag_ = get_register(A1);
        arguments.argv_ = reinterpret_cast<ObjectPtr*>(get_register(A2));
        arguments.retval_ = reinterpret_cast<ObjectPtr*>(get_register(A3));
        SimulatorRuntimeCall target =
            reinterpret_cast<SimulatorRuntimeCall>(external);
        target(arguments);
        set_register(V0, icount_);  // Zap result registers from void function.
        set_register(V1, icount_);
      } else if (redirection->call_kind() == kLeafRuntimeCall) {
        int32_t a0 = get_register(A0);
        int32_t a1 = get_register(A1);
        int32_t a2 = get_register(A2);
        int32_t a3 = get_register(A3);
        SimulatorLeafRuntimeCall target =
            reinterpret_cast<SimulatorLeafRuntimeCall>(external);
        a0 = target(a0, a1, a2, a3);
        set_register(V0, a0);       // Set returned result from function.
        set_register(V1, icount_);  // Zap second result register.
      } else if (redirection->call_kind() == kLeafFloatRuntimeCall) {
        ASSERT((0 <= redirection->argument_count()) &&
               (redirection->argument_count() <= 2));
        // double values are passed and returned in floating point registers.
        SimulatorLeafFloatRuntimeCall target =
            reinterpret_cast<SimulatorLeafFloatRuntimeCall>(external);
        double d0 = 0.0;
        double d6 = get_fregister_double(F12);
        double d7 = get_fregister_double(F14);
        d0 = target(d6, d7);
        set_fregister_double(F0, d0);
      } else {
        ASSERT(redirection->call_kind() == kNativeCallWrapper);
        Dart_NativeArguments arguments =
            reinterpret_cast<Dart_NativeArguments>(get_register(A0));
        Dart_NativeFunction target =
            reinterpret_cast<Dart_NativeFunction>(get_register(A1));
        SimulatorNativeCallWrapper wrapper =
            reinterpret_cast<SimulatorNativeCallWrapper>(external);
        wrapper(arguments, target);
        set_register(V0, icount_);  // Zap result register from void function.
        set_register(V1, icount_);
      }
      set_top_exit_frame_info(0);

      // Zap caller-saved registers, since the actual runtime call could have
      // used them.
      set_register(T0, icount_);
      set_register(T1, icount_);
      set_register(T2, icount_);
      set_register(T3, icount_);
      set_register(T4, icount_);
      set_register(T5, icount_);
      set_register(T6, icount_);
      set_register(T7, icount_);
      set_register(T8, icount_);
      set_register(T9, icount_);

      set_register(A0, icount_);
      set_register(A1, icount_);
      set_register(A2, icount_);
      set_register(A3, icount_);
      set_register(TMP, icount_);
      set_register(RA, icount_);

      // Zap floating point registers.
      int32_t zap_dvalue = icount_;
      for (int i = F4; i <= F18; i++) {
        set_fregister(static_cast<FRegister>(i), zap_dvalue);
      }

      // Return. Subtract to account for pc_ increment after return.
      set_pc(saved_ra - Instr::kInstrSize);
    } else {
      // Coming via long jump from a throw. Continue to exception handler.
      set_top_exit_frame_info(0);
      // Adjust for extra pc increment.
      set_pc(get_pc() - Instr::kInstrSize);
    }
  } else if (instr->BreakCodeField() == Instr::kSimulatorBreakCode) {
    SimulatorDebugger dbg(this);
    dbg.Stop(instr, "breakpoint");
    // Adjust for extra pc increment.
    set_pc(get_pc() - Instr::kInstrSize);
  } else {
    SimulatorDebugger dbg(this);
    set_pc(get_pc() + Instr::kInstrSize);
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "break #0x%x", instr->BreakCodeField());
    dbg.Stop(instr, buffer);
    // Adjust for extra pc increment.
    set_pc(get_pc() - Instr::kInstrSize);
  }
}

void Simulator::DecodeSpecial2(Instr* instr) {
  ASSERT(instr->OpcodeField() == SPECIAL2);
  switch (instr->FunctionField()) {
    case MADD: {
      ASSERT(instr->RdField() == 0);
      ASSERT(instr->SaField() == 0);
      // Format(instr, "madd 'rs, 'rt");
      uint32_t lo = get_lo_register();
      int32_t hi = get_hi_register();
      int64_t accum = Utils::LowHighTo64Bits(lo, hi);
      int64_t rs = get_register(instr->RsField());
      int64_t rt = get_register(instr->RtField());
      int64_t res = accum + rs * rt;
      set_hi_register(Utils::High32Bits(res));
      set_lo_register(Utils::Low32Bits(res));
      break;
    }
    case MADDU: {
      ASSERT(instr->RdField() == 0);
      ASSERT(instr->SaField() == 0);
      // Format(instr, "maddu 'rs, 'rt");
      uint32_t lo = get_lo_register();
      uint32_t hi = get_hi_register();
      uint64_t accum = Utils::LowHighTo64Bits(lo, hi);
      uint64_t rs = static_cast<uint32_t>(get_register(instr->RsField()));
      uint64_t rt = static_cast<uint32_t>(get_register(instr->RtField()));
      uint64_t res = accum + rs * rt;
      set_hi_register(Utils::High32Bits(res));
      set_lo_register(Utils::Low32Bits(res));
      break;
    }
    case CLO: {
      ASSERT(instr->SaField() == 0);
      ASSERT(instr->RtField() == instr->RdField());
      // Format(instr, "clo 'rd, 'rs");
      int32_t rs_val = get_register(instr->RsField());
      int32_t bitcount = 0;
      while (rs_val < 0) {
        bitcount++;
        rs_val <<= 1;
      }
      set_register(instr->RdField(), bitcount);
      break;
    }
    case CLZ: {
      ASSERT(instr->SaField() == 0);
      ASSERT(instr->RtField() == instr->RdField());
      // Format(instr, "clz 'rd, 'rs");
      int32_t rs_val = get_register(instr->RsField());
      int32_t bitcount = 0;
      if (rs_val != 0) {
        while (rs_val > 0) {
          bitcount++;
          rs_val <<= 1;
        }
      } else {
        bitcount = 32;
      }
      set_register(instr->RdField(), bitcount);
      break;
    }
    default: {
      OS::PrintErr("DecodeSpecial2: 0x%x\n", instr->InstructionBits());
      UnimplementedInstruction(instr);
      break;
    }
  }
}

void Simulator::DoBranch(Instr* instr, bool taken, bool likely) {
  ASSERT(!delay_slot_);
  int32_t imm_val = instr->SImmField() << 2;

  uword next_pc;
  if (taken) {
    // imm_val is added to the address of the instruction following the branch.
    next_pc = pc_ + imm_val + Instr::kInstrSize;
    if (likely) {
      ExecuteDelaySlot();
    }
  } else {
    next_pc = pc_ + (2 * Instr::kInstrSize);  // Next after delay slot.
  }
  if (!likely) {
    ExecuteDelaySlot();
  }
  pc_ = next_pc - Instr::kInstrSize;

  return;
}

void Simulator::DecodeRegImm(Instr* instr) {
  ASSERT(instr->OpcodeField() == REGIMM);
  switch (instr->RegImmFnField()) {
    case BGEZ: {
      // Format(instr, "bgez 'rs, 'dest");
      int32_t rs_val = get_register(instr->RsField());
      DoBranch(instr, rs_val >= 0, false);
      break;
    }
    case BGEZAL: {
      int32_t rs_val = get_register(instr->RsField());
      // Return address is one after the delay slot.
      set_register(RA, pc_ + (2 * Instr::kInstrSize));
      DoBranch(instr, rs_val >= 0, false);
      break;
    }
    case BLTZAL: {
      int32_t rs_val = get_register(instr->RsField());
      // Return address is one after the delay slot.
      set_register(RA, pc_ + (2 * Instr::kInstrSize));
      DoBranch(instr, rs_val < 0, false);
      break;
    }
    case BGEZL: {
      // Format(instr, "bgezl 'rs, 'dest");
      int32_t rs_val = get_register(instr->RsField());
      DoBranch(instr, rs_val >= 0, true);
      break;
    }
    case BLTZ: {
      // Format(instr, "bltz 'rs, 'dest");
      int32_t rs_val = get_register(instr->RsField());
      DoBranch(instr, rs_val < 0, false);
      break;
    }
    case BLTZL: {
      // Format(instr, "bltzl 'rs, 'dest");
      int32_t rs_val = get_register(instr->RsField());
      DoBranch(instr, rs_val < 0, true);
      break;
    }
    default: {
      OS::PrintErr("DecodeRegImm: 0x%x\n", instr->InstructionBits());
      UnimplementedInstruction(instr);
      break;
    }
  }
}

void Simulator::ExecuteDelaySlot() {
  UNIMPLEMENTED();
}

void Simulator::InstructionDecode(Instr* instr) {
  UNIMPLEMENTED();
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

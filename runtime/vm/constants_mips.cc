#if defined(TARGET_ARCH_MIPS)

#include "vm/constants.h"  // NOLINT

namespace dart {

const char* const cpu_reg_names[kNumberOfCpuRegisters] = {
    "zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
    "t0",  "t1",  "t2", "t3", "t4", "t5", "t6", "t7",
    "s0",  "s1",  "s2", "s3", "s4", "s5", "s6", "s7",
    "t8",  "t9",  "k0", "k1", "gp", "sp", "fp", "ra"};

const char* const cpu_reg_abi_names[kNumberOfCpuRegisters] = {
    "zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
    "t0",  "t1",  "t2", "t3", "t4", "t5", "t6", "t7",
    "s0",  "s1",  "s2", "s3", "s4", "s5", "s6", "s7",
    "t8",  "t9",  "k0", "k1", "gp", "sp", "fp", "ra"};

const char* const fpu_f_reg_names[kNumberOfFRegisters] = {
    "f0",  "f1",  "f2",  "f3",  "f4",  "f5",  "f6",  "f7",
    "f8",  "f9",  "f10", "f11", "f12", "f13", "f14", "f15",
    "f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23",
    "f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31"};

const char* const fpu_reg_names[kNumberOfDRegisters] = {
    "d0", "d1", "d2",  "d3",  "d4",  "d5",  "d6",  "d7",
    "d8", "d9", "d10", "d11", "d12", "d13", "d14", "d15"};

}  // namespace dart

#endif  // defined(TARGET_ARCH_MIPS)

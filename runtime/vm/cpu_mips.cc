// Copyright (c) 2025, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"
#if defined(TARGET_ARCH_MIPS)

#include "vm/cpu.h"
#include "vm/cpu_mips.h"

#include "vm/cpuinfo.h"

#if !defined(DART_INCLUDE_SIMULATOR)
#include <asm/cachectl.h> /* NOLINT */
#include <sys/syscall.h>  /* NOLINT */
#include <unistd.h>       /* NOLINT */
#endif

namespace dart {

void CPU::FlushICache(uword start, uword size) {
#if defined(DART_PRECOMPILED_RUNTIME)
  UNREACHABLE();
#elif !defined(DART_INCLUDE_SIMULATOR)
  // Nothing to do. Flushing no instructions.
  if (size == 0) {
    return;
  }

#if defined(DART_HOST_OS_LINUX)
  syscall(__NR_cacheflush, start, size, ICACHE);
#else
#error FlushICache only tested/supported on Linux
#endif

#endif
}

const char* CPU::Id() {
  return
#if defined(DART_INCLUDE_SIMULATOR)
      "sim"
#endif  // !defined(DART_INCLUDE_SIMULATOR)
      "mips";
}

const char* HostCPUFeatures::hardware_ = nullptr;
MIPSVersion HostCPUFeatures::mips_version_ = MIPSvUnknown;
#if defined(DEBUG)
bool HostCPUFeatures::initialized_ = false;
#endif

#if !defined(DART_INCLUDE_SIMULATOR)
void HostCPUFeatures::Init() {
  CpuInfo::Init();
  hardware_ = CpuInfo::GetCpuModel();

// We want to know the ISA version, but on MIPS, CpuInfo can't tell us, so
// we use the same ISA version that Dart's C++ compiler targeted.
#if defined(_MIPS_ARCH_MIPS32R2)
  mips_version_ = MIPS32r2;
#elif defined(_MIPS_ARCH_MIPS32)
  mips_version_ = MIPS32;
#endif

#if defined(DEBUG)
  initialized_ = true;
#endif
}

void HostCPUFeatures::Cleanup() {
  DEBUG_ASSERT(initialized_);
#if defined(DEBUG)
  initialized_ = false;
#endif
  ASSERT(hardware_ != nullptr);
  free(const_cast<char*>(hardware_));
  hardware_ = nullptr;
  CpuInfo::Cleanup();
}

#else // !defined(DART_INCLUDE_SIMULATOR)

void HostCPUFeatures::Init() {
  CpuInfo::Init();
  hardware_ = CpuInfo::GetCpuModel();
  mips_version_ = MIPS32r2;
#if defined(DEBUG)
  initialized_ = true;
#endif
}

void HostCPUFeatures::Cleanup() {
  DEBUG_ASSERT(initialized_);
#if defined(DEBUG)
  initialized_ = false;
#endif
  ASSERT(hardware_ != nullptr);
  free(const_cast<char*>(hardware_));
  hardware_ = nullptr;
  CpuInfo::Cleanup();
}
#endif // !defined(DART_INCLUDE_SIMULATOR)

}  // namespace dart

#endif  // defined TARGET_ARCH_MIPS

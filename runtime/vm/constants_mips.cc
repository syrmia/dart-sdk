#include "platform/globals.h"

#if defined(TARGET_ARCH_MIPS)

#include "vm/constants.h"

namespace dart {

const Register CallingConventions::ArgumentRegisters[] = {};

const FpuRegister CallingConventions::FpuArgumentRegisters[] = {};

}  // namespace dart

#endif  // defined(TARGET_ARCH_MIPS)

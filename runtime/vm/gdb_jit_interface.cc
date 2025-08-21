// Copyright (c) 2025, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/gdb_jit_interface.h"

#include <iostream>

// GDB JIT interface is only available in non-product, non-precompiled builds
// on host Linux and macOS platforms with supported target architectures

#include "vm/flags.h"
#include "vm/thread.h"
#include "vm/object.h"

#define DEBUG_GDB_JIT_INTERFACE 1

namespace dart {

// Define the flag for GDB JIT interface
DECLARE_FLAG(bool, gdb_jit_interface);

// Define the global symbols for GDB to find
extern "C" {
// Make the symbol visible to the debugger
__attribute__((used))
struct JITDescriptor {
  uint32_t version;
  uint32_t action_flag;
  struct JITCodeEntry* relevant_entry;
  struct JITCodeEntry* first_entry;
} __jit_debug_descriptor = {1, 0, nullptr, nullptr};

// Function that GDB sets a breakpoint on
__attribute__((used))
void __jit_debug_register_code() {
  // This asm statement prevents compiler from optimizing out this function
  __asm__ volatile("" ::: "memory");
}
} // extern "C"

// Initialize static members
bool GDBJITInterface::initialized_ = false;
JITCodeEntry* GDBJITInterface::first_entry_ = nullptr;

void GDBJITInterface::Init() {
  if (!FLAG_gdb_jit_interface || initialized_) {
    return;
  }

  // Initialize descriptor
  __jit_debug_descriptor.version = 1;
  __jit_debug_descriptor.action_flag = 0; // JIT_NOACTION
  __jit_debug_descriptor.relevant_entry = nullptr;
  __jit_debug_descriptor.first_entry = nullptr;
  first_entry_ = nullptr;

  initialized_ = true;
}

void GDBJITInterface::Cleanup() {
  if (!initialized_) {
    return;
  }

  // Free all registered code entries
  JITCodeEntry* entry = __jit_debug_descriptor.first_entry;
  while (entry != nullptr) {
    JITCodeEntry* next = entry->next_entry;
    free(const_cast<void*>(entry->symfile_addr));
    free(entry);
    entry = next;
  }

  __jit_debug_descriptor.first_entry = nullptr;
  __jit_debug_descriptor.relevant_entry = nullptr;
  first_entry_ = nullptr;

  initialized_ = false;
}

void GDBJITInterface::RegisterCode(const Code& code) {
  if (!IsEnabled() || code.IsNull()) {
    return;
  }

#if DEBUG_GDB_JIT_INTERFACE
  // Extract function name and address for debug output
  const Function& function = Function::Handle(code.function());
  const char* function_name = "<anonymous>";
  if (!function.IsNull()) {
    function_name = function.ToQualifiedCString();
  }

  std::cout << "*** GDBJITInterface::RegisterCode: " << function_name
            << " at 0x" << std::hex << code.PayloadStart()
            << std::dec << "\n";
#endif

  // Create debug info for this code object
  intptr_t size = 0;
  char* debug_info = CreateDebugInfo(code, &size);
  if (debug_info == nullptr) {
    return;
  }

  // Add entry to the linked list
  JITCodeEntry* entry = AddCodeEntry(debug_info, size);
  if (entry == nullptr) {
    free(debug_info);
    return;
  }

  // Notify the debugger
  __jit_debug_descriptor.relevant_entry = entry;
  __jit_debug_descriptor.action_flag = 1; // JIT_REGISTER_FN
  __jit_debug_register_code();
  __jit_debug_descriptor.action_flag = 0; // JIT_NOACTION
}

void GDBJITInterface::UnregisterCode(const Code& code) {
  if (!IsEnabled() || code.IsNull()) {
    return;
  }

  // Find the entry for this code by matching the payload address
  const void* code_addr = reinterpret_cast<const void*>(code.PayloadStart());
  JITCodeEntry* entry = __jit_debug_descriptor.first_entry;

  while (entry != nullptr) {
    if (entry->symfile_addr == code_addr) {
      break;
    }
    entry = entry->next_entry;
  }

  if (entry == nullptr) {
    return;
  }

  // Remove from linked list
  if (entry->prev_entry != nullptr) {
    entry->prev_entry->next_entry = entry->next_entry;
  } else {
    __jit_debug_descriptor.first_entry = entry->next_entry;
  }

  if (entry->next_entry != nullptr) {
    entry->next_entry->prev_entry = entry->prev_entry;
  }

  // Notify the debugger
  __jit_debug_descriptor.relevant_entry = entry;
  __jit_debug_descriptor.action_flag = 2; // JIT_UNREGISTER_FN
  __jit_debug_register_code();
  __jit_debug_descriptor.action_flag = 0; // JIT_NOACTION

  // Free resources
  free(const_cast<void*>(entry->symfile_addr));
  free(entry);
}

JITCodeEntry* GDBJITInterface::AddCodeEntry(const void* addr, uint64_t size) {
  JITCodeEntry* entry = static_cast<JITCodeEntry*>(malloc(sizeof(JITCodeEntry)));
  if (entry == nullptr) {
    return nullptr;
  }

  entry->symfile_addr = addr;
  entry->symfile_size = size;

  // Add to beginning of linked list
  entry->prev_entry = nullptr;
  entry->next_entry = __jit_debug_descriptor.first_entry;

  if (__jit_debug_descriptor.first_entry != nullptr) {
    __jit_debug_descriptor.first_entry->prev_entry = entry;
  }

  __jit_debug_descriptor.first_entry = entry;
  first_entry_ = entry;  // Keep our internal pointer in sync

  return entry;
}

char* GDBJITInterface::CreateDebugInfo(const Code& code, intptr_t* size) {
  // Create simple YAML-format debug info
  const Function& function = Function::Handle(code.function());
  const char* function_name = "unknown";
  const char* script_url = "unknown";

  if (!function.IsNull()) {
    function_name = function.ToQualifiedCString();
    const Script& script = Script::Handle(function.script());
    if (!script.IsNull()) {
      // Get C string from Dart String
      const String& url = String::Handle(script.url());
      if (!url.IsNull()) {
        script_url = url.ToCString();
      }
    }
  }

  // Format:
  // ---
  // name: function_name
  // start: 0x12345678
  // size: 1234
  // file: script_url
  // ---

  char addr_str[32];
  snprintf(addr_str, sizeof(addr_str), "%p",
           reinterpret_cast<void *>(code.PayloadStart()));

  intptr_t buffer_size =
      100 + strlen(function_name) + strlen(addr_str) + strlen(script_url);
  char *buffer = static_cast<char *>(malloc(buffer_size));

  if (buffer == nullptr) {
    *size = 0;
    return nullptr;
  }

  int written = snprintf(buffer, buffer_size,
                         "---\n"
                         "name: %s\n"
                         "start: %s\n"
                         "size: %" Pd "\n"
                         "file: %s\n"
                         "---\n",
                         function_name,
                         addr_str,
                         code.Size(),
                         script_url);

  if (written < 0 || written >= buffer_size) {
    free(buffer);
    *size = 0;
    return nullptr;
  }

  *size = written;
  return buffer;
}

bool GDBJITInterface::IsEnabled() {
  return initialized_ && FLAG_gdb_jit_interface;
}

} // namespace dart

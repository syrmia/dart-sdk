// Copyright (c) 2025, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef RUNTIME_VM_GDB_JIT_INTERFACE_H_
#define RUNTIME_VM_GDB_JIT_INTERFACE_H_

#include "vm/globals.h"

// GDB JIT interface is only available in non-product, non-precompiled builds
// on host Linux and macOS platforms with supported target architectures

#include "vm/object.h"
#include "vm/growable_array.h"

namespace dart {

// Forward declaration
class Code;

// GDB JIT interface declarations
struct JITCodeEntry {
  struct JITCodeEntry* next_entry;
  struct JITCodeEntry* prev_entry;
  const void* symfile_addr;
  uint64_t symfile_size;
};

// Class to manage GDB JIT interface
class GDBJITInterface {
 public:
  static void Init();
  static void Cleanup();

  // Register/unregister code with the debugger
  static void RegisterCode(const Code& code);
  static void UnregisterCode(const Code& code);

  static bool IsEnabled();

 private:
  // Create a simple debug info structure with function name, address, etc.
  static char* CreateDebugInfo(const Code& code, intptr_t* size);

  // Add/remove entries from the linked list
  static JITCodeEntry* AddCodeEntry(const void* addr, uint64_t size);
  static void RemoveCodeEntry(JITCodeEntry* entry);

  // Instead of using a dynamic array, use a linked list which is more compatible
  // with ValueObject allocation rules in Dart VM
  static JITCodeEntry* first_entry_;
  static bool initialized_;
};

} // namespace dart

#else // GDB JIT Interface is not available

namespace dart {

// Forward declaration
class Code;

// Provide empty stubs when feature is not available
class GDBJITInterface {
 public:
  static void Init() {}
  static void Cleanup() {}
  static void RegisterCode(const Code& code) {}
  static void UnregisterCode(const Code& code) {}
  static bool IsEnabled() { return false; }
};

} // namespace dart

#endif // RUNTIME_VM_GDB_JIT_INTERFACE_H_

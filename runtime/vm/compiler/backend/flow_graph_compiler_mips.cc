// Copyright (c) 2026, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"  // Needed here to get TARGET_ARCH_MIPS.
#if defined(TARGET_ARCH_MIPS)

#include "vm/compiler/backend/flow_graph_compiler.h"

#include "vm/compiler/backend/il_printer.h"

namespace dart {

FlowGraphCompiler::~FlowGraphCompiler() {
  // BlockInfos are zone-allocated, so their destructors are not called.
  // Verify the labels explicitly here.
  for (int i = 0; i < block_info_.length(); ++i) {
    ASSERT(!block_info_[i]->jump_label()->IsLinked());
  }
}

bool FlowGraphCompiler::SupportsUnboxedSimd128() {
  return false;
}

bool FlowGraphCompiler::CanConvertInt64ToDouble() {
  return false;
}

void FlowGraphCompiler::EnterIntrinsicMode() {
  ASSERT(!intrinsic_mode());
  intrinsic_mode_ = true;
  ASSERT(!assembler()->constant_pool_allowed());
}

void FlowGraphCompiler::ExitIntrinsicMode() {
  ASSERT(intrinsic_mode());
  intrinsic_mode_ = false;
}

#define __ assembler()->

void FlowGraphCompiler::EmitCallToStub(
    const Code& stub,
    ObjectPool::SnapshotBehavior snapshot_behavior) {
  ASSERT(!stub.IsNull());
  if (CanPcRelativeCall(stub)) {
    __ GenerateUnRelocatedPcRelativeCall();
    AddPcRelativeCallStubTarget(stub);
  } else {
    __ JumpAndLink(stub, compiler::ObjectPoolBuilderEntry::kNotPatchable,
                   CodeEntryKind::kNormal, snapshot_behavior);
    AddStubCallTarget(stub);
  }
}

void FlowGraphCompiler::EmitTailCallToStub(const Code& stub) {
  ASSERT(!stub.IsNull());
  if (CanPcRelativeCall(stub)) {
    if (flow_graph().graph_entry()->NeedsFrame()) {
      __ LeaveDartFrame();
    }
    __ GenerateUnRelocatedPcRelativeTailCall();
    AddPcRelativeTailCallStubTarget(stub);
#if defined(DEBUG)
    __ Breakpoint();
#endif
  } else {
    __ LoadObject(CODE_REG, stub);
    if (flow_graph().graph_entry()->NeedsFrame()) {
      __ LeaveDartFrame();
    }
    __ lw(TMP, compiler::FieldAddress(
                   CODE_REG, compiler::target::Code::entry_point_offset()));
    __ jr(TMP);
    AddStubCallTarget(stub);
  }
}

// This function must be in sync with FlowGraphCompiler::RecordSafepoint and
// FlowGraphCompiler::SlowPathEnvironmentFor.
void FlowGraphCompiler::SaveLiveRegisters(LocationSummary* locs) {
#if defined(DEBUG)
  locs->CheckWritableInputs();
  ClobberDeadTempRegisters(locs);
#endif
  __ PushRegisters(*locs->live_registers());
}

void FlowGraphCompiler::RestoreLiveRegisters(LocationSummary* locs) {
  __ PopRegisters(*locs->live_registers());
}

#undef __

}  // namespace dart

#endif  // defined(TARGET_ARCH_MIPS)

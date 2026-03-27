// Copyright (c) 2026, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"  // Needed here to get TARGET_ARCH_MIPS.
#if defined(TARGET_ARCH_MIPS)

#include "vm/compiler/backend/flow_graph_compiler.h"

#include "vm/compiler/backend/il_printer.h"
#include "vm/deopt_instructions.h"

namespace dart {

DEFINE_FLAG(bool, trap_on_deoptimization, false, "Trap on deoptimization.");

void FlowGraphCompiler::ArchSpecificInitialization() {}

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

TypedDataPtr CompilerDeoptInfo::CreateDeoptInfo(FlowGraphCompiler* compiler,
                                                DeoptInfoBuilder* builder,
                                                const Array& deopt_table) {
  if (deopt_env_ == NULL) {
    ++builder->current_info_number_;
    return TypedData::null();
  }

  AllocateOutgoingArguments(deopt_env_);

  intptr_t slot_ix = 0;
  Environment* current = deopt_env_;

  // Emit all kMaterializeObject instructions describing objects to be
  // materialized on the deoptimization as a prefix to the deoptimization info.
  EmitMaterializations(deopt_env_, builder);

  // The real frame starts here.
  builder->MarkFrameStart();

  Zone* zone = compiler->zone();

  builder->AddPp(current->function(), slot_ix++);
  builder->AddPcMarker(Function::ZoneHandle(zone), slot_ix++);
  builder->AddCallerFp(slot_ix++);
  builder->AddReturnAddress(current->function(), deopt_id(), slot_ix++);

  // Emit all values that are needed for materialization as a part of the
  // expression stack for the bottom-most frame. This guarantees that GC
  // will be able to find them during materialization.
  slot_ix = builder->EmitMaterializationArguments(slot_ix);

  // For the innermost environment, set outgoing arguments and the locals.
  for (intptr_t i = current->Length() - 1;
       i >= current->fixed_parameter_count(); i--) {
    builder->AddCopy(current->ValueAt(i), current->LocationAt(i), slot_ix++);
  }

  Environment* previous = current;
  current = current->outer();
  while (current != nullptr) {
    builder->AddPp(current->function(), slot_ix++);
    builder->AddPcMarker(previous->function(), slot_ix++);
    builder->AddCallerFp(slot_ix++);

    // For any outer environment the deopt id is that of the call instruction
    // which is recorded in the outer environment.
    builder->AddReturnAddress(current->function(),
                              DeoptId::ToDeoptAfter(current->GetDeoptId()),
                              slot_ix++);

    // The values of outgoing arguments can be changed from the inlined call so
    // we must read them from the previous environment.
    for (intptr_t i = previous->fixed_parameter_count() - 1; i >= 0; i--) {
      builder->AddCopy(previous->ValueAt(i), previous->LocationAt(i),
                       slot_ix++);
    }

    // Set the locals, note that outgoing arguments are not in the environment.
    for (intptr_t i = current->Length() - 1;
         i >= current->fixed_parameter_count(); i--) {
      builder->AddCopy(current->ValueAt(i), current->LocationAt(i), slot_ix++);
    }

    // Iterate on the outer environment.
    previous = current;
    current = current->outer();
  }
  // The previous pointer is now the outermost environment.
  ASSERT(previous != nullptr);

  // Set slots for the outermost environment.
  builder->AddCallerPp(slot_ix++);
  builder->AddPcMarker(previous->function(), slot_ix++);
  builder->AddCallerFp(slot_ix++);
  builder->AddCallerPc(slot_ix++);

  // For the outermost environment, set the incoming arguments.
  for (intptr_t i = previous->fixed_parameter_count() - 1; i >= 0; i--) {
    builder->AddCopy(previous->ValueAt(i), previous->LocationAt(i), slot_ix++);
  }

  return builder->CreateDeoptInfo(deopt_table);
}

void CompilerDeoptInfoWithStub::GenerateCode(FlowGraphCompiler* compiler,
                                             intptr_t stub_ix) {
  // Calls do not need stubs, they share a deoptimization trampoline.
  ASSERT(reason() != ICData::kDeoptAtCall);
  compiler::Assembler* assembler = compiler->assembler();
#define __ assembler->
  __ Comment("%s", Name());
  __ Bind(entry_label());
  if (FLAG_trap_on_deoptimization) {
    __ break_(0);
  }

  ASSERT(deopt_env() != nullptr);
  __ Call(compiler::Address(THR, Thread::deoptimize_entry_offset()));
  set_pc_offset(assembler->CodeSize());
#undef __
}

#define __ assembler->

void FlowGraphCompiler::GenerateIndirectTTSCall(compiler::Assembler* assembler,
                                                Register reg_to_call,
                                                intptr_t sub_type_cache_index) {
  __ LoadField(
      TTSInternalRegs::kScratchReg,
      compiler::FieldAddress(
          reg_to_call,
          compiler::target::AbstractType::type_test_stub_entry_point_offset()));
  __ LoadWordFromPoolIndex(TypeTestABI::kSubtypeTestCacheReg,
                           sub_type_cache_index);
  __ jalr(TTSInternalRegs::kScratchReg);
}
#undef __
#define __ assembler()->

// Fall through if bool_register contains null.
void FlowGraphCompiler::GenerateBoolToJump(Register bool_register,
                                           compiler::Label* is_true,
                                           compiler::Label* is_false) {
  __ Comment("BoolToJump");
  compiler::Label fall_through;
  __ BranchEqual(bool_register, Object::null_object(), &fall_through);
  BranchLabels labels = {is_true, is_false, &fall_through};
  Condition true_condition =
      EmitBoolTest(bool_register, labels, /*invert=*/false);
  ASSERT(true_condition != kInvalidCondition);
  __ BranchIf(true_condition, is_true);
  __ b(is_false);
  __ Bind(&fall_through);
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

void FlowGraphCompiler::GeneratePatchableCall(
    const InstructionSource& source,
    const Code& stub,
    UntaggedPcDescriptors::Kind kind,
    LocationSummary* locs,
    ObjectPool::SnapshotBehavior snapshot_behavior) {
  __ BranchLinkPatchable(stub, CodeEntryKind::kNormal, snapshot_behavior);
  EmitCallsiteMetadata(source, DeoptId::kNone, kind, locs,
                       pending_deoptimization_env_);
}

void FlowGraphCompiler::GenerateDartCall(intptr_t deopt_id,
                                         const InstructionSource& source,
                                         const Code& stub,
                                         UntaggedPcDescriptors::Kind kind,
                                         LocationSummary* locs,
                                         Code::EntryKind entry_kind) {
    ASSERT(CanCallDart());
  __ BranchLinkPatchable(stub, entry_kind);
  EmitCallsiteMetadata(source, deopt_id, kind, locs, pending_deoptimization_env_);
}

void FlowGraphCompiler::GenerateStaticDartCall(intptr_t deopt_id,
                                               const InstructionSource& source,
                                               UntaggedPcDescriptors::Kind kind,
                                               LocationSummary* locs,
                                               const Function& target,
                                               Code::EntryKind entry_kind) {
  ASSERT(CanCallDart());
  if (CanPcRelativeCall(target)) {
    __ GenerateUnRelocatedPcRelativeCall();
    AddPcRelativeCallTarget(target, entry_kind);
    EmitCallsiteMetadata(source, deopt_id, kind, locs,
                            pending_deoptimization_env_);
  } else {
    // Call sites to the same target can share object pool entries. These
    // call sites are never patched for breakpoints: the function is deoptimized
    // and the unoptimized code with IC calls for static calls is patched instead.
    ASSERT(is_optimizing());
    const auto& stub = StubCode::CallStaticFunction();
    __ BranchLinkWithEquivalence(stub, target, entry_kind);

    EmitCallsiteMetadata(source, deopt_id, kind, locs,
                        pending_deoptimization_env_);
    AddStaticCallTarget(target, entry_kind);
  }
}

void FlowGraphCompiler::EmitFrameEntry() {
  const Function& function = parsed_function().function();
  if (CanOptimizeFunction() && function.IsOptimizable() &&
      (!is_optimizing() || may_reoptimize())) {
    __ Comment("Invocation Count Check");
    const Register function_reg = T0;

    __ lw(function_reg, compiler::FieldAddress(CODE_REG, compiler::target::Code::owner_offset()));

    __ lw(T1, compiler::FieldAddress(function_reg, Function::usage_counter_offset()));
    // Reoptimization of an optimized function is triggered by counting in
    // IC stubs, but not at the entry of the function.
    if (!is_optimizing()) {
      __ addiu(T1, T1, compiler::Immediate(1));
      __ sw(T1, compiler::FieldAddress(function_reg, Function::usage_counter_offset()));
    }

    // Skip Branch if T1 is less than the threshold.
    compiler::Label dont_branch;
    __ BranchSignedLess(T1, compiler::Immediate(GetOptimizationThreshold()),
                        &dont_branch);

    ASSERT(function_reg == T0);
    __ lw(TMP, compiler::Address(THR, Thread::optimize_entry_offset())); // Load value from memory into TMP
    __ jr(TMP);                                                          // Jump to address in TMP

    __ Bind(&dont_branch);
  }
  if (flow_graph().graph_entry()->NeedsFrame()) {
    __ Comment("Enter frame");
    if (flow_graph().IsCompiledForOsr()) {
      const intptr_t extra_slots = ExtraStackSlotsOnOsrEntry();
      ASSERT(extra_slots >= 0);
      __ EnterOsrFrame(extra_slots * compiler::target::kWordSize);
    } else {
      ASSERT(StackSize() >= 0);
      __ EnterDartFrame(StackSize() * compiler::target::kWordSize);
    }
  } else if (FLAG_precompiled_mode) {
    assembler()->set_constant_pool_allowed(true);
  }
}

const InstructionSource& PrologueSource() {
  static InstructionSource prologue_source(TokenPosition::kDartCodePrologue,
                                           /*inlining_id=*/0);
  return prologue_source;
}

void FlowGraphCompiler::EmitPrologue() {
  BeginCodeSourceRange(PrologueSource());

  EmitFrameEntry();
  ASSERT(assembler()->constant_pool_allowed());

  // In unoptimized code, initialize (non-argument) stack allocated slots.
  if (!is_optimizing()) {
    const int num_locals = parsed_function().num_stack_locals();

    intptr_t args_desc_slot = -1;
    if (parsed_function().has_arg_desc_var()) {
      args_desc_slot = compiler::target::frame_layout.FrameSlotForVariable(
          parsed_function().arg_desc_var());
    }

    __ Comment("Initialize spill slots");
    if (num_locals > 1 || (num_locals == 1 && args_desc_slot == -1)) {
      __ LoadObject(T1, Object::null_object());
    }
    for (intptr_t i = 0; i < num_locals; ++i) {
      const intptr_t slot_index =
          compiler::target::frame_layout.FrameSlotForVariableIndex(-i);
      Register value_reg = slot_index == args_desc_slot ? ARGS_DESC_REG : T1;
      __ StoreToOffset(value_reg, FP, slot_index * compiler::target::kWordSize);
    }
  } else if (parsed_function().suspend_state_var() != nullptr &&
             !flow_graph().IsCompiledForOsr()) {
    // Initialize synthetic :suspend_state variable early
    // as it may be accessed by GC and exception handling before
    // InitSuspendableFunction stub is called.
    const intptr_t slot_index =
        compiler::target::frame_layout.FrameSlotForVariable(
            parsed_function().suspend_state_var());
    __ LoadObject(T7, Object::null_object());
    __ StoreToOffset(T7, FP, slot_index * compiler::target::kWordSize);
  }

  EndCodeSourceRange(PrologueSource());
}

void FlowGraphCompiler::EmitOptimizedInstanceCall(
    const Code& stub,
    const ICData& ic_data,
    intptr_t deopt_id,
    const InstructionSource& source,
    LocationSummary* locs,
    Code::EntryKind entry_kind) {
  ASSERT(CanCallDart());
  ASSERT(Array::Handle(zone(), ic_data.arguments_descriptor()).Length() > 0);
  // Each ICData propagated from unoptimized to optimized code contains the
  // function that corresponds to the Dart function of that IC call. Due
  // to inlining in optimized code, that function may not correspond to the
  // top-level function (parsed_function().function()) which could be
  // reoptimized and which counter needs to be incremented.
  // Pass the function explicitly, it is used in IC stub.
  __ Comment("OptimizedInstanceCall");
  __ LoadObject(S2, parsed_function().function());
  __ LoadFromOffset(A0, SP, (ic_data.SizeWithoutTypeArgs() - 1) * kWordSize);
  __ LoadUniqueObject(S5, ic_data);
  GenerateDartCall(deopt_id, source, stub, UntaggedPcDescriptors::kIcCall,
                   locs, entry_kind);
  EmitDropArguments(ic_data.SizeWithTypeArgs());
}

void FlowGraphCompiler::EmitInstanceCallJIT(const Code& stub,
                                            const ICData& ic_data,
                                            intptr_t deopt_id,
                                            const InstructionSource& source,
                                            LocationSummary* locs,
                                            Code::EntryKind entry_kind) {
  ASSERT(CanCallDart());
  ASSERT(entry_kind == Code::EntryKind::kNormal ||
         entry_kind == Code::EntryKind::kUnchecked);
  ASSERT(Array::Handle(zone(), ic_data.arguments_descriptor()).Length() > 0);
  __ LoadFromOffset(A0, SP, (ic_data.SizeWithoutTypeArgs() - 1) * kWordSize);
  __ LoadUniqueObject(S5, ic_data);
  __ LoadUniqueObject(CODE_REG, stub);
  const intptr_t entry_point_offset =
      entry_kind == Code::EntryKind::kNormal
          ? Code::entry_point_offset(Code::EntryKind::kMonomorphic)
          : Code::entry_point_offset(Code::EntryKind::kMonomorphicUnchecked);
  __ lw(T9, compiler::FieldAddress(CODE_REG, entry_point_offset));
  __ jalr(T9);
  EmitCallsiteMetadata(source, deopt_id, UntaggedPcDescriptors::kIcCall, locs,
                       pending_deoptimization_env_);
  EmitDropArguments(ic_data.SizeWithTypeArgs());
}

void FlowGraphCompiler::EmitInstanceCallAOT(const ICData& ic_data,
                                            intptr_t deopt_id,
                                            const InstructionSource& source,
                                            LocationSummary* locs,
                                            Code::EntryKind entry_kind,
                                            bool receiver_can_be_smi) {
  ASSERT(CanCallDart());
  ASSERT(entry_kind == Code::EntryKind::kNormal ||
         entry_kind == Code::EntryKind::kUnchecked);
  ASSERT(ic_data.NumArgsTested() == 1);
  const Code& initial_stub = StubCode::SwitchableCallMiss();
  const char* switchable_call_mode = "smiable";
  if (!receiver_can_be_smi) {
    switchable_call_mode = "non-smi";
    ic_data.set_receiver_cannot_be_smi(true);
  }
  const UnlinkedCall& data =
      UnlinkedCall::ZoneHandle(zone(), ic_data.AsUnlinkedCall());

  __ Comment("InstanceCallAOT (%s)", switchable_call_mode);
  __ LoadImmediate(ARGS_DESC_REG, 0);
  __ lw(A0,
        compiler::Address(SP, (ic_data.SizeWithoutTypeArgs() - 1) * compiler::target::kWordSize));
  // The AOT runtime will replace the slot in the object pool with the
  // entrypoint address - see app_snapshot.cc.
  const auto snapshot_behavior =
      compiler::ObjectPoolBuilderEntry::kResetToSwitchableCallMissEntryPoint;

  __ LoadUniqueObject(T9, initial_stub, snapshot_behavior);
  __ LoadUniqueObject(ICREG, data);
  __ jalr(T9);

  EmitCallsiteMetadata(source, deopt_id, UntaggedPcDescriptors::kOther, locs,
                       pending_deoptimization_env_);
  EmitDropArguments(ic_data.SizeWithTypeArgs());
}

void FlowGraphCompiler::EmitMegamorphicInstanceCall(
    const String& name,
    const Array& arguments_descriptor,
    intptr_t deopt_id,
    const InstructionSource& source,
    LocationSummary* locs) {
  ASSERT(CanCallDart());
  ASSERT(!arguments_descriptor.IsNull() && (arguments_descriptor.Length() > 0));
  ASSERT(!FLAG_precompiled_mode);
  const ArgumentsDescriptor args_desc(arguments_descriptor);
  const MegamorphicCache& cache = MegamorphicCache::ZoneHandle(
      zone(),
      MegamorphicCacheTable::Lookup(thread(), name, arguments_descriptor));

  __ Comment("MegamorphicCall");
  // Load receiver into A0,
  __ LoadFromOffset(A0, SP,
                    (args_desc.Count() - 1) * compiler::target::kWordSize);

  // Use same code pattern as instance call so it can be parsed by code patcher.
  __ LoadUniqueObject(S5, cache);
  __ LoadUniqueObject(CODE_REG, StubCode::MegamorphicCall());
  __ Call(compiler::FieldAddress(
      CODE_REG, Code::entry_point_offset(Code::EntryKind::kMonomorphic)));

  RecordSafepoint(locs);
  AddCurrentDescriptor(UntaggedPcDescriptors::kOther, DeoptId::kNone, source);
  const intptr_t deopt_id_after = DeoptId::ToDeoptAfter(deopt_id);
  if (is_optimizing()) {
    AddDeoptIndexAtCall(deopt_id_after, pending_deoptimization_env_);
  } else {
    // Add deoptimization continuation point after the call and before the
    // arguments are removed.
    AddCurrentDescriptor(UntaggedPcDescriptors::kDeopt, deopt_id_after, source);
  }
  RecordCatchEntryMoves(pending_deoptimization_env_);
  EmitDropArguments(args_desc.SizeWithTypeArgs());
}

void FlowGraphCompiler::EmitUnoptimizedStaticCall(
    intptr_t size_with_type_args,
    intptr_t deopt_id,
    const InstructionSource& source,
    LocationSummary* locs,
    const ICData& ic_data,
    Code::EntryKind entry_kind) {
  ASSERT(CanCallDart());
  const Code& stub =
      StubCode::UnoptimizedStaticCallEntry(ic_data.NumArgsTested());
  __ LoadObject(S5, ic_data);
  GenerateDartCall(deopt_id, source, stub,
                   UntaggedPcDescriptors::kUnoptStaticCall, locs, entry_kind);
  EmitDropArguments(size_with_type_args);
}

void FlowGraphCompiler::EmitOptimizedStaticCall(
    const Function& function,
    const Array& arguments_descriptor,
    intptr_t size_with_type_args,
    intptr_t deopt_id,
    const InstructionSource& source,
    LocationSummary* locs,
    Code::EntryKind entry_kind) {
  ASSERT(CanCallDart());
  ASSERT(!function.IsClosureFunction());
  if (function.PrologueNeedsArgumentsDescriptor()) {
    __ LoadObject(ARGS_DESC_REG, arguments_descriptor);
  } else {
    if (!FLAG_precompiled_mode) {
      __ LoadImmediate(ARGS_DESC_REG, 0);  // GC safe smi zero because of stub.
    }
  }
  // Do not use the code from the function, but let the code be patched so that
  // we can record the outgoing edges to other code.
  GenerateStaticDartCall(deopt_id, source, UntaggedPcDescriptors::kOther, locs,
                         function, entry_kind);
  EmitDropArguments(size_with_type_args);
}

Condition FlowGraphCompiler::EmitBoolTest(Register value,
                                          BranchLabels labels,
                                          bool invert) {
  __ Comment("BoolTest");
  __ TestImmediate(value, compiler::target::ObjectAlignment::kBoolValueMask);
  return invert ? NE : EQ;
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

#if defined(DEBUG)
void FlowGraphCompiler::ClobberDeadTempRegisters(LocationSummary* locs) {
  // Clobber temporaries that have not been manually preserved.
  for (intptr_t i = 0; i < locs->temp_count(); ++i) {
    Location tmp = locs->temp(i);
    if (tmp.IsRegister() &&
        !locs->live_registers()->ContainsRegister(tmp.reg())) {
      __ LoadImmediate(tmp.reg(), 0xf7);
    }
  }
}
#endif

Register FlowGraphCompiler::EmitTestCidRegister() {
  return A1;
}

void FlowGraphCompiler::EmitTestAndCallLoadReceiver(
    intptr_t argument_count,
    const Array& arguments_descriptor) {
  __ Comment("EmitTestAndCall");
  // Load receiver into T0.
  __ LoadFromOffset(T0, SP, (argument_count - 1) * kWordSize);
  __ LoadObject(S4, arguments_descriptor);
}

void FlowGraphCompiler::EmitTestAndCallSmiBranch(compiler::Label* label, bool if_smi) {
  __ AndImmediate(CMPRES1, T0, kSmiTagMask);
  if (if_smi) {
    // Jump if receiver is Smi.
    __ beq(CMPRES1, ZR, label);
  } else {
    // Jump if receiver is not Smi.
    __ bne(CMPRES1, ZR, label);
  }
}

void FlowGraphCompiler::EmitTestAndCallLoadCid(Register class_id_reg) {
  ASSERT(class_id_reg != T0);
  __ LoadClassId(class_id_reg, T0);
}

void FlowGraphCompiler::EmitMove(Location destination,
                                 Location source,
                                 TemporaryRegisterAllocator* allocator) {
  if (destination.Equals(source)) return;

  if (source.IsRegister()) {
    if (destination.IsRegister()) {
      __ mov(destination.reg(), source.reg());
    } else {
      ASSERT(destination.IsStackSlot());
      const intptr_t dest_offset = destination.ToStackSlotOffset();
      __ StoreToOffset(source.reg(), destination.base_reg(), dest_offset);
    }
  } else if (source.IsStackSlot()) {
    if (destination.IsRegister()) {
      const intptr_t source_offset = source.ToStackSlotOffset();
      __ LoadFromOffset(destination.reg(), source.base_reg(), source_offset);
    } else {
      ASSERT(destination.IsStackSlot());
      const intptr_t source_offset = source.ToStackSlotOffset();
      const intptr_t dest_offset = destination.ToStackSlotOffset();
      Register tmp = allocator->AllocateTemporary();
      __ LoadFromOffset(tmp, source.base_reg(), source_offset);
      __ StoreToOffset(tmp, destination.base_reg(), dest_offset);
      allocator->ReleaseTemporary();
    }
  } else if (source.IsFpuRegister()) {
    if (destination.IsFpuRegister()) {
      DRegister dst = destination.fpu_reg();
      DRegister src = source.fpu_reg();
      __ movd(dst, src);
    } else if (destination.IsStackSlot()) {
      // 32-bit float
      const intptr_t dest_offset = destination.ToStackSlotOffset();
      const FRegister src = EvenFRegisterOf(source.fpu_reg());
      __ swc1(src, compiler::Address(destination.base_reg(), dest_offset));
    } else {
      ASSERT(destination.IsDoubleStackSlot());
      const intptr_t dest_offset = destination.ToStackSlotOffset();
      DRegister src = source.fpu_reg();
      __ StoreDToOffset(src, destination.base_reg(), dest_offset);
    }
  } else if (source.IsDoubleStackSlot()) {
    if (destination.IsFpuRegister()) {
      const intptr_t source_offset = source.ToStackSlotOffset();
      DRegister dst = destination.fpu_reg();
      __ LoadDFromOffset(dst, source.base_reg(), source_offset);
    } else if (destination.IsStackSlot()) {
      const intptr_t source_offset = source.ToStackSlotOffset();
      const intptr_t dest_offset = destination.ToStackSlotOffset();
      __ lwc1(EvenFRegisterOf(DTMP), compiler::Address(source.base_reg(), source_offset));
      __ swc1(EvenFRegisterOf(DTMP), compiler::Address(destination.base_reg(), dest_offset));

    } else {
      ASSERT(destination.IsDoubleStackSlot());
      const intptr_t source_offset = source.ToStackSlotOffset();
      const intptr_t dest_offset = destination.ToStackSlotOffset();
      __ LoadDFromOffset(DTMP, source.base_reg(), source_offset);
      __ StoreDToOffset(DTMP, destination.base_reg(), dest_offset);
    }
  } else if (source.IsPairLocation()) {
    ASSERT(destination.IsPairLocation());
    for (intptr_t i : {0, 1}) {
      EmitMove(destination.Component(i), source.Component(i), allocator);
    }
  } else {
    ASSERT(source.IsConstant());
    if (destination.IsFpuRegister() || destination.IsDoubleStackSlot() ||
        destination.IsStackSlot()) {
      Register tmp = allocator->AllocateTemporary();
      source.constant_instruction()->EmitMoveToLocation(this, destination, tmp,
                                                        source.pair_index());
      allocator->ReleaseTemporary();
    } else {
      source.constant_instruction()->EmitMoveToLocation(
          this, destination, kNoRegister, source.pair_index());
    }
  }
}

static compiler::OperandSize BytesToOperandSize(intptr_t bytes) {
  switch (bytes) {
    case 8:
      return compiler::OperandSize::kEightBytes;
    case 4:
      return compiler::OperandSize::kFourBytes;
    case 2:
      return compiler::OperandSize::kTwoBytes;
    case 1:
      return compiler::OperandSize::kByte;
    default:
      UNIMPLEMENTED();
  }
}

void FlowGraphCompiler::EmitNativeMoveArchitecture(
    const compiler::ffi::NativeLocation& destination,
    const compiler::ffi::NativeLocation& source) {
  const auto& src_payload_type = source.payload_type();
  const auto& dst_payload_type = destination.payload_type();
  const auto& src_container_type = source.container_type();
  const auto& dst_container_type = destination.container_type();
  ASSERT(src_container_type.IsFloat() == dst_container_type.IsFloat());
  ASSERT(src_container_type.IsInt() == dst_container_type.IsInt());
  ASSERT(src_payload_type.IsSigned() == dst_payload_type.IsSigned());
  ASSERT(src_payload_type.IsPrimitive());
  ASSERT(dst_payload_type.IsPrimitive());
  const intptr_t src_size = src_payload_type.SizeInBytes();
  const intptr_t dst_size = dst_payload_type.SizeInBytes();
  const bool sign_or_zero_extend = dst_size > src_size;

  if (source.IsRegisters()) {
    const auto& src = source.AsRegisters();

    if (destination.IsRegisters()) {
      ASSERT(src.num_regs() == 1);
      ASSERT(src_size <= 4);
      const auto src_reg = src.reg_at(0);
      const auto& dst = destination.AsRegisters();
      ASSERT(dst.num_regs() == 1);
      const auto dst_reg = dst.reg_at(0);
      ASSERT(destination.container_type().SizeInBytes() <=
            compiler::target::kWordSize);
      if (!sign_or_zero_extend) {
        __ MoveRegister(dst_reg, src_reg);
      } else {
        switch (src_payload_type.AsPrimitive().representation()) {
          case compiler::ffi::kInt8:  // Sign extend operand.
            __ ExtendValue(dst_reg, src_reg, compiler::kByte);
            return;
          case compiler::ffi::kInt16:
            __ ExtendValue(dst_reg, src_reg, compiler::kTwoBytes);
            return;
          case compiler::ffi::kInt24:
            __ MoveRegister(dst_reg, src_reg);
            __ sll(dst_reg, dst_reg, 8);
            __ sra(dst_reg, dst_reg, 8);
            return;
          case compiler::ffi::kUint8:  // Zero extend operand.
            __ ExtendValue(dst_reg, src_reg, compiler::kUnsignedByte);
            return;
          case compiler::ffi::kUint16:
            __ ExtendValue(dst_reg, src_reg, compiler::kUnsignedTwoBytes);
            return;
          case compiler::ffi::kUint24:
            __ AndImmediate(dst_reg, src_reg, 0xFFFFFF);
            return;
          default:
            UNIMPLEMENTED();
        }
      }

    } else if (destination.IsFpuRegisters()) {
      const auto& dst = destination.AsFpuRegisters();
      ASSERT(src_size == dst_size);
      ASSERT(dst.fpu_reg_kind() == compiler::ffi::kSingleFpuReg ||
             dst.fpu_reg_kind() == compiler::ffi::kDoubleFpuReg);
      const DRegister dst_dreg = dst.fpu_reg();

      if (src_size == 4) {
        // Single float: move 1 int reg to even F-register
        ASSERT(src.num_regs() == 1);
        __ mtc1(src.reg_at(0), EvenFRegisterOf(dst_dreg));
      } else if (src_size == 8) {
        // Double: move 2 int regs to D-register (for varargs doubles passed in int regs)
        // On little-endian MIPS, reg_at(0) has low word, reg_at(1) has high word
        ASSERT(src.num_regs() == 2);
        __ mtc1(src.reg_at(0), EvenFRegisterOf(dst_dreg));  // Low word to even F-reg
        __ mtc1(src.reg_at(1), OddFRegisterOf(dst_dreg));   // High word to odd F-reg
      } else {
        UNREACHABLE();
      }

    } else {
      ASSERT(destination.IsStack());
      const auto& dst = destination.AsStack();
      ASSERT(!sign_or_zero_extend);
      auto const op_size =
          BytesToOperandSize(destination.container_type().SizeInBytes());
      __ StoreToOffset(src.reg_at(0), dst.base_register(),
                       dst.offset_in_bytes(), op_size);
    }

  } else if (source.IsFpuRegisters()) {
    const auto& src = source.AsFpuRegisters();
    // We have not implemented conversions here, use IL convert instructions.
    ASSERT(src_payload_type.Equals(dst_payload_type));

    if (destination.IsRegisters()) {
      const auto& dst = destination.AsRegisters();
      ASSERT(src_size == dst_size);
      ASSERT(src.fpu_reg_kind() == compiler::ffi::kSingleFpuReg ||
             src.fpu_reg_kind() == compiler::ffi::kDoubleFpuReg);
      const DRegister src_dreg = src.fpu_reg();

      if (src_size == 4) {
        // Single float: move even F-register to 1 int reg
        ASSERT(dst.num_regs() == 1);
        __ mfc1(dst.reg_at(0), EvenFRegisterOf(src_dreg));
      } else if (src_size == 8) {
        // Double: move D-register to 2 int regs (for varargs doubles passed in int regs)
        // On little-endian MIPS, reg_at(0) gets low word, reg_at(1) gets high word
        ASSERT(dst.num_regs() == 2);
        __ mfc1(dst.reg_at(0), EvenFRegisterOf(src_dreg));  // Low word from even F-reg
        __ mfc1(dst.reg_at(1), OddFRegisterOf(src_dreg));   // High word from odd F-reg
      } else {
        UNREACHABLE();
      }

    } else if (destination.IsFpuRegisters()) {
      const auto& dst = destination.AsFpuRegisters();
      // Get the D-register indices and convert to F-registers
      ASSERT(src.fpu_reg_kind() == compiler::ffi::kSingleFpuReg ||
             src.fpu_reg_kind() == compiler::ffi::kDoubleFpuReg);
      ASSERT(dst.fpu_reg_kind() == compiler::ffi::kSingleFpuReg ||
             dst.fpu_reg_kind() == compiler::ffi::kDoubleFpuReg);
      switch (dst_size) {
        case 8: {
          const DRegister src_dreg = src.fpu_reg();
          const DRegister dst_dreg = dst.fpu_reg();
          __ movd(dst_dreg, src_dreg);
          return;
        }
        case 4: {
          const intptr_t src_dreg_index = static_cast<intptr_t>(src.fpu_reg());
          const intptr_t dst_dreg_index = static_cast<intptr_t>(dst.fpu_reg());
          __ movs(FRegister(dst_dreg_index*2), FRegister(src_dreg_index*2));
          return;
        }
        default:
          UNREACHABLE();
      }

    } else {
      ASSERT(destination.IsStack());
      ASSERT(src_payload_type.IsFloat());
      const auto& dst = destination.AsStack();
      const auto dst_addr = NativeLocationToStackSlotAddress(dst);
      // Get the D-register index and convert to F-register.
      ASSERT(src.fpu_reg_kind() == compiler::ffi::kSingleFpuReg ||
             src.fpu_reg_kind() == compiler::ffi::kDoubleFpuReg);
      switch (dst_size) {
        case 8: {
          const DRegister src_dreg = src.fpu_reg();
          __ sdc1(src_dreg, dst_addr);
          return;
        }
        case 4: {
          const intptr_t src_dreg_index = static_cast<intptr_t>(src.fpu_reg());
          __ swc1(FRegister(src_dreg_index*2), dst_addr);
          return;
        }
        default:
          UNREACHABLE();
      }
    }

  } else {
    ASSERT(source.IsStack());
    const auto& src = source.AsStack();
    const auto src_addr = NativeLocationToStackSlotAddress(src);
    if (destination.IsRegisters()) {
      const auto& dst = destination.AsRegisters();
      ASSERT(dst.num_regs() == 1);
      const auto dst_reg = dst.reg_at(0);
      EmitNativeLoad(dst_reg, src.base_register(), src.offset_in_bytes(),
                     src_payload_type.AsPrimitive().representation());
    } else if (destination.IsFpuRegisters()) {
      ASSERT(src_payload_type.Equals(dst_payload_type));
      ASSERT(src_payload_type.IsFloat());
      const auto& dst = destination.AsFpuRegisters();
      // Get the D-register index and convert to F-register.
      ASSERT(dst.fpu_reg_kind() == compiler::ffi::kSingleFpuReg ||
             dst.fpu_reg_kind() == compiler::ffi::kDoubleFpuReg);
      switch (src_size) {
        case 8: {
          const DRegister dst_dreg = dst.fpu_reg();
          __ ldc1(dst_dreg, src_addr);
          return;
        }
        case 4: {
          const intptr_t dst_dreg_index = static_cast<intptr_t>(dst.fpu_reg());
          __ lwc1(FRegister(dst_dreg_index*2), src_addr);
          return;
        }
        default:
          UNIMPLEMENTED();
      }

    } else {
      ASSERT(destination.IsStack());
      UNREACHABLE();
    }
  }
}

void FlowGraphCompiler::EmitNativeLoad(Register dst,
                                       Register base,
                                       intptr_t offset,
                                       compiler::ffi::PrimitiveType type) {
  switch (type) {
    case compiler::ffi::kInt8:
      __ lb(dst, compiler::Address(base, offset));
      break;
    case compiler::ffi::kUint8:
      __ lbu(dst, compiler::Address(base, offset));
      break;
    case compiler::ffi::kInt16:
      __ lh(dst, compiler::Address(base, offset));
      break;
    case compiler::ffi::kUint16:
      __ lhu(dst, compiler::Address(base, offset));
      break;
    case compiler::ffi::kInt32:
      __ lw(dst, compiler::Address(base, offset));
      break;
    case compiler::ffi::kUint32:
    case compiler::ffi::kFloat:
    case compiler::ffi::kHalfDouble:
      __ lw (dst, compiler::Address(base, offset));
      break;
    case compiler::ffi::kInt24:
      __ lhu(dst, compiler::Address(base, offset));
      __ lb(TMP, compiler::Address(base, offset + 2));
      __ sll(TMP, TMP, 16);
      __ or_(dst, dst, TMP);
      break;
    case compiler::ffi::kUint24:
      __ lhu(dst, compiler::Address(base, offset));
      __ lbu(TMP, compiler::Address(base, offset + 2));
      __ sll(TMP, TMP, 16);
      __ or_(dst, dst, TMP);
      break;
    default:
      UNREACHABLE();
  }
}

#undef __

}  // namespace dart

#endif  // defined(TARGET_ARCH_MIPS)

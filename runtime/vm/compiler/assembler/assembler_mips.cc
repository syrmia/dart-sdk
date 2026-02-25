// Copyright (c) 2025, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#if defined(TARGET_ARCH_MIPS)

#define SHOULD_NOT_INCLUDE_RUNTIME

#include "vm/compiler/assembler/assembler.h"
#include "vm/compiler/backend/locations.h"

namespace dart {
  
DECLARE_FLAG(bool, check_code_pointer);

namespace compiler{

void Assembler::EmitBranch(Opcode b, Register rs, Register rt, Label* label) {
  UNIMPLEMENTED();
}  

void Assembler::EmitRegImmBranch(RtRegImm b, Register rs, Label* label) {
  UNIMPLEMENTED();
}

void Assembler::EmitFpuBranch(bool kind, Label* label) {
  UNIMPLEMENTED();
}

void Assembler::PushRegisters(const RegisterSet& registers) {
  UNIMPLEMENTED();
}

void Assembler::PopRegisters(const RegisterSet& registers) {
  UNIMPLEMENTED();
}

void Assembler::PushRegistersInOrder(std::initializer_list<Register> regs) {
  for (Register reg : regs) {
    PushRegister(reg);
  }
}

void Assembler::PushRegisterPair(Register r0, Register r1){
  ASSERT(r0 != SP);
  ASSERT(r1 != SP);

  addiu(SP, SP, Immediate(-2 * target::kWordSize));
  sw(r1, Address(SP, target::kWordSize));
  sw(r0, Address(SP, 0));
}

void Assembler::PopRegisterPair(Register r0, Register r1){
  ASSERT(r0 != SP);
  ASSERT(r1 != SP);

  lw(r1, Address(SP, target::kWordSize));
  lw(r0, Address(SP, 0));
  addiu(SP, SP, Immediate(2 * target::kWordSize));
}

void Assembler::PushImmediate(int64_t immediate){
  LoadImmediate(TMP, immediate);
  Push(TMP);
}

void Assembler::PushValueAtOffset(Register base, int32_t offset){
  addiu(SP, SP, Immediate(-target::kWordSize));
  lw(TMP, Address(base, offset));
  sw(TMP, Address(SP, 0));
}

void Assembler::CompareRegisters(Register rn, Register rm) {
  ASSERT(deferred_compare_ == kNone);
  deferred_compare_ = kCompareReg;
  deferred_left_ = rn;
  deferred_reg_ = rm;
}

void Assembler::CompareObjectRegisters(Register rn, Register rm) {
  CompareRegisters(rn, rm);
}

void Assembler::TestRegisters(Register rn, Register rm) {
  ASSERT(deferred_compare_ == kNone);
  deferred_compare_ = kTestReg;
  deferred_left_ = rn;
  deferred_reg_ = rm;
}

void Assembler::CompareImmediate(Register rn, int32_t imm, OperandSize sz) {
  ASSERT(deferred_compare_ == kNone);
  deferred_compare_ = kCompareImm;
  deferred_left_ = rn;
  deferred_imm_ = imm;
}

void Assembler::TestImmediate(Register rn, int32_t imm, OperandSize sz) {
  ASSERT(deferred_compare_ == kNone);
  deferred_compare_ = kTestImm;
  deferred_left_ = rn;
  deferred_imm_ = imm;
}

// Branch to label if condition is true.
void Assembler::BranchIf(Condition cond, Label* l, JumpDistance distance) {
  ASSERT(!in_delay_slot_);
  ASSERT(deferred_compare_ != kNone);

  if (deferred_compare_ == kCompareImm || deferred_compare_ == kCompareReg) {
    Register left = deferred_left_;
    Register right;
    if (deferred_compare_ == kCompareImm) {
      if (deferred_imm_ == 0) {
        right = ZR;
      } else {
        LoadImmediate(AT, deferred_imm_);
        right = AT;
      }
    } else {
      right = deferred_reg_;
    }
    switch (cond) {
      case NV: {
        deferred_compare_ = kNone; // Consumed.
        return;
      }
      case AL: {
        b(l);
        deferred_compare_ = kNone; // Consumed.
        return;
      }
      case EQ:{
        beq(left, right, l);
        break;
      }
      case NE:{
        bne(left, right, l);
        break;
      }
      case GT:{
        slt(AT, right, left);
        bne(AT, ZR, l);
        break;
      }
      case GE: {
        slt(AT, left, right);
        beq(AT, ZR, l);
        break;
      }
      case LT: {
        slt(AT, left, right);
        bne(AT, ZR, l);
        break;
      }
      case LE: {
        slt(AT, right, left);
        beq(AT, ZR, l);
        break;
      }
      case UGT: {
        sltu(AT, right, left);
        bne(AT, ZR, l);
        break;
      }
      case UGE: {
        sltu(AT, left, right);
        beq(AT, ZR, l);
        break;
      }
      case ULT: {
        sltu(AT, left, right);
        bne(AT, ZR, l);
        break;
      }
      case ULE: {
        sltu(AT, right, left);
        beq(AT, ZR, l);
        break;
      }
      default:
        UNREACHABLE();
    }
  } else if (deferred_compare_ == kTestImm || deferred_compare_ == kTestReg) {
    if (deferred_compare_ == kTestReg) {
      and_(CMPRES1, deferred_left_, deferred_reg_);
    } else {
      AndImmediate(CMPRES1, deferred_left_, deferred_imm_);
    }
    switch (cond) {
      case ZERO:
        beq(CMPRES1, ZR, l);
        break;
      case NOT_ZERO:
        bne(CMPRES1, ZR, l);
        break;
      default:
        UNREACHABLE();
    }
  } else {
    UNREACHABLE();
  }
  deferred_compare_ = kNone; // Consumed.
}

void Assembler::BranchIfZero(Register rn, Label* label, JumpDistance distance) {
  beq(rn, ZR, label);
}

void Assembler::SetIf(Condition condition, Register rd) {
  ASSERT(deferred_compare_ != kNone);

  Register left = deferred_left_;
  Register right;
  if (deferred_compare_ == kCompareImm || deferred_compare_ == kCompareReg) {
    if (deferred_compare_ == kCompareImm) {
      if (deferred_imm_ == 0) {
        right = ZR;
      } else {
        LoadImmediate(AT, deferred_imm_);
        right = AT;
      }
    } else {
      right = deferred_reg_;
    }

    switch (condition) {
      case AL:
      case NV:
        deferred_compare_ = kNone;
        return;  // Result holds true_value.
      case EQ:{
        xor_(rd, left, right);
        sltiu(rd, rd, compiler::Immediate(1));
        break;
      }
      case NE: {
        xor_(rd, left, right);
        break;
      }
      case GE:{
        slt(rd, left, right);
        xori(rd, rd, compiler::Immediate(1)); 
        break;
      }
      case LT: {
        slt(rd, left, right);
        break;
      }
      case LE:{
        slt(rd, right, left);
        xori(rd, rd, compiler::Immediate(1)); 
        break;
      }
      case GT: {
        slt(rd, right, left);
        break;
      }
      case UGE:{
        sltu(rd, left, right);
        xori(rd, rd, compiler::Immediate(1));
        break;
      }
      case ULT: {
        sltu(rd, left, right);
        break;
      }
      case ULE:{
        sltu(rd, right, left);
        xori(rd, rd, compiler::Immediate(1));
        break;
      }
      case UGT: {
        sltu(rd, right, left);
        break;
      }
      default:
        UNREACHABLE();
    }
  } else if (deferred_compare_ == kTestImm) {
    AndImmediate(rd, deferred_left_, deferred_imm_);
    switch (condition) {
      case ZERO:
        sltiu(rd, rd, compiler::Immediate(1));
        break;
      case NOT_ZERO:
        sltu(rd, ZR, rd);
        break;
      default:
        UNREACHABLE();
    }
  } else if (deferred_compare_ == kTestReg) {
    and_(rd, deferred_left_, deferred_reg_);
    switch (condition) {
      case ZERO:
        sltiu(rd, rd, compiler::Immediate(1));
        break;
      case NOT_ZERO:
        sltu(rd, ZR, rd);
        break;
      default:
        UNREACHABLE();
    }
  } else {
    UNREACHABLE();
  }

  deferred_compare_ = kNone;
}

void Assembler::Bind(Label* label) {
  UNIMPLEMENTED();
}

Address Assembler::PrepareLargeOffset(Register base, int32_t offset) {
  ASSERT(!in_delay_slot_);
  if (Utils::IsInt(kImmBits, offset)) {
    return Address(base, offset);
  } else {
    ASSERT(base != TMP);
    LoadImmediate(TMP, offset);     
    addu(TMP, base, TMP);            
    return Address(TMP, 0);          
  }
}

void Assembler::LoadObjectHelper(Register rd,
                                 const Object& object,
                                 bool is_unique,
                                 ObjectPoolBuilderEntry::SnapshotBehavior snapshot_behavior) {
  ASSERT(IsOriginalObject(object));
  // `is_unique == true` effectively means object has to be patchable.
  // (even if the object is null)
  if(!is_unique){
    intptr_t offset = 0;
    if (target::CanLoadFromThread(object, &offset)) {
      // Load common VM constants from the thread. This works also in places where
      // no constant pool is set up (e.g. intrinsic code).
      lw(rd, Address(THR, offset));
      return;
    }
    if (target::IsSmi(object)) {
      // Relocation doesn't apply to Smis.
      LoadImmediate(rd, target::ToRawSmi(object));
      return;
    }
  }
  RELEASE_ASSERT(CanLoadFromObjectPool(object));
  // Make sure that class CallPattern is able to decode this load from the
  // object pool.
  const intptr_t index =
      is_unique ? object_pool_builder().AddObject(
                      object, ObjectPoolBuilderEntry::kPatchable, snapshot_behavior)
                : object_pool_builder().FindObject(
                      object, ObjectPoolBuilderEntry::kNotPatchable,
                      snapshot_behavior);
  LoadWordFromPoolIndex(rd, index);
}

void Assembler::LoadObject(Register rd, const Object& object) {
  LoadObjectHelper(rd, object, false);
}

void Assembler::LoadUniqueObject(Register rd, const Object& object, 
                                ObjectPoolBuilderEntry::SnapshotBehavior snapshot_behavior) {
  LoadObjectHelper(rd, object, true, snapshot_behavior);
}

void Assembler::LoadClassId(Register result, Register object) {
  UNIMPLEMENTED();
}

void Assembler::LoadClassById(Register result, Register class_id) {
  UNIMPLEMENTED();
}

void Assembler::CompareClassId(Register object,
                               intptr_t class_id,
                               Register scratch) {
  UNIMPLEMENTED();
}

void Assembler::LoadClassIdMayBeSmi(Register result, Register object) {
  UNIMPLEMENTED();
}

void Assembler::LoadTaggedClassIdMayBeSmi(Register result, Register object) {
  UNIMPLEMENTED();
}

bool Assembler::CanLoadFromObjectPool(const Object& object) const {
  ASSERT(IsOriginalObject(object));
  if (!constant_pool_allowed()) {
    return false;
  }

#if defined(DEBUG)
  ASSERT(IsNotTemporaryScopedHandle(object));
#endif
  ASSERT(IsInOldSpace(object));
  return true;
}

void Assembler::LoadWordFromPoolIndex(Register rd,
                                      intptr_t index,
                                      Register pp) {
  ASSERT((pp != PP) || constant_pool_allowed());
  ASSERT(!in_delay_slot_);
  ASSERT(rd != pp);

  uint32_t offset = target::ObjectPool::element_offset(index) - kHeapObjectTag;

  if (Address::CanHoldOffset(offset)) {
    lw(rd, Address(pp, offset));
  } else {
    const int16_t offset_low = Utils::Low16Bits(offset);     // Signed.
    offset -= offset_low;
    const uint16_t offset_high = Utils::High16Bits(offset);  // Unsigned.
    if (offset_high != 0) {
      lui(rd, Immediate(offset_high));
      addu(rd, rd, pp);
      lw(rd, Address(rd, offset_low));
    } else {
      lw(rd, Address(pp, offset_low));
    }
  }
}

void Assembler::StoreWordToPoolIndex(Register rs,
                                     intptr_t index,
                                     Register pp) {
  ASSERT((pp != PP) || constant_pool_allowed());
  ASSERT(!in_delay_slot_);
  ASSERT(rs != pp);

  uint32_t offset =
      target::ObjectPool::element_offset(index) - kHeapObjectTag;

  if (Address::CanHoldOffset(offset)) {
    sw(rs, Address(pp, offset));
  } else {
    const int16_t offset_low = Utils::Low16Bits(offset);     // Signed.
    offset -= offset_low;
    const uint16_t offset_high = Utils::High16Bits(offset);  // Unsigned.
    if (offset_high != 0) {
      lui(TMP, Immediate(offset_high));
      addu(TMP, TMP, pp);
      sw(rs, Address(TMP, offset_low));
    } else {
      sw(rs, Address(pp, offset_low));
    }
  }
}

void Assembler::JumpAndLink(intptr_t target_code_pool_index,
                            CodeEntryKind entry_kind) {
  // Avoid clobbering CODE_REG when invoking code in precompiled mode.
  // We don't actually use CODE_REG in the callee and caller might
  // be using CODE_REG for a live value (e.g. a value that is alive
  // across invocation of a shared stub like the one we use for
  // allocating Mint boxes).
  const Register code_reg = FLAG_precompiled_mode ? TMP : CODE_REG;
  LoadWordFromPoolIndex(code_reg, target_code_pool_index);
  Call(FieldAddress(code_reg, target::Code::entry_point_offset(entry_kind)));
}

void Assembler::JumpAndLink(
    const Code& target,
    ObjectPoolBuilderEntry::Patchability patchable,
    CodeEntryKind entry_kind,
    ObjectPoolBuilderEntry::SnapshotBehavior snapshot_behavior) {
  const intptr_t index = object_pool_builder().FindObject(
      ToObject(target), patchable, snapshot_behavior);
  JumpAndLink(index, entry_kind);
}

// Generate code to call into the stub which will call the runtime
// function. Input for the stub is as follows:
//   SP : points to the arguments and return value array.
//   S5 : address of the runtime function to call.
//   S4 : number of arguments to the call.
void Assembler::CallRuntime(const RuntimeEntry& entry,
                            intptr_t argument_count) {
  ASSERT(!entry.is_leaf());
  // Argument count is not checked here, but in the runtime entry for a more
  // informative error message.
  lw(S5, compiler::Address(THR, entry.OffsetFromThread()));
  LoadImmediate(S4, argument_count);
  Call(Address(THR, target::Thread::call_to_runtime_entry_point_offset()));
}

void Assembler::LoadPoolPointer(Register reg) {
  ASSERT(!in_delay_slot_);
  CheckCodePointer();
  lw(reg, FieldAddress(CODE_REG, target::Code::object_pool_offset()));
  set_constant_pool_allowed(reg == PP);
}

void Assembler::CheckCodePointer() {
#ifdef DEBUG
  if (!FLAG_check_code_pointer) {
    return;
  }
  Comment("CheckCodePointer");
  Label cid_ok, instructions_ok;
  Push(CMPRES1);
  Push(CMPRES2);
  LoadClassId(CMPRES1, CODE_REG);
  BranchEqual(CMPRES1, Immediate(kCodeCid), &cid_ok);
  break_(0);
  Bind(&cid_ok);
  GetNextPC(CMPRES1, TMP);
  const intptr_t entry_offset = CodeSize() - Instr::kInstrSize +
                                target::Instructions::HeaderSize() - kHeapObjectTag;
  AddImmediate(CMPRES1, CMPRES1, -entry_offset);
  lw(CMPRES2, FieldAddress(CODE_REG, target::Code::instructions_offset()));
  BranchEqual(CMPRES1, CMPRES2, &instructions_ok);
  break_(1);
  Bind(&instructions_ok);
  Pop(CMPRES2);
  Pop(CMPRES1);
#endif
}

void Assembler::GetNextPC(Register dest, Register temp) {
  if (temp != kNoRegister) {
    mov(temp, RA);
  }
  EmitRegImmType(REGIMM, R0, BGEZAL, 1);
  mov(dest, RA);
  if (temp != kNoRegister) {
    mov(RA, temp);
  }
}

void Assembler::EnterFrame(intptr_t frame_size) {
  ASSERT(!in_delay_slot_);
  addiu(SP, SP, Immediate(-2 * target::kWordSize));
  sw(RA, Address(SP, 1 * target::kWordSize));
  sw(FP, Address(SP, 0 * target::kWordSize));
  mov(FP, SP);
  if (frame_size != 0) {
    addiu(SP, SP, Immediate(-frame_size));
  }
}

void Assembler::LeaveFrame() {
  ASSERT(!in_delay_slot_);
  mov(SP, FP);
  lw(RA, Address(SP, 1 * target::kWordSize));
  lw(FP, Address(SP, 0 * target::kWordSize));
  addiu(SP, SP, Immediate(2 * target::kWordSize));
}

void Assembler::LeaveFrameAndReturn() {
  ASSERT(!in_delay_slot_);
  mov(SP, FP);
  lw(RA, Address(SP, 1 * target::kWordSize));
  lw(FP, Address(SP, 0 * target::kWordSize));
  Ret();
  delay_slot()->addiu(SP, SP, Immediate(2 * target::kWordSize));
}

void Assembler::EnterStubFrame(intptr_t frame_size) {
  EnterDartFrame(frame_size);
}

void Assembler::LeaveStubFrame() {
  LeaveDartFrame();
}

void Assembler::LeaveStubFrameAndReturn(Register ra) {
  LeaveDartFrameAndReturn(ra);
}

void Assembler::EnterDartFrame(intptr_t frame_size, bool load_pool_pointer) {
  ASSERT(!in_delay_slot_);

  SetPrologueOffset();

  if (FLAG_precompiled_mode) {
    addiu(SP, SP, Immediate(-2 * target::kWordSize));
    sw(RA, Address(SP, 1 * target::kWordSize));
    sw(FP, Address(SP, 0 * target::kWordSize));
    mov(FP, SP);
  } else {
    addiu(SP, SP, Immediate(-4 * target::kWordSize));
    sw(RA, Address(SP, 3 * target::kWordSize));
    sw(FP, Address(SP, 2 * target::kWordSize));
    sw(CODE_REG, Address(SP, 1 * target::kWordSize));
    sw(PP, Address(SP, 0 * target::kWordSize));

    // Set FP to the saved previous FP.
    addiu(FP, SP, Immediate(2 * target::kWordSize));

    if (load_pool_pointer) LoadPoolPointer();
  }
  set_constant_pool_allowed(true);

  // Reserve space for locals.
  AddImmediate(SP, -frame_size);
}

void Assembler::LeaveDartFrame(RestorePP restore_pp) {
  ASSERT(!in_delay_slot_);
  addiu(SP, FP, Immediate(-2 * target::kWordSize));

  lw(RA, Address(SP, 3 * target::kWordSize));
  lw(FP, Address(SP, 2 * target::kWordSize));
  if (restore_pp == kRestoreCallerPP && !FLAG_precompiled_mode) {
    lw(PP, Address(SP, 0 * target::kWordSize));
  }
  set_constant_pool_allowed(false);

  // Adjust SP for PC, RA, FP, PP pushed in EnterDartFrame.
  addiu(SP, SP, Immediate(4 * target::kWordSize));
}

void Assembler::LeaveDartFrameAndReturn(Register ra) {
  ASSERT(!in_delay_slot_);
  addiu(SP, FP, Immediate(-2 * target::kWordSize));

  lw(RA, Address(SP, 3 * target::kWordSize));
  lw(FP, Address(SP, 2 * target::kWordSize));
  if (!FLAG_precompiled_mode) {
    lw(PP, Address(SP, 0 * target::kWordSize));
  }
  set_constant_pool_allowed(false);

  // Adjust SP for PC, RA, FP, PP pushed in EnterDartFrame, and return.
  addiu(SP, SP, Immediate(4 * target::kWordSize));
  jr(ra);
}

void Assembler::EnterFullSafepoint(Register addr, Register state) {
  // We generate the same number of instructions whether or not the slow-path is
  // forced. This simplifies GenerateJitCallbackTrampolines.
  Label slow_path, done, retry;
  if (FLAG_use_slow_path) {
    b(&slow_path);
  }

  AddImmediate(addr, THR, target::Thread::safepoint_state_offset());
  Bind(&retry);
  ll(state, Address(addr, 0));
  ASSERT(TMP!=addr);
  ASSERT(TMP!=state);
  LoadImmediate(TMP, target::Thread::native_safepoint_state_unacquired());
  BranchNotEqual(state, TMP, &slow_path);

  LoadImmediate(state, target::Thread::native_safepoint_state_acquired());
  sc(state, Address(addr, 0));
  BranchEqual(state, compiler::Immediate(1),
              &done);  // 1 means sc was successful.

  if (!FLAG_use_slow_path) {
    b(&retry);
  }

  Bind(&slow_path);
  lw(TMP, Address(THR, target::Thread::enter_safepoint_stub_offset()));
  lw(TMP, FieldAddress(TMP, target::Code::entry_point_offset()));
  mov(T9, TMP);
  jalr(T9);

  Bind(&done);
}

void Assembler::ExitFullSafepoint(Register addr,
                                  Register state,
                                  bool ignore_unwind_in_progress) {
  // We generate the same number of instructions whether or not the slow-path is
  // forced, for consistency with EnterFullSafepoint.
  Label slow_path, done, retry;
  if (FLAG_use_slow_path) {
    b(&slow_path);
  }

  AddImmediate(addr, THR, target::Thread::safepoint_state_offset());
  Bind(&retry);
  ll(state, Address(addr, 0));
  BranchNotEqual(
      state,
      compiler::Immediate(target::Thread::native_safepoint_state_acquired()),
      &slow_path);

  LoadImmediate(state, target::Thread::native_safepoint_state_unacquired());
  sc(state, Address(addr, 0));
  BranchEqual(state, compiler::Immediate(1),
              &done);  // 1 means sc was successful.

  if (!FLAG_use_slow_path) {
    b(&retry);
  }

  Bind(&slow_path);
  lw(TMP, Address(THR, target::Thread::exit_safepoint_stub_offset()));
  lw(T9, FieldAddress(TMP, target::Code::entry_point_offset()));
  jalr(T9);

  Bind(&done);
}

// A0 receiver, S5 ICData entries array
void Assembler::MonomorphicCheckedEntryJIT() {
  has_monomorphic_entry_ = true;
  bool saved_use_far_branches = use_far_branches();
  set_use_far_branches(false);

  const intptr_t start = CodeSize();

  Label miss;
  Bind(&miss);
  lw(T9, Address(THR, target::Thread::switchable_call_miss_entry_offset()));
  jr(T9);

  Comment("MonomorphicCheckedEntry");
  ASSERT_EQUAL(CodeSize() - start, target::Instructions::kMonomorphicEntryOffsetJIT);

  const intptr_t cid_offset = target::Array::element_offset(0);
  const intptr_t count_offset = target::Array::element_offset(1);

  lw(T1, FieldAddress(S5, cid_offset));
  LoadTaggedClassIdMayBeSmi(A1, A0);
  bne(T1, A1, &miss);

  lw(T1, FieldAddress(S5, count_offset));
  AddImmediate(T1, T1, target::ToRawSmi(1));
  sw(T1, FieldAddress(S5, count_offset));

  LoadImmediate(ARGS_DESC_REG, 0);  // GC-safe for OptimizeInvokedFunction

  // Fall through to unchecked entry.
  ASSERT_EQUAL(CodeSize() - start, target::Instructions::kPolymorphicEntryOffsetJIT);

  set_use_far_branches(saved_use_far_branches);
}

// A0 receiver, S5 guarded cid as Smi
void Assembler::MonomorphicCheckedEntryAOT() {
  has_monomorphic_entry_ = true;
  bool saved_use_far_branches = use_far_branches();
  set_use_far_branches(false);

  const intptr_t start = CodeSize();

  Label miss;
  Bind(&miss);
  lw(T9, Address(THR, target::Thread::switchable_call_miss_entry_offset()));
  jr(T9);

  Comment("MonomorphicCheckedEntry");
  ASSERT_EQUAL(CodeSize() - start, target::Instructions::kMonomorphicEntryOffsetAOT);
  LoadClassId(TMP, A0);
  SmiTag(TMP);
  bne(S5, TMP, &miss);

  // Fall through to unchecked entry.
  ASSERT_EQUAL(CodeSize() - start, target::Instructions::kPolymorphicEntryOffsetAOT);

  set_use_far_branches(saved_use_far_branches);
}

void Assembler::ReserveAlignedFrameSpace(intptr_t frame_space) {
  ASSERT(!in_delay_slot_);
  // Reserve space for arguments and align frame before entering
  // the C++ world.
  AddImmediate(SP, -frame_space);
  if (OS::ActivationFrameAlignment() > 1) {
    LoadImmediate(TMP, ~(OS::ActivationFrameAlignment() - 1));
    and_(SP, SP, TMP);
  }
}

void Assembler::EmitEntryFrameVerification(Register scratch) {
#if defined(DEBUG)
  Label done;
  ASSERT(!constant_pool_allowed());
  LoadImmediate(scratch, target::frame_layout.exit_link_slot_from_entry_fp *
                             target::kWordSize);
  addu(scratch, scratch, FPREG);
  BranchEqual(scratch, SPREG, &done);

  Breakpoint();

  Bind(&done);
#endif
}

void Assembler::PushObject(const Object& object) {
  ASSERT(IsOriginalObject(object));
  ASSERT(!in_delay_slot_);
  LoadObject(TMP, object);
  Push(TMP);
}

void Assembler::CompareObject(Register reg, const Object& object) {
  ASSERT(IsOriginalObject(object));
  if (IsSameObject(compiler::NullObject(), object)) {
    LoadObject(TMP, compiler::NullObject());
    CompareObjectRegisters(reg, TMP);
  } else if (target::IsSmi(object)) {
    CompareImmediate(reg, target::ToRawSmi(object), kObjectBytes);
  } else {
    LoadObject(TMP, object);
    CompareObjectRegisters(reg, TMP);
  }
}

}  // namespace compiler
}  // namespace dart

#endif  // defined TARGET_ARCH_MIPS

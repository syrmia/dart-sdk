// Copyright (c) 2026, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"  // Needed here to get TARGET_ARCH_MIPS.
#if defined(TARGET_ARCH_MIPS)

#include "vm/instructions.h"
#include "vm/instructions_mips.h"
#include "vm/object_store.h"
#include "vm/object.h"
namespace dart {

static bool IsBranchLinkScratch(Register reg) {
  // See Assembler::BranchLink and Assembler::BranchLinkWithEquivalence
  return FLAG_precompiled_mode ? reg == TMP : reg == CODE_REG;
}

CallPattern::CallPattern(uword pc, const Code& code)
    : object_pool_(ObjectPool::Handle(code.GetObjectPool())),
      target_code_pool_index_(-1) {
  ASSERT(code.ContainsInstructionAt(pc));
  // Last instruction: jalr RA, T9(=R25).
  ASSERT(*(reinterpret_cast<uword*>(pc) - 2) == 0x0320f809);
  Register reg;
  // The end of the pattern is the instruction after the delay slot of the jalr.
  InstructionPattern::DecodeLoadWordFromPool(pc - (3 * Instr::kInstrSize), &reg,
                                             &target_code_pool_index_);
  ASSERT(IsBranchLinkScratch(reg));
}

ICCallPattern::ICCallPattern(uword pc, const Code& code)
    : object_pool_(ObjectPool::Handle(code.GetObjectPool())),
      target_pool_index_(-1),
      data_pool_index_(-1) {
  ASSERT(code.ContainsInstructionAt(pc));
  // Last instruction: jalr RA, T9(=R25).
  ASSERT(*(reinterpret_cast<uword*>(pc) - 2) == 0x0320f809);
  Register reg;
  // The end of the pattern is the instruction after the delay slot of the jalr.
  uword data_load_end = InstructionPattern::DecodeLoadWordFromPool(
      pc - (3 * Instr::kInstrSize), &reg, &target_pool_index_);

  ASSERT(IsBranchLinkScratch(reg));

  InstructionPattern::DecodeLoadWordFromPool(data_load_end, &reg,
                                             &data_pool_index_);

  ASSERT(reg == ICREG);
}

NativeCallPattern::NativeCallPattern(uword pc, const Code& code)
    : object_pool_(ObjectPool::Handle(code.GetObjectPool())),
      end_(pc),
      native_function_pool_index_(-1),
      target_code_pool_index_(-1) {
  ASSERT(code.ContainsInstructionAt(pc));
  // Last instruction: jalr RA, T9(=R25).
  ASSERT(*(reinterpret_cast<uword*>(end_) - 2) == 0x0320f809);

  Register reg;
  uword native_function_load_end = InstructionPattern::DecodeLoadWordFromPool(
      end_ - 3 * Instr::kInstrSize, &reg, &target_code_pool_index_);
  ASSERT(IsBranchLinkScratch(reg));
  InstructionPattern::DecodeLoadWordFromPool(native_function_load_end, &reg,
                                             &native_function_pool_index_);
  ASSERT(reg == T5);
}

// Decodes a load sequence ending at 'end' (the last instruction of the load
// sequence is the instruction before the one at end).  Returns a pointer to
// the first instruction in the sequence.  Returns the register being loaded
// and the loaded immediate value in the output parameters 'reg' and 'value'
// respectively.
uword InstructionPattern::DecodeLoadWordImmediate(uword end,
                                                  Register* reg,
                                                  intptr_t* value) {
  // The pattern is a fixed size, but match backwards for uniformity with
  // DecodeLoadWordFromPool.
  uword start = end - Instr::kInstrSize;
  Instr* instr = Instr::At(start);
  intptr_t imm = 0;
  ASSERT(instr->OpcodeField() == ORI);
  imm = instr->UImmField();
  *reg = instr->RtField();

  start -= Instr::kInstrSize;
  instr = Instr::At(start);
  ASSERT(instr->OpcodeField() == LUI);
  ASSERT(instr->RtField() == *reg);
  imm |= (instr->UImmField() << 16);
  *value = imm;
  return start;
}

void InstructionPattern::EncodeLoadWordImmediate(uword end,
                                                 Register reg,
                                                 intptr_t value) {
  uint16_t low16 = value & 0xffff;
  uint16_t high16 = (value >> 16) & 0xffff;

  // lui reg, #imm_hi
  uint32_t lui_instr =
      LUI << kOpcodeShift | R0 << kRsShift | reg << kRtShift | high16;

  // ori reg, reg, #imm_lo
  uint32_t ori_instr =
      ORI << kOpcodeShift | reg << kRsShift | reg << kRtShift | low16;

  uint32_t* cursor = reinterpret_cast<uint32_t*>(end);
  *(--cursor) = ori_instr;
  *(--cursor) = lui_instr;

#if defined(DEBUG)
  Register decoded_reg;
  intptr_t decoded_value;
  DecodeLoadWordImmediate(end, &decoded_reg, &decoded_value);
  ASSERT(reg == decoded_reg);
  ASSERT(value == decoded_value);
#endif
}

// Decodes a load sequence ending at 'end' (the last instruction of the load
// sequence is the instruction before the one at end).  Returns a pointer to
// the first instruction in the sequence.  Returns the register being loaded
// and the index in the pool being read from in the output parameters 'reg'
// and 'index' respectively.
uword InstructionPattern::DecodeLoadWordFromPool(uword end,
                                                 Register* reg,
                                                 intptr_t* index) {
  uword start = end - Instr::kInstrSize;
  Instr* instr = Instr::At(start);
  intptr_t offset = 0;
  if ((instr->OpcodeField() == LW) && (instr->RsField() == PP)) {
    offset = instr->SImmField();
    *reg = instr->RtField();
  } else {
    ASSERT(instr->OpcodeField() == LW);
    offset = instr->SImmField();
    *reg = instr->RtField();

    start -= Instr::kInstrSize;
    instr = Instr::At(start);
    ASSERT(instr->OpcodeField() == SPECIAL);
    ASSERT(instr->FunctionField() == ADDU);
    ASSERT(instr->RdField() == *reg);
    ASSERT(instr->RsField() == *reg);
    ASSERT(instr->RtField() == PP);

    start -= Instr::kInstrSize;
    instr = Instr::At(start);
    ASSERT(instr->OpcodeField() == LUI);
    ASSERT(instr->RtField() == *reg);
    // Offset is signed, so add the upper 16 bits.
    offset += (instr->UImmField() << 16);
  }
  *index = ObjectPool::IndexFromOffset(offset);
  return start;
}

bool DecodeLoadObjectFromPoolOrThread(uword pc, const Code& code, Object* obj) {
  ASSERT(code.ContainsInstructionAt(pc));

  Instr* instr = Instr::At(pc);
  if ((instr->OpcodeField() == LW)) {
    intptr_t offset = instr->SImmField();
    if (instr->RsField() == PP) {
      intptr_t index = ObjectPool::IndexFromOffset(offset);
      return ObjectAtPoolIndex(code, index, obj);
    } else if (instr->RsField() == THR) {
      return Thread::ObjectAtOffset(offset, obj);
    }
  }
  return false;
}

CodePtr CallPattern::TargetCode() const {
  return static_cast<CodePtr>(object_pool_.ObjectAt<std::memory_order_acquire>(
      target_code_pool_index_));
}

void CallPattern::SetTargetCode(const Code& target) const {
  object_pool_.SetObjectAt<std::memory_order_release>(target_code_pool_index_,
                                                      target);
}

ObjectPtr ICCallPattern::Data() const {
  return object_pool_.ObjectAt<std::memory_order_acquire>(data_pool_index_);
}

void ICCallPattern::SetData(const Object& data) const {
  ASSERT(data.IsArray() || data.IsICData() || data.IsMegamorphicCache());
  object_pool_.SetObjectAt<std::memory_order_release>(data_pool_index_, data);
}

CodePtr ICCallPattern::TargetCode() const {
  return static_cast<CodePtr>(
      object_pool_.ObjectAt<std::memory_order_acquire>(target_pool_index_));
}

void ICCallPattern::SetTargetCode(const Code& target_code) const {
  object_pool_.SetObjectAt<std::memory_order_release>(target_pool_index_,
                                                      target_code);
}

CodePtr NativeCallPattern::target() const {
  return static_cast<CodePtr>(object_pool_.ObjectAt<std::memory_order_acquire>(
      target_code_pool_index_));
}

void NativeCallPattern::set_target(const Code& target) const {
  object_pool_.SetObjectAt<std::memory_order_release>(target_code_pool_index_,
                                                      target);
  // No need to flush the instruction cache, since the code is not modified.
}

NativeFunction NativeCallPattern::native_function() const {
  return reinterpret_cast<NativeFunction>(
      object_pool_.RawValueAt(native_function_pool_index_));
}

void NativeCallPattern::set_native_function(NativeFunction func) const {
  object_pool_.SetRawValueAt<std::memory_order_relaxed>(
      native_function_pool_index_, reinterpret_cast<uword>(func));
}

SwitchableCallPatternBase::SwitchableCallPatternBase(
    const ObjectPool& object_pool)
    : object_pool_(object_pool), data_pool_index_(-1), target_pool_index_(-1) {}

ObjectPtr SwitchableCallPatternBase::data() const {
  return object_pool_.ObjectAt<std::memory_order_acquire>(data_pool_index_);
}

void SwitchableCallPatternBase::SetDataRelease(const Object& data) const {
  ASSERT(!Object::Handle(object_pool_.ObjectAt<std::memory_order_relaxed>(
                             data_pool_index_))
              .IsCode());
  object_pool_.SetObjectAt<std::memory_order_release>(data_pool_index_, data);
}

SwitchableCallPattern::SwitchableCallPattern(uword pc, const Code& code)
    : SwitchableCallPatternBase(ObjectPool::Handle(code.GetObjectPool())) {
  ASSERT(code.ContainsInstructionAt(pc));
  // Last instruction: jalr t9.
  ASSERT(*(reinterpret_cast<uword*>(pc) - 1) == 0);  // Delay slot.
  ASSERT(*(reinterpret_cast<uword*>(pc) - 2) == 0x0320f809);

  Register reg;
  uword data_load_end = InstructionPattern::DecodeLoadWordFromPool(
      pc - 2 * Instr::kInstrSize, &reg, &data_pool_index_);
  ASSERT(reg == S5);
  InstructionPattern::DecodeLoadWordFromPool(data_load_end, &reg,
                                             &target_pool_index_);
  ASSERT(reg == T9);
}

ObjectPtr SwitchableCallPattern::target() const {
  return object_pool_.ObjectAt<std::memory_order_acquire>(target_pool_index_);
}

void SwitchableCallPattern::SetTargetRelease(const Code& target) const {
  ASSERT(Object::Handle(object_pool_.ObjectAt<std::memory_order_relaxed>(
                            target_pool_index_))
             .IsCode());
  object_pool_.SetObjectAt<std::memory_order_release>(target_pool_index_,
                                                      target);
}

BareSwitchableCallPattern::BareSwitchableCallPattern(uword pc)
    : SwitchableCallPatternBase(ObjectPool::Handle(
          IsolateGroup::Current()->object_store()->global_object_pool())) {
  // Last instruction: jalr t9.
  ASSERT(*(reinterpret_cast<uword*>(pc) - 1) == 0);  // Delay slot.
  ASSERT(*(reinterpret_cast<uword*>(pc) - 2) == 0x0320f809);

  Register reg;
  uword data_load_end = InstructionPattern::DecodeLoadWordFromPool(
      pc - 2 * Instr::kInstrSize, &reg, &data_pool_index_);
  ASSERT(reg == S5);
  InstructionPattern::DecodeLoadWordFromPool(data_load_end, &reg,
                                             &target_pool_index_);
  ASSERT(reg == T9);
}

uword BareSwitchableCallPattern::target_entry() const {
  return object_pool_.RawValueAt<std::memory_order_relaxed>(target_pool_index_);
}

void BareSwitchableCallPattern::SetTargetRelease(const Code& target) const {
  ASSERT(object_pool_.TypeAt(target_pool_index_) ==
         ObjectPool::EntryType::kImmediate);
  object_pool_.SetRawValueAt<std::memory_order_release>(
      target_pool_index_, target.MonomorphicEntryPoint());
}

ReturnPattern::ReturnPattern(uword pc) : pc_(pc) {}

bool ReturnPattern::IsValid() const {
  Instr* jr = Instr::At(pc_);
  return (jr->OpcodeField() == SPECIAL) && (jr->FunctionField() == JR) &&
         (jr->RsField() == RA);
}

}  // namespace dart

#endif  // defined TARGET_ARCH_MIPS

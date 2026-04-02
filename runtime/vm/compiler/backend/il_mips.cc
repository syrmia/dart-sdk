// Copyright (c) 2026, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"  // Needed here to get TARGET_ARCH_MIPS.
#if defined(TARGET_ARCH_MIPS)

#include "vm/compiler/backend/il.h"

#include "vm/compiler/backend/flow_graph.h"
#include "vm/compiler/backend/flow_graph_compiler.h"
#include "vm/compiler/backend/locations.h"
#include "vm/compiler/backend/locations_helpers.h"
#include "vm/compiler/backend/range_analysis.h"
#include "vm/compiler/jit/compiler.h"
#include "vm/object_store.h"
#include "vm/type_testing_stubs.h"

#define __ compiler->assembler()->
#define Z (compiler->zone())

namespace dart {

// Generic summary for call instructions that have all arguments pushed
// on the stack and return the result in a fixed register V0.
LocationSummary* Instruction::MakeCallSummary(Zone* zone,
                                              const Instruction* instr,
                                              LocationSummary* locs) {
  ASSERT(locs == nullptr || locs->always_calls());
  LocationSummary* result =
      ((locs == nullptr)
           ? (new (zone) LocationSummary(zone, 0, 0, LocationSummary::kCall))
           : locs);
  const auto representation = instr->representation();
  switch (representation) {
    case kTagged:
    case kUntagged:
    case kUnboxedUint32:
    case kUnboxedInt32:
      result->set_out(
          0, Location::RegisterLocation(CallingConventions::kReturnReg));
      break;
    case kPairOfTagged:
    case kUnboxedInt64:
      result->set_out(
          0, Location::Pair(
                 Location::RegisterLocation(CallingConventions::kReturnReg),
                 Location::RegisterLocation(
                     CallingConventions::kSecondReturnReg)));
      break;
    case kUnboxedDouble:
      result->set_out(
          0, Location::FpuRegisterLocation(CallingConventions::kReturnFpuReg));
      break;
    default:
      UNREACHABLE();
      break;
  }
  return result;
}

DEFINE_BACKEND(TailCall,
               (NoLocation,
                Fixed<Register, ARGS_DESC_REG>,
                Temp<Register> temp)) {
  compiler->EmitTailCallToStub(instr->code());

  // Even though the TailCallInstr will be the last instruction in a basic
  // block, the flow graph compiler will emit native code for other blocks after
  // the one containing this instruction and needs to be able to use the pool.
  // (The `LeaveDartFrame` above disables usages of the pool.)
  __ set_constant_pool_allowed(true);
}

LocationSummary* MemoryCopyInstr::MakeLocationSummary(Zone* zone,
                                                      bool opt) const {
  // The compiler must optimize any function that includes a MemoryCopy
  // instruction that uses typed data cids, since extracting the payload address
  // from views is done in a compiler pass after all code motion has happened.
  ASSERT((!IsTypedDataBaseClassId(src_cid_) &&
          !IsTypedDataBaseClassId(dest_cid_)) ||
         opt);
  const intptr_t kNumInputs = 5;
  const intptr_t kNumTemps = 3;
  LocationSummary* locs = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  locs->set_in(kSrcPos, Location::RequiresRegister());
  locs->set_in(kDestPos, Location::RequiresRegister());
  locs->set_in(kSrcStartPos, LocationRegisterOrConstant(src_start()));
  locs->set_in(kDestStartPos, LocationRegisterOrConstant(dest_start()));
  locs->set_in(kLengthPos,
               LocationWritableRegisterOrSmiConstant(length(), 0, 4));
  locs->set_temp(0, Location::RequiresRegister());
  locs->set_temp(1, Location::RequiresRegister());
  locs->set_temp(2, Location::RequiresRegister());
  return locs;
}

static compiler::OperandSize OperandSizeFor(intptr_t bytes) {
  ASSERT(Utils::IsPowerOfTwo(bytes));
  switch (bytes) {
    case 1:
      return compiler::kUnsignedByte;
    case 2:
      return compiler::kUnsignedTwoBytes;
    case 4:
      return compiler::kUnsignedFourBytes;
    case 8:
      return compiler::kEightBytes;
    default:
      UNREACHABLE();
      return compiler::kEightBytes;
  }
}

void MemoryCopyInstr::PrepareLengthRegForLoop(FlowGraphCompiler* compiler,
                                              Register length_reg,
                                              compiler::Label* done) {
  __ BranchEqual(length_reg, ZR, done);
}

// Copies [count] bytes from the memory region pointed to by [dest_reg] to the
// memory region pointed to by [src_reg]. If [reversed] is true, then [dest_reg]
// and [src_reg] are assumed to point at the end of the respective region.
static void CopyBytes(FlowGraphCompiler* compiler,
                      Register dest_reg,
                      Register src_reg,
                      intptr_t count,
                      bool reversed,
                      Register tmp) {
  ASSERT(Utils::IsPowerOfTwo(count));

  ASSERT(dest_reg != TMP);
  ASSERT(src_reg != TMP);

  if (count == 4 * compiler::target::kWordSize) {
    auto const sz = OperandSizeFor(compiler::target::kWordSize);
    const intptr_t offset = (reversed ? -1 : 1) * (compiler::target::kWordSize);
    const intptr_t initial = reversed ? offset : 0;
    __ LoadFromOffset(TMP, src_reg, initial, sz);
    __ LoadFromOffset(tmp, src_reg, initial + offset, sz);
    __ StoreToOffset(TMP, dest_reg, initial, sz);
    __ StoreToOffset(tmp, dest_reg, initial + offset, sz);
    __ LoadFromOffset(TMP, src_reg, initial + 2 * offset, sz);
    __ LoadFromOffset(tmp, src_reg, initial + 3 * offset, sz);
    __ StoreToOffset(TMP, dest_reg, initial + 2 * offset, sz);
    __ StoreToOffset(tmp, dest_reg, initial + 3 * offset, sz);
    __ AddImmediate(src_reg, src_reg, 4 * offset);
    __ AddImmediate(dest_reg, dest_reg, 4 * offset);
    return;
  }

  if (count == 2 * (compiler::target::kWordSize)) {
    auto const sz = OperandSizeFor(compiler::target::kWordSize);
    const intptr_t offset = (reversed ? -1 : 1) * (compiler::target::kWordSize);
    const intptr_t initial = reversed ? offset : 0;
    __ LoadFromOffset(TMP, src_reg, initial, sz);
    __ LoadFromOffset(tmp, src_reg, initial + offset, sz);
    __ StoreToOffset(TMP, dest_reg, initial, sz);
    __ StoreToOffset(tmp, dest_reg, initial + offset, sz);
    __ AddImmediate(src_reg, src_reg, 2 * offset);
    __ AddImmediate(dest_reg, dest_reg, 2 * offset);
    return;
  }

  auto const sz = OperandSizeFor(count);
  const intptr_t offset = (reversed ? -1 : 1) * count;
  const intptr_t initial = reversed ? offset : 0;
  __ LoadFromOffset(TMP, src_reg, initial, sz);
  __ StoreToOffset(TMP, dest_reg, initial, sz);
  __ AddImmediate(src_reg, src_reg, offset);
  __ AddImmediate(dest_reg, dest_reg, offset);
}

static void CopyUpToWordMultiple(FlowGraphCompiler* compiler,
                                 Register dest_reg,
                                 Register src_reg,
                                 Register length_reg,
                                 intptr_t element_size,
                                 bool unboxed_inputs,
                                 bool reversed,
                                 compiler::Label* done,
                                 Register temp) {
  ASSERT(Utils::IsPowerOfTwo(element_size));
  if (element_size >= compiler::target::kWordSize) return;

  const intptr_t element_shift = Utils::ShiftForPowerOfTwo(element_size);
  const intptr_t base_shift =
      (unboxed_inputs ? 0 : kSmiTagShift) - element_shift;
  intptr_t tested_bits = 0;

  __ Comment("Copying until region is a multiple of word size");

  for (intptr_t bit = compiler::target::kWordSizeLog2 - 1; bit >= element_shift;
       bit--) {
    const intptr_t bytes = 1 << bit;
    const intptr_t tested_bit = bit + base_shift;
    tested_bits |= (1 << tested_bit);
    compiler::Label skip_copy;
    __ AndImmediate(TMP, length_reg, 1 << tested_bit);
    __ BranchEqual(TMP, ZR, &skip_copy);
    for (intptr_t j = 0; j < bytes; j++) {
      CopyBytes(compiler, dest_reg, src_reg, 1, reversed, temp);
    }
    __ Bind(&skip_copy);
  }

  ASSERT(tested_bits != 0);
  __ AndImmediate(length_reg, length_reg, ~tested_bits);
  __ BranchEqual(length_reg, ZR, done);
}

void MemoryCopyInstr::EmitLoopCopy(FlowGraphCompiler* compiler,
                                   Register dest_reg,
                                   Register src_reg,
                                   Register length_reg,
                                   compiler::Label* done,
                                   compiler::Label* copy_forwards) {
  const bool reversed = copy_forwards != nullptr;
  const Register temp = locs()->temp(2).reg();
  if (reversed) {
    // Verify that the overlap actually exists by checking to see if the start
    // of the destination region is after the end of the source region.
    const intptr_t shift = Utils::ShiftForPowerOfTwo(element_size_) -
                           (unboxed_inputs() ? 0 : kSmiTagShift);

    if (shift == 0) {
      __ addu(TMP, src_reg, length_reg);
    } else if (shift < 0) {
      __ sra(TMP, length_reg, -shift);
      __ addu(TMP, src_reg, TMP);
    } else {
      __ sll(TMP, length_reg, shift);
      __ addu(TMP, src_reg, TMP);
    }
    __ BranchUnsignedGreaterEqual(dest_reg, TMP, copy_forwards);
    // Adjust dest_reg and src_reg to point at the end (i.e. one past the
    // last element) of their respective region.
    __ addu(dest_reg, dest_reg, TMP);
    __ subu(dest_reg, dest_reg, src_reg);
    __ MoveRegister(src_reg, TMP);
  }
  CopyUpToWordMultiple(compiler, dest_reg, src_reg, length_reg, element_size_,
                       unboxed_inputs_, reversed, done, temp);
  // The size of the uncopied region is a multiple of the word size, so now we
  // copy the rest by word (unless the element size is larger).
  const intptr_t loop_subtract =
      Utils::Maximum<intptr_t>(1, compiler::target::kWordSize / element_size_)
      << (unboxed_inputs_ ? 0 : kSmiTagShift);
  __ Comment("Copying by multiples of word size");
  compiler::Label loop;
  __ Bind(&loop);
  switch (element_size_) {
    // Fall through for the sizes smaller than compiler::target::kWordSize.
    case 1:
      CopyBytes(compiler, dest_reg, src_reg, 1, reversed, temp);
      CopyBytes(compiler, dest_reg, src_reg, 1, reversed, temp);
      CopyBytes(compiler, dest_reg, src_reg, 1, reversed, temp);
      CopyBytes(compiler, dest_reg, src_reg, 1, reversed, temp);
      break;
    case 2:
      CopyBytes(compiler, dest_reg, src_reg, 2, reversed, temp);
      CopyBytes(compiler, dest_reg, src_reg, 2, reversed, temp);
      break;
    case 4:
      CopyBytes(compiler, dest_reg, src_reg, 4, reversed, temp);
      break;
    case 8:
      CopyBytes(compiler, dest_reg, src_reg, 8, reversed, temp);
      break;
    case 16:
      CopyBytes(compiler, dest_reg, src_reg, 16, reversed, temp);
      break;
    default:
      UNREACHABLE();
      break;
  }
  __ AddImmediate(length_reg, length_reg, -loop_subtract);
  __ BranchNotEqual(length_reg, ZR, &loop);
}

void MemoryCopyInstr::EmitComputeStartPointer(FlowGraphCompiler* compiler,
                                              classid_t array_cid,
                                              Register array_reg,
                                              Register payload_reg,
                                              Representation array_rep,
                                              Location start_loc) {
  intptr_t offset = 0;
  if (array_rep != kTagged) {
    // Do nothing, array_reg already contains the payload address.
  } else if (IsTypedDataBaseClassId(array_cid)) {
    // The incoming array must have been proven to be an internal typed data
    // object, where the payload is in the object and we can just offset.
    ASSERT_EQUAL(array_rep, kTagged);
    offset = compiler::target::TypedData::payload_offset() - kHeapObjectTag;
  } else {
    ASSERT_EQUAL(array_rep, kTagged);
    ASSERT(!IsExternalPayloadClassId(array_cid));
    switch (array_cid) {
      case kOneByteStringCid:
        offset =
            compiler::target::OneByteString::data_offset() - kHeapObjectTag;
        break;
      case kTwoByteStringCid:
        offset =
            compiler::target::TwoByteString::data_offset() - kHeapObjectTag;
        break;
      default:
        UNREACHABLE();
        break;
    }
  }
  ASSERT(start_loc.IsRegister() || start_loc.IsConstant());
  if (start_loc.IsConstant()) {
    const auto& constant = start_loc.constant();
    ASSERT(constant.IsInteger());
    const int64_t start_value = Integer::Cast(constant).Value();
    const intptr_t add_value = Utils::AddWithWrapAround(
        Utils::MulWithWrapAround<intptr_t>(start_value, element_size_), offset);
    __ AddImmediate(payload_reg, array_reg, add_value);
    return;
  }
  const Register start_reg = start_loc.reg();
  intptr_t shift = Utils::ShiftForPowerOfTwo(element_size_) -
                   (unboxed_inputs() ? 0 : kSmiTagShift);

  __ AddShifted(payload_reg, array_reg, start_reg, shift);
  __ AddImmediate(payload_reg, offset);
}

LocationSummary* MoveArgumentInstr::MakeLocationSummary(Zone* zone,
                                                        bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* locs = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  ConstantInstr* constant = value()->definition()->AsConstant();
  if (constant != nullptr && constant->HasZeroRepresentation()) {
    locs->set_in(0, Location::Constant(constant));
  } else if (representation() == kUnboxedDouble) {
    locs->set_in(0, Location::RequiresFpuRegister());
  } else if (representation() == kUnboxedInt64) {
    locs->set_in(0, Location::Pair(Location::RequiresRegister(),
                                    Location::RequiresRegister()));
  } else {
    ASSERT(representation() == kTagged);
    locs->set_in(0, LocationAnyOrConstant(value()));
  }
  return locs;
}

void MoveArgumentInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  __ Comment("PushArgumentInstr");
  ASSERT(compiler->is_optimizing());

  const Location value = locs()->in(0);

  if (value.IsRegister()) {
    __ StoreToOffset(value.reg(), SP,
                     location().stack_index() * compiler::target::kWordSize);
  } else if (value.IsPairLocation()) {
    auto pair = location().AsPairLocation();
    RELEASE_ASSERT(pair->At(0).IsStackSlot());
    RELEASE_ASSERT(pair->At(1).IsStackSlot());
    __ StoreToOffset(value.AsPairLocation()->At(1).reg(), SP,
                     location().AsPairLocation()->At(1).stack_index() *
                         compiler::target::kWordSize);
    __ StoreToOffset(value.AsPairLocation()->At(0).reg(), SP,
                     location().AsPairLocation()->At(0).stack_index() *
                         compiler::target::kWordSize);
  } else if (value.IsConstant()) {
    if (representation() == kUnboxedDouble) {
      ASSERT(value.constant_instruction()->HasZeroRepresentation());
      intptr_t offset = location().stack_index() * compiler::target::kWordSize;
      __ StoreToOffset(ZR, SP, offset + compiler::target::kWordSize);
      __ StoreToOffset(ZR, SP, offset);
    } else if (representation() == kUnboxedInt64) {
      ASSERT(value.constant_instruction()->HasZeroRepresentation());
      __ StoreToOffset(ZR, SP,
                       location().AsPairLocation()->At(1).stack_index() *
                           compiler::target::kWordSize);
      __ StoreToOffset(ZR, SP,
                       location().AsPairLocation()->At(0).stack_index() *
                           compiler::target::kWordSize);
    } else {
      ASSERT(representation() == kTagged);
      const Object& constant = value.constant();
      Register reg;
      if (constant.IsNull()) {
        reg = TMP;
        __ LoadObject(TMP, compiler::NullObject());
      } else if (constant.IsSmi() && Smi::Cast(constant).Value() == 0) {
        reg = ZR;
      } else {
        reg = TMP;
        __ LoadObject(TMP, constant);
      }
      __ StoreToOffset(reg, SP,
                       location().stack_index() * compiler::target::kWordSize);
    }
  } else if (value.IsFpuRegister()) {
    __ StoreDToOffset(value.fpu_reg(), SP,
                      location().stack_index() * compiler::target::kWordSize);
  } else if (value.IsStackSlot()) {
    const intptr_t value_offset = value.ToStackSlotOffset();
    __ LoadFromOffset(TMP, value.base_reg(), value_offset);
    __ StoreToOffset(TMP, SP,
                     location().stack_index() * compiler::target::kWordSize);
  } else {
    UNREACHABLE();
  }
}

LocationSummary* DartReturnInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* locs = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  switch (representation()) {
    case kTagged:
      locs->set_in(0,
                   Location::RegisterLocation(CallingConventions::kReturnReg));
      break;
    case kPairOfTagged:
    case kUnboxedInt64:
      locs->set_in(
          0, Location::Pair(
                 Location::RegisterLocation(CallingConventions::kReturnReg),
                 Location::RegisterLocation(
                     CallingConventions::kSecondReturnReg)));
      break;
    case kUnboxedDouble:
      locs->set_in(
          0, Location::FpuRegisterLocation(CallingConventions::kReturnFpuReg));
      break;
    default:
      UNREACHABLE();
      break;
  }
  return locs;
}

// Attempt optimized compilation at return instruction instead of at the entry.
// The entry needs to be patchable, no inlined objects are allowed in the area
// that will be overwritten by the patch instructions: a branch macro sequence.
void DartReturnInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  if (locs()->in(0).IsRegister()) {
    const Register result = locs()->in(0).reg();
    ASSERT(result == CallingConventions::kReturnReg);
  } else if (locs()->in(0).IsPairLocation()) {
    const Register result_lo = locs()->in(0).AsPairLocation()->At(0).reg();
    const Register result_hi = locs()->in(0).AsPairLocation()->At(1).reg();
    ASSERT(result_lo == CallingConventions::kReturnReg);
    ASSERT(result_hi == CallingConventions::kSecondReturnReg);
  } else {
    ASSERT(locs()->in(0).IsFpuRegister());
    const FpuRegister result = locs()->in(0).fpu_reg();
    ASSERT(result == CallingConventions::kReturnFpuReg);
  }

  if (compiler->parsed_function().function().IsAsyncFunction() ||
      compiler->parsed_function().function().IsAsyncGenerator()) {
    ASSERT(compiler->flow_graph().graph_entry()->NeedsFrame());
    const Code& stub = GetReturnStub(compiler);
    compiler->EmitJumpToStub(stub);
    return;
  }

  if (!compiler->flow_graph().graph_entry()->NeedsFrame()) {
    __ Ret();
    return;
  }

#if defined(DEBUG)
  compiler::Label stack_ok;
  __ Comment("Stack Check");
  const intptr_t fp_sp_dist =
      (compiler::target::frame_layout.first_local_from_fp + 1 -
       compiler->StackSize()) *
      compiler::target::kWordSize;
  ASSERT(fp_sp_dist <= 0);
  __ subu(CMPRES1, SP, FP);
  __ BranchEqual(CMPRES1, compiler::Immediate(fp_sp_dist), &stack_ok);
  __ Breakpoint();
  __ Bind(&stack_ok);
#endif
  ASSERT(__ constant_pool_allowed());
  __ LeaveDartFrameAndReturn();  // Disallows constant pool use.
  // This DartReturnInstr may be emitted out of order by the optimizer. The next
  // block may be a target expecting a properly set constant pool pointer.
  __ set_constant_pool_allowed(true);
}

LocationSummary* IfThenElseInstr::MakeLocationSummary(Zone* zone,
                                                      bool opt) const {
  condition()->InitializeLocationSummary(zone, opt);
  return condition()->locs();
}

void IfThenElseInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  const Register result = locs()->out(0).reg();

  intptr_t true_value = if_true_;
  intptr_t false_value = if_false_;
  bool swapped = false;
  if (true_value == 0) {
    // Swap values so that false_value is zero.
    intptr_t temp = true_value;
    true_value = false_value;
    false_value = temp;
    swapped = true;
  }

  // Initialize result with the true value.
  __ LoadImmediate(result, Smi::RawValue(true_value));

  // Emit comparison code. This must not overwrite the result register.
  // IfThenElseInstr::Supports() should prevent EmitConditionCode from using
  // the labels or returning an invalid condition.
  BranchLabels labels = {NULL, NULL, NULL};  // Emit branch-free code.
  Condition true_condition = condition()->EmitConditionCode(compiler, labels);
  ASSERT(true_condition != kInvalidCondition);
  if (swapped) {
    true_condition = InvertCondition(true_condition);
  }

  // Evaluate condition and provide result in CMPRES1.
  __ SetIf(true_condition, CMPRES1);

  // CMPRES1 is the evaluated condition, zero or non-zero, as specified by the
  // flag zero_is_false.
  Register false_value_reg;
  if (false_value == 0) {
    false_value_reg = ZR;
  } else {
    __ LoadImmediate(CMPRES2, Smi::RawValue(false_value));
    false_value_reg = CMPRES2;
  }
  __ movz(result, false_value_reg, CMPRES1);
}

LocationSummary* ClosureCallInstr::MakeLocationSummary(Zone* zone,
                                                       bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kCall);
  summary->set_in(0,
                  Location::RegisterLocation(
                      FLAG_precompiled_mode ? T0 : FUNCTION_REG));  // Function.
  return MakeCallSummary(zone, this, summary);
}


void ClosureCallInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  // Load arguments descriptor in S4.
  const intptr_t argument_count = ArgumentCount();  // Includes type args.
  const Array& arguments_descriptor =
      Array::ZoneHandle(Z, GetArgumentsDescriptor());
  __ LoadObject(S4, arguments_descriptor);

  if (FLAG_precompiled_mode) {
    ASSERT(locs()->in(0).reg() == T0);
    // T0: Closure with a cached entry point.
    __ LoadFieldFromOffset(T2, T0,
                           compiler::target::Closure::entry_point_offset());
#if defined(DART_DYNAMIC_MODULES)
    ASSERT(FUNCTION_REG != T2);
    __ LoadFieldFromOffset(
        FUNCTION_REG, T0, compiler::target::Closure::function_offset());
#endif
  } else {
    // Load closure function code in T2.
    // S4: arguments descriptor array.
    // S5: Smi 0 (no IC data; the lazy-compile stub expects a GC-safe value).
    ASSERT(locs()->in(0).reg() == FUNCTION_REG);
    __ LoadImmediate(S5, 0);
    __ lw(T2, compiler::FieldAddress(FUNCTION_REG, compiler::target::Function::entry_point_offset()));
    __ lw(CODE_REG, compiler::FieldAddress(FUNCTION_REG, compiler::target::Function::code_offset()));
  }
  __ mov(T9, T2);
  __ jalr(T9);

  compiler->EmitCallsiteMetadata(source(), deopt_id(),
                                 UntaggedPcDescriptors::kOther, locs(), env());
  compiler->EmitDropArguments(argument_count);
}

LocationSummary* ConstantInstr::MakeLocationSummary(Zone* zone,
                                                    bool opt) const {
  return LocationSummary::Make(zone, 0, Location::RequiresRegister(),
                               LocationSummary::kNoCall);
}

void ConstantInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  // The register allocator drops constant definitions that have no uses.
  if (!locs()->out(0).IsInvalid()) {
    __ Comment("ConstantInstr");
    const Register result = locs()->out(0).reg();
    __ LoadObject(result, value());
  }
}

void ConstantInstr::EmitMoveToLocation(FlowGraphCompiler* compiler,
                                       const Location& destination,
                                       Register tmp,
                                       intptr_t pair_index) {
  if (destination.IsRegister()) {
    if (RepresentationUtils::IsUnboxedInteger(representation())) {
      int64_t v;
      const bool ok = compiler::HasIntegerValue(value_, &v);
      RELEASE_ASSERT(ok);
      if (value_.IsSmi() &&
          RepresentationUtils::IsUnsignedInteger(representation())) {
        // If the value is negative, then the sign bit was preserved during
        // Smi untagging, which means the resulting value may be unexpected.
        ASSERT(v >= 0);
      }
      __ LoadImmediate(destination.reg(), pair_index == 0
                                              ? Utils::Low32Bits(v)
                                              : Utils::High32Bits(v));
    } else {
      ASSERT(representation() == kTagged);
      __ LoadObject(destination.reg(), value_);
    }
  } else if (destination.IsFpuRegister()) {
    const DRegister dst = destination.fpu_reg();
    if (representation() == kUnboxedFloat) {
      __ LoadSImmediate(dst, Double::Cast(value_).value());
    } else {
      ASSERT(representation() == kUnboxedDouble);
      ASSERT(tmp != kNoRegister);
      __ LoadDImmediate(dst, Double::Cast(value_).value(), tmp);
    }
  } else if (destination.IsDoubleStackSlot()) {
    ASSERT(tmp != kNoRegister);
    const intptr_t dest_offset = destination.ToStackSlotOffset();
    __ LoadDImmediate(DTMP, Double::Cast(value_).value(), tmp);
    __ StoreDToOffset(DTMP, destination.base_reg(), dest_offset);
  } else {
    ASSERT(destination.IsStackSlot());
    ASSERT(tmp != kNoRegister);
    const intptr_t dest_offset = destination.ToStackSlotOffset();
    if (RepresentationUtils::IsUnboxedInteger(representation())) {
      int64_t v;
      const bool ok = compiler::HasIntegerValue(value_, &v);
      RELEASE_ASSERT(ok);
      __ LoadImmediate(
          tmp, pair_index == 0 ? Utils::Low32Bits(v) : Utils::High32Bits(v));
    } else if (representation() == kUnboxedFloat) {
      int32_t float_bits =
          bit_cast<int32_t, float>(Double::Cast(value_).value());
      __ LoadImmediate(tmp, float_bits);
    } else {
      ASSERT(representation() == kTagged);
      __ LoadObject(tmp, value_);
    }
    __ StoreToOffset(tmp, destination.base_reg(), dest_offset);
  }
}

LocationSummary* UnboxedConstantInstr::MakeLocationSummary(Zone* zone,
                                                           bool opt) const {
  const bool is_unboxed_int =
      RepresentationUtils::IsUnboxedInteger(representation());
  ASSERT(!is_unboxed_int || RepresentationUtils::ValueSize(representation()) <=
                                compiler::target::kWordSize);
  const intptr_t kNumInputs = 0;
  const intptr_t kNumTemps = is_unboxed_int ? 0 : 1;
  LocationSummary* locs = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  if (is_unboxed_int) {
    locs->set_out(0, Location::RequiresRegister());
  } else {
    ASSERT(representation_ == kUnboxedDouble);
    locs->set_out(0, Location::RequiresFpuRegister());
  }
  if (kNumTemps > 0) {
    locs->set_temp(0, Location::RequiresRegister());
  }
  return locs;
}

void UnboxedConstantInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  // The register allocator drops constant definitions that have no uses.
  if (!locs()->out(0).IsInvalid()) {
    const Register scratch =
        locs()->temp_count() == 0
            ? kNoRegister
            : locs()->temp(0).reg();
    EmitMoveToLocation(compiler, locs()->out(0), scratch);
  }
}

LocationSummary* EqualityCompareInstr::MakeLocationSummary(Zone* zone,
                                                           bool opt) const {
  const intptr_t kNumInputs = 2;
  const intptr_t kNumTemps = 0;
  if (is_null_aware()) {
    LocationSummary* locs = new (zone)
        LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
    locs->set_in(0, Location::RequiresRegister());
    locs->set_in(1, Location::RequiresRegister());
    locs->set_out(0, Location::RequiresRegister());
    return locs;
  }
  if (input_representation() == kUnboxedInt64) {
    LocationSummary* locs = new (zone)
        LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
    locs->set_in(0, Location::Pair(Location::RequiresRegister(),
                                   Location::RequiresRegister()));
    locs->set_in(1, Location::Pair(Location::RequiresRegister(),
                                   Location::RequiresRegister()));
    locs->set_out(0, Location::RequiresRegister());
    return locs;
  }
  if (input_representation() == kUnboxedDouble) {
    LocationSummary* locs = new (zone)
        LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
    locs->set_in(0, Location::RequiresFpuRegister());
    locs->set_in(1, Location::RequiresFpuRegister());
    locs->set_out(0, Location::RequiresRegister());
    return locs;
  }
  if (input_representation() == kTagged || input_representation() == kUnboxedInt64 ||
      input_representation() == kUnboxedInt32 || input_representation() == kUnboxedUint32) {
    LocationSummary* locs = new (zone)
        LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
    if (is_null_aware()) {
      locs->set_in(0, Location::RequiresRegister());
      locs->set_in(1, Location::RequiresRegister());
    } else {
      locs->set_in(0, LocationRegisterOrConstant(left()));
      // Only one input can be a constant operand. The case of two constant
      // operands should be handled by constant propagation.
      // Only right can be a stack slot.
      locs->set_in(1, locs->in(0).IsConstant()
                          ? Location::RequiresRegister()
                          : LocationRegisterOrConstant(right()));
    }
    locs->set_out(0, Location::RequiresRegister());
    return locs;
  }
  UNREACHABLE();
  return nullptr;
}

static void LoadValueCid(FlowGraphCompiler* compiler,
                         Register value_cid_reg,
                         Register value_reg,
                         compiler::Label* value_is_smi = NULL) {
  __ Comment("LoadValueCid");
  compiler::Label done;
  if (value_is_smi == NULL) {
    __ LoadImmediate(value_cid_reg, kSmiCid);
  }
  __ AndImmediate(CMPRES1, value_reg, kSmiTagMask);
  if (value_is_smi == NULL) {
    __ beq(CMPRES1, ZR, &done);
  } else {
    __ beq(CMPRES1, ZR, value_is_smi);
  }
  __ LoadClassId(value_cid_reg, value_reg);
  __ Bind(&done);
}

static Condition TokenKindToIntRelOp(Token::Kind kind) {
  switch (kind) {
    case Token::kEQ:
      return EQ;
    case Token::kNE:
      return NE;
    case Token::kLT:
      return LT;
    case Token::kGT:
      return GT;
    case Token::kLTE:
      return LE;
    case Token::kGTE:
      return GE;
    default:
      UNREACHABLE();
      return NV;
  }
}

static Condition TokenKindToUintRelOp(Token::Kind kind) {
  switch (kind) {
    case Token::kEQ:
      return EQ;
    case Token::kNE:
      return NE;
    case Token::kLT:
      return ULT;
    case Token::kGT:
      return UGT;
    case Token::kLTE:
      return ULE;
    case Token::kGTE:
      return UGE;
    default:
      UNREACHABLE();
      return NV;
  }
}

// The comparison code to emit is specified by true_condition.
static void EmitBranchOnCondition(FlowGraphCompiler* compiler,
                                  Condition true_condition,
                                  BranchLabels labels) {
  __ Comment("ControlInstruction::EmitBranchOnCondition");
  if (labels.fall_through == labels.false_label) {
    // If the next block is the false successor, fall through to it.
    __ BranchIf(true_condition, labels.true_label);
  } else {
    // If the next block is not the false successor, branch to it.
    Condition false_condition = InvertCondition(true_condition);
    __ BranchIf(false_condition, labels.false_label);
    // Fall through or jump to the true successor.
    if (labels.fall_through != labels.true_label) {
      __ b(labels.true_label);
    }
  }
}

static Condition FlipCondition(Condition condition) {
  switch (condition) {
    case EQ:
      return EQ;
    case NE:
      return NE;
    case LT:
      return GT;
    case LE:
      return GE;
    case GT:
      return LT;
    case GE:
      return LE;
    default:
      UNREACHABLE();
      return EQ;
  }
}

static Condition EmitSmiComparisonOp(FlowGraphCompiler* compiler,
                                     const LocationSummary& locs,
                                     Token::Kind kind) {
  __ Comment("EmitSmiComparisonOp");
  const Location left = locs.in(0);
  const Location right = locs.in(1);
  ASSERT(!left.IsConstant() || !right.IsConstant());
  ASSERT(left.IsRegister() || left.IsConstant());
  ASSERT(right.IsRegister() || right.IsConstant());

  int16_t imm = 0;
  const Register left_reg =
      left.IsRegister()
          ? left.reg()
          : __ LoadConditionOperand(CMPRES1, left.constant(), &imm);
  const Register right_reg =
      right.IsRegister()
          ? right.reg()
          : __ LoadConditionOperand(CMPRES2, right.constant(), &imm);
  __ CompareRegisters(left_reg, right_reg);
  return TokenKindToIntRelOp(kind);
}

static Condition EmitUnboxedInt64EqualityOp(FlowGraphCompiler* compiler,
                                            const LocationSummary& locs,
                                            Token::Kind kind,
                                            BranchLabels labels) {
  __ Comment("EmitUnboxedInt64EqualityOp");
  ASSERT(Token::IsEqualityOperator(kind));
  PairLocation* left_pair = locs.in(0).AsPairLocation();
  Register left_lo = left_pair->At(0).reg();
  Register left_hi = left_pair->At(1).reg();
  PairLocation* right_pair = locs.in(1).AsPairLocation();
  Register right_lo = right_pair->At(0).reg();
  Register right_hi = right_pair->At(1).reg();

  if (labels.false_label == NULL) {
    // Generate branch-free code.
    __ xor_(CMPRES1, left_lo, right_lo);
    __ xor_(AT, left_hi, right_hi);
    __ or_(CMPRES1, CMPRES1, AT);
    __ CompareRegisters(CMPRES1, ZR);
  } else {
    if (kind == Token::kEQ) {
      __ bne(left_hi, right_hi, labels.false_label);
    } else {
      ASSERT(kind == Token::kNE);
      __ bne(left_hi, right_hi, labels.true_label);
    }
    __ CompareRegisters(left_lo, right_lo);
  }
  return TokenKindToUintRelOp(kind);
}

static Condition EmitUnboxedInt64ComparisonOp(FlowGraphCompiler* compiler,
                                              const LocationSummary& locs,
                                              Token::Kind kind,
                                              BranchLabels labels) {
  __ Comment("EmitUnboxedInt64ComparisonOp");
  PairLocation* left_pair = locs.in(0).AsPairLocation();
  Register left_lo = left_pair->At(0).reg();
  Register left_hi = left_pair->At(1).reg();
  PairLocation* right_pair = locs.in(1).AsPairLocation();
  Register right_lo = right_pair->At(0).reg();
  Register right_hi = right_pair->At(1).reg();

  if (labels.false_label == NULL) {
    // Generate branch-free code (except for skipping the lower words compare).
    // Result in CMPRES1, CMPRES2, so that CMPRES1 op CMPRES2 === left op right.
    compiler::Label done;
    // Compare upper halves first.
    __ slt(CMPRES1, right_hi, left_hi);
    __ slt(CMPRES2, left_hi, right_hi);
    // If higher words aren't equal, skip comparing lower words.
    __ bne(CMPRES1, CMPRES2, &done);

    __ sltu(CMPRES1, right_lo, left_lo);
    __ sltu(CMPRES2, left_lo, right_lo);
    __ Bind(&done);
    __ CompareRegisters(CMPRES1, CMPRES2);
  } else {
    switch (kind) {
      case Token::kLT:
      case Token::kLTE: {
        __ slt(AT, left_hi, right_hi);
        __ bne(AT, ZR, labels.true_label);
        __ delay_slot()-> slt(AT, right_hi, left_hi);
        __ bne(AT, ZR, labels.false_label);
        break;
      }
      case Token::kGT:
      case Token::kGTE: {
        __ slt(AT, left_hi, right_hi);
        __ bne(AT, ZR, labels.false_label);
        __ delay_slot()-> slt(AT, right_hi, left_hi);
        __ bne(AT, ZR, labels.true_label);
        break;
      }
      default:
        UNREACHABLE();
    }
    __ CompareRegisters(left_lo, right_lo);
  }
  return TokenKindToUintRelOp(kind);
}

static Condition TokenKindToIntCondition(Token::Kind kind) {
  switch (kind) {
    case Token::kEQ:
      return EQ;
    case Token::kNE:
      return NE;
    case Token::kLT:
      return LT;
    case Token::kGT:
      return GT;
    case Token::kLTE:
      return LE;
    case Token::kGTE:
      return GE;
    default:
      UNREACHABLE();
      return INVALID_RELATION;
  }
}

static Condition EmitUnboxedWordComparisonOp(FlowGraphCompiler* compiler,
                                      const LocationSummary& locs,
                                      Token::Kind kind) {
  Location left = locs.in(0);
  Location right = locs.in(1);
  ASSERT(!left.IsConstant() || !right.IsConstant());

  Condition true_condition = TokenKindToIntCondition(kind);

  if (left.IsConstant()) {
    __ CompareImmediate(right.reg(),
                              static_cast<uword>(Integer::Cast(left.constant()).Value()));
    true_condition = FlipCondition(true_condition);
  } else if (right.IsConstant()) {
    __ CompareImmediate(left.reg(),
                               static_cast<uword>(Integer::Cast(right.constant()).Value()));
  } else {
    __ CompareRegisters(left.reg(), right.reg());
  }
  return true_condition;
}

static Condition EmitDoubleComparisonOp(FlowGraphCompiler* compiler,
                                        const LocationSummary& locs,
                                        Token::Kind kind,
                                        BranchLabels labels) {
  DRegister left = locs.in(0).fpu_reg();
  DRegister right = locs.in(1).fpu_reg();

  __ Comment("DoubleComparisonOp(left=%d, right=%d)", left, right);

  __ cund(left, right);
  compiler::Label* nan_label =
      (kind == Token::kNE) ? labels.true_label : labels.false_label;
  __ bc1t(nan_label);

  switch (kind) {
    case Token::kEQ:
      __ ceqd(left, right);
      break;
    case Token::kNE:
      __ ceqd(left, right);
      break;
    case Token::kLT:
      __ coltd(left, right);
      break;
    case Token::kLTE:
      __ coled(left, right);
      break;
    case Token::kGT:
      __ coltd(right, left);
      break;
    case Token::kGTE:
      __ coled(right, left);
      break;
    default: {
      // We should only be passing the above conditions to this function.
      UNREACHABLE();
      break;
    }
  }

  if (labels.false_label == NULL) {
    // Generate branch-free code and return result in condition.
    __ LoadImmediate(CMPRES1, 1);
    if (kind == Token::kNE) {
      __ movf(CMPRES1, ZR);
    } else {
      __ movt(CMPRES1, ZR);
    }
    __ CompareRegisters(CMPRES1, ZR);
    return EQ;
  } else {
    if (labels.fall_through == labels.false_label) {
      if (kind == Token::kNE) {
        __ bc1f(labels.true_label);
      } else {
        __ bc1t(labels.true_label);
      }
      // Since we already branched on true, return the never true condition.
      __ CompareRegisters(CMPRES1, CMPRES2);
      return NV;
    } else {
      if (kind == Token::kNE) {
        __ bc1t(labels.false_label);
      } else {
        __ bc1f(labels.false_label);
      }
      __ LoadImmediate(CMPRES1, 0);
      // Since we already branched on false, return the always true condition.
      __ CompareRegisters(CMPRES1, ZR);
      return EQ;
    }
  }
}

static Condition EmitNullAwareInt64ComparisonOp(FlowGraphCompiler* compiler,
                                                const LocationSummary& locs,
                                                Token::Kind kind,
                                                BranchLabels labels) {
  ASSERT((kind == Token::kEQ) || (kind == Token::kNE));
  const Register left = locs.in(0).reg();
  const Register right = locs.in(1).reg();
  const Condition true_condition = TokenKindToIntCondition(kind);
  compiler::Label* equal_result =
  (true_condition == EQ) ? labels.true_label : labels.false_label;
  compiler::Label* not_equal_result =
  (true_condition == EQ) ? labels.false_label : labels.true_label;

  // Check if operands have the same value. If they don't, then they could
  // be equal only if both of them are Mints with the same value.
  __ BranchEqual(left, right, equal_result);
  __ and_(TMP, left, right);
  __ BranchIfSmi(TMP, not_equal_result);
  __ LoadClassId(CMPRES1, left);
  __ LoadImmediate(CMPRES2, kMintCid);
  __ BranchNotEqual(CMPRES1, CMPRES2, not_equal_result);
  __ LoadClassId(CMPRES1, right);
  // In CMPRES2 is kMintCid
  __ BranchNotEqual(CMPRES1, CMPRES2, not_equal_result);
  __ LoadFieldFromOffset(CMPRES1, left, compiler::target::Mint::value_offset());
  __ LoadFieldFromOffset(CMPRES2, right, compiler::target::Mint::value_offset());
  __ bne(CMPRES1, CMPRES2, not_equal_result);
  __ LoadFieldFromOffset(
    CMPRES1, left,
  compiler::target::Mint::value_offset() + compiler::target::kWordSize);
  __ LoadFieldFromOffset(
    CMPRES2, right,
  compiler::target::Mint::value_offset() + compiler::target::kWordSize);
  __ CompareRegisters(CMPRES1, CMPRES2);
  return true_condition;
}

Condition EqualityCompareInstr::EmitConditionCode(
    FlowGraphCompiler* compiler,
    BranchLabels labels) {
  if (is_null_aware()) {
    return EmitNullAwareInt64ComparisonOp(compiler, *locs(), kind(), labels);
  }
  if (input_representation() == kTagged) {
    return EmitSmiComparisonOp(compiler, *locs(), kind());
  } else if (input_representation() == kUnboxedInt32 || input_representation() == kUnboxedUint32) {
    return EmitUnboxedWordComparisonOp(compiler, *locs(), kind());
  } else if (input_representation() == kUnboxedInt64) {
    return EmitUnboxedInt64EqualityOp(compiler, *locs(), kind(), labels);
  } else {
    ASSERT(input_representation() == kUnboxedDouble);
    return EmitDoubleComparisonOp(compiler, *locs(), kind(), labels);
  }
}

Condition StrictCompareInstr::EmitComparisonCodeRegConstant(
    FlowGraphCompiler* compiler,
    BranchLabels labels,
    Register reg,
    const Object& obj) {
  return compiler->EmitEqualityRegConstCompare(reg, obj, needs_number_check(),
                                                source(), deopt_id());
}

void ConditionInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  compiler::Label is_true, is_false;
  BranchLabels labels = {&is_true, &is_false, &is_false};
  Condition true_condition = EmitConditionCode(compiler, labels);
  if (true_condition != kInvalidCondition) {
    EmitBranchOnCondition(compiler, true_condition, labels);
  }

  Register result = this->locs()->out(0).reg();
  compiler::Label done;
  __ Bind(&is_false);
  __ LoadObject(result, Bool::False());
  __ b(&done);
  __ Bind(&is_true);
  __ LoadObject(result, Bool::True());
  __ Bind(&done);
}

void ConditionInstr::EmitBranchCode(FlowGraphCompiler* compiler,
                                      BranchInstr* branch) {
  BranchLabels labels = compiler->CreateBranchLabels(branch);
  Condition true_condition = EmitConditionCode(compiler, labels);
  if (true_condition != kInvalidCondition) {
    EmitBranchOnCondition(compiler, true_condition, labels);
  }
}

LocationSummary* TestIntInstr::MakeLocationSummary(Zone* zone,
                                                    bool opt) const {
  const intptr_t kNumInputs = 2;
  const intptr_t kNumTemps = 0;
  LocationSummary* locs = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  locs->set_in(0, Location::RequiresRegister());
  // Only one input can be a constant operand. The case of two constant
  // operands should be handled by constant propagation.
  locs->set_in(1, LocationRegisterOrConstant(right()));
  locs->set_out(0, Location::RequiresRegister());
  return locs;
}

Condition TestIntInstr::EmitConditionCode(FlowGraphCompiler* compiler,
                                            BranchLabels labels) {
  Register left = locs()->in(0).reg();
  Location right = locs()->in(1);

  if (right.IsConstant()) {
    const int32_t imm = static_cast<int32_t>(ComputeImmediateMask());
    __ TestImmediate(left, imm);
  } else {
    ASSERT(right.IsRegister());
    __ TestRegisters(left, right.reg());
  }
  Condition true_condition = (kind() == Token::kNE) ? NE : EQ;
  return true_condition;
}

LocationSummary* TestCidsInstr::MakeLocationSummary(Zone* zone,
                                                    bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 1;
  LocationSummary* locs = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  locs->set_in(0, Location::RequiresRegister());
  locs->set_temp(0, Location::RequiresRegister());
  locs->set_out(0, Location::RequiresRegister());
  return locs;
}

Condition TestCidsInstr::EmitConditionCode(FlowGraphCompiler* compiler,
                                            BranchLabels labels) {
  ASSERT((kind() == Token::kIS) || (kind() == Token::kISNOT));
  Register val_reg = locs()->in(0).reg();
  Register cid_reg = locs()->temp(0).reg();

  compiler::Label* deopt =
      CanDeoptimize()
          ? compiler->AddDeoptStub(deopt_id(), ICData::kDeoptTestCids)
          : nullptr;

  const intptr_t true_result = (kind() == Token::kIS) ? 1 : 0;
  const ZoneGrowableArray<intptr_t>& data = cid_results();
  ASSERT(data[0] == kSmiCid);
  bool result = data[1] == true_result;
  __ BranchIfSmi(val_reg, result ? labels.true_label : labels.false_label);
  __ LoadClassId(cid_reg, val_reg);
  for (intptr_t i = 2; i < data.length(); i += 2) {
    const intptr_t test_cid = data[i];
    ASSERT(test_cid != kSmiCid);
    result = data[i + 1] == true_result;
    __ BranchEqual(cid_reg, compiler::Immediate(test_cid),
                    result ? labels.true_label : labels.false_label);
  }
  // No match found, deoptimize or default action.
  if (deopt == NULL) {
    // If the cid is not in the list, jump to the opposite label from the cids
    // that are in the list.  These must be all the same (see asserts in the
    // constructor).
    compiler::Label* target = result ? labels.false_label : labels.true_label;
    if (target != labels.fall_through) {
      __ b(target);
    }
  } else {
    __ b(deopt);
  }
  // Dummy result as this method already did the jump, there's no need
  // for the caller to branch on a condition.
  return kInvalidCondition;
}

LocationSummary* RelationalOpInstr::MakeLocationSummary(Zone* zone,
                                                        bool opt) const {
  const intptr_t kNumInputs = 2;
  const intptr_t kNumTemps = 0;
  if (input_representation() == kUnboxedInt64) {
    const intptr_t kNumTemps = 0;
    LocationSummary* locs = new (zone) LocationSummary(
        zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
    locs->set_in(0, Location::Pair(Location::RequiresRegister(),
                                    Location::RequiresRegister()));
    locs->set_in(1, Location::Pair(Location::RequiresRegister(),
                                    Location::RequiresRegister()));
    locs->set_out(0, Location::RequiresRegister());
    return locs;
  }
  if (input_representation() == kUnboxedDouble) {
    LocationSummary* summary = new (zone) LocationSummary(
        zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
    summary->set_in(0, Location::RequiresFpuRegister());
    summary->set_in(1, Location::RequiresFpuRegister());
    summary->set_out(0, Location::RequiresRegister());
    return summary;
  }
  ASSERT(input_representation() == kTagged);
  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  summary->set_in(0, LocationRegisterOrConstant(left()));
  // Only one input can be a constant operand. The case of two constant
  // operands should be handled by constant propagation.
  summary->set_in(1, summary->in(0).IsConstant()
                          ? Location::RequiresRegister()
                          : LocationRegisterOrConstant(right()));
  summary->set_out(0, Location::RequiresRegister());
  return summary;
}

Condition RelationalOpInstr::EmitConditionCode(FlowGraphCompiler* compiler,
                                                BranchLabels labels) {
  if (input_representation() == kTagged) {
    return EmitSmiComparisonOp(compiler, *locs(), kind());
  } else if (input_representation() == kUnboxedInt64) {
    return EmitUnboxedInt64ComparisonOp(compiler, *locs(), kind(), labels);
  } else {
    ASSERT(input_representation() == kUnboxedDouble);
    return EmitDoubleComparisonOp(compiler, *locs(), kind(), labels);
  }
}

#define R(r) (1 << r)

LocationSummary* FfiCallInstr::MakeLocationSummary(Zone* zone,
                                                   bool is_optimizing) const {
  return MakeLocationSummaryInternal(
      zone, is_optimizing,
      (R(CallingConventions::kSecondNonArgumentRegister) |
       R(CallingConventions::kFfiAnyNonAbiRegister) | R(CALLEE_SAVED_TEMP)));
}

#undef R


void FfiCallInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  const Register target = locs()->in(TargetAddressIndex()).reg();

  // The temps are indexed according to their register number.
  const Register temp1 = locs()->temp(0).reg();
  // For regular calls, this holds the FP for rebasing the original locations
  // during EmitParamMoves.
  // For leaf calls, this holds the SP used to restore the pre-aligned SP after
  // the call.
  const Register saved_fp_or_sp = locs()->temp(1).reg();
  const Register temp2 = locs()->temp(2).reg();

  ASSERT(temp1 != target);
  ASSERT(temp2 != target);
  ASSERT(temp1 != saved_fp_or_sp);
  ASSERT(temp2 != saved_fp_or_sp);
  ASSERT(saved_fp_or_sp != target);

  // Ensure these are callee-saved register and are preserved across the call.
  ASSERT(IsCalleeSavedRegister(saved_fp_or_sp));
  // Other temps don't need to be preserved.

  __ mov(saved_fp_or_sp, is_leaf_ ? SPREG : FPREG);

  if (!is_leaf_) {
    // Make a space to put the return address.
    __ Push(ZR);

    // We need to create a dummy "exit frame". It will have a null code object.
    __ LoadObject(CODE_REG, Object::null_object());
    __ set_constant_pool_allowed(false);
    __ EnterDartFrame(0, /*load_pool_pointer=*/false);
  }

  __ ReserveAlignedFrameSpace((marshaller_.RequiredStackSpaceInBytes()==0) ? 
                              4 * kWordSize : marshaller_.RequiredStackSpaceInBytes());
  if (FLAG_target_memory_sanitizer) {
    UNIMPLEMENTED();
  }

  EmitParamMoves(compiler, is_leaf_ ? FPREG : saved_fp_or_sp, temp1, temp2);

  if (compiler::Assembler::EmittingComments()) {
    __ Comment(is_leaf_ ? "Leaf Call" : "Call");
  }

  if (is_leaf_) {
#if !defined(PRODUCT)
    // Set the thread object's top_exit_frame_info and VMTag to enable the
    // profiler to determine that thread is no longer executing Dart code.
    __ StoreToOffset(FPREG, THR,
                     compiler::target::Thread::top_exit_frame_info_offset());
    __ StoreToOffset(target, THR, compiler::target::Thread::vm_tag_offset());
#endif
    __ mov(T9, target);
    __ jalr(T9);

#if !defined(PRODUCT)
    __ LoadImmediate(temp1, compiler::target::Thread::vm_tag_dart_id());
    __ StoreToOffset(temp1, THR, compiler::target::Thread::vm_tag_offset());
    __ StoreToOffset(ZR, THR,
                     compiler::target::Thread::top_exit_frame_info_offset());
#endif
  } else {
    // We need to copy a dummy return address up into the dummy stack frame so
    // the stack walker will know which safepoint to use.
    compiler::Label label_for_getting_pc;
    __ bal(&label_for_getting_pc);
    __ Bind(&label_for_getting_pc);
    __ addiu(RA, RA, compiler::Immediate(8));
    __ StoreToOffset(RA, FPREG, kSavedCallerPcSlotFromFp * kWordSize);
    compiler->EmitCallsiteMetadata(source(), deopt_id(),
                                   UntaggedPcDescriptors::Kind::kOther, locs(),
                                   env());

    if (CanExecuteGeneratedCodeInSafepoint()) {
      // Update information in the thread object and enter a safepoint.
      __ LoadImmediate(temp1, compiler::target::Thread::exit_through_ffi());
      __ TransitionGeneratedToNative(target, FPREG, temp1, temp2,
                                     /*enter_safepoint=*/true);

      __ mov(T9, target);
      __ jalr(T9);

      // Update information in the thread object and leave the safepoint.
      __ TransitionNativeToGenerated(temp1, temp2, /*leave_safepoint=*/true);
    } else {
      // Update information in the thread object and enter a safepoint.
      // Outline state transition. In AOT, for code size. In JIT, because we
      // cannot trust that code will be executable.
      __ lw(temp1,
            compiler::Address(
                THR, compiler::target::Thread::
                         call_native_through_safepoint_entry_point_offset()));

      // Calls T0 and clobbers S1 (along with volatile registers).
      ASSERT(target == T0);
      __ mov(T9, temp1);
      __ jalr(T9);
    }

    if (marshaller_.IsHandleCType(compiler::ffi::kResultIndex)) {
      __ Comment("Check Dart_Handle for Error.");
      ASSERT(temp1 != CallingConventions::kReturnReg);
      ASSERT(temp2 != CallingConventions::kReturnReg);
      compiler::Label not_error;
      __ LoadFromOffset(temp1, CallingConventions::kReturnReg,
                        compiler::target::LocalHandle::ptr_offset());
      __ BranchIfSmi(temp1, &not_error);
      __ LoadClassId(temp1, temp1);
      __ RangeCheck(temp1, temp2, kFirstErrorCid, kLastErrorCid,
                    compiler::AssemblerBase::kIfNotInRange, &not_error);

      // Slow path, use the stub to propagate error, to save on code-size.
      __ Comment("Slow path: call Dart_PropagateError through stub.");
      __ lw(temp1,
            compiler::Address(
                THR, compiler::target::Thread::
                         call_native_through_safepoint_entry_point_offset()));
      __ lw(target, compiler::Address(
                        THR, kPropagateErrorRuntimeEntry.OffsetFromThread()));
      __ mov(CallingConventions::ArgumentRegisters[0], CallingConventions::kReturnReg);
      ASSERT(target == T0);
      __ mov(T9, temp1);
      __ jalr(T9);
#if defined(DEBUG)
      // We should never return with normal controlflow from this.
      __ Breakpoint();
#endif

      __ Bind(&not_error);
    }

    // Restore the global object pool after returning from runtime.
    if (FLAG_precompiled_mode) {
    __ lw(PP, compiler::Address(THR, compiler::target::Thread::global_object_pool_offset()));
    }
  }

  EmitReturnMoves(compiler, temp1, temp2);

  if (is_leaf_) {
    // Restore the pre-aligned SP.
    __ mov(SPREG, saved_fp_or_sp);
  } else {
    // Leave dummy exit frame.
    __ LeaveDartFrame();
    __ set_constant_pool_allowed(true);

    // Instead of returning to the "fake" return address, we just pop it.
    __ PopRegister(temp1);
  }
}

LocationSummary* OneByteStringFromCharCodeInstr::MakeLocationSummary(
    Zone* zone,
    bool opt) const {
  const intptr_t kNumInputs = 1;
  return LocationSummary::Make(zone, kNumInputs, Location::RequiresRegister(),
                               LocationSummary::kNoCall);
}

void OneByteStringFromCharCodeInstr::EmitNativeCode(
    FlowGraphCompiler* compiler) {
  ASSERT(compiler->is_optimizing());
  Register char_code = locs()->in(0).reg();
  Register result = locs()->out(0).reg();

  __ lw(result, compiler::Address(THR, Thread::predefined_symbols_address_offset()));
  __ AddImmediate(result, Symbols::kNullCharCodeSymbolOffset * compiler::target::kWordSize);
  __ sll(TMP, char_code, 1);  // Char code is a smi.
  __ addu(TMP, TMP, result);
  __ lw(result, compiler::Address(TMP));
}

LocationSummary* StringToCharCodeInstr::MakeLocationSummary(Zone* zone,
                                                            bool opt) const {
  const intptr_t kNumInputs = 1;
  return LocationSummary::Make(zone, kNumInputs, Location::RequiresRegister(),
                               LocationSummary::kNoCall);
}

void StringToCharCodeInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  __ Comment("StringToCharCodeInstr");

  ASSERT(cid_ == kOneByteStringCid);
  Register str = locs()->in(0).reg();
  Register result = locs()->out(0).reg();
  ASSERT(str != result);
  compiler::Label is_one, done;
  __ lw(result, compiler::FieldAddress(str, String::length_offset()));
  __ BranchEqual(result, compiler::Immediate(Smi::RawValue(1)), &is_one);
  __ addiu(result, ZR, compiler::Immediate(Smi::RawValue(-1)));
  __ b(&done);
  __ Bind(&is_one);
  __ lbu(result, compiler::FieldAddress(str, OneByteString::data_offset()));
  __ SmiTag(result);
  __ Bind(&done);
}

DEFINE_UNIMPLEMENTED_INSTRUCTION(GuardFieldTypeInstr)

LocationSummary* GuardFieldClassInstr::MakeLocationSummary(Zone* zone,
                                                           bool opt) const {
  const intptr_t kNumInputs = 1;

  const intptr_t value_cid = value()->Type()->ToCid();
  const intptr_t field_cid = field().guarded_cid();

  const bool emit_full_guard = !opt || (field_cid == kIllegalCid);
  const bool needs_value_cid_temp_reg =
        emit_full_guard || ((value_cid == kDynamicCid) && (field_cid != kSmiCid));
  const bool needs_field_temp_reg = emit_full_guard;

  intptr_t num_temps = 0;
  if (needs_value_cid_temp_reg) {
    num_temps++;
  }
  if (needs_field_temp_reg) {
    num_temps++;
  }

  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, num_temps, LocationSummary::kNoCall);
  summary->set_in(0, Location::RequiresRegister());

  for (intptr_t i = 0; i < num_temps; i++) {
    summary->set_temp(i, Location::RequiresRegister());
  }

  return summary;
}

void GuardFieldClassInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  ASSERT(compiler::target::UntaggedObject::kClassIdTagSize == 20);
  ASSERT(sizeof(UntaggedField::guarded_cid_) == 4);
  ASSERT(sizeof(UntaggedField::is_nullable_) == 4);
  __ Comment("GuardFieldClassInstr");

  const intptr_t value_cid = value()->Type()->ToCid();
  const intptr_t field_cid = field().guarded_cid();
  const intptr_t nullability = field().is_nullable() ? kNullCid : kIllegalCid;

  if (field_cid == kDynamicCid) {
    if (Compiler::IsBackgroundCompilation()) {
      // Field state changed while compiling.
      Compiler::AbortBackgroundCompilation(
          deopt_id(),
          "GuardFieldClassInstr: field state changed while compiling");
    }
    ASSERT(!compiler->is_optimizing());
    return;  // Nothing to emit.
  }

  const bool emit_full_guard =
      !compiler->is_optimizing() || (field_cid == kIllegalCid);

  const bool needs_value_cid_temp_reg =
      emit_full_guard || ((value_cid == kDynamicCid) && (field_cid != kSmiCid));

  const bool needs_field_temp_reg = emit_full_guard;

  const Register value_reg = locs()->in(0).reg();

  const Register value_cid_reg =
      needs_value_cid_temp_reg ? locs()->temp(0).reg() : kNoRegister;

  const Register field_reg = needs_field_temp_reg
                                 ? locs()->temp(locs()->temp_count() - 1).reg()
                                 : kNoRegister;

  compiler::Label ok, fail_label;

  compiler::Label* deopt =
      compiler->is_optimizing()
          ? compiler->AddDeoptStub(deopt_id(), ICData::kDeoptGuardField)
          : nullptr;

  compiler::Label* fail = (deopt != nullptr) ? deopt : &fail_label;

  if (emit_full_guard) {
    __ LoadObject(field_reg, Field::ZoneHandle(field().Original()));

    compiler::FieldAddress field_cid_operand(field_reg, compiler::target::Field::guarded_cid_offset());
    compiler::FieldAddress field_nullability_operand(field_reg,
                                          compiler::target::Field::is_nullable_offset());

    if (value_cid == kDynamicCid) {
      LoadValueCid(compiler, value_cid_reg, value_reg);

      __ lw(CMPRES1, field_cid_operand);
      __ beq(value_cid_reg, CMPRES1, &ok);
      __ lw(TMP, field_nullability_operand);
      __ subu(CMPRES1, value_cid_reg, TMP);
    } else if (value_cid == kNullCid) {
      __ lw(TMP, field_nullability_operand);
      __ LoadImmediate(CMPRES1, value_cid);
      __ subu(CMPRES1, TMP, CMPRES1);
    } else {
      __ lw(TMP, field_cid_operand);
      __ LoadImmediate(CMPRES1, value_cid);
      __ subu(CMPRES1, TMP, CMPRES1);
    }
    __ beq(CMPRES1, ZR, &ok);

    // Check if the tracked state of the guarded field can be initialized
    // inline. If the field needs length check we fall through to runtime
    // which is responsible for computing offset of the length field
    // based on the class id.
    // Length guard will be emitted separately when needed via GuardFieldLength
    // instruction after GuardFieldClass.
    if (!field().needs_length_check()) {
      // Uninitialized field can be handled inline. Check if the
      // field is still unitialized.
      __ lw(CMPRES1, field_cid_operand);
      __ BranchNotEqual(CMPRES1, compiler::Immediate(kIllegalCid), fail);

      if (value_cid == kDynamicCid) {
        __ sw(value_cid_reg, field_cid_operand);
        __ sw(value_cid_reg, field_nullability_operand);
      } else {
        __ LoadImmediate(TMP, value_cid);
        __ sw(TMP, field_cid_operand);
        __ sw(TMP, field_nullability_operand);
      }

      __ b(&ok);
    }

    if (deopt == nullptr) {
      __ Bind(fail);

      __ lw(CMPRES1,
             compiler::FieldAddress(field_reg, Field::guarded_cid_offset()));
      __ BranchEqual(CMPRES1, compiler::Immediate(kDynamicCid), &ok);

      __ addiu(SP, SP, compiler::Immediate(-2 * kWordSize));
      __ sw(field_reg, compiler::Address(SP, 1 * kWordSize));
      __ sw(value_reg, compiler::Address(SP, 0 * kWordSize));
      ASSERT(!compiler->is_optimizing());
      __ CallRuntime(kUpdateFieldCidRuntimeEntry, 2);
      __ Drop(2);  // Drop the field and the value.
    } else {
      __ b(fail);
    }
  } else {
    ASSERT(compiler->is_optimizing());
    ASSERT(deopt != nullptr);

    // Field guard class has been initialized and is known.
    if (value_cid == kDynamicCid) {
      // Value's class id is not known.
      __ AndImmediate(CMPRES1, value_reg, kSmiTagMask);

      if (field_cid != kSmiCid) {
        __ beq(CMPRES1, ZR, fail);
        __ LoadClassId(value_cid_reg, value_reg);
        __ LoadImmediate(TMP, field_cid);
        __ subu(CMPRES1, value_cid_reg, TMP);
      }

      if (field().is_nullable() && (field_cid != kNullCid)) {
        __ beq(CMPRES1, ZR, &ok);
        if (field_cid != kSmiCid) {
          __ LoadImmediate(TMP, kNullCid);
          __ subu(CMPRES1, value_cid_reg, TMP);
        } else {
          __ LoadObject(TMP, Object::null_object());
          __ subu(CMPRES1, value_reg, TMP);
        }
      }

      __ bne(CMPRES1, ZR, fail);
    } else if (value_cid == field_cid) {
      // This would normally be caught by Canonicalize, but RemoveRedefinitions
      // may sometimes produce the situation after the last Canonicalize pass.
    } else {
      // Both value's and field's class id is known.
      ASSERT((value_cid != field_cid) && (value_cid != nullability));
      __ b(fail);
    }
  }
  __ Bind(&ok);
}

LocationSummary* GuardFieldLengthInstr::MakeLocationSummary(Zone* zone,
                                                            bool opt) const {
  const intptr_t kNumInputs = 1;

  if (!opt || (field().guarded_list_length() == Field::kUnknownFixedLength)) {
    const intptr_t kNumTemps = 1;
    LocationSummary* summary = new (zone)
        LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
    summary->set_in(0, Location::RequiresRegister());
    // We need temporaries for field object.
    summary->set_temp(0, Location::RequiresRegister());
    return summary;
  }
  LocationSummary* summary =
      new (zone) LocationSummary(zone, kNumInputs, 0, LocationSummary::kNoCall);
  summary->set_in(0, Location::RequiresRegister());
  return summary;
}

void GuardFieldLengthInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  if (field().guarded_list_length() == Field::kNoFixedLength) {
    if (Compiler::IsBackgroundCompilation()) {
      // Field state changed while compiling.
      Compiler::AbortBackgroundCompilation(
          deopt_id(),
          "GuardFieldLengthInstr: field state changed while compiling");
    }
    ASSERT(!compiler->is_optimizing());
    return;  // Nothing to emit.
  }

  compiler::Label* deopt =
      compiler->is_optimizing()
          ? compiler->AddDeoptStub(deopt_id(), ICData::kDeoptGuardField)
          : nullptr;

  const Register value_reg = locs()->in(0).reg();

  if (!compiler->is_optimizing() ||
      (field().guarded_list_length() == Field::kUnknownFixedLength)) {
    const Register field_reg = locs()->temp(0).reg();

    compiler::Label ok;

    __ LoadObject(field_reg, Field::ZoneHandle(field().Original()));

    __ lb(CMPRES1,
          compiler::FieldAddress(field_reg,
                       Field::guarded_list_length_in_object_offset_offset()));

    __ lw(CMPRES2,
          compiler::FieldAddress(field_reg, Field::guarded_list_length_offset()));

    __ bltz(CMPRES1, &ok);

    // Load the length from the value. GuardFieldClass already verified that
    // value's class matches guarded class id of the field.
    // CMPRES1 contains offset already corrected by -kHeapObjectTag that is
    // why we can use Address instead of FieldAddress.
    __ addu(TMP, value_reg, CMPRES1);
    __ lw(TMP, compiler::Address(TMP));

    if (deopt == nullptr) {
      __ beq(CMPRES2, TMP, &ok);

      __ addiu(SP, SP, compiler::Immediate(-2 * kWordSize));
      __ sw(field_reg, compiler::Address(SP, 1 * kWordSize));
      __ sw(value_reg, compiler::Address(SP, 0 * kWordSize));
      ASSERT(!compiler->is_optimizing());  // No deopt info needed.
      __ CallRuntime(kUpdateFieldCidRuntimeEntry, 2);
      __ Drop(2);  // Drop the field and the value.
    } else {
      __ bne(CMPRES2, TMP, deopt);
    }

    __ Bind(&ok);
  } else {
    ASSERT(compiler->is_optimizing());
    ASSERT(field().guarded_list_length() >= 0);
    ASSERT(field().guarded_list_length_in_object_offset() !=
           Field::kUnknownLengthOffset);

    __ lw(CMPRES1,
          compiler::FieldAddress(value_reg,
                       field().guarded_list_length_in_object_offset()));
    __ LoadImmediate(TMP, Smi::RawValue(field().guarded_list_length()));
    __ bne(CMPRES1, TMP, deopt);
  }
}

LocationSummary* AllocateUninitializedContextInstr::MakeLocationSummary(
    Zone* zone,
    bool opt) const {
  ASSERT(opt);
  const intptr_t kNumInputs = 0;
  const intptr_t kNumTemps = 3;
  LocationSummary* locs = new (zone) LocationSummary(
      zone, kNumInputs, kNumTemps, LocationSummary::kCallOnSlowPath);
  locs->set_temp(0, Location::RegisterLocation(T1));
  locs->set_temp(1, Location::RegisterLocation(T2));
  locs->set_temp(2, Location::RegisterLocation(T3));
  locs->set_out(0, Location::RegisterLocation(V0));
  return locs;
}

class AllocateContextSlowPath
    :  public TemplateSlowPathCode<AllocateUninitializedContextInstr> {
 public:
  explicit AllocateContextSlowPath(
      AllocateUninitializedContextInstr* instruction)
      : TemplateSlowPathCode(instruction) {}

  virtual void EmitNativeCode(FlowGraphCompiler* compiler) {
    __ Comment("AllocateContextSlowPath");
    __ Bind(entry_label());

    LocationSummary* locs = instruction()->locs();
    locs->live_registers()->Remove(locs->out(0));

    compiler->SaveLiveRegisters(locs);

    auto slow_path_env = compiler->SlowPathEnvironmentFor(
        instruction(), /*num_slow_path_args=*/0);
    ASSERT(slow_path_env != nullptr);

    auto object_store = compiler->isolate_group()->object_store();
    const auto& allocate_context_stub = Code::ZoneHandle(
        compiler->zone(), object_store->allocate_context_stub());
    __ LoadImmediate(T1, instruction()->num_context_variables());
    compiler->GenerateStubCall(instruction()->source(), allocate_context_stub,
                               UntaggedPcDescriptors::kOther, locs,
                               instruction()->deopt_id(), slow_path_env);
    ASSERT(instruction()->locs()->out(0).reg() == V0);
    compiler->RestoreLiveRegisters(instruction()->locs());
    __ b(exit_label());
  }
};

void AllocateUninitializedContextInstr::EmitNativeCode(
    FlowGraphCompiler* compiler) {
  Register temp0 = locs()->temp(0).reg();
  Register temp1 = locs()->temp(1).reg();
  Register temp2 = locs()->temp(2).reg();
  Register result = locs()->out(0).reg();
  // Try allocate the object.
  AllocateContextSlowPath* slow_path = new AllocateContextSlowPath(this);
  compiler->AddSlowPathCode(slow_path);
  intptr_t instance_size = Context::InstanceSize(num_context_variables());
  if (!FLAG_use_slow_path && FLAG_inline_alloc) {
  __ TryAllocateArray(kContextCid, instance_size, slow_path->entry_label(),
                      result,  // instance
                      temp0, temp1, temp2);

  // Setup up number of context variables field.
  __ LoadImmediate(temp0, num_context_variables());
  __ sw(temp0, compiler::FieldAddress(result, Context::num_variables_offset()));
  } else {
    __ Jump(slow_path->entry_label());
  }

  __ Bind(slow_path->exit_label());
}

LocationSummary* AllocateContextInstr::MakeLocationSummary(Zone* zone,
                                                           bool opt) const {
  const intptr_t kNumInputs = 0;
  const intptr_t kNumTemps = 1;
  LocationSummary* locs = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kCall);
  locs->set_temp(0, Location::RegisterLocation(T1));
  locs->set_out(0, Location::RegisterLocation(V0));
  return locs;
}

void AllocateContextInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  ASSERT(locs()->temp(0).reg() == T1);
  ASSERT(locs()->out(0).reg() == V0);

  __ Comment("AllocateContextInstr");
  __ LoadImmediate(T1, num_context_variables());
  auto object_store = compiler->isolate_group()->object_store();
  const auto& allocate_context_stub =
      Code::ZoneHandle(compiler->zone(), object_store->allocate_context_stub());
  compiler->GenerateStubCall(source(), allocate_context_stub,
                             UntaggedPcDescriptors::kOther, locs(), deopt_id(),
                             env());
}

LocationSummary* CloneContextInstr::MakeLocationSummary(Zone* zone,
                                                        bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* locs = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kCall);
  locs->set_in(0, Location::RegisterLocation(T5));
  locs->set_out(0, Location::RegisterLocation(V0));
  return locs;
}

void CloneContextInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  ASSERT(locs()->in(0).reg() == T5);
  ASSERT(locs()->out(0).reg() == V0);

  auto object_store = compiler->isolate_group()->object_store();
  const auto& clone_context_stub =
      Code::ZoneHandle(compiler->zone(), object_store->clone_context_stub());
  compiler->GenerateStubCall(source(), clone_context_stub,
                             /*kind=*/UntaggedPcDescriptors::kOther, locs(),
                             deopt_id(), env());
}

LocationSummary* CheckStackOverflowInstr::MakeLocationSummary(Zone* zone,
                                                              bool opt) const {
  const intptr_t kNumInputs = 0;
  const intptr_t kNumTemps = 2;
  const bool using_shared_stub = UseSharedSlowPathStub(opt);
  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps,
                      using_shared_stub ? LocationSummary::kCallOnSharedSlowPath
                                        : LocationSummary::kCallOnSlowPath);
  summary->set_temp(0, Location::RequiresRegister());
  summary->set_temp(1, Location::RequiresRegister());
  return summary;
}

class CheckStackOverflowSlowPath : public TemplateSlowPathCode<CheckStackOverflowInstr> {
 public:
  static constexpr intptr_t kNumSlowPathArgs = 0;

  explicit CheckStackOverflowSlowPath(CheckStackOverflowInstr* instruction)
      : TemplateSlowPathCode(instruction) {}

  virtual void EmitNativeCode(FlowGraphCompiler* compiler) {
    if (compiler->isolate_group()->use_osr() && osr_entry_label()->IsLinked()) {
      Register value = instruction()->locs()->temp(0).reg();
      __ Comment("CheckStackOverflowSlowPathOsr");
      __ Bind(osr_entry_label());
      __ LoadImmediate(value, Thread::kOsrRequest);
      __ sw(value, compiler::Address(THR, Thread::stack_overflow_flags_offset()));
    }
    __ Comment("CheckStackOverflowSlowPath");
    __ Bind(entry_label());
    const bool using_shared_stub =
        instruction()->locs()->call_on_shared_slow_path();
    if (!using_shared_stub) {
      compiler->SaveLiveRegisters(instruction()->locs());
    }
    // pending_deoptimization_env_ is needed to generate a runtime call that
    // may throw an exception.
    ASSERT(compiler->pending_deoptimization_env_ == nullptr);
    Environment* env = compiler->SlowPathEnvironmentFor(instruction(), kNumSlowPathArgs);
    compiler->pending_deoptimization_env_ = env;

    const bool has_frame = compiler->flow_graph().graph_entry()->NeedsFrame();
    if (using_shared_stub) {
      if (!has_frame) {
        ASSERT(__ constant_pool_allowed());
        __ set_constant_pool_allowed(false);
        __ EnterDartFrame(0);
      }
      const uword entry_point_offset = compiler::target::Thread::
          stack_overflow_shared_stub_entry_point_offset(
              instruction()->locs()->live_registers()->FpuRegisterCount() > 0);
      __ Call(compiler::Address(THR, entry_point_offset));
      compiler->RecordSafepoint(instruction()->locs(), kNumSlowPathArgs);
      compiler->RecordCatchEntryMoves(env);
      compiler->AddCurrentDescriptor(UntaggedPcDescriptors::kOther,
                                     instruction()->deopt_id(),
                                     instruction()->source());
      if (!has_frame) {
        __ LeaveDartFrame();
        __ set_constant_pool_allowed(true);
      }
    } else {
      ASSERT(has_frame);
      __ CallRuntime(kInterruptOrStackOverflowRuntimeEntry, kNumSlowPathArgs);
      compiler->EmitCallsiteMetadata(
          instruction()->source(), instruction()->deopt_id(),
          UntaggedPcDescriptors::kOther, instruction()->locs(), env);
    }

    if (compiler->isolate_group()->use_osr() && !compiler->is_optimizing() &&
        instruction()->in_loop()) {
      // In unoptimized code, record loop stack checks as possible OSR entries.
      compiler->AddCurrentDescriptor(UntaggedPcDescriptors::kOsrEntry,
                                     instruction()->deopt_id(),
                                     InstructionSource());
    }
    compiler->pending_deoptimization_env_ = nullptr;
    if (!using_shared_stub) {
      compiler->RestoreLiveRegisters(instruction()->locs());
    }
    __ b(exit_label());
  }

  compiler::Label* osr_entry_label() {
    ASSERT(IsolateGroup::Current()->use_osr());
    return &osr_entry_label_;
  }

 private:
  compiler::Label osr_entry_label_;
};

void CheckStackOverflowInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  __ Comment("CheckStackOverflowInstr");
  CheckStackOverflowSlowPath* slow_path = new CheckStackOverflowSlowPath(this);
  compiler->AddSlowPathCode(slow_path);

  __ lw(CMPRES1, compiler::Address(THR, Thread::stack_limit_offset()));
  __ BranchUnsignedLessEqual(SP, CMPRES1, slow_path->entry_label());
  if (compiler->CanOSRFunction() && in_loop()) {
    const Register function = locs()->temp(0).reg();
    const Register count = locs()->temp(1).reg();
    // In unoptimized code check the usage counter to trigger OSR at loop
    // stack checks.  Use progressively higher thresholds for more deeply
    // nested loops to attempt to hit outer loops with OSR when possible.
    __ LoadObject(function, compiler->parsed_function().function());
    const intptr_t configured_optimization_counter_threshold =
        compiler->thread()->isolate_group()->optimization_counter_threshold();
    const int32_t threshold =
        configured_optimization_counter_threshold * (loop_depth() + 1);
    __ lw(count,
          compiler::FieldAddress(
              function, compiler::target::Function::usage_counter_offset()));
    __ AddImmediate(count, count, 1);
    __ sw(count,
          compiler::FieldAddress(
              function, compiler::target::Function::usage_counter_offset()));
    __ BranchSignedGreaterEqual(count, compiler::Immediate(threshold),
                                slow_path->osr_entry_label());
  }
  if (compiler->ForceSlowPathForStackOverflow()) {
    __ b(slow_path->entry_label());
  }
  __ Bind(slow_path->exit_label());
}

LocationSummary* BinarySmiOpInstr::MakeLocationSummary(Zone* zone,
                                                       bool opt) const {
  const intptr_t kNumInputs = 2;
  const intptr_t kNumTemps =
      ((op_kind() == Token::kMOD) || (op_kind() == Token::kTRUNCDIV) ||
       ((op_kind() == Token::kSHL) && can_overflow()) ||
        (op_kind() == Token::kSHR) || (op_kind() == Token::kUSHR))
          ? 1
          : 0;
  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  if (op_kind() == Token::kTRUNCDIV) {
    summary->set_in(0, Location::RequiresRegister());
    if (RightOperandIsPowerOfTwoConstant()) {
      ConstantInstr* right_constant = right()->definition()->AsConstant();
      summary->set_in(1, Location::Constant(right_constant));
    } else {
      summary->set_in(1, Location::RequiresRegister());
    }
    summary->set_temp(0, Location::RequiresRegister());
    summary->set_out(0, Location::RequiresRegister());
    return summary;
  }
  if (op_kind() == Token::kMOD) {
    summary->set_in(0, Location::RequiresRegister());
    summary->set_in(1, Location::RequiresRegister());
    summary->set_temp(0, Location::RequiresRegister());
    summary->set_out(0, Location::RequiresRegister());
    return summary;
  }
  summary->set_in(0, Location::RequiresRegister());
  summary->set_in(1, LocationRegisterOrSmiConstant(right()));
  if (((op_kind() == Token::kSHL) && can_overflow()) ||
      (op_kind() == Token::kSHR) || (op_kind() == Token::kUSHR)) {
    summary->set_temp(0, Location::RequiresRegister());
  }
  // We make use of 3-operand instructions by not requiring result register
  // to be identical to first input register as on Intel.
  summary->set_out(0, Location::RequiresRegister());
  return summary;
}

void BinarySmiOpInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  __ Comment("BinarySmiOpInstr");
  if (op_kind() == Token::kSHL) {
    EmitSmiShiftLeft(compiler, this);
    return;
  }

  Register left = locs()->in(0).reg();
  Register result = locs()->out(0).reg();
  compiler::Label* deopt = nullptr;
  if (CanDeoptimize()) {
    deopt = compiler->AddDeoptStub(deopt_id(), ICData::kDeoptBinarySmiOp);
  }

  if (locs()->in(1).IsConstant()) {
    const Object& constant = locs()->in(1).constant();
    ASSERT(constant.IsSmi());
    const int32_t imm = compiler::target::ToRawSmi(constant);
    switch (op_kind()) {
      case Token::kADD: {
        if (deopt == nullptr) {
          __ AddImmediate(result, left, imm);
        } else {
          __ AddImmediateBranchOverflow(result, left, imm, deopt);
        }
        break;
      }
      case Token::kSUB: {
        __ Comment("kSUB imm");
        if (deopt == nullptr) {
          __ AddImmediate(result, left, -imm);
        } else {
          // Negating imm and using AddImmediateSetFlags would not detect the
          // overflow when imm == kMinInt32.
          __ SubtractImmediateBranchOverflow(result, left, imm, deopt);
        }
        break;
      }
      case Token::kMUL: {
        // Keep left value tagged and untag right value.
        const intptr_t value = Smi::Cast(constant).Value();
        __ LoadImmediate(TMP, value);
        __ mult(left, TMP);
        __ mflo(result);
        if (deopt != nullptr) {
          __ mfhi(CMPRES2);
          __ sra(CMPRES1, result, 31);
          __ bne(CMPRES1, CMPRES2, deopt);
        }
        break;
      }
      case Token::kTRUNCDIV: {
        const intptr_t value = Smi::Cast(constant).Value();
        ASSERT(value != kIntptrMin);
        ASSERT(Utils::IsPowerOfTwo(Utils::Abs(value)));
        const intptr_t shift_count =
            Utils::ShiftForPowerOfTwo(Utils::Abs(value)) + kSmiTagSize;
        ASSERT(kSmiTagSize == 1);
        __ sra(TMP, left, 31);
        ASSERT(shift_count > 1);  // 1, -1 case handled above.
        Register temp = locs()->temp(0).reg();
        __ srl(TMP, TMP, 32 - shift_count);
        __ addu(temp, left, TMP);
        ASSERT(shift_count > 0);
        __ sra(result, temp, shift_count);
        if (value < 0) {
          __ subu(result, ZR, result);
        }
        __ SmiTag(result);
        break;
      }
      case Token::kBIT_AND: {
        // No overflow check.
        __ AndImmediate(result, left, imm);
        break;
      }
      case Token::kBIT_OR: {
        // No overflow check.
        __ OrImmediate(result, left, imm);
        break;
      }
      case Token::kBIT_XOR: {
        // No overflow check.
        __ XorImmediate(result, left, imm);
        break;
      }
      case Token::kSHR: {
        // sarl operation masks the count to 5 bits.
        const intptr_t kCountLimit = 0x1F;
        const intptr_t value = compiler::target::SmiValue(constant);
        __ Comment("kSHR");
        __ sra(result, left, Utils::Minimum(value + kSmiTagSize, kCountLimit));
        __ SmiTag(result);
        break;
      }
      case Token::kUSHR: {
        __ Comment("kUSHR");
        const intptr_t value = compiler::target::SmiValue(constant);
        ASSERT((value > 0) && (value < 64));
        COMPILE_ASSERT(compiler::target::kSmiBits < 32);
        // 64-bit representation of left operand value:
        //
        //       ss...sssss  s  s  xxxxxxxxxxxxx
        //       |        |  |  |  |           |
        //       63      32  31 30 kSmiBits-1  0
        //
        // Where 's' is a sign bit.
        //
        // If left operand is negative (sign bit is set), then
        // result will fit into Smi range if and only if
        // the shift amount >= 64 - kSmiBits.
        //
        // If left operand is non-negative, the result always
        // fits into Smi range.
        //
        if (value < (64 - compiler::target::kSmiBits)) {
          if (deopt != nullptr) {
            __ bltz(left, deopt);
          } else {
            // Operation cannot overflow only if left value is always
            // non-negative.
            ASSERT(!can_overflow());
          }
          // At this point left operand is non-negative, so unsigned shift
          // can't overflow.
          if (value >= compiler::target::kSmiBits) {
            __ LoadImmediate(result, 0);
          } else {
            __ srl(result, left, value + kSmiTagSize);
            __ SmiTag(result);
          }
        } else {
          // Shift amount > 32, and the result is guaranteed to fit into Smi.
          // Low (Smi) part of the left operand is shifted out.
          // High part is filled with sign bits.
          __ sra(result, left, 31);
          __ srl(result, result, value - 32);
          __ SmiTag(result);
        }
        break;
      }
      default:
        UNREACHABLE();
        break;
    }
    return;
  }

  Register right = locs()->in(1).reg();
  switch (op_kind()) {
    case Token::kADD: {
      if (deopt == nullptr) {
        __ addu(result, left, right);
      } else if (RangeUtils::IsPositive(right_range())) {
        ASSERT(result != left);
        __ addu(result, left, right);
        __ BranchSignedLess(result, left, deopt);
      } else if (RangeUtils::IsNegative(right_range())) {
        ASSERT(result != left);
        __ addu(result, left, right);
        __ BranchSignedGreater(result, left, deopt);
      } else {
        __ AddBranchOverflow(result, left, right, deopt);
      }
      break;
    }
    case Token::kSUB: {
      __ Comment("kSUB");
      if (deopt == nullptr) {
        __ subu(result, left, right);
      } else if (RangeUtils::IsPositive(right_range())) {
        ASSERT(result != left);
        __ subu(result, left, right);
        __ BranchSignedGreater(result, left, deopt);
      } else if (RangeUtils::IsNegative(right_range())) {
        ASSERT(result != left);
        __ subu(result, left, right);
        __ BranchSignedLess(result, left, deopt);
      } else {
        __ SubtractBranchOverflow(result, left, right, deopt);
      }
      break;
    }
    case Token::kMUL: {
      __ Comment("kMUL");
      __ SmiUntag(TMP, left);
      __ mult(TMP, right);
      __ mflo(result);
      if (deopt != nullptr) {
        __ mfhi(CMPRES2);
        __ sra(CMPRES1, result, 31);
        __ bne(CMPRES1, CMPRES2, deopt);
      }
      break;
    }
    case Token::kBIT_AND: {
      // No overflow check.
      __ and_(result, left, right);
      break;
    }
    case Token::kBIT_OR: {
      // No overflow check.
      __ or_(result, left, right);
      break;
    }
    case Token::kBIT_XOR: {
      // No overflow check.
      __ xor_(result, left, right);
      break;
    }
    case Token::kTRUNCDIV: {
      if (RangeUtils::CanBeZero(right_range())) {
        // Handle divide by zero in runtime.
        __ beq(right, ZR, deopt);
      }
      Register temp = locs()->temp(0).reg();
      __ SmiUntag(temp, left);
      __ SmiUntag(TMP, right);
      __ div(temp, TMP);
      __ mflo(temp);
      __ SmiTag(result, temp);

      if (RangeUtils::Overlaps(right_range(), -1, -1)) {
        // Check the corner case of dividing the 'MIN_SMI' with -1, in which
        // case we cannot tag the result.
        __ SmiUntag(TMP, result);
        __ bne(temp, TMP, deopt);
      }
      break;
    }
    case Token::kMOD: {
      if (RangeUtils::CanBeZero(right_range())) {
        // Handle divide by zero in runtime.
        __ beq(right, ZR, deopt);
      }
      Register temp = locs()->temp(0).reg();
      __ SmiUntag(temp, left);
      __ SmiUntag(TMP, right);
      __ div(temp, TMP);
      __ mfhi(result);
      //  res = left % right;
      //  if (res < 0) {
      //    if (right < 0) {
      //      res = res - right;
      //    } else {
      //      res = res + right;
      //    }
      //  }
      compiler::Label done, adjust;
      __ bgez(result, &done);
      // Result is negative, adjust it.
      __ bgez(right, &adjust);
      __ subu(result, result, TMP);
      __ b(&done);
      __ Bind(&adjust);
      __ addu(result, result, TMP);
      __ Bind(&done);
      __ SmiTag(result);
      break;
    }
    case Token::kSHR: {
      Register temp = locs()->temp(0).reg();
      if (CanDeoptimize()) {
        __ bltz(right, deopt);
      }
      __ SmiUntag(temp, right);
      // sra operation masks the count to 5 bits.
      const intptr_t kCountLimit = 0x1F;
      if (!RangeUtils::OnlyLessThanOrEqualTo(right_range(), kCountLimit)) {
        compiler::Label ok;
        __ BranchSignedLessEqual(temp, compiler::Immediate(kCountLimit), &ok);
        __ LoadImmediate(temp, kCountLimit);
        __ Bind(&ok);
      }
      __ SmiUntag(CMPRES1, left);
      __ srav(result, CMPRES1, temp);
      __ SmiTag(result);
      break;
    }
    case Token::kUSHR: {
      compiler::Label done;
      __ SmiUntag(TMP, right);
      // 64-bit representation of left operand value:
      //
      //       ss...sssss  s  s  xxxxxxxxxxxxx
      //       |        |  |  |  |           |
      //       63      32  31 30 kSmiBits-1  0
      //
      // Where 's' is a sign bit.
      //
      // If left operand is negative (sign bit is set), then
      // result will fit into Smi range if and only if
      // the shift amount >= 64 - kSmiBits.
      //
      // If left operand is non-negative, the result always
      // fits into Smi range.
      //
      if (!RangeUtils::OnlyLessThanOrEqualTo(
              right_range(), 64 - compiler::target::kSmiBits - 1)) {
        if (!RangeUtils::OnlyLessThanOrEqualTo(right_range(),
                                                kBitsPerInt64 - 1)) {
          ASSERT(result != left);
          ASSERT(result != right);
          __ LoadImmediate(result, 0);
          // If shift amount >= 64, then result is 0.
          __ BranchSignedGreaterEqual(TMP,
                    compiler::Immediate(kBitsPerInt64), &done);
        }
        // Shift amount >= 64 - kSmiBits > 32, but < 64.
        // Result is guaranteed to fit into Smi range.
        // Low (Smi) part of the left operand is shifted out.
        // High part is filled with sign bits.
        compiler::Label next;
        __ BranchSignedLess(TMP,
              compiler::Immediate(64 - compiler::target::kSmiBits), &next);
        __ addi(TMP, TMP, compiler::Immediate(-32));
        __ sra(result, left, 31);
        __ srlv(result, result, TMP);
        __ SmiTag(result);
        __ b(&done);
        __ Bind(&next);
      }
      // Shift amount < 64 - kSmiBits.
      // If left is negative, then result will not fit into Smi range.
      // Also deopt in case of negative shift amount.
      if (deopt != nullptr) {
        __ bltz(left, deopt);
        __ bltz(right, deopt);
      } else {
        ASSERT(!can_overflow());
      }
      // At this point left operand is non-negative, so unsigned shift
      // can't overflow.
      if (!RangeUtils::OnlyLessThanOrEqualTo(right_range(),
                                              compiler::target::kSmiBits - 1)) {
        ASSERT(result != left);
        ASSERT(result != right);
        __ LoadImmediate(result, 0);
        // Left operand >= 0, shift amount >= kSmiBits. Result is 0.
        __ BranchSignedGreaterEqual(TMP,
                compiler::Immediate(compiler::target::kSmiBits), &done);
      }
      // Left operand >= 0, shift amount < kSmiBits < 32.
      const Register temp = locs()->temp(0).reg();
      __ SmiUntag(temp, left);
      __ srlv(result, temp, TMP);
      __ SmiTag(result);
      __ Bind(&done);
      break;
    }
    case Token::kDIV: {
      // Dispatches to 'Double./'.
      UNREACHABLE();
      break;
    }
    case Token::kOR:
    case Token::kAND: {
      // Flow graph builder has dissected this operation to guarantee correct
      // behavior (short-circuit evaluation).
      UNREACHABLE();
      break;
    }
    default:
      UNREACHABLE();
      break;
  }
}

LocationSummary* CheckEitherNonSmiInstr::MakeLocationSummary(Zone* zone,
                                                             bool opt) const {
  intptr_t left_cid = left()->Type()->ToCid();
  intptr_t right_cid = right()->Type()->ToCid();
  ASSERT((left_cid != kDoubleCid) && (right_cid != kDoubleCid));
  const intptr_t kNumInputs = 2;
  const intptr_t kNumTemps = 0;
  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  summary->set_in(0, Location::RequiresRegister());
  summary->set_in(1, Location::RequiresRegister());
  return summary;
}

void CheckEitherNonSmiInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  compiler::Label* deopt =
      compiler->AddDeoptStub(deopt_id(), ICData::kDeoptBinaryDoubleOp);
  intptr_t left_cid = left()->Type()->ToCid();
  intptr_t right_cid = right()->Type()->ToCid();
  Register left = locs()->in(0).reg();
  Register right = locs()->in(1).reg();
  if (this->left()->definition() == this->right()->definition()) {
    __ AndImmediate(CMPRES1, left, kSmiTagMask);
  } else if (left_cid == kSmiCid) {
    __ AndImmediate(CMPRES1, right, kSmiTagMask);
  } else if (right_cid == kSmiCid) {
    __ AndImmediate(CMPRES1, left, kSmiTagMask);
  } else {
    __ or_(TMP, left, right);
    __ AndImmediate(CMPRES1, TMP, kSmiTagMask);
  }
  __ beq(CMPRES1, ZR, deopt);
}

LocationSummary* UnboxInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  ASSERT(BoxCid() != kSmiCid);
  const bool needs_temp = CanDeoptimize();
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = needs_temp ? 1 : 0;
  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  summary->set_in(0, Location::RequiresRegister());
  if (needs_temp) {
    summary->set_temp(0, Location::RequiresRegister());
  }
  if (representation() == kUnboxedInt64) {
    summary->set_out(0, Location::Pair(Location::RequiresRegister(),
                                       Location::RequiresRegister()));
  } else if (representation() == kUnboxedInt32) {
    summary->set_out(0, Location::RequiresRegister());
  } else {
    summary->set_out(0, Location::RequiresFpuRegister());
  }
  return summary;
}

void UnboxInstr::EmitLoadFromBox(FlowGraphCompiler* compiler) {
  const Register box = locs()->in(0).reg();

  switch (representation()) {
    case kUnboxedInt64: {
      PairLocation* result = locs()->out(0).AsPairLocation();
      ASSERT(result->At(0).reg() != box);
      __ LoadFromOffset(result->At(0).reg(), box,
                        ValueOffset() - kHeapObjectTag);
      __ LoadFromOffset(result->At(1).reg(), box,
                        ValueOffset() - kHeapObjectTag + compiler::target::kWordSize);
      break;
    }
    case kUnboxedDouble: {
      const DRegister result = locs()->out(0).fpu_reg();
      __ LoadDFromOffset(result, box, Double::value_offset() - kHeapObjectTag);
      break;
    }
    case kUnboxedFloat: {
      const DRegister result = locs()->out(0).fpu_reg();
      __ LoadDFromOffset(result, box, ValueOffset() - kHeapObjectTag);
      __ cvtsd(EvenFRegisterOf(result), result);
      break;
    }
    case kUnboxedFloat32x4:
    case kUnboxedFloat64x2:
    case kUnboxedInt32x4: {
      UNIMPLEMENTED();
      break;
    }
    default:
      UNREACHABLE();
      break;
  }
}

void UnboxInstr::EmitSmiConversion(FlowGraphCompiler* compiler) {
  const Register box = locs()->in(0).reg();

  switch (representation()) {
    case kUnboxedInt64: {
      PairLocation* result = locs()->out(0).AsPairLocation();
      __ SmiUntag(result->At(0).reg(), box);
      __ sra(result->At(1).reg(), result->At(0).reg(), 31);
      break;
    }
    case kUnboxedDouble: {
      const DRegister result = locs()->out(0).fpu_reg();
      __ SmiUntag(TMP, box);
      __ mtc1(TMP, STMP1);
      __ cvtdw(result, STMP1);
      break;
    }
    default:
      UNREACHABLE();
      break;
  }
}

void UnboxInstr::EmitLoadInt32FromBoxOrSmi(FlowGraphCompiler* compiler) {
  const Register value = locs()->in(0).reg();
  const Register result = locs()->out(0).reg();
  __ LoadInt32FromBoxOrSmi(result, value);
}

void UnboxInstr::EmitLoadInt64FromBoxOrSmi(FlowGraphCompiler* compiler) {
  const Register box = locs()->in(0).reg();
  PairLocation* result = locs()->out(0).AsPairLocation();
  ASSERT(result->At(0).reg() != box);
  ASSERT(result->At(1).reg() != box);
  compiler::Label done;
  __ sra(result->At(1).reg(), box, 31);
  __ SmiUntag(result->At(0).reg(), box);
  __ BranchIfSmi(box, &done);
  EmitLoadFromBox(compiler);
  __ Bind(&done);
}

LocationSummary* BoxInteger32Instr::MakeLocationSummary(Zone* zone,
                                                        bool opt) const {
  ASSERT((from_representation() == kUnboxedInt32) ||
         (from_representation() == kUnboxedUint32));
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 1;
  LocationSummary* summary = new (zone) LocationSummary(
      zone, kNumInputs, kNumTemps, LocationSummary::kCallOnSlowPath);
  summary->set_in(0, Location::RequiresRegister());
  summary->set_temp(0, Location::RequiresRegister());
  summary->set_out(0, Location::RequiresRegister());
  return summary;
}

void BoxInteger32Instr::EmitNativeCode(FlowGraphCompiler* compiler) {
  Register value = locs()->in(0).reg();
  Register out = locs()->out(0).reg();
  ASSERT(value != out);

  __ SmiTag(out, value);
  if (!ValueFitsSmi()) {
    Register temp = locs()->temp(0).reg();
    compiler::Label done;
    if (from_representation() == kUnboxedInt32) {
      __ SmiUntag(CMPRES1, out);
      __ BranchEqual(CMPRES1, value, &done);
    } else {
      ASSERT(from_representation() == kUnboxedUint32);
      __ AndImmediate(CMPRES1, value, 0xC0000000);
      __ BranchEqual(CMPRES1, ZR, &done);
    }
    BoxAllocationSlowPath::Allocate(compiler, this, compiler->mint_class(), out,
                                    temp);
    Register hi;
    if (from_representation() == kUnboxedInt32) {
      hi = temp;
      __ sra(hi, value, kBitsPerWord - 1);
    } else {
      ASSERT(from_representation() == kUnboxedUint32);
      hi = ZR;
    }
    __ StoreToOffset(value, out, Mint::value_offset() - kHeapObjectTag);
    __ StoreToOffset(hi, out,
                     Mint::value_offset() - kHeapObjectTag + kWordSize);
    __ Bind(&done);
  }
}

LocationSummary* BoxInt64Instr::MakeLocationSummary(Zone* zone,
                                                    bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = ValueFitsSmi() ? 0 : 1;
  // Shared slow path is used in BoxInt64Instr::EmitNativeCode in
  // precompiled mode and only after VM isolate stubs where
  // replaced with isolate-specific stubs.
  auto object_store = IsolateGroup::Current()->object_store();
  const bool stubs_in_vm_isolate =
      object_store->allocate_mint_with_fpu_regs_stub()
          ->untag()
          ->InVMIsolateHeap() ||
      object_store->allocate_mint_without_fpu_regs_stub()
          ->untag()
          ->InVMIsolateHeap();
  const bool shared_slow_path_call =
      SlowPathSharingSupported(opt) && !stubs_in_vm_isolate;
  LocationSummary* summary = new (zone) LocationSummary(
      zone, kNumInputs, kNumTemps,
      ValueFitsSmi()
          ? LocationSummary::kNoCall
          : ((shared_slow_path_call ? LocationSummary::kCallOnSharedSlowPath
                                    : LocationSummary::kCallOnSlowPath)));
  summary->set_in(0, Location::Pair(Location::RequiresRegister(),
                                    Location::RequiresRegister()));
  if (ValueFitsSmi()) {
    summary->set_out(0, Location::RequiresRegister());
  } else if (shared_slow_path_call) {
    summary->set_out(0,
                     Location::RegisterLocation(AllocateMintABI::kResultReg));
    summary->set_temp(0, Location::RegisterLocation(AllocateMintABI::kTempReg));
  } else {
    summary->set_out(0, Location::RequiresRegister());
    summary->set_temp(0, Location::RequiresRegister());
  }
  return summary;
}

void BoxInt64Instr::EmitNativeCode(FlowGraphCompiler* compiler) {
  if (ValueFitsSmi()) {
    PairLocation* value_pair = locs()->in(0).AsPairLocation();
    Register value_lo = value_pair->At(0).reg();
    Register out_reg = locs()->out(0).reg();
    __ SmiTag(out_reg, value_lo);
    return;
  }

  PairLocation* value_pair = locs()->in(0).AsPairLocation();
  Register value_lo = value_pair->At(0).reg();
  Register value_hi = value_pair->At(1).reg();
  Register tmp = locs()->temp(0).reg();
  Register out_reg = locs()->out(0).reg();

  compiler::Label not_smi, done;
  __ SmiTag(out_reg, value_lo);
  __ SmiUntag(tmp, out_reg);
  __ bne(tmp, value_lo, &not_smi);
  __ delay_slot()->sra(tmp, out_reg, 31);
  __ beq(tmp, value_hi, &done);

  __ Bind(&not_smi);

  if (compiler->intrinsic_mode()) {
    __ TryAllocate(compiler->mint_class(),
                   compiler->intrinsic_slow_path_label(),
                   compiler::Assembler::kNearJump, out_reg, tmp);
  } else if (locs()->call_on_shared_slow_path()) {
    const bool has_frame = compiler->flow_graph().graph_entry()->NeedsFrame();
    if (!has_frame) {
      ASSERT(__ constant_pool_allowed());
      __ set_constant_pool_allowed(false);
      __ EnterDartFrame(0);
    }
    auto object_store = compiler->isolate_group()->object_store();
    const bool live_fpu_regs = locs()->live_registers()->FpuRegisterCount() > 0;
    const auto& stub = Code::ZoneHandle(
        compiler->zone(),
        live_fpu_regs ? object_store->allocate_mint_with_fpu_regs_stub()
                      : object_store->allocate_mint_without_fpu_regs_stub());

    ASSERT(!locs()->live_registers()->ContainsRegister(
        AllocateMintABI::kResultReg));
    auto extended_env = compiler->SlowPathEnvironmentFor(this, 0);
    compiler->GenerateStubCall(source(), stub, UntaggedPcDescriptors::kOther,
                               locs(), DeoptId::kNone, extended_env);
    if (!has_frame) {
      __ LeaveDartFrame();
      __ set_constant_pool_allowed(true);
    }
  } else {
    BoxAllocationSlowPath::Allocate(compiler, this, compiler->mint_class(),
                                    out_reg, tmp);
  }

  __ StoreToOffset(value_lo, out_reg, Mint::value_offset() - kHeapObjectTag);
  __ StoreToOffset(value_hi, out_reg,
                   Mint::value_offset() - kHeapObjectTag + kWordSize);
  __ Bind(&done);
}

LocationSummary* UnboxInteger32Instr::MakeLocationSummary(Zone* zone,
                                                          bool opt) const {
  ASSERT((representation() == kUnboxedInt32) ||
         (representation() == kUnboxedUint32));
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  summary->set_in(0, Location::RequiresRegister());
  summary->set_out(0, Location::RequiresRegister());
  return summary;
}

static void LoadInt32FromMint(FlowGraphCompiler* compiler,
                              Register mint,
                              Register result,
                              compiler::Label* deopt) {
  __ LoadFieldFromOffset(result, mint, Mint::value_offset());
  if (deopt != NULL) {
    __ LoadFieldFromOffset(CMPRES1, mint, Mint::value_offset() + kWordSize);
    __ sra(CMPRES2, result, kBitsPerWord - 1);
    __ BranchNotEqual(CMPRES1, CMPRES2, deopt);
  }
}

void UnboxInteger32Instr::EmitNativeCode(FlowGraphCompiler* compiler) {
  const intptr_t value_cid = value()->Type()->ToCid();
  const Register value = locs()->in(0).reg();
  const Register out = locs()->out(0).reg();
  compiler::Label* deopt =
      CanDeoptimize()
          ? compiler->AddDeoptStub(GetDeoptId(), ICData::kDeoptUnboxInteger)
          : NULL;
  compiler::Label* out_of_range = !is_truncating() ? deopt : NULL;
  ASSERT(value != out);

  if (value_cid == kSmiCid) {
    __ SmiUntag(out, value);
  } else if (value_cid == kMintCid) {
    LoadInt32FromMint(compiler, value, out, out_of_range);
  } else if (!CanDeoptimize()) {
    compiler::Label done;
    __ SmiUntag(out, value);
    __ AndImmediate(CMPRES1, value, kSmiTagMask);
    __ beq(CMPRES1, ZR, &done);
    LoadInt32FromMint(compiler, value, out, NULL);
    __ Bind(&done);
  } else {
    compiler::Label done;
    __ SmiUntag(out, value);
    __ AndImmediate(CMPRES1, value, kSmiTagMask);
    __ beq(CMPRES1, ZR, &done);
    __ LoadClassId(CMPRES1, value);
    __ BranchNotEqual(CMPRES1, compiler::Immediate(kMintCid), deopt);
    LoadInt32FromMint(compiler, value, out, out_of_range);
    __ Bind(&done);
  }
}

LocationSummary* BinaryDoubleOpInstr::MakeLocationSummary(Zone* zone,
                                                          bool opt) const {
  const intptr_t kNumInputs = 2;
  const intptr_t kNumTemps = 0;
  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  summary->set_in(0, Location::RequiresFpuRegister());
  summary->set_in(1, Location::RequiresFpuRegister());
  summary->set_out(0, Location::RequiresFpuRegister());
  return summary;
}

void BinaryDoubleOpInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  DRegister left = locs()->in(0).fpu_reg();
  DRegister right = locs()->in(1).fpu_reg();
  DRegister result = locs()->out(0).fpu_reg();
  switch (op_kind()) {
    case Token::kADD:
      __ addd(result, left, right);
      break;
    case Token::kSUB:
      __ subd(result, left, right);
      break;
    case Token::kMUL:
      __ muld(result, left, right);
      break;
    case Token::kDIV:
      __ divd(result, left, right);
      break;
    default:
      UNREACHABLE();
  }
}

LocationSummary* DoubleTestOpInstr::MakeLocationSummary(Zone* zone,
                                                        bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  summary->set_in(0, Location::RequiresFpuRegister());
  summary->set_out(0, Location::RequiresRegister());
  return summary;
}

Condition DoubleTestOpInstr::EmitConditionCode(FlowGraphCompiler* compiler,
                                                BranchLabels labels) {
  ASSERT(compiler->is_optimizing());
  const DRegister value = locs()->in(0).fpu_reg();
  const bool is_negated = kind() != Token::kEQ;
  if (op_kind() == MethodRecognizer::kDouble_getIsNaN) {
    __ cund(value, value);
    if (labels.fall_through == labels.true_label) {
      if (is_negated) {
        __ bc1t(labels.false_label);
      } else {
        __ bc1f(labels.false_label);
      }
    } else if (labels.fall_through == labels.false_label) {
      if (is_negated) {
        __ bc1f(labels.true_label);
      } else {
        __ bc1t(labels.true_label);
      }
    } else {
      if (is_negated) {
        __ bc1t(labels.false_label);
      } else {
        __ bc1f(labels.false_label);
      }
      __ b(labels.true_label);
    }
    return kInvalidCondition;  // Unused.
  } else if(op_kind() == MethodRecognizer::kDouble_getIsInfinite){
    __ mfc1(CMPRES1, EvenFRegisterOf(value));
    // If the low word isn't zero, then it isn't infinity.
    __ bne(CMPRES1, ZR, is_negated ? labels.true_label : labels.false_label);
    __ mfc1(CMPRES1, OddFRegisterOf(value));
    // Mask off the sign bit.
    __ AndImmediate(CMPRES1, CMPRES1, 0x7FFFFFFF);
    // Compare with +infinity.
    __ LoadImmediate(CMPRES2, 0x7FF00000);
    __ CompareRegisters(CMPRES1, CMPRES2);
    return is_negated ? NE : EQ;
  } else if(op_kind() == MethodRecognizer::kDouble_getIsNegative){
    ASSERT(value!=FpuTMP);
    compiler::Label oddIsNotZero;
    __ cund(value, value);
    // If it's NaN, it's not negative.
    __ bc1t(is_negated ? labels.true_label : labels.false_label);

    // Move the high 32 bits of the double into a general-purpose register.
    __ mfc1(CMPRES1, OddFRegisterOf(value));

    // High word has sign bit = 1 for negative doubles.
    // Comparing with zero as int works because signed ints also use MSB as sign (two's complement).
    __ CompareRegisters(CMPRES1, ZR);
    return is_negated ? GE : LT;
  } else {
    UNREACHABLE();
  }
}

LocationSummary* SimdOpInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  UNIMPLEMENTED();
  return nullptr;
}

void SimdOpInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  UNIMPLEMENTED();
}

LocationSummary* MathMinMaxInstr::MakeLocationSummary(Zone* zone,
                                                      bool opt) const {
  if (representation() == kUnboxedDouble) {
    const intptr_t kNumInputs = 2;
    const intptr_t kNumTemps = 1;
    LocationSummary* summary = new (zone)
        LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
    summary->set_in(0, Location::RequiresFpuRegister());
    summary->set_in(1, Location::RequiresFpuRegister());
    // Reuse the left register so that code can be made shorter.
    summary->set_out(0, Location::SameAsFirstInput());
    summary->set_temp(0, Location::RequiresRegister());
    return summary;
  }
  ASSERT(representation() == kTagged);
  const intptr_t kNumInputs = 2;
  const intptr_t kNumTemps = 0;
  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  summary->set_in(0, Location::RequiresRegister());
  summary->set_in(1, Location::RequiresRegister());
  // Reuse the left register so that code can be made shorter.
  summary->set_out(0, Location::SameAsFirstInput());
  return summary;
}

void MathMinMaxInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  ASSERT((op_kind() == MethodRecognizer::kMathMin) ||
         (op_kind() == MethodRecognizer::kMathMax));
  const intptr_t is_min = (op_kind() == MethodRecognizer::kMathMin);
  if (representation() == kUnboxedDouble) {
    compiler::Label done, returns_nan, are_equal;
    DRegister left = locs()->in(0).fpu_reg();
    DRegister right = locs()->in(1).fpu_reg();
    DRegister result = locs()->out(0).fpu_reg();
    Register temp = locs()->temp(0).reg();
    __ cund(left, right);
    __ bc1t(&returns_nan);
    __ ceqd(left, right);
    __ bc1t(&are_equal);
    if (is_min) {
      __ coltd(left, right);
    } else {
      __ coltd(right, left);
    }
    ASSERT(left == result);
    __ bc1t(&done);
    __ movd(result, right);
    __ b(&done);

    __ Bind(&returns_nan);
    __ LoadImmediate(result, NAN);
    __ b(&done);

    __ Bind(&are_equal);
    compiler::Label left_is_negative;
    // Check for negative zero: -0.0 is equal 0.0 but min or max must return
    // -0.0 or 0.0 respectively.
    // Check for negative left value (get the sign bit):
    // - min -> left is negative ? left : right.
    // - max -> left is negative ? right : left
    // Check the sign bit.
    __ mfc1(temp, OddFRegisterOf(left));  // Moves bits 32...63 of left to temp.
    if (is_min) {
      ASSERT(left == result);
      __ bltz(temp, &done);  // Left is negative.
    } else {
      __ bgez(temp, &done);  // Left is positive.
    }
    __ movd(result, right);
    __ Bind(&done);
    return;
  }

  compiler::Label done;
  ASSERT(representation() == kTagged);
  Register left = locs()->in(0).reg();
  Register right = locs()->in(1).reg();
  Register result = locs()->out(0).reg();
  ASSERT(result == left);
  if (is_min) {
    __ BranchSignedLessEqual(left, right, &done);
  } else {
    __ BranchSignedGreaterEqual(left, right, &done);
  }
  __ mov(result, right);
  __ Bind(&done);
}

LocationSummary* UnarySmiOpInstr::MakeLocationSummary(Zone* zone,
                                                      bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  summary->set_in(0, Location::RequiresRegister());
  // We make use of 3-operand instructions by not requiring result register
  // to be identical to first input register as on Intel.
  summary->set_out(0, Location::RequiresRegister());
  return summary;
}

void UnarySmiOpInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  Register value = locs()->in(0).reg();
  Register result = locs()->out(0).reg();
  switch (op_kind()) {
    case Token::kNEGATE: {
      compiler::Label* deopt = compiler->AddDeoptStub(deopt_id(), ICData::kDeoptUnaryOp);
      __ SubtractBranchOverflow(result, ZR, value, deopt);
      break;
    }
    case Token::kBIT_NOT:
      __ nor(result, value, ZR);
      __ addiu(result, result, compiler::Immediate(-1));  // Remove inverted smi-tag.
      break;
    default:
      UNREACHABLE();
  }
}

LocationSummary* UnaryDoubleOpInstr::MakeLocationSummary(Zone* zone,
                                                         bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  summary->set_in(0, Location::RequiresFpuRegister());
  summary->set_out(0, Location::RequiresFpuRegister());
  return summary;
}

void UnaryDoubleOpInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  ASSERT(representation() == kUnboxedDouble);
  FpuRegister result = locs()->out(0).fpu_reg();
  FpuRegister value = locs()->in(0).fpu_reg();
  switch (op_kind()) {
    case Token::kNEGATE:
      __ negd(result, value);
      break;
    case Token::kSQRT:
      __ sqrtd(result, value);
      break;
    case Token::kSQUARE:
      __ muld(result, value, value);
      break;
    default:
      UNREACHABLE();
  }
}

LocationSummary* Int32ToDoubleInstr::MakeLocationSummary(Zone* zone,
                                                         bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* result = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  result->set_in(0, Location::RequiresRegister());
  result->set_out(0, Location::RequiresFpuRegister());
  return result;
}

void Int32ToDoubleInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  Register value = locs()->in(0).reg();
  FpuRegister result = locs()->out(0).fpu_reg();
  __ mtc1(value, STMP1);
  __ cvtdw(result, STMP1);
}

LocationSummary* SmiToDoubleInstr::MakeLocationSummary(Zone* zone,
                                                       bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* result = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  result->set_in(0, Location::RequiresRegister());
  result->set_out(0, Location::RequiresFpuRegister());
  return result;
}

void SmiToDoubleInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  Register value = locs()->in(0).reg();
  FpuRegister result = locs()->out(0).fpu_reg();
  __ SmiUntag(TMP, value);
  __ mtc1(TMP, STMP1);
  __ cvtdw(result, STMP1);
}

LocationSummary* Int64ToDoubleInstr::MakeLocationSummary(Zone* zone,
                                                        bool opt) const {
  UNIMPLEMENTED();
  return NULL;
}

void Int64ToDoubleInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  UNIMPLEMENTED();
}

LocationSummary* DoubleToIntegerInstr::MakeLocationSummary(Zone* zone,
                                                           bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* result = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kCallOnSlowPath);
  result->set_in(0, Location::RequiresFpuRegister());
  result->set_out(0, Location::RegisterLocation(V0));
  return result;
}

void DoubleToIntegerInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  const Register result = locs()->out(0).reg();
  const FpuRegister value_double = locs()->in(0).fpu_reg();

  DoubleToIntegerSlowPath* slow_path =
    new DoubleToIntegerSlowPath(this, value_double);
  compiler->AddSlowPathCode(slow_path);

  __ truncwd(STMP1, value_double);
  __ mfc1(result, STMP1);

  // Overflow is signaled with minint.

  // Check for overflow and that it fits into Smi.
  __ LoadImmediate(TMP, 0xC0000000);
  __ subu(CMPRES1, result, TMP);
  __ bltz(CMPRES1, slow_path->entry_label());
  __ SmiTag(result);
  __ Bind(slow_path->exit_label());
}

LocationSummary* DoubleToSmiInstr::MakeLocationSummary(Zone* zone,
                                                       bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* result = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  result->set_in(0, Location::RequiresFpuRegister());
  result->set_out(0, Location::RequiresRegister());
  return result;
}

void DoubleToSmiInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  compiler::Label* deopt = compiler->AddDeoptStub(deopt_id(), ICData::kDeoptDoubleToSmi);
  Register result = locs()->out(0).reg();
  DRegister value = locs()->in(0).fpu_reg();
  __ truncwd(STMP1, value);
  __ mfc1(result, STMP1);

  // Check for overflow and that it fits into Smi.
  __ LoadImmediate(TMP, 0xC0000000);
  __ subu(CMPRES1, result, TMP);
  __ bltz(CMPRES1, deopt);
  __ SmiTag(result);
}

LocationSummary* DoubleToFloatInstr::MakeLocationSummary(Zone* zone,
                                                         bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* result = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  result->set_in(0, Location::RequiresFpuRegister());
  result->set_out(0, Location::SameAsFirstInput());
  return result;
}

void DoubleToFloatInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  DRegister value = locs()->in(0).fpu_reg();
  FRegister result = EvenFRegisterOf(locs()->out(0).fpu_reg());
  __ cvtsd(result, value);
}

LocationSummary* FloatToDoubleInstr::MakeLocationSummary(Zone* zone,
                                                         bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* result = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  result->set_in(0, Location::RequiresFpuRegister());
  result->set_out(0, Location::SameAsFirstInput());
  return result;
}

void FloatToDoubleInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  FRegister value = EvenFRegisterOf(locs()->in(0).fpu_reg());
  DRegister result = locs()->out(0).fpu_reg();
  __ cvtds(result, value);
}

LocationSummary* FloatCompareInstr::MakeLocationSummary(Zone* zone,
                                                        bool opt) const {
  UNREACHABLE();
  return NULL;
}

void FloatCompareInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  UNREACHABLE();
}

LocationSummary* InvokeMathCFunctionInstr::MakeLocationSummary(Zone* zone,
                                                               bool opt) const {
  // Calling convention on MIPS uses D6 and D7 to pass the first two
  // double arguments.
  ASSERT((InputCount() == 1) || (InputCount() == 2));
  const intptr_t kNumTemps = 0;
  LocationSummary* result = new (zone)
      LocationSummary(zone, InputCount(), kNumTemps, LocationSummary::kCall);
  result->set_in(0, Location::FpuRegisterLocation(D6));
  if (InputCount() == 2) {
    result->set_in(1, Location::FpuRegisterLocation(D7));
  }
  result->set_out(0, Location::FpuRegisterLocation(D0));
  return result;
}

// Pseudo code:
// if (exponent == 0.0) return 1.0;
// // Speed up simple cases.
// if (exponent == 1.0) return base;
// if (exponent == 2.0) return base * base;
// if (exponent == 3.0) return base * base * base;
// if (base == 1.0) return 1.0;
// if (base.isNaN || exponent.isNaN) {
//    return double.NAN;
// }
// if (base != -Infinity && exponent == 0.5) {
//   if (base == 0.0) return 0.0;
//   return sqrt(value);
// }
static void InvokeDoublePow(FlowGraphCompiler* compiler,
                            InvokeMathCFunctionInstr* instr) {
  ASSERT(instr->recognized_kind() == MethodRecognizer::kMathDoublePow);
  const intptr_t kInputCount = 2;
  ASSERT(instr->InputCount() == kInputCount);
  LocationSummary* locs = instr->locs();

  DRegister base = locs->in(0).fpu_reg();
  DRegister exp = locs->in(1).fpu_reg();
  DRegister result = locs->out(0).fpu_reg();

  compiler::Label check_base, skip_call;
  __ LoadImmediate(DTMP, 0.0);
  __ LoadImmediate(result, 1.0);
  // exponent == 0.0 -> return 1.0;
  __ cund(exp, exp);
  __ bc1t(&check_base);  // NaN -> check base.
  __ ceqd(exp, DTMP);
  __ bc1t(&skip_call);  // exp is 0.0, result is 1.0.

  // exponent == 1.0 ?
  __ ceqd(exp, result);
  compiler::Label return_base;
  __ bc1t(&return_base);
  // exponent == 2.0 ?
  __ LoadImmediate(DTMP, 2.0);
  __ ceqd(exp, DTMP);
  compiler::Label return_base_times_2;
  __ bc1t(&return_base_times_2);
  // exponent == 3.0 ?
  __ LoadImmediate(DTMP, 3.0);
  __ ceqd(exp, DTMP);
  __ bc1f(&check_base);

  // base_times_3.
  __ muld(result, base, base);
  __ muld(result, result, base);
  __ b(&skip_call);

  __ Bind(&return_base);
  __ movd(result, base);
  __ b(&skip_call);

  __ Bind(&return_base_times_2);
  __ muld(result, base, base);
  __ b(&skip_call);

  __ Bind(&check_base);
  // Note: 'exp' could be NaN.
  // base == 1.0 -> return 1.0;
  __ cund(base, base);
  compiler::Label return_nan;
  __ bc1t(&return_nan);
  __ ceqd(base, result);
  __ bc1t(&skip_call);  // base and result are 1.0.

  __ cund(exp, exp);
  compiler::Label try_sqrt;
  __ bc1f(&try_sqrt);  // Neither 'exp' nor 'base' are NaN.

  __ Bind(&return_nan);
  __ LoadImmediate(result, NAN);
  __ b(&skip_call);

  __ Bind(&try_sqrt);
  // Before calling pow, check if we could use sqrt instead of pow.
  __ LoadImmediate(result, kNegInfinity);
  // base == -Infinity -> call pow;
  __ ceqd(base, result);
  compiler::Label do_pow;
  __ bc1t(&do_pow);

  // exponent == 0.5 ?
  __ LoadImmediate(DTMP, 0.5);
  __ ceqd(exp, DTMP);
  __ bc1f(&do_pow);

  // base > 0 check
  __ LoadImmediate(DTMP, 0.0);
  __ coled(base, DTMP);  // base <= 0.0?
  __ bc1t(&do_pow);      // If base <= 0 → fallback to pow

  __ sqrtd(result, base);
  __ b(&skip_call);

  __ Bind(&do_pow);
  {
    // double values are passed and returned in vfp registers.
    compiler::LeafRuntimeScope rt(compiler->assembler(),
                                  /*frame_size=*/0,
                                  /*preserve_registers=*/false);
    rt.Call(instr->TargetFunction(), kInputCount);
  }
  __ Bind(&skip_call);
}

void InvokeMathCFunctionInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  // For pow-function return NaN if exponent is NaN.
  if (recognized_kind() == MethodRecognizer::kMathDoublePow) {
    InvokeDoublePow(compiler, this);
    return;
  }
  compiler::LeafRuntimeScope rt(compiler->assembler(),
                                /*frame_size=*/0,
                                /*preserve_registers=*/false);
  rt.Call(TargetFunction(), TargetFunction().argument_count());
}

LocationSummary* TruncDivModInstr::MakeLocationSummary(Zone* zone,
                                                       bool opt) const {
  const intptr_t kNumInputs = 2;
  const intptr_t kNumTemps = 1;
  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  summary->set_in(0, Location::RequiresRegister());
  summary->set_in(1, Location::RequiresRegister());
  summary->set_temp(0, Location::RequiresRegister());
  // Output is a pair of registers.
  summary->set_out(0, Location::Pair(Location::RequiresRegister(),
                                     Location::RequiresRegister()));
  return summary;
}

void TruncDivModInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  ASSERT(CanDeoptimize());
  compiler::Label* deopt = compiler->AddDeoptStub(deopt_id(), ICData::kDeoptBinarySmiOp);
  Register left = locs()->in(0).reg();
  Register right = locs()->in(1).reg();
  Register temp = locs()->temp(0).reg();
  ASSERT(locs()->out(0).IsPairLocation());
  PairLocation* pair = locs()->out(0).AsPairLocation();
  Register result_div = pair->At(0).reg();
  Register result_mod = pair->At(1).reg();
  if (RangeUtils::CanBeZero(divisor_range())) {
    // Handle divide by zero in runtime.
    __ beq(right, ZR, deopt);
  }
  __ SmiUntag(temp, left);
  __ SmiUntag(TMP, right);
  __ div(temp, TMP);
  __ mflo(result_div);
  __ mfhi(result_mod);
  // Check the corner case of dividing the 'MIN_SMI' with -1, in which
  // case we cannot tag the result.
  __ BranchEqual(result_div, compiler::Immediate(0x40000000), deopt);
  //  res = left % right;
  //  if (res < 0) {
  //    if (right < 0) {
  //      res = res - right;
  //    } else {
  //      res = res + right;
  //    }
  //  }
  compiler::Label done;
  __ bgez(result_mod, &done);
  if (RangeUtils::Overlaps(divisor_range(), -1, 1)) {
    compiler::Label subtract;
    __ bltz(right, &subtract);
    __ addu(result_mod, result_mod, TMP);
    __ b(&done);
    __ Bind(&subtract);
    __ subu(result_mod, result_mod, TMP);
  } else if (divisor_range()->IsPositive()) {
    // Right is positive.
    __ addu(result_mod, result_mod, TMP);
  } else {
    // Right is negative.
    __ subu(result_mod, result_mod, TMP);
  }
  __ Bind(&done);

  __ SmiTag(result_div);
  __ SmiTag(result_mod);
}

// Should be kept in sync with integers.cc Multiply64Hash
static void EmitHashIntegerCodeSequence(FlowGraphCompiler* compiler,
                                        const Register result,
                                        const Register value_lo,
                                        const Register value_hi) {
// C=0x2d51 (multiplier constant)
// x_lo​=value_lo
// x_hi​=value_hi
//
// Product of the lower 32 bits:
// P=x_lo​⋅C(mod 2^64)
// P_lo​=P mod 2^32 (lower 32 bits)
// P_hi​=[P/2^32] (upper 32 bits)
//
// Product of the upper 32 bits:
// Q=x_hi​⋅C(mod 2^64)
// Q_lo​=Q mod 2^32 (lower 32 bits)
// Q_hi​=[Q/2^32] (upper 32 bits)
//
// Combining the products into the hash:
// M1​=(Q_lo​+P_hi​) mod 2^32
// M2​=Q_hi
// M0​=P_lo
//
// hash=(M2 xor M1 xor M0​) and 0x3FFFFFFF

  ASSERT(value_lo != TMP);
  ASSERT(value_hi != TMP);
  ASSERT(result != TMP);

  __ LoadImmediate(TMP, 0x2d51);
  __ multu(value_lo, TMP);
  __ mflo(value_lo);
  __ mfhi(result);
  __ multu(value_hi, TMP);
  __ mflo(value_hi);
  __ mfhi(TMP);
  __ addu(value_hi, value_hi, result);
  __ xor_(result, value_hi, TMP);
  __ xor_(result, result, value_lo);
  __ AndImmediate(result, result, 0x3fffffff);
}

LocationSummary* HashDoubleOpInstr::MakeLocationSummary(Zone* zone,
                                                        bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 3;
  LocationSummary* summary = new (zone) LocationSummary(
      zone, kNumInputs, kNumTemps, LocationSummary::kNativeLeafCall);

  summary->set_in(0, Location::RequiresFpuRegister());
  summary->set_temp(0, Location::RequiresRegister());
  summary->set_temp(1, Location::RequiresRegister());
  summary->set_temp(2, Location::RequiresFpuRegister());
  summary->set_out(0, Location::Pair(Location::RequiresRegister(),
                                     Location::RequiresRegister()));
  return summary;
}

void HashDoubleOpInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  const FpuRegister value = locs()->in(0).fpu_reg();
  const Register temp = locs()->temp(0).reg();
  const Register temp1 = locs()->temp(1).reg();
  const FpuRegister temp_double = locs()->temp(2).fpu_reg();
  const PairLocation* out_pair = locs()->out(0).AsPairLocation();
  Register result = out_pair->At(0).reg();
  Register result_hi = out_pair->At(1).reg();

  compiler::Label hash_double, hash_double_value, try_convert;

  __ mfc1(temp, static_cast<FRegister>(value * 2 + 1));
  __ AndImmediate(temp, temp, 0x7FF00000);
  __ LoadImmediate(TMP, 0x7FF00000);
  __ BranchEqual(temp, TMP, &hash_double_value);

  compiler::Label slow_path;
  __ Bind(&try_convert);
  // value -> temp1 -> temp_double
  __ truncwd(STMP1, value);
  __ mfc1(temp1, STMP1);
  // Checks whether temp1 is INT_MAX or INT_MIN which indicates failed truncwd
  __ BranchEqual(temp1, compiler::Immediate(0x7fffffff), &slow_path);
  __ BranchEqual(temp1, compiler::Immediate(0x80000000), &slow_path);

  __ mtc1(temp1, STMP1);
  __ cvtdw(temp_double, STMP1);

  // value != temp_double, then go to hash_double_value
  __ ceqd(value, temp_double);
  __ bc1f(&hash_double_value);
  // Sign-extend 32-bit [temp1] value to 64-bit pair of (temp:temp1), which
  // is used by integer hash code sequence.
  __ sra(temp, temp1, 31);

  compiler::Label hash_integer, done;
  {
    __ Bind(&hash_integer);
    // integer hash of (temp:temp1)
    EmitHashIntegerCodeSequence(compiler, result, temp1, temp);
    __ b(&done);
  }

  __ Bind(&slow_path);
  // double value is potentially doesn't fit into Smi range, so
  // do the double->int64->double via runtime call.
  __ StoreDToOffset(value, THR,
                    compiler::target::Thread::unboxed_runtime_arg_offset());
  {
    compiler::LeafRuntimeScope rt(compiler->assembler(), /*frame_size=*/0,
                                  /*preserve_registers=*/true);
    __ mov(A0, THR);
    // Check if double can be represented as int64, load it into (temp:EAX) if
    // it can.
    rt.Call(kTryDoubleAsIntegerRuntimeEntry, 1);
    __ mov(TMP, V0);
  }
  __ LoadFromOffset(temp1, THR,
                    compiler::target::Thread::unboxed_runtime_arg_offset());
  __ LoadFromOffset(temp, THR,
                    compiler::target::Thread::unboxed_runtime_arg_offset() +
                        compiler::target::kWordSize);
  __ BranchNotEqual(TMP, compiler::Immediate(0), &hash_integer);
  __ b(&hash_double);

  __ Bind(&hash_double_value);
  __ mfc1(temp, static_cast<FRegister>(value * 2));
  __ mfc1(temp1, static_cast<FRegister>(value * 2 + 1));

  __ Bind(&hash_double);
  // Convert the double bits (temp:temp1) to a hash code that fits in a Smi.
  __ xor_(result, temp1, temp);
  __ AndImmediate(result, result, compiler::target::kSmiMax);

  __ Bind(&done);
  __ xor_(result_hi, result_hi, result_hi);
}

LocationSummary* HashIntegerOpInstr::MakeLocationSummary(Zone* zone,
                                                         bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 1;
  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  summary->set_in(0, Location::WritableRegister());
  summary->set_out(0, Location::RequiresRegister());
  summary->set_temp(0, Location::RequiresRegister());
  return summary;
}

void HashIntegerOpInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  Register value = locs()->in(0).reg();
  Register result = locs()->out(0).reg();
  Register temp = locs()->temp(0).reg();

  if (smi_) {
    __ SmiUntag(value);
    __ sra(temp, value, 31);
  } else {
    __ LoadFieldFromOffset(temp, value,
                           Mint::value_offset() + compiler::target::kWordSize);
    __ LoadFieldFromOffset(value, value, Mint::value_offset());
  }
  EmitHashIntegerCodeSequence(compiler, result, value, temp);
  __ SmiTag(result);
}

LocationSummary* BranchInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  condition()->InitializeLocationSummary(zone, opt);
  // Branches don't produce a result.
  condition()->locs()->set_out(0, Location::NoLocation());
  return condition()->locs();
}

void BranchInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  __ Comment("BranchInstr");
  condition()->EmitBranchCode(compiler, this);
}

LocationSummary* CheckClassInstr::MakeLocationSummary(Zone* zone,
                                                      bool opt) const {
  const intptr_t kNumInputs = 1;
  const bool need_mask_temp = IsBitTest();
  const intptr_t kNumTemps = !IsNullCheck() ? (need_mask_temp ? 2 : 1) : 0;
  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  summary->set_in(0, Location::RequiresRegister());
  if (!IsNullCheck()) {
    summary->set_temp(0, Location::RequiresRegister());
    if (need_mask_temp) {
      summary->set_temp(1, Location::RequiresRegister());
    }
  }
  return summary;
}

void CheckClassInstr::EmitNullCheck(FlowGraphCompiler* compiler, compiler::Label* deopt) {
  if (IsDeoptIfNull()) {
    __ BranchEqual(locs()->in(0).reg(), Object::null_object(), deopt);
  } else {
    ASSERT(IsDeoptIfNotNull());
    __ BranchNotEqual(locs()->in(0).reg(), Object::null_object(), deopt);
  }
}

void CheckClassInstr::EmitBitTest(FlowGraphCompiler* compiler,
                                  intptr_t min,
                                  intptr_t max,
                                  intptr_t mask,
                                  compiler::Label* deopt) {
  Register biased_cid = locs()->temp(0).reg();
  __ LoadImmediate(TMP, min);
  __ subu(biased_cid, biased_cid, TMP);
  __ LoadImmediate(TMP, max - min);
  __ BranchUnsignedGreater(biased_cid, TMP, deopt);

  Register bit_reg = locs()->temp(1).reg();
  __ LoadImmediate(bit_reg, 1);
  __ sllv(bit_reg, bit_reg, biased_cid);
  __ AndImmediate(bit_reg, bit_reg, mask);
  __ beq(bit_reg, ZR, deopt);
}

int CheckClassInstr::EmitCheckCid(FlowGraphCompiler* compiler,
                                  int bias,
                                  intptr_t cid_start,
                                  intptr_t cid_end,
                                  bool is_last,
                                  compiler::Label* is_ok,
                                  compiler::Label* deopt,
                                  bool use_near_jump) {
  Register biased_cid = locs()->temp(0).reg();
  if (cid_start == cid_end) {
    __ LoadImmediate(TMP, cid_start - bias);
    if (is_last) {
      __ bne(biased_cid, TMP, deopt);
    } else {
      __ beq(biased_cid, TMP, is_ok);
    }
  } else {
    // For class ID ranges use a subtract followed by an unsigned
    // comparison to check both ends of the ranges with one comparison.
    __ AddImmediate(biased_cid, biased_cid, bias - cid_start);
    bias = cid_start;
    // the range is small enough.
    __ LoadImmediate(TMP, cid_end - cid_start);
    // Reverse comparison so we get 1 if biased_cid > tmp ie cid is out of
    // range.
    __ sltu(TMP, TMP, biased_cid);
    if (is_last) {
      __ bne(TMP, ZR, deopt);
    } else {
      __ beq(TMP, ZR, is_ok);
    }
  }
  return bias;
}

LocationSummary* CheckSmiInstr::MakeLocationSummary(Zone* zone,
                                                    bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  summary->set_in(0, Location::RequiresRegister());
  return summary;
}

void CheckSmiInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  __ Comment("CheckSmiInstr");
  Register value = locs()->in(0).reg();
  compiler::Label* deopt = compiler->AddDeoptStub(deopt_id(), ICData::kDeoptCheckSmi);
  __ BranchIfNotSmi(value, deopt);
}

void CheckNullInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  ThrowErrorSlowPathCode* slow_path = new NullErrorSlowPath(this);
  compiler->AddSlowPathCode(slow_path);

  Register value_reg = locs()->in(0).reg();
  // in order to be able to allocate it on register.
  __ BranchEqual(value_reg, Object::null_object(), slow_path->entry_label());
}

LocationSummary* CheckClassIdInstr::MakeLocationSummary(Zone* zone,
                                                        bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  summary->set_in(0, cids_.IsSingleCid() ? Location::RequiresRegister()
                                         : Location::WritableRegister());
  return summary;
}

void CheckClassIdInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  Register value = locs()->in(0).reg();
  compiler::Label* deopt = compiler->AddDeoptStub(deopt_id(), ICData::kDeoptCheckClass);
  if (cids_.IsSingleCid()) {
    __ BranchNotEqual(value, compiler::Immediate(Smi::RawValue(cids_.cid_start)), deopt);
  } else {
    __ AddImmediate(value, value, -Smi::RawValue(cids_.cid_start));
    // the range is small enough.
    __ LoadImmediate(TMP, cids_.Extent());
    // Reverse comparison so we get 1 if biased_cid > tmp ie cid is out of
    // range.
    __ sltu(TMP, TMP, value);
    __ bne(TMP, ZR, deopt);
  }
}

LocationSummary* CheckArrayBoundInstr::MakeLocationSummary(Zone* zone,
                                                           bool opt) const {
  const intptr_t kNumInputs = 2;
  const intptr_t kNumTemps = 0;
  LocationSummary* locs = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  locs->set_in(kLengthPos, LocationRegisterOrSmiConstant(length()));
  locs->set_in(kIndexPos, LocationRegisterOrSmiConstant(index()));
  return locs;
}

void CheckArrayBoundInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  uint32_t flags = generalized_ ? ICData::kGeneralized : 0;
  compiler::Label* deopt =
      compiler->AddDeoptStub(deopt_id(), ICData::kDeoptCheckArrayBound, flags);

  Location length_loc = locs()->in(kLengthPos);
  Location index_loc = locs()->in(kIndexPos);

  if (length_loc.IsConstant() && index_loc.IsConstant()) {
    ASSERT((Smi::Cast(length_loc.constant()).Value() <=
            Smi::Cast(index_loc.constant()).Value()) ||
           (Smi::Cast(index_loc.constant()).Value() < 0));
    // Unconditionally deoptimize for constant bounds checks because they
    // only occur only when index is out-of-bounds.
    __ b(deopt);
    return;
  }

  const intptr_t index_cid = index()->Type()->ToCid();
  if (index_loc.IsConstant()) {
    Register length = length_loc.reg();
    const Smi& index = Smi::Cast(index_loc.constant());
    __ BranchUnsignedLessEqual(
        length, compiler::Immediate(compiler::target::ToRawSmi(index)), deopt);
  } else if (length_loc.IsConstant()) {
    const Smi& length = Smi::Cast(length_loc.constant());
    Register index = index_loc.reg();
    if (index_cid != kSmiCid) {
      __ BranchIfNotSmi(index, deopt);
    }
    if (length.Value() == Smi::kMaxValue) {
      __ BranchSignedLess(index, compiler::Immediate(0), deopt);
    } else {
      __ BranchUnsignedGreaterEqual(
          index, compiler::Immediate(compiler::target::ToRawSmi(length)), deopt);
    }
  } else {
    Register length = length_loc.reg();
    Register index = index_loc.reg();
    if (index_cid != kSmiCid) {
      __ BranchIfNotSmi(index, deopt);
    }
    __ BranchUnsignedGreaterEqual(index, length, deopt);
  }
}

LocationSummary* CheckWritableInstr::MakeLocationSummary(Zone* zone,
                                                         bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* locs = new (zone) LocationSummary(
      zone, kNumInputs, kNumTemps,
      UseSharedSlowPathStub(opt) ? LocationSummary::kCallOnSharedSlowPath
                                 : LocationSummary::kCallOnSlowPath);
  locs->set_in(kReceiver, Location::RequiresRegister());
  return locs;
}

void CheckWritableInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  WriteErrorSlowPath* slow_path = new WriteErrorSlowPath(this);
  compiler->AddSlowPathCode(slow_path);
  __ lbu(TMP, compiler::FieldAddress(locs()->in(0).reg(),
                                    compiler::target::Object::tags_offset()));
  // In the first byte.
  ASSERT(compiler::target::UntaggedObject::kDeeplyImmutableBit < 8);
  ASSERT(compiler::target::UntaggedObject::kShallowImmutableBit < 8);
  __ AndImmediate(TMP, TMP,
                  1 << compiler::target::UntaggedObject::kDeeplyImmutableBit |
               1 << compiler::target::UntaggedObject::kShallowImmutableBit);
  __ bne(TMP, ZR, slow_path->entry_label());
}

LocationSummary* BinaryInt64OpInstr::MakeLocationSummary(Zone* zone,
                                                        bool opt) const {
  const intptr_t kNumInputs = 2;
  const intptr_t kNumTemps = (op_kind() == Token::kMUL || op_kind() == Token::kADD ||
                              op_kind() == Token::kSUB) ? 1 : 0;
  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  summary->set_in(0, Location::Pair(Location::RequiresRegister(),
                                    Location::RequiresRegister()));
  summary->set_in(1, Location::Pair(Location::RequiresRegister(),
                                    Location::RequiresRegister()));
  summary->set_out(0, Location::Pair(Location::RequiresRegister(),
                                     Location::RequiresRegister()));

  if (op_kind() == Token::kMUL || op_kind() == Token::kADD ||
      op_kind() == Token::kSUB) {
    summary->set_temp(0, Location::RequiresRegister());
  }

  return summary;
}

void BinaryInt64OpInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  PairLocation* left_pair = locs()->in(0).AsPairLocation();
  Register left_lo = left_pair->At(0).reg();
  Register left_hi = left_pair->At(1).reg();
  PairLocation* right_pair = locs()->in(1).AsPairLocation();
  Register right_lo = right_pair->At(0).reg();
  Register right_hi = right_pair->At(1).reg();
  PairLocation* out_pair = locs()->out(0).AsPairLocation();
  Register out_lo = out_pair->At(0).reg();
  Register out_hi = out_pair->At(1).reg();
  ASSERT(!can_overflow());
  ASSERT(!CanDeoptimize());

  switch (op_kind()) {
    case Token::kBIT_AND: {
      __ and_(out_lo, left_lo, right_lo);
      __ and_(out_hi, left_hi, right_hi);
      break;
    }
    case Token::kBIT_OR: {
      __ or_(out_lo, left_lo, right_lo);
      __ or_(out_hi, left_hi, right_hi);
      break;
    }
    case Token::kBIT_XOR: {
      __ xor_(out_lo, left_lo, right_lo);
      __ xor_(out_hi, left_hi, right_hi);
      break;
    }
    case Token::kADD: {
      Register temp = locs()->temp(0).reg();
      __ addu(out_hi, left_hi, right_hi);
      __ addu(out_lo, left_lo, right_lo);
      __ sltu(temp, out_lo, right_lo);  // Carry
      __ addu(out_hi, out_hi, temp);
      break;
    }
    case Token::kSUB: {
      Register temp = locs()->temp(0).reg();
      __ sltu(temp, left_lo, right_lo);  // Borrow
      __ subu(out_hi, left_hi, right_hi);
      __ subu(out_hi, out_hi, temp);
      __ subu(out_lo, left_lo, right_lo);
      break;
    }
    case Token::kMUL: {
      // Compute 64-bit a * b as:
      //     a_l * b_l + (a_h * b_l + a_l * b_h) << 32
      Register temp = locs()->temp(0).reg();

      __ multu(left_lo, right_lo);
      __ mflo(out_lo);
      __ mfhi(temp);
      __ mov(out_hi, temp);

      __ multu(left_hi, right_lo);
      __ mflo(temp);
      __ addu(out_hi, out_hi, temp);

      __ multu(left_lo, right_hi);
      __ mflo(temp);
      __ addu(out_hi, out_hi, temp);

      break;
    }
    default:
      UNREACHABLE();
  }
}

LocationSummary* UnaryInt64OpInstr::MakeLocationSummary(Zone* zone,
                                                       bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  summary->set_in(0, Location::Pair(Location::RequiresRegister(),
                                    Location::RequiresRegister()));
  summary->set_out(0, Location::Pair(Location::RequiresRegister(),
                                     Location::RequiresRegister()));
  return summary;
}

void UnaryInt64OpInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  PairLocation* left_pair = locs()->in(0).AsPairLocation();
  Register left_lo = left_pair->At(0).reg();
  Register left_hi = left_pair->At(1).reg();

  PairLocation* out_pair = locs()->out(0).AsPairLocation();
  Register out_lo = out_pair->At(0).reg();
  Register out_hi = out_pair->At(1).reg();

  compiler::Label isZero, notZero;
  switch (op_kind()){
    case Token::kBIT_NOT:
    __ nor(out_lo, ZR, left_lo);
    __ nor(out_hi, ZR, left_hi);
      break;
    case Token::kNEGATE:
      __ BranchEqual(left_lo, ZR, &isZero);
      __ LoadImmediate(TMP, 1);
      __ b(&notZero);
      __ Bind(&isZero);
      __ mov(TMP, ZR);
      __ Bind(&notZero);
      __ subu(out_lo, ZR, left_lo);
      __ subu(out_hi, ZR, left_hi);
      __ subu(out_hi, out_hi, TMP);
      break;
    default:
      UNREACHABLE();
  }
}

LocationSummary* BinaryUint32OpInstr::MakeLocationSummary(Zone* zone,
                                                          bool opt) const {
  const intptr_t kNumInputs = 2;
  const intptr_t kNumTemps = 0;
  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  summary->set_in(0, Location::RequiresRegister());
  summary->set_in(1, LocationRegisterOrConstant(right()));
  summary->set_out(0, Location::RequiresRegister());
  return summary;
}

void BinaryUint32OpInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  Register left = locs()->in(0).reg();
  Register out = locs()->out(0).reg();
  ASSERT(out != left);
  if (locs()->in(1).IsConstant()) {
    int64_t right;
    const bool ok = compiler::HasIntegerValue(locs()->in(1).constant(), &right);
    RELEASE_ASSERT(ok);
    switch (op_kind()) {
      case Token::kBIT_AND:
        __ AndImmediate(out, left, right);
        break;
      case Token::kBIT_OR:
        __ OrImmediate(out, left, right);
        break;
      case Token::kBIT_XOR:
        __ XorImmediate(out, left, right);
        break;
      case Token::kADD:
        __ AddImmediate(out, left, right);
        break;
      case Token::kSUB:
        __ AddImmediate(out, left, -right);
        break;
      case Token::kMUL:
        __ mov(out, left);
        __ MulImmediate(out, right);
        break;
      default:
        UNREACHABLE();
    }
  } else {
    Register right = locs()->in(1).reg();
    switch (op_kind()) {
      case Token::kBIT_AND:
        __ and_(out, left, right);
        break;
      case Token::kBIT_OR:
        __ or_(out, left, right);
        break;
      case Token::kBIT_XOR:
        __ xor_(out, left, right);
        break;
      case Token::kADD:
        __ addu(out, left, right);
        break;
      case Token::kSUB:
        __ subu(out, left, right);
        break;
      case Token::kMUL:
        __ multu(left, right);
        __ mflo(out);
        break;
      default:
        UNREACHABLE();
    }
  }
}

LocationSummary* UnaryUint32OpInstr::MakeLocationSummary(Zone* zone,
                                                         bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  summary->set_in(0, Location::RequiresRegister());
  summary->set_out(0, Location::RequiresRegister());
  return summary;
}

void UnaryUint32OpInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  Register left = locs()->in(0).reg();
  Register out = locs()->out(0).reg();
  ASSERT(left != out);

  ASSERT(op_kind() == Token::kBIT_NOT);

  __ nor(out, ZR, left);
}

LocationSummary* BinaryInt32OpInstr::MakeLocationSummary(Zone* zone,
                                                         bool opt) const {
  const intptr_t kNumInputs = 2;
  // Calculate number of temporaries.
  intptr_t num_temps = 0;
  if (((op_kind() == Token::kSHL) && can_overflow()) ||
      (op_kind() == Token::kSHR) || (op_kind() == Token::kUSHR) ||
      (op_kind() == Token::kMUL)) {
    num_temps = 1;
  }
  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, num_temps, LocationSummary::kNoCall);
  summary->set_in(0, Location::RequiresRegister());
  summary->set_in(1, LocationRegisterOrSmiConstant(right()));
  if (num_temps == 1) {
    summary->set_temp(0, Location::RequiresRegister());
  }
  // We make use of 3-operand instructions by not requiring result register
  // to be identical to first input register as on Intel.
  summary->set_out(0, Location::RequiresRegister());
  return summary;
}

void BinaryInt32OpInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  if (op_kind() == Token::kSHL) {
    EmitInt32ShiftLeft(compiler, this);
    return;
  }

  const Register left = locs()->in(0).reg();
  const Register result = locs()->out(0).reg();
  compiler::Label* deopt = nullptr;
  if (CanDeoptimize()) {
    deopt = compiler->AddDeoptStub(deopt_id(), ICData::kDeoptBinarySmiOp);
  }

  if (locs()->in(1).IsConstant()) {
    const Object& constant = locs()->in(1).constant();
    ASSERT(compiler::target::IsSmi(constant));
    const intptr_t value = compiler::target::SmiValue(constant);
    switch (op_kind()) {
      case Token::kADD: {
        if (deopt == nullptr) {
          __ AddImmediate(result, left, value);
        } else {
          __ AddImmediateBranchOverflow(result, left, value, deopt);
        }
        break;
      }
      case Token::kSUB: {
        if (deopt == nullptr) {
          __ AddImmediate(result, left, -value);
        } else {
          // Negating value and using AddImmediateSetFlags would not detect the
          // overflow when value == kMinInt32.
          __ SubtractImmediateBranchOverflow(result, left, value, deopt);
        }
        break;
      }
      case Token::kMUL: {
        const Register right = locs()->temp(0).reg();
        __ LoadImmediate(right, value);
        __ mult(left, right);
        __ mflo(result);
        if (deopt != nullptr) {
          __ mfhi(CMPRES2);
          __ sra(CMPRES1, result, 31);
          __ bne(CMPRES1, CMPRES2, deopt);
        }
        break;
      }
      case Token::kBIT_AND: {
        // No overflow check.
        __ AndImmediate(result, left, value);
        break;
      }
      case Token::kBIT_OR: {
        // No overflow check.
        __ OrImmediate(result, left, value);
        break;
      }
      case Token::kBIT_XOR: {
        // No overflow check.
        __ XorImmediate(result, left, value);
        break;
      }
      case Token::kSHR: {
        // sarl operation masks the count to 5 bits.
        const intptr_t kCountLimit = 0x1F;
        __ sra(result, left, Utils::Minimum(value, kCountLimit));
        break;
      }
      case Token::kUSHR: {
        UNIMPLEMENTED();
        break;
      }
      default:
        UNREACHABLE();
        break;
    }
    return;
  }

  const Register right = locs()->in(1).reg();
  switch (op_kind()) {
    case Token::kADD: {
      if (deopt == nullptr) {
        __ addu(result, left, right);
      } else {
        __ AddBranchOverflow(result, left, right, deopt);
      }
      break;
    }
    case Token::kSUB: {
      if (deopt == nullptr) {
        __ subu(result, left, right);
      } else {
        __ SubtractBranchOverflow(result, left, right, deopt);
      }
      break;
    }
    case Token::kMUL: {
        __ mult(left, right);
        __ mflo(result);
        if (deopt != nullptr) {
          __ mfhi(CMPRES2);
          __ sra(CMPRES1, result, 31);
          __ bne(CMPRES1, CMPRES2, deopt);
        }
      break;
    }
    case Token::kBIT_AND: {
      // No overflow check.
      __ and_(result, left, right);
      break;
    }
    case Token::kBIT_OR: {
      // No overflow check.
      __ or_(result, left, right);
      break;
    }
    case Token::kBIT_XOR: {
      // No overflow check.
      __ xor_(result, left, right);
      break;
    }
    default:
      UNREACHABLE();
      break;
  }
}

LocationSummary* IntConverterInstr::MakeLocationSummary(Zone* zone,
                                                        bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 0;
  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  if (from() == kUntagged || to() == kUntagged) {
    ASSERT((from() == kUntagged && to() == kUnboxedInt32) ||
           (from() == kUntagged && to() == kUnboxedUint32) ||
           (from() == kUnboxedInt32 && to() == kUntagged) ||
           (from() == kUnboxedUint32 && to() == kUntagged));
    ASSERT(!CanDeoptimize());
    summary->set_in(0, Location::RequiresRegister());
    summary->set_out(0, Location::SameAsFirstInput());
  } else if (from() == kUnboxedInt64) {
    ASSERT((to() == kUnboxedUint32) || (to() == kUnboxedInt32));
    summary->set_in(0, Location::Pair(Location::RequiresRegister(),
                                      Location::RequiresRegister()));
    summary->set_out(0, Location::RequiresRegister());
  } else if (to() == kUnboxedInt64) {
    ASSERT((from() == kUnboxedUint32) || (from() == kUnboxedInt32));
    summary->set_in(0, Location::RequiresRegister());
    summary->set_out(0, Location::Pair(Location::RequiresRegister(),
                                       Location::RequiresRegister()));
  } else {
    ASSERT((to() == kUnboxedUint32) || (to() == kUnboxedInt32));
    ASSERT((from() == kUnboxedUint32) || (from() == kUnboxedInt32));
    summary->set_in(0, Location::RequiresRegister());
    summary->set_out(0, Location::SameAsFirstInput());
  }
  return summary;
}

void IntConverterInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  const bool is_nop_conversion =
      (from() == kUntagged && to() == kUnboxedInt32) ||
      (from() == kUntagged && to() == kUnboxedUint32) ||
      (from() == kUnboxedInt32 && to() == kUntagged) ||
      (from() == kUnboxedUint32 && to() == kUntagged);
  if (is_nop_conversion) {
    ASSERT(locs()->in(0).reg() == locs()->out(0).reg());
    return;
  }

  if (from() == kUnboxedInt32 && to() == kUnboxedUint32) {
    const Register out = locs()->out(0).reg();
    // Representations are bitwise equivalent.
    ASSERT(out == locs()->in(0).reg());
  } else if (from() == kUnboxedUint32 && to() == kUnboxedInt32) {
    const Register out = locs()->out(0).reg();
    // Representations are bitwise equivalent.
    ASSERT(out == locs()->in(0).reg());
    if (CanDeoptimize()) {
      compiler::Label* deopt =
          compiler->AddDeoptStub(deopt_id(), ICData::kDeoptUnboxInteger);
      __ BranchSignedLess(out, compiler::Immediate(0), deopt);
    }
  } else if (from() == kUnboxedInt64) {
    ASSERT(to() == kUnboxedUint32 || to() == kUnboxedInt32);
    PairLocation* in_pair = locs()->in(0).AsPairLocation();
    Register in_lo = in_pair->At(0).reg();
    Register in_hi = in_pair->At(1).reg();
    Register out = locs()->out(0).reg();
    // Copy low word.
    __ mov(out, in_lo);
    if (CanDeoptimize()) {
      compiler::Label* deopt =
          compiler->AddDeoptStub(deopt_id(), ICData::kDeoptUnboxInteger);
      ASSERT(to() == kUnboxedInt32);
      __ sra(TMP, in_lo, 31);
      __ bne(in_hi, TMP, deopt);
    }
  } else if (from() == kUnboxedUint32 || from() == kUnboxedInt32) {
    ASSERT(to() == kUnboxedInt64);
    Register in = locs()->in(0).reg();
    PairLocation* out_pair = locs()->out(0).AsPairLocation();
    Register out_lo = out_pair->At(0).reg();
    Register out_hi = out_pair->At(1).reg();
    // Copy low word.
    __ mov(out_lo, in);
    if (from() == kUnboxedUint32) {
      __ xor_(out_hi, out_hi, out_hi);
    } else {
      ASSERT(from() == kUnboxedInt32);
      __ sra(out_hi, in, 31);
    }
  } else {
    UNREACHABLE();
  }
}

LocationSummary* BitCastInstr::MakeLocationSummary(Zone* zone, bool opt) const {

  LocationSummary* summary =
      new (zone) LocationSummary(zone, /*num_inputs=*/InputCount(),
                                 /*num_temps=*/0, LocationSummary::kNoCall);
  switch (from()) {
    case kUnboxedInt32:
      summary->set_in(0, Location::RequiresRegister());
      break;
    case kUnboxedInt64:
      summary->set_in(0, Location::Pair(Location::RequiresRegister(),
                                        Location::RequiresRegister()));
      break;
    case kUnboxedFloat:
    case kUnboxedDouble:
      summary->set_in(0, Location::RequiresFpuRegister());
      break;
    default:
      UNREACHABLE();
  }
  switch (to()) {
    case kUnboxedInt32:
      summary->set_out(0, Location::RequiresRegister());
      break;
    case kUnboxedInt64:
      summary->set_out(0, Location::Pair(Location::RequiresRegister(),
                                         Location::RequiresRegister()));
      break;
    case kUnboxedFloat:
    case kUnboxedDouble:
      summary->set_out(0, Location::RequiresFpuRegister());
      break;
    default:
      UNREACHABLE();
  }
  return summary;
}

void BitCastInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  switch (from()) {
    case kUnboxedFloat: {
      switch (to()) {
        case kUnboxedInt32: {
          const FpuRegister src = locs()->in(0).fpu_reg();
          const Register dst = locs()->out(0).reg();
          __ mfc1(dst, EvenFRegisterOf(src));
          break;
        }
        case kUnboxedInt64: {
          const FpuRegister src = locs()->in(0).fpu_reg();
          const Register dst0 = locs()->out(0).AsPairLocation()->At(0).reg();
          const Register dst1 = locs()->out(0).AsPairLocation()->At(1).reg();
          __ mfc1(dst0, EvenFRegisterOf(src));
          __ mov(dst1, ZR);
          break;
        }
        default:
          UNREACHABLE();
      }
      break;
    }
    case kUnboxedDouble: {
      ASSERT(to() == kUnboxedInt64);
      const FpuRegister src = locs()->in(0).fpu_reg();
      const Register dst0 = locs()->out(0).AsPairLocation()->At(0).reg();
      const Register dst1 = locs()->out(0).AsPairLocation()->At(1).reg();
      __ AddImmediate(SP, SP, -8);
      __ StoreDToOffset(src, SP, 0);
      __ lw(dst0, compiler::Address(SP, 0));
      __ lw(dst1, compiler::Address(SP, 4));
      __ AddImmediate(SP, SP, 8);
      break;
    }
    case kUnboxedInt64: {
      switch (to()) {
        case kUnboxedDouble: {
          const FpuRegister dst = locs()->out(0).fpu_reg();
          const Register src0 = locs()->in(0).AsPairLocation()->At(0).reg();
          const Register src1 = locs()->in(0).AsPairLocation()->At(1).reg();
          __ AddImmediate(SP, SP, -8);
          __ sw(src0, compiler::Address(SP, 0));
          __ sw(src1, compiler::Address(SP, 4));
          __ LoadDFromOffset(dst, SP, 0);
          __ AddImmediate(SP, SP, 8);
          break;
        }
        case kUnboxedFloat: {
          const FpuRegister dst = locs()->out(0).fpu_reg();
          const Register src0 = locs()->in(0).AsPairLocation()->At(0).reg();
          __ mtc1(src0, EvenFRegisterOf(dst));
          break;
        }
        default:
          UNREACHABLE();
      }
      break;
    }

    case kUnboxedInt32: {
      ASSERT(to() == kUnboxedFloat);
      const Register src = locs()->in(0).reg();
      const FpuRegister dst = locs()->out(0).fpu_reg();
      __ mtc1(src, EvenFRegisterOf(dst));
      break;
    }
    default:
      UNREACHABLE();
  }
}

LocationSummary* GotoInstr::MakeLocationSummary(Zone* zone, bool opt) const {
  return new (zone) LocationSummary(zone, 0, 0, LocationSummary::kNoCall);
}

void GotoInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  __ Comment("GotoInstr");
  if (!compiler->is_optimizing()) {
    if (FLAG_reorder_basic_blocks) {
      compiler->EmitEdgeCounter(block()->preorder_number());
    }
    // Add a deoptimization descriptor for deoptimizing instructions that
    // may be inserted before this instruction.
    compiler->AddCurrentDescriptor(UntaggedPcDescriptors::kDeopt, GetDeoptId(),
                                   InstructionSource());
  }
  if (HasParallelMove()) {
    parallel_move()->EmitNativeCode(compiler);
  }

  // We can fall through if the successor is the next block in the list.
  // Otherwise, we need a jump.
  if (!compiler->CanFallThroughTo(successor())) {
    __ b(compiler->GetJumpLabel(successor()));
  }
}

LocationSummary* IndirectGotoInstr::MakeLocationSummary(Zone* zone,
                                                        bool opt) const {
  const intptr_t kNumInputs = 1;
  const intptr_t kNumTemps = 2;

  LocationSummary* summary = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);

  summary->set_in(0, Location::RequiresRegister());
  summary->set_temp(0, Location::RequiresRegister());
  summary->set_temp(1, Location::RequiresRegister());

  return summary;
}

void IndirectGotoInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  Register index_reg = locs()->in(0).reg();
  Register target_address_reg = locs()->temp(0).reg();
  Register offset_reg = locs()->temp(1).reg();

  ASSERT(RequiredInputRepresentation(0) == kTagged);
  __ LoadObject(offset_reg, offsets_);
  const auto element_address = __ ElementAddressForRegIndex(
      /*is_load=*/true,
      /*is_external=*/false, kTypedDataInt32ArrayCid,
      /*index_scale=*/4,
      /*index_unboxed=*/false, offset_reg, index_reg);
  __ lw(offset_reg, element_address);

  __ GetNextPC(target_address_reg, TMP);
  const intptr_t entry_to_pc_offset = __ CodeSize();
  __ AddImmediate(target_address_reg, -entry_to_pc_offset);

  __ addu(target_address_reg, target_address_reg, offset_reg);

  // Jump to the absolute address.
  __ jr(target_address_reg);
}

LocationSummary* StrictCompareInstr::MakeLocationSummary(Zone* zone,
                                                         bool opt) const {
  const intptr_t kNumInputs = 2;
  const intptr_t kNumTemps = 0;
  if (needs_number_check()) {
    LocationSummary* locs = new (zone)
        LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kCall);
    locs->set_in(0, Location::RegisterLocation(A0));
    locs->set_in(1, Location::RegisterLocation(A1));
    locs->set_out(0, Location::RegisterLocation(A0));
    return locs;
  }
  LocationSummary* locs = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kNoCall);
  // If a constant has more than one use, make sure it is loaded in register
  // so that multiple immediate loads can be avoided.
  ConstantInstr* constant = left()->definition()->AsConstant();
  if ((constant != nullptr) && !left()->IsSingleUse()) {
    locs->set_in(0, Location::RequiresRegister());
  } else {
    locs->set_in(0, LocationRegisterOrConstant(left()));
  }

  constant = right()->definition()->AsConstant();
  if ((constant != nullptr) && !right()->IsSingleUse()) {
    locs->set_in(1, Location::RequiresRegister());
  } else {
    // Only one of the inputs can be a constant. Choose register if the first
    // one is a constant.
    locs->set_in(1, locs->in(0).IsConstant()
                        ? Location::RequiresRegister()
                        : LocationRegisterOrConstant(right()));
  }
  locs->set_out(0, Location::RequiresRegister());
  return locs;
}

LocationSummary* BooleanNegateInstr::MakeLocationSummary(Zone* zone,
                                                         bool opt) const {
  return LocationSummary::Make(zone, 1, Location::RequiresRegister(),
                               LocationSummary::kNoCall);
}

void BooleanNegateInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  Register value = locs()->in(0).reg();
  Register result = locs()->out(0).reg();

  __ xori(result, value,
      compiler::Immediate(compiler::target::ObjectAlignment::kBoolValueMask));
}

LocationSummary* BoolToIntInstr::MakeLocationSummary(Zone* zone,
                                                     bool opt) const {
  UNREACHABLE();
  return NULL;
}

void BoolToIntInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  UNREACHABLE();
}

LocationSummary* IntToBoolInstr::MakeLocationSummary(Zone* zone,
                                                     bool opt) const {
  UNREACHABLE();
  return NULL;
}

void IntToBoolInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  UNREACHABLE();
}

LocationSummary* AllocateObjectInstr::MakeLocationSummary(Zone* zone,
                                                          bool opt) const {
  const intptr_t kNumInputs = (type_arguments() != nullptr) ? 1 : 0;
  const intptr_t kNumTemps = 0;
  LocationSummary* locs = new (zone)
      LocationSummary(zone, kNumInputs, kNumTemps, LocationSummary::kCall);
  if (type_arguments() != nullptr) {
    locs->set_in(kTypeArgumentsPos, Location::RegisterLocation(
                                        AllocateObjectABI::kTypeArgumentsReg));
  }
  locs->set_out(0, Location::RegisterLocation(AllocateObjectABI::kResultReg));
  return locs;
}

void AllocateObjectInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  if (type_arguments() != nullptr) {
    TypeUsageInfo* type_usage_info = compiler->thread()->type_usage_info();
    if (type_usage_info != nullptr) {
      RegisterTypeArgumentsUse(compiler->function(), type_usage_info, cls_,
                               type_arguments()->definition());
    }
  }
  const Code& stub = Code::ZoneHandle(
      compiler->zone(), StubCode::GetAllocationStubForClass(cls()));
  compiler->GenerateStubCall(source(), stub, UntaggedPcDescriptors::kOther,
                             locs(), deopt_id(), env());
}

void DebugStepCheckInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
#ifdef PRODUCT
  UNREACHABLE();
#else
  ASSERT(!compiler->is_optimizing());
  __ BranchLinkPatchable(StubCode::DebugStepCheck());
  compiler->AddCurrentDescriptor(stub_kind_, deopt_id_, source());
  compiler->RecordSafepoint(locs());
#endif
}

}  // namespace dart

#endif  // defined TARGET_ARCH_MIPS

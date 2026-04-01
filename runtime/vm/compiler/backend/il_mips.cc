// Copyright (c) 2026, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"  // Needed here to get TARGET_ARCH_MIPS.
#if defined(TARGET_ARCH_MIPS)

#include "vm/compiler/backend/il.h"

#include "vm/compiler/backend/flow_graph.h"
#include "vm/compiler/backend/flow_graph_compiler.h"
#include "vm/compiler/backend/locations.h"
#include "vm/object_store.h"

#define __ compiler->assembler()->
#define Z (compiler->zone())

namespace dart {

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

LocationSummary* FloatCompareInstr::MakeLocationSummary(Zone* zone,
                                                        bool opt) const {
  UNREACHABLE();
  return NULL;
}

void FloatCompareInstr::EmitNativeCode(FlowGraphCompiler* compiler) {
  UNREACHABLE();
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

}  // namespace dart

#endif  // defined TARGET_ARCH_MIPS

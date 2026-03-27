// Copyright (c) 2026, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"  // Needed here to get TARGET_ARCH_MIPS.
#if defined(TARGET_ARCH_MIPS)

#include "vm/compiler/backend/il.h"

#include "vm/compiler/backend/flow_graph_compiler.h"
#include "vm/compiler/backend/locations.h"

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

}  // namespace dart

#endif  // defined TARGET_ARCH_MIPS

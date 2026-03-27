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

void MemoryCopyInstr::PrepareLengthRegForLoop(FlowGraphCompiler* compiler,
                                              Register length_reg,
                                              compiler::Label* done) {
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

}  // namespace dart

#endif  // defined TARGET_ARCH_MIPS

// Copyright (c) 2026, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"  // Needed here to get TARGET_ARCH_MIPS.
#if defined(TARGET_ARCH_MIPS)

#define SHOULD_NOT_INCLUDE_RUNTIME

#include "vm/class_id.h"
#include "vm/compiler/asm_intrinsifier.h"
#include "vm/compiler/assembler/assembler.h"

namespace dart {
namespace compiler{

#define __ assembler->

// Loads args from stack into T0 and T1
// Tests if they are smis, jumps to label not_smi if not.
static void TestBothArgumentsSmis(Assembler* assembler, Label* not_smi) {
  __ lw(T0, Address(SP, 0 * target::kWordSize));
  __ lw(T1, Address(SP, 1 * target::kWordSize));
  __ or_(CMPRES1, T0, T1);
  __ AndImmediate(CMPRES1, CMPRES1, kSmiTagMask);
  __ bne(CMPRES1, ZR, not_smi);
  return;
}

void AsmIntrinsifier::Integer_shl(Assembler* assembler, Label* normal_ir_body) {
  ASSERT(kSmiTagShift == 1);
  ASSERT(kSmiTag == 0);
  Label overflow;

  TestBothArgumentsSmis(assembler, normal_ir_body);
  __ BranchUnsignedGreater(T0, Immediate(target::ToRawSmi(target::kSmiBits)),
                           normal_ir_body);
  __ SmiUntag(T0);

  // Check for overflow by shifting left and shifting back arithmetically.
  // If the result is different from the original, there was overflow.
  __ sllv(TMP, T1, T0);
  __ srav(CMPRES1, TMP, T0);
  __ bne(CMPRES1, T1, &overflow);

  // No overflow, result in V0.
  __ Ret();
  __ delay_slot()->sllv(V0, T1, T0);

  __ Bind(&overflow);
  // Arguments are Smi but the shift produced an overflow to Mint.
  __ bltz(T1, normal_ir_body);
  __ SmiUntag(T1);

  // Pull off high bits that will be shifted off of T1 by making a mask
  // ((1 << T0) - 1), shifting it to the right, masking T1, then shifting back.
  // high bits = (((1 << T0) - 1) << (32 - T0)) & T1) >> (32 - T0)
  // lo bits = T1 << T0
  __ LoadImmediate(T3, 1);
  __ sllv(T3, T3, T0);              // T3 <- T3 << T0
  __ addiu(T3, T3, Immediate(-1));  // T3 <- T3 - 1
  __ subu(T4, ZR, T0);              // T4 <- -T0
  __ addiu(T4, T4, Immediate(32));  // T4 <- 32 - T0
  __ sllv(T3, T3, T4);              // T3 <- T3 << T4
  __ and_(T3, T3, T1);              // T3 <- T3 & T1
  __ srlv(T3, T3, T4);              // T3 <- T3 >> T4
  // Now T3 has the bits that fall off of T1 on a left shift.
  __ sllv(T0, T1, T0);  // T0 gets low bits.

  const Class& mint_class = MintClass();
  __ TryAllocate(mint_class, normal_ir_body, Assembler::kFarJump, V0, T1);

  __ sw(T0, FieldAddress(V0, target::Mint::value_offset()));
  __ Ret();
  __ delay_slot()->sw(T3, FieldAddress(V0, target::Mint::value_offset() + target::kWordSize));
  __ Bind(normal_ir_body);
}

static void Get64SmiOrMint(Assembler* assembler,
                           Register res_hi,
                           Register res_lo,
                           Register reg,
                           Label* not_smi_or_mint) {
  Label not_smi, done;
  __ AndImmediate(CMPRES1, reg, kSmiTagMask);
  __ bne(CMPRES1, ZR, &not_smi);
  __ SmiUntag(reg);

  // Sign extend to 64 bit
  __ mov(res_lo, reg);
  __ b(&done);
  __ delay_slot()->sra(res_hi, reg, 31);

  __ Bind(&not_smi);
  __ LoadClassId(CMPRES1, reg);
  __ BranchNotEqual(CMPRES1, Immediate(kMintCid), not_smi_or_mint);

  // Mint.
  __ lw(res_lo, FieldAddress(reg, target::Mint::value_offset()));
  __ lw(res_hi, FieldAddress(reg, target::Mint::value_offset() + target::kWordSize));
  __ Bind(&done);
  return;
}

static void CompareIntegers(Assembler* assembler,
                            Label* normal_ir_body,
                            Condition cond) {
  Label try_mint_smi, is_true, is_false, drop_two_fall_through;
  TestBothArgumentsSmis(assembler, &try_mint_smi);
  // T0 contains the right argument. T1 contains left argument

  switch (cond) {
    case LT:
      __ BranchSignedLess(T1, T0, &is_true);
      break;
    case LE:
      __ BranchSignedLessEqual(T1, T0, &is_true);
      break;
    case GT:
      __ BranchSignedGreater(T1, T0, &is_true);
      break;
    case GE:
      __ BranchSignedGreaterEqual(T1, T0, &is_true);
      break;
    default:
      UNREACHABLE();
      break;
  }

  __ Bind(&is_false);
  __ LoadObject(V0, CastHandle<Object>(FalseObject()));
  __ Ret();
  __ Bind(&is_true);
  __ LoadObject(V0, CastHandle<Object>(TrueObject()));
  __ Ret();

  __ Bind(&try_mint_smi);
  // Get left as 64 bit integer.
  Get64SmiOrMint(assembler, T3, T2, T1, normal_ir_body);
  // Get right as 64 bit integer.
  Get64SmiOrMint(assembler, T5, T4, T0, normal_ir_body);
  // T3: left high.
  // T2: left low.
  // T5: right high.
  // T4: right low.

  // 64-bit comparison
  switch (cond) {
    case LT:
    case LE: {
      // Compare left hi, right high.
      __ BranchSignedGreater(T3, T5, &is_false);
      __ BranchSignedLess(T3, T5, &is_true);
      // Compare left lo, right lo.
      if (cond == LT) {
        __ BranchUnsignedGreaterEqual(T2, T4, &is_false);
      } else {
        __ BranchUnsignedGreater(T2, T4, &is_false);
      }
      break;
    }
    case GT:
    case GE: {
      // Compare left hi, right high.
      __ BranchSignedLess(T3, T5, &is_false);
      __ BranchSignedGreater(T3, T5, &is_true);
      // Compare left lo, right lo.
      if (cond == GT) {
        __ BranchUnsignedLessEqual(T2, T4, &is_false);
      } else {
        __ BranchUnsignedLess(T2, T4, &is_false);
      }
      break;
    }
    default:
      UNREACHABLE();
      break;
  }
  // Else is true.
  __ b(&is_true);

  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Integer_lessThan(Assembler* assembler,
                                       Label* normal_ir_body) {
  CompareIntegers(assembler, normal_ir_body, LT);
}

void AsmIntrinsifier::Integer_greaterThan(Assembler* assembler,
                                       Label* normal_ir_body) {
  CompareIntegers(assembler, normal_ir_body, GT);
}

void AsmIntrinsifier::Integer_lessEqualThan(Assembler* assembler,
                                           Label* normal_ir_body) {
  CompareIntegers(assembler, normal_ir_body, LE);
}

void AsmIntrinsifier::Integer_greaterEqualThan(Assembler* assembler,
                                              Label* normal_ir_body) {
  CompareIntegers(assembler, normal_ir_body, GE);
}

// This is called for Smi, Mint and Bigint receivers. The right argument
// can be Smi, Mint, Bigint or double.
void AsmIntrinsifier::Integer_equalToInteger(Assembler* assembler,
                                             Label* normal_ir_body) {
  Label true_label, check_for_mint;
  // For integer receiver '===' check first.
  __ lw(T0, Address(SP, 0 * target::kWordSize));
  __ lw(T1, Address(SP, 1 * target::kWordSize));
  __ beq(T0, T1, &true_label);

  __ or_(T2, T0, T1);
  __ AndImmediate(CMPRES1, T2, kSmiTagMask);
  // If T0 or T1 is not a smi do Mint checks.
  __ bne(CMPRES1, ZR, &check_for_mint);

  // Both arguments are smi, '===' is good enough.
  __ LoadObject(V0, CastHandle<Object>(FalseObject()));
  __ Ret();
  __ Bind(&true_label);
  __ LoadObject(V0, CastHandle<Object>(TrueObject()));
  __ Ret();

  // At least one of the arguments was not Smi.
  Label receiver_not_smi;
  __ Bind(&check_for_mint);

  __ AndImmediate(CMPRES1, T1, kSmiTagMask);
  __ bne(CMPRES1, ZR, &receiver_not_smi);  // Check receiver.

  // Left (receiver) is Smi, return false if right is not Double.
  // Note that an instance of Mint or Bigint never contains a value that can be
  // represented by Smi.

  __ LoadClassId(CMPRES1, T0);
  __ BranchEqual(CMPRES1, Immediate(kDoubleCid), normal_ir_body);
  __ LoadObject(V0, CastHandle<Object>(FalseObject()));  // Smi == Mint -> false.
  __ Ret();

  __ Bind(&receiver_not_smi);
  // T1:: receiver.

  __ LoadClassId(CMPRES1, T1);
  __ BranchNotEqual(CMPRES1, Immediate(kMintCid), normal_ir_body);
  // Receiver is Mint, return false if right is Smi.
  __ AndImmediate(CMPRES1, T0, kSmiTagMask);
  __ bne(CMPRES1, ZR, normal_ir_body);
  __ LoadObject(V0, CastHandle<Object>(FalseObject()));
  __ Ret();

  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Integer_equal(Assembler* assembler,
                                    Label* normal_ir_body) {
  Integer_equalToInteger(assembler, normal_ir_body);
}

static void JumpIfInteger(Assembler* assembler,
                          Register cid,
                          Register tmp,
                          Label* target) {
  assembler->RangeCheck(cid, tmp, kSmiCid, kMintCid, Assembler::kIfInRange, target);
}

static void JumpIfNotInteger(Assembler* assembler,
                             Register cid,
                             Register tmp,
                             Label* target) {
  assembler->RangeCheck(cid, tmp, kSmiCid, kMintCid, Assembler::kIfNotInRange, target);
}

static void JumpIfString(Assembler* assembler,
                         Register cid,
                         Register tmp,
                         Label* target) {
  assembler->RangeCheck(cid, tmp, kOneByteStringCid, kTwoByteStringCid,
             Assembler::kIfInRange, target);
}

static void JumpIfNotString(Assembler* assembler,
                            Register cid,
                            Register tmp,
                            Label* target) {
  assembler->RangeCheck(cid, tmp, kOneByteStringCid, kTwoByteStringCid,
             Assembler::kIfNotInRange, target);
}

static void JumpIfNotList(Assembler* assembler,
                          Register cid,
                          Register tmp,
                          Label* target) {
  assembler->RangeCheck(cid, tmp, kArrayCid, kGrowableObjectArrayCid,
                        Assembler::kIfNotInRange, target);
}

static void JumpIfType(Assembler* assembler,
                       Register cid,
                       Register tmp,
                       Label* target) {
  COMPILE_ASSERT((kFunctionTypeCid == kTypeCid + 1) &&
                 (kRecordTypeCid == kTypeCid + 2));
  assembler->RangeCheck(cid, tmp, kTypeCid, kRecordTypeCid,
                        Assembler::kIfInRange, target);
}

static void JumpIfNotType(Assembler* assembler,
                          Register cid,
                          Register tmp,
                          Label* target) {
  COMPILE_ASSERT((kFunctionTypeCid == kTypeCid + 1) &&
                 (kRecordTypeCid == kTypeCid + 2));
  assembler->RangeCheck(cid, tmp, kTypeCid, kRecordTypeCid,
                        Assembler::kIfNotInRange, target);
}

// Compares cid1 and cid2 to see if they're syntactically equivalent. If this
// can be determined by this fast path, it jumps to either equal or not_equal,
// otherwise it jumps to normal_ir_body. May clobber cid1, cid2, and scratch.
static void EquivalentClassIds(Assembler* assembler,
                               Label* normal_ir_body,
                               Label* equal_may_be_generic,
                               Label* equal_not_generic,
                               Label* not_equal,
                               Register cid1,
                               Register cid2,
                               Register scratch,
                               bool testing_instance_cids) {
  Label not_integer, not_integer_or_string, not_integer_or_string_or_list;

  // Check if left hand side is a closure. Closures are handled in the runtime.
  __ BranchEqual(cid1, Immediate(kClosureCid), normal_ir_body);

  // Check if left hand side is a record. Records are handled in the runtime.
  __ BranchEqual(cid1, Immediate(kRecordCid), normal_ir_body);

  // Check whether class ids match. If class ids don't match types may still be
  // considered equivalent (e.g. multiple string implementation classes map to a
  // single String type).
  __ beq(cid1, cid2, equal_may_be_generic);

  // Class ids are different. Check if we are comparing two string types (with
  // different representations), two integer types, two list types or two type
  // types.
  __ BranchUnsignedGreater(cid1, Immediate(kNumPredefinedCids), not_equal);

  // Check if both are integers.
  JumpIfNotInteger(assembler, cid1, scratch, &not_integer);

  // First type is an integer. Check if the second is an integer too.
  JumpIfInteger(assembler, cid2, scratch, equal_not_generic);
  // Integer types are only equivalent to other integer types.
  __ b(not_equal);

  __ Bind(&not_integer);
  // Check if both are String types.
  JumpIfNotString(assembler, cid1, scratch,
                  testing_instance_cids ? &not_integer_or_string : not_equal);

  // First type is String. Check if the second is a string too.
  JumpIfString(assembler, cid2, scratch, equal_not_generic);
  // String types are only equivalent to other String types.
  __ b(not_equal);

  if (testing_instance_cids) {
    __ Bind(&not_integer_or_string);
    // Check if both are List types.
    JumpIfNotList(assembler, cid1, scratch, &not_integer_or_string_or_list);

    // First type is a List. Check if the second is a List too.
    JumpIfNotList(assembler, cid2, scratch, not_equal);
    ASSERT(compiler::target::Array::type_arguments_offset() ==
           compiler::target::GrowableObjectArray::type_arguments_offset());
    __ b(equal_may_be_generic);

    __ Bind(&not_integer_or_string_or_list);
    // Check if the first type is a Type. If it is not then types are not
    // equivalent because they have different class ids and they are not String
    // or integer or List or Type.
    JumpIfNotType(assembler, cid1, scratch, not_equal);

    // First type is a Type. Check if the second is a Type too.
    JumpIfType(assembler, cid2, scratch, equal_not_generic);
    // Type types are only equivalent to other Type types.
    __ b(not_equal);
  }
}

void AsmIntrinsifier::String_getHashCode(Assembler* assembler,
                                         Label* normal_ir_body) {
  __ lw(T0, Address(SP, 0 * target::kWordSize));
  __ lw(V0, FieldAddress(T0, target::String::hash_offset()));
  __ beq(V0, ZR, normal_ir_body);
  __ Ret();
  __ Bind(normal_ir_body);  // Hash not yet computed.
}

void AsmIntrinsifier::Type_equality(Assembler* assembler,
                                    Label* normal_ir_body) {
  Label equal, not_equal, equiv_cids_may_be_generic, equiv_cids;

  __ lw(T0, Address(SP, 1 * target::kWordSize));
  __ lw(T1, Address(SP, 0 * target::kWordSize));
  __ beq(T0, T1, &equal);

  // T1 might not be a Type object, so check that first (T0 should be though,
  // since this is a method on the Type class).
  __ LoadClassIdMayBeSmi(T3, T1);
  __ BranchNotEqual(T3, compiler::Immediate(kTypeCid), normal_ir_body);

  // Check if types are syntactically equal.
  __ LoadTypeClassId(T3, T1);
  __ LoadTypeClassId(T4, T0);
  // We are not testing instance cids, but type class cids of Type instances.
  EquivalentClassIds(assembler, normal_ir_body, &equiv_cids_may_be_generic,
                     &equiv_cids, &not_equal, T3, T4, TMP,
                     /* testing_instance_cids = */ false);

  __ Bind(&equiv_cids_may_be_generic);
  // Compare type arguments in Type instances.
  __ lw(T3, FieldAddress(T1, target::Type::arguments_offset()));
  __ lw(T4, FieldAddress(T0, target::Type::arguments_offset()));
  __ BranchNotEqual(T3, T4, normal_ir_body);
  // Fall through to check nullability if type arguments are equal.

  // Check nullability.
  __ Bind(&equiv_cids);
  __ LoadAbstractTypeNullability(T0, T0);
  __ LoadAbstractTypeNullability(T1, T1);
  __ bne(T0, T1, &not_equal);
  // Fall through to equal case if nullability is equal.

  __ Bind(&equal);
  __ LoadObject(V0, CastHandle<Object>(TrueObject()));
  __ Ret();

  __ Bind(&not_equal);
  __ LoadObject(V0, CastHandle<Object>(FalseObject()));
  __ Ret();

  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::AbstractType_getHashCode(Assembler* assembler,
                                               Label* normal_ir_body) {
  __ lw(T0, Address(SP, 0 * target::kWordSize));
  __ LoadCompressed(V0, FieldAddress(T0, target::AbstractType::hash_offset()));
  __ beq(V0, ZR, normal_ir_body);
  __ Ret();
  // Hash not yet computed.
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::AbstractType_equality(Assembler* assembler,
                                            Label* normal_ir_body) {
  __ lw(T0, Address(SP, 1 * target::kWordSize));
  __ lw(T1, Address(SP, 0 * target::kWordSize));
  __ bne(T0, T1, normal_ir_body);

  __ LoadObject(V0, CastHandle<Object>(TrueObject()));
  __ Ret();

  __ Bind(normal_ir_body);
}

void GenerateSubstringMatchesSpecialization(Assembler* assembler,
                                            intptr_t receiver_cid,
                                            intptr_t other_cid,
                                            Label* return_true,
                                            Label* return_false) {
  __ SmiUntag(A1);
  __ lw(T1, FieldAddress(A0, target::String::length_offset()));  // this.length
  __ SmiUntag(T1);
  __ lw(T2, FieldAddress(A2, target::String::length_offset()));  // other.length
  __ SmiUntag(T2);

  // if (other.length == 0) return true;
  __ beq(T2, ZR, return_true);

  // if (start < 0) return false;
  __ bltz(A1, return_false);

  // if (start + other.length > this.length) return false;
  __ addu(T0, A1, T2);
  __ BranchSignedGreater(T0, T1, return_false);

  if (receiver_cid == kOneByteStringCid) {
    __ AddImmediate(A0, A0, target::OneByteString::data_offset() - kHeapObjectTag);
    __ addu(A0, A0, A1);
  } else {
    ASSERT(receiver_cid == kTwoByteStringCid);
    __ AddImmediate(A0, A0, target::TwoByteString::data_offset() - kHeapObjectTag);
    __ addu(A0, A0, A1);
    __ addu(A0, A0, A1);
  }
  if (other_cid == kOneByteStringCid) {
    __ AddImmediate(A2, A2, target::OneByteString::data_offset() - kHeapObjectTag);
  } else {
    ASSERT(other_cid == kTwoByteStringCid);
    __ AddImmediate(A2, A2, target::TwoByteString::data_offset() - kHeapObjectTag);
  }

  // i = 0
  __ LoadImmediate(T0, 0);

  // do
  Label loop;
  __ Bind(&loop);

  if (receiver_cid == kOneByteStringCid) {
    __ lbu(T3, Address(A0, 0));  // this.codeUnitAt(i + start)
  } else {
    __ lhu(T3, Address(A0, 0));  // this.codeUnitAt(i + start)
  }
  if (other_cid == kOneByteStringCid) {
    __ lbu(T4, Address(A2, 0));  // other.codeUnitAt(i)
  } else {
    __ lhu(T4, Address(A2, 0));  // other.codeUnitAt(i)
  }
  __ bne(T3, T4, return_false);

  // i++, while (i < len)
  __ AddImmediate(T0, T0, 1);
  __ AddImmediate(A0, A0, receiver_cid == kOneByteStringCid ? 1 : 2);
  __ AddImmediate(A2, A2, other_cid == kOneByteStringCid ? 1 : 2);
  __ BranchSignedLess(T0, T2, &loop);

  __ b(return_true);
}

// bool _substringMatches(int start, String other)
// This intrinsic handles a OneByteString or TwoByteString receiver with a
// OneByteString other.
void AsmIntrinsifier::StringBaseSubstringMatches(Assembler* assembler,
                                                 Label* normal_ir_body) {
  Label return_true, return_false, try_two_byte;
  __ lw(A0, Address(SP, 2 * target::kWordSize));  // this
  __ lw(A1, Address(SP, 1 * target::kWordSize));  // start
  __ lw(A2, Address(SP, 0 * target::kWordSize));  // other

  __ AndImmediate(CMPRES1, A1, kSmiTagMask);
  __ bne(CMPRES1, ZR, normal_ir_body);  // 'start' is not a Smi.

  __ LoadClassId(CMPRES1, A2);
  __ BranchNotEqual(CMPRES1, Immediate(kOneByteStringCid), normal_ir_body);

  __ LoadClassId(CMPRES1, A0);
  __ BranchNotEqual(CMPRES1, Immediate(kOneByteStringCid), &try_two_byte);

  GenerateSubstringMatchesSpecialization(assembler, kOneByteStringCid,
                                         kOneByteStringCid, &return_true,
                                         &return_false);

  __ Bind(&try_two_byte);
  __ LoadClassId(CMPRES1, A0);
  __ BranchNotEqual(CMPRES1, Immediate(kTwoByteStringCid), normal_ir_body);

  GenerateSubstringMatchesSpecialization(assembler, kTwoByteStringCid,
                                         kOneByteStringCid, &return_true,
                                         &return_false);

  __ Bind(&return_true);
  __ LoadObject(V0, CastHandle<Object>(TrueObject()));
  __ Ret();

  __ Bind(&return_false);
  __ LoadObject(V0, CastHandle<Object>(FalseObject()));
  __ Ret();

  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Object_getHash(Assembler* assembler,
                                     Label* normal_ir_body) {
  UNREACHABLE();
}

void AsmIntrinsifier::StringBaseCharAt(Assembler* assembler,
                                       Label* normal_ir_body) {
  Label try_two_byte_string;

  __ lw(T1, Address(SP, 0 * target::kWordSize));  // Index.
  __ lw(T0, Address(SP, 1 * target::kWordSize));  // String.

  // Checks.
  __ AndImmediate(CMPRES1, T1, kSmiTagMask);
  __ bne(CMPRES1, ZR, normal_ir_body);                    // Index is not a Smi.
  __ lw(T2, FieldAddress(T0, target::String::length_offset()));  // Range check.
  // Runtime throws exception.
  __ BranchUnsignedGreaterEqual(T1, T2, normal_ir_body);
  __ LoadClassId(CMPRES1, T0);  // Class ID check.
  __ BranchNotEqual(CMPRES1, Immediate(kOneByteStringCid),
                    &try_two_byte_string);

  // Grab byte and return.
  __ SmiUntag(T1);
  __ addu(T2, T0, T1);
  __ lbu(T2, FieldAddress(T2, target::OneByteString::data_offset()));
  __ BranchSignedGreaterEqual(
      T2, Immediate(target::Symbols::kNumberOfOneCharCodeSymbols), normal_ir_body);
  __ lw(V0, Address(THR, target::Thread::predefined_symbols_address_offset()));
  __ AddImmediate(V0, target::Symbols::kNullCharCodeSymbolOffset * target::kWordSize);
  __ sll(T2, T2, 2);
  __ addu(T2, T2, V0);
  __ Ret();
  __ delay_slot()->lw(V0, Address(T2));

  __ Bind(&try_two_byte_string);
  __ BranchNotEqual(CMPRES1, Immediate(kTwoByteStringCid), normal_ir_body);
  ASSERT(kSmiTagShift == 1);
  __ addu(T2, T0, T1);
  __ lhu(T2, FieldAddress(T2, target::TwoByteString::data_offset()));
  __ BranchSignedGreaterEqual(
      T2, Immediate(target::Symbols::kNumberOfOneCharCodeSymbols), normal_ir_body);
  __ lw(V0, Address(THR, target::Thread::predefined_symbols_address_offset()));
  __ AddImmediate(V0, target::Symbols::kNullCharCodeSymbolOffset * target::kWordSize);
  __ sll(T2, T2, 2);
  __ addu(T2, T2, V0);
  __ Ret();
  __ delay_slot()->lw(V0, Address(T2));

  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::StringBaseIsEmpty(Assembler* assembler,
                                        Label* normal_ir_body) {
  Label is_true;

  __ lw(T0, Address(SP, 0 * target::kWordSize));
  __ lw(T0, FieldAddress(T0, target::String::length_offset()));

  __ beq(T0, ZR, &is_true);
  __ LoadObject(V0, CastHandle<Object>(FalseObject()));
  __ Ret();
  __ Bind(&is_true);
  __ LoadObject(V0, CastHandle<Object>(TrueObject()));
  __ Ret();
}

void AsmIntrinsifier::OneByteString_getHashCode(Assembler* assembler, Label* normal_ir_body) {
  Label no_hash;

  __ lw(T1, Address(SP, 0 * target::kWordSize));
  __ lw(V0, FieldAddress(T1, target::String::hash_offset()));
  __ beq(V0, ZR, &no_hash);
  __ Ret();  // Return if already computed.
  __ Bind(&no_hash);
  __ lw(T2, FieldAddress(T1, target::String::length_offset()));
  __ SmiUntag(T2);
  __ mov(T3, ZR);
  __ AddImmediate(T4, T1,
                  target::OneByteString::data_offset() - kHeapObjectTag);

  // V0: Hash code, untagged integer.
  // T1: Instance of OneByteString.
  // T2: String length, untagged integer.
  // T3: Loop counter, untagged integer..
  // T4: String data.

  Label loop, done;
  __ Bind(&loop);
  __ BranchEqual(T3, T2, &done);
  // Add to hash code: (hash_ is uint32)
  // Get one characters (ch).
  __ lbu(TMP, Address(T4, 0));
  // TMP: ch.
  __ addiu(T3, T3, compiler::Immediate(1));
  __ addiu(T4, T4, compiler::Immediate(1));
  //ovu funkciju implementirati
  __ CombineHashes(V0, TMP);
  __ b(&loop);

  __ Bind(&done);
  __ FinalizeHashForSize(target::String::kHashBits, V0);
  __ SmiTag(V0);
  __ sw(V0, FieldAddress(T1, target::String::hash_offset()));
  __ Ret();
}

// Allocates one-byte string of length 'end - start'. The content is not
// initialized.
// 'length-reg' (T2) contains tagged length.
// Returns new string as tagged pointer in V0.
static void TryAllocateString(Assembler* assembler,
                              classid_t cid,
                              intptr_t max_elements,
                              Label* ok,
                              Label* failure) {
  ASSERT(cid == kOneByteStringCid || cid == kTwoByteStringCid);
  const Register length_reg = T2;
  // _Mint length: call to runtime to produce error.
  __ BranchIfNotSmi(length_reg, failure);
  // Negative length: call to runtime to produce error.
  // Too big: call to runtime to allocate old.
  __ BranchUnsignedGreater(length_reg,
                           compiler::Immediate(target::ToRawSmi(max_elements)),
                           failure);
  NOT_IN_PRODUCT(__ MaybeTraceAllocation(cid, failure, V0));
  __ mov(T6, length_reg);  // Save the length register.

  if (cid == kOneByteStringCid) {
    __ SmiUntag(length_reg);
  } else {
    // Untag length and multiply by element size -> no-op.
  }
  const intptr_t fixed_size_plus_alignment_padding =
      target::String::InstanceSize() +
      target::ObjectAlignment::kObjectAlignment - 1;
  __ AddImmediate(length_reg, fixed_size_plus_alignment_padding);
  __ LoadImmediate(TMP, ~(target::ObjectAlignment::kObjectAlignment - 1));
  __ and_(length_reg, length_reg, TMP);

  __ lw(V0, Address(THR, target::Thread::top_offset()));

  // length_reg: allocation size.
  __ addu(T1, V0, length_reg);
  __ BranchUnsignedLess(T1, V0, failure);  // Fail on unsigned overflow.

  // Check if the allocation fits into the remaining space.
  // V0: potential new object start.
  // T1: potential next object start.
  // T2: allocation size.
  __ lw(T4, Address(THR, target::Thread::end_offset()));
  __ BranchUnsignedGreaterEqual(T1, T4, failure);
  __ CheckAllocationCanary(V0);

  // Successfully allocated the object(s), now update top to point to
  // next object start and initialize the object.
  __ sw(T1, Address(THR, target::Thread::top_offset()));
  __ AddImmediate(V0, kHeapObjectTag);

  // Clear last double word to ensure string comparison doesn't need to
  // specially handle remainder of strings with lengths not factors of double
  // offsets.
  __ sw(ZR, Address(T1, -1 * target::kWordSize));
  __ sw(ZR, Address(T1, -2 * target::kWordSize));


  // Initialize the tags.
  // V0: new object start as a tagged pointer.
  // T1: new object end address.
  // T2: allocation size.
  {
    Label overflow, done;
    const intptr_t shift = target::UntaggedObject::kSizeTagPos -
                           target::ObjectAlignment::kObjectAlignmentLog2;
    __ BranchUnsignedGreater(T2, Immediate(target::UntaggedObject::kSizeTagMaxSizeTag),
                             &overflow);
    __ b(&done);
    __ delay_slot()->sll(T2, T2, shift);
    __ Bind(&overflow);
    __ mov(T2, ZR);
    __ Bind(&done);

    // Get the class index and insert it into the tags.
    // T2: size and bit tags.
    const uword tags =
        target::MakeTagWordForNewSpaceObject(cid, /*instance_size=*/0);
    __ LoadImmediate(TMP, tags);
    __ or_(T2, T2, TMP);
    __ sw(T2, FieldAddress(V0, target::Object::tags_offset()));  // Store tags.
  }

  // Set the length field using the saved length (T6).
  __ StoreIntoObjectNoBarrier(V0, FieldAddress(V0, target::String::length_offset()),
                              T6);
  // Clear hash.
  __ StoreIntoObjectNoBarrier(
      V0, FieldAddress(V0, target::String::hash_offset()), ZR);

  __ b(ok);
}

// Arg0: OneByteString (receiver).
// Arg1: Start index as Smi.
// Arg2: End index as Smi.
// The indexes must be valid.
void AsmIntrinsifier::OneByteString_substringUnchecked(Assembler* assembler,
                                                       Label* normal_ir_body) {
  const intptr_t kStringOffset = 2 * target::kWordSize;
  const intptr_t kStartIndexOffset = 1 * target::kWordSize;
  const intptr_t kEndIndexOffset = 0 * target::kWordSize;
  Label ok;

  __ lw(T2, Address(SP, kEndIndexOffset));
  __ lw(TMP, Address(SP, kStartIndexOffset));
  __ or_(CMPRES1, T2, TMP);
  __ AndImmediate(CMPRES1, CMPRES1, kSmiTagMask);
  __ bne(CMPRES1, ZR, normal_ir_body);  // 'start', 'end' not Smi.

  __ subu(T2, T2, TMP);
  TryAllocateString(assembler, kOneByteStringCid,
                    target::OneByteString::kMaxNewSpaceElements, &ok, normal_ir_body);
  __ Bind(&ok);
  // V0: new string as tagged pointer.
  // Copy string.
  __ lw(T3, Address(SP, kStringOffset));
  __ lw(T1, Address(SP, kStartIndexOffset));
  __ SmiUntag(T1);
  __ addu(T3, T3, T1);

  // T3: Start address to copy from (untagged).
  // T1: Untagged start index.
  __ lw(T2, Address(SP, kEndIndexOffset));
  __ SmiUntag(T2);
  __ subu(T2, T2, T1);

  // T3: Start address to copy from (untagged).
  // T2: Untagged number of bytes to copy.
  // V0: Tagged result string.
  // T6: Pointer into T3.
  // T7: Pointer into T0.
  // T1: Scratch register.
  Label loop, done;
  __ blez(T2, &done);
  __ mov(T6, T3);
  __ mov(T7, V0);

  __ Bind(&loop);
  __ AddImmediate(T2, T2, -1);
  __ lbu(T1, FieldAddress(T6, target::OneByteString::data_offset()));
  __ AddImmediate(T6, 1);
  __ sb(T1, FieldAddress(T7, target::OneByteString::data_offset()));
  __ addiu(T7, T7, Immediate(1));
  __ bgtz(T2, &loop);

  __ Bind(&done);
  __ Ret();
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::WriteIntoOneByteString(Assembler* assembler,
                                             Label* normal_ir_body) {
  __ lw(T2, Address(SP, 0 * target::kWordSize));  // Value.
  __ lw(T1, Address(SP, 1 * target::kWordSize));  // Index.
  __ lw(T0, Address(SP, 2 * target::kWordSize));  // OneByteString.
  __ SmiUntag(T1);
  __ SmiUntag(T2);
  __ addu(T3, T0, T1);
  __ Ret();
  __ delay_slot()->sb(T2, FieldAddress(T3, target::OneByteString::data_offset()));
}

void AsmIntrinsifier::WriteIntoTwoByteString(Assembler* assembler,
                                             Label* normal_ir_body) {
  __ lw(T2, Address(SP, 0 * target::kWordSize));  // Value.
  __ lw(T1, Address(SP, 1 * target::kWordSize));  // Index.
  __ lw(T0, Address(SP, 2 * target::kWordSize));  // TwoByteString.
  // Untag index and multiply by element size -> no-op.
  __ SmiUntag(T2);
  __ addu(T3, T0, T1);
  __ Ret();
  __ delay_slot() -> sh(T2,
                        FieldAddress(T3, target::TwoByteString::data_offset()));
}

void AsmIntrinsifier::AllocateOneByteString(Assembler* assembler,
                                             Label* normal_ir_body) {
  Label ok;

  __ lw(T2, Address(SP, 0 * target::kWordSize));  // Length.
  TryAllocateString(assembler, kOneByteStringCid,
                    target::OneByteString::kMaxNewSpaceElements, &ok, normal_ir_body);
  __ Bind(&ok);
  __ Ret();
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::AllocateTwoByteString(Assembler* assembler,
                                            Label* normal_ir_body) {
  Label ok;

  __ lw(T2, Address(SP, 0 * target::kWordSize));  // Length.
  TryAllocateString(assembler, kTwoByteStringCid,
                    target::TwoByteString::kMaxNewSpaceElements, &ok,
                    normal_ir_body);
  __ Bind(&ok);
  __ Ret();
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::OneByteString_equality(Assembler* assembler,
                                             Label* normal_ir_body) {
  __ lw(T0, Address(SP, 1 * target::kWordSize));  // This.
  __ lw(T1, Address(SP, 0 * target::kWordSize));  // Other.

  StringEquality(assembler, T0, T1, T2, T3, V0, normal_ir_body,
                 kOneByteStringCid);
}

void AsmIntrinsifier::TwoByteString_equality(Assembler* assembler,
                                             Label* normal_ir_body) {
  __ lw(T0, Address(SP, 1 * target::kWordSize));  // This.
  __ lw(T1, Address(SP, 0 * target::kWordSize));  // Other.

  StringEquality(assembler, T0, T1, T2, T3, V0, normal_ir_body,
                 kTwoByteStringCid);
}

void AsmIntrinsifier::Timeline_getNextTaskId(Assembler* assembler,
                                             Label* normal_ir_body) {
#if !defined(SUPPORT_TIMELINE)
  __ LoadImmediate(V0, target::ToRawSmi(0));
  __ Ret();
#else
  __ lw(T0, Address(THR, target::Thread::next_task_id_offset()));
  __ lw(T1, Address(THR, target::Thread::next_task_id_offset() + 4));
  __ SmiTag(V0, T0);  // Ignore loss of precision.
  __ AddImmediate(T2, T0, 1);
  __ sltu(T3, T2, T0);  // Carry.
  __ addu(T1, T1, T3);
  __ sw(T2, Address(THR, target::Thread::next_task_id_offset()));
  __ sw(T1, Address(THR, target::Thread::next_task_id_offset() + 4));
  __ Ret();
#endif
}

}  // namespace compiler
}  // namespace dart

#endif  // defined TARGET_ARCH_MIPS

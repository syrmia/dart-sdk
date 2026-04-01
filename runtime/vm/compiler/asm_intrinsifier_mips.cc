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

void AsmIntrinsifier::Smi_bitLength(Assembler* assembler,
                                    Label* normal_ir_body) {
  __ lw(V0, Address(SP, 0 * target::kWordSize));
  __ SmiUntag(V0);
  // XOR with sign bit to complement bits if value is negative.
  __ sra(T0, V0, 31);
  __ xor_(V0, V0, T0);
  __ clz(V0, V0);
  __ LoadImmediate(T0, 32);
  __ subu(V0, T0, V0);
  __ Ret();
  __ delay_slot()->SmiTag(V0);
}

void AsmIntrinsifier::Bigint_lsh(Assembler* assembler, Label* normal_ir_body) {
  // static void _lsh(Uint32List x_digits, int x_used, int n,
  //                  Uint32List r_digits)

  // T2 = x_used, T3 = x_digits, x_used > 0, x_used is Smi.
  __ lw(T2, Address(SP, 2 * target::kWordSize));
  __ lw(T3, Address(SP, 3 * target::kWordSize));
  // T4 = r_digits, T5 = n, n is Smi, n % _DIGIT_BITS != 0.
  __ lw(T4, Address(SP, 0 * target::kWordSize));
  __ lw(T5, Address(SP, 1 * target::kWordSize));
  __ SmiUntag(T5);
  // T0 = n ~/ _DIGIT_BITS
  __ sra(T0, T5, 5);
  // T6 = &x_digits[0]
  __ addiu(T6, T3, Immediate(target::TypedData::payload_offset() - kHeapObjectTag));
  // V0 = &x_digits[x_used]
  __ sll(T2, T2, 1);
  __ addu(V0, T6, T2);
  // V1 = &r_digits[1]
  __ addiu(V1, T4, Immediate(target::TypedData::payload_offset() - kHeapObjectTag +
                             kBytesPerBigIntDigit));
  // V1 = &r_digits[x_used + n ~/ _DIGIT_BITS + 1]
  __ addu(V1, V1, T2);
  __ sll(T1, T0, 2);
  __ addu(V1, V1, T1);
  // T3 = n % _DIGIT_BITS
  __ AndImmediate(T3, T5, 31);
  // T2 = 32 - T3
  __ subu(T2, ZR, T3);
  __ addiu(T2, T2, Immediate(32));
  __ mov(T1, ZR);
  Label loop;
  __ Bind(&loop);
  __ addiu(V0, V0, Immediate(-kBytesPerBigIntDigit));
  __ lw(T0, Address(V0, 0));
  __ srlv(AT, T0, T2);
  __ or_(T1, T1, AT);
  __ addiu(V1, V1, Immediate(-kBytesPerBigIntDigit));
  __ sw(T1, Address(V1, 0));
  __ bne(V0, T6, &loop);
  __ delay_slot()->sllv(T1, T0, T3);
  __ sw(T1, Address(V1, -kBytesPerBigIntDigit));
  // Returning Object::null() is not required, since this method is private.
  __ Ret();
}

void AsmIntrinsifier::Bigint_rsh(Assembler* assembler, Label* normal_ir_body) {
  // static void _lsh(Uint32List x_digits, int x_used, int n,
  //                  Uint32List r_digits)

  // T2 = x_used, T3 = x_digits, x_used > 0, x_used is Smi.
  __ lw(T2, Address(SP, 2 * target::kWordSize));
  __ lw(T3, Address(SP, 3 * target::kWordSize));
  // T4 = r_digits, T5 = n, n is Smi, n % _DIGIT_BITS != 0.
  __ lw(T4, Address(SP, 0 * target::kWordSize));
  __ lw(T5, Address(SP, 1 * target::kWordSize));
  __ SmiUntag(T5);
  // T0 = n ~/ _DIGIT_BITS
  __ sra(T0, T5, 5);
  // V1 = &r_digits[0]
  __ addiu(V1, T4, Immediate(target::TypedData::payload_offset() - kHeapObjectTag));
  // V0 = &x_digits[n ~/ _DIGIT_BITS]
  __ addiu(V0, T3, Immediate(target::TypedData::payload_offset() - kHeapObjectTag));
  __ sll(T1, T0, 2);
  __ addu(V0, V0, T1);
  // T6 = &r_digits[x_used - n ~/ _DIGIT_BITS - 1]
  __ sll(T2, T2, 1);
  __ addu(T6, V1, T2);
  __ subu(T6, T6, T1);
  __ addiu(T6, T6, Immediate(-4));
  // T3 = n % _DIGIT_BITS
  __ AndImmediate(T3, T5, 31);
  // T2 = 32 - T3
  __ subu(T2, ZR, T3);
  __ addiu(T2, T2, Immediate(32));
  // T1 = x_digits[n ~/ _DIGIT_BITS] >> (n % _DIGIT_BITS)
  __ lw(T1, Address(V0, 0));
  __ addiu(V0, V0, Immediate(kBytesPerBigIntDigit));
  Label loop_exit;
  __ beq(V1, T6, &loop_exit);
  __ delay_slot()->srlv(T1, T1, T3);
  Label loop;
  __ Bind(&loop);
  __ lw(T0, Address(V0, 0));
  __ addiu(V0, V0, Immediate(kBytesPerBigIntDigit));
  __ sllv(AT, T0, T2);
  __ or_(T1, T1, AT);
  __ sw(T1, Address(V1, 0));
  __ addiu(V1, V1, Immediate(kBytesPerBigIntDigit));
  __ bne(V1, T6, &loop);
  __ delay_slot()->srlv(T1, T0, T3);
  __ Bind(&loop_exit);
  __ sw(T1, Address(V1, 0));
  // Returning Object::null() is not required, since this method is private.
  __ Ret();
}

void AsmIntrinsifier::Bigint_absAdd(Assembler* assembler,
                                    Label* normal_ir_body) {
  // static void _absAdd(Uint32List digits, int used,
  //                     Uint32List a_digits, int a_used,
  //                     Uint32List r_digits)

  // T2 = used, T3 = digits
  __ lw(T2, Address(SP, 3 * target::kWordSize));
  __ lw(T3, Address(SP, 4 * target::kWordSize));
  // T3 = &digits[0]
  __ addiu(T3, T3, Immediate(target::TypedData::payload_offset() - kHeapObjectTag));

  // T4 = a_used, T5 = a_digits
  __ lw(T4, Address(SP, 1 * target::kWordSize));
  __ lw(T5, Address(SP, 2 * target::kWordSize));
  // T5 = &a_digits[0]
  __ addiu(T5, T5, Immediate(target::TypedData::payload_offset() - kHeapObjectTag));

  // T6 = r_digits
  __ lw(T6, Address(SP, 0 * target::kWordSize));
  // T6 = &r_digits[0]
  __ addiu(T6, T6, Immediate(target::TypedData::payload_offset() - kHeapObjectTag));

  // V0 = &digits[a_used >> 1], a_used is Smi.
  __ sll(V0, T4, 1);
  __ addu(V0, V0, T3);

  // V1 = &digits[used >> 1], used is Smi.
  __ sll(V1, T2, 1);
  __ addu(V1, V1, T3);

  // T2 = carry in = 0.
  __ mov(T2, ZR);
  Label add_loop;
  __ Bind(&add_loop);
  // Loop a_used times, a_used > 0.
  __ lw(T0, Address(T3, 0));  // T0 = x.
  __ addiu(T3, T3, Immediate(kBytesPerBigIntDigit));
  __ lw(T1, Address(T5, 0));  // T1 = y.
  __ addiu(T5, T5, Immediate(kBytesPerBigIntDigit));
  __ addu(T1, T0, T1);  // T1 = x + y.
  __ sltu(T4, T1, T0);  // T4 = carry out of x + y.
  __ addu(T0, T1, T2);  // T0 = x + y + carry in.
  __ sltu(T2, T0, T1);  // T2 = carry out of (x + y) + carry in.
  __ or_(T2, T2, T4);   // T2 = carry out of x + y + carry in.
  __ sw(T0, Address(T6, 0));
  __ bne(T3, V0, &add_loop);
  __ delay_slot()->addiu(T6, T6, Immediate(kBytesPerBigIntDigit));

  Label last_carry;
  __ beq(T3, V1, &last_carry);

  Label carry_loop;
  __ Bind(&carry_loop);
  // Loop used - a_used times, used - a_used > 0.
  __ lw(T0, Address(T3, 0));  // T0 = x.
  __ addiu(T3, T3, Immediate(kBytesPerBigIntDigit));
  __ addu(T1, T0, T2);  // T1 = x + carry in.
  __ sltu(T2, T1, T0);  // T2 = carry out of x + carry in.
  __ sw(T1, Address(T6, 0));
  __ bne(T3, V1, &carry_loop);
  __ delay_slot()->addiu(T6, T6, Immediate(kBytesPerBigIntDigit));

  __ Bind(&last_carry);
  __ sw(T2, Address(T6, 0));

  // Returning Object::null() is not required, since this method is private.
  __ Ret();
}

void AsmIntrinsifier::Bigint_absSub(Assembler* assembler,
                                    Label* normal_ir_body) {
  // static void _absSub(Uint32List digits, int used,
  //                     Uint32List a_digits, int a_used,
  //                     Uint32List r_digits)

  // T2 = used, T3 = digits
  __ lw(T2, Address(SP, 3 * target::kWordSize));
  __ lw(T3, Address(SP, 4 * target::kWordSize));
  // T3 = &digits[0]
  __ addiu(T3, T3, Immediate(target::TypedData::payload_offset() - kHeapObjectTag));

  // T4 = a_used, T5 = a_digits
  __ lw(T4, Address(SP, 1 * target::kWordSize));
  __ lw(T5, Address(SP, 2 * target::kWordSize));
  // T5 = &a_digits[0]
  __ addiu(T5, T5, Immediate(target::TypedData::payload_offset() - kHeapObjectTag));

  // T6 = r_digits
  __ lw(T6, Address(SP, 0 * target::kWordSize));
  // T6 = &r_digits[0]
  __ addiu(T6, T6, Immediate(target::TypedData::payload_offset() - kHeapObjectTag));

  // V0 = &digits[a_used >> 1], a_used is Smi.
  __ sll(V0, T4, 1);
  __ addu(V0, V0, T3);

  // V1 = &digits[used >> 1], used is Smi.
  __ sll(V1, T2, 1);
  __ addu(V1, V1, T3);

  // T2 = borrow in = 0.
  __ mov(T2, ZR);
  Label sub_loop;
  __ Bind(&sub_loop);
  // Loop a_used times, a_used > 0.
  __ lw(T0, Address(T3, 0));  // T0 = x.
  __ addiu(T3, T3, Immediate(kBytesPerBigIntDigit));
  __ lw(T1, Address(T5, 0));  // T1 = y.
  __ addiu(T5, T5, Immediate(kBytesPerBigIntDigit));
  __ subu(T1, T0, T1);  // T1 = x - y.
  __ sltu(T4, T0, T1);  // T4 = borrow out of x - y.
  __ subu(T0, T1, T2);  // T0 = x - y - borrow in.
  __ sltu(T2, T1, T0);  // T2 = borrow out of (x - y) - borrow in.
  __ or_(T2, T2, T4);   // T2 = borrow out of x - y - borrow in.
  __ sw(T0, Address(T6, 0));
  __ bne(T3, V0, &sub_loop);
  __ delay_slot()->addiu(T6, T6, Immediate(kBytesPerBigIntDigit));

  Label done;
  __ beq(T3, V1, &done);

  Label borrow_loop;
  __ Bind(&borrow_loop);
  // Loop used - a_used times, used - a_used > 0.
  __ lw(T0, Address(T3, 0));  // T0 = x.
  __ addiu(T3, T3, Immediate(kBytesPerBigIntDigit));
  __ subu(T1, T0, T2);  // T1 = x - borrow in.
  __ sltu(T2, T0, T1);  // T2 = borrow out of x - borrow in.
  __ sw(T1, Address(T6, 0));
  __ bne(T3, V1, &borrow_loop);
  __ delay_slot()->addiu(T6, T6, Immediate(kBytesPerBigIntDigit));

  __ Bind(&done);
  // Returning Object::null() is not required, since this method is private.
  __ Ret();
}

void AsmIntrinsifier::Bigint_mulAdd(Assembler* assembler,
                                    Label* normal_ir_body) {
  // Pseudo code:
  // static int _mulAdd(Uint32List x_digits, int xi,
  //                    Uint32List m_digits, int i,
  //                    Uint32List a_digits, int j, int n) {
  //   uint32_t x = x_digits[xi >> 1];  // xi is Smi.
  //   if (x == 0 || n == 0) {
  //     return 1;
  //   }
  //   uint32_t* mip = &m_digits[i >> 1];  // i is Smi.
  //   uint32_t* ajp = &a_digits[j >> 1];  // j is Smi.
  //   uint32_t c = 0;
  //   SmiUntag(n);
  //   do {
  //     uint32_t mi = *mip++;
  //     uint32_t aj = *ajp;
  //     uint64_t t = x*mi + aj + c;  // 32-bit * 32-bit -> 64-bit.
  //     *ajp++ = low32(t);
  //     c = high32(t);
  //   } while (--n > 0);
  //   while (c != 0) {
  //     uint64_t t = *ajp + c;
  //     *ajp++ = low32(t);
  //     c = high32(t);  // c == 0 or 1.
  //   }
  //   return 1;
  // }

  Label done;
  // T3 = x, no_op if x == 0
  __ lw(T0, Address(SP, 5 * target::kWordSize));  // T0 = xi as Smi.
  __ lw(T1, Address(SP, 6 * target::kWordSize));  // T1 = x_digits.
  __ sll(T0, T0, 1);
  __ addu(T1, T0, T1);
  __ lw(T3, FieldAddress(T1, target::TypedData::payload_offset()));
  __ beq(T3, ZR, &done);

  // T6 = SmiUntag(n), no_op if n == 0
  __ lw(T6, Address(SP, 0 * target::kWordSize));
  __ SmiUntag(T6);
  __ beq(T6, ZR, &done);
  __ delay_slot()->addiu(T6, T6, Immediate(-1));  // ... while (n-- > 0).

  // T4 = mip = &m_digits[i >> 1]
  __ lw(T0, Address(SP, 3 * target::kWordSize));  // T0 = i as Smi.
  __ lw(T1, Address(SP, 4 * target::kWordSize));  // T1 = m_digits.
  __ sll(T0, T0, 1);
  __ addu(T1, T0, T1);
  __ addiu(T4, T1, Immediate(target::TypedData::payload_offset() - kHeapObjectTag));

  // T5 = ajp = &a_digits[j >> 1]
  __ lw(T0, Address(SP, 1 * target::kWordSize));  // T0 = j as Smi.
  __ lw(T1, Address(SP, 2 * target::kWordSize));  // T1 = a_digits.
  __ sll(T0, T0, 1);
  __ addu(T1, T0, T1);
  __ addiu(T5, T1, Immediate(target::TypedData::payload_offset() - kHeapObjectTag));

  // T1 = c = 0
  __ mov(T1, ZR);

  Label muladd_loop;
  __ Bind(&muladd_loop);
  // x:   T3
  // mip: T4
  // ajp: T5
  // c:   T1
  // n-1: T6

  // uint32_t mi = *mip++
  __ lw(T2, Address(T4, 0));

  // uint32_t aj = *ajp
  __ lw(T0, Address(T5, 0));

  // uint64_t t = x*mi + aj + c
  __ multu(T2, T3);  // HI:LO = x*mi.
  __ addiu(T4, T4, Immediate(kBytesPerBigIntDigit));
  __ mflo(V0);
  __ mfhi(V1);
  __ addu(V0, V0, T0);  // V0 = low32(x*mi) + aj.
  __ sltu(T7, V0, T0);  // T7 = carry out of low32(x*mi) + aj.
  __ addu(V1, V1, T7);  // V1:V0 = x*mi + aj.
  __ addu(T0, V0, T1);  // T0 = low32(x*mi + aj) + c.
  __ sltu(T7, T0, T1);  // T7 = carry out of low32(x*mi + aj) + c.
  __ addu(T1, V1, T7);  // T1 = c = high32(x*mi + aj + c).

  // *ajp++ = low32(t) = T0
  __ sw(T0, Address(T5, 0));
  __ addiu(T5, T5, Immediate(kBytesPerBigIntDigit));

  // while (n-- > 0)
  __ bgtz(T6, &muladd_loop);
  __ delay_slot()->addiu(T6, T6, Immediate(-1));  // --n

  __ beq(T1, ZR, &done);

  // *ajp++ += c
  __ lw(T0, Address(T5, 0));
  __ addu(T0, T0, T1);
  __ sltu(T1, T0, T1);
  __ sw(T0, Address(T5, 0));
  __ beq(T1, ZR, &done);
  __ delay_slot()->addiu(T5, T5, Immediate(kBytesPerBigIntDigit));

  Label propagate_carry_loop;
  __ Bind(&propagate_carry_loop);
  __ lw(T0, Address(T5, 0));
  __ addiu(T0, T0, Immediate(1));
  __ sw(T0, Address(T5, 0));
  __ beq(T0, ZR, &propagate_carry_loop);
  __ delay_slot()->addiu(T5, T5, Immediate(kBytesPerBigIntDigit));

  __ Bind(&done);
  __ addiu(V0, ZR, Immediate(target::ToRawSmi(1)));  // One digit processed.
  __ Ret();
}

void AsmIntrinsifier::Bigint_sqrAdd(Assembler* assembler,
                                    Label* normal_ir_body) {
  // Pseudo code:
  // static int _sqrAdd(Uint32List x_digits, int i,
  //                    Uint32List a_digits, int used) {
  //   uint32_t* xip = &x_digits[i >> 1];  // i is Smi.
  //   uint32_t x = *xip++;
  //   if (x == 0) return 1;
  //   uint32_t* ajp = &a_digits[i];  // j == 2*i, i is Smi.
  //   uint32_t aj = *ajp;
  //   uint64_t t = x*x + aj;
  //   *ajp++ = low32(t);
  //   uint64_t c = high32(t);
  //   int n = ((used - i) >> 1) - 1;  // used and i are Smi.
  //   while (--n >= 0) {
  //     uint32_t xi = *xip++;
  //     uint32_t aj = *ajp;
  //     uint96_t t = 2*x*xi + aj + c;  // 2-bit * 32-bit * 32-bit -> 65-bit.
  //     *ajp++ = low32(t);
  //     c = high64(t);  // 33-bit.
  //   }
  //   uint32_t aj = *ajp;
  //   uint64_t t = aj + c;  // 32-bit + 33-bit -> 34-bit.
  //   *ajp++ = low32(t);
  //   *ajp = high32(t);
  //   return 1;
  // }

  // T4 = xip = &x_digits[i >> 1]
  __ lw(T2, Address(SP, 2 * target::kWordSize));  // T2 = i as Smi.
  __ lw(T3, Address(SP, 3 * target::kWordSize));  // T3 = x_digits.
  __ sll(T0, T2, 1);
  __ addu(T3, T0, T3);
  __ addiu(T4, T3, Immediate(target::TypedData::payload_offset() - kHeapObjectTag));

  // T3 = x = *xip++, return if x == 0
  Label x_zero;
  __ lw(T3, Address(T4, 0));
  __ beq(T3, ZR, &x_zero);
  __ delay_slot()->addiu(T4, T4, Immediate(kBytesPerBigIntDigit));

  // T5 = ajp = &a_digits[i]
  __ lw(T1, Address(SP, 1 * target::kWordSize));  // a_digits
  __ sll(T0, T2, 2);                      // j == 2*i, i is Smi.
  __ addu(T1, T0, T1);
  __ addiu(T5, T1, Immediate(target::TypedData::payload_offset() - kHeapObjectTag));

  // T6:T0 = t = x*x + *ajp
  __ lw(T0, Address(T5, 0));  // *ajp.
  __ mthi(ZR);
  __ mtlo(T0);
  __ maddu(T3, T3);  // HI:LO = T3*T3 + *ajp.
  __ mfhi(T6);
  __ mflo(T0);

  // *ajp++ = low32(t) = R0
  __ sw(T0, Address(T5, 0));
  __ addiu(T5, T5, Immediate(kBytesPerBigIntDigit));

  // T6 = low32(c) = high32(t)
  // T7 = high32(c) = 0
  __ mov(T7, ZR);

  // int n = used - i - 1; while (--n >= 0) ...
  __ lw(T0, Address(SP, 0 * target::kWordSize));  // used is Smi
  __ subu(V0, T0, T2);
  __ SmiUntag(V0);  // V0 = used - i
  // int n = used - i - 2; if (n >= 0) ... while (n-- > 0)
  __ addiu(V0, V0, Immediate(-2));

  Label loop, done;
  __ bltz(V0, &done);

  __ Bind(&loop);
  // x:   T3
  // xip: T4
  // ajp: T5
  // c:   T7:T6
  // t:   A2:A1:A0 (not live at loop entry)
  // n:   V0

  // uint32_t xi = *xip++
  __ lw(T2, Address(T4, 0));
  __ addiu(T4, T4, Immediate(kBytesPerBigIntDigit));

  // uint32_t aj = *ajp
  __ lw(T0, Address(T5, 0));

  // uint96_t t = T7:T6:T0 = 2*x*xi + aj + c
  __ multu(T2, T3);
  __ mfhi(A1);
  __ mflo(A0);  // A1:A0 = x*xi.
  __ srl(A2, A1, 31);
  __ sll(A1, A1, 1);
  __ srl(T1, A0, 31);
  __ or_(A1, A1, T1);
  __ sll(A0, A0, 1);  // A2:A1:A0 = 2*x*xi.
  __ addu(A0, A0, T0);
  __ sltu(T1, A0, T0);
  __ addu(A1, A1, T1);  // No carry out possible; A2:A1:A0 = 2*x*xi + aj.
  __ addu(T0, A0, T6);
  __ sltu(T1, T0, T6);
  __ addu(T6, A1, T1);  // No carry out; A2:T6:T0 = 2*x*xi + aj + low32(c).
  __ addu(T6, T6, T7);  // No carry out; A2:T6:T0 = 2*x*xi + aj + c.
  __ mov(T7, A2);       // T7:T6:T0 = 2*x*xi + aj + c.

  // *ajp++ = low32(t) = T0
  __ sw(T0, Address(T5, 0));
  __ addiu(T5, T5, Immediate(kBytesPerBigIntDigit));

  // while (n-- > 0)
  __ bgtz(V0, &loop);
  __ delay_slot()->addiu(V0, V0, Immediate(-1));  // --n

  __ Bind(&done);
  // uint32_t aj = *ajp
  __ lw(T0, Address(T5, 0));

  // uint64_t t = aj + c
  __ addu(T6, T6, T0);
  __ sltu(T1, T6, T0);
  __ addu(T7, T7, T1);

  // *ajp = low32(t) = T6
  // *(ajp + 1) = high32(t) = T7
  __ sw(T6, Address(T5, 0));
  __ sw(T7, Address(T5, kBytesPerBigIntDigit));

  __ Bind(&x_zero);
  __ addiu(V0, ZR, Immediate(target::ToRawSmi(1)));  // One digit processed.
  __ Ret();
}

void AsmIntrinsifier::Bigint_estimateQuotientDigit(Assembler* assembler,
                                                   Label* normal_ir_body) {
  // No unsigned 64-bit / 32-bit divide instruction.
}

void AsmIntrinsifier::Montgomery_mulMod(Assembler* assembler,
                                        Label* normal_ir_body) {
  // T4 = args
  __ lw(T4, Address(SP, 2 * target::kWordSize));  // args

  // T3 = rho = args[2]
  __ lw(T3, FieldAddress(
                T4, target::TypedData::payload_offset() + 2 * kBytesPerBigIntDigit));

  // T2 = d = digits[i >> 1]
  __ lw(T0, Address(SP, 0 * target::kWordSize));  // T0 = i as Smi.
  __ lw(T1, Address(SP, 1 * target::kWordSize));  // T1 = digits.
  __ sll(T0, T0, 1);
  __ addu(T1, T0, T1);
  __ lw(T2, FieldAddress(T1, target::TypedData::payload_offset()));

  // HI:LO = t = rho*d
  __ multu(T2, T3);

  // args[4] = t mod DIGIT_BASE = low32(t)
  __ mflo(T0);
  __ sw(T0, FieldAddress(
                T4, target::TypedData::payload_offset() + 4 * kBytesPerBigIntDigit));

  __ addiu(V0, ZR, Immediate(target::ToRawSmi(1)));  // One digit processed.
  __ Ret();
}

// Check if the last argument is a double, jump to label 'is_smi' if smi
// (easy to convert to double), otherwise jump to label 'not_double_smi',
// Returns the last argument in T0.
static void TestLastArgumentIsDouble(Assembler* assembler,
                                     Label* is_smi,
                                     Label* not_double_smi) {
  __ lw(T0, Address(SP, 0 * target::kWordSize));
  __ AndImmediate(CMPRES1, T0, kSmiTagMask);
  __ beq(CMPRES1, ZR, is_smi);
  __ LoadClassId(CMPRES1, T0);
  __ BranchNotEqual(CMPRES1, Immediate(kDoubleCid), not_double_smi);
  // Fall through with Double in T0.
}

// Both arguments on stack, arg0 (left) is a double, arg1 (right) is of unknown
// type. Return true or false object in the register V0. Any NaN argument
// returns false. Any non-double arg1 causes control flow to fall through to the
// slow case (compiled method body).
static void CompareDoubles(Assembler* assembler,
                           Label* normal_ir_body,
                           Condition cond) {
  Label is_smi, double_op, no_NaN;
  __ Comment("CompareDoubles Intrinsic");

  TestLastArgumentIsDouble(assembler, &is_smi, normal_ir_body);
  // Both arguments are double, right operand is in T0.
  __ LoadDFromOffset(D1, T0, target::Double::value_offset() - kHeapObjectTag);
  __ Bind(&double_op);
  __ lw(T0, Address(SP, 1 * target::kWordSize));  // Left argument.
  __ LoadDFromOffset(D0, T0, target::Double::value_offset() - kHeapObjectTag);
  // Now, left is in D0, right is in D1.

  __ cund(D0, D1);  // Check for NaN.
  __ bc1f(&no_NaN);
  __ LoadObject(V0, CastHandle<Object>(FalseObject()));  // Return false if either is NaN.
  __ Ret();
  __ Bind(&no_NaN);

  switch (cond) {
    case EQ:
      __ ceqd(D0, D1);
      break;
    case LT:
      __ coltd(D0, D1);
      break;
    case LE:
      __ coled(D0, D1);
      break;
    case GT:
      __ coltd(D1, D0);
      break;
    case GE:
      __ coled(D1, D0);
      break;
    default: {
      // Only passing the above conditions to this function.
      UNREACHABLE();
      break;
    }
  }

  Label is_true;
  __ bc1t(&is_true);
  __ LoadObject(V0, CastHandle<Object>(FalseObject()));
  __ Ret();
  __ Bind(&is_true);
  __ LoadObject(V0, CastHandle<Object>(TrueObject()));
  __ Ret();


  __ Bind(&is_smi);
  __ SmiUntag(T0);
  __ mtc1(T0, STMP1);
  __ b(&double_op);
  __ delay_slot()->cvtdw(D1, STMP1);


  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Double_greaterThan(Assembler* assembler,
                                         Label* normal_ir_body) {
  CompareDoubles(assembler, normal_ir_body, GT);
}

void AsmIntrinsifier::Double_greaterEqualThan(Assembler* assembler,
                                              Label* normal_ir_body) {
  CompareDoubles(assembler, normal_ir_body, GE);
}

void AsmIntrinsifier::Double_lessThan(Assembler* assembler,
                                      Label* normal_ir_body) {
  CompareDoubles(assembler, normal_ir_body, LT);
}

void AsmIntrinsifier::Double_equal(Assembler* assembler,
                                   Label* normal_ir_body) {
  CompareDoubles(assembler, normal_ir_body, EQ);
}

void AsmIntrinsifier::Double_lessEqualThan(Assembler* assembler,
                                           Label* normal_ir_body) {
  CompareDoubles(assembler, normal_ir_body, LE);
}

// Expects left argument to be double (receiver). Right argument is unknown.
// Both arguments are on stack.
static void DoubleArithmeticOperations(Assembler* assembler,
                                       Label* normal_ir_body,
                                       Token::Kind kind) {
  Label is_smi, double_op;

  TestLastArgumentIsDouble(assembler, &is_smi, normal_ir_body);
  // Both arguments are double, right operand is in T0.
  __ lwc1(F2, FieldAddress(T0, target::Double::value_offset()));
  __ lwc1(F3, FieldAddress(T0, target::Double::value_offset() + target::kWordSize));
  __ Bind(&double_op);
  __ lw(T0, Address(SP, 1 * target::kWordSize));  // Left argument.
  __ lwc1(F0, FieldAddress(T0, target::Double::value_offset()));
  __ lwc1(F1, FieldAddress(T0, target::Double::value_offset() + target::kWordSize));
  switch (kind) {
    case Token::kADD:
      __ addd(D0, D0, D1);
      break;
    case Token::kSUB:
      __ subd(D0, D0, D1);
      break;
    case Token::kMUL:
      __ muld(D0, D0, D1);
      break;
    case Token::kDIV:
      __ divd(D0, D0, D1);
      break;
    default:
      UNREACHABLE();
  }
  const Class& double_class = DoubleClass();
  __ TryAllocate(double_class, normal_ir_body, Assembler::kFarJump, V0, T1);  // Result register.
  __ swc1(F0, FieldAddress(V0, target::Double::value_offset()));
  __ Ret();
  __ delay_slot()->swc1(F1,
                        FieldAddress(V0, target::Double::value_offset() + target::kWordSize));

  __ Bind(&is_smi);
  __ SmiUntag(T0);
  __ mtc1(T0, STMP1);
  __ b(&double_op);
  __ delay_slot()->cvtdw(D1, STMP1);

  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Double_add(Assembler* assembler, Label* normal_ir_body) {
  DoubleArithmeticOperations(assembler, normal_ir_body, Token::kADD);
}

void AsmIntrinsifier::Double_mul(Assembler* assembler, Label* normal_ir_body) {
  DoubleArithmeticOperations(assembler, normal_ir_body, Token::kMUL);
}

void AsmIntrinsifier::Double_sub(Assembler* assembler, Label* normal_ir_body) {
  DoubleArithmeticOperations(assembler, normal_ir_body, Token::kSUB);
}

void AsmIntrinsifier::Double_div(Assembler* assembler, Label* normal_ir_body) {
  DoubleArithmeticOperations(assembler, normal_ir_body, Token::kDIV);
}

// Left is double right is integer (Bigint, Mint or Smi)
void AsmIntrinsifier::Double_mulFromInteger(Assembler* assembler,
                                            Label* normal_ir_body) {
  // Only smis allowed.
  __ lw(T0, Address(SP, 0 * target::kWordSize));
  __ AndImmediate(CMPRES1, T0, kSmiTagMask);
  __ bne(CMPRES1, ZR, normal_ir_body);

  // Is Smi.
  __ SmiUntag(T0);
  __ mtc1(T0, F4);
  __ cvtdw(D1, F4);

  __ lw(T0, Address(SP, 1 * target::kWordSize));
  __ lwc1(F0, FieldAddress(T0, target::Double::value_offset()));
  __ lwc1(F1, FieldAddress(T0, target::Double::value_offset() + target::kWordSize));
  __ muld(D0, D0, D1);
  const Class& double_class = DoubleClass();
  __ TryAllocate(double_class, normal_ir_body, Assembler::kFarJump, V0, T1);  // Result register.
  __ swc1(F0, FieldAddress(V0, target::Double::value_offset()));
  __ Ret();
  __ delay_slot()->swc1(F1,
                        FieldAddress(V0, target::Double::value_offset() + target::kWordSize));
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::DoubleFromInteger(Assembler* assembler,
                                        Label* normal_ir_body) {

  __ lw(T0, Address(SP, 0 * target::kWordSize));
  __ AndImmediate(CMPRES1, T0, kSmiTagMask);
  __ bne(CMPRES1, ZR, normal_ir_body);

  // Is Smi.
  __ SmiUntag(T0);
  __ mtc1(T0, F4);
  __ cvtdw(D0, F4);
  const Class& double_class = DoubleClass();
  __ TryAllocate(double_class, normal_ir_body, Assembler::kFarJump, V0, T1);  // Result register.
  __ swc1(F0, FieldAddress(V0, target::Double::value_offset()));
  __ Ret();
  __ delay_slot()->swc1(F1,
                        FieldAddress(V0, target::Double::value_offset() + target::kWordSize));
  __ Bind(normal_ir_body);
}

void AsmIntrinsifier::Double_getIsNaN(Assembler* assembler,
                                      Label* normal_ir_body) {
  Label is_true;

  __ lw(T0, Address(SP, 0 * target::kWordSize));
  __ lwc1(F0, FieldAddress(T0, target::Double::value_offset()));
  __ lwc1(F1, FieldAddress(T0, target::Double::value_offset() + target::kWordSize));
  __ cund(D0, D0);  // Check for NaN.
  __ bc1t(&is_true);
  __ LoadObject(V0, CastHandle<Object>(FalseObject()));  // Return false if either is NaN.
  __ Ret();
  __ Bind(&is_true);
  __ LoadObject(V0, CastHandle<Object>(TrueObject()));
  __ Ret();
}

void AsmIntrinsifier::Double_getIsInfinite(Assembler* assembler,
                                           Label* normal_ir_body) {
  Label not_inf;
  __ lw(T0, Address(SP, 0 * target::kWordSize));
  __ lw(T1, FieldAddress(T0, target::Double::value_offset()));
  __ lw(T2, FieldAddress(T0, target::Double::value_offset() + target::kWordSize));
  // If the low word isn't zero, then it isn't infinity.
  __ bne(T1, ZR, &not_inf);
  // Mask off the sign bit.
  __ AndImmediate(T2, T2, 0x7FFFFFFF);
  // Compare with +infinity.
  __ BranchNotEqual(T2, Immediate(0x7FF00000), &not_inf);

  __ LoadObject(V0, CastHandle<Object>(TrueObject()));
  __ Ret();

  __ Bind(&not_inf);
  __ LoadObject(V0, CastHandle<Object>(FalseObject()));
  __ Ret();
}

void AsmIntrinsifier::Double_getIsNegative(Assembler* assembler,
                                           Label* normal_ir_body) {
  Label is_false, is_true, is_zero;
  __ lw(T0, Address(SP, 0 * target::kWordSize));
  __ LoadDFromOffset(D0, T0, target::Double::value_offset() - kHeapObjectTag);

  __ cund(D0, D0);
  __ bc1t(&is_false);  // NaN -> false.

  __ LoadImmediate(D1, 0.0);
  __ ceqd(D0, D1);
  __ bc1t(&is_zero);  // Check for negative zero.

  __ coled(D1, D0);
  __ bc1t(&is_false);  // >= 0 -> false.

  __ Bind(&is_true);
  __ LoadObject(V0, CastHandle<Object>(TrueObject()));
  __ Ret();

  __ Bind(&is_false);
  __ LoadObject(V0, CastHandle<Object>(FalseObject()));
  __ Ret();

  __ Bind(&is_zero);
  // Check for negative zero by looking at the sign bit.
  __ mfc1(T0, F1);                     // Moves bits 32...63 of D0 to T0.
  __ srl(T0, T0, 31);                  // Get the sign bit down to bit 0 of T0.
  __ AndImmediate(CMPRES1, T0, 1);  // Check if the bit is set.
  __ bne(T0, ZR, &is_true);            // Sign bit set. True.
  __ b(&is_false);
}

void AsmIntrinsifier::ObjectEquals(Assembler* assembler,
                                   Label* normal_ir_body) {
  Label is_true;

  __ lw(T0, Address(SP, 0 * target::kWordSize));
  __ lw(T1, Address(SP, 1 * target::kWordSize));
  __ beq(T0, T1, &is_true);
  __ LoadObject(V0, CastHandle<Object>(FalseObject()));
  __ Ret();
  __ Bind(&is_true);
  __ LoadObject(V0, CastHandle<Object>(TrueObject()));
  __ Ret();
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

// Return type quickly for simple types (not parameterized and not signature).
void AsmIntrinsifier::ObjectRuntimeType(Assembler* assembler,
                                        Label* normal_ir_body) {
  Label use_declaration_type, not_integer, not_double, not_string;
  __ lw(T0, Address(SP, 0 * target::kWordSize));
  __ LoadClassIdMayBeSmi(T1, T0);

  // Instance is a closure.
  __ BranchEqual(T1, Immediate(kClosureCid), normal_ir_body);

  // Instance is a record.
  __ BranchEqual(T1, Immediate(kRecordCid), normal_ir_body);

  __ BranchUnsignedGreaterEqual(T1, Immediate(kNumPredefinedCids),
                                &use_declaration_type);

  __ LoadIsolateGroup(T2);
  __ LoadFromOffset(T2, T2, target::IsolateGroup::object_store_offset());

  __ BranchNotEqual(T1, Immediate(kDoubleCid), &not_double);

  __ LoadFromOffset(V0, T2, target::ObjectStore::double_type_offset());
  __ Ret();

  __ Bind(&not_double);
  JumpIfNotInteger(assembler, T1, V0, &not_integer);
  // Object is an integer.
  __ LoadFromOffset(V0, T2, target::ObjectStore::int_type_offset());
  __ Ret();

  __ Bind(&not_integer);
  JumpIfNotString(assembler, T1, V0, &not_string);
  // Object is a string.
  __ LoadFromOffset(V0, T2, target::ObjectStore::string_type_offset());
  __ Ret();

  __ Bind(&not_string);
  JumpIfNotType(assembler, T1, V0, &use_declaration_type);
  __ LoadFromOffset(V0, T2, target::ObjectStore::type_type_offset());
  __ Ret();

  __ Bind(&use_declaration_type);
  __ LoadClassById(T2, T1);
  __ lhu(T3, FieldAddress(T2, target::Class::num_type_arguments_offset()));
  __ BranchNotEqual(T3, Immediate(0), normal_ir_body);

  __ lw(V0, FieldAddress(T2, target::Class::declaration_type_offset()));
  __ BranchEqual(V0, NullObject(), normal_ir_body);
  __ Ret();

  __ Bind(normal_ir_body);
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

void AsmIntrinsifier::ObjectHaveSameRuntimeType(Assembler* assembler,
                                                Label* normal_ir_body) {
  __ lw(T0, Address(SP, 1 * target::kWordSize));
  __ lw(T1, Address(SP, 0 * target::kWordSize));
  __ LoadClassIdMayBeSmi(T2, T1);
  __ LoadClassIdMayBeSmi(T1, T0);

  Label equal_may_be_generic, equal, not_equal;
  EquivalentClassIds(assembler, normal_ir_body, &equal_may_be_generic, &equal,
                     &not_equal, T1, T2, TMP,
                     /* testing_instance_cids = */ true);

  __ Bind(&equal_may_be_generic);
  // Classes are equivalent and neither is a closure class.
  // Check if there are no type arguments. In this case we can return true.
  // Otherwise fall through into the runtime to handle comparison.
  __ LoadClassById(T0, T1);
  __ lw(T3,
        FieldAddress(
            T0,
            target::Class::host_type_arguments_field_offset_in_words_offset()));
  __ BranchEqual(T3, Immediate(target::Class::kNoTypeArguments), &equal);

  // Compare type arguments, host_type_arguments_field_offset_in_words in T0.
  __ lw(T0, Address(SP, 1 * target::kWordSize));
  __ lw(T1, Address(SP, 0 * target::kWordSize));
  __ sll(T3, T3, target::kCompressedWordSizeLog2);
  __ addu(T0, T0, T3);
  __ addu(T1, T1, T3);
  __ lw(T0, FieldAddress(T0, 0));
  __ lw(T1, FieldAddress(T1, 0));
  __ bne(T0, T1, normal_ir_body);
  // Fall through to equal case if type arguments are equal.

  __ Bind(&equal);
  __ LoadObject(V0, CastHandle<Object>(TrueObject()));
  __ Ret();

  __ Bind(&not_equal);
  __ LoadObject(V0, CastHandle<Object>(FalseObject()));
  __ Ret();

  __ Bind(normal_ir_body);
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

// Copyright (c) 2025, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef RUNTIME_VM_CONSTANTS_MIPS_H_
#define RUNTIME_VM_CONSTANTS_MIPS_H_

namespace dart {

// There is no dedicated status register on MIPS, but Condition values are used
// and passed around by the intermediate language, so we need a Condition type.
// We delay code generation of a comparison that would result in a traditional
// condition code in the status register by keeping both register operands and
// the relational operator between them as the Condition.

class Condition : public ValueObject {
 public:
  enum RelationOperator {
    kNoCondition = -1,
    AL = 0,    // always
    NV = 1,    // never
    EQ = 2,    // equal
    NE = 3,    // not equal
    GE = 4,    // greater equal
    LT = 5,    // less than
    GT = 6,    // greater than
    LE = 7,    // less equal
    UGT = 8,   // unsigned greater than
    ULE = 9,   // unsigned less equal
    UGE = 10,  // unsigned greater equal
    ULT = 11,  // unsigned less than
    kSpecialCondition = 12,
    kNumberOfConditions = 13,

    // Platform-independent variants declared for all platforms
    EQUAL = EQ,
    ZERO = EQUAL,
    NOT_EQUAL = NE,
    NOT_ZERO = NOT_EQUAL,
    LESS = LT,
    LESS_EQUAL = LE,
    GREATER_EQUAL = GE,
    GREATER = GT,
    UNSIGNED_LESS = ULT,
    UNSIGNED_LESS_EQUAL = ULE,
    UNSIGNED_GREATER = UGT,
    UNSIGNED_GREATER_EQUAL = UGE,

    INVALID_RELATION = 13,
    kInvalidCondition = INVALID_RELATION
  };
};   

}  // namespace dart

#endif  // RUNTIME_VM_CONSTANTS_MIPS_H_

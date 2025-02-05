// |reftest| skip-if(!this.hasOwnProperty('BigInt')) error:SyntaxError -- BigInt is not enabled unconditionally
// Copyright (C) 2017 The V8 Project authors. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-numeric-literal-static-semantics-early-errors
description: > 
  It is a Syntax Error if the MV is not an integer. (decimalIntegerLiteral dot decimalDigits)
info: |
  Static Semantics: BigInt Value

  NumericLiteral :: NumericLiteralBase NumericLiteralSuffix

  1. Assert: NumericLiteralSuffix is n.
  2. Let the value of NumericLiteral be the MV of NumericLiteralBase represented as BigInt.

  DecimalLiteral ::
    DecimalIntegerLiteral . DecimalDigits_opt
    . DecimalDigits
features: [BigInt]
negative:
  phase: parse
  type: SyntaxError
---*/

throw "Test262: This statement should not be evaluated.";

2017.8n;

/*
                 __________
    _____   __ __\______   \_____  _______  ______  ____ _______
   /     \ |  |  \|     ___/\__  \ \_  __ \/  ___/_/ __ \\_  __ \
  |  Y Y  \|  |  /|    |     / __ \_|  | \/\___ \ \  ___/ |  | \/
  |__|_|  /|____/ |____|    (____  /|__|  /____  > \___  >|__|
        \/                       \/            \/      \/
  Copyright (C) 2012 Ingo Berg

  Permission is hereby granted, free of charge, to any person obtaining a copy of this
  software and associated documentation files (the "Software"), to deal in the Software
  without restriction, including without limitation the rights to use, copy, modify,
  merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
  permit persons to whom the Software is furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all copies or
  substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
  NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
  DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
#ifndef MU_PARSER_H
#define MU_PARSER_H

//--- Standard includes ------------------------------------------------------------------------
#include <vector>

//--- Parser includes --------------------------------------------------------------------------
#include "muParserBase.h"

/** \file
    \brief Definition of the standard floating point parser.
*/

namespace mu
{
  /** \brief Mathematical expressions parser.

    Standard implementation of the mathematical expressions parser.
    Can be used as a reference implementation for subclassing the parser.

    <small>
    (C) 2011 Ingo Berg<br>
    muparser(at)gmx.de
    </small>
  */
  /* final */ class Parser : public ParserBase
  {
  public:

    Parser();

    virtual void InitCharSets() override;
    virtual void InitFun() override;
    virtual void InitConst() override;
    virtual void InitOprt() override;
    virtual void OnDetectVar(string_type *pExpr, int &nStart, int &nEnd) override;

    std::vector<Array> Diff(Value *a_Var,
                            const Array& a_fPos,
                            Value a_fEpsilon = 0.0,
                            size_t order = 1);

  protected:

    // Trigonometric functions
#warning FIXME (numere#1#06/29/24): Move those to function implementation file
    static Array  Sin(const Array&);
    static Array  Cos(const Array&);
    static Array  Tan(const Array&);
    static Array  Tan2(const Array&, const Array&);
    // arcus functions
    static Array  ASin(const Array&);
    static Array  ACos(const Array&);
    static Array  ATan(const Array&);
    static Array  ATan2(const Array&, const Array&);

    // hyperbolic functions
    static Array  Sinh(const Array&);
    static Array  Cosh(const Array&);
    static Array  Tanh(const Array&);
    // arcus hyperbolic functions
    static Array  ASinh(const Array&);
    static Array  ACosh(const Array&);
    static Array  ATanh(const Array&);
    // Logarithm functions
    static Array  Log2(const Array&);  // Logarithm Base 2
    static Array  Log10(const Array&); // Logarithm Base 10
    static Array  Ln(const Array&);    // Logarithm Base e (natural logarithm)
    // misc
    static Array  Exp(const Array&);
    static Array  Abs(const Array&);
    static Array  Sqrt(const Array&);
    static Array  Rint(const Array&);
    static Array  Sign(const Array&);

    // Prefix operators
    // !!! Unary Minus is a MUST if you want to use negative signs !!!
    static Array  UnaryMinus(const Array&);
    static Array  UnaryPlus(const Array&);
    static Array  LogicalNot(const Array&);

    // Functions with variable number of arguments
#warning FIXME (numere#1#06/29/24): Some of those should stay here
    static Array Sum(const Array*, int);  // sum
    static Array Avg(const Array*, int);  // mean value
    static Array Min(const Array*, int);  // minimum
    static Array Max(const Array*, int);  // maximum

    static int IsVal(const char_type* a_szExpr, int *a_iPos, Value *a_fVal);
  };
} // namespace mu

#endif



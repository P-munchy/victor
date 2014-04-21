/**
File: utilities_declarations.h
Author: Peter Barnum
Created: 2013

Various simple macros and utilities.

Copyright Anki, Inc. 2013
For internal use only. No part of this code may be used without a signed non-disclosure agreement with Anki, inc.
**/

#ifndef _ANKICORETECHEMBEDDED_COMMON_UTILITIES_DECLARATIONS_H_
#define _ANKICORETECHEMBEDDED_COMMON_UTILITIES_DECLARATIONS_H_

#include "anki/common/robot/config.h"
#include "anki/common/robot/errorHandling.h"
#include "anki/common/robot/utilities_c.h"

namespace Anki
{
  namespace Embedded
  {
    template<typename Type> class Array;
    template<typename Type> class FixedLengthList;

    template<typename Type> inline Type RoundUp(const Type number, const Type multiple);

    template<typename Type> inline Type RoundDown(const Type number, const Type multiple);

    // Only s32, u32, f32, and f64 outputs are supported. If you need something else, also do a saturate_cast
    // Rounding to unsigned also saturates negative numbers to zero
    template<typename Type> inline Type Round(const f32 v);
    template<typename Type> inline Type Round(const f64 v);

    // Some templated functions should only round a result if it is an integer.
    // Using saturate_cast<> would work, but RoundIfInteger is faster if the value is never too big or small.
    template<typename Type> inline Type RoundIfInteger(const f32 v);
    template<typename Type> inline Type RoundIfInteger(const f64 v);

    // Taylor-series approximation of an exponential function
    template<typename Type> Type approximateExp(const Type exponent, const s32 numTerms = 10);

    // Swap a with b
    template<typename Type> void Swap(Type &a, Type &b);

    template<typename Type> u32 BinaryStringToUnsignedNumber(const FixedLengthList<Type> &bits, bool firstBitIsLow = false);

    // Simple matrix operations
    // |a b|
    // |c d|
    // return a*d - b*c;
    template<typename Type> Type Determinant2x2(const Type a, const Type b, const Type c, const Type d);

    // |a b c|
    // |d e f|
    // |g h i|
    // return (aei + bfg + cfh) - (ceg + bdi + afh)
    template<typename Type> Type Determinant3x3(const Type a, const Type b, const Type c, const Type d, const Type e, const Type f, const Type g, const Type h, const Type i);

    // Invert:
    // [a b c]
    // [d e f]
    // [g h i]
    template<typename Type> void Invert3x3(Type &a, Type &b, Type &c, Type &d, Type &e, Type &f, Type &g, Type &h, Type &i);

    // Cartesian coordinates to Polar coordinates
    template<typename Type> void Cart2Pol(const Type x, const Type y, Type &rho, Type &theta);
    template<typename Type> void Pol2Cart(const Type rho, const Type theta, Type &x, Type &y);

    // This saturate cast is similar to OpenCV 2.4.8, except:
    // 1. It saturates signed to unsigned without wrapping (OpenCV does not clip negative s32 ints, which makes -1 become 0xffffffff etc. I don't know why?)
    // 2. It includes some cases that were missing
    // 3. It doesn't use saturate_casts that call other saturate_casts, which helps less-sophisticated compilers
    //
    // WARNING: When rounding from very large floats to ints, the exact value may differ on the M4
    //          and PC. For some examples, see the unit test "CoreTech_Common RoundAndSaturate".
    template<typename Type> inline Type saturate_cast(const u8  v);
    template<typename Type> inline Type saturate_cast(const s8  v);
    template<typename Type> inline Type saturate_cast(const u16 v);
    template<typename Type> inline Type saturate_cast(const s16 v);
    template<typename Type> inline Type saturate_cast(const u32 v);
    template<typename Type> inline Type saturate_cast(const s32 v);
    template<typename Type> inline Type saturate_cast(const u64 v);
    template<typename Type> inline Type saturate_cast(const s64 v);
    template<typename Type> inline Type saturate_cast(const f32 v);
    template<typename Type> inline Type saturate_cast(const f64 v);

#if ANKICORETECH_EMBEDDED_USE_OPENCV
    // Converts from typeid names to openCV types
    int ConvertToOpenCvType(const char *typeName, size_t byteDepth);
#endif // #if ANKICORETECH_EMBEDDED_USE_OPENCV

    // Returns the index of the first instance of bytePattern in buffer. If the pattern is not found, returns -1
    // Warning: bytePattern must not contain repeated bytes
    s32 FindBytePattern(const void * restrict buffer, const s32 bufferLength, const u8 * restrict bytePattern, const s32 bytePatternLength);

    // Returns a number [minLimit, maxLimit]
    s32 RandS32(const s32 minLimit, const s32 maxLimit);
  } // namespace Embedded
} // namespace Anki

#endif // _ANKICORETECHEMBEDDED_COMMON_UTILITIES_DECLARATIONS_H_

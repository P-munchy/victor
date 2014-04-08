/**
File: config.h
Author: Peter Barnum
Created: 2013

Basic configuation file for all common files, as well as all other coretech libraries.
Everything in this file should be compatible with plain C, as well as C++

Copyright Anki, Inc. 2013
For internal use only. No part of this code may be used without a signed non-disclosure agreement with Anki, inc.
**/

#ifndef _ANKICORETECHEMBEDDED_COMMON_CONFIG_H_
#define _ANKICORETECHEMBEDDED_COMMON_CONFIG_H_

//
// Compiler-specific configuration, with various defines that make different compilers work on the same code
//

// Section directives to allocate memory in specific places
#ifdef ROBOT_HARDWARE  // STM32F4 version
// NOTE: If you don't define a location, read-only will go to IROM, and read-write will go to RW_IRAM2 (Core-coupled memory)
#define OFFCHIP __attribute__((section("OFFCHIP")))
#define ONCHIP __attribute__((section("ONCHIP")))
#define CCM __attribute__((section("CCM")))
#else
#define OFFCHIP
#define ONCHIP
#define CCM
#endif

#if defined(_MSC_VER) // We're using the MSVC compiler
#pragma warning(disable: 4068) // Disable warning for unknown pragma
#pragma warning(disable: 4127) // Disable warning for conditional expression is constant
//#pragma warning(2: 4100) // Enable warning for unused variable warning
#pragma warning(disable: 4800) // Disable warning for 'BOOL' : forcing value to bool 'true' or 'false' (performance warning)

#ifndef __PRETTY_FUNCTION__
#define __PRETTY_FUNCTION__ __FUNCSIG__
#endif

#ifndef restrict
#define restrict
#endif

#ifndef snprintf
#define snprintf sprintf_s
#endif

#ifndef vsprintf_s
#define vsnprintf vsprintf_s
#endif

#ifndef staticInline
#define staticInline static __inline
#endif

#ifndef NO_INLINE
#define NO_INLINE
#endif

#ifdef _DEBUG
#define ANKI_DEBUG_LEVEL ANKI_DEBUG_ERRORS_AND_WARNS_AND_ASSERTS
#else
#define ANKI_DEBUG_LEVEL ANKI_DEBUG_ERRORS
#endif

// Warning: High levels of compiler optimization could cause this test to not work
#define isnan(a) ((a) != (a))

// Hack, because __EDG__ is used to detect the ARM compiler
#undef __EDG__

#endif // #if defined(_MSC_VER)

#if defined(__APPLE_CC__) // Apple Xcode

#ifndef restrict
#define restrict
#endif

#ifndef _strcmpi
#define _strcmpi strcasecmp
#endif

#ifndef staticInline
#define staticInline static inline
#endif

#ifndef NO_INLINE
#define NO_INLINE
#endif

#define ANKI_DEBUG_LEVEL ANKI_DEBUG_ERRORS_AND_WARNS_AND_ASSERTS

#endif // #if defined(__APPLE_CC__)

// WARNING: Visual Studio also defines __EDG__
#if defined(__EDG__)  // MDK-ARM

#ifndef restrict
#define restrict __restrict
#endif

#ifndef staticInline
#define staticInline static __inline
#endif

#ifndef NO_INLINE
#define NO_INLINE __declspec(noinline)
#endif

#define ANKI_DEBUG_LEVEL ANKI_DEBUG_ERRORS

//#define ARM_MATH_CM4
//#define ARM_MATH_ROUNDING
//#define __FPU_PRESENT 1

//#include "ARMCM4.h"
//#include "arm_math.h"

#endif // #if defined(__EDG__)  // MDK-ARM

//
// Non-compiler-specific configuration
//

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <float.h>
#include <stdarg.h>

#include "anki/common/types.h"
#include "anki/common/constantsAndMacros.h"

#ifdef __cplusplus
}
#endif

// If we're not building mex (which will replace printf w/ mexPrintf),
// then we want to swap printf for explicitPrintf
#ifndef ANKI_MEX_BUILD
#undef printf
#define printf(...) explicitPrintf(0, 0, __VA_ARGS__)
#endif

#define ANKICORETECHEMBEDDED_VERSION_MAJOR 0
#define ANKICORETECHEMBEDDED_VERSION_MINOR 1
#define ANKICORETECHEMBEDDED_VERSION_REVISION 0

// To support 128-bit SIMD loads and stores
#define MEMORY_ALIGNMENT_RAW 16 // Sometimes the preprocesor can't handle the safer version MEMORY_ALIGNMENT
#define MEMORY_ALIGNMENT ( (size_t)(MEMORY_ALIGNMENT_RAW) )

// To make processing faster, some kernels require image widths that are a multiple of ANKI_VISION_IMAGE_WIDTH_MULTIPLE
#define ANKI_VISION_IMAGE_WIDTH_SHIFT 4
#define ANKI_VISION_IMAGE_WIDTH_MULTIPLE (1<<ANKI_VISION_IMAGE_WIDTH_SHIFT)

// All scales will be relative to this base image width
#define BASE_IMAGE_WIDTH 320
#define BASE_IMAGE_HEIGHT 240

// Which errors will be checked and reported?
#define ANKI_DEBUG_MINIMAL 0 // Only check and output issues with explicit unit tests
#define ANKI_DEBUG_ERRORS 10 // Check and output AnkiErrors and explicit unit tests
#define ANKI_DEBUG_ERRORS_AND_WARNS 20 // Check and output AnkiErrors, AnkiWarns, and explicit unit tests
#define ANKI_DEBUG_ERRORS_AND_WARNS_AND_ASSERTS 30 // Check and output AnkiErrors, AnkiWarns, AnkiAsserts, and explicit unit tests
#define ANKI_DEBUG_ALL 40 // Check and output AnkiErrors, AnkiWarns, and explicit unit tests, plus run any additional extensive tests

// How will errors be reported?
#define ANKI_OUTPUT_DEBUG_NONE 0
#define ANKI_OUTPUT_DEBUG_PRINTF 10

#define ANKI_OUTPUT_DEBUG_LEVEL ANKI_OUTPUT_DEBUG_PRINTF

#endif // _ANKICORETECHEMBEDDED_COMMON_CONFIG_H_

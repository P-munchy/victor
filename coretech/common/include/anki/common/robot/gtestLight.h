/**
File: gtestLight.h
Author: Peter Barnum
Created: 2013

This file contains API-compatible versions of the Google Test framework, for use on an embedded system.

Copyright Anki, Inc. 2013
For internal use only. No part of this code may be used without a signed non-disclosure agreement with Anki, inc.
**/

#ifndef _ANKICORETECHEMBEDDED_COMMON_GTEST_LIGHT_H_
#define _ANKICORETECHEMBEDDED_COMMON_GTEST_LIGHT_H_

#include "anki/common/robot/config.h"

#if ANKICORETECH_EMBEDDED_USE_GTEST

// To prevent a warning (or error) about a function not returning a value, include this macro at the end of any GTEST_TEST. Also, this lets you set a breakpoint at the end.
#define GTEST_RETURN_HERE {printf("");}

#else

// To prevent a warning (or error) about a function not returning a value, include this macro at the end of any GTEST_TEST
#define GTEST_RETURN_HERE {printf(""); return 0; }

// Same usage as the Gtest macro
#define GTEST_TEST(test_case_name, test_name) s32 test_case_name ## test_name()

// Same usage as the Gtest macro
#define ASSERT_TRUE(condition)\
  if(!(condition)) { \
  _Anki_Logf(ANKI_LOG_LEVEL_ERROR,\
  "\n------------------------------------------------------------------------\nUnitTestAssert(" #condition ") is false\nUnit Test Assert Failure\n------------------------------------------------------------------------",\
  "", __FILE__, __PRETTY_FUNCTION__, __LINE__); \
  return -1;\
  }

// Same usage as the Gtest macro
#define ASSERT_FALSE(condition) ASSERT_TRUE(!(condition))

// Same usage as the Gtest macro
#define ASSERT_EQ(term1, term2)\
  ASSERT_TRUE((term1) == (term2))

// Call a GTEST_TEST, and increment the variable numPassedTests if the test passed, and  the variable numFailedTests if the test failed.
#define CALL_GTEST_TEST(test_case_name, test_name)\
  if(test_case_name ## test_name() == 0) {\
  printf("\n\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\nPASSED:" #test_case_name # test_name "\n" "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n\n");\
  numPassedTests++;\
  } else {\
  printf("\n\nxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\nFAILED:" #test_case_name # test_name "\n" "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n\n"); numFailedTests++; };

#endif // ANKICORETECH_EMBEDDED_USE_GTEST

#endif // #ifndef _ANKICORETECHEMBEDDED_COMMON_GTEST_LIGHT_H_

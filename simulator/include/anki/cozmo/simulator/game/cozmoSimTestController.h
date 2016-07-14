/*
 * File:          cozmoSimTestController.h
 * Date:
 * Description:   Any UI/Game to be run as a Webots controller should be derived from this class.
 * Author:
 * Modifications:
 */

#ifndef __COZMO_SIM_TEST_CONTROLLER__H__
#define __COZMO_SIM_TEST_CONTROLLER__H__


#include "anki/cozmo/simulator/game/uiGameController.h"

namespace Anki {
namespace Cozmo {

  
// Registration of test controller derived from CozmoSimTestController
#define REGISTER_COZMO_SIM_TEST_CLASS(CLASS) static CozmoSimTestRegistrar<CLASS> registrar(#CLASS);
  

////////// Macros for condition checking and exiting ////////
  
// For local testing, set to 1 so that Webots doesn't exit
#define DO_NOT_QUIT_WEBOTS 0
  
#if (DO_NOT_QUIT_WEBOTS == 1)
#define CST_EXIT()  QuitController(_result);
#else
#define CST_EXIT()  QuitWebots(_result);  
#endif
  
#define DEFAULT_TIMEOUT 10
  
#define CST_EXPECT(x, errorStreamOutput) \
if (!(x)) { \
  PRINT_STREAM_WARNING("CST_EXPECT", "(" << #x << "): " << errorStreamOutput << "(" << __FILE__ << "." << __FUNCTION__ << "." << __LINE__ << ")"); \
  _result = -1; \
}
  
#define CST_ASSERT(x, errorStreamOutput) \
if (!(x)) { \
  PRINT_STREAM_WARNING("CST_ASSERT", "(" << #x << "): " << errorStreamOutput << "(" << __FILE__ << "." << __FUNCTION__ << "." << __LINE__ << ")"); \
  _result = -1; \
  CST_EXIT(); \
}
  
// Returns evaluation of condition until timeout seconds past sinceTime
// at which point it asserts on the condition.
#define CONDITION_WITH_TIMEOUT_ASSERT(cond, start_time, timeout) (IsTrueBeforeTimeout(cond, #cond, start_time, timeout, __FILE__, __FUNCTION__, __LINE__))

// Start of if block which is entered if condition evaluates to true
// until timeout seconds past the first time this line is reached
// at which point it asserts on the condition.
#define IF_CONDITION_WITH_TIMEOUT_ASSERT(cond, timeout) static double startTime##__LINE__ = GetSupervisor()->getTime(); if (IsTrueBeforeTimeout(cond, #cond, startTime##__LINE__, timeout, __FILE__, __FUNCTION__, __LINE__))
  
  

  
/////////////// CozmoSimTestController /////////////////

// Base class from which all cozmo simulation tests should be derived
class CozmoSimTestController : public UiGameController {

public:
  CozmoSimTestController();
  virtual ~CozmoSimTestController();
  
protected:
  
  s32 UpdateInternal() final;
  virtual s32 UpdateSimInternal() = 0;
  
  u8 _result;
  bool _isRecording;
  
  //Variables for taking screenshots
  f32 _screenshotInterval;
  time_t _timeOfLastScreenshot;
  std::string _screenshotID;
  int _screenshotNum;
  
  
  bool IsTrueBeforeTimeout(bool cond,
                           std::string condAsString,
                           double start_time,
                           double timeout,
                           std::string file,
                           std::string func,
                           int line);
  
  //Only runs if #define RECORD_TEST 1, use for local testing
  void StartMovieConditional(const std::string& name, int speed = 1);

  //Use for movies on teamcity - be sure to add to build artifacts
  void StartMovieAlways(const std::string& name, int speed = 1);
  void StopMovie();
  
  //Use to take regular screenshots - on the build server this is preferable to recording movies
  void TakeScreenshotsAtInterval(const std::string& screenshotID, f32 interval);

  void MakeSynchronous();

  void DisableRandomPathSpeeds();

  // call in the update loop to occasionally print info about blocks
  void PrintPeriodicBlockDebug();
  void SetBlockDebugPrintInterval(double interval_s) { _printInterval_s = interval_s; }

  double _nextPrintTime = -1.0f;
  double _printInterval_s = 1.0;
  
}; // class CozmoSimTestController

  
  
/////////////// CozmoSimTestFactory /////////////////
  
// Factory for creating and registering tests derived from CozmoSimTestController
class CozmoSimTestFactory
{
public:
  
  static CozmoSimTestFactory * getInstance()
  {
    static CozmoSimTestFactory factory;
    return &factory;
  }
  
  std::shared_ptr<CozmoSimTestController> Create(std::string name);
  
  void RegisterFactoryFunction(std::string name,
                               std::function<CozmoSimTestController*(void)> classFactoryFunction);
  
protected:
  std::map<std::string, std::function<CozmoSimTestController*(void)>> factoryFunctionRegistry;
};


template<class T>
class CozmoSimTestRegistrar {
public:
  CozmoSimTestRegistrar(std::string className)
  {
    // register the class factory function
    CozmoSimTestFactory::getInstance()->RegisterFactoryFunction(className,
                                                                [](void) -> CozmoSimTestController * { return new T();});
  }

};
  
  
} // namespace Cozmo
} // namespace Anki

#endif // __COZMO_SIM_TEST_CONTROLLER__H__



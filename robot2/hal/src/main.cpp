#include <stdio.h>
#include <chrono>
#include <thread>

#include "anki/cozmo/robot/hal.h"
#include "anki/cozmo/robot/logging.h"
#include "anki/cozmo/robot/cozmoBot.h"

// For development purposes, while HW is scarce, it's useful to be able to run on phones
#ifdef USING_ANDROID_PHONE
#define HAL_NOT_PROVIDING_CLOCK 1
#endif


int main(int argc, const char* argv[])
{
  AnkiEvent("robot.main", "Starting robot process");

  //Robot::Init calls HAL::INIT before anything else.
  // TODO: move HAL::Init here into HAL main.
  Anki::Cozmo::Robot::Init();

  auto start = std::chrono::steady_clock::now();

  for (;;) {
    //HAL::Step should never return !OK, but if it does, best not to trust its data.
    if (Anki::Cozmo::HAL::Step() == Anki::RESULT_OK) {
      if (Anki::Cozmo::Robot::step_MainExecution() != Anki::RESULT_OK) {
        AnkiError("robot.main", "MainExecution failed");
        return -1;
      }
    }

    auto end = std::chrono::steady_clock::now();
#ifdef HAL_NOT_PROVIDING_CLOCK
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::chrono::duration<double, std::micro> sleepTime = std::chrono::milliseconds(5) - elapsed;
    std::this_thread::sleep_for(sleepTime);
    ///printf("Main tic: %lld, Sleep time: %f us\n", elapsed.count(), sleepTime.count());
#endif
    //printf("TS: %d\n", Anki::Cozmo::HAL::GetTimeStamp() );
    start = end;
  }
  return 0;
}

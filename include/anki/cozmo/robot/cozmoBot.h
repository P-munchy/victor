#ifndef ANKI_COZMOBOT_H
#define ANKI_COZMOBOT_H

#include "anki/common/types.h"

namespace Anki {
  
  namespace Cozmo {
    
    namespace Robot {
      
      ReturnCode Init();
      void Destroy();
      
      //
      // Stepping Functions
      //
      // We will have (at least?) two threads to step along:
      //
      // 1. The low-level functions which happen at a strict, determinstic rate
      //    (like motor control and sensor updates)
      //
      // 2. The slower functions which take longer and will be run "as quickly
      //    as possible" (like the vision system for finding block markers)
      //
      ReturnCode step_MainExecution();
      ReturnCode step_LongExecution();
      
      //
      // State Machine Operation Modes
      //
      enum OperationMode {
        INIT_MOTOR_CALIBRATION,
        WAITING,
        PICK_UP_BLOCK,
        PUT_DOWN_BLOCK,
        FOLLOW_PATH
      };
      
      OperationMode GetOperationMode();
      void SetOperationMode(OperationMode newMode);
      
    } // namespace Robot
    
  } // namespace Cozmo
  
} // namespace Anki

#endif // ANKI_COZMOBOT_H

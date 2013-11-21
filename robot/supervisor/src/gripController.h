/**
 * File: gripController.h
 *
 * Author: Kevin Yoon (kevin)
 * Created: 11/18/2013
 *
 * Description:
 *
 *   Controller for the gripper
 *
 * Copyright: Anki, Inc. 2013
 *
 **/

#ifndef COZMO_GRIP_CONTROLLER_H_
#define COZMO_GRIP_CONTROLLER_H_

#include "anki/common/types.h"

namespace Anki {
  namespace Cozmo {
    namespace GripController {

      ReturnCode Update();
      
      void EngageGripper();
      void DisengageGripper();
      bool IsGripperEngaged();
      
    } // namespace GripController
  } // namespace Cozmo
} // namespace Anki

#endif // COZMO_GRIP_CONTROLLER_H_
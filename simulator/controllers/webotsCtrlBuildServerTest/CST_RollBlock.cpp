/**
 * File: CST_RollBlock.cpp
 *
 * Author: Al Chaussee
 * Created: 2/12/16
 *
 * Description: See TestStates below
 *
 * Copyright: Anki, inc. 2016
 *
 */

#include "anki/cozmo/simulator/game/cozmoSimTestController.h"
#include "anki/common/basestation/math/point_impl.h"
#include "anki/cozmo/basestation/actions/basicActions.h"
#include "anki/cozmo/basestation/robot.h"


namespace Anki {
  namespace Cozmo {
    
    enum class TestState {
      Init,
      RollObject,
      TestDone
    };
    
    // Motion profile for test
    const f32 defaultPathSpeed_mmps = 60;
    const f32 defaultPathAccel_mmps2 = 200;
    const f32 defaultPathDecel_mmps2 = 500;
    const f32 defaultPathPointTurnSpeed_rad_per_sec = 1.5;
    const f32 defaultPathPointTurnAccel_rad_per_sec2 = 100;
    const f32 defaultPathPointTurnDecel_rad_per_sec2 = 500;
    const f32 defaultDockSpeed_mmps = 100;
    const f32 defaultDockAccel_mmps2 = 200;
    const f32 defaultReverseSpeed_mmps = 30;
    PathMotionProfile motionProfile4(defaultPathSpeed_mmps,
                                    defaultPathAccel_mmps2,
                                    defaultPathDecel_mmps2,
                                    defaultPathPointTurnSpeed_rad_per_sec,
                                    defaultPathPointTurnAccel_rad_per_sec2,
                                    defaultPathPointTurnDecel_rad_per_sec2,
                                    defaultDockSpeed_mmps,
                                    defaultDockAccel_mmps2,
                                    defaultReverseSpeed_mmps);
    
    
    // ============ Test class declaration ============
    class CST_RollBlock : public CozmoSimTestController {
      
    private:
      
      virtual s32 UpdateInternal() override;
      
      TestState _testState = TestState::Init;
    };
    
    // Register class with factory
    REGISTER_COZMO_SIM_TEST_CLASS(CST_RollBlock);
    
    
    // =========== Test class implementation ===========
    
    s32 CST_RollBlock::UpdateInternal()
    {
      switch (_testState) {
        case TestState::Init:
        {
          MakeSynchronous();
          StartMovie("RollBlock");
          
          SendMoveHeadToAngle(0, 100, 100);
          _testState = TestState::RollObject;
          break;
        }
        case TestState::RollObject:
        {
          IF_CONDITION_WITH_TIMEOUT_ASSERT(!IsRobotStatus(RobotStatusFlag::IS_MOVING) &&
                                           NEAR(GetRobotHeadAngle_rad(), 0, HEAD_ANGLE_TOL) &&
                                           GetNumObjects() == 1, 20)
          {
            ExternalInterface::QueueSingleAction m;
            m.robotID = 1;
            m.position = QueueActionPosition::NOW;
            m.idTag = 11;
            m.numRetries = 3;
            // Roll object 0
            m.action.Set_rollObject(ExternalInterface::RollObject(0, motionProfile4, 0, false, true, false));
            ExternalInterface::MessageGameToEngine message;
            message.Set_QueueSingleAction(m);
            SendMessage(message);
            _testState = TestState::TestDone;
          }
          break;
        }
        case TestState::TestDone:
        {
          // Verify robot has rolled the block
          Pose3d pose;
          GetObjectPose(0, pose);
          IF_CONDITION_WITH_TIMEOUT_ASSERT(!IsRobotStatus(RobotStatusFlag::IS_MOVING) &&
                                           GetCarryingObjectID() == -1 &&
                                           NEAR(pose.GetRotationAxis().x(), 0.0, 0.1) &&
                                           NEAR(pose.GetRotationAxis().z(), 0.0, 0.1) &&
                                           NEAR(pose.GetRotationAxis().y(), 1.0, 0.1), 20)
          {
            StopMovie();
            CST_EXIT();
          }
          break;
        }
      }
      return _result;
    }
  } // end namespace Cozmo
} // end namespace Anki


/**
 * File: CST_StackBlocks.cpp
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
      PickupObject,
      Stack,
      TestDone
    };
    
    // Motion profile for test
    const f32 defaultPathSpeed_mmps = 60;
    const f32 defaultPathAccel_mmps2 = 200;
    const f32 defaultPathDecel_mmps2 = 500;
    const f32 defaultPathPointTurnSpeed_rad_per_sec = 1.5;
    const f32 defaultPathPointTurnAccel_rad_per_sec2 = 100;
    const f32 defaultPathPointTurnDecel_rad_per_sec2 = 500;
    const f32 defaultDockSpeed_mmps = 60;
    const f32 defaultDockAccel_mmps2 = 200;
    const f32 defaultDockDecel_mmps2 = 100;
    const f32 defaultReverseSpeed_mmps = 30;
    PathMotionProfile motionProfile3(defaultPathSpeed_mmps,
                                    defaultPathAccel_mmps2,
                                    defaultPathDecel_mmps2,
                                    defaultPathPointTurnSpeed_rad_per_sec,
                                    defaultPathPointTurnAccel_rad_per_sec2,
                                    defaultPathPointTurnDecel_rad_per_sec2,
                                    defaultDockSpeed_mmps,
                                    defaultDockAccel_mmps2,
                                    defaultDockDecel_mmps2,
                                    defaultReverseSpeed_mmps);
    
    const f32 ROBOT_POSITION_TOL_MM = 10;
    const f32 ROBOT_ANGLE_TOL_DEG = 5;
    const f32 BLOCK_HEIGHT_TOL_MM = 10;
    
    // ============ Test class declaration ============
    class CST_StackBlocks : public CozmoSimTestController {
      
    private:
      
      virtual s32 UpdateInternal() override;
      
      TestState _testState = TestState::Init;
      
      bool _lastActionSucceeded = false;
      
      // Message handlers
      virtual void HandleRobotCompletedAction(const ExternalInterface::RobotCompletedAction& msg) override;
    };
    
    // Register class with factory
    REGISTER_COZMO_SIM_TEST_CLASS(CST_StackBlocks);
    
    
    // =========== Test class implementation ===========
    
    s32 CST_StackBlocks::UpdateInternal()
    {
      switch (_testState) {
        case TestState::Init:
        {
          MakeSynchronous();
          StartMovie("StackBlocks");
          
          SendMoveHeadToAngle(0, 100, 100);
          _testState = TestState::PickupObject;
          break;
        }
        case TestState::PickupObject:
        {
          IF_CONDITION_WITH_TIMEOUT_ASSERT(!IsRobotStatus(RobotStatusFlag::IS_MOVING) &&
                                           NEAR(GetRobotHeadAngle_rad(), 0, HEAD_ANGLE_TOL) &&
                                           GetNumObjects() == 1, DEFAULT_TIMEOUT)
          {
            ExternalInterface::QueueSingleAction m;
            m.robotID = 1;
            m.position = QueueActionPosition::NOW;
            m.idTag = 1;
            // Pickup object 0
            m.action.Set_pickupObject(ExternalInterface::PickupObject(0, motionProfile3, 0, false, true, false));
            ExternalInterface::MessageGameToEngine message;
            message.Set_QueueSingleAction(m);
            SendMessage(message);
            _testState = TestState::Stack;
          }
          break;
        }
        case TestState::Stack:
        {
          IF_CONDITION_WITH_TIMEOUT_ASSERT(!IsRobotStatus(RobotStatusFlag::IS_MOVING) &&
                                           NEAR(GetRobotPose().GetRotation().GetAngleAroundZaxis().getDegrees(), 0, ROBOT_ANGLE_TOL_DEG) &&
                                           NEAR(GetRobotPose().GetTranslation().x(), 60, ROBOT_POSITION_TOL_MM) &&
                                           NEAR(GetRobotPose().GetTranslation().y(), 0, ROBOT_POSITION_TOL_MM) &&
                                           GetCarryingObjectID() == 0, 20)
          {
            ExternalInterface::QueueCompoundAction m;
            m.robotID = 1;
            m.position = QueueActionPosition::NOW;
            m.idTag = 2;
            m.parallel = false;
            m.numRetries = 3;
            // Wait a few seconds to see the block behind the one we just picked up
            m.actions.push_back((ExternalInterface::RobotActionUnion)ExternalInterface::Wait(1));
            // Place object 0 on object 1
            m.actions.push_back((ExternalInterface::RobotActionUnion)ExternalInterface::PlaceOnObject(1, motionProfile3, 0, false, true, false));
            ExternalInterface::MessageGameToEngine message;
            
            message.Set_QueueCompoundAction(m);
            SendMessage(message);
            
            _testState = TestState::TestDone;
          }
          break;
        }
        case TestState::TestDone:
        {
          // Verify robot has stacked the blocks
          Pose3d pose0;
          GetObjectPose(0, pose0);
          Pose3d pose1;
          GetObjectPose(1, pose1);
          
          PRINT_NAMED_INFO("BAASDF", "BlockZ: %f %f, Robot (xy) %f %f",
                           pose0.GetTranslation().z(),
                           pose1.GetTranslation().z(),
                           GetRobotPose().GetTranslation().x(),
                           GetRobotPose().GetTranslation().y());
          
          IF_CONDITION_WITH_TIMEOUT_ASSERT(!IsRobotStatus(RobotStatusFlag::IS_MOVING) &&
                                           GetCarryingObjectID() == -1 &&
                                           NEAR(pose0.GetTranslation().z(), 65, BLOCK_HEIGHT_TOL_MM) &&
                                           NEAR(pose1.GetTranslation().z(), 22, BLOCK_HEIGHT_TOL_MM) &&
                                           NEAR(GetRobotPose().GetTranslation().x(), 130, ROBOT_POSITION_TOL_MM) &&
                                           NEAR(GetRobotPose().GetTranslation().y(), 0, ROBOT_POSITION_TOL_MM), 20)
          {
            StopMovie();
            CST_EXIT();
          }
          break;
        }
      }
      return _result;
    }
    
    
    // ================ Message handler callbacks ==================
    void CST_StackBlocks::HandleRobotCompletedAction(const ExternalInterface::RobotCompletedAction& msg)
    {
      if (msg.result == ActionResult::SUCCESS) {
        _lastActionSucceeded = true;
      } else {
        StopMovie();
      }
    }
    
    // ================ End of message handler callbacks ==================
    
  } // end namespace Cozmo
} // end namespace Anki


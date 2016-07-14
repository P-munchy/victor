/**
 * File: CST_DockingSpeeds.cpp
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
      DockingSpeed1,
      DockingSpeed2,
      DockingSpeed3,
      DockingSpeed4,
      DockingSpeed5,
      PlaceBlock,
      VerifyPlaced,
      ResetTest,
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
    PathMotionProfile mp(defaultPathSpeed_mmps,
                         defaultPathAccel_mmps2,
                         defaultPathDecel_mmps2,
                         defaultPathPointTurnSpeed_rad_per_sec,
                         defaultPathPointTurnAccel_rad_per_sec2,
                         defaultPathPointTurnDecel_rad_per_sec2,
                         defaultDockSpeed_mmps,
                         defaultDockAccel_mmps2,
                         defaultDockDecel_mmps2,
                         defaultReverseSpeed_mmps,
                         true);
    
    
    // ============ Test class declaration ============
    class CST_DockingSpeeds : public CozmoSimTestController
    {
      
    public:
      CST_DockingSpeeds();
      
    private:
      
      virtual s32 UpdateSimInternal() override;
      void ResetTest();
      
      TestState _testState = TestState::Init;
      
      const Pose3d _startingRobotPose;
      const Pose3d _startingCubePose;
      
      TestState _nextState = TestState::Init;
      
      ExternalInterface::RobotState _robotState;
      
      bool _placeActionCompleted = false;
      u32 _placeActionTag = 1000;
      
      virtual void HandleRobotStateUpdate(ExternalInterface::RobotState const& msg) override;
      virtual void HandleRobotCompletedAction(const ExternalInterface::RobotCompletedAction& msg) override;
    };
    
    // Register class with factory
    REGISTER_COZMO_SIM_TEST_CLASS(CST_DockingSpeeds);
    
    
    // =========== Test class implementation ===========
    
    CST_DockingSpeeds::CST_DockingSpeeds()
    : _startingRobotPose(0, Z_AXIS_3D(), {0, 0, 0})
    , _startingCubePose(0, Z_AXIS_3D(), {150.f, 0.f, 22.f})
    {
      
    }
    
    s32 CST_DockingSpeeds::UpdateSimInternal()
    {
      switch (_testState) {
        case TestState::Init:
        {
          MakeSynchronous();
          StartMovieConditional("DockingSpeed");
          //TakeScreenshotsAtInterval("DockingSpeed", 1.f);
          
          SendMoveHeadToAngle(0, 100, 100);
          _testState = TestState::DockingSpeed1;
          break;
        }
        case TestState::DockingSpeed1:
        {
          IF_CONDITION_WITH_TIMEOUT_ASSERT(!IsRobotStatus(RobotStatusFlag::IS_MOVING) &&
                                           NEAR(GetRobotHeadAngle_rad(), 0, HEAD_ANGLE_TOL) &&
                                           GetNumObjects() == 1, 20)
          {
            PRINT_NAMED_INFO("DockingSpeed1", "Docking with speed:%f accel:%f decel:%f", mp.dockSpeed_mmps, mp.accel_mmps2, mp.dockDecel_mmps2);
          
            ExternalInterface::QueueSingleAction m;
            m.robotID = 1;
            m.position = QueueActionPosition::NOW;
            m.idTag = 1;
            m.numRetries = 3;
            // Pickup object 0
            m.action.Set_pickupObject(ExternalInterface::PickupObject(0, mp, 0, false, true, false));
            ExternalInterface::MessageGameToEngine message;
            message.Set_QueueSingleAction(m);
            SendMessage(message);
            _nextState = TestState::DockingSpeed2;
            _testState = TestState::PlaceBlock;
          }
          break;
        }
        case TestState::DockingSpeed2:
        {
          IF_CONDITION_WITH_TIMEOUT_ASSERT(!IsRobotStatus(RobotStatusFlag::IS_MOVING) &&
                                           GetCarryingObjectID() == -1 &&
                                           GetNumObjects() == 1, DEFAULT_TIMEOUT)
          {
            ResetTest();
            
            mp.dockSpeed_mmps = 80;
            mp.dockDecel_mmps2 = 200;
            
            PRINT_NAMED_INFO("DockingSpeed2", "Docking with speed:%f accel:%f decel:%f", mp.dockSpeed_mmps, mp.accel_mmps2, mp.dockDecel_mmps2);
            
            ExternalInterface::QueueSingleAction m;
            m.robotID = 1;
            m.position = QueueActionPosition::NOW;
            m.idTag = 2;
            m.numRetries = 3;
            // Pickup object 0
            m.action.Set_pickupObject(ExternalInterface::PickupObject(0, mp, 0, false, true, false));
            ExternalInterface::MessageGameToEngine message;
            message.Set_QueueSingleAction(m);
            SendMessage(message);
            _nextState = TestState::DockingSpeed3;
            _testState = TestState::PlaceBlock;
          }
          break;
        }
        case TestState::DockingSpeed3:
        {
          IF_CONDITION_WITH_TIMEOUT_ASSERT(!IsRobotStatus(RobotStatusFlag::IS_MOVING) &&
                                           GetCarryingObjectID() == -1, DEFAULT_TIMEOUT)
          {
            ResetTest();
            
            mp.dockSpeed_mmps = 40;
            
            PRINT_NAMED_INFO("DockingSpeed3", "Docking with speed:%f accel:%f decel:%f", mp.dockSpeed_mmps, mp.accel_mmps2, mp.dockDecel_mmps2);
            
            ExternalInterface::QueueSingleAction m;
            m.robotID = 1;
            m.position = QueueActionPosition::NOW;
            m.idTag = 3;
            m.numRetries = 3;
            // Pickup object 0
            m.action.Set_pickupObject(ExternalInterface::PickupObject(0, mp, 0, false, true, false));
            ExternalInterface::MessageGameToEngine message;
            message.Set_QueueSingleAction(m);
            SendMessage(message);
            _nextState = TestState::DockingSpeed4;
            _testState = TestState::PlaceBlock;
          }
          break;
        }
        case TestState::DockingSpeed4:
        {
          IF_CONDITION_WITH_TIMEOUT_ASSERT(!IsRobotStatus(RobotStatusFlag::IS_MOVING) &&
                                           GetCarryingObjectID() == -1, DEFAULT_TIMEOUT)
          {
            ResetTest();
            
            mp.dockSpeed_mmps = 100;
            mp.dockDecel_mmps2 = 200;
            
            PRINT_NAMED_INFO("DockingSpeed4", "Docking with speed:%f accel:%f decel:%f", mp.dockSpeed_mmps, mp.accel_mmps2, mp.dockDecel_mmps2);
            
            ExternalInterface::QueueSingleAction m;
            m.robotID = 1;
            m.position = QueueActionPosition::NOW;
            m.idTag = 4;
            m.numRetries = 3;
            // Pickup object 0
            m.action.Set_pickupObject(ExternalInterface::PickupObject(0, mp, 0, false, true, false));
            ExternalInterface::MessageGameToEngine message;
            message.Set_QueueSingleAction(m);
            SendMessage(message);
            _nextState = TestState::DockingSpeed5;
            _testState = TestState::PlaceBlock;
          }
          break;
        }
        case TestState::DockingSpeed5:
        {
          IF_CONDITION_WITH_TIMEOUT_ASSERT(!IsRobotStatus(RobotStatusFlag::IS_MOVING) &&
                                           GetCarryingObjectID() == -1, DEFAULT_TIMEOUT)
          {
            ResetTest();
            
            mp.dockSpeed_mmps = 200;
            mp.dockDecel_mmps2 = 1000;
            
            PRINT_NAMED_INFO("DockingSpeed5", "Docking with speed:%f accel:%f decel:%f", mp.dockSpeed_mmps, mp.accel_mmps2, mp.dockDecel_mmps2);
            
            ExternalInterface::QueueSingleAction m;
            m.robotID = 1;
            m.position = QueueActionPosition::NOW;
            m.idTag = 5;
            m.numRetries = 3;
            // Pickup object 0
            m.action.Set_pickupObject(ExternalInterface::PickupObject(0, mp, 0, false, true, false));
            ExternalInterface::MessageGameToEngine message;
            message.Set_QueueSingleAction(m);
            SendMessage(message);
            _testState = TestState::TestDone;
          }
          break;
        }
        case TestState::TestDone:
        {
          IF_CONDITION_WITH_TIMEOUT_ASSERT(!IsRobotStatus(RobotStatusFlag::IS_MOVING) &&
                                           GetCarryingObjectID() == 0, 20)
          {
            StopMovie();
            CST_EXIT();
          }
          break;
        }
        case TestState::PlaceBlock:
        {
          IF_CONDITION_WITH_TIMEOUT_ASSERT(!IsRobotStatus(RobotStatusFlag::IS_MOVING) &&
                                           GetCarryingObjectID() == 0, 55)
          {
            ExternalInterface::QueueSingleAction m;
            m.robotID = 1;
            m.position = QueueActionPosition::NOW;
            m.idTag = _placeActionTag;
            m.numRetries = 3;
            // Pickup object 0
            m.action.Set_placeObjectOnGroundHere(ExternalInterface::PlaceObjectOnGroundHere());
            ExternalInterface::MessageGameToEngine message;
            message.Set_QueueSingleAction(m);
            SendMessage(message);
            _testState = TestState::VerifyPlaced;
            
            _placeActionCompleted = false;
          }
          break;
        }
        case TestState::VerifyPlaced:
        {
          Pose3d pose0 = GetLightCubePoseActual(0);
          IF_CONDITION_WITH_TIMEOUT_ASSERT(!IsRobotStatus(RobotStatusFlag::IS_MOVING) &&
                                           GetCarryingObjectID() == -1 &&
                                           _placeActionCompleted &&
                                           NEAR(pose0.GetTranslation().z(), 22, 1), 55)
          {
            ResetTest();
            
            // This wait is for things to settle down after resetting the world
            ExternalInterface::QueueSingleAction m;
            m.robotID = 1;
            m.position = QueueActionPosition::NOW_AND_CLEAR_REMAINING;
            m.idTag = 10;
            m.action.Set_waitForImages(ExternalInterface::WaitForImages(5,0,VisionMode::DetectingMarkers));
            ExternalInterface::MessageGameToEngine message;
            message.Set_QueueSingleAction(m);
            SendMessage(message);
            
            ExternalInterface::QueueSingleAction m1;
            m1.robotID = 1;
            m1.position = QueueActionPosition::NEXT;
            m1.idTag = 20;
            m1.action.Set_setHeadAngle(ExternalInterface::SetHeadAngle(0,100,100,0));
            ExternalInterface::MessageGameToEngine message1;
            message1.Set_QueueSingleAction(m1);
            SendMessage(message1);
            
            // This wait is to ensure block pose is stable before trying to pickup
            ExternalInterface::QueueSingleAction m2;
            m2.robotID = 1;
            m2.position = QueueActionPosition::NEXT;
            m2.idTag = 30;
            m2.action.Set_waitForImages(ExternalInterface::WaitForImages(5,0,VisionMode::DetectingMarkers));
            ExternalInterface::MessageGameToEngine message2;
            message2.Set_QueueSingleAction(m2);
            SendMessage(message2);
            
            _testState = TestState::ResetTest;
          }
          break;
        }
        case TestState::ResetTest:
        {
          IF_CONDITION_WITH_TIMEOUT_ASSERT(!IsRobotStatus(RobotStatusFlag::IS_MOVING) &&
                                           NEAR(GetRobotHeadAngle_rad(), 0, HEAD_ANGLE_TOL) &&
                                           GetNumObjects() == 1, 55)
          {
            _testState = _nextState;
          }
          break;
        }
      }
      return _result;
    }
    
    void CST_DockingSpeeds::ResetTest()
    {
      SetLightCubePose(0, _startingCubePose);
      SetActualRobotPose(_startingRobotPose);
    }
    
    void CST_DockingSpeeds::HandleRobotStateUpdate(const ExternalInterface::RobotState &msg)
    {
      _robotState = msg;
    }
    
    void CST_DockingSpeeds::HandleRobotCompletedAction(const ExternalInterface::RobotCompletedAction& msg)
    {
      if(msg.idTag == _placeActionTag)
      {
        _placeActionCompleted = true;
      };
    }
    
  } // end namespace Cozmo
} // end namespace Anki


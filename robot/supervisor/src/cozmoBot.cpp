#include "anki/cozmo/robot/cozmoBot.h"
#include "anki/cozmo/shared/cozmoConfig.h"
#include "anki/cozmo/robot/hal.h" // simulated or real!
#include "clad/types/imageTypes.h"
#include "timeProfiler.h"
#include "messages.h"
#include "clad/robotInterface/messageRobotToEngine_send_helper.h"
#include "liftController.h"
#include "headController.h"
#include "imuFilter.h"
#include "proxSensors.h"
#include "version.h"
#include "speedController.h"
#include "steeringController.h"
#include "wheelController.h"
#include "localization.h"
#include "pickAndPlaceController.h"
#include "dockingController.h"
#include "pathFollower.h"
#include "testModeController.h"
#include "anki/cozmo/robot/logging.h"
#include "backpackLightController.h"
#ifndef TARGET_K02
#include "animationController.h"
#include "anki/common/shared/utilities_shared.h"
#include "blockLightController.h"
#endif

#ifdef SIMULATOR
#include "anki/vision/CameraSettings.h"
#include "../sim_hal/sim_nvStorage.h"
#include <math.h>
#endif

#define ACTIVE_OBJECT_DISCONNECT_ON_ENGINE_DISCONNECT
#ifdef ACTIVE_OBJECT_DISCONNECT_ON_ENGINE_DISCONNECT
#include "clad/types/activeObjectTypes.h"
#include "clad/robotInterface/messageEngineToRobot.h"
#include "clad/robotInterface/messageEngineToRobot_send_helper.h"
#endif

///////// TESTING //////////

#if ANKICORETECH_EMBEDDED_USE_MATLAB && USING_MATLAB_VISION
#include "anki/embeddedCommon/matlabConverters.h"
#endif

///////// END TESTING //////

namespace Anki {
  namespace Cozmo {

#ifdef SIMULATOR
    namespace HAL {
      ImageSendMode imageSendMode_;
      ImageResolution captureResolution_ = QVGA;
      void SetImageSendMode(const ImageSendMode mode, const ImageResolution res)
      {
        imageSendMode_ = mode;
        captureResolution_ = res;
      }
    }
#endif

    namespace Robot {

      // "Private Member Variables"
      namespace {

        // Parameters / Constants:

        // TESTING
        // Change this value to run different test modes
        const TestMode DEFAULT_TEST_MODE = TM_NONE;

        Robot::OperationMode mode_ = INIT_MOTOR_CALIBRATION;
        bool wasConnected_ = false;

        // For only sending robot state messages every STATE_MESSAGE_FREQUENCY
        // times through the main loop
        u32 robotStateMessageCounter_ = 0;

        // Main cycle time errors
        u32 mainTooLongCnt_ = 0;
        u32 mainTooLateCnt_ = 0;
        u32 avgMainTooLongTime_ = 0;
        u32 avgMainTooLateTime_ = 0;
        u32 lastCycleStartTime_ = 0;
        u32 lastMainCycleTimeErrorReportTime_ = 0;
        const u32 MAIN_TOO_LATE_TIME_THRESH_USEC = TIME_STEP * 1500;  // Normal cycle time plus 50% margin
        const u32 MAIN_TOO_LONG_TIME_THRESH_USEC = 700;
        const u32 MAIN_CYCLE_ERROR_REPORTING_PERIOD_USEC = 1000000;

      } // Robot private namespace

      //
      // Accessors:
      //
      OperationMode GetOperationMode()
      { return mode_; }

      void SetOperationMode(OperationMode newMode)
      { mode_ = newMode; }

      //
      // Methods:
      //

      void StartMotorCalibrationRoutine()
      {
        LiftController::StartCalibrationRoutine();
        HeadController::StartCalibrationRoutine();
        SteeringController::ExecuteDirectDrive(0,0);
      }


      // The initial "stretch" and reset motor positions routine
      // Returns true when done.
      bool MotorCalibrationUpdate()
      {
        bool isDone = false;

        if (LiftController::IsCalibrated() && HeadController::IsCalibrated())
				{
          AnkiEvent( 38, "CozmoBot", 239, "Motors calibrated", 0);
          IMUFilter::Reset();
          isDone = true;
        }
        return isDone;
      }


      Result Init(void)
      {
        Result lastResult = RESULT_OK;

        // Coretech setup
#ifndef TARGET_K02
#ifndef SIMULATOR
#if(DIVERT_PRINT_TO_RADIO)
        SetCoreTechPrintFunctionPtr(Messages::SendText);
#else
        SetCoreTechPrintFunctionPtr(0);
#endif
#elif(USING_UART_RADIO && DIVERT_PRINT_TO_RADIO)
        SetCoreTechPrintFunctionPtr(Messages::SendText);
#endif
#endif
        // HAL and supervisor init
#ifndef ROBOT_HARDWARE    // The HAL/Operating System cannot be Init()ed or Destroy()ed on a real robot
        lastResult = HAL::Init();
        AnkiConditionalErrorAndReturnValue(lastResult == RESULT_OK, lastResult, 39, "Robot::Init()", 240, "HAL init failed.\n", 0);
#endif
#ifndef TARGET_K02
        lastResult = Messages::Init();
        AnkiConditionalErrorAndReturnValue(lastResult == RESULT_OK, lastResult, 39, "Robot::Init()", 241, "Messages / Reliable Transport init failed.\n", 0);
#endif

        lastResult = Localization::Init();
        AnkiConditionalErrorAndReturnValue(lastResult == RESULT_OK, lastResult, 39, "Robot::Init()", 242, "Localization System init failed.\n", 0);

        /*
        lastResult = VisionSystem::Init();
        AnkiConditionalErrorAndReturnValue(lastResult == RESULT_OK, lastResult, 39, "Robot::Init()", 243, "Vision System init failed.\n", 0);
         */

        lastResult = PathFollower::Init();
        AnkiConditionalErrorAndReturnValue(lastResult == RESULT_OK, lastResult, 39, "Robot::Init()", 244, "PathFollower System init failed.\n", 0);
        lastResult = BackpackLightController::Init();
        AnkiConditionalErrorAndReturnValue(lastResult == RESULT_OK, lastResult, 39, "Robot::Init()", 245, "BackpackLightController init failed.\n", 0);
        // Initialize subsystems if/when available:
        /*
         if(WheelController::Init() == RESULT_FAIL) {
         PRINT("WheelController initialization failed.\n");
         return RESULT_FAIL;
         }

         if(SpeedController::Init() == RESULT_FAIL) {
         PRINT("SpeedController initialization failed.\n");
         return RESULT_FAIL;
         }

         if(SteeringController::Init() == RESULT_FAIL) {
         PRINT("SteeringController initialization failed.\n");
         return RESULT_FAIL;
         }

         if(HeadController::Init() == RESULT_FAIL) {
         PRINT("HeadController initialization failed.\n");
         return RESULT_FAIL;
         }
         */
        lastResult = DockingController::Init();;
        AnkiConditionalErrorAndReturnValue(lastResult == RESULT_OK, lastResult, 39, "Robot::Init()", 246, "DockingController init failed.\n", 0);

        // Before liftController?!
        lastResult = PickAndPlaceController::Init();
        AnkiConditionalErrorAndReturnValue(lastResult == RESULT_OK, lastResult, 39, "Robot::Init()", 247, "PickAndPlaceController init failed.\n", 0);

        lastResult = LiftController::Init();
        AnkiConditionalErrorAndReturnValue(lastResult == RESULT_OK, lastResult, 39, "Robot::Init()", 248, "LiftController init failed.\n", 0);
#ifndef TARGET_K02
        lastResult = AnimationController::Init();
        AnkiConditionalErrorAndReturnValue(lastResult == RESULT_OK, lastResult, 39, "Robot::Init()", 249, "AnimationController init failed.\n", 0);
#endif
        // Start calibration
        StartMotorCalibrationRoutine();

        // Set starting state
        mode_ = INIT_MOTOR_CALIBRATION;

        robotStateMessageCounter_ = 0;

				SendVersionInfo();

        return RESULT_OK;

      } // Robot::Init()


#ifndef ROBOT_HARDWARE    // The HAL/Operating System cannot be Init()ed or Destroy()ed on a real robot
      void Destroy()
      {
        HAL::Destroy();
      }
#endif


      Result step_MainExecution()
      {
        START_TIME_PROFILE(CozmoBotMain, TOTAL);
        START_TIME_PROFILE(CozmoBot, HAL);

        // HACK: Manually setting timestamp here in mainExecution until
        // until Nathan implements this the correct way.
        HAL::SetTimeStamp(HAL::GetTimeStamp()+TIME_STEP);

        // Detect if it took too long in between mainExecution calls
        u32 cycleStartTime = HAL::GetMicroCounter();
        if (lastCycleStartTime_ != 0) {
          u32 timeBetweenCycles = cycleStartTime - lastCycleStartTime_;
          if (timeBetweenCycles > MAIN_TOO_LATE_TIME_THRESH_USEC) {
            ++mainTooLateCnt_;
            avgMainTooLateTime_ = (u32)((f32)(avgMainTooLateTime_ * (mainTooLateCnt_ - 1) + timeBetweenCycles)) / mainTooLateCnt_;
          }
        }


/*
        // Test code for measuring number of mainExecution tics per second
        static u32 cnt = 0;
        static u32 startTime = 0;
        const u32 interval_seconds = 5;

        if (++cnt == (200 * interval_seconds)) {
          u32 numTicsPerSec = (cnt * 1000000) / (cycleStartTime - startTime);
          AnkiInfo( 94, "CozmoBot.TicsPerSec", 347, "%d", 1, numTicsPerSec);
          startTime = cycleStartTime;
          cnt = 0;
        }
*/

        //////////////////////////////////////////////////////////////
        // Simulated NVStorage
        //////////////////////////////////////////////////////////////
#if SIMULATOR
        SimNVStorageSpace::Update();
#endif

        //////////////////////////////////////////////////////////////
        // Test Mode
        //////////////////////////////////////////////////////////////
        MARK_NEXT_TIME_PROFILE(CozmoBot, TEST);
        TestModeController::Update();


        //////////////////////////////////////////////////////////////
        // Localization
        //////////////////////////////////////////////////////////////
        MARK_NEXT_TIME_PROFILE(CozmoBot, LOC);
        Localization::Update();

        //////////////////////////////////////////////////////////////
        // Communications
        //////////////////////////////////////////////////////////////

        // Check if there is a new or dropped connection to a basestation
        if (HAL::RadioIsConnected() && !wasConnected_) {
          AnkiEvent( 40, "Radio", 447, "Robot radio is connected.", 0);
          wasConnected_ = true;
          BackpackLightController::TurnOffAll();
          LiftController::Enable();
          HeadController::Enable();
        } else if (!HAL::RadioIsConnected() && wasConnected_) {
          AnkiEvent( 40, "Radio", 251, "Radio disconnected", 0);
          Messages::ResetInit();
          SteeringController::ExecuteDirectDrive(0,0);
          LiftController::Disable();
          HeadController::Disable();
          PickAndPlaceController::Reset();
          PickAndPlaceController::SetCarryState(CARRY_NONE);
          BackpackLightController::Init();

#ifdef ACTIVE_OBJECT_DISCONNECT_ON_ENGINE_DISCONNECT
          // TEMP: Disconnecting active objects from K02 because it seems the Espressif's
          //       backgroundTaskOnDisconnect(), which is supposed to do this, is not getting called.
          for (u32 i=0; i< MAX_NUM_ACTIVE_OBJECTS; ++i) {
            SetPropSlot cubeSlotMsg;
            cubeSlotMsg.slot = i;
            cubeSlotMsg.factory_id = 0;
            RobotInterface::SendMessage(cubeSlotMsg);
          }
#endif

          
          
#ifndef TARGET_K02
          TestModeController::Start(TM_NONE);
          AnimationController::EnableTracks(ALL_TRACKS);
          HAL::FaceClear();
#endif
          wasConnected_ = false;
        }

        // Process any messages from the basestation
        MARK_NEXT_TIME_PROFILE(CozmoBot, MSG);
        Messages::ProcessBTLEMessages();

        //////////////////////////////////////////////////////////////
        // Sensor updates
        //////////////////////////////////////////////////////////////
        MARK_NEXT_TIME_PROFILE(CozmoBot, IMU);
        IMUFilter::Update();
        ProxSensors::Update();


        //////////////////////////////////////////////////////////////
        // Head & Lift Position Updates
        //////////////////////////////////////////////////////////////
        MARK_NEXT_TIME_PROFILE(CozmoBot, ANIM);
#ifndef TARGET_K02
        if(AnimationController::Update() != RESULT_OK) {
          AnkiWarn( 38, "CozmoBot", 252, "Failed updating AnimationController. Clearing.", 0);
          AnimationController::Clear();
        }
#endif
        MARK_NEXT_TIME_PROFILE(CozmoBot, EYEHEADLIFT);
        HeadController::Update();
        LiftController::Update();
        BackpackLightController::Update();
#ifndef TARGET_K02
        BlockLightController::Update();
#endif
        MARK_NEXT_TIME_PROFILE(CozmoBot, PATHDOCK);
        PathFollower::Update();
        PickAndPlaceController::Update();
        DockingController::Update();

#ifndef TARGET_K02
        //////////////////////////////////////////////////////////////
        // Audio Subsystem
        //////////////////////////////////////////////////////////////
        MARK_NEXT_TIME_PROFILE(CozmoBot, AUDIO);
        Anki::Cozmo::HAL::AudioFill();
#endif

        //////////////////////////////////////////////////////////////
        // State Machine
        //////////////////////////////////////////////////////////////
        MARK_NEXT_TIME_PROFILE(CozmoBot, WHEELS);
        switch(mode_)
        {
          case INIT_MOTOR_CALIBRATION:
          {
            if(MotorCalibrationUpdate()) {
              // Once initialization is done, broadcast a message that this robot
              // is ready to go
#ifndef TARGET_K02
              RobotInterface::RobotAvailable msg;
              msg.robotID = HAL::GetIDCard()->esn;
              AnkiEvent( 179, "CozmoBot.BroadcastingAvailability", 479, "RobotID: %d", 1, msg.robotID);
              RobotInterface::SendMessage(msg);
              // Start test mode
              if (DEFAULT_TEST_MODE != TM_NONE) {
                if(TestModeController::Start(DEFAULT_TEST_MODE) == RESULT_FAIL) {
                  AnkiWarn( 180, "CozmoBot.TestModeFailed", 480, "TestMode %d failed to start.", 1, DEFAULT_TEST_MODE);
                  return RESULT_FAIL;
                }
              }
#endif
              
              // Keep lift and head limp by default if not already connected
              // which at this point it usually shouldn't be
              if (!HAL::RadioIsConnected()) {
                LiftController::Disable();
                HeadController::Disable();
              }
              
              mode_ = WAITING;
            }

            break;
          }
          case WAITING:
          {
            // Idle.  Nothing to do yet...

            break;
          }

          default:
            AnkiWarn( 38, "CozmoBot", 253, "Unrecognized CozmoBot mode.", 0);

        } // switch(mode_)

        // Manage the various motion controllers:
        SpeedController::Manage();
        SteeringController::Manage();
        WheelController::Manage();


        //////////////////////////////////////////////////////////////
        // Feedback / Display
        //////////////////////////////////////////////////////////////

        Messages::UpdateRobotStateMsg();
#if(!STREAM_DEBUG_IMAGES)
        ++robotStateMessageCounter_;
        if(robotStateMessageCounter_ >= STATE_MESSAGE_FREQUENCY) {
          Messages::SendRobotStateMsg();
          robotStateMessageCounter_ = 0;
        }
#endif

        // Print time profile stats
        END_TIME_PROFILE(CozmoBot);
        END_TIME_PROFILE(CozmoBotMain);
        PERIODIC_PRINT_AND_RESET_TIME_PROFILE(CozmoBot, 400);
        PERIODIC_PRINT_AND_RESET_TIME_PROFILE(CozmoBotMain, 400);

        // Check if main took too long
        u32 cycleEndTime = HAL::GetMicroCounter();
        u32 cycleTime = cycleEndTime - cycleStartTime;
        if (cycleTime > MAIN_TOO_LONG_TIME_THRESH_USEC) {
          ++mainTooLongCnt_;
          avgMainTooLongTime_ = (u32)((f32)(avgMainTooLongTime_ * (mainTooLongCnt_ - 1) + cycleTime)) / mainTooLongCnt_;
        }
        lastCycleStartTime_ = cycleStartTime;

        // Report main cycle time error
        if ((mainTooLateCnt_ > 0 || mainTooLongCnt_ > 0) &&
            (cycleEndTime - lastMainCycleTimeErrorReportTime_ > MAIN_CYCLE_ERROR_REPORTING_PERIOD_USEC)) {
          RobotInterface::MainCycleTimeError m;
          m.numMainTooLateErrors = mainTooLateCnt_;
          m.avgMainTooLateTime = avgMainTooLateTime_;
          m.numMainTooLongErrors = mainTooLongCnt_;
          m.avgMainTooLongTime = avgMainTooLongTime_;

          RobotInterface::SendMessage(m);

          mainTooLateCnt_ = 0;
          avgMainTooLateTime_ = 0;
          mainTooLongCnt_ = 0;
          avgMainTooLongTime_ = 0;

          lastMainCycleTimeErrorReportTime_ = cycleEndTime;
        }


        return RESULT_OK;

      } // Robot::step_MainExecution()




      // Long Execution now just captures image
      Result step_LongExecution()
      {
        Result retVal = RESULT_OK;

#       ifdef SIMULATOR

        if (!HAL::IsVideoEnabled()) {
          return retVal;
        }

        if (HAL::imageSendMode_ != Off) {

          TimeStamp_t currentTime = HAL::GetTimeStamp();

          // This computation is based on Cyberbotics support's explaination for how to compute
          // the actual capture time of the current available image from the simulated
          // camera, *except* I seem to need the extra "- VISION_TIME_STEP" for some reason.
          // (The available frame is still one frame behind? I.e. we are just *about* to capture
          //  the next one?)
          TimeStamp_t currentImageTime = floor((currentTime-HAL::GetCameraStartTime())/VISION_TIME_STEP) * VISION_TIME_STEP + HAL::GetCameraStartTime() - VISION_TIME_STEP;

          // Keep up with the capture time of the last image we sent
          static TimeStamp_t lastImageSentTime = 0;

          // Have we already sent the currently-available image?
          if(lastImageSentTime != currentImageTime)
          {
            // Nope, so get the (new) available frame from the camera:
            const s32 captureHeight = Vision::CameraResInfo[HAL::captureResolution_].height;
            const s32 captureWidth  = Vision::CameraResInfo[HAL::captureResolution_].width * 3; // The "*3" is a hack to get enough room for color

            static const int bufferSize = 1000000;
            static u8 buffer[bufferSize];

            HAL::CameraGetFrame(buffer,
                                HAL::captureResolution_, false);
            // Send the image, with its actual capture time (not the current system time)
            Messages::CompressAndSendImage(buffer, captureHeight, captureWidth, currentImageTime);

            //PRINT("Sending state message from time = %d to correspond to image at time = %d\n",
            //      robotState.timestamp, currentImageTime);

            // Mark that we've already sent the image for the current time
            lastImageSentTime = currentImageTime;
          } // if(lastImageSentTime != currentImageTime)


          if (HAL::imageSendMode_ == SingleShot) {
            HAL::imageSendMode_ = Off;
          }

        } // if (HAL::imageSendMode_ != ISM_OFF)

#       endif // ifdef SIMULATOR

        return retVal;

      } // Robot::step_longExecution()


    } // namespace Robot
  } // namespace Cozmo
} // namespace Anki

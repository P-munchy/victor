/*
 * File:          webotsCtrlKeyboard.cpp
 * Date:
 * Description:   
 * Author:        
 * Modifications: 
 */

#include "webotsCtrlKeyboard.h"

#include "anki/common/basestation/colorRGBA.h"
#include "anki/common/basestation/math/point_impl.h"
#include "anki/common/basestation/math/pose.h"
#include "anki/cozmo/basestation/behaviorManager.h"
#include "anki/cozmo/basestation/behaviorSystem/behaviorChooserTypesHelpers.h"
#include "anki/cozmo/basestation/behaviorSystem/behaviorGroupHelpers.h"
#include "anki/cozmo/basestation/block.h"
#include "anki/cozmo/basestation/components/unlockIdsHelpers.h"
#include "anki/cozmo/basestation/imageDeChunker.h"
#include "anki/cozmo/basestation/moodSystem/emotionTypesHelpers.h"
#include "anki/cozmo/shared/cozmoConfig.h"
#include "anki/cozmo/shared/cozmoEngineConfig.h"
#include "anki/vision/basestation/image.h"
#include "clad/types/actionTypes.h"
#include "clad/types/activeObjectTypes.h"
#include "clad/types/behaviorChooserType.h"
#include "clad/types/behaviorTypes.h"
#include "clad/types/ledTypes.h"
#include "clad/types/proceduralEyeParameters.h"
#include "util/logging/printfLoggerProvider.h"
#include "util/fileUtils/fileUtils.h"
#include <fstream>
#include <opencv2/imgproc/imgproc.hpp>
#include <stdio.h>
#include <string.h>
#include <webots/Compass.hpp>
#include <webots/Display.hpp>
#include <webots/GPS.hpp>
#include <webots/ImageRef.hpp>


// CAUTION: If enabled, you can mess up stuff stored on the robot's flash.
#define ENABLE_NVSTORAGE_WRITE 0

namespace Anki {
  namespace Cozmo {
      
      
      // Private members:
      namespace {

        std::set<int> lastKeysPressed_;
        
        bool wasMovingWheels_ = false;
        bool wasMovingHead_   = false;
        bool wasMovingLift_   = false;
        
        webots::Node* root_ = nullptr;
        
        u8 poseMarkerMode_ = 0;
        Anki::Pose3d prevPoseMarkerPose_;
        Anki::Pose3d poseMarkerPose_;
        webots::Field* poseMarkerDiffuseColor_ = nullptr;
        double poseMarkerColor_[2][3] = { {0.1, 0.8, 0.1} // Goto pose color
          ,{0.8, 0.1, 0.1} // Place object color
        };
        
        double lastKeyPressTime_;
        
        PathMotionProfile pathMotionProfile_ = PathMotionProfile();
        
        // For displaying cozmo's POV:
        webots::Display* cozmoCam_;
        webots::ImageRef* img_ = nullptr;
        
        ImageDeChunker _imageDeChunker;
        
        // Save robot image to file
        bool saveRobotImageToFile_ = false;
        
        std::string _drivingStartAnim = "";
        std::string _drivingLoopAnim = "";
        std::string _drivingEndAnim = "";

        // Manufacturing data save folder name
        std::string _mfgDataSaveFolder = "";
        std::string _mfgDataSaveFile = "nvStorageStuff.txt";
        
      } // private namespace
    
      // ======== Message handler callbacks =======
    
    // For processing image chunks arriving from robot.
    // Sends complete images to VizManager for visualization (and possible saving).
    void WebotsKeyboardController::HandleImageChunk(ImageChunk const& msg)
    {
      const u16 width  = Vision::CameraResInfo[(int)msg.resolution].width;
      const u16 height = Vision::CameraResInfo[(int)msg.resolution].height;
      const bool isImageReady = _imageDeChunker.AppendChunk(msg.imageId, msg.frameTimeStamp, height, width,
        msg.imageEncoding, msg.imageChunkCount, msg.chunkId, msg.data.data(), (uint32_t)msg.data.size());
      
      
      if(isImageReady)
      {
        cv::Mat img = _imageDeChunker.GetImage();
        if(img.channels() == 1) {
          cvtColor(img, img, CV_GRAY2RGB);
        }
        
        const s32 outputColor = 1; // 1 for Green, 2 for Blue
        
        for(s32 i=0; i<img.rows; ++i) {
          
          if(i % 2 == 0) {
            cv::Mat img_i = img.row(i);
            img_i.setTo(0);
          } else {
            u8* img_i = img.ptr(i);
            for(s32 j=0; j<img.cols; ++j) {
              img_i[3*j+outputColor] = std::max(std::max(img_i[3*j], img_i[3*j + 1]), img_i[3*j + 2]);
              
              img_i[3*j+(3-outputColor)] /= 2;
              img_i[3*j] = 0; // kill red channel
              
              // [Optional] Add a bit of noise
              f32 noise = 20.f*static_cast<f32>(std::rand()) / static_cast<f32>(RAND_MAX) - 0.5f;
              img_i[3*j+outputColor] = static_cast<u8>(std::max(0.f,std::min(255.f,static_cast<f32>(img_i[3*j+outputColor]) + noise)));
              
            }
          }
        }
        
        // Delete existing image if there is one.
        if (img_ != nullptr) {
          cozmoCam_->imageDelete(img_);
        }
        
        img_ = cozmoCam_->imageNew(img.cols, img.rows, img.data, webots::Display::RGB);
        cozmoCam_->imagePaste(img_, 0, 0);
        
        // Save image to file
        if (saveRobotImageToFile_) {
          static u32 imgCnt = 0;
          char imgFileName[16];
          printf("SAVING IMAGE\n");
          sprintf(imgFileName, "robotImg_%d.jpg", imgCnt++);
          cozmoCam_->imageSave(img_, imgFileName);
          saveRobotImageToFile_ = false;
        }
        
      } // if(isImageReady)
      
    } // HandleImageChunk()
    
    
    void WebotsKeyboardController::HandleRobotObservedObject(ExternalInterface::RobotObservedObject const& msg)
    {
      if(cozmoCam_ == nullptr) {
        printf("RECEIVED OBJECT OBSERVED: objectID %d\n", msg.objectID);
      } else {
        // Draw a rectangle in red with the object ID as text in the center
        cozmoCam_->setColor(0x000000);
        
        //std::string dispStr(ObjectType::GetName(msg.objectType));
        //dispStr += " ";
        //dispStr += std::to_string(msg.objectID);
        std::string dispStr("Type=" + std::string(ObjectTypeToString(msg.objectType)) + "\nID=" + std::to_string(msg.objectID));
        cozmoCam_->drawText(dispStr,
                            msg.img_topLeft_x + msg.img_width/4 + 1,
                            msg.img_topLeft_y + msg.img_height/2 + 1);
        
        cozmoCam_->setColor(0xff0000);
        cozmoCam_->drawRectangle(msg.img_topLeft_x, msg.img_topLeft_y,
                                 msg.img_width, msg.img_height);
        cozmoCam_->drawText(dispStr,
                            msg.img_topLeft_x + msg.img_width/4,
                            msg.img_topLeft_y + msg.img_height/2);

      }
      
    }
    
    void WebotsKeyboardController::HandleRobotObservedFace(ExternalInterface::RobotObservedFace const& msg)
    {
      //printf("RECEIVED FACE OBSERVED: faceID %llu\n", msg.faceID);
      // _lastFace = msg;
    }

    void WebotsKeyboardController::HandleDebugString(ExternalInterface::DebugString const& msg)
    {
      // Useful for debug, but otherwise unneeded since this is displayed in the
      // status window
      //printf("HandleDebugString: %s\n", msg.text.c_str());
    }


    void WebotsKeyboardController::HandleRobotCompletedAction(const ExternalInterface::RobotCompletedAction &msg)
    {
      switch(msg.actionType)
      {
        case RobotActionType::ENROLL_NAMED_FACE:
        {
          auto & completionInfo = msg.completionInfo.Get_faceEnrollmentCompleted();
          if(msg.result == ActionResult::SUCCESS)
          {
            printf("RobotEnrolledFace: Added '%s' with ID=%d\n",
                   completionInfo.name.c_str(), completionInfo.faceID);
            
//            using namespace ExternalInterface;
//            SayText sayText;
//            sayText.text = completionInfo.name;
//            //sayText.playEvent = GameEvent::OnLearnedPlayerName;
//            sayText.style = SayTextStyle::Name_Normal;
//            
//            SendMessage(MessageGameToEngine(std::move(sayText)));
          } else {
            printf("RobotEnrolledFace FAILED\n");
          }
          break;
        } // ENROLL_NAMED_FACE
          
        default:
          // Just ignore other action types
          break;
          
      } // switch(actionType)
      
    } // HandleRobotCompletedAction()
    
    // ============== End of message handlers =================

    void WebotsKeyboardController::PreInit()
    {
      // Make root point to WebotsKeyBoardController node
      root_ = GetSupervisor()->getSelf();

      // enable keyboard
      GetSupervisor()->keyboardEnable(GetStepTimeMS());
    }
  
    void WebotsKeyboardController::WaitOnKeyboardToConnect()
    {
      webots::Field* autoConnectField = root_->getField("autoConnect");
      if( autoConnectField == nullptr ) {
        PRINT_NAMED_ERROR("WebotsKeyboardController.MissingField",
                          "missing autoConnect field, assuming we shoudl auto connect");
        return;
      }
      else {
        bool autoConnect = autoConnectField->getSFBool();
        if( autoConnect ) {
          return;
        }
      }

      PRINT_NAMED_INFO("WebotsKeyboardController.WaitForStart",
                       "Press Shift+Enter to start the engine");
      
      const int EnterKey = 4; // tested experimentally... who knows if this will work on other platforms
      const int ShiftEnterKey = EnterKey | webots::Supervisor::KEYBOARD_SHIFT;

      bool start = false;
      while( !start && !_shouldQuit ) {
        int key = -1;
        while((key = GetSupervisor()->keyboardGetKey()) != 0 && !_shouldQuit) {
          if(key == ShiftEnterKey) {
            start = true;
          }
        }
        // manually step simulation
        GetSupervisor()->step(GetStepTimeMS());
      }
    }
  
    void WebotsKeyboardController::InitInternal()
    { 
      poseMarkerDiffuseColor_ = root_->getField("poseMarkerDiffuseColor");
        
      cozmoCam_ = GetSupervisor()->getDisplay("uiCamDisplay");
    }    
    
    WebotsKeyboardController::WebotsKeyboardController(s32 step_time_ms) :
    UiGameController(step_time_ms)
    {
      
    }
      
      void WebotsKeyboardController::PrintHelp()
      {
        printf("\nBasestation keyboard control\n");
        printf("===============================\n");
        printf("                           Drive:  arrows  (Hold shift for slower speeds)\n");
        printf("               Move lift up/down:  a/z\n");
        printf("               Move head up/down:  s/x\n");
        printf("             Lift low/high/carry:  1/2/3\n");
        printf("            Head down/forward/up:  4/5/6\n");
        printf("            Request *game* image:  i\n");
        printf("           Request *robot* image:  Alt+i\n");
        printf("      Toggle *game* image stream:  Shift+i\n");
        printf("     Toggle *robot* image stream:  Alt+Shift+i\n");
        printf("              Toggle save images:  e\n");
        printf("        Toggle VizObject display:  d\n");
        printf("   Toggle addition/deletion mode:  Shift+d\n");
        printf("Goto/place object at pose marker:  g\n");
        printf("         Toggle pose marker mode:  Shift+g\n");
        printf("              Cycle block select:  .\n");
        printf("              Clear known blocks:  c\n");
        printf("         Clear all known objects:  Alt+c\n");
        printf("         Select behavior by type:  Shift+c\n");
        printf("         Select behavior by name:  Alt+Shift+c\n");
        printf("          Dock to selected block:  p\n");
        printf("          Dock from current pose:  Shift+p\n");
        printf("    Travel up/down selected ramp:  r\n");
        printf("              Abort current path:  q\n");
        printf("                Abort everything:  Shift+q\n");
        printf("           Cancel current action:  Alt+q\n");
        printf("         Update controller gains:  k\n");
        printf("                 Request IMU log:  o\n");
        printf("           Toggle face detection:  f\n");
        printf(" Assign userName to current face:  Shift+f\n");
        printf("          Turn towards last face:  Alt+f\n");
        printf("              Reset 'owner' face:  Alt+Shift+f\n");
        printf("                      Test modes:  Alt + Testmode#\n");
        printf("                Follow test plan:  t\n");
        printf("        Force-add specifed robot:  Shift+r\n");
        printf("                 Select behavior:  Shift+c\n");
        printf("         Select behavior chooser:  h\n");
        printf("           Enable behavior group:  Shift+h\n");
        printf("          Disable behavior group:  Alt+h\n");
        printf("            Set emotion to value:  m\n");
        printf("      Search side to side action:  Shift+l\n");
        printf("    Toggle cliff sensor handling:  Alt+l\n");
        printf("                 Next Demo State:  j\n");
        printf("            Start Demo (hasEdge):  Shift+j\n");
        printf("      Play 'animationToSendName':  Shift+6\n");
        printf("  Set idle to'idleAnimationName':  Shift+Alt+6\n");
        printf("     Update Viz origin alignment:  ` <backtick>\n");
        printf("       unlock progression unlock:  n\n");
        printf("         lock progression unlock:  Shift+n\n");
        printf("    Respond 'no' to game request:  Alt+n\n");
        printf("             Flip selected block:  y\n");
        printf("        Quit keyboard controller:  Shift+Alt+x\n");
        printf("                      Print help:  ?\n");
        printf("\n");
      }
      
      //Check the keyboard keys and issue robot commands
      void WebotsKeyboardController::ProcessKeystroke()
      {
        bool movingHead   = false;
        bool movingLift   = false;
        bool movingWheels = false;
        s8 steeringDir = 0;  // -1 = left, 0 = straight, 1 = right
        s8 throttleDir = 0;  // -1 = reverse, 0 = stop, 1 = forward
        
        f32 leftSpeed = 0.f;
        f32 rightSpeed = 0.f;
        
        f32 commandedLiftSpeed = 0.f;
        f32 commandedHeadSpeed = 0.f;
        
        root_ = GetSupervisor()->getSelf();
        f32 wheelSpeed = root_->getField("driveSpeedNormal")->getSFFloat();
        f32 driveAccel = root_->getField("driveAccel")->getSFFloat();
        
        f32 steeringCurvature = root_->getField("steeringCurvature")->getSFFloat();
        
        static bool keyboardRestart = false;
        if (keyboardRestart) {
          GetSupervisor()->keyboardDisable();
          GetSupervisor()->keyboardEnable(BS_TIME_STEP);
          keyboardRestart = false;
        }
        
        // Get all keys pressed this tic
        std::set<int> keysPressed;
        int key;
        while((key = GetSupervisor()->keyboardGetKey()) != 0) {
          keysPressed.insert(key);
        }
        
        // If exact same keys were pressed last tic, do nothing.
        if (lastKeysPressed_ == keysPressed) {
          return;
        }
        lastKeysPressed_ = keysPressed;        
        
        for(auto key : keysPressed)
        {
          // Extract modifier key(s)
          int modifier_key = key & ~webots::Supervisor::KEYBOARD_KEY;
          
          // Set key to its modifier-less self
          key &= webots::Supervisor::KEYBOARD_KEY;
          
          lastKeyPressTime_ = GetSupervisor()->getTime();
          
          // DEBUG: Display modifier key information
          /*
          printf("Key = '%c'", char(key));
          if(modifier_key) {
            printf(", with modifier keys: ");
            if(modifier_key & webots::Supervisor::KEYBOARD_ALT) {
              printf("ALT ");
            }
            if(modifier_key & webots::Supervisor::KEYBOARD_SHIFT) {
              printf("SHIFT ");
            }
            if(modifier_key & webots::Supervisor::KEYBOARD_CONTROL) {
              printf("CTRL/CMD ");
            }
           
          }
          printf("\n");
          */
          
          // Use slow motor speeds if SHIFT is pressed
          f32 liftSpeed = DEG_TO_RAD_F32(root_->getField("liftSpeedDegPerSec")->getSFFloat());
          f32 liftAccel = DEG_TO_RAD_F32(root_->getField("liftAccelDegPerSec2")->getSFFloat());
          f32 liftDurationSec = root_->getField("liftDurationSec")->getSFFloat();
          f32 headSpeed = DEG_TO_RAD_F32(root_->getField("headSpeedDegPerSec")->getSFFloat());
          f32 headAccel = DEG_TO_RAD_F32(root_->getField("headAccelDegPerSec2")->getSFFloat());
          f32 headDurationSec = root_->getField("headDurationSec")->getSFFloat();
          if (modifier_key & webots::Supervisor::KEYBOARD_SHIFT) {
            wheelSpeed = root_->getField("driveSpeedSlow")->getSFFloat();
            liftSpeed *= 0.5;
            headSpeed *= 0.5;
          } else if(modifier_key & webots::Supervisor::KEYBOARD_ALT) {
            wheelSpeed = root_->getField("driveSpeedTurbo")->getSFFloat();
          }
          
          // Point turn amount and speed/accel
          f32 pointTurnAngle = std::fabs(root_->getField("pointTurnAngle_deg")->getSFFloat());
          f32 pointTurnSpeed = std::fabs(root_->getField("pointTurnSpeed_degPerSec")->getSFFloat());
          f32 pointTurnAccel = std::fabs(root_->getField("pointTurnAccel_degPerSec2")->getSFFloat());
          
          // Dock speed
          const f32 dockSpeed_mmps = root_->getField("dockSpeed_mmps")->getSFFloat();
          const f32 dockAccel_mmps2 = root_->getField("dockAccel_mmps2")->getSFFloat();
          const f32 dockDecel_mmps2 = root_->getField("dockDecel_mmps2")->getSFFloat();
          
          // Path speeds
          const f32 pathSpeed_mmps = root_->getField("pathSpeed_mmps")->getSFFloat();
          const f32 pathAccel_mmps2 = root_->getField("pathAccel_mmps2")->getSFFloat();
          const f32 pathDecel_mmps2 = root_->getField("pathDecel_mmps2")->getSFFloat();
          const f32 pathPointTurnSpeed_radPerSec = root_->getField("pathPointTurnSpeed_radPerSec")->getSFFloat();
          const f32 pathPointTurnAccel_radPerSec2 = root_->getField("pathPointTurnAccel_radPerSec2")->getSFFloat();
          const f32 pathPointTurnDecel_radPerSec2 = root_->getField("pathPointTurnDecel_radPerSec2")->getSFFloat();
          const f32 pathReverseSpeed_mmps = root_->getField("pathReverseSpeed_mmps")->getSFFloat();

          // If any of the pathMotionProfile fields are different than the default values use a custom profile
          if(pathMotionProfile_.speed_mmps != pathSpeed_mmps ||
             pathMotionProfile_.accel_mmps2 != pathAccel_mmps2 ||
             pathMotionProfile_.decel_mmps2 != pathDecel_mmps2 ||
             pathMotionProfile_.pointTurnSpeed_rad_per_sec != pathPointTurnSpeed_radPerSec ||
             pathMotionProfile_.pointTurnAccel_rad_per_sec2 != pathPointTurnAccel_radPerSec2 ||
             pathMotionProfile_.pointTurnDecel_rad_per_sec2 != pathPointTurnDecel_radPerSec2 ||
             pathMotionProfile_.dockSpeed_mmps != dockSpeed_mmps ||
             pathMotionProfile_.dockAccel_mmps2 != dockAccel_mmps2 ||
             pathMotionProfile_.dockDecel_mmps2 != dockDecel_mmps2 ||
             pathMotionProfile_.reverseSpeed_mmps != pathReverseSpeed_mmps)
          {
            pathMotionProfile_.isCustom = true;
          }

          pathMotionProfile_.speed_mmps = pathSpeed_mmps;
          pathMotionProfile_.accel_mmps2 = pathAccel_mmps2;
          pathMotionProfile_.decel_mmps2 = pathDecel_mmps2;
          pathMotionProfile_.pointTurnSpeed_rad_per_sec = pathPointTurnSpeed_radPerSec;
          pathMotionProfile_.pointTurnAccel_rad_per_sec2 = pathPointTurnAccel_radPerSec2;
          pathMotionProfile_.pointTurnDecel_rad_per_sec2 = pathPointTurnDecel_radPerSec2;
          pathMotionProfile_.dockSpeed_mmps = dockSpeed_mmps;
          pathMotionProfile_.dockAccel_mmps2 = dockAccel_mmps2;
          pathMotionProfile_.dockDecel_mmps2 = dockDecel_mmps2;
          pathMotionProfile_.reverseSpeed_mmps = pathReverseSpeed_mmps;
          
          // For pickup or placeRel, specify whether or not you want to use the
          // given approach angle for pickup, placeRel, or roll actions
          bool useApproachAngle = root_->getField("useApproachAngle")->getSFBool();
          f32 approachAngle_rad = DEG_TO_RAD_F32(root_->getField("approachAngle_deg")->getSFFloat());
          
          // For placeOn and placeOnGround, specify whether or not to use the exactRotation specified
          bool useExactRotation = root_->getField("useExactPlacementRotation")->getSFBool();
          
          //printf("keypressed: %d, modifier %d, orig_key %d, prev_key %d\n",
          //       key, modifier_key, key | modifier_key, lastKeyPressed_);
          
          const std::string drivingStartAnim = root_->getField("drivingStartAnim")->getSFString();
          const std::string drivingLoopAnim = root_->getField("drivingLoopAnim")->getSFString();
          const std::string drivingEndAnim = root_->getField("drivingEndAnim")->getSFString();
          if(_drivingStartAnim.compare(drivingStartAnim) != 0 ||
             _drivingLoopAnim.compare(drivingLoopAnim) != 0 ||
             _drivingEndAnim.compare(drivingEndAnim) != 0)
          {
            _drivingStartAnim = drivingStartAnim;
            _drivingLoopAnim = drivingLoopAnim;
            _drivingEndAnim = drivingEndAnim;
          
            // Pop whatever driving animations were being used and push the new ones
            ExternalInterface::MessageGameToEngine msg1;
            msg1.Set_PopDrivingAnimations(ExternalInterface::PopDrivingAnimations());
            SendMessage(msg1);
          
            ExternalInterface::PushDrivingAnimations m;
            m.drivingStartAnim = _drivingStartAnim;
            m.drivingLoopAnim = _drivingLoopAnim;
            m.drivingEndAnim = _drivingEndAnim;
            
            ExternalInterface::MessageGameToEngine msg2;
            msg2.Set_PushDrivingAnimations(m);
            SendMessage(msg2);
          }
          
          
          // Check for test mode (alt + key)
          bool testMode = false;
          if (modifier_key & webots::Supervisor::KEYBOARD_ALT) {
            if (key >= '0' && key <= '9') {
              if (modifier_key & webots::Supervisor::KEYBOARD_SHIFT) {
                // Hold shift down too to add 10 to the pressed key
                key += 10;
              }
              
              TestMode m = TestMode(key - '0');

              // Set parameters for special test cases
              s32 p1 = 0, p2 = 0, p3 = 0;
              switch(m) {
                case TestMode::TM_DIRECT_DRIVE:
                  // p1: flags (See DriveTestFlags)
                  // p2: wheelPowerStepPercent (only applies if DTF_ENABLE_DIRECT_HAL_TEST is set)
                  // p3: wheelSpeed_mmps (only applies if DTF_ENABLE_DIRECT_HAL_TEST is not set)
                  //p1 = DTF_ENABLE_DIRECT_HAL_TEST | DTF_ENABLE_CYCLE_SPEEDS_TEST;
                  //p2 = 5;
                  //p3 = 20;
                  p1 = root_->getField("driveTest_flags")->getSFInt32();
                  p2 = 10;
                  p3 = root_->getField("driveTest_wheel_power")->getSFInt32();
                  break;
                case TestMode::TM_LIFT:
                  p1 = root_->getField("liftTest_flags")->getSFInt32();
                  p2 = root_->getField("liftTest_nodCycleTimeMS")->getSFInt32();  // Nodding cycle time in ms (if LiftTF_NODDING flag is set)
                  p3 = root_->getField("liftTest_powerPercent")->getSFInt32();    // Power to run motor at. If 0, cycle through increasing power. Only used during LiftTF_TEST_POWER.
                  break;
                case TestMode::TM_HEAD:
                  p1 = root_->getField("headTest_flags")->getSFInt32();
                  p2 = root_->getField("headTest_nodCycleTimeMS")->getSFInt32();  // Nodding cycle time in ms (if HTF_NODDING flag is set)
                  p3 = root_->getField("headTest_powerPercent")->getSFInt32();    // Power to run motor at. If 0, cycle through increasing power. Only used during HTF_TEST_POWER.
                  break;
                case TestMode::TM_PLACE_BLOCK_ON_GROUND:
                  p1 = 100;  // x_offset_mm
                  p2 = -10;  // y_offset_mm
                  p3 = 0;    // angle_offset_degrees
                  break;
                case TestMode::TM_LIGHTS:
                  // p1: flags (See LightTestFlags)
                  // p2: The LED channel to activate (applies if LTF_CYCLE_ALL not enabled)
                  // p3: The color to set it to (applies if LTF_CYCLE_ALL not enabled)
                  p1 = (s32)LightTestFlags::LTF_CYCLE_ALL;
                  p2 = (s32)LEDId::LED_BACKPACK_RIGHT;
                  p3 = (s32)LEDColor::LED_GREEN;
                  break;
                case TestMode::TM_STOP_TEST:
                  p1 = 100;  // slow speed (mmps)
                  p2 = 200;  // fast speed (mmps)
                  p3 = 1000; // period (ms)
                  break;
                default:
                  break;
              }
              
              printf("Sending test mode %s\n", TestModeToString(m));
              SendStartTestMode(m,p1,p2,p3);

              testMode = true;
            }
          }
          
          if(!testMode) {

            // Check for (mostly) single key commands
            switch (key)
            {
              case webots::Robot::KEYBOARD_UP:
              {
                ++throttleDir;
                break;
              }
                
              case webots::Robot::KEYBOARD_DOWN:
              {
                --throttleDir;
                break;
              }
                
              case webots::Robot::KEYBOARD_LEFT:
              {
                --steeringDir;
                break;
              }
                
              case webots::Robot::KEYBOARD_RIGHT:
              {
                ++steeringDir;
                break;
              }
                
              case (s32)'<':
              {
                if(modifier_key & webots::Supervisor::KEYBOARD_ALT) {
                  SendTurnInPlaceAtSpeed(DEG_TO_RAD(pointTurnSpeed), DEG_TO_RAD(pointTurnAccel));
                }
                else {
                  SendTurnInPlace(DEG_TO_RAD(pointTurnAngle), DEG_TO_RAD(pointTurnSpeed), DEG_TO_RAD(pointTurnAccel));
                }
                break;
              }
                
              case (s32)'>':
              {
                if(modifier_key & webots::Supervisor::KEYBOARD_ALT) {
                  SendTurnInPlaceAtSpeed(DEG_TO_RAD(-pointTurnSpeed), DEG_TO_RAD(pointTurnAccel));
                } else {
                  SendTurnInPlace(DEG_TO_RAD(-pointTurnAngle), DEG_TO_RAD(-pointTurnSpeed), DEG_TO_RAD(pointTurnAccel));
                }
                break;
              }
                
              case webots::Supervisor::KEYBOARD_PAGEUP:
              {
                SendMoveHeadToAngle(MAX_HEAD_ANGLE, 20, 2);
                break;
              }
                
              case webots::Supervisor::KEYBOARD_PAGEDOWN:
              {
                SendMoveHeadToAngle(MIN_HEAD_ANGLE, 20, 2);
                break;
              }
                
              case (s32)'S':
              {
                if(modifier_key == webots::Supervisor::KEYBOARD_ALT) {
                  // Re-read animations and send them to physical robot
                  SendReplayLastAnimation();
                }
                else {
                  commandedHeadSpeed += headSpeed;
                  movingHead = true;
                }
                break;
              }
                
              case (s32)'X':
              {
                if( modifier_key & webots::Supervisor::KEYBOARD_ALT &&
                    modifier_key & webots::Supervisor::KEYBOARD_SHIFT ) {
                  _shouldQuit = true;
                }
                else {
                  commandedHeadSpeed -= headSpeed;
                  movingHead = true;
                }
                break;
              }
                
              case (s32)'A':
              {
                if(modifier_key == webots::Supervisor::KEYBOARD_ALT) {
                  // Re-read animations and send them to physical robot
                  SendReadAnimationFile();
                } else {
                  commandedLiftSpeed += liftSpeed;
                  movingLift = true;
                }
                break;
              }
                
              case (s32)'Z':
              {
                if(modifier_key == webots::Supervisor::KEYBOARD_ALT) {
                  static bool liftPowerEnable = false;
                  SendEnableLiftPower(liftPowerEnable);
                  liftPowerEnable = !liftPowerEnable;
                } else {
                  commandedLiftSpeed -= liftSpeed;
                  movingLift = true;
                }
                break;
              }
                
              case '1': //set lift to low dock height
              {
                SendMoveLiftToHeight(LIFT_HEIGHT_LOWDOCK, liftSpeed, liftAccel, liftDurationSec);
                break;
              }
                
              case '2': //set lift to high dock height
              {
                SendMoveLiftToHeight(LIFT_HEIGHT_HIGHDOCK, liftSpeed, liftAccel, liftDurationSec);
                break;
              }
                
              case '3': //set lift to carry height
              {
                SendMoveLiftToHeight(LIFT_HEIGHT_CARRY, liftSpeed, liftAccel, liftDurationSec);
                break;
              }
                
              case '4': //set head to look all the way down
              {
                SendMoveHeadToAngle(MIN_HEAD_ANGLE, headSpeed, headAccel, headDurationSec);
                break;
              }
                
              case '5': //set head to straight ahead
              {
                SendMoveHeadToAngle(0, headSpeed, headAccel, headDurationSec);
                break;
              }
                
              case '6': //set head to look all the way up
              {
                SendMoveHeadToAngle(MAX_HEAD_ANGLE, headSpeed, headAccel, headDurationSec);
                break;
              }
                
              case (s32)' ': // (space bar)
              {
                SendStopAllMotors();
                break;
              }
                
              case (s32)'I':
              {
                //if(modifier_key & webots::Supervisor::KEYBOARD_CONTROL) {
                // CTRL/CMD+I - Tell physical robot to send a single image
                ImageSendMode mode = ImageSendMode::SingleShot;
                
                if (modifier_key & webots::Supervisor::KEYBOARD_SHIFT) {
                  // CTRL/CMD+SHIFT+I - Toggle physical robot image streaming
                  static bool streamOn = false;
                  if (streamOn) {
                    mode = ImageSendMode::Off;
                    printf("Turning robot image streaming OFF.\n");
                  } else {
                    mode = ImageSendMode::Stream;
                    printf("Turning robot image streaming ON.\n");
                  }
                  streamOn = !streamOn;
                } else {
                  printf("Requesting single robot image.\n");
                }
                

                // Determine resolution from "streamResolution" setting in the keyboard controller
                // node
                ImageResolution resolution = (ImageResolution)IMG_STREAM_RES;
               
                if (root_) {
                  const std::string resString = root_->getField("streamResolution")->getSFString();
                  printf("Attempting to switch robot to %s resolution.\n", resString.c_str());
                  if(resString == "VGA") {
                    resolution = ImageResolution::VGA;
                  } else if(resString == "QVGA") {
                    resolution = ImageResolution::QVGA;
                  } else if(resString == "CVGA") {
                    resolution = ImageResolution::CVGA;
                  } else {
                    printf("Unsupported streamResolution = %s\n", resString.c_str());
                  }
                }
                
                SendSetRobotImageSendMode(mode, resolution);

                break;
              }
                
              case (s32)'U':
              {
                // TODO: How to choose which robot
                const RobotID_t robotID = 1;
                
                // U - Request a single image from the game for a specified robot
                ImageSendMode mode = ImageSendMode::SingleShot;
                
                if (modifier_key & webots::Supervisor::KEYBOARD_SHIFT) {
                  // SHIFT+I - Toggle image streaming from the game
                  static bool streamOn = true;
                  if (streamOn) {
                    mode = ImageSendMode::Off;
                    printf("Turning game image streaming OFF.\n");
                  } else {
                    mode = ImageSendMode::Stream;
                    printf("Turning game image streaming ON.\n");
                  }
                  streamOn = !streamOn;
                } else {
                  printf("Requesting single game image.\n");
                }
                
                SendImageRequest(mode, robotID);
                
                break;
              }
                
              case (s32)'E':
              {
                // Toggle saving of images to pgm
                SaveMode_t mode = SAVE_ONE_SHOT;
                
                const bool alsoSaveState = modifier_key & webots::Supervisor::KEYBOARD_ALT;
                
                if (modifier_key & webots::Supervisor::KEYBOARD_SHIFT) {
                  static bool streamOn = false;
                  if (streamOn) {
                    mode = SAVE_OFF;
                    printf("Saving robot image/state stream OFF.\n");
                  } else {
                    mode = SAVE_CONTINUOUS;
                    printf("Saving robot image %sstream ON.\n", alsoSaveState ? "and state " : "");
                  }
                  streamOn = !streamOn;
                } else {
                  printf("Saving single robot image%s.\n", alsoSaveState ? " and state message" : "");
                }
                
                SendSaveImages(mode, alsoSaveState);
                break;
              }
                
              case (s32)'D':
              {
              
                // Shift+Alt+D = delocalize
                if((modifier_key & webots::Supervisor::KEYBOARD_ALT) &&
                   (modifier_key & webots::Supervisor::KEYBOARD_SHIFT) )
                {
                  ExternalInterface::ForceDelocalizeRobot delocMsg;
                  delocMsg.robotID = 1;
                  SendMessage(ExternalInterface::MessageGameToEngine(std::move(delocMsg)));
                } else if(modifier_key & webots::Supervisor::KEYBOARD_SHIFT) {
                  
                  static const std::array<std::pair<bool,bool>,4> enableModes = {{
                    {false, false}, {false, true}, {true, false}, {true, true}
                  }};
                  static auto enableModeIter = enableModes.begin();
                  
                  printf("Setting addition/deletion mode to %s/%s.\n",
                         enableModeIter->first ? "TRUE" : "FALSE",
                         enableModeIter->second ? "TRUE" : "FALSE");
                  ExternalInterface::SetObjectAdditionAndDeletion msg;
                  msg.robotID = 1;
                  msg.enableAddition = enableModeIter->first;
                  msg.enableDeletion = enableModeIter->second;
                  ExternalInterface::MessageGameToEngine msgWrapper;
                  msgWrapper.Set_SetObjectAdditionAndDeletion(msg);
                  SendMessage(msgWrapper);
                  
                  ++enableModeIter;
                  if(enableModeIter == enableModes.end()) {
                    enableModeIter = enableModes.begin();
                  }
                } else if(modifier_key & webots::Supervisor::KEYBOARD_ALT) {
                  static bool enablePickupParalysis = true;
                  SendEnableRobotPickupParalysis(enablePickupParalysis);
                  printf("Sent EnableRobotPickupParalysis = %d\n", enablePickupParalysis);
                  enablePickupParalysis = !enablePickupParalysis;
                } else {
                  static bool showObjects = false;
                  SendEnableDisplay(showObjects);
                  showObjects = !showObjects;
                }
                break;
              }
                
              case (s32)'G':
              {
                if (modifier_key & webots::Supervisor::KEYBOARD_SHIFT) {
                  poseMarkerMode_ = !poseMarkerMode_;
                  printf("Pose marker mode: %d\n", poseMarkerMode_);
                  poseMarkerDiffuseColor_->setSFColor(poseMarkerColor_[poseMarkerMode_]);
                  SendErasePoseMarker();
                  break;
                }
                
                bool useManualSpeed = (modifier_key & webots::Supervisor::KEYBOARD_ALT);
                  
                if (poseMarkerMode_ == 0) {
                  // Execute path to pose
                  printf("Going to pose marker at x=%f y=%f angle=%f (useManualSpeed: %d)\n",
                         poseMarkerPose_.GetTranslation().x(),
                         poseMarkerPose_.GetTranslation().y(),
                         poseMarkerPose_.GetRotationAngle<'Z'>().ToFloat(),
                         useManualSpeed);

                  SendExecutePathToPose(poseMarkerPose_, pathMotionProfile_, useManualSpeed);
                  //SendMoveHeadToAngle(-0.26, headSpeed, headAccel);
                } else {
                  
                  // Indicate whether or not to place object at the exact rotation specified or
                  // just use the nearest preActionPose so that it's merely aligned with the specified pose.
                  printf("Setting block on ground at rotation %f rads about z-axis (%s)\n", poseMarkerPose_.GetRotationAngle<'Z'>().ToFloat(), useExactRotation ? "Using exact rotation" : "Using nearest preActionPose" );
                  
                  SendPlaceObjectOnGroundSequence(poseMarkerPose_,
                                                  pathMotionProfile_,
                                                  useExactRotation,
                                                  useManualSpeed);
                  // Make sure head is tilted down so that it can localize well
                  //SendMoveHeadToAngle(-0.26, headSpeed, headAccel);
                  
                }
                break;
              }
                
              case (s32)'L':
              {

                if( modifier_key & webots::Supervisor::KEYBOARD_SHIFT ) {
                  ExternalInterface::QueueSingleAction msg;
                  msg.robotID = 1;
                  msg.position = QueueActionPosition::NOW;
                  msg.action.Set_searchSideToSide(ExternalInterface::SearchSideToSide(msg.robotID));

                  ExternalInterface::MessageGameToEngine message;
                  message.Set_QueueSingleAction(msg);
                  SendMessage(message);
                }
                else if (modifier_key & webots::Supervisor::KEYBOARD_ALT ) {
                  static bool enableCliffSensor = false;

                  printf("setting enable cliff sensor to %d\n", enableCliffSensor);
                  ExternalInterface::MessageGameToEngine msg;
                  msg.Set_EnableCliffSensor(ExternalInterface::EnableCliffSensor{enableCliffSensor});
                  SendMessage(msg);

                  enableCliffSensor = !enableCliffSensor;
                }
                else {

                  static bool backpackLightsOn = false;
                
                  ExternalInterface::SetBackpackLEDs msg;
                  msg.robotID = 1;
                  for(s32 i=0; i<(s32)LEDId::NUM_BACKPACK_LEDS; ++i)
                  {
                    msg.onColor[i] = 0;
                    msg.offColor[i] = 0;
                    msg.onPeriod_ms[i] = 1000;
                    msg.offPeriod_ms[i] = 2000;
                    msg.transitionOnPeriod_ms[i] = 500;
                    msg.transitionOffPeriod_ms[i] = 500;
                  }
                
                  if(!backpackLightsOn) {
                    // Use red channel to control left and right lights
                    msg.onColor[(uint32_t)LEDId::LED_BACKPACK_RIGHT]  = ::Anki::NamedColors::RED >> 1; // Make right light dimmer
                    msg.onColor[(uint32_t)LEDId::LED_BACKPACK_LEFT]   = ::Anki::NamedColors::RED;
                    msg.onColor[(uint32_t)LEDId::LED_BACKPACK_BACK]   = ::Anki::NamedColors::RED;
                    msg.onColor[(uint32_t)LEDId::LED_BACKPACK_MIDDLE] = ::Anki::NamedColors::CYAN;
                    msg.onColor[(uint32_t)LEDId::LED_BACKPACK_FRONT]  = ::Anki::NamedColors::YELLOW;
                  }
                
                  ExternalInterface::MessageGameToEngine msgWrapper;
                  msgWrapper.Set_SetBackpackLEDs(msg);
                  SendMessage(msgWrapper);

                  backpackLightsOn = !backpackLightsOn;
                }
                
                break;
              }
                
              case (s32)'T':
              {
                const bool shiftPressed = modifier_key & webots::Supervisor::KEYBOARD_SHIFT;
                const bool altPressed   = modifier_key & webots::Supervisor::KEYBOARD_ALT;
                
                if(altPressed && shiftPressed)
                {
                  SendMessage(ExternalInterface::MessageGameToEngine(ExternalInterface::ReadToolCode()));
                } else if(shiftPressed) {
                  static bool trackingObject = false;
                  
                  trackingObject = !trackingObject;

                  if(trackingObject) {
                    const bool headOnly = false;
                    
                    printf("Telling robot to track %sto the currently observed object %d\n",
                           headOnly ? "its head " : "",
                           GetLastObservedObject().id);
                    
                    SendTrackToObject(GetLastObservedObject().id, headOnly);
                  } else {
                    // Disable tracking
                    SendTrackToObject(u32_MAX);
                  }
                  
                } else if(altPressed) {
                  static bool trackingFace = false;
                  
                  trackingFace = !trackingFace;
                  
                  if(trackingFace) {
                    const bool headOnly = false;
                    
                    printf("Telling robot to track %sto the currently observed face %d\n",
                           headOnly ? "its head " : "",
                           (u32)GetLastObservedFaceID());
                    
                    SendTrackToFace((u32)GetLastObservedFaceID(), headOnly);
                  } else {
                    // Disable tracking
                    SendTrackToFace(u32_MAX);
                  }

                } else {
                  SendExecuteTestPlan(pathMotionProfile_);
                }
                break;
              }
                
              case (s32)'.':
              {
                SendSelectNextObject();
                break;
              }
                
              case (s32)',':
              {
                static bool toggle = true;
                printf("Turning headlight %s\n", toggle ? "ON" : "OFF");
                SendSetHeadlight(toggle);
                toggle = !toggle;
                break;
              }
                
              case (s32)'C':
              {
                if(modifier_key & webots::Supervisor::KEYBOARD_SHIFT) {
               
                  // Send whatever animation is specified in the animationToSendName field
                  webots::Field* behaviorNameField = root_->getField("behaviorName");
                  if (behaviorNameField == nullptr) {
                    printf("ERROR: No behaviorName field found in WebotsKeyboardController.proto\n");
                    break;
                  }
                  std::string behaviorName = behaviorNameField->getSFString();
                  if (behaviorName.empty()) {
                    printf("ERROR: behaviorName field is empty\n");
                    break;
                  }
                  
                  // FactoryTest behavior has to start on a charger so we need to wake up the robot first
                  if(behaviorName == "FactoryTest")
                  {
                    SendMessage(ExternalInterface::MessageGameToEngine(ExternalInterface::WakeUp(true)));
                  }
                  
                  SendMessage(ExternalInterface::MessageGameToEngine(
                                ExternalInterface::ActivateBehaviorChooser(BehaviorChooserType::Selection)));

                  if( modifier_key & webots::Supervisor::KEYBOARD_ALT ) {
                    printf("Selecting behavior by NAME: %s\n", behaviorName.c_str());
                    SendMessage(ExternalInterface::MessageGameToEngine(
                                  ExternalInterface::ExecuteBehaviorByName(behaviorName)));
                  }
                  else {
                    printf("Selecting behavior by TYPE: %s\n", behaviorName.c_str());
                    SendMessage(ExternalInterface::MessageGameToEngine(
                                  ExternalInterface::ExecuteBehavior(GetBehaviorType(behaviorName))));
                  }
                }
                else if(modifier_key & webots::Supervisor::KEYBOARD_ALT) {
                  SendClearAllObjects();
                }
                else {
                  // 'c' without SHIFT
                  SendClearAllBlocks();
                }
                break;
              }

              case (s32)'H':
              {

                if( modifier_key & webots::Supervisor::KEYBOARD_SHIFT ||
                    modifier_key & webots::Supervisor::KEYBOARD_ALT)
                {

                  if( modifier_key & webots::Supervisor::KEYBOARD_SHIFT &&
                      modifier_key & webots::Supervisor::KEYBOARD_ALT ) {
                    printf("ERROR: invalid hotkey\n");
                    break;
                  }
                  
                  // Do not use, soon we'll use games and sparks here!

                }
                else {
                  // select behavior chooser
                  webots::Field* behaviorChooserNameField = root_->getField("behaviorChooserName");
                  if (behaviorChooserNameField == nullptr) {
                    printf("ERROR: No behaviorChooserNameField field found in WebotsKeyboardController.proto\n");
                    break;
                  }
                  
                  std::string behaviorChooserName = behaviorChooserNameField->getSFString();
                  if (behaviorChooserName.empty()) {
                    printf("ERROR: behaviorChooserName field is empty\n");
                    break;
                  }
                  
                  BehaviorChooserType chooser = BehaviorChooserTypeFromString(behaviorChooserName);
                  if( chooser == BehaviorChooserType::Count ) {
                    printf("ERROR: could not convert string '%s' to valid behavior chooser type\n",
                           behaviorChooserName.c_str());
                    break;
                  }
                  
                  printf("sending behavior chooser '%s'\n", BehaviorChooserTypeToString(chooser));
                
                  SendMessage(ExternalInterface::MessageGameToEngine(
                                ExternalInterface::ActivateBehaviorChooser(chooser)));
                }
                
                break;
              }

              case (s32)'M':
              {
                const uint32_t tag = root_->getField("nvTag")->getSFInt32();
                const uint32_t numBlobs = root_->getField("nvNumBlobs")->getSFInt32();
                const uint32_t blobLength = root_->getField("nvBlobDataLength")->getSFInt32();
                
                // Shift + Alt + M: Erases specified tag
                if(modifier_key & webots::Supervisor::KEYBOARD_SHIFT &&
                   modifier_key & webots::Supervisor::KEYBOARD_ALT)
                {
                  if(ENABLE_NVSTORAGE_WRITE)
                  {
                    SendNVStorageEraseEntry((NVStorage::NVEntryTag)tag);
                  }
                  else
                  {
                    PRINT_NAMED_INFO("SendNVStorageEraseEntry.Disabled",
                                     "Set ENABLE_NVSTORAGE_WRITE to 1 if you really want to do this!");
                  }
                }
                // Shift + M: Stores random data to tag
                // If tag is a multi-tag, writes numBlobs blobs of random data blobLength long
                // If tag is a single tag, writes 1 blob of random data that is blobLength long
                else if(modifier_key & webots::Supervisor::KEYBOARD_SHIFT)
                {
                  if(ENABLE_NVSTORAGE_WRITE)
                  {
                    Util::RandomGenerator r;
                    
                    for(int i=0;i<numBlobs;i++)
                    {
                      printf("blob data: %d\n", i);
                      uint8_t data[blobLength];
                      for(int i=0;i<blobLength;i++)
                      {
                        int n = r.RandInt(256);
                        printf("%d ", n);
                        data[i] = (uint8_t)n;
                      }
                      printf("\n\n");
                      SendNVStorageWriteEntry((NVStorage::NVEntryTag)tag, data, blobLength, i, numBlobs);
                    }
                  }
                  else
                  {
                    PRINT_NAMED_INFO("SendNVStorageWriteEntry.Disabled",
                                     "Set ENABLE_NVSTORAGE_WRITE to 1 if you really want to do this!");
                  }
                  
                  break;
                }
                // Alt + M: Reads data at tag
                else if(modifier_key & webots::Supervisor::KEYBOARD_ALT)
                {
                  ClearReceivedNVStorageData((NVStorage::NVEntryTag)tag);
                  SendNVStorageReadEntry((NVStorage::NVEntryTag)tag);
                  break;
                }
              
                webots::Field* emotionNameField = root_->getField("emotionName");
                if (emotionNameField == nullptr) {
                  printf("ERROR: No emotionNameField field found in WebotsKeyboardController.proto\n");
                  break;
                }
                
                std::string emotionName = emotionNameField->getSFString();
                if (emotionName.empty()) {
                  printf("ERROR: emotionName field is empty\n");
                  break;
                }
                
                webots::Field* emotionValField = root_->getField("emotionVal");
                if (emotionValField == nullptr) {
                  printf("ERROR: No emotionValField field found in WebotsKeyboardController.proto\n");
                  break;
                }

                float emotionVal = emotionValField->getSFFloat();
                EmotionType emotionType = EmotionTypeFromString(emotionName.c_str());

                SendMessage(ExternalInterface::MessageGameToEngine(
                              ExternalInterface::MoodMessage(1,
                                ExternalInterface::MoodMessageUnion(
                                  ExternalInterface::SetEmotion( emotionType, emotionVal )))));

                break;
              }
                
              case (s32)'P':
              {
                bool usePreDockPose = !(modifier_key & webots::Supervisor::KEYBOARD_SHIFT);
                //bool useManualSpeed = (modifier_key & webots::Supervisor::KEYBOARD_ALT);
                
                // Hijacking ALT key for low placement
                bool useManualSpeed = false;
                bool placeOnGroundAtOffset = (modifier_key & webots::Supervisor::KEYBOARD_ALT);

                f32 placementOffsetX_mm = 0;
                if (placeOnGroundAtOffset) {
                  placementOffsetX_mm = root_->getField("placeOnGroundOffsetX_mm")->getSFFloat();
                }
                
                // Exact rotation to use if useExactRotation == true
                const double* rotVals = root_->getField("exactPlacementRotation")->getSFRotation();
                Rotation3d rot(rotVals[3], {static_cast<f32>(rotVals[0]), static_cast<f32>(rotVals[1]), static_cast<f32>(rotVals[2])} );
                printf("Rotation %f\n", rot.GetAngleAroundZaxis().ToFloat());
                
                if (GetCarryingObjectID() < 0) {
                  // Not carrying anything so pick up!
                  SendPickupSelectedObject(pathMotionProfile_,
                                           usePreDockPose,
                                           useApproachAngle,
                                           approachAngle_rad,
                                           useManualSpeed);
                } else {
                  if (placeOnGroundAtOffset) {
                    SendPlaceRelSelectedObject(pathMotionProfile_,
                                               usePreDockPose,
                                               placementOffsetX_mm,
                                               useApproachAngle,
                                               approachAngle_rad,
                                               useManualSpeed);
                  } else {
                    SendPlaceOnSelectedObject(pathMotionProfile_,
                                              usePreDockPose,
                                              useApproachAngle,
                                              approachAngle_rad,
                                              useManualSpeed);
                  }
                }
                
                
                break;
              }
                
              case (s32)'R':
              {
                bool usePreDockPose = !(modifier_key & webots::Supervisor::KEYBOARD_SHIFT);
                bool useManualSpeed = false;
                
                if (modifier_key & webots::Supervisor::KEYBOARD_ALT) {
                  SendTraverseSelectedObject(pathMotionProfile_,
                                             usePreDockPose,
                                             useManualSpeed);
                } else {
                  SendMountSelectedCharger(pathMotionProfile_,
                                           usePreDockPose,
                                           useManualSpeed);
                }
                break;
              }
                
              case (s32)'W':
              {
                bool usePreDockPose = !(modifier_key & webots::Supervisor::KEYBOARD_SHIFT);
                bool useManualSpeed = false;
                
                if (modifier_key & webots::Supervisor::KEYBOARD_ALT) {
                  SendPopAWheelie(-1,
                                  pathMotionProfile_,
                                  usePreDockPose,
                                  useApproachAngle,
                                  approachAngle_rad,
                                  useManualSpeed);
                } else {
                  SendRollSelectedObject(pathMotionProfile_,
                                         usePreDockPose,
                                         useApproachAngle,
                                         approachAngle_rad,
                                         useManualSpeed);
                }
                
                
                break;
              }

              case (s32)'Q':
              {
                /* Debugging U2G message for drawing quad
                ExternalInterface::VisualizeQuad msg;
                msg.xLowerRight = 250.f;
                msg.yLowerRight = -0250.f;
                msg.zLowerRight = 10.f*static_cast<f32>(std::rand())/static_cast<f32>(RAND_MAX) + 10.f;
                msg.xUpperLeft = -250.f;
                msg.yUpperLeft = 250.f;
                msg.zUpperLeft = 10.f*static_cast<f32>(std::rand())/static_cast<f32>(RAND_MAX) + 10.f;
                msg.xLowerLeft = -250.f;
                msg.yLowerLeft = -250.f;
                msg.zLowerLeft = 10.f*static_cast<f32>(std::rand())/static_cast<f32>(RAND_MAX) + 10.f;
                msg.xUpperRight = 250.f;
                msg.yUpperRight = 250.f;
                msg.zUpperRight = 10.f*static_cast<f32>(std::rand())/static_cast<f32>(RAND_MAX) + 10.f;
                msg.quadID = 998;
                msg.color = ::Anki::NamedColors::ORANGE;
                
                ExternalInterface::MessageGameToEngine msgWrapper;
                msgWrapper.Set_VisualizeQuad(msg);
                msgHandler_.SendMessage(1, msgWrapper);
                */
                
                if (modifier_key & webots::Supervisor::KEYBOARD_SHIFT) {
                  // SHIFT + Q: Cancel everything (paths, animations, docking, etc.)
                  SendAbortAll();
                } else if(modifier_key & webots::Supervisor::KEYBOARD_ALT) {
                  // ALT + Q: Cancel action
                  SendCancelAction();
                } else {
                  // Just Q: Just cancel path
                  SendAbortPath();
                }
                
                break;
              }
                
              case (s32)'K':
              {
                if (root_) {
                  
                  if(modifier_key & webots::Supervisor::KEYBOARD_SHIFT) {
                    f32 steer_k1 = root_->getField("steerK1")->getSFFloat();
                    f32 steer_k2 = root_->getField("steerK2")->getSFFloat();
                    f32 steerDistOffsetCap = root_->getField("steerDistOffsetCap_mm")->getSFFloat();
                    f32 steerAngOffsetCap = root_->getField("steerAngOffsetCap_rad")->getSFFloat();
                    printf("New steering gains: k1 %f, k2 %f, distOffsetCap %f, angOffsetCap %f\n",
                           steer_k1, steer_k2, steerDistOffsetCap, steerAngOffsetCap);
                    SendControllerGains(ControllerChannel::controller_steering, steer_k1, steer_k2, steerDistOffsetCap, steerAngOffsetCap);
                    
                    // Point turn gains
                    f32 kp = root_->getField("pointTurnKp")->getSFFloat();
                    f32 ki = root_->getField("pointTurnKi")->getSFFloat();
                    f32 kd = root_->getField("pointTurnKd")->getSFFloat();
                    f32 maxErrorSum = root_->getField("pointTurnMaxErrorSum")->getSFFloat();
                    printf("New pointTurn gains: kp=%f ki=%f kd=%f maxErrorSum=%f\n", kp, ki, kd, maxErrorSum);
                    SendControllerGains(ControllerChannel::controller_pointTurn, kp, ki, kd, maxErrorSum);
                    
                  } else {
                    
                    // Wheel gains
                    f32 kp = root_->getField("wheelKp")->getSFFloat();
                    f32 ki = root_->getField("wheelKi")->getSFFloat();
                    f32 kd = 0;
                    f32 maxErrorSum = root_->getField("wheelMaxErrorSum")->getSFFloat();
                    printf("New wheel gains: kp=%f ki=%f kd=%f\n", kp, ki, maxErrorSum);
                    SendControllerGains(ControllerChannel::controller_wheel, kp, ki, kd, maxErrorSum);
                    
                    // Head and lift gains
                    kp = root_->getField("headKp")->getSFFloat();
                    ki = root_->getField("headKi")->getSFFloat();
                    kd = root_->getField("headKd")->getSFFloat();
                    maxErrorSum = root_->getField("headMaxErrorSum")->getSFFloat();
                    printf("New head gains: kp=%f ki=%f kd=%f maxErrorSum=%f\n", kp, ki, kd, maxErrorSum);
                    SendControllerGains(ControllerChannel::controller_head, kp, ki, kd, maxErrorSum);
                    
                    kp = root_->getField("liftKp")->getSFFloat();
                    ki = root_->getField("liftKi")->getSFFloat();
                    kd = root_->getField("liftKd")->getSFFloat();
                    maxErrorSum = root_->getField("liftMaxErrorSum")->getSFFloat();
                    printf("New lift gains: kp=%f ki=%f kd=%f maxErrorSum=%f\n", kp, ki, kd, maxErrorSum);
                    SendControllerGains(ControllerChannel::controller_lift, kp, ki, kd, maxErrorSum);
                  }
                } else {
                  printf("No WebotsKeyboardController was found in world\n");
                }
                break;
              }
                
              case (s32)'V':
              {
                if(modifier_key & webots::Supervisor::KEYBOARD_SHIFT) {
                  static bool visionWhileMovingEnabled = false;
                  visionWhileMovingEnabled = !visionWhileMovingEnabled;
                  printf("%s vision while moving.\n", (visionWhileMovingEnabled ? "Enabling" : "Disabling"));
                  ExternalInterface::VisionWhileMoving msg;
                  msg.enable = visionWhileMovingEnabled;
                  ExternalInterface::MessageGameToEngine msgWrapper;
                  msgWrapper.Set_VisionWhileMoving(msg);
                  SendMessage(msgWrapper);
                } else {
                  f32 robotVolume = root_->getField("robotVolume")->getSFFloat();
                  SendSetRobotVolume(robotVolume);
                }
                break;
              }
                
              case (s32)'B':
              {
                if(modifier_key & webots::Supervisor::KEYBOARD_ALT &&
                   modifier_key & webots::Supervisor::KEYBOARD_SHIFT)
                {
                  ExternalInterface::SetAllActiveObjectLEDs msg;
                  static int jsonMsgCtr = 0;
                  Json::Value jsonMsg;
                  Json::Reader reader;
                  std::string jsonFilename("../webotsCtrlGameEngine/SetBlockLights_" + std::to_string(jsonMsgCtr++) + ".json");
                  std::ifstream jsonFile(jsonFilename);
                  
                  if(jsonFile.fail()) {
                    jsonMsgCtr = 0;
                    jsonFilename = "../webotsCtrlGameEngine/SetBlockLights_" + std::to_string(jsonMsgCtr++) + ".json";
                    jsonFile.open(jsonFilename);
                  }
                  
                  printf("Sending message from: %s\n", jsonFilename.c_str());
                  
                  reader.parse(jsonFile, jsonMsg);
                  jsonFile.close();
                  //ExternalInterface::SetActiveObjectLEDs msg(jsonMsg);
                  msg.robotID = 1;
                  msg.makeRelative = MakeRelativeMode::RELATIVE_LED_MODE_OFF;
                  msg.objectID = jsonMsg["objectID"].asUInt();
                  for(s32 iLED = 0; iLED<(s32)ActiveObjectConstants::NUM_CUBE_LEDS; ++iLED) {
                    msg.onColor[iLED]  = jsonMsg["onColor"][iLED].asUInt();
                    msg.offColor[iLED]  = jsonMsg["offColor"][iLED].asUInt();
                    msg.onPeriod_ms[iLED]  = jsonMsg["onPeriod_ms"][iLED].asUInt();
                    msg.offPeriod_ms[iLED]  = jsonMsg["offPeriod_ms"][iLED].asUInt();
                    msg.transitionOnPeriod_ms[iLED]  = jsonMsg["transitionOnPeriod_ms"][iLED].asUInt();
                    msg.transitionOffPeriod_ms[iLED]  = jsonMsg["transitionOffPeriod_ms"][iLED].asUInt();
                  }
                  
                  ExternalInterface::MessageGameToEngine msgWrapper;
                  msgWrapper.Set_SetAllActiveObjectLEDs(msg);
                  SendMessage(msgWrapper);
                }
                else if(GetLastObservedObject().id >= 0 && GetLastObservedObject().isActive)
                {
                  // Proof of concept: cycle colors
                  const s32 NUM_COLORS = 4;
                  const ColorRGBA colorList[NUM_COLORS] = {
                    ::Anki::NamedColors::RED, ::Anki::NamedColors::GREEN, ::Anki::NamedColors::BLUE,
                    ::Anki::NamedColors::BLACK
                  };

                  static s32 colorIndex = 0;

                  ExternalInterface::SetActiveObjectLEDs msg;
                  msg.objectID = GetLastObservedObject().id;
                  msg.robotID = 1;
                  msg.onPeriod_ms = 250;
                  msg.offPeriod_ms = 250;
                  msg.transitionOnPeriod_ms = 500;
                  msg.transitionOffPeriod_ms = 100;
                  msg.turnOffUnspecifiedLEDs = 1;
                  if(modifier_key & webots::Supervisor::KEYBOARD_SHIFT) {
                    printf("Updating active block edge\n");
                    msg.onColor = ::Anki::NamedColors::RED;
                    msg.offColor = ::Anki::NamedColors::BLACK;
                    msg.whichLEDs = WhichCubeLEDs::FRONT;
                    msg.makeRelative = MakeRelativeMode::RELATIVE_LED_MODE_BY_SIDE;
                    msg.relativeToX = GetRobotPose().GetTranslation().x();
                    msg.relativeToY = GetRobotPose().GetTranslation().y();
                    
                  } else if( modifier_key & webots::Supervisor::KEYBOARD_ALT) {
                    static s32 edgeIndex = 0;
                    
                    printf("Turning edge %d new color %d (%x)\n",
                           edgeIndex, colorIndex, u32(colorList[colorIndex]));
                    
                    msg.whichLEDs = (WhichCubeLEDs)(1 << edgeIndex);
                    msg.onColor = colorList[colorIndex];
                    msg.offColor = 0;
                    msg.turnOffUnspecifiedLEDs = 0;
                    msg.makeRelative = MakeRelativeMode::RELATIVE_LED_MODE_BY_SIDE;
                    msg.relativeToX = GetRobotPose().GetTranslation().x();
                    msg.relativeToY = GetRobotPose().GetTranslation().y();
                    
                    ++edgeIndex;
                    if(edgeIndex == (s32)ActiveObjectConstants::NUM_CUBE_LEDS) {
                      edgeIndex = 0;
                      ++colorIndex;
                    }
                    
                  } else {
                    printf("Cycling active block %d color from (%d,%d,%d) to (%d,%d,%d)\n",
                           msg.objectID,
                           colorList[colorIndex==0 ? NUM_COLORS-1 : colorIndex-1].r(),
                           colorList[colorIndex==0 ? NUM_COLORS-1 : colorIndex-1].g(),
                           colorList[colorIndex==0 ? NUM_COLORS-1 : colorIndex-1].b(),
                           colorList[colorIndex].r(),
                           colorList[colorIndex].g(),
                           colorList[colorIndex].b());
                    msg.onColor = colorList[colorIndex++];
                    msg.offColor = ::Anki::NamedColors::BLACK;
                    msg.whichLEDs = WhichCubeLEDs::FRONT;
                    msg.makeRelative = MakeRelativeMode::RELATIVE_LED_MODE_OFF;
                    msg.turnOffUnspecifiedLEDs = 1;
                    
                    
/*
                    static bool white = false;
                    white = !white;
                    if (white) {
                      ExternalInterface::SetAllActiveObjectLEDs m;
                      m.robotID = 1;
                      m.makeRelative = MakeRelativeMode::RELATIVE_LED_MODE_OFF;
                      m.objectID = GetLastObservedObject().id;
                      for(s32 iLED = 0; iLED<(s32)ActiveObjectConstants::NUM_CUBE_LEDS; ++iLED) {
                        m.onColor[iLED]  = ::Anki::NamedColors::WHITE;
                        m.offColor[iLED]  = ::Anki::NamedColors::BLACK;
                        m.onPeriod_ms[iLED] = 250;
                        m.offPeriod_ms[iLED] = 250;
                        m.transitionOnPeriod_ms[iLED] = 500;
                        m.transitionOffPeriod_ms[iLED] = 100;
                      }
                      ExternalInterface::MessageGameToEngine msgWrapper;
                      msgWrapper.Set_SetAllActiveObjectLEDs(m);
                      SendMessage(msgWrapper);
                      break;
                    } else {
                      msg.onColor = ::Anki::NamedColors::RED;
                      msg.offColor = ::Anki::NamedColors::BLACK;
                      msg.whichLEDs = WhichCubeLEDs::FRONT;
                      msg.makeRelative = MakeRelativeMode::RELATIVE_LED_MODE_OFF;
                      msg.turnOffUnspecifiedLEDs = 0;
                      ExternalInterface::MessageGameToEngine msgWrapper;
                      msgWrapper.Set_SetActiveObjectLEDs(msg);
                      SendMessage(msgWrapper);

                      msg.onColor = ::Anki::NamedColors::GREEN;
                      msg.offColor = ::Anki::NamedColors::BLACK;
                      msg.whichLEDs = WhichCubeLEDs::RIGHT;
                      msg.makeRelative = MakeRelativeMode::RELATIVE_LED_MODE_OFF;
                      msg.turnOffUnspecifiedLEDs = 0;
                      msgWrapper.Set_SetActiveObjectLEDs(msg);
                      SendMessage(msgWrapper);
                      
                      msg.onColor = ::Anki::NamedColors::BLUE;
                      msg.offColor = ::Anki::NamedColors::BLACK;
                      msg.whichLEDs = WhichCubeLEDs::BACK;
                      msg.makeRelative = MakeRelativeMode::RELATIVE_LED_MODE_OFF;
                      msg.turnOffUnspecifiedLEDs = 0;
                      msgWrapper.Set_SetActiveObjectLEDs(msg);
                      SendMessage(msgWrapper);

                      msg.onColor = ::Anki::NamedColors::YELLOW;
                      msg.offColor = ::Anki::NamedColors::BLACK;
                      msg.whichLEDs = WhichCubeLEDs::LEFT;
                      msg.makeRelative = MakeRelativeMode::RELATIVE_LED_MODE_OFF;
                      msg.turnOffUnspecifiedLEDs = 0;
                      msgWrapper.Set_SetActiveObjectLEDs(msg);
                      SendMessage(msgWrapper);
                    }
*/

                  }
                  
                  if(colorIndex == NUM_COLORS) {
                    colorIndex = 0;
                  }
                  
                  ExternalInterface::MessageGameToEngine msgWrapper;
                  msgWrapper.Set_SetActiveObjectLEDs(msg);
                  SendMessage(msgWrapper);
                }
                break;
              }
                
              case (s32)'O':
              {
                if (modifier_key & webots::Supervisor::KEYBOARD_SHIFT && modifier_key & webots::Supervisor::KEYBOARD_ALT) {
                  f32 distToMarker = root_->getField("alignWithObjectDistToMarker_mm")->getSFFloat();
                  SendAlignWithObject(-1, // tell game to use blockworld's "selected" object
                                      distToMarker,
                                      pathMotionProfile_,
                                      true,
                                      useApproachAngle,
                                      approachAngle_rad);
                  
                } else if(modifier_key & webots::Supervisor::KEYBOARD_SHIFT) {
                  ExternalInterface::TurnTowardsObject msg;
                  msg.robotID = 1;
                  msg.objectID = u32_MAX; // HACK to tell game to use blockworld's "selected" object
                  msg.panTolerance_rad = DEG_TO_RAD(5);
                  msg.maxTurnAngle = DEG_TO_RAD(90);
                  msg.headTrackWhenDone = 0;
                  
                  ExternalInterface::MessageGameToEngine msgWrapper;
                  msgWrapper.Set_TurnTowardsObject(msg);
                  SendMessage(msgWrapper);
                } else if (modifier_key & webots::Supervisor::KEYBOARD_ALT) {
                  SendGotoObject(-1, // tell game to use blockworld's "selected" object
                                 sqrtf(2.f)*44.f,
                                 pathMotionProfile_);
                } else {
                  SendIMURequest(2000);
                }
                break;
              }
                
              case (s32)'`':
              {
                printf("Updating viz origin\n");
                UpdateVizOrigin();
                break;
              }
                
              case (s32) '!':
              {
                webots::Field* factoryIDs = root_->getField("activeObjectFactoryIDs");
                webots::Field* connect = root_->getField("activeObjectConnect");
                
                if (factoryIDs && connect) {
                  ExternalInterface::BlockSelectedMessage msg;
                  for (int i=0; i<factoryIDs->getCount(); ++i) {
                    msg.factoryId = static_cast<u32>(strtol(factoryIDs->getMFString(i).c_str(), nullptr, 16));
                    msg.selected = connect->getSFBool();
                    
                    if (msg.factoryId == 0) {
                      continue;
                    }
                    
                    PRINT_NAMED_INFO("BlockSelected", "factoryID 0x%x, connect %d", msg.factoryId, msg.selected);
                    ExternalInterface::MessageGameToEngine msgWrapper;
                    msgWrapper.Set_BlockSelectedMessage(msg);
                    SendMessage(msgWrapper);
                  }
                }
                break;
              }

              case (s32)'@':
              {
                static bool enable = true;
                ExternalInterface::SendAvailableObjects msg;
                msg.robotID = 1;
                msg.enable = enable;
                
                PRINT_NAMED_INFO("SendAvailableObjects", "enable: %d", enable);
                ExternalInterface::MessageGameToEngine msgWrapper;
                msgWrapper.Set_SendAvailableObjects(msg);
                SendMessage(msgWrapper);
                
                enable = !enable;
                break;
              }
              case (s32)'#':
              {
                SendQueuePlayAnimAction("ANIM_TEST", 1, QueueActionPosition::NEXT);
                SendQueuePlayAnimAction("ANIM_TEST", 1, QueueActionPosition::NEXT);
                
                //SendAnimation("ANIM_BLINK", 0);
                break;
              }
              case (s32)'$':
              {
                if(modifier_key & webots::Supervisor::KEYBOARD_ALT) {
                  SendClearCalibrationImages();
                } else {
                  SendSaveCalibrationImage();
                }
                break;
              }
              case (s32)'%':
              {
                SendComputeCameraCalibration();
                break;
              }
              case (s32)'&':
              {
                if(modifier_key & webots::Supervisor::KEYBOARD_ALT) {
                  PRINT_NAMED_INFO("SendNVStorageReadEntry", "NVEntry_CameraCalib");
                  ClearReceivedNVStorageData(NVStorage::NVEntryTag::NVEntry_CameraCalib);
                  SendNVStorageReadEntry(NVStorage::NVEntryTag::NVEntry_CameraCalib);
                } else {
                  
                  if (ENABLE_NVSTORAGE_WRITE) {
                    // Toggle write/erase
                    static bool writeNotErase = true;
                    if (writeNotErase) {
                      f32 focalLength_x = root_->getField("focalLength_x")->getSFFloat();
                      f32 focalLength_y = root_->getField("focalLength_y")->getSFFloat();
                      f32 center_x = root_->getField("imageCenter_x")->getSFFloat();
                      f32 center_y = root_->getField("imageCenter_y")->getSFFloat();
                      PRINT_NAMED_INFO("SendCameraCalibrationraseEntry",
                                       "fx: %f, fy: %f, cx: %f, cy: %f",
                                       focalLength_x, focalLength_y, center_x, center_y);

                      // Method 1
                      //SendCameraCalibration(focalLength_x, focalLength_y, center_x, center_y);
                      
                      // Method 2
                      CameraCalibration calib(focalLength_x, focalLength_y,
                                              center_x, center_y,
                                              0, 240, 320, {});
                      std::vector<u8> calibVec(calib.Size());
                      calib.Pack(calibVec.data(), calib.Size());
                      SendNVStorageWriteEntry(NVStorage::NVEntryTag::NVEntry_CameraCalib,
                                              calibVec.data(), calibVec.size(),
                                              0, 1);
                    } else {
                      PRINT_NAMED_INFO("SendNVStorageEraseEntry", "NVEntry_CameraCalib");
                      SendNVStorageEraseEntry(NVStorage::NVEntryTag::NVEntry_CameraCalib);
                    }
                    writeNotErase = !writeNotErase;
                    
                  } else {
                    PRINT_NAMED_INFO("SendNVStorageWriteEntry.CameraCalibration.Disabled",
                                     "Set ENABLE_NVSTORAGE_WRITE to 1 if you really want to do this!");
                  }
                  
                }
                break;
              }
              case (s32)'(':
              {
                NVStorage::NVEntryTag tag = NVStorage::NVEntryTag::NVEntry_MultiBlobJunk;
                
                // NVStorage multiWrite / multiRead test
                if(modifier_key & webots::Supervisor::KEYBOARD_ALT) {
                  PRINT_NAMED_INFO("SendNVStorageReadEntry", "Putting image in %s", EnumToString(tag));
                  ClearReceivedNVStorageData(tag);
                  SendNVStorageReadEntry(tag);
                } else {
                  
                  if (ENABLE_NVSTORAGE_WRITE) {
                    // Toggle write/erase
                    static bool writeNotErase = true;
                    if (writeNotErase) {
                      static const char* inFile = "nvstorage_input.jpg";
                      FILE* fp = fopen(inFile, "rb");
                      if (fp) {
                        std::vector<u8> d(30000);
                        size_t numBytes = fread(d.data(), 1, d.size(), fp);
                        d.resize(numBytes);
                        PRINT_NAMED_INFO("SendNVStorageWriteEntry.ReadInputImage", "Tag: %s, read %zu bytes\n", EnumToString(tag), numBytes);
                        
                        ExternalInterface::NVStorageWriteEntry temp;
                        u32 MAX_BLOB_SIZE = temp.data.size();
                        u8 numTotalBlobs = static_cast<u8>(ceilf(static_cast<f32>(numBytes) / MAX_BLOB_SIZE));
                        
                        PRINT_NAMED_INFO("SendNVStorageWriteEntry.Sending",
                                         "Tag: %s, NumBlobs %d, maxBlobSize %d",
                                         EnumToString(tag), numTotalBlobs, MAX_BLOB_SIZE);

                        for (int i=0; i<numTotalBlobs; ++i) {
                          SendNVStorageWriteEntry(tag,
                                                  d.data() + i * MAX_BLOB_SIZE, MIN(MAX_BLOB_SIZE, numBytes - (i*MAX_BLOB_SIZE)),
                                                  i, numTotalBlobs);
                        }
                      } else {
                        printf("%s open failed\n", inFile);
                      }
                    } else {
                      
                      PRINT_NAMED_INFO("SendNVStorageEraseEntry", "%s", EnumToString(tag));
                      SendNVStorageEraseEntry(tag);
                    }
                    writeNotErase = !writeNotErase;
                  } else {
                    PRINT_NAMED_INFO("SendNVStorageWriteEntry.Disabled",
                                     "Set ENABLE_NVSTORAGE_WRITE to 1 if you really want to do this! (Tag: %s)", EnumToString(tag));
                  }
                  
                }
                break;
              }
              case (s32)')':
              {
                PRINT_NAMED_INFO("RetrievingAllMfgTestData", "...");
                
                // Get all Mfg test images and results
                SendNVStorageReadEntry(NVStorage::NVEntryTag::NVEntry_PlaypenTestResults);
                SendNVStorageReadEntry(NVStorage::NVEntryTag::NVEntry_CameraCalib);
                SendNVStorageReadEntry(NVStorage::NVEntryTag::NVEntry_CalibPose);
                SendNVStorageReadEntry(NVStorage::NVEntryTag::NVEntry_ToolCodeInfo);
                SendNVStorageReadEntry(NVStorage::NVEntryTag::NVEntry_ObservedCubePose);
                
                if(modifier_key & webots::Supervisor::KEYBOARD_ALT) {
                  SendNVStorageReadEntry(NVStorage::NVEntryTag::NVEntry_CalibImage1);
                  SendNVStorageReadEntry(NVStorage::NVEntryTag::NVEntry_CalibImage2);
                  SendNVStorageReadEntry(NVStorage::NVEntryTag::NVEntry_CalibImage3);
                  SendNVStorageReadEntry(NVStorage::NVEntryTag::NVEntry_CalibImage4);
                  SendNVStorageReadEntry(NVStorage::NVEntryTag::NVEntry_CalibImage5);
                  SendNVStorageReadEntry(NVStorage::NVEntryTag::NVEntry_CalibImage6);
                  
                  SendNVStorageReadEntry(NVStorage::NVEntryTag::NVEntry_ToolCodeImageLeft);
                  SendNVStorageReadEntry(NVStorage::NVEntryTag::NVEntry_ToolCodeImageRight);
                }
                
                // Set mfg save folder and file
                auto time_point = std::chrono::system_clock::now();
                time_t nowTime = std::chrono::system_clock::to_time_t(time_point);
                auto nowLocalTime = localtime(&nowTime);
                char buf[80];
                strftime(buf, sizeof(buf), "%F_%H-%M-%S/", nowLocalTime);
                
                _mfgDataSaveFolder = buf;
                Util::FileUtils::CreateDirectory(_mfgDataSaveFolder);
                _mfgDataSaveFile = _mfgDataSaveFolder + "mfgData.txt";
                printf("MFG FILE: %s", _mfgDataSaveFile.c_str());
                break;
              }
              case (s32)'*':
              {
                using namespace ExternalInterface;
                using Param = ProceduralEyeParameter;
                DisplayProceduralFace msg;
                msg.robotID = 1;
                msg.leftEye.resize(static_cast<size_t>(Param::NumParameters),  0);
                msg.rightEye.resize(static_cast<size_t>(Param::NumParameters), 0);
                
                if(modifier_key & webots::Supervisor::KEYBOARD_ALT) {
                  // Reset to base face
                  msg.leftEye[static_cast<s32>(Param::EyeCenterX)]  = 32;
                  msg.leftEye[static_cast<s32>(Param::EyeCenterY)]  = 32;
                  msg.rightEye[static_cast<s32>(Param::EyeCenterX)] = 96;
                  msg.rightEye[static_cast<s32>(Param::EyeCenterY)] = 32;
                  
                  msg.leftEye[static_cast<s32>(Param::EyeScaleX)] = 1.f;
                  msg.leftEye[static_cast<s32>(Param::EyeScaleY)] = 1.f;
                  msg.rightEye[static_cast<s32>(Param::EyeScaleX)] = 1.f;
                  msg.rightEye[static_cast<s32>(Param::EyeScaleY)] = 1.f;
                  
                  for(auto radiusParam : {Param::UpperInnerRadiusX, Param::UpperInnerRadiusY,
                    Param::UpperOuterRadiusX, Param::UpperOuterRadiusY,
                    Param::LowerInnerRadiusX, Param::LowerInnerRadiusY,
                    Param::LowerOuterRadiusX, Param::LowerOuterRadiusY})
                  {
                    const s32 radiusIndex = static_cast<s32>(radiusParam);
                    msg.leftEye[radiusIndex]  = 0.25f;
                    msg.rightEye[radiusIndex] = 0.25f;
                  }
                  
                  msg.faceAngle = 0;
                  msg.faceScaleX = 1.f;
                  msg.faceScaleY = 1.f;
                  msg.faceCenX  = 0;
                  msg.faceCenY  = 0;
                  
                } else {
                  // Send a random procedural face
                  // NOTE: idle animatino should be set to _LIVE_ or _ANIM_TOOL_ first.
                  Util::RandomGenerator rng;
                  
                  msg.leftEye[static_cast<s32>(Param::UpperInnerRadiusX)]   = rng.RandDblInRange(0., 1.);
                  msg.leftEye[static_cast<s32>(Param::UpperInnerRadiusY)]   = rng.RandDblInRange(0., 1.);
                  msg.leftEye[static_cast<s32>(Param::LowerInnerRadiusX)]   = rng.RandDblInRange(0., 1.);
                  msg.leftEye[static_cast<s32>(Param::LowerInnerRadiusY)]   = rng.RandDblInRange(0., 1.);
                  msg.leftEye[static_cast<s32>(Param::UpperOuterRadiusX)]   = rng.RandDblInRange(0., 1.);
                  msg.leftEye[static_cast<s32>(Param::UpperOuterRadiusY)]   = rng.RandDblInRange(0., 1.);
                  msg.leftEye[static_cast<s32>(Param::LowerOuterRadiusX)]   = rng.RandDblInRange(0., 1.);
                  msg.leftEye[static_cast<s32>(Param::LowerOuterRadiusY)]   = rng.RandDblInRange(0., 1.);
                  msg.leftEye[static_cast<s32>(Param::EyeCenterX)]    = rng.RandIntInRange(24,40);
                  msg.leftEye[static_cast<s32>(Param::EyeCenterY)]    = rng.RandIntInRange(28,36);
                  msg.leftEye[static_cast<s32>(Param::EyeScaleX)]     = 1.f;
                  msg.leftEye[static_cast<s32>(Param::EyeScaleY)]     = 1.f;
                  msg.leftEye[static_cast<s32>(Param::EyeAngle)]      = 0;//rng.RandIntInRange(-10,10);
                  msg.leftEye[static_cast<s32>(Param::LowerLidY)]     = rng.RandDblInRange(0., .25);
                  msg.leftEye[static_cast<s32>(Param::LowerLidAngle)] = rng.RandIntInRange(-20, 20);
                  msg.leftEye[static_cast<s32>(Param::LowerLidBend)]  = 0;//rng.RandDblInRange(0, 0.2);
                  msg.leftEye[static_cast<s32>(Param::UpperLidY)]     = rng.RandDblInRange(0., .25);
                  msg.leftEye[static_cast<s32>(Param::UpperLidAngle)] = rng.RandIntInRange(-20, 20);
                  msg.leftEye[static_cast<s32>(Param::UpperLidBend)]  = 0;//rng.RandDblInRange(0, 0.2);
                  
                  msg.rightEye[static_cast<s32>(Param::UpperInnerRadiusX)]   = rng.RandDblInRange(0., 1.);
                  msg.rightEye[static_cast<s32>(Param::UpperInnerRadiusY)]   = rng.RandDblInRange(0., 1.);
                  msg.rightEye[static_cast<s32>(Param::LowerInnerRadiusX)]   = rng.RandDblInRange(0., 1.);
                  msg.rightEye[static_cast<s32>(Param::LowerInnerRadiusY)]   = rng.RandDblInRange(0., 1.);
                  msg.rightEye[static_cast<s32>(Param::UpperOuterRadiusX)]   = rng.RandDblInRange(0., 1.);
                  msg.rightEye[static_cast<s32>(Param::UpperOuterRadiusY)]   = rng.RandDblInRange(0., 1.);
                  msg.rightEye[static_cast<s32>(Param::LowerOuterRadiusX)]   = rng.RandDblInRange(0., 1.);
                  msg.rightEye[static_cast<s32>(Param::LowerOuterRadiusY)]   = rng.RandDblInRange(0., 1.);
                  msg.rightEye[static_cast<s32>(Param::EyeCenterX)]    = rng.RandIntInRange(88,104);
                  msg.rightEye[static_cast<s32>(Param::EyeCenterY)]    = rng.RandIntInRange(28,36);
                  msg.rightEye[static_cast<s32>(Param::EyeScaleX)]     = rng.RandDblInRange(0.8, 1.2);
                  msg.rightEye[static_cast<s32>(Param::EyeScaleY)]     = rng.RandDblInRange(0.8, 1.2);
                  msg.rightEye[static_cast<s32>(Param::EyeAngle)]      = 0;//rng.RandIntInRange(-15,15);
                  msg.rightEye[static_cast<s32>(Param::LowerLidY)]     = rng.RandDblInRange(0., .25);
                  msg.rightEye[static_cast<s32>(Param::LowerLidAngle)] = rng.RandIntInRange(-20, 20);
                  msg.rightEye[static_cast<s32>(Param::LowerLidBend)]  = rng.RandDblInRange(0., 0.2);
                  msg.rightEye[static_cast<s32>(Param::UpperLidY)]     = rng.RandDblInRange(0., .25);
                  msg.rightEye[static_cast<s32>(Param::UpperLidAngle)] = rng.RandIntInRange(-20, 20);
                  msg.rightEye[static_cast<s32>(Param::UpperLidBend)]  = rng.RandDblInRange(0, 0.2);
                  
                  msg.faceAngle = 0; //rng.RandIntInRange(-10, 10);
                  msg.faceScaleX = 1.f;//rng.RandDblInRange(0.9, 1.1);
                  msg.faceScaleY = 1.f;//rng.RandDblInRange(0.9, 1.1);
                  msg.faceCenX  = 0; //rng.RandIntInRange(-5, 5);
                  msg.faceCenY  = 0; //rng.RandIntInRange(-5, 5);
                }
                
                SendMessage(MessageGameToEngine(std::move(msg)));

                break;
              }
              case (s32)'^':
              {
                if(modifier_key & webots::Supervisor::KEYBOARD_ALT)
                {
                  webots::Field* idleAnimToSendField = root_->getField("idleAnimationName");
                  if(idleAnimToSendField == nullptr) {
                    printf("ERROR: No idleAnimationName field found in WebotsKeyboardController.proto\n");
                    break;
                  }
                  std::string idleAnimToSendName = idleAnimToSendField->getSFString();
                  
                  using namespace ExternalInterface;
                  if(idleAnimToSendName.empty()) {
                    SendMessage(MessageGameToEngine(PopIdleAnimation()));
                  } else {
                    SendMessage(MessageGameToEngine(PushIdleAnimation(idleAnimToSendName)));
                  }

                }
                else {
                  // Send whatever animation is specified in the animationToSendName field
                  webots::Field* animToSendNameField = root_->getField("animationToSendName");
                  if (animToSendNameField == nullptr) {
                    printf("ERROR: No animationToSendName field found in WebotsKeyboardController.proto\n");
                    break;
                  }
                  std::string animToSendName = animToSendNameField->getSFString();
                  if (animToSendName.empty()) {
                    printf("ERROR: animationToSendName field is empty\n");
                    break;
                  }
                  
                  webots::Field* animNumLoopsField = root_->getField("animationNumLoops");
                  u32 animNumLoops = 1;
                  if (animNumLoopsField && (animNumLoopsField->getSFInt32() > 0)) {
                    animNumLoops = animNumLoopsField->getSFInt32();
                  }
                  
                  SendAnimation(animToSendName.c_str(), animNumLoops);
                }
                break;
              }
              case (s32)'~':
              {
                // Send whatever animation is specified in the animationToSendName field
                webots::Field* animToSendNameField = root_->getField("animationToSendName");
                if (animToSendNameField == nullptr) {
                  printf("ERROR: No animationToSendName field found in WebotsKeyboardController.proto\n");
                  break;
                }
                std::string animToSendName = animToSendNameField->getSFString();
                if (animToSendName.empty()) {
                  printf("ERROR: animationToSendName field is empty\n");
                  break;
                }
                SendAnimationGroup(animToSendName.c_str());
                break;
              }
                
              case (s32)'/':
              {
                PrintHelp();
                break;
              }
                
              case (s32)']':
              {
                // Set console variable
                webots::Field* field = root_->getField("consoleVarName");
                if(nullptr == field) {
                  printf("No consoleVarName field\n");
                } else {
                  ExternalInterface::SetDebugConsoleVarMessage msg;
                  msg.varName = field->getSFString();
                  if(msg.varName.empty()) {
                    printf("Empty consoleVarName\n");
                  } else {
                    field = root_->getField("consoleVarValue");
                    if(nullptr == field) {
                      printf("No consoleVarValue field\n");
                    } else {
                      msg.tryValue = field->getSFString();
                      printf("Trying to set console var '%s' to '%s'\n",
                             msg.varName.c_str(), msg.tryValue.c_str());
                      SendMessage(ExternalInterface::MessageGameToEngine(std::move(msg)));
                    }
                  }
                }
                break;
              }
                
              case (s32)'F':
              {
                const bool shiftPressed = modifier_key & webots::Supervisor::KEYBOARD_SHIFT;
                const bool altPressed   = modifier_key & webots::Supervisor::KEYBOARD_ALT;
                if (shiftPressed && !altPressed) {
                  // SHIFT+F: Associate name with current face
                  webots::Field* userNameField = root_->getField("userName");
                  if(nullptr != userNameField)
                  {
                    std::string userName = userNameField->getSFString();
                    if(!userName.empty())
                    {
//                      printf("Assigning name '%s' to ID %d\n", userName.c_str(), GetLastObservedFaceID());
//                      ExternalInterface::AssignNameToFace assignNameToFace;
//                      assignNameToFace.faceID = GetLastObservedFaceID();
//                      assignNameToFace.name   = userName;
//                      SendMessage(ExternalInterface::MessageGameToEngine(std::move(assignNameToFace)));
                      printf("Enrolling face ID %d with name '%s'\n", GetLastObservedFaceID(), userName.c_str());
                      ExternalInterface::EnrollNamedFace enrollNamedFace;
                      enrollNamedFace.faceID   = GetLastObservedFaceID();
                      enrollNamedFace.name     = userName;
                      enrollNamedFace.sequence = FaceEnrollmentSequence::Simple;
                      enrollNamedFace.saveToRobot = false; // for testing it's nice not to save
                      SendMessage(ExternalInterface::MessageGameToEngine(std::move(enrollNamedFace)));
                    } else {
                      // No user name, enable enrollment
                      ExternalInterface::SetFaceEnrollmentPose setEnrollmentPose;
                      setEnrollmentPose.pose = Vision::FaceEnrollmentPose::LookingStraight;
                      printf("Enabling enrollment of next face\n");
                      SendMessage(ExternalInterface::MessageGameToEngine(std::move(setEnrollmentPose)));
                    }
                    
                  } else {
                    printf("No 'userName' field\n");
                  }
                  
                } else if(altPressed && !shiftPressed) {
                  // ALT+F: Turn to face the pose of the last observed face:
                  printf("Turning to last face\n");
                  ExternalInterface::TurnTowardsLastFacePose turnTowardsPose; // construct w/ defaults for speed
                  turnTowardsPose.panTolerance_rad = DEG_TO_RAD(10);
                  turnTowardsPose.maxTurnAngle = M_PI;
                  turnTowardsPose.robotID = 1;
                  turnTowardsPose.sayName = true;
                  SendMessage(ExternalInterface::MessageGameToEngine(std::move(turnTowardsPose)));
                } else if(altPressed && shiftPressed) {
                  // SHIFT+ALT+F: Erase current face
                  using namespace ExternalInterface;
                  SendMessage(MessageGameToEngine(EraseEnrolledFaceByID(GetLastObservedFaceID())));
                } else {
                  // Just F: Toggle face detection
                  static bool isFaceDetectionEnabled = true;
                  isFaceDetectionEnabled = !isFaceDetectionEnabled;
                  SendEnableVisionMode(VisionMode::DetectingFaces, isFaceDetectionEnabled);
                }
                break;
              }
                
              case (s32)'J':
              {

                using namespace ExternalInterface;

                if( modifier_key & webots::Supervisor::KEYBOARD_SHIFT ) {
                
                  webots::Field* hasEdgeField = root_->getField("demoHasEdge");
                  if( hasEdgeField != nullptr ) {
                    bool hasEdge = hasEdgeField->getSFBool();
                    SendMessage(MessageGameToEngine(WakeUp(hasEdge)));
                  }
                  else {
                    printf("ERROR: no field 'demoHasEdge', not sending edge message\n");
                  }
                }
                else {
                  SendMessage(MessageGameToEngine(TransitionToNextDemoState()));
                }

                break;
              }

              case (s32)'N':
              {
                if( modifier_key & webots::Supervisor::KEYBOARD_ALT ) {
                  SendMessage(ExternalInterface::MessageGameToEngine(ExternalInterface::DenyGameStart()));
                }
                else {
                  webots::Field* unlockNameField = root_->getField("unlockName");
                  if (unlockNameField == nullptr) {
                    printf("ERROR: No unlockNameField field found in WebotsKeyboardController.proto\n");
                    break;
                  }
                
                  std::string unlockName = unlockNameField->getSFString();
                  if (unlockName.empty()) {
                    printf("ERROR: unlockName field is empty\n");
                    break;
                  }

                  UnlockId unlock = UnlockIdsFromString(unlockName.c_str());
                  bool val = ! ( modifier_key & webots::Supervisor::KEYBOARD_SHIFT );
                  SendMessage( ExternalInterface::MessageGameToEngine(
                                 ExternalInterface::RequestSetUnlock(unlock, val)));

                }
                break;
              }
                
              case (s32)';':
              {
                // Toggle enabling of reactionary behaviors
                static bool enable = false;
                printf("Enable reactionary behaviors: %d\n", enable);
                ExternalInterface::EnableReactionaryBehaviors m;
                m.enabled = enable;
                ExternalInterface::MessageGameToEngine message;
                message.Set_EnableReactionaryBehaviors(m);
                SendMessage(message);
                
                enable = !enable;
                break;
              }
                
              case (s32)'"':
              {
                webots::Field* sayStringField = root_->getField("sayString");
                if(sayStringField == nullptr) {
                  printf("ERROR: No sayString field found in WebotsKeyboardController.proto\n");
                  break;
                }
                
                ExternalInterface::SayText sayTextMsg;
                sayTextMsg.text = sayStringField->getSFString();
                if(sayTextMsg.text.empty()) {
                  printf("ERROR: sayString field is empty\n");
                }
                // TODO: Add ability to set style from KB controller field too
                sayTextMsg.style = SayTextStyle::Name_Normal;
                
                printf("Saying '%s' in style '%s'\n", sayTextMsg.text.c_str(), EnumToString(sayTextMsg.style));
                SendMessage(ExternalInterface::MessageGameToEngine(std::move(sayTextMsg)));
                break;
              }
              
              case (s32)'Y':
              {
                ExternalInterface::FlipBlock m;
                m.objectID = -1;
                m.motionProf = pathMotionProfile_;
                ExternalInterface::MessageGameToEngine message;
                message.Set_FlipBlock(m);
                SendMessage(message);
                break;
              }
            
              default:
              {
                // Unsupported key: ignore.
                break;
              }
                
            } // switch
          } // if/else testMode
          
        } // for(auto key : keysPressed_)
        
        movingWheels = throttleDir || steeringDir;

        if(movingWheels) {
          
          // Set wheel speeds based on drive commands
          if (throttleDir > 0) {
            leftSpeed = wheelSpeed + steeringDir * wheelSpeed * steeringCurvature;
            rightSpeed = wheelSpeed - steeringDir * wheelSpeed * steeringCurvature;
          } else if (throttleDir < 0) {
            leftSpeed = -wheelSpeed - steeringDir * wheelSpeed * steeringCurvature;
            rightSpeed = -wheelSpeed + steeringDir * wheelSpeed * steeringCurvature;
          } else {
            leftSpeed = steeringDir * wheelSpeed;
            rightSpeed = -steeringDir * wheelSpeed;
          }
          
          SendDriveWheels(leftSpeed, rightSpeed, driveAccel, driveAccel);
          wasMovingWheels_ = true;
        } else if(wasMovingWheels_ && !movingWheels) {
          // If we just stopped moving the wheels:
          SendDriveWheels(0, 0, driveAccel, driveAccel);
          wasMovingWheels_ = false;
        }
        
        // If the last key pressed was a move lift key then stop it.
        if(movingLift) {
          SendMoveLift(commandedLiftSpeed);
          wasMovingLift_ = true;
        } else if (wasMovingLift_ && !movingLift) {
          // If we just stopped moving the lift:
          SendMoveLift(0);
          wasMovingLift_ = false;
        }
        
        if(movingHead) {
          SendMoveHead(commandedHeadSpeed);
          wasMovingHead_ = true;
        } else if (wasMovingHead_ && !movingHead) {
          // If we just stopped moving the head:
          SendMoveHead(0);
          wasMovingHead_ = false;
        }
       
        
      } // BSKeyboardController::ProcessKeyStroke()
      
    
    void WebotsKeyboardController::TestLightCube()
      {
        static std::vector<ColorRGBA> colors = {{
          ::Anki::NamedColors::RED, ::Anki::NamedColors::GREEN, ::Anki::NamedColors::BLUE,
          ::Anki::NamedColors::CYAN, ::Anki::NamedColors::ORANGE, ::Anki::NamedColors::YELLOW
        }};
        static std::vector<WhichCubeLEDs> leds = {{
          WhichCubeLEDs::BACK,
          WhichCubeLEDs::LEFT,
          WhichCubeLEDs::FRONT,
          WhichCubeLEDs::RIGHT
        }};
        
        static auto colorIter = colors.begin();
        static auto ledIter = leds.begin();
        static s32 counter = 0;
        
        if(counter++ == 30) {
          counter = 0;
          
          ExternalInterface::SetActiveObjectLEDs msg;
          msg.objectID = GetLastObservedObject().id;
          msg.robotID = 1;
          msg.onPeriod_ms = 100;
          msg.offPeriod_ms = 100;
          msg.transitionOnPeriod_ms = 50;
          msg.transitionOffPeriod_ms = 50;
          msg.turnOffUnspecifiedLEDs = 1;
          msg.onColor = *colorIter;
          msg.offColor = 0;
          msg.whichLEDs = *ledIter;
          msg.makeRelative = MakeRelativeMode::RELATIVE_LED_MODE_OFF;
          
          ++ledIter;
          if(ledIter==leds.end()) {
            ledIter = leds.begin();
            ++colorIter;
            if(colorIter == colors.end()) {
              colorIter = colors.begin();
            }
          }
          
          ExternalInterface::MessageGameToEngine message;
          message.Set_SetActiveObjectLEDs(msg);
          SendMessage(message);
        }
      } // TestLightCube()
    
            
      s32 WebotsKeyboardController::UpdateInternal()
      {
        // Get poseMarker pose
        const double* trans = root_->getPosition();
        const double* rot = root_->getOrientation();
        poseMarkerPose_.SetTranslation({1000*static_cast<f32>(trans[0]),
                                        1000*static_cast<f32>(trans[1]),
                                        1000*static_cast<f32>(trans[2])});
        poseMarkerPose_.SetRotation({static_cast<f32>(rot[0]), static_cast<f32>(rot[1]), static_cast<f32>(rot[2]),
                                     static_cast<f32>(rot[3]), static_cast<f32>(rot[4]), static_cast<f32>(rot[5]),
                                     static_cast<f32>(rot[6]), static_cast<f32>(rot[7]), static_cast<f32>(rot[8])} );
        
        // Update pose marker if different from last time
        if (!(prevPoseMarkerPose_ == poseMarkerPose_)) {
          if (poseMarkerMode_ != 0) {
            // Place object mode
            SendDrawPoseMarker(poseMarkerPose_);
          }
        }

        
        ProcessKeystroke();

        if( _shouldQuit ) {
          return 1;
        }
        else {        
          return 0;
        }
      }
    
      void WebotsKeyboardController::HandleNVStorageData(const ExternalInterface::NVStorageData &msg)
      {
        // Could handle single-blob reads here, but for consistency all reads are handled upon
        // receipt of NVStorageOpResult message below.
      }
    
      void AppendToFile(const std::string& filename, const std::string& data) {
        auto contents = Util::FileUtils::ReadFile(_mfgDataSaveFile);
        contents = contents + '\n' + data;
        Util::FileUtils::WriteFile(_mfgDataSaveFile, contents);
      }
    
      void WebotsKeyboardController::HandleNVStorageOpResult(const ExternalInterface::NVStorageOpResult &msg)
      {
        if (msg.op != NVStorage::NVOperation::NVOP_READ) {
          // Do nothing for write/erase acks
        } else {

          // Check result flag
          if (msg.result != NVStorage::NVResult::NV_OKAY) {
            PRINT_NAMED_WARNING("HandleNVStorageOpResult.Read.Failed",
                                "tag: %s, res: %s",
                                EnumToString(msg.tag),
                                EnumToString(msg.result));
            return;
          }
          
          const std::vector<u8>* recvdData = GetReceivedNVStorageData(msg.tag);
          if (recvdData == nullptr) {
            PRINT_NAMED_INFO("HandleNVStorageOpResult.Read.NoDataReceived", "Tag: %s", EnumToString(msg.tag));
            return;
          }
          
          switch(msg.tag) {
            case NVStorage::NVEntryTag::NVEntry_CameraCalib:
            {
              CameraCalibration calib;
              if (recvdData->size() != MakeWordAligned(calib.Size())) {
                PRINT_NAMED_INFO("HandleNVStorageOpResult.CamCalibration.UnexpectedSize",
                                 "Expected %zu, got %zu", MakeWordAligned(calib.Size()), recvdData->size());
                break;
              }
              calib.Unpack(recvdData->data(), calib.Size());
              
              char buf[256];
              snprintf(buf, sizeof(buf),
                       "[CameraCalibration]\nfx: %f\nfy: %f\ncx: %f\ncy: %f\nskew: %f\nnrows: %d\nncols: %d\n",
                      calib.focalLength_x, calib.focalLength_y,
                      calib.center_x, calib.center_y,
                      calib.skew,
                      calib.nrows, calib.ncols);
              
              PRINT_NAMED_INFO("HandleNVStorageOpResult.CamCalibration", "%s", buf);

              AppendToFile(_mfgDataSaveFile, buf);
              break;
            }
            case NVStorage::NVEntryTag::NVEntry_ToolCodeInfo:
            {
              ToolCodeInfo info;
              if (recvdData->size() != MakeWordAligned(info.Size())) {
                PRINT_NAMED_INFO("HandleNVStorageOpResult.ToolCodeInfo.UnexpectedSize",
                                 "Expected %zu, got %zu", MakeWordAligned(info.Size()), recvdData->size());
                break;
              }
              info.Unpack(recvdData->data(), info.Size());
              
              char buf[256];
              snprintf(buf, sizeof(buf),
                       "[ToolCode]\nCode: %s\nExpected_L: %f, %f\nExpected_R: %f, %f\nObserved_L: %f, %f\nObserved_R: %f, %f\n",
                       EnumToString(info.code),
                       info.expectedCalibDotLeft_x, info.expectedCalibDotLeft_y,
                       info.expectedCalibDotRight_x, info.expectedCalibDotRight_y,
                       info.observedCalibDotLeft_x, info.observedCalibDotLeft_y,
                       info.observedCalibDotRight_x, info.observedCalibDotRight_y);
              
              PRINT_NAMED_INFO("HandleNVStorageOpResult.ToolCodeInfo","%s", buf);
              
              AppendToFile(_mfgDataSaveFile, buf);
              break;
            }
            case NVStorage::NVEntryTag::NVEntry_CalibPose:
            {
              
              // Pose data is stored like this. (See VisionSystem.cpp)
              //const f32 poseData[6] = {
              //  angleX.ToFloat(), angleY.ToFloat(), angleZ.ToFloat(),
              //  calibPose.GetTranslation().x(),
              //  calibPose.GetTranslation().y(),
              //  calibPose.GetTranslation().z(),
              //};
              
              const u32 sizeOfPoseData = 6 * sizeof(f32);
              if (recvdData->size() != MakeWordAligned(sizeOfPoseData)) {
                PRINT_NAMED_INFO("HandleNVStorageOpResult.CalibPose.UnexpectedSize",
                                 "Expected %zu, got %zu", MakeWordAligned(sizeOfPoseData), recvdData->size());
                break;
              }
              
              char buf[128];
              f32* poseData = (f32*)(recvdData->data());
              snprintf(buf, sizeof(buf),
                       "[CalibPose]\nRot: %f %f %f\nTrans: %f %f %f\n",
                       poseData[0], poseData[1], poseData[2], poseData[3], poseData[4], poseData[5] );
              
              PRINT_NAMED_INFO("HandleNVStorageOpResult.CalibPose","%s", buf);
              
              AppendToFile(_mfgDataSaveFile, buf);
              break;
            }
            case NVStorage::NVEntryTag::NVEntry_ObservedCubePose:
            {
              
              // Pose data is stored like this. (See BehaviorFactoryTest.cpp)
              //const f32 poseData[6] = {
              //  angleX.ToFloat(), angleY.ToFloat(), angleZ.ToFloat(),
              //  calibPose.GetTranslation().x(),
              //  calibPose.GetTranslation().y(),
              //  calibPose.GetTranslation().z(),
              //};
              
              const u32 sizeOfPoseData = 6 * sizeof(f32);
              if (recvdData->size() != MakeWordAligned(sizeOfPoseData)) {
                PRINT_NAMED_INFO("HandleNVStorageOpResult.ObservedCubePose.UnexpectedSize",
                                 "Expected %zu, got %zu", MakeWordAligned(sizeOfPoseData), recvdData->size());
                break;
              }
              
              char buf[128];
              f32* poseData = (f32*)(recvdData->data());
              snprintf(buf, sizeof(buf),
                       "[ObservedCubePose]\nRot: %f %f %f\nTrans: %f %f %f\n",
                       poseData[0], poseData[1], poseData[2], poseData[3], poseData[4], poseData[5] );
              
              PRINT_NAMED_INFO("HandleNVStorageOpResult.ObservedCubePose","%s", buf);
              
              AppendToFile(_mfgDataSaveFile, buf);
              break;
            }
            case NVStorage::NVEntryTag::NVEntry_PlaypenTestResults:
            {
              FactoryTestResultEntry result;
              if (recvdData->size() != MakeWordAligned(result.Size())) {
                PRINT_NAMED_INFO("HandleNVStorageOpResult.PlaypenTestResults.UnexpectedSize",
                                 "Expected %zu, got %zu", MakeWordAligned(result.Size()), recvdData->size());
                break;
              }
              result.Unpack(recvdData->data(), result.Size());
              //time_t rawtime = static_cast<time_t>(result.utcTime);
              
              char buf[512];
              snprintf(buf, sizeof(buf),
                       "[PlayPenTest]\nResult: %s\nTime: %llu\nSHA-1: %x\nStationID: %d\nTimestamps: %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
                       EnumToString(result.result),
                       //ctime(&rawtime),
                       result.utcTime,
                       result.engineSHA1, result.stationID,
                       result.timestamps[0], result.timestamps[1], result.timestamps[2], result.timestamps[3],
                       result.timestamps[4], result.timestamps[5], result.timestamps[6], result.timestamps[7],
                       result.timestamps[8], result.timestamps[9], result.timestamps[10], result.timestamps[11],
                       result.timestamps[12], result.timestamps[13], result.timestamps[14], result.timestamps[15] );
              
              PRINT_NAMED_INFO("HandleNVStorageOpResult.PlaypenTestResults", "%s", buf);
              
              AppendToFile(_mfgDataSaveFile, buf);
              break;
            }
            case NVStorage::NVEntryTag::NVEntry_CalibImage1:
            case NVStorage::NVEntryTag::NVEntry_CalibImage2:
            case NVStorage::NVEntryTag::NVEntry_CalibImage3:
            case NVStorage::NVEntryTag::NVEntry_CalibImage4:
            case NVStorage::NVEntryTag::NVEntry_CalibImage5:
            case NVStorage::NVEntryTag::NVEntry_CalibImage6:
            case NVStorage::NVEntryTag::NVEntry_ToolCodeImageLeft:
            case NVStorage::NVEntryTag::NVEntry_ToolCodeImageRight:
            case NVStorage::NVEntryTag::NVEntry_MultiBlobJunk:
            {
              char outFile[128];
              sprintf(outFile,  "%snvstorage_output_%s.jpg", _mfgDataSaveFolder.c_str(), EnumToString(msg.tag));
              PRINT_NAMED_INFO("HandleNVStorageOpResult.Read.CalibImage",
                               "Writing to %s, size: %zu",
                               outFile, recvdData->size());
              
              FILE* fp = fopen(outFile, "wb");
              if (fp) {
                fwrite(recvdData->data(),1,recvdData->size(),fp);
                fclose(fp);
              } else {
                printf("%s open failed\n", outFile);
              }
              
              break;
            }
            default:
              PRINT_NAMED_INFO("HandleNVStorageOpResult.UnhandledTag", "%s", EnumToString(msg.tag));
              for(auto data : *recvdData)
              {
                printf("%d ", data);
              }
              printf("\n");
              break;
          }
        }
      }

  } // namespace Cozmo
} // namespace Anki


// =======================================================================

using namespace Anki;
using namespace Anki::Cozmo;

int main(int argc, char **argv)
{
  Anki::Util::PrintfLoggerProvider loggerProvider;
  loggerProvider.SetMinLogLevel(Anki::Util::ILoggerProvider::LOG_LEVEL_DEBUG);
  loggerProvider.SetMinToStderrLevel(Anki::Util::ILoggerProvider::LOG_LEVEL_WARN);  
  Anki::Util::gLoggerProvider = &loggerProvider;
  Anki::Cozmo::WebotsKeyboardController webotsCtrlKeyboard(BS_TIME_STEP);

  webotsCtrlKeyboard.PreInit();
  webotsCtrlKeyboard.WaitOnKeyboardToConnect();
  
  webotsCtrlKeyboard.Init();
  while (webotsCtrlKeyboard.Update() == 0)
  {
  }

  return 0;
}

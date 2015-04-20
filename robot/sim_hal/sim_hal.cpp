// System Includes
#include <cmath>
#include <cstdlib>
#include <vector>
#include <set>

// Our Includes
#include "anki/common/robot/errorHandling.h"

#include "anki/cozmo/robot/hal.h"
#include "anki/cozmo/shared/cozmoConfig.h"
#include "anki/cozmo/shared/activeBlockTypes.h"
#include "messages.h"
#include "wheelController.h"

#include "sim_overlayDisplay.h"
#include "BlockMessages.h"

// Webots Includes
#include <webots/Robot.hpp>
#include <webots/Supervisor.hpp>

#define BLUR_CAPTURED_IMAGES 1

#define DEBUG_GRIPPER 0

#if BLUR_CAPTURED_IMAGES
#include "opencv2/imgproc/imgproc.hpp"
#endif

#ifndef SIMULATOR
#error SIMULATOR should be defined by any target using sim_hal.cpp
#endif

namespace Anki {
  namespace Cozmo {
    
    namespace { // "Private members"

      // Const paramters / settings
      // TODO: some of these should be defined elsewhere (e.g. comms)
      
      // Represents the number of cycles it takes to engage or disengage an active gripper
#if defined(HAVE_ACTIVE_GRIPPER) && HAVE_ACTIVE_GRIPPER
      const s32 UNLOCK_HYSTERESIS = 50;
#else
      //const s32 UNLOCK_HYSTERESIS = 0;
#endif
      
      const f64 WEBOTS_INFINITY = std::numeric_limits<f64>::infinity();
      
      const f32 MIN_WHEEL_POWER_FOR_MOTION = 0.15;
      
#pragma mark --- Simulated HardwareInterface "Member Variables" ---
      
      bool isInitialized = false;
      
      webots::Supervisor webotRobot_;
      
      s32 robotID_ = -1;
      
      TimeStamp_t timeStamp_ = 0;
      
      // Motors
      webots::Motor* leftWheelMotor_;
      webots::Motor* rightWheelMotor_;
      
      webots::Motor* headMotor_;
      webots::Motor* liftMotor_;
      
      webots::Motor* motors_[HAL::MOTOR_COUNT];
      
      // Motor position sensors
      webots::PositionSensor* leftWheelPosSensor_;
      webots::PositionSensor* rightWheelPosSensor_;
      webots::PositionSensor* headPosSensor_;
      webots::PositionSensor* liftPosSensor_;
      webots::PositionSensor* motorPosSensors_[HAL::MOTOR_COUNT];
      
      
      // Gripper
      webots::Connector* con_;
      //bool gripperEngaged_ = false;
      bool isGripperEnabled_ = false;
      //s32 unlockhysteresis_ = UNLOCK_HYSTERESIS;
      
      
      // Cameras / Vision Processing
      webots::Camera* headCam_;
      HAL::CameraInfo headCamInfo_;
      u32 cameraStartTime_ms_;
      
      // For pose information
      webots::GPS* gps_;
      webots::Compass* compass_;
      //webots::Node* estPose_;
      //char locStr[MAX_TEXT_DISPLAY_LENGTH];
      
      // IMU
      webots::Gyro* gyro_;
      webots::Accelerometer* accel_;
      
      // Prox sensors
      webots::DistanceSensor *proxLeft_;
      webots::DistanceSensor *proxCenter_;
      webots::DistanceSensor *proxRight_;
      
      // Emitter / receiver for block communication
      webots::Emitter *blockCommsEmitter_;
      webots::Receiver *blockCommsReceiver_;
      
      // Block ID flashing parameters
      s32 flashBlockIdx_ = -1;
      TimeStamp_t flashStartTime_ = 0;
      
      // List of all blockIDs
      std::set<s32> blockIDs_;
      
      // For tracking wheel distance travelled
      f32 motorPositions_[HAL::MOTOR_COUNT];
      f32 motorPrevPositions_[HAL::MOTOR_COUNT];
      f32 motorSpeeds_[HAL::MOTOR_COUNT];
      f32 motorSpeedCoeffs_[HAL::MOTOR_COUNT];

      HAL::IDCard idCard_;
      
      // Lights
      webots::LED* leds_[NUM_LEDS] = {0};
      
#pragma mark --- Simulated Hardware Interface "Private Methods" ---
      // Localization
      //void GetGlobalPose(f32 &x, f32 &y, f32& rad);
      

      // Approximate open-loop conversion of wheel power to angular wheel speed
      float WheelPowerToAngSpeed(float power)
      {
        // Inverse of speed-power formula in WheelController
        float speed_mm_per_s = power / 0.005f;
        
        // Convert mm/s to rad/s
        return speed_mm_per_s / WHEEL_RAD_TO_MM;
      }
      
      void MotorUpdate()
      {
        // Update position and speed info
        f32 posDelta = 0;
        for(int i = 0; i < HAL::MOTOR_COUNT; i++)
        {
          if (motors_[i]) {
            f32 pos = motorPosSensors_[i]->getValue();
            posDelta = pos - motorPrevPositions_[i];
            
            // Update position
            motorPositions_[i] += posDelta;
            
            // Update speed
            motorSpeeds_[i] = (posDelta * ONE_OVER_CONTROL_DT) * (1.0 - motorSpeedCoeffs_[i]) + motorSpeeds_[i] * motorSpeedCoeffs_[i];
            
            motorPrevPositions_[i] = pos;
          }
        }
      }
      
      
      void SetHeadAngularVelocity(const f32 rad_per_sec)
      {
        headMotor_->setVelocity(rad_per_sec);
      }
      
      
      void SetLiftAngularVelocity(const f32 rad_per_sec)
      {
        liftMotor_->setVelocity(rad_per_sec);
      }
      
      Result SendBlockMessage(const u8 blockID, BlockMessages::ID msgID, u8* buffer)
      {
        
        // Check that blockID is valid
        //if (blockID >= blockIDs_.size()) {
        if (blockIDs_.count(blockID) == 0) {
          PRINT("***ERROR (SendBlockMessage): Unknown active block ID %d\n", blockID);
          return RESULT_FAIL;
        }
        
        // Set channel
        blockCommsEmitter_->setChannel( blockID );
        
        // Prepend msgID to buffer
        u16 msgSize = BlockMessages::GetSize(msgID);
        u8 buf[msgSize+1];
        buf[0] = msgID;
        memcpy(buf+1, buffer, msgSize);
        blockCommsEmitter_->send(buf, msgSize + 1);
        
        return RESULT_OK;
      }
      
      Result FlashBlock(const u8 blockID) {
        Anki::Cozmo::BlockMessages::FlashID m;
        return SendBlockMessage(blockID, BlockMessages::FlashID_ID, (u8*)&m);
      }
      

      
    } // "private" namespace
    
    namespace Sim {
      // Create a pointer to the webots supervisor object within
      // a Simulator namespace so that other Simulation-specific code
      // can talk to it.  This avoids there being a global gCozmoBot
      // running around, accessible in non-simulator code.
      webots::Supervisor* CozmoBot = &webotRobot_;
    }
    
#pragma mark --- Simulated Hardware Method Implementations ---
    
    // Forward Declaration.  This is implemented in sim_radio.cpp
    Result InitSimRadio(const char* advertisementIP);
    
    Result HAL::Init()
    {
      assert(TIME_STEP >= webotRobot_.getBasicTimeStep());
      
      leftWheelMotor_  = webotRobot_.getMotor("LeftWheelMotor");
      rightWheelMotor_ = webotRobot_.getMotor("RightWheelMotor");
      
      headMotor_  = webotRobot_.getMotor("HeadMotor");
      liftMotor_  = webotRobot_.getMotor("LiftMotor");
      
      leftWheelPosSensor_ = webotRobot_.getPositionSensor("LeftWheelMotorPosSensor");
      rightWheelPosSensor_ = webotRobot_.getPositionSensor("RightWheelMotorPosSensor");
      headPosSensor_ = webotRobot_.getPositionSensor("HeadMotorPosSensor");
      liftPosSensor_ = webotRobot_.getPositionSensor("LiftMotorPosSensor");
      
      
      con_ = webotRobot_.getConnector("gripperConnector");
      //con_->enablePresence(TIME_STEP);
      
      //matCam_ = webotRobot_.getCamera("cam_down");
      headCam_ = webotRobot_.getCamera("HeadCamera");
      
      if(VISION_TIME_STEP % static_cast<u32>(webotRobot_.getBasicTimeStep()) != 0) {
        PRINT("VISION_TIME_STEP (%d) must be a multiple of the world's basic timestep (%.0f).\n",
              VISION_TIME_STEP, webotRobot_.getBasicTimeStep());
        return RESULT_FAIL;
      }
      //matCam_->enable(VISION_TIME_STEP);
      headCam_->enable(VISION_TIME_STEP);
      
      // HACK: Figure out when first camera image will actually be taken (next
      // timestep from now), so we can reference to it when computing frame
      // capture time from now on.
      // TODO: Not sure from Cyberbotics support message whether this should include "+ TIME_STEP" or not...
      cameraStartTime_ms_ = HAL::GetTimeStamp(); // + TIME_STEP;
      printf("Setting camera start time as %d.\n", cameraStartTime_ms_);
      
      // Set ID
      // Expected format of name is <SomeName>_<robotID>
      std::string name = webotRobot_.getName();
      size_t lastDelimPos = name.rfind('_');
      if (lastDelimPos != std::string::npos) {
        robotID_ = atoi( name.substr(lastDelimPos+1).c_str() );
        if (robotID_ < 1) {
          printf("***ERROR: Invalid robot name (%s). ID must be greater than 0\n", name.c_str());
          return RESULT_FAIL;
        }
        printf("Initializing robot ID: %d\n", robotID_);
      } else {
        printf("***ERROR: Cozmo robot name %s is invalid.  Must end with '_<ID number>'\n.", name.c_str());
        return RESULT_FAIL;
      }
      
      // ID card info
      idCard_.esn = robotID_;
      idCard_.modelNumber = 0;
      idCard_.lotCode = 0;
      idCard_.birthday = 0;
      idCard_.hwVersion = 0;
      
      
      //Set the motors to velocity mode
      headMotor_->setPosition(WEBOTS_INFINITY);
      liftMotor_->setPosition(WEBOTS_INFINITY);
      leftWheelMotor_->setPosition(WEBOTS_INFINITY);
      rightWheelMotor_->setPosition(WEBOTS_INFINITY);
      
      // Load motor array
      motors_[MOTOR_LEFT_WHEEL] = leftWheelMotor_;
      motors_[MOTOR_RIGHT_WHEEL] = rightWheelMotor_;
      motors_[MOTOR_HEAD] = headMotor_;
      motors_[MOTOR_LIFT] = liftMotor_;
      //motors_[MOTOR_GRIP] = NULL;
      
      // Load position sensor array
      motorPosSensors_[MOTOR_LEFT_WHEEL] = leftWheelPosSensor_;
      motorPosSensors_[MOTOR_RIGHT_WHEEL] = rightWheelPosSensor_;
      motorPosSensors_[MOTOR_HEAD] = headPosSensor_;
      motorPosSensors_[MOTOR_LIFT] = liftPosSensor_;
      
      
      // Initialize motor positions
      for (int i=0; i < MOTOR_COUNT; ++i) {
        motorPositions_[i] = 0;
        motorPrevPositions_[i] = 0;
        motorSpeeds_[i] = 0;
        motorSpeedCoeffs_[i] = 0.2;
      }
      
      // Enable position measurements on head, lift, and wheel motors
      leftWheelPosSensor_->enable(TIME_STEP);
      rightWheelPosSensor_->enable(TIME_STEP);
      headPosSensor_->enable(TIME_STEP);
      liftPosSensor_->enable(TIME_STEP);
      
      // Set speeds to 0
      leftWheelMotor_->setVelocity(0);
      rightWheelMotor_->setVelocity(0);
      headMotor_->setVelocity(0);
      liftMotor_->setVelocity(0);
      
      // Get localization sensors
      gps_ = webotRobot_.getGPS("gps");
      compass_ = webotRobot_.getCompass("compass");
      gps_->enable(TIME_STEP);
      compass_->enable(TIME_STEP);
      //estPose_ = webotRobot_.getFromDef("CozmoBotPose");
      
      // Gyro
      gyro_ = webotRobot_.getGyro("gyro");
      gyro_->enable(TIME_STEP);
      
      // Accelerometer
      accel_ = webotRobot_.getAccelerometer("accel");
      accel_->enable(TIME_STEP);
      
      // Proximity sensors
      proxLeft_ = webotRobot_.getDistanceSensor("proxSensorLeft");
      proxCenter_ = webotRobot_.getDistanceSensor("proxSensorCenter");
      proxRight_ = webotRobot_.getDistanceSensor("proxSensorRight");
      proxLeft_->enable(TIME_STEP);
      proxCenter_->enable(TIME_STEP);
      proxRight_->enable(TIME_STEP);
      
      // Block radio
      blockCommsEmitter_ = webotRobot_.getEmitter("blockCommsEmitter");
      blockCommsReceiver_ = webotRobot_.getReceiver("blockCommsReceiver");
      blockCommsReceiver_->setChannel(-1); // Listen to all blocks
      blockCommsReceiver_->enable(TIME_STEP);

      // Reset index of block that is currently flashing ID
      flashBlockIdx_ = -1;
      
      
      // Get IDs of all available active blocks in the world
      blockIDs_.clear();
      webots::Node* root = webotRobot_.getRoot();
      webots::Field* rootChildren = root->getField("children");
      int numRootChildren = rootChildren->getCount();
      
      for (s32 n = 0 ; n<numRootChildren; ++n) {

        // Check for nodes that have a 'blockColor' and 'active' field
        webots::Node* nd = rootChildren->getMFNode(n);
        webots::Field* blockColorField = nd->getField("blockColor");
        webots::Field* activeField = nd->getField("active");
        webots::Field* activeIdField = nd->getField("activeID");
        if (blockColorField && activeField && activeIdField) {
          if (activeField->getSFBool()) {
            const s32 activeID = activeIdField->getSFInt32();
            
            if(blockIDs_.count(activeID) > 0) {
              printf("ERROR: ignoring active block with duplicate ID of %d\n", activeID);
            } else {
              printf("Found active block %d\n", activeID);
              blockIDs_.insert(activeID);
            }
            continue;
          }
        }
      }

      
      // Get advertisement host IP
      webots::Field *advertisementHostField = webotRobot_.getSelf()->getField("advertisementHost");
      std::string advertisementIP = "127.0.0.1";
      if (advertisementHostField) {
        advertisementIP = advertisementHostField->getSFString();
      } else {
        printf("No valid advertisement IP found\n");
      }
      
      if(InitSimRadio(advertisementIP.c_str()) == RESULT_FAIL) {
        printf("Failed to initialize Simulated Radio.\n");
        return RESULT_FAIL;
      }
      
      // Lights
      leds_[LED_LEFT_EYE_TOP] = webotRobot_.getLED("LeftEyeLED_top");
      leds_[LED_LEFT_EYE_LEFT] = webotRobot_.getLED("LeftEyeLED_left");
      leds_[LED_LEFT_EYE_RIGHT] = webotRobot_.getLED("LeftEyeLED_right");
      leds_[LED_LEFT_EYE_BOTTOM] = webotRobot_.getLED("LeftEyeLED_bottom");
      
      leds_[LED_RIGHT_EYE_TOP] = webotRobot_.getLED("RightEyeLED_top");
      leds_[LED_RIGHT_EYE_LEFT] = webotRobot_.getLED("RightEyeLED_left");
      leds_[LED_RIGHT_EYE_RIGHT] = webotRobot_.getLED("RightEyeLED_right");
      leds_[LED_RIGHT_EYE_BOTTOM] = webotRobot_.getLED("RightEyeLED_bottom");
      
      leds_[LED_HEALTH_0] = webotRobot_.getLED("ledHealth0");
      leds_[LED_HEALTH_1] = webotRobot_.getLED("ledHealth1");
      leds_[LED_HEALTH_2] = webotRobot_.getLED("ledHealth2");
      leds_[LED_DIR_LEFT] = webotRobot_.getLED("ledDirLeft");
      leds_[LED_DIR_RIGHT] = webotRobot_.getLED("ledDirRight");
      
      isInitialized = true;
      return RESULT_OK;
      
    } // Init()
    
    void HAL::Destroy()
    {
      // Turn off components: (strictly necessary?)
      //matCam_->disable();
      headCam_->disable();
      
      gps_->disable();
      compass_->disable();

    } // Destroy()
    
    bool HAL::IsInitialized(void)
    {
      return isInitialized;
    }
    
    
    void HAL::GetGroundTruthPose(f32 &x, f32 &y, f32& rad)
    {
      
      const double* position = gps_->getValues();
      const double* northVector = compass_->getValues();
      
      x = position[0];
      y = position[1];
      
      rad = std::atan2(-northVector[1], northVector[0]);
      
      //PRINT("GroundTruth:  pos %f %f %f   rad %f %f %f\n", position[0], position[1], position[2],
      //      northVector[0], northVector[1], northVector[2]);
      
      
    } // GetGroundTruthPose()
    
    
    bool HAL::IsGripperEngaged() {
      return isGripperEnabled_ && con_->getPresence()==1;
    }
    
    void HAL::UpdateDisplay(void)
    {
      using namespace Sim::OverlayDisplay;
     /*
      PRINT("speedDes: %d, speedCur: %d, speedCtrl: %d, speedMeas: %d\n",
              GetUserCommandedDesiredVehicleSpeed(),
              GetUserCommandedCurrentVehicleSpeed(),
              GetControllerCommandedVehicleSpeed(),
              GetCurrentMeasuredVehicleSpeed());
      */
       
    } // HAL::UpdateDisplay()
    
    
    
    void HAL::IMUReadData(HAL::IMU_DataStructure &IMUData)
    {
      const double* vals = gyro_->getValues();  // rad/s
      IMUData.rate_x = (f32)(vals[0]);
      IMUData.rate_y = (f32)(vals[1]);
      IMUData.rate_z = (f32)(vals[2]);
      
      vals = accel_->getValues();   // m/s^2
      IMUData.acc_x = (f32)(vals[0] * 1000);  // convert to mm/s^2
      IMUData.acc_y = (f32)(vals[1] * 1000);
      IMUData.acc_z = (f32)(vals[2] * 1000);
    }
    
    
    // Set the motor power in the unitless range [-1.0, 1.0]
    void HAL::MotorSetPower(MotorID motor, f32 power)
    {
      switch(motor) {
        case MOTOR_LEFT_WHEEL:
          leftWheelMotor_->setVelocity(WheelPowerToAngSpeed(power));
          break;
        case MOTOR_RIGHT_WHEEL:
          rightWheelMotor_->setVelocity(WheelPowerToAngSpeed(power));
          break;
        case MOTOR_LIFT:
          // TODO: Assuming linear relationship, but it's not!
          SetLiftAngularVelocity(power * MAX_LIFT_SPEED);
          break;
#if defined(HAVE_ACTIVE_GRIPPER) && HAVE_ACTIVE_GRIPPER
        case MOTOR_GRIP:
          if (power > 0) {
            EngageGripper();
          } else {
            DisengageGripper();
          }
          break;
#endif
        case MOTOR_HEAD:
          // TODO: Assuming linear relationship, but it's not!
          SetHeadAngularVelocity(power * MAX_HEAD_SPEED);
          break;
        default:
          PRINT("ERROR (HAL::MotorSetPower) - Undefined motor type %d\n", motor);
          return;
      }
    }
    
    // Reset the internal position of the specified motor to 0
    void HAL::MotorResetPosition(MotorID motor)
    {
      if (motor >= MOTOR_COUNT) {
        PRINT("ERROR (HAL::MotorResetPosition) - Undefined motor type %d\n", motor);
        return;
      }
      
      motorPositions_[motor] = 0;
      //motorPrevPositions_[motor] = 0;
    }
    
    // Returns units based on the specified motor type:
    // Wheels are in mm/s, everything else is in degrees/s.
    f32 HAL::MotorGetSpeed(MotorID motor)
    {
      switch(motor) {
        case MOTOR_LEFT_WHEEL:
        case MOTOR_RIGHT_WHEEL:
        {
          return motorSpeeds_[motor] * WHEEL_RAD_TO_MM;
        }

        case MOTOR_LIFT:
        {
          return motorSpeeds_[MOTOR_LIFT];
        }
          
        //case MOTOR_GRIP:
        //  // TODO
        //  break;
          
        case MOTOR_HEAD:
        {
          return motorSpeeds_[MOTOR_HEAD];
        }
          
        default:
          PRINT("ERROR (HAL::MotorGetSpeed) - Undefined motor type %d\n", motor);
          break;
      }
      return 0;
    }
    
    // Returns units based on the specified motor type:
    // Wheels are in mm since reset, everything else is in degrees.
    f32 HAL::MotorGetPosition(MotorID motor)
    {
      switch(motor) {
        case MOTOR_RIGHT_WHEEL:
        case MOTOR_LEFT_WHEEL:
          return motorPositions_[motor] * WHEEL_RAD_TO_MM;
        case MOTOR_LIFT:
        case MOTOR_HEAD:
          return motorPositions_[motor];
          break;
        default:
          PRINT("ERROR (HAL::MotorGetPosition) - Undefined motor type %d\n", motor);
          return 0;
      }
      
      return motorPositions_[motor];
    }
    
    
    void HAL::EngageGripper()
    {
      con_->lock();
      con_->enablePresence(TIME_STEP);
      isGripperEnabled_ = true;
#     if DEBUG_GRIPPER
      PRINT("GRIPPER LOCKED!\n");
#     endif
      
      /*
      //Should we lock to a block which is close to the connector?
      if (!gripperEngaged_ && con_->getPresence() == 1)
      {
        if (unlockhysteresis_ == 0)
        {
          con_->lock();
          gripperEngaged_ = true;
          printf("GRIPPER LOCKED!\n");
        }else{
          unlockhysteresis_--;
        }
      }
       */
    }
    
    void HAL::DisengageGripper()
    {
      con_->unlock();
      con_->disablePresence();
      isGripperEnabled_ = false;
#     if DEBUG_GRIPPER
      PRINT("GRIPPER UNLOCKED!\n");
#     endif
      
      /*
      if (gripperEngaged_)
      {
        gripperEngaged_ = false;
        unlockhysteresis_ = UNLOCK_HYSTERESIS;
        con_->unlock();
        printf("GRIPPER UNLOCKED!\n");
      }
       */
    }

    
    
    // Forward declaration
    void RadioUpdate();
      
    Result HAL::Step(void)
    {

      if(webotRobot_.step(Cozmo::TIME_STEP) == -1) {
        return RESULT_FAIL;
      } else {
        MotorUpdate();
        RadioUpdate();
        
        /*
        // Always display ground truth pose:
        {
          const double* position = gps_->getValues();
          const double* northVector = compass_->getValues();
          
          const f32 rad = std::atan2(-northVector[1], northVector[0]);
          
          char buffer[256];
          snprintf(buffer, 256, "Robot %d Pose: (%.1f,%.1f,%.1f), %.1fdeg@(0,0,1)",
                   robotID_,
                   M_TO_MM(position[0]), M_TO_MM(position[1]), M_TO_MM(position[2]),
                   RAD_TO_DEG(rad));
          
          std::string poseString(buffer);
          webotRobot_.setLabel(robotID_, poseString, 0.5, robotID_*.05, .05, 0xff0000, 0.);
        }
         */
        
        // Manage block flashing
        if (flashBlockIdx_ >= 0) {
          if (HAL::GetTimeStamp() >= flashStartTime_ + FLASH_BLOCK_TIME_INTERVAL_MS) {
            if (flashBlockIdx_ >= blockIDs_.size()) {
              //printf("Block flashing complete\n");
              flashBlockIdx_ = -1;
              flashStartTime_ = 0;
            } else {
              //printf("Flashing block %d\n", flashBlockIdx_);
              if (FlashBlock(flashBlockIdx_) == RESULT_FAIL) {
                printf("FAILED to flash block %d\n", flashBlockIdx_);
              }
              ++flashBlockIdx_;
              flashStartTime_ = HAL::GetTimeStamp();
            }
          }
        }
        
        // TODO: Make block comms receiver checking into a HAL function at some point
        //   and call it from the main execution loop
        while(blockCommsReceiver_->getQueueLength() > 0) {
          int dataSize = blockCommsReceiver_->getDataSize();
          
          if(dataSize == BlockMessages::GetSize(BlockMessages::BlockMoved_ID)) {
            // Pass along block-moved messages to basestation
            u8* data = (u8*)blockCommsReceiver_->getData();
            BlockMessages::BlockMoved* msgIn = reinterpret_cast<BlockMessages::BlockMoved*>(data);
            Messages::ActiveObjectMoved msgOut;
            msgOut.objectID = msgIn->blockID;
            HAL::RadioSendMessage(Messages::ActiveObjectMoved_ID, &msgOut);
          } else {
            printf("Received unknown-sized message (%d bytes) over block comms.\n", dataSize);
          }
          blockCommsReceiver_->nextPacket();
        }
        
        return RESULT_OK;
      }
      
      
    } // step()
    
    
    // Helper function to create a CameraInfo struct from Webots camera properties:
    void FillCameraInfo(const webots::Camera *camera,
                        HAL::CameraInfo &info)
    {
      
      const u16 nrows  = static_cast<u16>(camera->getHeight());
      const u16 ncols  = static_cast<u16>(camera->getWidth());
      const f32 width  = static_cast<f32>(ncols);
      const f32 height = static_cast<f32>(nrows);
      //f32 aspect = width/height;
      
      const f32 fov_hor = camera->getFov();

      // Compute focal length from simulated camera's reported FOV:
      const f32 f = width / (2.f * std::tan(0.5f*fov_hor));
      
      // There should only be ONE focal length, because simulated pixels are
      // square, so no need to compute/define a separate fy
      //f32 fy = height / (2.f * std::tan(0.5f*fov_ver));
      
      info.focalLength_x = f;
      info.focalLength_y = f;
      info.center_x      = 0.5f*width;
      info.center_y      = 0.5f*height;
      info.skew          = 0.f;
      info.nrows         = nrows;
      info.ncols         = ncols;
      
      for(u8 i=0; i<NUM_RADIAL_DISTORTION_COEFFS; ++i) {
        info.distortionCoeffs[i] = 0.f;
      }
      
    } // FillCameraInfo
    
    const HAL::CameraInfo* HAL::GetHeadCamInfo(void)
    {
      if(isInitialized) {
        FillCameraInfo(headCam_, headCamInfo_);
        return &headCamInfo_;
      }
      else {
        PRINT("HeadCam calibration requested before HAL initialized.\n");
        return NULL;
      }
    }
    
    /*
    HAL::CameraMode HAL::GetHeadCamMode(void)
    {
      return headCamMode_;
    }
    
    // TODO: there is a copy of this in hal.cpp -- consolidate into one location.
    // TODO: perhaps we'd rather have this be a switch statement
    //       (However, if the header is stored as a member of the CameraModeInfo
    //        struct, we can't use it as a case in the switch statement b/c
    //        the compiler doesn't think it's a constant expression.  We can
    //        get around this using "constexpr" when declaring CameraModeInfo,
    //        but that's a C++11 thing and not likely supported on the Movidius
    //        compiler)
    void HAL::SetHeadCamMode(const u8 frameResHeader)
    {
      bool found = false;
      for(CameraMode mode = CAMERA_MODE_VGA;
          not found && mode != CAMERA_MODE_COUNT; ++mode)
      {
        if(frameResHeader == CameraModeInfo[mode].header) {
          headCamMode_ = mode;
          found = true;
        }
      }
      
      if(not found) {
        PRINT("ERROR(SetCameraMode): Unknown frame res: %d", frameResHeader);
      }
    } //SetHeadCamMode()
    */
    
    void HAL::CameraSetParameters(f32 exposure, bool enableVignettingCorrection)
    {
      // Can't control simulated camera's exposure.
      
      // TODO: Simulate this somehow?
      
      return;
      
    } // HAL::CameraSetParameters()
    
    
    u32 HAL::GetCameraStartTime()
    {
      return cameraStartTime_ms_;
    }
    
    // Starts camera frame synchronization
    void HAL::CameraGetFrame(u8* frame, Vision::CameraResolution res, bool enableLight)
    {
      // TODO: enableLight?
      AnkiConditionalErrorAndReturn(frame != NULL, "SimHAL.CameraGetFrame.NullFramePointer",
                                    "NULL frame pointer provided to CameraGetFrame(), check "
                                    "to make sure the image allocation succeeded.\n");
      
      static u32 lastFrameTime_ms = static_cast<u32>(webotRobot_.getTime()*1000.0);
      u32 currentTime_ms = static_cast<u32>(webotRobot_.getTime()*1000.0);
      AnkiConditionalWarn(currentTime_ms - lastFrameTime_ms > headCam_->getSamplingPeriod(),
                          "SimHAL.CameraGetFrame",
                          "Image requested too soon -- new frame may not be ready yet.\n");
      
      const u8* image = headCam_->getImage();
      
      AnkiConditionalErrorAndReturn(image != NULL, "SimHAL.CameraGetFrame.NullImagePointer",
                                    "NULL image pointer returned from simulated camera's getFrame() method.\n");

      s32 pixel = 0;
      for (s32 y=0; y < headCamInfo_.nrows; y++) {
        for (s32 x=0; x < headCamInfo_.ncols; x++) {
          frame[pixel++] = webots::Camera::imageGetRed(image,   headCam_->getWidth(), x, y);
          frame[pixel++] = webots::Camera::imageGetGreen(image, headCam_->getWidth(), x, y);
          frame[pixel++] = webots::Camera::imageGetBlue(image,  headCam_->getWidth(), x, y);
        }
      }
      
#     if BLUR_CAPTURED_IMAGES
      // Add some blur to simulated images
      cv::Mat cvImg(headCamInfo_.nrows, headCamInfo_.ncols, CV_8UC3, frame);
      cv::GaussianBlur(cvImg, cvImg, cv::Size(0,0), 0.75f);
#     endif
      
    } // CameraGetFrame()
  
    
    // Get the number of microseconds since boot
    u32 HAL::GetMicroCounter(void)
    {
      return static_cast<u32>(webotRobot_.getTime() * 1000000.0);
    }
    
    void HAL::MicroWait(u32 microseconds)
    {
      u32 now = GetMicroCounter();
      while ((GetMicroCounter() - now) < microseconds)
        ;
    }
    
    TimeStamp_t HAL::GetTimeStamp(void)
    {
      return static_cast<TimeStamp_t>(webotRobot_.getTime() * 1000.0);
      //return timeStamp_;
    }
    
    void HAL::SetTimeStamp(TimeStamp_t t)
    {
      //timeStamp_ = t;
    };
    
    void HAL::SetLED(LEDId led_id, u32 color) {
      if (leds_[led_id]) {
        leds_[led_id]->set(color);
      } else {
        PRINT("Unhandled LED %d\n", led_id);
      }
    }

    void HAL::SetHeadlights(bool state)
    {
      // TODO: ...
    }
    
    HAL::IDCard* HAL::GetIDCard()
    {
      return &idCard_;
    }
   
    
    void HAL::GetProximity(ProximityValues *prox)
    {
      static int proxID = IRleft;
      switch(proxID)
      {
        case IRforward:
          prox->forward = proxCenter_->getValue();
          prox->latest  = IRforward;
          proxID = IRleft;
          break;
          
        case IRleft:
          prox->left = proxLeft_->getValue();
          prox->latest = IRleft;
          proxID = IRright;
          break;
          
        case IRright:
          prox->right = proxRight_->getValue();
          prox->latest = IRright;
          proxID = IRforward;
          break;
          
        default:
          AnkiAssert(false);
      }
      
      return;
    } // GetProximity_INT()
    
    namespace HAL {
      int UARTGetFreeSpace()
      {
        return 100000000;
      }
      
      int UARTGetWriteBufferSize()
      {
        return 100000000;
      }
    }
    
    u8 HAL::BatteryGetVoltage10x()
    {
      // Return voltage*10 for now...
      return 50;
    }
    
    bool HAL::BatteryIsCharging()
    {
      return false; // XXX On Cozmo 3, head is off if robot is charging
    }
    
    bool HAL::BatteryIsOnCharger()
    {
      return false; // XXX On Cozmo 3, head is off if robot is charging
    }
    
    void HAL::FlashBlockIDs() {
      flashBlockIdx_ = 0;
      flashStartTime_ = HAL::GetTimeStamp();
    }
    
    Result HAL::SetBlockLight(const u8 blockID, const u32* color,
                              const u32* onPeriod_ms, const u32* offPeriod_ms,
                              const u32* transitionOnPeriod_ms, const u32* transitionOffPeriod_ms)
    {
      Anki::Cozmo::BlockMessages::SetBlockLights m;
      for (int i=0; i<NUM_BLOCK_LEDS; ++i) {
        m.color[i] = color[i];
        m.onPeriod_ms[i] = onPeriod_ms[i];
        m.offPeriod_ms[i] = offPeriod_ms[i];
        m.transitionOnPeriod_ms[i] = (transitionOnPeriod_ms == nullptr ? 0 : transitionOnPeriod_ms[i]);
        m.transitionOffPeriod_ms[i] = (transitionOffPeriod_ms == nullptr ? 0 : transitionOffPeriod_ms[i]);
      }
      
      return SendBlockMessage(blockID, BlockMessages::SetBlockLights_ID, (u8*)&m);
    }
    
    
  } // namespace Cozmo
} // namespace Anki

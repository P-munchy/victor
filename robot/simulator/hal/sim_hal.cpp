// System Includes
#include <cmath>
#include <cstdlib>

// Our Includes
#include "anki/common/robot/errorHandling.h"

#include "anki/cozmo/robot/hal.h"
#include "anki/cozmo/robot/cozmoConfig.h"
#include "anki/cozmo/robot/messages.h"
#include "anki/cozmo/robot/wheelController.h"

#include "sim_overlayDisplay.h"

// Webots Includes
#include <webots/Robot.hpp>
#include <webots/Supervisor.hpp>

#define BLUR_CAPTURED_IMAGES 1

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
      const s32 UNLOCK_HYSTERESIS = 0;
#endif
      
      const f64 WEBOTS_INFINITY = std::numeric_limits<f64>::infinity();
      
      
      const u32 CAMERA_SINGLE_CAPTURE_TIME_US = 1000000 / 15;  // 15Hz, VGA
      const u32 CAMERA_CONTINUOUS_CAPTURE_TIME_US = 1000000 / 30;  // 30Hz, VGA

      const f32 MIN_WHEEL_POWER_FOR_MOTION = 0.15;
      
#pragma mark --- Simulated HardwareInterface "Member Variables" ---
      
      bool isInitialized = false;
      
      webots::Supervisor webotRobot_;
      
      s32 robotID_ = -1;
      
      // Motors
      webots::Motor* leftWheelMotor_;
      webots::Motor* rightWheelMotor_;
      
      webots::Motor* headMotor_;
      webots::Motor* liftMotor_;
      
      webots::Motor* motors_[HAL::MOTOR_COUNT];
      
      
      
      // Gripper
      webots::Connector* con_;
      bool gripperEngaged_ = false;
      s32 unlockhysteresis_ = UNLOCK_HYSTERESIS;
      
      
      // Cameras / Vision Processing
      //webots::Camera* matCam_;
      webots::Camera* headCam_;
      HAL::CameraInfo headCamInfo_;
      HAL::CameraInfo matCamInfo_;
      Vision::CameraResolution headCamMode_;
      // HAL::CameraMode matCamMode_;
      //u8* headCamBuffer_;
      //u8* matCamBuffer_;
      
      //HAL::CameraUpdateMode headCamUpdateMode_;
      //HAL::CameraUpdateMode matCamUpdateMode_;
      
      // For pose information
      webots::GPS* gps_;
      webots::Compass* compass_;
      //webots::Node* estPose_;
      //char locStr[MAX_TEXT_DISPLAY_LENGTH];
      
      // IMU
      webots::Gyro* gyro_;
      webots::Accelerometer* accel_;
      
      // For tracking wheel distance travelled
      f32 motorPositions_[HAL::MOTOR_COUNT];
      f32 motorPrevPositions_[HAL::MOTOR_COUNT];
      f32 motorSpeeds_[HAL::MOTOR_COUNT];
      f32 motorSpeedCoeffs_[HAL::MOTOR_COUNT];

      
#pragma mark --- Simulated Hardware Interface "Private Methods" ---
      // Localization
      //void GetGlobalPose(f32 &x, f32 &y, f32& rad);
      

      // Approximate open-loop conversion of wheel power to angular wheel speed
      float WheelPowerToAngSpeed(float power)
      {
        float speed_mm_per_s = 0;
        
        // A minimum amount of power is required to actually move the wheels
        if (ABS(power) < MIN_WHEEL_POWER_FOR_MOTION) {
          return 0;
        }
        
        // Convert power to mm/s
        if (ABS(power) < WheelController::TRANSITION_POWER) {
          speed_mm_per_s = power / WheelController::LOW_OPEN_LOOP_GAIN;
        } else {
          speed_mm_per_s = CLIP(power, -1.0, 1.0) / WheelController::HIGH_OPEN_LOOP_GAIN;
        }
        
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
            f32 pos = motors_[i]->getPosition();
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
    Result InitSimRadio(s32 robotID);
    
    Result HAL::Init()
    {
      assert(TIME_STEP >= webotRobot_.getBasicTimeStep());
      
      leftWheelMotor_  = webotRobot_.getMotor("LeftWheelMotor");
      rightWheelMotor_ = webotRobot_.getMotor("RightWheelMotor");
      
      headMotor_  = webotRobot_.getMotor("HeadMotor");
      liftMotor_  = webotRobot_.getMotor("LiftMotor");
      
      con_ = webotRobot_.getConnector("gripperConnector");
      con_->enablePresence(TIME_STEP);
      
      //matCam_ = webotRobot_.getCamera("cam_down");
      headCam_ = webotRobot_.getCamera("HeadCamera");
      
      //matCam_->enable(VISION_TIME_STEP);
      headCam_->enable(VISION_TIME_STEP);

      
      // Set ID
      // Expected format of name is <SomeName>_<robotID>
      std::string name = webotRobot_.getName();
      size_t lastDelimPos = name.rfind('_');
      if (lastDelimPos != std::string::npos) {
        robotID_ = atoi( name.substr(lastDelimPos+1).c_str() );
        if (robotID_ < 1) {
          PRINT("***ERROR: Invalid robot name (%s). ID must be greater than 0\n", name.c_str());
          return RESULT_FAIL;
        }
        PRINT("Initializing robot ID: %d\n", robotID_);
      } else {
        PRINT("***ERROR: Cozmo robot name %s is invalid.  Must end with '_<ID number>'\n.", name.c_str());
        return RESULT_FAIL;
      }
      
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
      
      // Initialize motor positions
      for (int i=0; i < MOTOR_COUNT; ++i) {
        motorPositions_[i] = 0;
        motorPrevPositions_[i] = 0;
        motorSpeeds_[i] = 0;
        motorSpeedCoeffs_[i] = 0.2;
      }
      
      // Enable position measurements on head, lift, and wheel motors
      leftWheelMotor_->enablePosition(TIME_STEP);
      rightWheelMotor_->enablePosition(TIME_STEP);
      headMotor_->enablePosition(TIME_STEP);
      liftMotor_->enablePosition(TIME_STEP);
      
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
      
      if(InitSimRadio(robotID_) == RESULT_FAIL) {
        PRINT("Failed to initialize Simulated Radio.\n");
        return RESULT_FAIL;
      }
      
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
      return gripperEngaged_;
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
      //Should we lock to a block which is close to the connector?
      if (!gripperEngaged_ && con_->getPresence() == 1)
      {
        if (unlockhysteresis_ == 0)
        {
          con_->lock();
          gripperEngaged_ = true;
          //printf("GRIPPER LOCKED!\n");
        }else{
          unlockhysteresis_--;
        }
      }
    }
    
    void HAL::DisengageGripper()
    {
      if (gripperEngaged_)
      {
        gripperEngaged_ = false;
        unlockhysteresis_ = UNLOCK_HYSTERESIS;
        con_->unlock();
        //printf("GRIPPER UNLOCKED!\n");
      }
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
        
        return RESULT_OK;
      }
      
      
    } // step()
    
    
    // Helper function to create a CameraInfo struct from Webots camera properties:
    void FillCameraInfo(const webots::Camera *camera,
                        HAL::CameraInfo &info)
    {
      
      u16 nrows  = static_cast<u16>(camera->getHeight());
      u16 ncols  = static_cast<u16>(camera->getWidth());
      f32 width  = static_cast<f32>(ncols);
      f32 height = static_cast<f32>(nrows);
      f32 aspect = width/height;
      
      f32 fov_hor = camera->getFov();
      f32 fov_ver = fov_hor / aspect;
      
      f32 fy = height / (2.f * std::tan(0.5f*fov_ver));
      
      info.focalLength_x = fy;
      info.focalLength_y = fy;
      //info.fov_ver       = fov_ver;
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
    
    
    // Starts camera frame synchronization
    void HAL::CameraGetFrame(u8* frame, Vision::CameraResolution res, f32 exposure, bool enableLight)
    {
      // TODO: exposure? enableLight?
      
      const u8* image = headCam_->getImage();
      if(image == NULL) {
        PRINT("CameraGetFrame(): no image captured!");
      }
      else {
        // Set the increment / windowsize for downsampling
        s32 inc = 1;
        s32 pixel = 0;
        
        // TODO: add averaging once we support it in hardware
        const bool supportAveraging = false;
        
        if(supportAveraging) {
          AnkiAssert(false);
          // Need a way to get downsampling increment (LUT for resolution based on res?)
          //AnkiAssert(headCamInfo_.nrows >= HAL::CameraModeInfo[res].height);
          //inc = headCamInfo_.nrows / HAL::CameraModeInfo[res].height;
        }
        
        if(inc == 1)
        {
          // No averaging
          for (s32 y=0; y < headCamInfo_.nrows; y++) {
            for (s32 x=0; x < headCamInfo_.ncols; x++) {
              frame[pixel++] = webots::Camera::imageGetGrey(image, headCamInfo_.ncols, x, y);
            }
          }
        } else {
          // Average [inc x inc] windows
          for (s32 y = 0; y < headCamInfo_.nrows; y += inc)
          {
            for (s32 x = 0; x < headCamInfo_.ncols; x += inc)
            {
              s32 sum = 0;
              for (s32 y1 = y; y1 < y + inc; y1++)
              {
                for (s32 x1 = x; x1 < x + inc; x1++)
                {
                  //int index = x1 + y1 * headCamInfo_.ncols;
                  //sum += frame[index];
                  sum += webots::Camera::imageGetGrey(image, headCamInfo_.ncols, x1, y1);
                }
              }
              frame[pixel++] = (sum / (inc * inc));
            }
          }
        } // if averaging or not
      
#if BLUR_CAPTURED_IMAGES
        // Add some blur to simulated images
        cv::Mat_<u8> cvImg(headCamInfo_.nrows, headCamInfo_.ncols, frame);
        cv::GaussianBlur(cvImg, cvImg, cv::Size(0,0), 0.5f);
#endif
      } // if image==NULL or not
      
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
    }
    
    s32 HAL::GetRobotID(void)
    {
      return robotID_;
    }
    
    void HAL::SetLED(LEDId led_id, LEDColor color) {
      // TODO: ...
    }

  } // namespace Cozmo
} // namespace Anki

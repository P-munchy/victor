#include "headController.h"
#include "anki/cozmo/robot/hal.h"
#include "anki/common/robot/utilities_c.h"
#include "anki/common/shared/radians.h"
#include "anki/common/shared/velocityProfileGenerator.h"

#define DEBUG_HEAD_CONTROLLER 0

namespace Anki {
  namespace Cozmo {
  namespace HeadController {

    namespace {
    
      const Radians ANGLE_TOLERANCE = DEG_TO_RAD(3.f);
      
      // Head angle on startup
      const f32 HEAD_START_ANGLE = 0;   // Convenient for docking to set head angle at -15 degrees.
      
      // Currently applied power
      f32 power_ = 0;
      
      // Head angle control vars
      // 0 radians == looking straight ahead
      Radians currentAngle_ = 0.f;
      Radians desiredAngle_ = 0.f;
      f32 currDesiredAngle_ = 0.f;
      f32 currDesiredRadVel_ = 0.f;
      f32 angleError_ = 0.f;
      f32 angleErrorSum_ = 0.f;
      f32 prevHalPos_ = 0.f;
      bool inPosition_  = true;
      
      const f32 SPEED_FILTERING_COEFF = 0.9f;

      const f32 Kp_ = 0.5f; // proportional control constant
      const f32 Ki_ = 0.001f; // integral control constant
      const f32 MAX_ERROR_SUM = 200.f;
     
      // Open loop gain
      // power_open_loop = SPEED_TO_POWER_OL_GAIN * desiredSpeed + BASE_POWER
      // TODO: Measure this when the head is working! These numbers are completely made up.
      const f32 SPEED_TO_POWER_OL_GAIN = 0.1;
      const f32 BASE_POWER_UP = 0.1;
      const f32 BASE_POWER_DOWN = -0.1;
      
      // Current speed
      f32 radSpeed_ = 0.f;
      
      // Speed and acceleration params
      f32 maxSpeedRad_ = 1.0f;
      f32 accelRad_ = 2.0f;
      f32 approachSpeedRad_ = 0.1f;
      
      // For generating position and speed profile
      VelocityProfileGenerator vpg_;
      
      // Whether or not to recalibrate the motors when they hard limit
      const bool RECALIBRATE_AT_LIMIT = false;
      
      // If head comes within this distance to limit angle, trigger recalibration.
      const f32 RECALIBRATE_LIMIT_ANGLE_THRESH = 0.1f;
      
      // Calibration parameters
      typedef enum {
        HCS_IDLE,
        HCS_LOWER_HEAD,
        HCS_WAIT_FOR_STOP,
        HCS_SET_CURR_ANGLE
      } HeadCalibState;
      
      HeadCalibState calState_ = HCS_IDLE;
      bool isCalibrated_ = false;
      bool limitingDetected_ = false;
      u32 lastHeadMovedTime_us = 0;
      
      const f32 MAX_HEAD_CONSIDERED_STOPPED_RAD_PER_SEC = 0.001;
      
      const u32 HEAD_STOP_TIME = 500000;  // usec
      
      bool enable_ = true;
      
    } // "private" members

    
    void Enable()
    {
      enable_ = true;
    }
    
    void Disable()
    {
      enable_ = false;
    }
    

    void StartCalibrationRoutine()
    {
      PRINT("Starting Head calibration\n");
      
#ifdef SIMULATOR
      // Skipping actual calibration routine in sim due to weird lift behavior when attempting to move it when
      // it's at the joint limit.  The arm flies off the robot!
      isCalibrated_ = true;
      SetDesiredAngle(MIN_HEAD_ANGLE);
#else
      calState_ = HCS_LOWER_HEAD;
      isCalibrated_ = false;
#endif
    }
    
    bool IsCalibrated()
    {
      return isCalibrated_;
    }
    
    
    void ResetLowAnglePosition()
    {
      currentAngle_ = MIN_HEAD_ANGLE;
      HAL::MotorResetPosition(HAL::MOTOR_HEAD);
      prevHalPos_ = HAL::MotorGetPosition(HAL::MOTOR_HEAD);
      isCalibrated_ = true;
    }
    
    bool IsMoving()
    {
      return (ABS(radSpeed_) > MAX_HEAD_CONSIDERED_STOPPED_RAD_PER_SEC);
    }
    
    void CalibrationUpdate()
    {
      if (!isCalibrated_) {
        
        switch(calState_) {
            
          case HCS_IDLE:
            break;
            
          case HCS_LOWER_HEAD:
            power_ = -0.7;
            HAL::MotorSetPower(HAL::MOTOR_HEAD, power_);
            lastHeadMovedTime_us = HAL::GetMicroCounter();
            calState_ = HCS_WAIT_FOR_STOP;
            break;
            
          case HCS_WAIT_FOR_STOP:
            // Check for when head stops moving for 0.2 seconds
            if (!IsMoving()) {
              
              if (HAL::GetMicroCounter() - lastHeadMovedTime_us > HEAD_STOP_TIME) {
                // Turn off motor
                power_ = 0.0;
                HAL::MotorSetPower(HAL::MOTOR_HEAD, power_);
                
                // Set timestamp to be used in next state to wait for motor to "relax"
                lastHeadMovedTime_us = HAL::GetMicroCounter();
                
                // Go to next state
                calState_ = HCS_SET_CURR_ANGLE;
              }
            } else {
              lastHeadMovedTime_us = HAL::GetMicroCounter();
            }
            break;
            
          case HCS_SET_CURR_ANGLE:
            // Wait for motor to relax and then set angle
            if (HAL::GetMicroCounter() - lastHeadMovedTime_us > HEAD_STOP_TIME) {
              PRINT("HEAD Calibrated\n");
              ResetLowAnglePosition();
              //SetDesiredAngle(HEAD_START_ANGLE);
              calState_ = HCS_IDLE;
            }
            break;
        }
      }
      
    }

    
    f32 GetAngleRad()
    {
      return currentAngle_.ToFloat();
    }
    
    void SetAngleRad(f32 angle)
    {
      currentAngle_ = angle;
    }

    f32 GetLastCommandedAngle()
    {
      return desiredAngle_.ToFloat();
    }
    
    void PoseAndSpeedFilterUpdate()
    {
      // Get encoder speed measurements
      f32 measuredSpeed = Cozmo::HAL::MotorGetSpeed(HAL::MOTOR_HEAD);
      
      radSpeed_ = (measuredSpeed *
                   (1.0f - SPEED_FILTERING_COEFF) +
                   (radSpeed_ * SPEED_FILTERING_COEFF));
      
      // Update position
      currentAngle_ += (HAL::MotorGetPosition(HAL::MOTOR_HEAD) - prevHalPos_);
      
#if(DEBUG_HEAD_CONTROLLER)
      PRINT("HEAD FILT: speed %f, speedFilt %f, currentAngle %f, currHalPos %f, prevPos %f, pwr %f\n",
            measuredSpeed, radSpeed_, currentAngle_.ToFloat(), HAL::MotorGetPosition(HAL::MOTOR_HEAD), prevHalPos_, power_);
#endif
      prevHalPos_ = HAL::MotorGetPosition(HAL::MOTOR_HEAD);
    }

    
    void SetAngularVelocity(const f32 rad_per_sec)
    {
      // TODO: Figure out power-to-speed ratio on actual robot. Normalize with battery power?
      f32 power = CLIP(rad_per_sec / HAL::MAX_HEAD_SPEED, -1.0, 1.0);
      HAL::MotorSetPower(HAL::MOTOR_HEAD, power);
      inPosition_ = true;
    }
    
    void SetSpeedAndAccel(f32 max_speed_rad_per_sec, f32 accel_rad_per_sec2)
    {
      maxSpeedRad_ = MAX(ABS(max_speed_rad_per_sec), approachSpeedRad_);
      accelRad_ = accel_rad_per_sec2;
    }
    
    void SetDesiredAngle(f32 angle)
    {
      // Do range check on angle
      angle = CLIP(angle, MIN_HEAD_ANGLE, MAX_HEAD_ANGLE);
      
      
#if(DEBUG_HEAD_CONTROLLER)
      PRINT("HEAD: SetDesiredAngle %f rads\n", desiredAngle_.ToFloat());
#endif
      
      desiredAngle_ = angle;
      angleError_ = desiredAngle_.ToFloat() - currentAngle_.ToFloat();
      angleErrorSum_ = 0.f;
      
      inPosition_ = false;

      if (FLT_NEAR(angleError_,0.f)) {
        inPosition_ = true;
        PRINT("Head: Already at desired position\n");
        return;
      }
      
      // Start profile of head trajectory
      vpg_.StartProfile(radSpeed_, currentAngle_.ToFloat(),
                        maxSpeedRad_, accelRad_,
                        approachSpeedRad_, desiredAngle_.ToFloat(),
                        CONTROL_DT);
      
#if(DEBUG_HEAD_CONTROLLER)
      PRINT("HEAD VPG: startVel %f, startPos %f, maxVel %f, endVel %f, endPos %f\n",
            radSpeed_, currentAngle_.ToFloat(), maxSpeedRad_, approachSpeedRad_, desiredAngle_.ToFloat());
#endif

      
    }
    
    bool IsInPosition(void) {
      return inPosition_;
    }
    
    Result Update()
    {
      CalibrationUpdate();
      
      PoseAndSpeedFilterUpdate();
      
      if (!enable_)
        return RESULT_OK;
      
      
      // Note that a new call to SetDesiredAngle will get
      // Update() working again after it has reached a previous
      // setting.
      if(not inPosition_) {

        // Get the current desired head angle
        vpg_.Step(currDesiredRadVel_, currDesiredAngle_);
        
        // Compute current angle error
        angleError_ = currDesiredAngle_ - currentAngle_.ToFloat();
        
        
        // Open loop value to drive at desired speed
        power_ = currDesiredRadVel_ * SPEED_TO_POWER_OL_GAIN;
        
        // Compute corrective value
        f32 power_corr = (Kp_ * angleError_) + (Ki_ * angleErrorSum_);
        
        // Add base power in the direction of corrective value
        power_ += power_corr + ((power_corr > 0) ? BASE_POWER_UP : BASE_POWER_DOWN);
        
        // Update angle error sum
        angleErrorSum_ += angleError_;
        angleErrorSum_ = CLIP(angleErrorSum_, -MAX_ERROR_SUM, MAX_ERROR_SUM);
        
        // If accurately tracking current desired angle...
        if((ABS(angleError_) < ANGLE_TOLERANCE && desiredAngle_ == currDesiredAngle_)
           || ABS(currentAngle_ - desiredAngle_) < ANGLE_TOLERANCE) {
          power_ = 0.f;
          inPosition_ = true;
#if(DEBUG_HEAD_CONTROLLER)
          PRINT(" HEAD ANGLE REACHED (%f rad)\n", GetAngleRad() );
#endif
        }
        
        
        /*
        // Convert angleError_ to power
        if(ABS(angleError_) < ANGLE_TOLERANCE) {
          angleErrorSum_ = 0.f;
          
          // If desired angle is low position, let it fall through to recalibration
          if (!(RECALIBRATE_AT_LIMIT && desiredAngle_.ToFloat() == MIN_HEAD_ANGLE)) {
            power_ = 0.f;
            
            if (desiredAngle_ == currDesiredAngle_) {
              inPosition_ = true;
#if(DEBUG_HEAD_CONTROLLER)
              PRINT(" HEAD ANGLE REACHED (%f rad)\n", currentAngle_.ToFloat());
#endif
            }
          }
        } else {
          power_ = minPower_ + (Kp_ * angleError_) + (Ki_ * angleErrorSum_);
          angleErrorSum_ += angleError_;
          angleErrorSum_ = CLIP(angleErrorSum_, -MAX_ERROR_SUM, MAX_ERROR_SUM);
          inPosition_ = false;
        }
         */
        
#if(DEBUG_HEAD_CONTROLLER)
        PERIODIC_PRINT(100, "HEAD: currA %f, curDesA %f, desA %f, err %f, errSum %f, pwr %f, spd %f\n",
                       currentAngle_.ToFloat(),
                       currDesiredAngle_,
                       desiredAngle_.ToFloat(),
                       angleError_,
                       angleErrorSum_,
                       power_,
                       radSpeed_);
        PERIODIC_PRINT(100, "  POWER terms: %f  %f\n", (Kp_ * angleError_), (Ki_ * angleErrorSum_))
#endif
        
        power_ = CLIP(power_, -1.0, 1.0);
        
/*
        // If within 5 degrees of MIN_HEAD_ANGLE and the head isn't moving while downward power is applied,
        // assume we've hit the limit and recalibrate.
        if (limitingDetected_ ||
              ((power_ < 0)
               && (desiredAngle_.ToFloat() == MIN_HEAD_ANGLE)
               && (desiredAngle_.ToFloat() == currDesiredAngle_)
               && (ABS(angleError_) < RECALIBRATE_LIMIT_ANGLE_THRESH)
               && NEAR_ZERO(HAL::MotorGetSpeed(HAL::MOTOR_HEAD)))) {
                
          if (!limitingDetected_) {
#if(DEBUG_LIFT_CONTROLLER)
            PRINT("START RECAL HEAD\n");
#endif
            lastHeadMovedTime_us = HAL::GetMicroCounter();
            limitingDetected_ = true;
          } else if (HAL::GetMicroCounter() - lastHeadMovedTime_us > HEAD_STOP_TIME) {
#if(DEBUG_LIFT_CONTROLLER)
            PRINT("END RECAL HEAD\n");
#endif
            ResetLowAnglePosition();
            inPosition_ = true;
          }
          power_ = 0.f;
            
        }
*/
        
        HAL::MotorSetPower(HAL::MOTOR_HEAD, power_);
      } // if not in position
      
      return RESULT_OK;
    }
  } // namespace HeadController
  } // namespace Cozmo
} // namespace Anki

#include "liftController.h"
#include "pickAndPlaceController.h"
#include <math.h>
#include "anki/common/constantsAndMacros.h"
#include "anki/common/robot/config.h"
#include "anki/cozmo/shared/cozmoConfig.h"
#include "anki/cozmo/robot/hal.h"
#include "anki/cozmo/robot/logging.h"
#include "messages.h"
#include "radians.h"
#include "velocityProfileGenerator.h"

#define DEBUG_LIFT_CONTROLLER 0


namespace Anki {
  namespace Cozmo {
    namespace LiftController {


      namespace {

        // How long the lift needs to stop moving for before it is considered to be limited.
        const u32 LIFT_STOP_TIME_MS = 500;

        // Amount of time to allow lift to relax with power == 0, before considering it
        // to have settled enough for recalibration.
        const u32 LIFT_RELAX_TIME_MS = 250;

        const f32 MAX_LIFT_CONSIDERED_STOPPED_RAD_PER_SEC = 0.001;

        const f32 SPEED_FILTERING_COEFF = 0.9f;

        // Used when calling SetDesiredHeight with just a height:
        const f32 DEFAULT_START_ACCEL_FRAC = 0.25f;
        const f32 DEFAULT_END_ACCEL_FRAC   = 0.25f;

        // Only angles greater than this can contribute to error
        // TODO: Find out what this actually is
        const f32 ENCODER_ANGLE_RES = DEG_TO_RAD(0.35f);
        
        // Motor burnout protection
        const f32 BURNOUT_POWER_THRESH = 0.6;
        const u32 BURNOUT_TIME_THRESH_MS = 2000.f;

        // Initialized in Init()
        f32 LIFT_ANGLE_LOW_LIMIT;

#ifdef SIMULATOR
        // For disengaging gripper once the lift has reached its final position
        bool disengageGripperAtDest_ = false;
        f32  disengageAtAngle_ = 0.f;

        f32 Kp_ = 3.f; // proportional control constant
        f32 Kd_ = 0.f;  // derivative gain
        f32 Ki_ = 0.f; // integral control constant
        f32 angleErrorSum_ = 0.f;
        f32 MAX_ERROR_SUM = 10.f;

        // Constant power bias to counter gravity
        const f32 ANTI_GRAVITY_POWER_BIAS = 0.0f;

        // The height of the "fingers"
        const f32 LIFT_FINGER_HEIGHT = 3.8f;
#else
        f32 Kp_ = 3.f;     // proportional control constant
        f32 Kd_ = 2000.f;  // derivative gain
        f32 Ki_ = 0.1f;    // integral control constant
        f32 angleErrorSum_ = 0.f;
        f32 MAX_ERROR_SUM = 5.f;

        // Constant power bias to counter gravity
        const f32 ANTI_GRAVITY_POWER_BIAS = 0.15f;
#endif
        // Angle of the main lift arm.
        // On the real robot, this is the angle between the lower lift joint on the robot body
        // and the lower lift joint on the forklift assembly.
        Radians currentAngle_ = 0.f;
        Radians desiredAngle_ = 0.f;
        f32 desiredHeight_ = 0.f;
        f32 currDesiredAngle_ = 0.f;
        f32 prevAngleError_ = 0.f;
        f32 prevHalPos_ = 0.f;
        bool inPosition_  = true;

        const u32 IN_POSITION_TIME_MS = 100;
        u32 lastInPositionTime_ms_ = 0;

        // Speed and acceleration params
        f32 maxSpeedRad_ = PI_F;
        f32 accelRad_ = 1000.f;

        // For generating position and speed profile
        VelocityProfileGenerator vpg_;

        // Current speed
        f32 radSpeed_ = 0;

        // Currently applied power
        f32 power_ = 0;


        // Calibration parameters
        typedef enum {
          LCS_IDLE,
          LCS_LOWER_LIFT,
          LCS_WAIT_FOR_STOP,
          LCS_SET_CURR_ANGLE
        } LiftCalibState;

        LiftCalibState calState_ = LCS_IDLE;

        bool isCalibrated_ = false;
        u32 lastLiftMovedTime_ms = 0;


        // Whether or not to command anything to motor
        bool enable_ = true;
        
        // If disabled, lift motor is automatically re-enabled at this time if non-zero.
        u32 enableAtTime_ms_ = 0;
        
        // If enableAtTime_ms_ is non-zero, this is the time beyond current time
        // that the motor will be re-enabled if the lift is not moving.
        const u32 REENABLE_TIMEOUT_MS = 2000;

      } // "private" members

      // Returns the angle between the shoulder joint and the wrist joint.
      f32 Height2Rad(f32 height_mm) {
        height_mm = CLIP(height_mm, LIFT_HEIGHT_LOWDOCK, LIFT_HEIGHT_CARRY);
        return asinf((height_mm - LIFT_BASE_POSITION[2] - LIFT_FORK_HEIGHT_REL_TO_ARM_END)/LIFT_ARM_LENGTH);
      }

      f32 Rad2Height(f32 angle) {
        return (sinf(angle) * LIFT_ARM_LENGTH) + LIFT_BASE_POSITION[2] + LIFT_FORK_HEIGHT_REL_TO_ARM_END;
      }


      Result Init()
      {
        // Init consts
        LIFT_ANGLE_LOW_LIMIT = Height2Rad(LIFT_HEIGHT_LOWDOCK);
        return RESULT_OK;
      }


      void Enable()
      {
        if (!enable_) {
          enable_ = true;
          enableAtTime_ms_ = 0;  // Reset auto-enable trigger time

          currDesiredAngle_ = currentAngle_.ToFloat();
          SetDesiredHeight(Rad2Height(currentAngle_.ToFloat()));
        }
      }

      void Disable(bool autoReEnable)
      {
        if (enable_) {
          enable_ = false;

          inPosition_ = true;
          prevAngleError_ = 0.f;
          angleErrorSum_ = 0.f;

          power_ = 0;
          HAL::MotorSetPower(HAL::MOTOR_LIFT, power_);
          
          if (autoReEnable) {
            enableAtTime_ms_ = HAL::GetTimeStamp() + REENABLE_TIMEOUT_MS;
          }
        }
      }


      void ResetAnglePosition(f32 currAngle)
      {
        currentAngle_ = currAngle;
        desiredAngle_ = currentAngle_;
        currDesiredAngle_ = currentAngle_.ToFloat();
        desiredHeight_ = Rad2Height(currAngle);

        HAL::MotorResetPosition(HAL::MOTOR_LIFT);
        prevHalPos_ = HAL::MotorGetPosition(HAL::MOTOR_LIFT);
        isCalibrated_ = true;
      }

      void StartCalibrationRoutine()
      {
        AnkiEvent( 16, "LiftController", 144, "Starting calibration", 0);
        calState_ = LCS_LOWER_LIFT;
        isCalibrated_ = false;
        Messages::SendMotorCalibrationMsg(HAL::MOTOR_LIFT, true);
      }

      bool IsCalibrated()
      {
        return isCalibrated_;
      }


      bool IsMoving()
      {
        return (ABS(radSpeed_) > MAX_LIFT_CONSIDERED_STOPPED_RAD_PER_SEC);
      }


      void CalibrationUpdate()
      {
        if (!isCalibrated_) {

          switch(calState_) {

            case LCS_IDLE:
              break;

            case LCS_LOWER_LIFT:
              power_ = -0.3;
              HAL::MotorSetPower(HAL::MOTOR_LIFT, power_);
              lastLiftMovedTime_ms = HAL::GetTimeStamp();
              calState_ = LCS_WAIT_FOR_STOP;
              break;

            case LCS_WAIT_FOR_STOP:
              // Check for when lift stops moving for 0.2 seconds
              if (!IsMoving()) {

                if (HAL::GetTimeStamp() - lastLiftMovedTime_ms > LIFT_STOP_TIME_MS) {
                  // Turn off motor
                  power_ = 0;  // Not strong enough to lift motor, but just enough to unwind backlash. Not sure if this is actually helping.
                  HAL::MotorSetPower(HAL::MOTOR_LIFT, power_);

                  // Set timestamp to be used in next state to wait for motor to "relax"
                  lastLiftMovedTime_ms = HAL::GetTimeStamp();

                  // Go to next state
                  calState_ = LCS_SET_CURR_ANGLE;
                }
              } else {
                lastLiftMovedTime_ms = HAL::GetTimeStamp();
              }
              break;

            case LCS_SET_CURR_ANGLE:
              // Wait for motor to relax and then set angle
              if (HAL::GetTimeStamp() - lastLiftMovedTime_ms > LIFT_RELAX_TIME_MS) {
                AnkiEvent( 16, "LiftController", 91, "Calibrated", 0);
                ResetAnglePosition(LIFT_ANGLE_LOW_LIMIT);
                calState_ = LCS_IDLE;
                Messages::SendMotorCalibrationMsg(HAL::MOTOR_LIFT, false);
              }
              break;
          }
        }
      }


      f32 GetLastCommandedHeightMM()
      {
        return desiredHeight_;
      }

      f32 GetHeightMM()
      {
        return Rad2Height(currentAngle_.ToFloat());
      }

      f32 GetAngleRad()
      {
        return currentAngle_.ToFloat();
      }

      void SetMaxSpeedAndAccel(const f32 max_speed_rad_per_sec, const f32 accel_rad_per_sec2)
      {
        maxSpeedRad_ = ABS(max_speed_rad_per_sec);
        accelRad_ = accel_rad_per_sec2;
      }

      void SetMaxLinearSpeedAndAccel(const f32 max_speed_mm_per_sec, const f32 accel_mm_per_sec2)
      {
        maxSpeedRad_ = max_speed_mm_per_sec / LIFT_ARM_LENGTH;
        accelRad_    = accel_mm_per_sec2 / LIFT_ARM_LENGTH;
      }

      void GetMaxSpeedAndAccel(f32 &max_speed_rad_per_sec, f32 &accel_rad_per_sec2)
      {
        max_speed_rad_per_sec = maxSpeedRad_;
        accel_rad_per_sec2 = accelRad_;
      }

      void SetLinearVelocity(const f32 mm_per_sec)
      {
        const f32 rad_per_sec = Height2Rad(mm_per_sec);
        SetAngularVelocity(rad_per_sec);
      }

      void SetAngularVelocity(const f32 rad_per_sec)
      {
        // Command a target height based on the sign of the desired speed
        f32 targetHeight = 0;
        if (rad_per_sec > 0) {
          targetHeight = LIFT_HEIGHT_CARRY;
          maxSpeedRad_ = rad_per_sec;
        } else if (rad_per_sec < 0) {
          targetHeight = LIFT_HEIGHT_LOWDOCK;
          maxSpeedRad_ = rad_per_sec;
        } else {
          // Compute the expected height if we were to start slowing down now
          f32 radToStop = 0.5f*(radSpeed_*radSpeed_) / accelRad_;
          if (radSpeed_ < 0) {
            radToStop *= -1;
          }
          targetHeight = CLIP(Rad2Height( currentAngle_.ToFloat() + radToStop ), LIFT_HEIGHT_LOWDOCK, LIFT_HEIGHT_CARRY);
          //PRINT("Stopping: radSpeed %f, accelRad %f, radToStop %f, currentAngle %f, targetHeight %f\n",
          //      radSpeed_, accelRad_, radToStop, currentAngle_.ToFloat(), targetHeight);
        }
        SetDesiredHeight(targetHeight);
      }

      f32 GetAngularVelocity()
      {
        return radSpeed_;
      }

      void PoseAndSpeedFilterUpdate()
      {
        // Get encoder speed measurements
        f32 measuredSpeed = Cozmo::HAL::MotorGetSpeed(HAL::MOTOR_LIFT);

        radSpeed_ = (measuredSpeed *
                     (1.0f - SPEED_FILTERING_COEFF) +
                     (radSpeed_ * SPEED_FILTERING_COEFF));

        // Update position
        currentAngle_ += (HAL::MotorGetPosition(HAL::MOTOR_LIFT) - prevHalPos_);

#if(DEBUG_LIFT_CONTROLLER)
        AnkiDebug( 16, "LiftController", 308, "LIFT FILT: speed %f, speedFilt %f, currentAngle %f, currHalPos %f, prevPos %f, pwr %f\n", 6,
              measuredSpeed, radSpeed_, currentAngle_.ToFloat(), HAL::MotorGetPosition(HAL::MOTOR_LIFT), prevHalPos_, power_);
#endif
        prevHalPos_ = HAL::MotorGetPosition(HAL::MOTOR_LIFT);
      }

      void SetDesiredHeight(f32 height_mm)
      {
        //PRINT("LiftHeight: %fmm, speed %f, accel %f\n", height_mm, maxSpeedRad_, accelRad_);
        SetDesiredHeight(height_mm, DEFAULT_START_ACCEL_FRAC, DEFAULT_END_ACCEL_FRAC, 0);
      }

      static void SetDesiredHeight_internal(f32 height_mm, f32 acc_start_frac, f32 acc_end_frac, f32 duration_seconds)
      {

        // Do range check on height
        const f32 newDesiredHeight = CLIP(height_mm, LIFT_HEIGHT_LOWDOCK, LIFT_HEIGHT_CARRY);

#ifdef SIMULATOR
        if(!HAL::IsGripperEngaged()) {
          // If the new desired height will make the lift move upward, turn on
          // the gripper's locking mechanism so that we might pick up a block as
          // it goes up
          if(newDesiredHeight > desiredHeight_) {
            HAL::EngageGripper();
          }
        }
        else {
          // If we're moving the lift down and the end goal is at low-place or
          // high-place height, disengage the gripper when we get there
          if(newDesiredHeight < desiredHeight_ &&
             (newDesiredHeight == LIFT_HEIGHT_LOWDOCK ||
              newDesiredHeight == LIFT_HEIGHT_HIGHDOCK))
          {
            disengageGripperAtDest_ = true;
            disengageAtAngle_ = Height2Rad(newDesiredHeight + 3.f*LIFT_FINGER_HEIGHT);
          }
          else {
            disengageGripperAtDest_ = false;
          }
        }
#endif
        // Check if already at desired height
        if (inPosition_ &&
            (Height2Rad(newDesiredHeight) == desiredAngle_) &&
            (ABS((desiredAngle_ - currentAngle_).ToFloat()) < LIFT_ANGLE_TOL) ) {
          #if(DEBUG_LIFT_CONTROLLER)
          AnkiDebug( 16, "LiftController", 145, "Already at desired height %f", 1, newDesiredHeight);
          #endif
          return;
        }

        desiredHeight_ = newDesiredHeight;
        desiredAngle_ = Height2Rad(desiredHeight_);

        // Convert desired height into the necessary angle:
#if(DEBUG_LIFT_CONTROLLER)
        AnkiDebug( 16, "LiftController", 146, "LIFT DESIRED HEIGHT: %f mm (curr height %f mm), duration = %f s", 3, desiredHeight_, GetHeightMM(), duration_seconds);
#endif


        f32 startRadSpeed = radSpeed_;
        f32 startRad = currDesiredAngle_;
        if (!inPosition_) {
          vpg_.Step(startRadSpeed, startRad);
        }

        lastInPositionTime_ms_ = 0;
        inPosition_ = false;


        bool res = false;
        if (duration_seconds > 0) {
          res = vpg_.StartProfile_fixedDuration(startRad, startRadSpeed, acc_start_frac*duration_seconds,
                                                   desiredAngle_.ToFloat(), acc_end_frac*duration_seconds,
                                                   MAX_LIFT_SPEED_RAD_PER_S,
                                                   MAX_LIFT_ACCEL_RAD_PER_S2,
                                                   duration_seconds,
                                                   CONTROL_DT);

          AnkiConditionalWarn(res, 16, "LiftController", 147, "FAIL: VPG (fixedDuration): startVel %f, startPos %f, acc_start_frac %f, acc_end_frac %f, endPos %f, duration %f. Trying VPG without fixed duration.\n", 6,
                  startRadSpeed, startRad, acc_start_frac, acc_end_frac, desiredAngle_.ToFloat(), duration_seconds);
        }
        if (!res) {
          vpg_.StartProfile(startRadSpeed, startRad,
                            maxSpeedRad_, accelRad_,
                            0, desiredAngle_.ToFloat(),
                            CONTROL_DT);
        }

#if DEBUG_LIFT_CONTROLLER
        AnkiDebug( 16, "LiftController", 148, "VPG (fixedDuration): startVel %f, startPos %f, acc_start_frac %f, acc_end_frac %f, endPos %f, duration %f\n", 6,
              startRadSpeed, startRad, acc_start_frac, acc_end_frac, desiredAngle_.ToFloat(), duration_seconds);
#endif
      } // SetDesiredHeight_internal


      void SetDesiredHeight(f32 height_mm, f32 acc_start_frac, f32 acc_end_frac, f32 duration_seconds)
      {
        SetDesiredHeight_internal(height_mm, acc_start_frac, acc_end_frac, duration_seconds);
      }


      f32 GetDesiredHeight()
      {
        return desiredHeight_;
      }

      bool IsInPosition(void) {
        return inPosition_;
      }

      // Check for conditions that could lead to motor burnout.
      // If motor is powered at greater than BURNOUT_POWER_THRESH for more than BURNOUT_TIME_THRESH_MS, stop it!
      // If the lift was in position, assuming that someone is messing with the motor.
      // If the lift was not in position, assuming that it's mis-calibrated and it's hitting the low or high hard limit. Do calibration.
      // Returns true if a protection action was triggered.
      bool MotorBurnoutProtection() {
        
        static u32 potentialBurnoutStartTime_ms = 0;

        if (ABS(power_) < BURNOUT_POWER_THRESH) {
          potentialBurnoutStartTime_ms = 0;
          return false;
        }
        
        if (potentialBurnoutStartTime_ms == 0) {
          potentialBurnoutStartTime_ms = HAL::GetTimeStamp();
        } else if (HAL::GetTimeStamp() - potentialBurnoutStartTime_ms > BURNOUT_TIME_THRESH_MS) {
          if (IsInPosition()) {
            AnkiWarn( 16, "LiftController", 149, "burnout protection triggered. Stop messing with the lift! Going limp until you do!", 0);
            Disable(true);
            return true;
          } else {
            AnkiWarn( 16, "LiftController", 150, "burnout protection triggered. Recalibrating.", 0);
            StartCalibrationRoutine();
            return true;
          }
        }
        
        return false;
      }
      
      
      Result Update()
      {
        // Update routine for calibration sequence
        CalibrationUpdate();

        PoseAndSpeedFilterUpdate();

        // If disabled, do not activate motors
        if(!enable_) {
          if (enableAtTime_ms_ == 0) {
            return RESULT_OK;
          }
          
          // Auto-enable check
          if (IsMoving()) {
            enableAtTime_ms_ = HAL::GetTimeStamp() + REENABLE_TIMEOUT_MS;
            return RESULT_OK;
          } else if (HAL::GetTimeStamp() >= enableAtTime_ms_) {
            AnkiInfo( 16, "LiftController", 151, "Lift auto-enabled", 0);
            Enable();
          } else {
            return RESULT_OK;
          }
        }

        if (!IsCalibrated()) {
          return RESULT_OK;
        }
        
        if (MotorBurnoutProtection()) {
          return RESULT_OK;
        }


#if SIMULATOR
        if (disengageGripperAtDest_ && currentAngle_.ToFloat() < disengageAtAngle_) {
          HAL::DisengageGripper();
          disengageGripperAtDest_ = false;
        }
#endif



        // Get the current desired lift angle
        if (currDesiredAngle_ != desiredAngle_) {
          f32 currDesiredRadVel;
          vpg_.Step(currDesiredRadVel, currDesiredAngle_);
        }

        // Compute position error
        // Ignore if it's less than encoder resolution
        f32 angleError = currDesiredAngle_ - currentAngle_.ToFloat();
        if (ABS(angleError) < ENCODER_ANGLE_RES) {
          angleError = 0;
        }


        // Compute power
        power_ = ANTI_GRAVITY_POWER_BIAS + (Kp_ * angleError) + (Kd_ * (angleError - prevAngleError_) * CONTROL_DT) + (Ki_ * angleErrorSum_);

        // Update error terms
        prevAngleError_ = angleError;
        angleErrorSum_ += angleError;
        angleErrorSum_ = CLIP(angleErrorSum_, -MAX_ERROR_SUM, MAX_ERROR_SUM);



        // If accurately tracking current desired angle...
        if((ABS(angleError) < LIFT_ANGLE_TOL) && (desiredAngle_ == currDesiredAngle_)) {

          // Keep angleErrorSum from accumulating once we're in position
          angleErrorSum_ -= angleError;
          
          if (lastInPositionTime_ms_ == 0) {
            lastInPositionTime_ms_ = HAL::GetTimeStamp();
          } else if (HAL::GetTimeStamp() - lastInPositionTime_ms_ > IN_POSITION_TIME_MS) {

            inPosition_ = true;
#if(DEBUG_LIFT_CONTROLLER)
            AnkiDebug( 16, "LiftController", 152, " LIFT HEIGHT REACHED (%f mm)", 1, GetHeightMM());
#endif
          }
        } else {
          lastInPositionTime_ms_ = 0;
        }


#if(DEBUG_LIFT_CONTROLLER)
        PERIODIC_PRINT(100, "LIFT: currA %f, curDesA %f, currVel %f, desA %f, err %f, errSum %f, inPos %d, pwr %f\n",
                       currentAngle_.ToFloat(),
                       currDesiredAngle_,
                       radSpeed_,
                       desiredAngle_.ToFloat(),
                       angleError,
                       angleErrorSum_,
                       inPosition_ ? 1 : 0,
                       power_);
        PERIODIC_PRINT(100, "  POWER terms: %f  %f\n", (Kp_ * angleError_), (Ki_ * angleErrorSum_))
#endif

        power_ = CLIP(power_, -1.0, 1.0);
        HAL::MotorSetPower(HAL::MOTOR_LIFT, power_);

        return RESULT_OK;
      }

      void SetGains(const f32 kp, const f32 ki, const f32 kd, const f32 maxIntegralError)
      {
        Kp_ = kp;
        Ki_ = ki;
        Kd_ = kd;
        MAX_ERROR_SUM = maxIntegralError;
        AnkiInfo( 16, "LiftController", 153, "New lift gains: kp = %f, ki = %f, kd = %f, maxSum = %f", 4,
              Kp_, Ki_, Kd_, MAX_ERROR_SUM);
      }

      void Stop()
      {
        SetAngularVelocity(0);
      }

    } // namespace LiftController
  } // namespace Cozmo
} // namespace Anki

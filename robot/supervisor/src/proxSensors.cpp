#include "proxSensors.h"
#include "trig_fast.h"
#include "headController.h"
#include "liftController.h"
#include "animationController.h"
#include "pickAndPlaceController.h"
#include "wheelController.h"
#include "imuFilter.h"
#include "anki/cozmo/robot/hal.h"
#include "clad/robotInterface/messageRobotToEngine_send_helper.h"
#include "localization.h"
#ifdef TARGET_K02
#include "anki/cozmo/robot/logging.h"
#else
#include "anki/common/robot/errorHandling.h"
#endif


namespace Anki {
  namespace Cozmo {
    namespace ProxSensors {

      namespace {
        //f32 _proxLeft, _proxFwd, _proxRight;
        //bool _prevBlockedSides = true, _prevBlockedFwd = false;
        bool _blockedSides, _blockedFwd;

        //const f32 FILT_COEFF = 0.05f;

        // The amount of time to consider a sensor blocked or unblocked
        // after it has moved into a blocked region. This delay is to
        // account for filtering time.
        //const TimeStamp_t BLOCKED_TIMEOUT = 1000; //ms
        //TimeStamp_t _fwdBlockedTransitionTime = 0;
        //TimeStamp_t _sidesBlockedTransitionTime = 0;

        bool _enableCliffDetect = true;
        bool _wasCliffDetected = false;
        
        const u32 PROX_EVENT_CYCLE_PERIOD = 6;

      } // "private" namespace


      Result Update()
      {
        Result retVal = RESULT_OK;
/*
        // Get current readings and filter
        HAL::ProximityValues currProxVals;
        HAL::GetProximity(&currProxVals);

        switch(currProxVals.latest)
        {
          case HAL::IRleft:
            _proxLeft = (FILT_COEFF * currProxVals.left) + ((1.f - FILT_COEFF) * _proxLeft);
            break;

          case HAL::IRright:
            _proxRight = (FILT_COEFF * currProxVals.right) + ((1.f - FILT_COEFF) * _proxRight);
            break;

          case HAL::IRforward:
            _proxFwd = (FILT_COEFF * currProxVals.forward) + ((1.f - FILT_COEFF) * _proxFwd);
            break;

          default:
            AnkiError( 9, "ProxSensors.Update.BadLatestValue", 76, "Got invalid/unhandled value for ProximityValues.latest.\n", 0);
            return RESULT_FAIL;

        } // switch(currProxVals.latest)


        // TODO: Logic for when proximity sensors are blocked by the lift.
        //       Eventually, this should be computed from actual geometry,
        //       but for now just have a few very conservative cases.

        // If lift is in low carry position all readings always valid since
        // nothing can block the sensors.
        // If lift is in carry position, assume readings below a certain
        // head angle are valid.
        // If lift is any other position, assume all readings are invalid.

        f32 headAngle = HeadController::GetAngleRad();
        TimeStamp_t currTime = HAL::GetTimeStamp();
        bool currBlockedFwd = false, currBlockedSides = false;



        if (LiftController::IsMoving()) {
          currBlockedSides = currBlockedFwd = true;

        } else if (LiftController::GetDesiredHeight() == LIFT_HEIGHT_LOWDOCK) {

          if (headAngle > -0.3f) {
            currBlockedSides = false;
          } else {
            currBlockedSides = true;
          }
          currBlockedFwd = false;

        } else if (LiftController::GetDesiredHeight() == LIFT_HEIGHT_CARRY) {

          if (headAngle < 0.1f) {
            currBlockedFwd = false;
            currBlockedSides = false;
          } else if (headAngle > 0.65f) {
            currBlockedFwd = false;
            currBlockedSides = true;
          } else {
            currBlockedFwd = true;
            currBlockedSides = true;
          }

        } else {
          currBlockedSides = currBlockedFwd = true;
        }


        // Update blocked state transition time
        if (currBlockedFwd != _prevBlockedFwd) {
          _fwdBlockedTransitionTime = currTime;
        }
        if (currBlockedSides != _prevBlockedSides) {
          _sidesBlockedTransitionTime = currTime;
        }

        // Transitions to blocked state is immediate. (Sensors can be occluded as the lift is moving)
        // Transitions to unblocked state is subject to a delay. (We don't want to immediately validate sensors readings off the lift as it just starts to move out of view.)
        if (currBlockedFwd) {
          _blockedFwd = true;
        }
        if (currBlockedSides) {
          _blockedSides = true;
        }

        // Set blocked state based on timeout
        if (currTime - _fwdBlockedTransitionTime > BLOCKED_TIMEOUT) {
          _blockedFwd = currBlockedFwd;
        }
        if (currTime - _sidesBlockedTransitionTime > BLOCKED_TIMEOUT) {
          _blockedSides = currBlockedSides;
        }

        // Update prevBlocked state
        _prevBlockedFwd = currBlockedFwd;
        _prevBlockedSides = currBlockedSides;
 */

        // FAKING obstacle detection via prox sensor.
        // TODO: This will eventually be done entirely on the engine using images.
#ifndef TARGET_K02
        static u32 proxCycleCnt = 0;
        if (++proxCycleCnt == PROX_EVENT_CYCLE_PERIOD) {
          u8 proxVal = HAL::GetForwardProxSensor();
          if (proxVal > 0) {
            ProxObstacle msg;
            msg.distance_mm = proxVal;
            RobotInterface::SendMessage(msg);
          }
          proxCycleCnt = 0;
        }
#endif        

        /////// Cliff detect reaction ///////
        bool movingForward = WheelController::GetAverageFilteredWheelSpeed() > WheelController::WHEEL_SPEED_CONSIDER_STOPPED_MM_S;
        if (_enableCliffDetect) {
          if (HAL::IsCliffDetected() &&
              !IMUFilter::IsPickedUp() &&
              movingForward &&
              !_wasCliffDetected) {

            // TODO (maybe): Check for cases where cliff detect should not stop motors
            // 1) Turning in place
            // 2) Driving over something (i.e. pitch is higher than some degrees).
            PRINT("Stopping due to cliff\n");

            // Stop all motors and animations
            PickAndPlaceController::Reset();
#ifndef TARGET_K02
            AnimationController::Clear();
#endif
            // Send cliff detected message to engine
            CliffEvent msg;
            Radians angle;
            Localization::GetCurrentMatPose(msg.x_mm, msg.y_mm, angle);
            msg.detected = true;
            msg.angle_rad = angle.ToFloat();
            RobotInterface::SendMessage(msg);

            _wasCliffDetected = true;
          } else if ((!HAL::IsCliffDetected() || IMUFilter::IsPickedUp()) &&
                     _wasCliffDetected) {
            CliffEvent msg;
            msg.detected = false;
            RobotInterface::SendMessage(msg);

            _wasCliffDetected = false;
          }
        }

        return retVal;

      } // Update()


      void EnableCliffDetector(bool enable) {
        _enableCliffDetect = enable;
      }


      // Returns the proximity sensor values
      void GetValues(u8 &left, u8 &forward, u8 &right)
      {
        // TEMP: Until we actually have prox sensors
        left=forward=right=0;

        //PERIODIC_PRINT(200, "PROX: %f  %f  %f (%d %d %d) (%d %d %d)\n",
        //               _proxLeft, _proxFwd, _proxRight,
        //               left, forward, right,
        //               _blockedSides, _blockedFwd, _blockedSides);
      }


      bool IsSideBlocked() {return _blockedSides;}
      bool IsForwardBlocked() {return _blockedFwd;}


    } // namespace ProxSensors
  } // namespace Cozmo
} // namespace Anki

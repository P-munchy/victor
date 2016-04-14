#include "proxSensors.h"
#include "trig_fast.h"
#include "headController.h"
#include "liftController.h"
#include "animationController.h"
#include "pickAndPlaceController.h"
#include "steeringController.h"
#include "wheelController.h"
#include "imuFilter.h"
#include "anki/cozmo/robot/hal.h"
#include "clad/robotInterface/messageRobotToEngine_send_helper.h"
#include "localization.h"
#include "anki/cozmo/robot/logging.h"


namespace Anki {
  namespace Cozmo {
    namespace ProxSensors {

      namespace {

        // Cliff sensor
        bool _enableCliffDetect = true;
        bool _wasCliffDetected = false;

        CliffEvent _cliffMsg;
        TimeStamp_t _pendingCliffEvent = 0;
        TimeStamp_t _pendingUncliffEvent = 0;
        const u32 CLIFF_EVENT_DELAY_MS = 500;
        
        // Forward prox sensor
        u8 _lastForwardObstacleDetectedDist = FORWARD_COLLISION_SENSOR_LENGTH_MM + 1;
        const u32 PROX_EVENT_CYCLE_PERIOD = 6;
        
      } // "private" namespace

      void QueueCliffEvent(f32 x, f32 y, f32 angle) {
        if (_pendingCliffEvent == 0) {
          _pendingCliffEvent = HAL::GetTimeStamp() + CLIFF_EVENT_DELAY_MS;
          _cliffMsg.x_mm = x;
          _cliffMsg.y_mm = y;
          _cliffMsg.angle_rad = angle;
          _cliffMsg.detected = true;
        }
      }
      
      // If no cliff event is queued, queue this undetected
      // event to go out immediately.
      // If a cliff detected event is already queued,
      // just cancel it.
      void QueueUncliffEvent() {
        if (_pendingCliffEvent == 0) {
          _pendingUncliffEvent = HAL::GetTimeStamp();
        } else {
          _pendingCliffEvent = 0;
        }
      }

      // Stops robot if cliff detected as wheels are driving forward.
      // Delays cliff event to allow pickup event to cancel it in case the
      // reason for the cliff was actually a pickup.
      void UpdateCliff()
      {
        bool isDrivingForward = WheelController::GetAverageFilteredWheelSpeed() > WheelController::WHEEL_SPEED_CONSIDER_STOPPED_MM_S;
        if (_enableCliffDetect) {
          if (HAL::IsCliffDetected() &&
              !IMUFilter::IsPickedUp() &&
              isDrivingForward &&
              !_wasCliffDetected) {
            
            // TODO (maybe): Check for cases where cliff detect should not stop motors
            // 1) Turning in place
            // 2) Driving over something (i.e. pitch is higher than some degrees).
            AnkiEvent( 20, "Cliff", 157, "Stopping due to cliff", 0);
            
            // Stop all motors and animations
            PickAndPlaceController::Reset();
            SteeringController::ExecuteDirectDrive(0,0);
            
            // Send stopped message
            RobotInterface::RobotStopped msg;
            RobotInterface::SendMessage(msg);
            
#ifndef TARGET_K02
            // TODO: On K02, need way to tell Espressif to cancel animations
            AnimationController::Clear();
#endif
            // Queue cliff detected message
            QueueCliffEvent(Localization::GetCurrPose_x(), Localization::GetCurrPose_y(), Localization::GetCurrPose_angle().ToFloat());
            
            _wasCliffDetected = true;
          } else if ((!HAL::IsCliffDetected() || IMUFilter::IsPickedUp()) &&
                     _wasCliffDetected) {
            QueueUncliffEvent();
            _wasCliffDetected = false;
          }
        }
        
        // Clear queued cliff events if pickedup
        if (IMUFilter::IsPickedUp()) {
          _pendingCliffEvent = 0;
          _pendingUncliffEvent = 0;
        }
        
        // Send queued cliff event
        if (_pendingCliffEvent != 0 && HAL::GetTimeStamp() >= _pendingCliffEvent) {
          RobotInterface::SendMessage(_cliffMsg);
          _pendingCliffEvent = 0;
        }
        
        // Send queued uncliff event
        if (_pendingUncliffEvent != 0 && HAL::GetTimeStamp() >= _pendingUncliffEvent) {
          _cliffMsg.detected = false;
          RobotInterface::SendMessage(_cliffMsg);
          _pendingUncliffEvent = 0;
        }
      }
      
      
      Result Update()
      {
        Result retVal = RESULT_OK;

        // FAKING obstacle detection via prox sensor.
        // TODO: This will eventually be done entirely on the engine using images.
#ifdef SIMULATOR
        {
          
          if ( HAL::RadioIsConnected() )
          {
            static u32 proxCycleCnt = 0;
            if (++proxCycleCnt == PROX_EVENT_CYCLE_PERIOD) {
              u8 proxVal = HAL::GetForwardProxSensorCurrentValue();
              const bool eventChanged = (proxVal != _lastForwardObstacleDetectedDist);
              if ( eventChanged )
              {
                // send changes in events
                ProxObstacle msg;
                msg.distance_mm = proxVal;
                RobotInterface::SendMessage(msg);
                _lastForwardObstacleDetectedDist = proxVal;
              }
              proxCycleCnt = 0;
            } // period
          }
          else {
            // reset last since we are not connected anymore
            _lastForwardObstacleDetectedDist = FORWARD_COLLISION_SENSOR_LENGTH_MM + 1;
          }
          
        }
#endif        

        UpdateCliff();
        
        return retVal;

      } // Update()


      void EnableCliffDetector(bool enable) {
        _enableCliffDetect = enable;
      }


    } // namespace ProxSensors
  } // namespace Cozmo
} // namespace Anki

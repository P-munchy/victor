/**
 * File: imuFilter.h
 *
 * Author: Kevin Yoon
 * Created: 4/1/2014
 *
 * Description:
 *
 *   Filter for gyro and accelerometer
 *
 * Copyright: Anki, Inc. 2014
 *
 **/

#ifndef IMU_FILTER_H_
#define IMU_FILTER_H_

#include "coretech/common/shared/radians.h"
#include "coretech/common/shared/types.h"
#include "anki/cozmo/robot/hal.h"

namespace Anki {

  namespace Cozmo {

    namespace IMUFilter {

      Result Init();

      // TODO: Add if/when needed?
      // ReturnCode Init();
      Result Update();

      // Returns the latest IMU data read in the last Update() call.
      HAL::IMU_DataStructure GetLatestRawData();
      
      const f32* GetBiasCorrectedGyroData();

      // Rotation (or "yaw") in radians. Turning left is positive.
      f32 GetRotation();

      // Rotation speed in rad/sec
      f32 GetRotationSpeed();

      // Angle above gravity horizontal
      f32 GetPitch();

      // Starts recording a buffer of data for the specified time and sends it to basestation
      void RecordAndSend( const u32 length_ms );

      // If false, IsPickedUp() always returns false
      void EnablePickupDetect(bool enable);

      // Returns true when pickup detected.
      // Pickup detect is reset when the robot stops moving.
      bool IsPickedUp();

      // Returns true if falling detected
      bool IsFalling();
      
      // Enables/Disables the brace reaction when falling is detected
      void EnableBraceWhenFalling(bool enable);
      
      // Whether or not we have finished accumulating enough readings of the gyro offset
      // while the robot is not moving.
      // SyncTimeAck is blocked until this completes!
      bool IsBiasFilterComplete();

      // Get pointer to array of gyro biases
      const f32* GetGyroBias();
      
    } // namespace IMUFilter
  } // namespace Cozmo
} // namespace Anki

#endif // IMU_FILTER_H_

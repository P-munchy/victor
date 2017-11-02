/**
 * File: cliffSensorComponent.h
 *
 * Author: Matt Michini
 * Created: 6/27/2017
 *
 * Description: Component for managing cliff sensors and cliff sensor-related functions such as carpet detection (and
 *              corresponding cliff threshold adjustment), 'suspicious' cliff detection, and raw data access.
 *
 * Copyright: Anki, Inc. 2017
 *
 **/

#ifndef __Anki_Cozmo_Basestation_Components_CliffSensorComponent_H__
#define __Anki_Cozmo_Basestation_Components_CliffSensorComponent_H__

#include "engine/components/sensors/iSensorComponent.h"

#include "clad/types/proxMessages.h"
#include "clad/types/robotStatusAndActions.h"

#include "util/helpers/templateHelpers.h"

namespace Anki {
class Pose3d;
namespace Cozmo {

class CliffSensorComponent : public ISensorComponent
{
public:
  static const int kNumCliffSensors = static_cast<int>(CliffSensor::CLIFF_COUNT);
  
  using CliffSensorDataArray = std::array<uint16_t, kNumCliffSensors>;
  static_assert(std::is_same<CliffSensorDataArray, decltype(RobotState::cliffDataRaw)>::value, "CliffSensorDataArray must be same as type used in RobotState");

  CliffSensorComponent(Robot& robot);
  ~CliffSensorComponent() = default;

protected:
  virtual void UpdateInternal(const RobotState& msg) override;
  
  virtual std::string GetLogHeader() override;
  virtual std::string GetLogRow() override;

public:
  
  bool IsCliffSensorEnabled() const { return _enableCliffSensor; }
  void SetEnableCliffSensor(const bool val) { _enableCliffSensor = val; }
  
  bool IsCliffDetected() const { return _isCliffDetected; }
  void SetCliffDetected(const bool isCliffDetected) { _isCliffDetected = isCliffDetected; }
  
  bool IsCliffDetectedStatusBitOn() const { return _cliffDetectedStatusBitOn; }
  
  // Adjusts cliff threshold if necessary
  void UpdateCliffDetectThresholds();
  
  // Returns current threshold for cliff detection for the given cliff sensor
  uint16_t GetCliffDetectThreshold(unsigned int ind) const;
  
  CliffSensorDataArray GetCliffDetectThresholds() const { return _cliffDetectThresholds; }
  
  // Sends message to the robot process containing all of the current cliff detection thresholds
  void SendCliffDetectThresholdsToRobot();
  
  // index corresponds to CliffSensor enum
  // TODO - remove the default value =0 here (VIC-138)
  uint16_t  GetCliffDataRaw(unsigned int ind = 0) const;
  
  // Compute the estimated pose of the cliff obstacle for the given cliff event.
  bool ComputeCliffPose(const CliffEvent& cliffEvent, Pose3d& cliffPose) const;
  
private:
  
  void QueueCliffThresholdUpdate();
  
  void SetCliffDetectThreshold(unsigned int ind, uint16_t newThresh);
  
  bool _enableCliffSensor = true;
  bool _isCliffDetected = false;
  
  // True if the robot is reporting CLIFF_DETECTED in its status message
  bool _cliffDetectedStatusBitOn = false;
  
  uint32_t _lastMsgTimestamp = 0;
  
  // Cliff detection thresholds for each sensor
  CliffSensorDataArray _cliffDetectThresholds;
  
  // Raw cliff sensor data from robot process
  CliffSensorDataArray _cliffDataRaw;
  
  // Filtered cliff sensor data, which smooths out noise in the data
  std::array<f32, kNumCliffSensors> _cliffDataFilt;
  
  // Minimum observed cliff sensor values (used to adaptively adjust detection thresholds)
  CliffSensorDataArray _cliffMinObserved;
  
  // This is the allowed delta above the minimum-ever-seen cliff sensor value below which cliff detection is triggered.
  // In other words, if the lowest-ever-seen value for a given cliff sensor is 90, and _cliffDetectAllowedDelta is 40, then
  // the cliff detection threshold will be set at 130 (90+40). If the raw cliff value ever falls below the threshold, cliff
  // detection is triggered. This value can also be dynamically adjusted if it is suspected that we are driving on a surface
  // with poor IR reflectivity (e.g. a carpet).
  uint16_t _cliffDetectAllowedDelta;

  uint32_t _nextCliffThresholdUpdateToRobot_ms = 0;
  
};


} // Cozmo namespace
} // Anki namespace

#endif // __Anki_Cozmo_Basestation_Components_CliffSensorComponent_H__

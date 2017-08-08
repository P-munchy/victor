//
//  robotGyroDriftDetector.h
//
//  Created by Matt Michini on 2017-05-15.
//  Copyright (c) 2017 Anki, Inc. All rights reserved.
//

#ifndef __Anki_Cozmo_RobotGyroDriftDetector_H__
#define __Anki_Cozmo_RobotGyroDriftDetector_H__

#include "anki/common/basestation/math/point.h"
#include "anki/common/shared/radians.h"
#include "anki/common/types.h"

#include "util/helpers/noncopyable.h"

namespace Anki {
namespace Cozmo {
  
// forward decl:
class Robot;
struct RobotState;
  
class RobotGyroDriftDetector : private Util::noncopyable
{
public:
  RobotGyroDriftDetector(const Robot& robot);
  
  // 'Legacy' drift detection, which uses robot's computed pose to determine
  // if gyro drift is occurring.
  [[deprecated]]
  void DetectGyroDrift(const RobotState& msg);
  
  // Uses raw IMU data to detect bias in the gyro readings. Any bias should have
  // been corrected on the robot before being sent to engine.
  void DetectBias(const RobotState& msg);
  
  void ResetBiasDetector();
  
private:
  
  const Robot& _robot;
  
  // For 'legacy' DetectGyroDrift:
  bool          _gyroDriftReported = false;
  PoseFrameID_t _startPoseFrameId = 0;
  Radians       _startAngle_rad;
  f32           _startGyroZ_rad_per_sec = 0.f;
  TimeStamp_t   _startTime_ms = 0;
  f32           _cumSumGyroZ_rad_per_sec = 0.f;
  f32           _minGyroZ_rad_per_sec = 0.f;
  f32           _maxGyroZ_rad_per_sec = 0.f;
  u32           _numReadings = 0;
  
  
  // For DetectBias:
  
  // Has gyro bias been reported to DAS during this app run?
  bool _gyroBiasReported = false;
  
  // high-pass filtered accelerometer magnitude
  float _hpFiltAccelMag = 0.f;
  
  // previous accelerometer magnitude
  float _accelMagPrev = 0.f;
  
  Vec3f _gyroFilt = {0.f, 0.f, 0.f};
  Vec3f _minFiltGyroVals = {0.f, 0.f, 0.f};
  Vec3f _maxFiltGyroVals = {0.f, 0.f, 0.f};
  
  uint32_t _biasCheckStartTime_ms = 0;
  
  int _nReadings = 0;
  
};

} // namespace Cozmo
} // namespace Anki

#endif // __Anki_Cozmo_RobotGyroDriftDetector_H__

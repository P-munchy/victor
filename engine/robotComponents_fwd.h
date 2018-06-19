/**
* File: robotComponents_fwd.h
*
* Author: Kevin M. Karol
* Created: 1/5/18
*
* Description: Enumeration of components within robot.h's entity
*
* Copyright: Anki, Inc. 2018
*
**/

#ifndef __Engine_RobotComponentsFWD_H__
#define __Engine_RobotComponentsFWD_H__

#include <set>

namespace Anki {

// forward declarations
template<typename EnumType>
class DependencyManagedEntity;

template<typename EnumType>
class IDependencyManagedComponent;

namespace Cozmo {

// When adding to this enum be sure to also declare a template specialization
// in the _impl.cpp file mapping the enum to the class type it is associated with
enum class RobotComponentID{
  AIComponent,
  ActionList,
  Animation,
  Battery,
  BeatDetector,
  BlockTapFilter,
  BlockWorld,
  BodyLights,
  Carrying,
  CliffSensor,
  CozmoContextWrapper,
  CubeAccel,
  CubeComms,
  CubeLights,
  DataAccessor,
  Docking,
  DrivingAnimationHandler,
  EngineAudioClient,
  FaceWorld,
  FullRobotPose,
  GyroDriftDetector,
  Inventory,
  Map,
  MicComponent,
  MoodManager,
  StimulationFaceDisplay,
  Movement,
  NVStorage,
  ObjectPoseConfirmer,
  PathPlanning,
  PetWorld,
  PhotographyManager,
  ProgressionUnlock,
  ProxSensor,
  PublicStateBroadcaster,
  SDK,
  SettingsCommManager,
  SettingsManager,
  RobotIdleTimeout,
  RobotStatsTracker,
  RobotToEngineImplMessaging,
  StateHistory,
  TextToSpeechCoordinator,
  TouchSensor,
  Vision,
  VisionScheduleMediator,
  Count
};

using RobotComp =  IDependencyManagedComponent<RobotComponentID>;
using RobotCompMap = DependencyManagedEntity<RobotComponentID>;
using RobotCompIDSet = std::set<RobotComponentID>;

} // namespace Cozmo
} // namespace Anki

#endif // __Engine_RobotComponentsFWD_H__

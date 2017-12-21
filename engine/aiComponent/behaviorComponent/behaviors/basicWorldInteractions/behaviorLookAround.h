/**
 * File: behaviorLookAround.h
 *
 * Author: Lee
 * Created: 08/13/15
 *
 * Description: Behavior for looking around the environment for stuff to interact with.
 *
 * Copyright: Anki, Inc. 2015
 *
 **/

#ifndef __Cozmo_Basestation_Behaviors_BehaviorLookAround_H__
#define __Cozmo_Basestation_Behaviors_BehaviorLookAround_H__

#include "anki/common/basestation/math/pose.h"
#include "anki/common/basestation/objectIDs.h"
#include "engine/aiComponent/behaviorComponent/behaviors/iCozmoBehavior.h"
#include "util/math/math.h"

#include <vector>
#include <set>

namespace Anki {
namespace Cozmo {

// Forward declaration
template<typename TYPE> class AnkiEvent;
namespace ExternalInterface {
struct RobotObservedObject;
}
  
class BehaviorLookAround : public ICozmoBehavior
{
protected:
  
  // Enforce creation through BehaviorContainer
  friend class BehaviorContainer;
  BehaviorLookAround(const Json::Value& config);
  
public:

  virtual ~BehaviorLookAround() override;
  
  virtual bool WantsToBeActivatedBehavior(BehaviorExternalInterface& behaviorExternalInterface) const override;
  virtual bool CarryingObjectHandledInternally() const override {return false;}


  void SetLookAroundHeadAngle(float angle_rads) { _lookAroundHeadAngle_rads = angle_rads; }
  
protected:
  
  virtual void OnBehaviorActivated(BehaviorExternalInterface& behaviorExternalInterface) override;
  virtual bool ShouldCancelWhenInControl() const override { return false;}
  virtual void BehaviorUpdate(BehaviorExternalInterface& behaviorExternalInterface) override;
  virtual void OnBehaviorDeactivated(BehaviorExternalInterface& behaviorExternalInterface) override;

  virtual void HandleWhileActivated(const EngineToGameEvent& event, BehaviorExternalInterface& behaviorExternalInterface) override;
  virtual void AlwaysHandle(const EngineToGameEvent& event, BehaviorExternalInterface& behaviorExternalInterface) override;

private:
  enum class State {
    WaitForOtherActions,
    Inactive,
    Roaming,
    LookingAtPossibleObject,
    ExaminingFoundObject
  };
  void SetState_internal(State state, const std::string& stateName);

  void TransitionToWaitForOtherActions(BehaviorExternalInterface& behaviorExternalInterface);
  void TransitionToInactive(BehaviorExternalInterface& behaviorExternalInterface);
  void TransitionToRoaming(BehaviorExternalInterface& behaviorExternalInterface);
  void TransitionToLookingAtPossibleObject(BehaviorExternalInterface& behaviorExternalInterface);
  void TransitionToExaminingFoundObject(BehaviorExternalInterface& behaviorExternalInterface);

  enum class Destination {
    North = 0,
    West,
    South,
    East,
    Center,
    
    Count
  };

  static const char* DestinationToString(const Destination& dest);
  
  // How fast to rotate when looking around
  constexpr static f32 kDegreesRotatePerSec = 25;
  // The default radius (in mm) we assume exists for us to move around in
  constexpr static f32 kDefaultSafeRadius = 150;
  // How far back (at most) to move the center when we encounter a cliff
  constexpr static f32 kMaxCliffShiftDist = 100.0f;
  // Number of destinations we want to reach before resting for a bit (needs to be at least 2)
  constexpr static u32 kDestinationsToReach = 6;
  // How far back from a possible object to observe it (at most)
  constexpr static f32 kMaxObservationDistanceSq_mm = SQUARE(200.0f);
  // If the possible block is too far, this is the distance to view it from
  constexpr static f32 kPossibleObjectViewingDist_mm = 100.0f;
  // How long to wait to try to see a possible object (might do this 3 times)
  constexpr static f32 kPossibleObjectWaitTime_s = 0.5f;
  
  State _currentState = State::Inactive;
  Destination _currentDestination = Destination::North;

  // note that this is reset when the robot is put down, so no need to worry about origins
  Pose3d _moveAreaCenter;
  f32 _safeRadius = kDefaultSafeRadius;
  u32 _currentDriveActionID = 0;
  u32 _numDestinationsLeft = kDestinationsToReach;
  f32 _lookAroundHeadAngle_rads = DEG_TO_RAD(-5);

  bool _shouldHandleConfirmedObjectOverved;
  bool _shouldHandlePossibleObjectOverved;
  
  std::set<ObjectID> _recentObjects;
  std::set<ObjectID> _oldBoringObjects;
  Pose3d _lastPossibleObjectPose;
  
  Pose3d GetDestinationPose(Destination destination);
  void ResetBehavior(BehaviorExternalInterface& behaviorExternalInterface);
  Destination GetNextDestination(Destination current);

  void ResetSafeRegion(BehaviorExternalInterface& behaviorExternalInterface);

  // this function may extend the safe region, since we know that if a cube can rest there, we probably can as
  // well
  void UpdateSafeRegionForCube(const Vec3f& objectPosition);

  // This version may shrink the safe region, and/or move it away from the position of the cliff
  void UpdateSafeRegionForCliff(const Pose3d& objectPose);

  void HandleObjectObserved(const ExternalInterface::RobotObservedObject& msg, bool confirmed, BehaviorExternalInterface& behaviorExternalInterface);
  void HandleRobotOfftreadsStateChanged(const EngineToGameEvent& event, BehaviorExternalInterface& behaviorExternalInterface);
  void HandleCliffEvent(const EngineToGameEvent& event, BehaviorExternalInterface& behaviorExternalInterface);

};
  

} // namespace Cozmo
} // namespace Anki

#endif // __Cozmo_Basestation_Behaviors_BehaviorLookAround_H__

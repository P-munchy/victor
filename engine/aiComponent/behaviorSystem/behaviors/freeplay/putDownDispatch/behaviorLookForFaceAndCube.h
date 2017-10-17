/**
 * File: behaviorLookForFaceAndCube
 *
 * Author: Raul
 * Created: 11/01/2016
 *
 * Description: Look for faces and cubes from the current position.
 *
 *
 * Copyright: Anki, Inc. 2016
 *
 **/
#ifndef __Cozmo_Basestation_Behaviors_BehaviorLookForFaceAndCube_H__
#define __Cozmo_Basestation_Behaviors_BehaviorLookForFaceAndCube_H__

#include "engine/aiComponent/behaviorSystem/behaviors/iBehavior.h"
#include "engine/events/animationTriggerHelpers.h"
#include "anki/vision/basestation/faceIdTypes.h"
#include <set>

namespace Anki {
namespace Cozmo {

// Forward declaration
namespace ExternalInterface {
struct RobotObservedObject;
}
class IAction;
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// BehaviorLookForFaceAndCube
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
class BehaviorLookForFaceAndCube : public IBehavior
{
private:
  
  using BaseClass = IBehavior;
  
  // Enforce creation through BehaviorContainer
  friend class BehaviorContainer;
  BehaviorLookForFaceAndCube(const Json::Value& config);
  
public:

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Initialization/destruction
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

  // destructor
  virtual ~BehaviorLookForFaceAndCube() override;

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // IBehavior API
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  
  // This behavior is runnable if when we check the memory map around the current robot position, there are still
  // undiscovered areas
  virtual bool WantsToBeActivatedBehavior(BehaviorExternalInterface& behaviorExternalInterface) const override;
  virtual bool CarryingObjectHandledInternally() const override { return false;}

  virtual void HandleWhileRunning(const EngineToGameEvent& event, BehaviorExternalInterface& behaviorExternalInterface) override;
  
protected:
  
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Initialization
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  
  // set attributes from the given config
  void LoadConfig(const Json::Value& config);
  
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // IBehavior API
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  
  virtual Result OnBehaviorActivated(BehaviorExternalInterface& behaviorExternalInterface) override;
  virtual Result ResumeInternal(BehaviorExternalInterface& behaviorExternalInterface) override;
  virtual void   OnBehaviorDeactivated(BehaviorExternalInterface& behaviorExternalInterface) override;
  
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // State transitions
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  
  // store state to resume
  enum class State {
    S0FaceOnCenter,
    S1FaceOnLeft,
    S2FaceOnRight,
    S3CubeOnRight, // because we ended right for face, start on right for cube
    S4CubeOnLeft,
    S5Center,
    Done
  };
  
  // S0: look for face center
  void TransitionToS1_FaceOnLeft(BehaviorExternalInterface& behaviorExternalInterface);
  void TransitionToS2_FaceOnRight(BehaviorExternalInterface& behaviorExternalInterface);
  void TransitionToS3_CubeOnRight(BehaviorExternalInterface& behaviorExternalInterface);
  void TransitionToS4_CubeOnLeft(BehaviorExternalInterface& behaviorExternalInterface);
  void TransitionToS5_Center(BehaviorExternalInterface& behaviorExternalInterface);
  void TransitionToS6_Done(BehaviorExternalInterface& behaviorExternalInterface);
 
private:

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Types
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

  using FaceID_t = Vision::FaceID_t;
  
  // attributes specifically for configuration of every state
  struct Configuration
  {
    // turn speeds
    Radians bodyTurnSpeed_radPerSec;
    Radians headTurnSpeed_radPerSec;
    // faces
    Radians face_headAngleAbsRangeMin_rad;  // min head angle when looking for faces
    Radians face_headAngleAbsRangeMax_rad;  // max head angle when looking for faces
    Radians face_bodyAngleRelRangeMin_rad;  // min body angle to turn a little when looking for faces
    Radians face_bodyAngleRelRangeMax_rad;  // max body angle to turn a little when looking for faces
    uint8_t face_sidePicks = 0;               // in addition to center, how many angle picks we do per side - face (x per left, x per right)
    bool verifySeenFaces = false; // if true, turn towards and verify any faces we see during this behavior
    // cubes
    Radians cube_headAngleAbsRangeMin_rad;  // min head angle when looking for cubes
    Radians cube_headAngleAbsRangeMax_rad;  // max head angle when looking for cubes
    Radians cube_bodyAngleRelRangeMin_rad;  // min body angle to turn a little when looking for cubes
    Radians cube_bodyAngleRelRangeMax_rad;  // max body angle to turn a little when looking for cubes
    uint8_t cube_sidePicks = 0;               // in addition to center, how many angle picks we do per side - cube (x per left, x per right)
    // anims
    AnimationTrigger lookInPlaceAnimTrigger;
    // early stopping
    bool stopBehaviorOnAnyFace = false; // leave the behavior as soon as any face is seen
    bool stopBehaviorOnNamedFace = false; // leave the behavior as soon as a named face is seen
    
  };
  
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // State helpers
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  
  IAction* CreateBodyAndHeadTurnAction(BehaviorExternalInterface& behaviorExternalInterface,
            const Radians& bodyRelativeMin_rad, const Radians& bodyRelativeMax_rad, // body min/max range added relative to target
            const Radians& bodyAbsoluteTargetAngle_rad,                             // center of the body rotation range
            const Radians& headAbsoluteMin_rad, const Radians& headAbsoluteMax_rad, // head min/max range absolute
            const Radians& bodyTurnSpeed_radPerSec,                                 // body turn speed
            const Radians& headTurnSpeed_radPerSec);                                // head turn speed

  void ResumeCurrentState(BehaviorExternalInterface& behaviorExternalInterface);

  // stop the behavior if desired based on observing the given face
  void StopBehaviorOnFaceIfNeeded(BehaviorExternalInterface& behaviorExternalInterface, FaceID_t observedFace);

  // cancel the current action and do a verify face action instead
  void CancelActionAndVerifyFace(BehaviorExternalInterface& behaviorExternalInterface, FaceID_t observedFace);

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Attributes
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  
  // parsed configurations params from json
  Configuration _configParams;

  // facing direction when we start the behavior
  Radians _startingBodyFacing_rad;
  
  // number of angle picks we have done for the current state
  uint8_t _currentSidePicksDone;
  
  // current state so that we resume at the proper stage (react to cube interrupts behavior for example)
  State _currentState;

  // set of face ID's that we have "verified" with a turn to action (if desired)
  std::set<FaceID_t> _verifiedFaces;
  bool _isVerifyingFace = false;
};

} // namespace Cozmo
} // namespace Anki

#endif //

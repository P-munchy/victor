/**
* File: behaviorFeedingEat.cpp
*
* Author: Kevin M. Karol
* Created: 2017-3-28
*
* Description: Behavior for cozmo to interact with an "energy" filled cube
* and drain the energy out of it
*
* Copyright: Anki, Inc. 2017
*
**/

#include "engine/aiComponent/behaviorComponent/behaviors/feeding/behaviorFeedingEat.h"

#include "engine/actions/animActions.h"
#include "engine/actions/basicActions.h"
#include "engine/actions/driveToActions.h"
#include "engine/aiComponent/aiComponent.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/behaviorExternalInterface.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/beiRobotInfo.h"
#include "engine/aiComponent/behaviorComponent/behaviorListenerInterfaces/iFeedingListener.h"
#include "engine/blockWorld/blockWorld.h"
#include "engine/components/animationComponent.h"
#include "engine/components/cubeAccelComponent.h"
#include "engine/components/cubeAccelComponentListeners.h"
#include "engine/cozmoContext.h"
#include "engine/externalInterface/externalInterface.h"
#include "engine/robotInterface/messageHandler.h"
#include "engine/robotDataLoader.h"

#include "coretech/common/engine/utils/timer.h"
#include "clad/externalInterface/messageEngineToGame.h"
#include "util/console/consoleInterface.h"
#include "util/math/math.h"


namespace Anki {
namespace Cozmo {
  
namespace{
#define SET_STATE(s) SetState_internal(State::s, #s)

#define CONSOLE_GROUP "Behavior.FeedingEat"
  
CONSOLE_VAR(f32, kDistanceFromMarker_mm, CONSOLE_GROUP,  45.0f);
  
// Constants for the CubeAccelComponent MovementListener:
CONSOLE_VAR(f32, kHighPassFiltCoef,          CONSOLE_GROUP,  0.4f);
CONSOLE_VAR(f32, kMaxMovementScoreToAdd,     CONSOLE_GROUP,  3.f);
CONSOLE_VAR(f32, kMovementScoreDecay,        CONSOLE_GROUP,  2.f);
CONSOLE_VAR(f32, kFeedingMovementScoreMax,   CONSOLE_GROUP,  100.f);
CONSOLE_VAR(f32, kCubeMovedTooFastInterrupt, CONSOLE_GROUP,  8.f);

CONSOLE_VAR(f32, kFeedingPreActionAngleTol_deg, CONSOLE_GROUP, 15.0f);
}
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorFeedingEat::BehaviorFeedingEat(const Json::Value& config)
: ICozmoBehavior(config)
, _timeCubeIsSuccessfullyDrained_sec(FLT_MAX)
, _hasRegisteredActionComplete(false)
, _currentState(State::DrivingToFood)
{
  SubscribeToTags({
    EngineToGameTag::RobotObservedObject
  });
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorFeedingEat::WantsToBeActivatedBehavior() const
{
  if(_targetID.IsSet()){
    if( IsCubeBad( _targetID ) ) {
      _targetID.SetToUnknown();
      return false;
    }
    
    const ObservableObject* obj = GetBEI().GetBlockWorld().GetLocatedObjectByID(_targetID);

    // require a known object so we don't drive to and try to eat a moved cube
    const bool canRun = (obj != nullptr) && obj->IsPoseStateKnown();
    if(!canRun){
      _targetID.SetToUnknown();
    }
    return canRun;
  }
  return false;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorFeedingEat::RemoveListeners(IFeedingListener* listener)
{
  size_t numRemoved = _feedingListeners.erase(listener);
  return numRemoved > 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorFeedingEat::OnBehaviorActivated()
{
  if(GetBEI().GetBlockWorld().GetLocatedObjectByID(_targetID) == nullptr){
    return;
  }
  
  _timeCubeIsSuccessfullyDrained_sec = FLT_MAX;
  _hasRegisteredActionComplete = false;

  // generic lambda closure for cube accel listeners
  auto movementDetectedCallback = [this] (const float movementScore) {
    CubeMovementHandler(movementScore);
  };
  
  if(GetBEI().HasCubeAccelComponent()){
    auto listener = std::make_shared<CubeAccelListeners::MovementListener>(kHighPassFiltCoef,
                                                                          kMaxMovementScoreToAdd,
                                                                          kMovementScoreDecay,
                                                                          kFeedingMovementScoreMax, // max allowed movement score
                                                                          movementDetectedCallback);
                                                                        
    GetBEI().GetCubeAccelComponent().AddListener(_targetID, listener);
    DEV_ASSERT(_cubeMovementListener == nullptr,
             "BehaviorFeedingEat.InitInternal.PreviousListenerAlreadySetup");
    // keep a pointer to this listener around so that we can remove it later:
    _cubeMovementListener = listener;
  }


  
  TransitionToDrivingToFood();
  
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorFeedingEat::BehaviorUpdate()
{
  if(!IsActivated()){
    return;
  }

  // Feeding should be considered "complete" so long as the animation has reached
  // the point where all light has been drained from the cube.  If the behavior
  // is interrupted after that point in the animation or the animation completes
  // successfully, register the action as complete.  If it's interrupted before
  // reaching that time (indicated by _timeCubeIsSuccessfullyDrained_sec) then
  // Cozmo didn't successfully finish "eating" and doesn't get the energy for it
  const float currentTime_s = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
  if(!_hasRegisteredActionComplete &&
     (currentTime_s > _timeCubeIsSuccessfullyDrained_sec)){
    _hasRegisteredActionComplete = true;
    for(auto& listener: _feedingListeners){
      listener->EatingComplete();
    }
  }

  
  if((_currentState != State::ReactingToInterruption) &&
     (GetBEI().GetOffTreadsState() != OffTreadsState::OnTreads) &&
     !_hasRegisteredActionComplete){
    TransitionToReactingToInterruption();
  }  
}

  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorFeedingEat::CubeMovementHandler(const float movementScore)
{
  // Logic for determining whether the player has "stolen" cozmo's cube while he's
  // eating.  We only want to respond if the player pulls the cube away while
  // Cozmo is actively in the "eating" stage and has not drained the cube yet

  if(GetBEI().GetRobotInfo().IsPhysical()){
    if(movementScore > kCubeMovedTooFastInterrupt){
      const float currentTime_s = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
      const bool currentlyEating = (_currentState == State::Eating) &&
                       (_timeCubeIsSuccessfullyDrained_sec > currentTime_s);
      
      if(currentlyEating ||
         (_currentState == State::PlacingLiftOnCube)){
        CancelDelegates(false);
        TransitionToReactingToInterruption();
      }else if(_currentState == State::DrivingToFood){
        CancelDelegates(false);
      }
      
    }
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorFeedingEat::OnBehaviorDeactivated()
{
  // If the behavior is being stopped while feeding is still ongoing notify
  // listeners that feeding is being interrupted
  if(!_hasRegisteredActionComplete &&
     (_currentState >= State::PlacingLiftOnCube)){
    for(auto& listener: _feedingListeners){
      listener->EatingInterrupted();
    }
  }


  GetBEI().GetRobotInfo().EnableStopOnCliff(true);
  
  const bool removeSuccessfull = GetBEI().HasCubeAccelComponent() &&
    GetBEI().GetCubeAccelComponent().RemoveListener(_targetID, _cubeMovementListener);
  ANKI_VERIFY(removeSuccessfull,
             "BehaviorFeedingEat.StopInternal.FailedToRemoveAccellComponent",
              "");
  _cubeMovementListener.reset();
  _targetID.UnSet();
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorFeedingEat::TransitionToDrivingToFood()
{
  SET_STATE(DrivingToFood);
  const ObservableObject* obj = GetBEI().GetBlockWorld().GetLocatedObjectByID(_targetID);
  if(obj == nullptr){
    return;
  }

  DriveToAlignWithObjectAction* action = new DriveToAlignWithObjectAction(_targetID,
                                                                          kDistanceFromMarker_mm);
  action->SetPreActionPoseAngleTolerance(DEG_TO_RAD(kFeedingPreActionAngleTol_deg));
  
  DelegateIfInControl(action, [this](ActionResult result){
    if( result == ActionResult::SUCCESS ){
      TransitionToPlacingLiftOnCube();
    }
    else if( result == ActionResult::VISUAL_OBSERVATION_FAILED ) {
      // can't see the cube, maybe it's obstructed? give up on the cube until we see it again. Let the
      // behavior end (it may get re-selected with a different cube)
      MarkCubeAsBad();
    } else {
      const ActionResultCategory resCat = IActionRunner::GetActionResultCategory(result);

      if( resCat == ActionResultCategory::RETRY ) {
        TransitionToDrivingToFood();
      }
      else {
        // something else is wrong. Make this cube invalid, let the behavior end
        MarkCubeAsBad();
      }
    }
    });
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorFeedingEat::TransitionToPlacingLiftOnCube()
{
  SET_STATE(PlacingLiftOnCube);
  
  bool isNeedSevere = false;

  AnimationTrigger bestAnim = isNeedSevere ?
                               AnimationTrigger::FeedingPlaceLiftOnCube_Severe :
                               AnimationTrigger::FeedingPlaceLiftOnCube_Normal;
  
  DelegateIfInControl(new TriggerAnimationAction(bestAnim),
              &BehaviorFeedingEat::TransitionToEating);
}

  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorFeedingEat::TransitionToEating()
{
  SET_STATE(Eating);
  GetBEI().GetRobotInfo().EnableStopOnCliff(false);
  
  AnimationTrigger eatingAnim = CheckNeedsStateAndCalculateAnimation();
  
  uint32_t timeDrainCube_s = 0;
  auto* data_ldr = GetBEI().GetRobotInfo().GetContext()->GetDataLoader();
  if( data_ldr->HasAnimationForTrigger(eatingAnim) )
  {
    // Extract the length of time that the animation will be playing for so that
    // it can be passed through to listeners
    const auto& animComponent = GetBEI().GetAnimationComponent();
    const auto& animGroupName = data_ldr->GetAnimationForTrigger(eatingAnim);
    const auto& animName = animComponent.GetAnimationNameFromGroup(animGroupName);

    AnimationComponent::AnimationMetaInfo metaInfo;
    if (animComponent.GetAnimationMetaInfo(animName, metaInfo) == RESULT_OK) {
      timeDrainCube_s = Util::MilliSecToSec((float)metaInfo.length_ms);
    } else {
      PRINT_NAMED_WARNING("BehaviorFeedingEat.TransitionToEating.AnimationLengthNotFound", "Anim: %s", animName.c_str());
      timeDrainCube_s = 2.f;
    }
  }
  
  
  for(auto & listener: _feedingListeners){
    if(ANKI_VERIFY(listener != nullptr,
                   "BehaviorFeedingEat.TransitionToEating.ListenerIsNull",
                   "")) {
      listener->StartedEating(timeDrainCube_s);
    }
  }
  
  const float currentTime_s = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
  _timeCubeIsSuccessfullyDrained_sec = currentTime_s + timeDrainCube_s;
  
  // DelegateIfInControl(new TriggerAnimationAction(eatingAnim)); // TEMP: only for this branch
  DelegateIfInControl(new PlayAnimationAction("anim_energy_eat_01")); // TEMP: 
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorFeedingEat::TransitionToReactingToInterruption()
{
  const float currentTime_s = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
  const bool currentlyEating = (_currentState == State::Eating) &&
                 (_timeCubeIsSuccessfullyDrained_sec > currentTime_s);
  
  if(currentlyEating ||
     (_currentState == State::PlacingLiftOnCube)){
    for(auto& listener: _feedingListeners){
      listener->EatingInterrupted();
    }
  }
  
  SET_STATE(ReactingToInterruption);
  _timeCubeIsSuccessfullyDrained_sec = FLT_MAX;
  
  CancelDelegates(false);
  AnimationTrigger trigger = AnimationTrigger::FeedingInterrupted;
  
  {
    DelegateIfInControl(new TriggerLiftSafeAnimationAction(trigger));
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
AnimationTrigger BehaviorFeedingEat::CheckNeedsStateAndCalculateAnimation()
{
  bool isSeverePreFeeding  = false;
  bool isWarningPreFeeding = false;
  bool isSeverePostFeeding = false;
  bool isWarningPostFeeding = false;
  bool isFullPostFeeding = false;
  // Eating animation is dependent on both the current and post feeding energy level
  // (code was here that set these)
  AnimationTrigger bestAnimation;
  if(isSeverePreFeeding && isSeverePostFeeding){
    bestAnimation = AnimationTrigger::FeedingAteNotFullEnough_Severe;
  }else if(isSeverePreFeeding && isWarningPostFeeding){
    bestAnimation = AnimationTrigger::FeedingAteFullEnough_Severe;
  }else if(isWarningPreFeeding && !isFullPostFeeding){
    bestAnimation = AnimationTrigger::FeedingAteNotFullEnough_Normal;
  }else{
    bestAnimation = AnimationTrigger::FeedingAteFullEnough_Normal;
  }
  
  PRINT_CH_INFO("Feeding",
                "BehaviorFeedingEat.UpdateNeedsStateCalcAnim.AnimationSelected",
                "AnimationTrigger: %s SeverePreFeeding: %d severePostFeeding: %d warningPreFeeding: %d fullyFullPost: %d ",
                AnimationTriggerToString(bestAnimation),
                isSeverePreFeeding, isSeverePostFeeding,
                isWarningPreFeeding, isFullPostFeeding);
  
  return bestAnimation;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorFeedingEat::MarkCubeAsBad()
{
  if( ! ANKI_VERIFY(_targetID.IsSet(), "BehaviorFeedingEat.MarkCubeAsBad.NoTargetID",
                    "Behavior %s trying to mark target cube as bad, but target is unset",
                    GetDebugLabel().c_str()) ) {
    return;
  }

  const TimeStamp_t lastPoseUpdateTime_ms = GetBEI().GetObjectPoseConfirmer().GetLastPoseUpdatedTime(_targetID);
  _badCubesMap[_targetID] = lastPoseUpdateTime_ms;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorFeedingEat::IsCubeBad(const ObjectID& objectID) const
{ 
  const TimeStamp_t lastPoseUpdateTime_ms = GetBEI().GetObjectPoseConfirmer().GetLastPoseUpdatedTime(objectID);

  auto iter = _badCubesMap.find( objectID );
  if( iter != _badCubesMap.end() ) {
    if( lastPoseUpdateTime_ms <= iter->second ) {
      // cube hasn't been re-observed, so is bad (shouldn't be used by the behavior
      return true;
    }
    // otherwise, the cube was invalid, but has a new pose, so consider it OK
  }

  // cube isn't bad
  return false;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorFeedingEat::SetState_internal(State state, const std::string& stateName)
{
  _currentState = state;
  SetDebugStateName(stateName);
}


  
} // namespace Cozmo
} // namespace Anki


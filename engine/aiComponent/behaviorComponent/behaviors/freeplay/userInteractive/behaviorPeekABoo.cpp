/**
 * File: behaviorPeekABoo.cpp
 *
 * Author: Molly Jameson
 * Created: 2016-02-15
 *
 * Description: Behavior to do PeekABoo
 *
 * Copyright: Anki, Inc. 2017
 *
 **/

#include "engine/aiComponent/behaviorComponent/behaviors/freeplay/userInteractive/behaviorPeekABoo.h"

#include "anki/common/basestation/utils/timer.h"
#include "engine/actions/animActions.h"
#include "engine/actions/basicActions.h"
#include "engine/actions/retryWrapperAction.h"
#include "engine/actions/trackFaceAction.h"
#include "engine/aiComponent/behaviorComponent/behaviorManager.h"
#include "engine/aiComponent/AIWhiteboard.h"
#include "engine/aiComponent/aiComponent.h"
#include "engine/components/animTrackHelpers.h"
#include "engine/cozmoContext.h"
#include "engine/faceWorld.h"
#include "engine/robot.h"
#include "engine/utils/cozmoFeatureGate.h"
#include "anki/vision/basestation/faceTracker.h"

#include "clad/types/animationTrigger.h"

#include "util/console/consoleInterface.h"
#include "util/math/numericCast.h"

namespace Anki {
namespace Cozmo {

namespace{
#define SET_STATE(s) SetState_internal(State::s, #s)
CONSOLE_VAR(uint, kFramesWithoutFaceForPeek, "Behavior.PeekABoo", 6);
CONSOLE_VAR(bool, kCenterFaceAfterPeekABoo, "Behavior.PeekABoo", true);
static constexpr float kPercentCompleteSmallReaction  = 0.3f;
static constexpr float kPercentCompleteMedReaction    = 0.6f;
static constexpr float kSparkShouldPlaySparkFailFlag  = -1.0f;
static constexpr int   kMaxTurnToFaceRetryCount       = 4;
static constexpr int   kMaxCountTrackingEyesEntries   = 50;
static constexpr float kHeadAngleWhereLiftBlocksCamera_deg = 22.0f;
  
constexpr ReactionTriggerHelpers::FullReactionArray kAffectTriggersPeekABooArray = {
  {ReactionTrigger::CliffDetected,                false},
  {ReactionTrigger::CubeMoved,                    true},
  {ReactionTrigger::FacePositionUpdated,          true},
  {ReactionTrigger::FistBump,                     true},
  {ReactionTrigger::Frustration,                  false},
  {ReactionTrigger::Hiccup,                       false},
  {ReactionTrigger::MotorCalibration,             false},
  {ReactionTrigger::NoPreDockPoses,               false},
  {ReactionTrigger::ObjectPositionUpdated,        true},
  {ReactionTrigger::PlacedOnCharger,              false},
  {ReactionTrigger::PetInitialDetection,          true},
  {ReactionTrigger::RobotPickedUp,                false},
  {ReactionTrigger::RobotPlacedOnSlope,           false},
  {ReactionTrigger::ReturnedToTreads,             false},
  {ReactionTrigger::RobotOnBack,                  false},
  {ReactionTrigger::RobotOnFace,                  false},
  {ReactionTrigger::RobotOnSide,                  false},
  {ReactionTrigger::RobotShaken,                  false},
  {ReactionTrigger::Sparked,                      false},
  {ReactionTrigger::UnexpectedMovement,           false},
  {ReactionTrigger::VC,                           false}
};

static_assert(ReactionTriggerHelpers::IsSequentialArray(kAffectTriggersPeekABooArray),
              "Reaction triggers duplicate or non-sequential");
  
}
  
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorPeekABoo::BehaviorPeekABoo(const Json::Value& config)
: IBehavior(config)
, _cachedFace(Vision::UnknownFaceID)
, _numPeeksRemaining(0)
, _numPeeksTotal(1)
, _nextTimeIsRunnable_Sec(0.0f)
, _lastRequestTime_Sec(0.0f)
, _hasMadeFollowUpRequest(false)
, _turnToFaceRetryCount(0)
, _stillSawFaceAfterRequest(false)
, _currentState(State::DoingInitialReaction)
, _timeSparkAboutToEnd_Sec(0.0f)
{
  JsonTools::GetValueOptional(config, "minTimesPeekBeforeQuit",       _params.minPeeks);
  JsonTools::GetValueOptional(config, "maxTimesPeekBeforeQuit",       _params.maxPeeks);
  JsonTools::GetValueOptional(config, "noUserInteractionTimeout_numIdles", _params.noUserInteractionTimeout_numIdles);
  JsonTools::GetValueOptional(config, "numReRequestsPerTimeout",      _params.numReRequestsPerTimeout);  
  JsonTools::GetValueOptional(config, "requireFaceConfirmBeforeRequest", _params.requireFaceConfirmBeforeRequest);
  JsonTools::GetValueOptional(config, "playGetIn",                       _params.playGetIn);
  JsonTools::GetValueOptional(config, "minCooldown_Sec",                 _params.minCoolDown_Sec);
  
  if( JsonTools::GetValueOptional(config, "maxTimeOldestFaceToConsider_Sec", _params.oldestFaceToConsider_MS) ) {
    _params.oldestFaceToConsider_MS *= 1000;
  }

  if( ! ANKI_VERIFY(_params.noUserInteractionTimeout_numIdles > _params.numReRequestsPerTimeout,
                    "BehaviorPeekABoo.Config.InvalidTimeouts",
                    "Behavior '%s' specified invalid values. timeout in %d idles, but re-request %d times",
                    GetIDStr().c_str(),
                    _params.noUserInteractionTimeout_numIdles,
                    _params.numReRequestsPerTimeout) ) {
    // in prod, just update to hardcoded reasonable values
    _params.noUserInteractionTimeout_numIdles = 3;
    _params.numReRequestsPerTimeout = 2;
  }
}

  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorPeekABoo::WantsToBeActivatedBehavior(BehaviorExternalInterface& behaviorExternalInterface) const
{
  const float currentTime_Sec = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
  // for COZMO-8914 - no way to play spark get out if no face is found during spark search
  // so run the peek a boo behavior with a flag set to indicate that we should just play the
  // spark get out animation
  // DEPRECATED - Grabbing robot to support current cozmo code, but this should
  // be removed
  const Robot& robot = behaviorExternalInterface.GetRobot();
  if((robot.GetBehaviorManager().GetActiveSpark() == UnlockId::PeekABoo) &&
     robot.GetBehaviorManager().IsActiveSparkHard() &&
     (_timeSparkAboutToEnd_Sec > 0) &&
     (currentTime_Sec > _timeSparkAboutToEnd_Sec)){
    _timeSparkAboutToEnd_Sec = kSparkShouldPlaySparkFailFlag;
    return true;
  }
  
  // The sparked version of this behavior is grouped with look for faces behavior in case no faces were seen recently.
  _cachedFace = Vision::UnknownFaceID;

  return (_nextTimeIsRunnable_Sec < currentTime_Sec) &&
         (GetInteractionFace(behaviorExternalInterface) != Vision::UnknownFaceID) &&
         robot.GetContext()->GetFeatureGate()->IsFeatureEnabled(Anki::Cozmo::FeatureType::PeekABoo);
}

  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Result BehaviorPeekABoo::OnBehaviorActivated(BehaviorExternalInterface& behaviorExternalInterface)
{
  // for COZMO-8914
  if(ShouldStreamline() &&
     (_timeSparkAboutToEnd_Sec == kSparkShouldPlaySparkFailFlag)){
    // DEPRECATED - Grabbing robot to support current cozmo code, but this should
    // be removed
    Robot& robot = behaviorExternalInterface.GetRobot();
    _timeSparkAboutToEnd_Sec = 0.0f;
    DelegateIfInControl(new TriggerAnimationAction(robot, AnimationTrigger::SparkFailure));
    return RESULT_OK;
  }
  
  _hasMadeFollowUpRequest = false;
  _turnToFaceRetryCount = 0;
  _timestampEyeNotVisibleMap.clear();
  
  _numPeeksTotal = _numPeeksRemaining = behaviorExternalInterface.GetRNG().RandIntInRange(_params.minPeeks, _params.maxPeeks);
  // Disable idle so it doesn't move the head down
  SmartPushIdleAnimation(behaviorExternalInterface, AnimationTrigger::Count);
  SmartDisableReactionsWithLock(GetIDStr(), kAffectTriggersPeekABooArray);
  
  
  if( _params.playGetIn )
  {
    TransitionToIntroAnim(behaviorExternalInterface);
  }
  else
  {
    TransitionTurnToFace(behaviorExternalInterface);
  }
  return RESULT_OK;
}

  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
IBehavior::Status BehaviorPeekABoo::UpdateInternal_WhileRunning(BehaviorExternalInterface& behaviorExternalInterface)
{
  UpdateTimestampSets(behaviorExternalInterface);
  // DEPRECATED - Grabbing robot to support current cozmo code, but this should
  // be removed
  const Robot& robot = behaviorExternalInterface.GetRobot();
  const bool seeingEyes = !WasFaceHiddenAfterTimestamp(behaviorExternalInterface, robot.GetLastImageTimeStamp());
  
  // Check to see if a face has appeared/disappeared every tick
  // these functions are their own callback, so allowing the callback
  // to run means that we have a holding loop with face tracking
  if(_currentState == State::WaitingToHideFace){
    if( !seeingEyes ) {
      StopActing(false);
      TransitionWaitToSeeFace(behaviorExternalInterface);
    }
  }else if(_currentState == State::WaitingToSeeFace){
    if(seeingEyes){
      StopActing(false);
      TransitionSeeFaceAfterHiding(behaviorExternalInterface);
    }
  }
  
  return super::UpdateInternal_WhileRunning(behaviorExternalInterface);
}

  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPeekABoo::OnBehaviorDeactivated(BehaviorExternalInterface& behaviorExternalInterface)
{
  _nextTimeIsRunnable_Sec = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + _params.minCoolDown_Sec;
}

  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPeekABoo::TransitionToIntroAnim(BehaviorExternalInterface& behaviorExternalInterface)
{
  SET_STATE(DoingInitialReaction);
  // DEPRECATED - Grabbing robot to support current cozmo code, but this should
  // be removed
  Robot& robot = behaviorExternalInterface.GetRobot();
  DelegateIfInControl(new TriggerLiftSafeAnimationAction(robot, AnimationTrigger::PeekABooGetIn),&BehaviorPeekABoo::TransitionTurnToFace);
}

  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPeekABoo::TransitionTurnToFace(BehaviorExternalInterface& behaviorExternalInterface)
{
  SET_STATE(TurningToFace);
  // DEPRECATED - Grabbing robot to support current cozmo code, but this should
  // be removed
  Robot& robot = behaviorExternalInterface.GetRobot();
  TurnTowardsFaceAction* action = new TurnTowardsFaceAction(robot, GetInteractionFace(behaviorExternalInterface), M_PI_F, false);
  action->SetRequireFaceConfirmation(_params.requireFaceConfirmBeforeRequest);
  DelegateIfInControl(action, [this, &behaviorExternalInterface](ActionResult ret )
  {
    if( ret == ActionResult::SUCCESS )
    {
      _turnToFaceRetryCount = 0;
      TransitionPlayPeekABooAnim(behaviorExternalInterface);
    }
    else
    {
      // If we've retried too many times for whatever reason, bail out
      // otherwise, retry if appropriate, ro try to select a new face to turn to
      ++_turnToFaceRetryCount;
      if(_turnToFaceRetryCount >= kMaxTurnToFaceRetryCount){
        TransitionExit(behaviorExternalInterface);
        return;
      }
      
      const ActionResultCategory resCat = IActionRunner::GetActionResultCategory(ret);
      if(resCat == ActionResultCategory::RETRY){
        TransitionTurnToFace(behaviorExternalInterface);
      }else{
        // Failed because target face wasn't there, but maybe another one is
        if(GetInteractionFace(behaviorExternalInterface) != Vision::UnknownFaceID )
        {
          // Try to look for the next best face
          TransitionTurnToFace(behaviorExternalInterface);
        }
        else
        {
          // Failed because no faces were found
          TransitionExit(behaviorExternalInterface);
        }
      }
    }
  });
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPeekABoo::TransitionPlayPeekABooAnim(BehaviorExternalInterface& behaviorExternalInterface)
{
  SET_STATE(RequestPeekaBooAnim);
  // DEPRECATED - Grabbing robot to support current cozmo code, but this should
  // be removed
  Robot& robot = behaviorExternalInterface.GetRobot();
  _lastRequestTime_Sec = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
  CompoundActionSequential* action = new CompoundActionSequential(robot);

  // TODO: Peekaboo animations all end with the head looking up at a high angle
  // If the user doesn't have their face in this part of face world we have less
  // accuracy since we get a few frames for free at the end of the anim
  action->AddAction(new TriggerLiftSafeAnimationAction(robot, GetPeekABooAnimation()));
  if(kCenterFaceAfterPeekABoo){
    action->AddAction(new TurnTowardsFaceAction(robot, GetInteractionFace(behaviorExternalInterface)));
  }
  
  DelegateIfInControl(action,[this, &robot](BehaviorExternalInterface& behaviorExternalInterface) {
    // If we saw a face in the frame buffer, assume that they haven't tried to peekaboo yet
    // if we didn't see a face, assume their face is hidden and they are about to finish the peekaboo
    const TimeStamp_t timestampHeadSteady = robot.GetLastImageTimeStamp();
    if(WasFaceHiddenAfterTimestamp(behaviorExternalInterface, timestampHeadSteady)) {
      _stillSawFaceAfterRequest = false;
      TransitionWaitToSeeFace(behaviorExternalInterface);
    }
    else {
      _stillSawFaceAfterRequest = true;
      TransitionWaitToHideFace(behaviorExternalInterface);
    }
  });
}

  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPeekABoo::TransitionWaitToHideFace(BehaviorExternalInterface& behaviorExternalInterface)
{
  SET_STATE(WaitingToHideFace);
  // DEPRECATED - Grabbing robot to support current cozmo code, but this should
  // be removed
  Robot& robot = behaviorExternalInterface.GetRobot();
  // first turn towards the face so the head angle is set (needed for GetIdleAndReRequestAction)
  DelegateIfInControl(new TurnTowardsFaceAction(robot, GetInteractionFace(behaviorExternalInterface)), [this](BehaviorExternalInterface& behaviorExternalInterface) {

    // DEPRECATED - Grabbing robot to support current cozmo code, but this should
    // be removed
    Robot& robot = behaviorExternalInterface.GetRobot();
    // now track the face and set up the idles
    CompoundActionParallel* trackAndIdleAction = new CompoundActionParallel(robot);

    {
      TrackFaceAction* trackFaceAction = new TrackFaceAction(robot, GetInteractionFace(behaviorExternalInterface));
      IActionRunner* idleAction = GetIdleAndReRequestAction(behaviorExternalInterface, false);

      // tracking should stop when the idles finish (to handle timeouts)
      trackFaceAction->StopTrackingWhenOtherActionCompleted( idleAction->GetTag() );
    
      trackAndIdleAction->AddAction( trackFaceAction );
      trackAndIdleAction->AddAction( idleAction );
    }

    // Idle until the timeout. this transition will be aborted if the face gets hidden, so this is just for
    // the no user interaction timeout
    DelegateIfInControl(trackAndIdleAction, [this](BehaviorExternalInterface& behaviorExternalInterface) {
        LOG_EVENT("robot.peekaboo_face_never_hidden","%u", _numPeeksRemaining);
        TransitionToNoUserInteraction(behaviorExternalInterface);
    });
  });
}
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPeekABoo::TransitionWaitToSeeFace(BehaviorExternalInterface& behaviorExternalInterface)
{
  SET_STATE(WaitingToSeeFace);
  // DEPRECATED - Grabbing robot to support current cozmo code, but this should
  // be removed
  Robot& robot = behaviorExternalInterface.GetRobot();
  // first turn towards the face so the head angle is set (needed for GetIdleAndReRequestAction)
  DelegateIfInControl(new TurnTowardsFaceAction(robot, GetInteractionFace(behaviorExternalInterface)), [this](BehaviorExternalInterface& behaviorExternalInterface) {
      // Idle until the timeout. This transition will be aborted if the face is seen, so this just handles no
      // user interaction timeout
      DelegateIfInControl( GetIdleAndReRequestAction(behaviorExternalInterface, true), [this](BehaviorExternalInterface& behaviorExternalInterface) {
          LOG_EVENT("robot.peekaboo_face_never_came_back","%u", _numPeeksRemaining);
          TransitionToNoUserInteraction(behaviorExternalInterface);
      });
  });
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
IActionRunner* BehaviorPeekABoo::GetIdleAndReRequestAction(BehaviorExternalInterface& behaviorExternalInterface, bool lockHeadTrack) const
{
  // create action which alternated between idle and re-request for the desired number of times, and then
  // loops idle the desired number of times until the timeout.
  // DEPRECATED - Grabbing robot to support current cozmo code, but this should
  // be removed
  Robot& robot = behaviorExternalInterface.GetRobot();
  CompoundActionSequential* idleAndReRequestAction = new CompoundActionSequential(robot);

  const u32 singleLoop = 1;
  const bool interruptRunningAnimation = true;
  // In cases where the head isn't already in use, lock it here so that it doesn't move
  const u8 headLock = lockHeadTrack ? (u8)AnimTrackFlag::HEAD_TRACK : (u8)AnimTrackFlag::NO_TRACKS;

  // If the face is too low, then the "PeekABooShort" anim will actually cause the lift to block the camera,
  // which looses track of the face (and then thinks the user peeked when they didn't). If the robots head is
  // below a certain angle, _also_ lock the lift to avoid this case
  const bool headBelowAngle = robot.GetHeadAngle() < DEG_TO_RAD( kHeadAngleWhereLiftBlocksCamera_deg );
  const u8 liftLock = headBelowAngle ? (u8)AnimTrackFlag::LIFT_TRACK : (u8)AnimTrackFlag::NO_TRACKS;

  const u8 lockTracks = headLock | liftLock;

  PRINT_CH_INFO("Behaviors", (GetIDStr() + ".BuildAnims").c_str(),
                "Playing idle with %d re-requests. Head angle = %fdeg Locking: %s",
                _params.numReRequestsPerTimeout,
                RAD_TO_DEG(robot.GetHeadAngle()),
                AnimTrackHelpers::AnimTrackFlagsToString(lockTracks).c_str());

  if( _params.numReRequestsPerTimeout > 0 ) {
    // we want to do re-requests. To avoid eye pops, we will alternate idle animations (which are a few
    // seconds each) with re-requests for the desired number of times
    for( int i=0; i<_params.numReRequestsPerTimeout; ++i ) {
      idleAndReRequestAction->AddAction( new TriggerLiftSafeAnimationAction(robot,
                                                                            AnimationTrigger::PeekABooIdle,
                                                                            singleLoop,
                                                                            interruptRunningAnimation,
                                                                            lockTracks) );
      idleAndReRequestAction->AddAction( new TriggerLiftSafeAnimationAction(robot,
                                                                            AnimationTrigger::PeekABooShort,
                                                                            singleLoop,
                                                                            interruptRunningAnimation,
                                                                            lockTracks) );
    }
  }

  // after re-requests are done (or if there are none), do the desired number of loops to achieve a "timeout",
  // which is actually in terms of number of idles rather than raw seconds
  if( ANKI_VERIFY( _params.noUserInteractionTimeout_numIdles > _params.numReRequestsPerTimeout,
                   "BehaviorPeekABoo.InvalidIdleConfig",
                   "Doing %d re-requests, but only supposed to wait %d idles before timing out. This won't work",
                   _params.numReRequestsPerTimeout,
                   _params.noUserInteractionTimeout_numIdles) ) {
    const u32 numFinalIdles = _params.noUserInteractionTimeout_numIdles - _params.numReRequestsPerTimeout;
    idleAndReRequestAction->AddAction( new TriggerLiftSafeAnimationAction(robot,
                                                                          AnimationTrigger::PeekABooIdle,
                                                                          numFinalIdles,
                                                                          lockTracks) );
  }
  
  return idleAndReRequestAction;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPeekABoo::TransitionSeeFaceAfterHiding(BehaviorExternalInterface& behaviorExternalInterface)
{
  SET_STATE(ReactingToPeekABooReturned);
  _numPeeksRemaining--;
  
  TimeStamp_t timeSinceStart = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() - _lastRequestTime_Sec;
  if( _stillSawFaceAfterRequest )
  {
    LOG_EVENT("robot.single_peekaboo_success.face_noface_face","%u",timeSinceStart);
  }
  else
  {
    LOG_EVENT("robot.single_peekaboo_success.noface_timepass_face","%u",timeSinceStart);
  }
  
  if(_numPeeksRemaining == 0) {
    TransitionExit(behaviorExternalInterface);
  }else{
    // DEPRECATED - Grabbing robot to support current cozmo code, but this should
    // be removed
    Robot& robot = behaviorExternalInterface.GetRobot();
    DelegateIfInControl(new TriggerLiftSafeAnimationAction(robot, AnimationTrigger::PeekABooSurprised),
                &BehaviorPeekABoo::TransitionTurnToFace);
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPeekABoo::TransitionToNoUserInteraction(BehaviorExternalInterface& behaviorExternalInterface)
{
  SET_STATE(ReactingToNoUserInteraction);

  const bool shouldReRequest = (_numPeeksTotal == _numPeeksRemaining)  && !_hasMadeFollowUpRequest;
  _hasMadeFollowUpRequest = true;

  if(shouldReRequest){
    // DEPRECATED - Grabbing robot to support current cozmo code, but this should
    // be removed
    Robot& robot = behaviorExternalInterface.GetRobot();
    IActionRunner* failAnim = new TriggerAnimationAction(robot, AnimationTrigger::PeekABooNoUserInteraction);
    DelegateIfInControl(failAnim, &BehaviorPeekABoo::TransitionTurnToFace);
  }else{
    TransitionExit(behaviorExternalInterface);
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPeekABoo::TransitionExit(BehaviorExternalInterface& behaviorExternalInterface)
{
  SET_STATE(DoingFinalReaction);
  
  const bool anySuccessfullReactions = _numPeeksRemaining != _numPeeksTotal;
  // DEPRECATED - Grabbing robot to support current cozmo code, but this should
  // be removed
  Robot& robot = behaviorExternalInterface.GetRobot();
  // last state, just existing after this...
  DelegateIfInControl(new TriggerLiftSafeAnimationAction(robot,
     anySuccessfullReactions ? AnimationTrigger::PeekABooGetOutHappy : AnimationTrigger::PeekABooGetOutSad));
  
  // Must be done after the animation so this plays
  BehaviorObjectiveAchieved(BehaviorObjective::PeekABooComplete);
  if(anySuccessfullReactions){
    BehaviorObjectiveAchieved(BehaviorObjective::PeekABooSuccess);
    NeedActionCompleted();
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPeekABoo::UpdateTimestampSets(BehaviorExternalInterface& behaviorExternalInterface)
{
  // Prune list so it doesn't expand infinitely in size
  while(_timestampEyeNotVisibleMap.size() > kMaxCountTrackingEyesEntries){
    _timestampEyeNotVisibleMap.erase(_timestampEyeNotVisibleMap.begin());
  }
  
  // DEPRECATED - Grabbing robot to support current cozmo code, but this should
  // be removed
  const Robot& robot = behaviorExternalInterface.GetRobot();
  // If no robot images have been received, don't bother updating
  if(!(_timestampEyeNotVisibleMap.size() == 0) &&
     (robot.GetLastImageTimeStamp() == _timestampEyeNotVisibleMap.rbegin()->first)){
    return;
  }
  
  
  const bool kRecognizableFacesOnly = true;
  std::set< Vision::FaceID_t > faceIDs = robot.GetFaceWorld().GetFaceIDsObservedSince(robot.GetLastImageTimeStamp(),
                                                                                      kRecognizableFacesOnly);
  // We originally kept a "Target face' to know where to initially turn, however
  // when they're constantly covering up their eyes it's likely our face ID is changing a lot. So just allow multiple faces
  // if multiple people are looking at cozmo this means it'll be easier for him to be happy.
  bool seeingFace = !faceIDs.empty();
  bool seeingEyes = false;
  if( seeingFace )
  {
    for(const auto& faceID : faceIDs)
    {
      const Vision::TrackedFace* face = robot.GetFaceWorld().GetFace(faceID);
      if(face != nullptr )
      {
        // If we've seen any eyes go for it...
        Vision::TrackedFace::Feature eyeFeature = face->GetFeature(Vision::TrackedFace::FeatureName::LeftEye);
        if( !eyeFeature.empty() )
        {
          seeingEyes = true;
          break;
        }
      }
    }
  }
  
  const int cumulativeEyeMissingCount = _timestampEyeNotVisibleMap.size() == 0 ? 1 :
                                          _timestampEyeNotVisibleMap.rbegin()->second + 1;
  
  const int cumulativeEyeMissingCountNoWrap = cumulativeEyeMissingCount > 0 ?
                                                cumulativeEyeMissingCount : INT_MAX;
 
  const int framesEyesMissingCount = seeingEyes ? 0 : cumulativeEyeMissingCountNoWrap;

  
  _timestampEyeNotVisibleMap.insert(std::make_pair(robot.GetLastImageTimeStamp(), framesEyesMissingCount));
}
  
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorPeekABoo::WasFaceHiddenAfterTimestamp(BehaviorExternalInterface& behaviorExternalInterface, TimeStamp_t timestamp)
{
  // DEPRECATED - Grabbing robot to support current cozmo code, but this should
  // be removed
  const Robot& robot = behaviorExternalInterface.GetRobot();
  // make sure the list is up to date
  if(_timestampEyeNotVisibleMap.rbegin()->first != robot.GetLastImageTimeStamp()){
    UpdateTimestampSets(behaviorExternalInterface);
  }
  
  bool anyCountOverThreshold = false;
  auto reverseIter = _timestampEyeNotVisibleMap.rbegin();
  while((reverseIter != _timestampEyeNotVisibleMap.rend()) &&
        (reverseIter->first >= timestamp)){
    if(reverseIter->second > kFramesWithoutFaceForPeek){
      anyCountOverThreshold = true;
      break;
    }
    ++reverseIter;
  }
  
  return anyCountOverThreshold;
}

  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Vision::FaceID_t BehaviorPeekABoo::GetInteractionFace(const BehaviorExternalInterface& behaviorExternalInterface) const
{
  // DEPRECATED - Grabbing robot to support current cozmo code, but this should
  // be removed
  const Robot& robot = behaviorExternalInterface.GetRobot();
  const bool kUseRecognizableOnly = false;
  std::set< Vision::FaceID_t > faces = robot.GetFaceWorld().GetFaceIDsObservedSince(
                    robot.GetLastImageTimeStamp() - _params.oldestFaceToConsider_MS, kUseRecognizableOnly);
  
  if(faces.find(_cachedFace) == faces.end()){
    const AIWhiteboard& whiteboard = robot.GetAIComponent().GetWhiteboard();
    _cachedFace = whiteboard.GetBestFaceToTrack(faces, false);
  }

  return _cachedFace;
}
 

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
AnimationTrigger BehaviorPeekABoo::GetPeekABooAnimation()
{
  const bool shouldMakeShortRequest = (_numPeeksTotal == _numPeeksRemaining)  && _hasMadeFollowUpRequest;

  if(shouldMakeShortRequest){
    return AnimationTrigger::PeekABooShort;
  }
  
  float percentComplete = 1.f - (Util::numeric_cast<float>(_numPeeksRemaining) / Util::numeric_cast<float>(_numPeeksTotal));
  
  AnimationTrigger playTrigger = AnimationTrigger::PeekABooHighIntensity;
  if( percentComplete < kPercentCompleteSmallReaction)
  {
    playTrigger = AnimationTrigger::PeekABooLowIntensity;
  }
  else if( percentComplete < kPercentCompleteMedReaction)
  {
    playTrigger = AnimationTrigger::PeekABooMedIntensity;
  }
  
  return playTrigger;
}

  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPeekABoo::SetState_internal(State state, const std::string& stateName)
{
  _currentState = state;
  SetDebugStateName(stateName);
}

  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPeekABoo::PeekABooSparkStarted(float sparkTimeout)
{
  const float bufferBeforeSparkEnd = 2.0;
  _timeSparkAboutToEnd_Sec = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds()
                                   + sparkTimeout - bufferBeforeSparkEnd;
}

  

} // namespace Cozmo
} // namespace Anki


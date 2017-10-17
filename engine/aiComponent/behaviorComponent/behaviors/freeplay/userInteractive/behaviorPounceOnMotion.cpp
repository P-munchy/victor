/**
 * File: behaviorPounceOnMotion.cpp
 *
 * Author: Brad Neuman
 * Created: 2015-11-19
 *
 * Description: This is a behavior which "pounces". Basically, it looks for motion nearby in the
 *              ground plane, and then drive to drive towards it and "catch" it underneath it's lift
 *
 * Copyright: Anki, Inc. 2015
 *
 **/

#include "engine/aiComponent/behaviorComponent/behaviors/freeplay/userInteractive/behaviorPounceOnMotion.h"

#include "anki/common/basestation/math/point.h"
#include "anki/common/basestation/math/pose.h"
#include "anki/common/basestation/utils/timer.h"
#include "engine/actions/animActions.h"
#include "engine/actions/basicActions.h"
#include "engine/aiComponent/AIWhiteboard.h"
#include "engine/components/movementComponent.h"
#include "engine/components/visionComponent.h"
#include "engine/drivingAnimationHandler.h"
#include "engine/events/ankiEvent.h"
#include "engine/externalInterface/externalInterface.h"
#include "engine/robot.h"
#include "clad/externalInterface/messageEngineToGame.h"

#define SET_STATE(s) SetState_internal(State::s, #s)

namespace Anki {
namespace Cozmo {

using namespace ExternalInterface;

namespace {

static const char* kMaxNoMotionBeforeBored_running_Sec    = "maxNoGroundMotionBeforeBored_running_Sec";
static const char* kMaxNoMotionBeforeBored_notRunning_Sec = "maxNoGroundMotionBeforeBored_notRunning_Sec";
static const char* kMaxTimeBehaviorTimeout_Sec            = "maxTimeBehaviorTimeout_Sec";
static const char* kTimeBeforeRotate_Sec                  = "timeBeforeRotate_Sec";
static const char* kOddsOfPouncingOnTurn                  = "oddsOfPouncingOnTurn";
static const char* kBoredomMultiplier                     = "boredomMultiplier";
static const char* kSearchAmplitudeDeg                    = "searchAmplitudeDeg";
static const char* kSkipGetOutAnim                        = "skipGetOutAnim";
static float kBoredomMultiplierDefault = 0.8f;

// combination of offset between lift and robot orign and motion built into animation
static constexpr float kDriveForwardUntilDist = 50.f;
// creeping less than this is boring so pounce even if the finger might be a bit out of range
static constexpr float kMinCreepDistance = 10.f;
// Anything below this basically all looks the same, so just play the animation and possibly miss
static constexpr float kVisionMinDistMM = 65.f;
// How long to wait before re-calling
static constexpr float kWaitForMotionInterval_s = 2.0f;

// how far to randomly turn the body
static constexpr float kRandomPanMin_Deg = 20;
static constexpr float kRandomPanMax_Deg = 45;
  
  
// how long ago to consider a cliff currently in front of us for an initial pounce
static constexpr float kMinCliffInFrontWait_sec = 10.f;
  
// count of creep forwards/turns cozmo should do on motion before pouncing
static constexpr int kMotionObservedCountBeforePossiblePounce = 2;
  
static const constexpr char* const kTrackLockName = "behaviorPounceOnMotionWaitLock";

} 
  
// Cozmo's low head angle for watching for fingers
static const Radians tiltRads(MIN_HEAD_ANGLE);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorPounceOnMotion::BehaviorPounceOnMotion(const Json::Value& config)
  : ICozmoBehavior(config)
  , _cumulativeTurn_rad(0)
  , _observedX(0)
  , _observedY(0)
  , _lastTimeRotate(0.0f)
  , _lastCliffEvent_sec(0.0f)
  , _motionObservedNoPounceCount(0)
{
  SubscribeToTags({{
    EngineToGameTag::RobotObservedMotion,
    EngineToGameTag::CliffEvent,
    EngineToGameTag::RobotOffTreadsStateChanged
  }});

  _maxTimeSinceNoMotion_running_sec = config.get(kMaxNoMotionBeforeBored_running_Sec,
                                                 _maxTimeSinceNoMotion_running_sec).asFloat();
  _maxTimeSinceNoMotion_notRunning_sec = config.get(kMaxNoMotionBeforeBored_notRunning_Sec,
                                                    _maxTimeSinceNoMotion_notRunning_sec).asFloat();
  _maxTimeBehaviorTimeout_sec = config.get(kMaxTimeBehaviorTimeout_Sec,
                                                    _maxTimeBehaviorTimeout_sec).asFloat();
  _boredomMultiplier = config.get(kBoredomMultiplier, kBoredomMultiplierDefault).asFloat();
  _maxTimeBeforeRotate = config.get(kTimeBeforeRotate_Sec, _maxTimeBeforeRotate).asFloat();
  _oddsOfPouncingOnTurn = config.get(kOddsOfPouncingOnTurn, 0.0).asFloat();
  _searchAmplitude_rad = Radians(DEG_TO_RAD(config.get(kSearchAmplitudeDeg, 45).asFloat()));
  _skipGetOutAnim = config.get(kSkipGetOutAnim, false).asBool();

  SET_STATE(Inactive);
  _lastMotionTime = -1000.f;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorPounceOnMotion::WantsToBeActivatedBehavior(BehaviorExternalInterface& behaviorExternalInterface) const
{
  return true;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
float BehaviorPounceOnMotion::EvaluateScoreInternal(BehaviorExternalInterface& behaviorExternalInterface) const
{
  // more likely to run if we did happen to see ground motion recently.
  // This isn't likely unless cozmo is looking down in explore mode, but possible
  float multiplier = 1.f;
  if( !IsRunning() )
  {
    const float currentTime_sec = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
    if ( _lastMotionTime + _maxTimeSinceNoMotion_notRunning_sec < currentTime_sec )
    {
      multiplier = _boredomMultiplier;
    }
  }
  return ICozmoBehavior::EvaluateScoreInternal(behaviorExternalInterface) * multiplier;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Result BehaviorPounceOnMotion::OnBehaviorActivated(BehaviorExternalInterface& behaviorExternalInterface)
{
  _humanInteracted = false;
  InitHelper(behaviorExternalInterface);
  TransitionToInitialPounce(behaviorExternalInterface);
  return Result::RESULT_OK;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Result BehaviorPounceOnMotion::ResumeInternal(BehaviorExternalInterface& behaviorExternalInterface)
{
  _motionObserved = false;
  InitHelper(behaviorExternalInterface);
  TransitionToBringingHeadDown(behaviorExternalInterface);
  return Result::RESULT_OK;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPounceOnMotion::InitHelper(BehaviorExternalInterface& behaviorExternalInterface)
{
  const float currentTime_sec = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
  _startedBehaviorTime_sec = currentTime_sec;
  _lastMotionTime = currentTime_sec;
  _motionObservedNoPounceCount = 0;
  
  // Don't override sparks idle animation
  if(!ShouldStreamline()){
    SmartPushIdleAnimation(behaviorExternalInterface, AnimationTrigger::PounceFace);
    // DEPRECATED - Grabbing robot to support current cozmo code, but this should
    // be removed
    Robot& robot = behaviorExternalInterface.GetRobot();
    robot.GetDrivingAnimationHandler().PushDrivingAnimations(
     {AnimationTrigger::PounceDriveStart,
      AnimationTrigger::PounceDriveLoop,
      AnimationTrigger::PounceDriveEnd},
        GetIDStr());
  }
  
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPounceOnMotion::OnBehaviorDeactivated(BehaviorExternalInterface& behaviorExternalInterface)
{
  Cleanup(behaviorExternalInterface);

  if (_humanInteracted)
  {
    _humanInteracted = false;
    NeedActionCompleted();
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPounceOnMotion::TransitionToInitialPounce(BehaviorExternalInterface& behaviorExternalInterface)
{
  SET_STATE(InitialPounce);
  
  // Determine if there is a cliff in front of us so we don't pounce off an edge
  IActionRunner* potentialCliffSafetyTurn = nullptr;
  const float currentTime_sec = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
  const bool cliffInFront = (currentTime_sec - _lastCliffEvent_sec) < kMinCliffInFrontWait_sec;
  
  if(cliffInFront){
    // DEPRECATED - Grabbing robot to support current cozmo code, but this should
    // be removed
    Robot& robot = behaviorExternalInterface.GetRobot();
    // This initial turn means that if Cozmo hits a cliff during an initial pounce
    // he won't get stuck in an infinite loop
    const Radians bodyPan(DEG_TO_RAD(90));
    const Radians headTilt(0);
    potentialCliffSafetyTurn = new PanAndTiltAction(robot, bodyPan, headTilt, false, false);
  }
  
  // Skip the initial pounce and go straight to search if streamlined
  if(ShouldStreamline())
  {
    TransitionToInitialSearch(behaviorExternalInterface);
  }
  else
  {
    PounceOnMotionWithCallback(behaviorExternalInterface,
                               &BehaviorPounceOnMotion::TransitionToInitialReaction,
                               potentialCliffSafetyTurn);
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorPounceOnMotion::IsFingerCaught(BehaviorExternalInterface& behaviorExternalInterface)
{
  // DEPRECATED - Grabbing robot to support current cozmo code, but this should
  // be removed
  const Robot& robot = behaviorExternalInterface.GetRobot();
  const float liftHeightThresh = 35.5f;
  const float bodyAngleThresh = 0.02f;
  
  float robotBodyAngleDelta = robot.GetPitchAngle().ToFloat() - _prePouncePitch;
  
  // check the lift angle, after some time, transition state
  PRINT_CH_INFO("Behaviors", "BehaviorPounceOnMotion.CheckResult", "lift: %f body: %fdeg (%frad) (%f -> %f)",
                robot.GetLiftHeight(),
                RAD_TO_DEG(robotBodyAngleDelta),
                robotBodyAngleDelta,
                RAD_TO_DEG(_prePouncePitch),
                robot.GetPitchAngle().getDegrees());
  return robot.GetLiftHeight() > liftHeightThresh || robotBodyAngleDelta > bodyAngleThresh;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPounceOnMotion::TransitionToInitialReaction(BehaviorExternalInterface& behaviorExternalInterface)
{
  SET_STATE(InitialReaction);
  // If we didn't catch anything this first anim is just showing intent, but react if he does happen to catch something.
  bool caught = IsFingerCaught(behaviorExternalInterface);
  if( caught )
  {
    // DEPRECATED - Grabbing robot to support current cozmo code, but this should
    // be removed
    Robot& robot = behaviorExternalInterface.GetRobot();
    DelegateIfInControl(new TriggerLiftSafeAnimationAction(robot, AnimationTrigger::PounceSuccess), &BehaviorPounceOnMotion::TransitionToInitialSearch);
    PRINT_CH_INFO("Behaviors", "BehaviorPounceOnMotion.TransitionToInitialReaction.Caught", "got it!");
  }
  else
  {
    TransitionToInitialSearch(behaviorExternalInterface);
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPounceOnMotion::TransitionToInitialSearch(BehaviorExternalInterface& behaviorExternalInterface)
{
  PRINT_NAMED_DEBUG("BehaviorPounceOnMotion.TransitionToInitialSearch",
                    "BehaviorPounceOnMotion.TransitionToInitialSearch");
  SET_STATE(InitialSearch);
  // DEPRECATED - Grabbing robot to support current cozmo code, but this should
  // be removed
  Robot& robot = behaviorExternalInterface.GetRobot();
  
  CompoundActionSequential* fullAction = new CompoundActionSequential(robot);
  
  float panDirection = 1.0f;
  {
    // set pan and tilt
    Radians panAngle(DEG_TO_RAD(GetRNG().RandDblInRange(kRandomPanMin_Deg,kRandomPanMax_Deg)));
    // randomize direction
    if( GetRNG().RandDbl() < 0.5 )
    {
      panDirection = -1.0f;
    }
    
    panAngle *= panDirection;
    
    IActionRunner* panAndTilt = new PanAndTiltAction(robot, panAngle, tiltRads, false, true);
    fullAction->AddAction(panAndTilt);
  }
  
  // pan another random amount in the other direction (should get us back close to where we started, but not
  // exactly)
  {
    // get new pan angle
    Radians panAngle(DEG_TO_RAD(GetRNG().RandDblInRange(kRandomPanMin_Deg,kRandomPanMax_Deg)));
    // opposite direction
    panAngle *= -panDirection;
    
    IActionRunner* panAndTilt = new PanAndTiltAction(robot, panAngle, tiltRads, false, true);
    fullAction->AddAction(panAndTilt);
  }
  
  DelegateIfInControl(fullAction, &BehaviorPounceOnMotion::TransitionToWaitForMotion);
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPounceOnMotion::TransitionToBringingHeadDown(BehaviorExternalInterface& behaviorExternalInterface)
{
  SmartUnLockTracks(kTrackLockName);

  PRINT_NAMED_DEBUG("BehaviorPounceOnMotion.TransitionToBringingHeadDown","BehaviorPounceOnMotion.TransitionToBringingHeadDown");
  SET_STATE(BringingHeadDown);
  
  // DEPRECATED - Grabbing robot to support current cozmo code, but this should
  // be removed
  Robot& robot = behaviorExternalInterface.GetRobot();
  
  Radians tiltRads(MIN_HEAD_ANGLE);
  DelegateIfInControl(new MoveHeadToAngleAction(robot, tiltRads),
              &BehaviorPounceOnMotion::TransitionToWaitForMotion);
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPounceOnMotion::TransitionToRotateToWatchingNewArea(BehaviorExternalInterface& behaviorExternalInterface)
{
  SET_STATE( RotateToWatchingNewArea );
  _lastTimeRotate = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();

  Radians panAngle(DEG_TO_RAD(GetRNG().RandDblInRange(kRandomPanMin_Deg,kRandomPanMax_Deg)));

  // other direction weighted based on the cumulative turn rads - constantly pulls robot towards center
  const double weightedSearchAmplitude = (_searchAmplitude_rad.ToDouble()/(_searchAmplitude_rad.ToDouble() - _cumulativeTurn_rad.ToDouble())) * 0.5;
  if( GetRNG().RandDbl() < weightedSearchAmplitude)
  {
    panAngle *= -1.f;
  }
  _cumulativeTurn_rad += panAngle;
  
  // DEPRECATED - Grabbing robot to support current cozmo code, but this should
  // be removed
  Robot& robot = behaviorExternalInterface.GetRobot();
  
  IActionRunner* panAction = new PanAndTiltAction(robot, panAngle, tiltRads, false, false);
  
  //if we are above the threshold percentage, pounce and pan - otherwise, just pan
  const float shouldPounceOnTurn = GetRNG().RandDblInRange(0, 1);
  if(shouldPounceOnTurn < _oddsOfPouncingOnTurn){
    PounceOnMotionWithCallback(behaviorExternalInterface, &BehaviorPounceOnMotion::TransitionToWaitForMotion, panAction);
  }else{
    DelegateIfInControl(panAction, &BehaviorPounceOnMotion::TransitionToWaitForMotion);
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPounceOnMotion::TransitionToWaitForMotion(BehaviorExternalInterface& behaviorExternalInterface)
{
  SET_STATE( WaitingForMotion);
  _numValidPouncePoses = 0;
  _backUpDistance = 0.f;
  _motionObserved = false;
  SmartLockTracks((u8)AnimTrackFlag::HEAD_TRACK, kTrackLockName, kTrackLockName);
  // DEPRECATED - Grabbing robot to support current cozmo code, but this should
  // be removed
  Robot& robot = behaviorExternalInterface.GetRobot();
  
  DelegateIfInControl(new WaitAction(robot, kWaitForMotionInterval_s), &BehaviorPounceOnMotion::TransitionFromWaitForMotion);
  
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPounceOnMotion::TransitionFromWaitForMotion(BehaviorExternalInterface& behaviorExternalInterface)
{
  SmartUnLockTracks(kTrackLockName);

  // In the event motion is seen, this callback is triggered immediately
  if(_motionObserved){
    TransitionToTurnToMotion(behaviorExternalInterface, _observedX, _observedY);
    return;
  }
  
  //Otherwise, check to see if there has been a timeout or go back to waitingForMotion
  
  const float currentTime_sec = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
  
  // We're done if we haven't seen motion in a long while or since start.
  if ( (_lastMotionTime + _maxTimeSinceNoMotion_running_sec) < currentTime_sec )
  {
    PRINT_CH_INFO("Behaviors", "BehaviorPounceOnMotion.Timeout", "No motion found, giving up");
    
    //Set the exit state information and then cancel the hang action
    TransitionToGetOutBored(behaviorExternalInterface);
  }
  else if ( (_lastTimeRotate + _maxTimeBeforeRotate) < currentTime_sec )
  {
    //Set the exit state information and then cancel the hang action
    TransitionToRotateToWatchingNewArea(behaviorExternalInterface);
  } else if ( (_startedBehaviorTime_sec + _maxTimeBehaviorTimeout_sec) < currentTime_sec) {
    TransitionToGetOutBored(behaviorExternalInterface);
  } else {
    TransitionToWaitForMotion(behaviorExternalInterface);
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPounceOnMotion::TransitionToTurnToMotion(BehaviorExternalInterface& behaviorExternalInterface, int16_t motion_img_x, int16_t motion_img_y)
{
  SET_STATE(TurnToMotion);
  _lastTimeRotate = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();

  const Point2f motionCentroid(motion_img_x, motion_img_y);
  Radians relPanAngle;
  Radians relTiltAngle;
  
  // DEPRECATED - Grabbing robot to support current cozmo code, but this should
  // be removed
  Robot& robot = behaviorExternalInterface.GetRobot();
  
  robot.GetVisionComponent().GetCamera().ComputePanAndTiltAngles(motionCentroid, relPanAngle, relTiltAngle);
  
  auto callback = &BehaviorPounceOnMotion::TransitionToCreepForward;
  // steadily increase the chance we'll pounce if we haven't pounced while seeing motion in a while
  const bool shouldPounceNoMatterWhat = _motionObservedNoPounceCount > kMotionObservedCountBeforePossiblePounce &&
                                           (_motionObservedNoPounceCount * .2) > GetRNG().RandDblInRange(0, 1);
  
  if(_lastPoseDist <= kVisionMinDistMM || GetDriveDistance() < kMinCreepDistance || shouldPounceNoMatterWhat){
    callback = &BehaviorPounceOnMotion::TransitionToPounce;
  }else{
    _motionObservedNoPounceCount++;
  }
  
  DelegateIfInControl(new PanAndTiltAction(robot, relPanAngle, tiltRads, false, false),
              callback);
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
float BehaviorPounceOnMotion::GetDriveDistance()
{
  return _lastPoseDist - kDriveForwardUntilDist;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPounceOnMotion::TransitionToCreepForward(BehaviorExternalInterface& behaviorExternalInterface)
{
  SET_STATE(CreepForward);
  // Sneak... Sneak... Sneak...
  _backUpDistance = GetDriveDistance();
  
  // DEPRECATED - Grabbing robot to support current cozmo code, but this should
  // be removed
  Robot& robot = behaviorExternalInterface.GetRobot();
  DriveStraightAction* driveAction = new DriveStraightAction(robot, _backUpDistance, DEFAULT_PATH_MOTION_PROFILE.dockSpeed_mmps);
  driveAction->SetAccel(DEFAULT_PATH_MOTION_PROFILE.dockAccel_mmps2);

  SmartLockTracks((u8)AnimTrackFlag::HEAD_TRACK, kTrackLockName, kTrackLockName);
  DelegateIfInControl(driveAction, &BehaviorPounceOnMotion::TransitionToBringingHeadDown);
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPounceOnMotion::TransitionToPounce(BehaviorExternalInterface& behaviorExternalInterface)
{
  SET_STATE(Pouncing);
  
  // DEPRECATED - Grabbing robot to support current cozmo code, but this should
  // be removed
  const Robot& robot = behaviorExternalInterface.GetRobot();
  _prePouncePitch = robot.GetPitchAngle().ToFloat();
  if( _backUpDistance <= 0.f )
  {
    _backUpDistance = GetDriveDistance();
  }
  
  
  PounceOnMotionWithCallback(behaviorExternalInterface, &BehaviorPounceOnMotion::TransitionToResultAnim);
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPounceOnMotion::TransitionToResultAnim(BehaviorExternalInterface& behaviorExternalInterface)
{
  SET_STATE(PlayingFinalReaction);
  
  bool caught = IsFingerCaught(behaviorExternalInterface);

  IActionRunner* newAction = nullptr;
  if( caught ) {
    // DEPRECATED - Grabbing robot to support current cozmo code, but this should
    // be removed
    Robot& robot = behaviorExternalInterface.GetRobot();
    newAction = new TriggerLiftSafeAnimationAction(robot, AnimationTrigger::PounceSuccess);
    PRINT_CH_INFO("Behaviors", "BehaviorPounceOnMotion.CheckResult.Caught", "got it!");
  }
  else {
    // currently equivalent to "isSparked" - don't play failure anim when sparked
    if(!ShouldStreamline()){
      // DEPRECATED - Grabbing robot to support current cozmo code, but this should
      // be removed
      Robot& robot = behaviorExternalInterface.GetRobot();
      newAction = new TriggerLiftSafeAnimationAction(robot, AnimationTrigger::PounceFail );
    }else{
      // DEPRECATED - Grabbing robot to support current cozmo code, but this should
      // be removed
      Robot& robot = behaviorExternalInterface.GetRobot();
      newAction = new TriggerAnimationAction(robot, AnimationTrigger::Count);
    }
    PRINT_CH_INFO("Behaviors", "BehaviorPounceOnMotion.CheckResult.Miss", "missed...");
  }
  
  auto callback = &BehaviorPounceOnMotion::TransitionToBringingHeadDown;
  if(_backUpDistance > 0.f){
    callback = &BehaviorPounceOnMotion::TransitionToBackUp;
  }
  
  _numValidPouncePoses = 0; // wait until we're seeing motion again

  DelegateIfInControl(newAction, callback);

  if( caught ) {
    // send this after we start the action, so if the activity tries to cancel us,
    // we will play the react first
    BehaviorObjectiveAchieved(BehaviorObjective::PouncedAndCaught);
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPounceOnMotion::TransitionToBackUp(BehaviorExternalInterface& behaviorExternalInterface)
{
  SET_STATE(BackUp);
  // back up some of the way
  // DEPRECATED - Grabbing robot to support current cozmo code, but this should
  // be removed
  Robot& robot = behaviorExternalInterface.GetRobot();
  DelegateIfInControl(new DriveStraightAction(robot, -_backUpDistance, DEFAULT_PATH_MOTION_PROFILE.reverseSpeed_mmps),
              &BehaviorPounceOnMotion::TransitionToBringingHeadDown);
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPounceOnMotion::TransitionToGetOutBored(BehaviorExternalInterface& behaviorExternalInterface)
{
  SET_STATE(GetOutBored);
  if(!_skipGetOutAnim)
  {
    // DEPRECATED - Grabbing robot to support current cozmo code, but this should
    // be removed
    Robot& robot = behaviorExternalInterface.GetRobot();
    DelegateIfInControl(new TriggerLiftSafeAnimationAction(robot, AnimationTrigger::PounceGetOut));
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPounceOnMotion::AlwaysHandle(const EngineToGameEvent& event, BehaviorExternalInterface& behaviorExternalInterface)
{
  switch (event.GetData().GetTag())
  {
    case MessageEngineToGameTag::RobotObservedMotion: {
      // handled differently based on running/not running
      break;
    }
      
    case MessageEngineToGameTag::CliffEvent: {
      if(event.GetData().Get_CliffEvent().detectedFlags != 0){
        _lastCliffEvent_sec = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
      }
      break;
    }
      
    case EngineToGameTag::RobotOffTreadsStateChanged: {
      _lastCliffEvent_sec = 0.0f;
      break;
    }
      
    default: {
      PRINT_NAMED_ERROR("BehaviorPounceOnMotion.AlwaysHandle.InvalidEvent", "");
      break;
    }
  }
  
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPounceOnMotion::HandleWhileNotRunning(const EngineToGameEvent& event, BehaviorExternalInterface& behaviorExternalInterface)
{
  switch (event.GetData().GetTag())
  {
    case MessageEngineToGameTag::RobotObservedMotion: {
      // be more likely to run with observed motion
      const auto & motionObserved = event.GetData().Get_RobotObservedMotion();
      const bool inGroundPlane = motionObserved.ground_area > _minGroundAreaForPounce;
      if( inGroundPlane )
      {
        const float robotOffsetX = motionObserved.ground_x;
        const float robotOffsetY = motionObserved.ground_y;
        float distSquared = robotOffsetX * robotOffsetX + robotOffsetY * robotOffsetY;
        float maxDistSquared = _maxPounceDist * _maxPounceDist;
        if( distSquared <= maxDistSquared )
        {
          const float currentTime_sec = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
          _lastMotionTime = currentTime_sec;
        }
      }
      break;
    }
      
    case MessageEngineToGameTag::CliffEvent:
    case MessageEngineToGameTag::RobotOffTreadsStateChanged:
    {
      // handled in always handle
      break;
    }
      
    default: {
      PRINT_NAMED_ERROR("BehaviorPounceOnMotion.AlwaysHandle.InvalidEvent", "");
      break;
    }
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPounceOnMotion::HandleWhileRunning(const EngineToGameEvent& event, BehaviorExternalInterface& behaviorExternalInterface)
{
  switch (event.GetData().GetTag())
  {
    case MessageEngineToGameTag::RobotObservedMotion: {
      // don't update the pounce location while we are active but go back.
      const auto & motionObserved = event.GetData().Get_RobotObservedMotion();
      const bool inGroundPlane = motionObserved.ground_area > _minGroundAreaForPounce;
      
      const float currTime = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
      if ( inGroundPlane )
      {
        _lastMotionTime = currTime;
      }
      if ( _state == State::WaitingForMotion )
      {
        const float robotOffsetX = motionObserved.ground_x;
        const float robotOffsetY = motionObserved.ground_y;
        
        bool gotPose = false;
        // we haven't started the pounce, so update the pounce location
        if ( inGroundPlane )
        {
          float dist = std::sqrt( std::pow( robotOffsetX, 2 ) + std::pow( robotOffsetY, 2) );
          if ( dist <= _maxPounceDist )
          {
            gotPose = true;
            _numValidPouncePoses++;
            _lastValidPouncePoseTime = currTime;
            _humanInteracted = true;
            
            PRINT_CH_INFO("Behaviors", "BehaviorPounceOnMotion.GotPose", "got valid pose with dist = %f. Now have %d",
                          dist, _numValidPouncePoses);
            _lastPoseDist = dist;
            
            //Set the exit state information and then cancel the hang action
            _observedX = motionObserved.img_x;
            _observedY = motionObserved.img_y;
            _motionObserved = true;
            StopActing();
          }
          else
          {
            PRINT_CH_INFO("Behaviors", "BehaviorPounceOnMotion.IgnorePose",
                          "got pose, but dist of %f is too large, ignoring",
                          dist);
          }
        }
        else if(_numValidPouncePoses > 0)
        {
          PRINT_NAMED_DEBUG("BehaviorPounceOnMotion.IgnorePose", "got pose, but ground plane area is %f, which is too low",
                            motionObserved.ground_area);
        }
        
        // reset everything if it's been this long since we got a valid pose
        if ( ! gotPose && currTime >= _lastValidPouncePoseTime + _maxTimeBetweenPoses ) {
          if ( _numValidPouncePoses > 0 ) {
            PRINT_CH_INFO("Behaviors", "BehaviorPounceOnMotion.ResetValid",
                          "resetting num valid poses because it has been %f seconds since the last one",
                          currTime - _lastValidPouncePoseTime);
            _numValidPouncePoses = 0;
          }
        }
      }
      break;
    }
      
    case MessageEngineToGameTag::CliffEvent:
    case MessageEngineToGameTag::RobotOffTreadsStateChanged:
    {
      // handled in always handle
      break;
    }
      
    default: {
      PRINT_NAMED_ERROR("BehaviorPounceOnMotion.AlwaysHandle.InvalidEvent", "");
      break;
    }
  }
}
 
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
template<typename T>
void BehaviorPounceOnMotion::PounceOnMotionWithCallback(BehaviorExternalInterface& behaviorExternalInterface, void(T::*callback)(BehaviorExternalInterface&),  IActionRunner* intermittentAction)
{
  // DEPRECATED - Grabbing robot to support current cozmo code, but this should
  // be removed
  Robot& robot = behaviorExternalInterface.GetRobot();
  CompoundActionSequential* compAction = new CompoundActionSequential(robot);
  
  if(intermittentAction != nullptr){
    compAction->AddAction(intermittentAction);
  }
  
  compAction->AddAction(new TriggerLiftSafeAnimationAction(robot, AnimationTrigger::PouncePounce));

  DelegateIfInControl(compAction, [this, callback](BehaviorExternalInterface& behaviorExternalInterface){
    // DEPRECATED - Grabbing robot to support current cozmo code, but this should
    // be removed
    Robot& robot = behaviorExternalInterface.GetRobot();
    // wait for the lift to relax 
    robot.GetMoveComponent().EnableLiftPower(false);
    SET_STATE(RelaxingLift);
    _relaxedLift = true;
    // We don't get an accurate pitch evaulation if the head is moving during an animation
    // so hold this for a bit longer
    const float relaxTime = 0.15f;
    
    DelegateIfInControl(new WaitAction(robot, relaxTime), [this, callback](BehaviorExternalInterface& behaviorExternalInterface){
      // DEPRECATED - Grabbing robot to support current cozmo code, but this should
      // be removed
      Robot& robot = behaviorExternalInterface.GetRobot();
      robot.GetMoveComponent().EnableLiftPower(true);
      _relaxedLift = false;
      (this->*callback)(behaviorExternalInterface);
    });
  });
  
  // reset count
  _motionObservedNoPounceCount = 0;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPounceOnMotion::Cleanup(BehaviorExternalInterface& behaviorExternalInterface)
{
  SET_STATE(Complete);
  if( _relaxedLift ) {
    // DEPRECATED - Grabbing robot to support current cozmo code, but this should
    // be removed
    Robot& robot = behaviorExternalInterface.GetRobot();
    robot.GetMoveComponent().EnableLiftPower(true);
    _relaxedLift = false;
  }
  
  _numValidPouncePoses = 0;
  _lastValidPouncePoseTime = 0.0f;
  _observedX = 0;
  _observedY = 0;
  
  // Only pop animations if set within this behavior
  if(!ShouldStreamline()){
    // DEPRECATED - Grabbing robot to support current cozmo code, but this should
    // be removed
    Robot& robot = behaviorExternalInterface.GetRobot();
    SmartRemoveIdleAnimation(behaviorExternalInterface);
    robot.GetDrivingAnimationHandler().RemoveDrivingAnimations(GetIDStr());
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPounceOnMotion::SetState_internal(State state, const std::string& stateName)
{
  _state = state;
  SetDebugStateName(stateName);
}

}
}

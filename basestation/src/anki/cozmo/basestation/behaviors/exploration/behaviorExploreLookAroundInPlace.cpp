/**
 * File: behaviorExploreLookAroundInPlace
 *
 * Author: Raul
 * Created: 03/11/16
 *
 * Description: Behavior for looking around the environment for stuff to interact with. This behavior starts
 * as a copy from behaviorLookAround, which we expect to eventually replace. The difference is this new
 * behavior will simply gather information and put it in a place accessible to other behaviors, rather than
 * actually handling the observed information itself.
 *
 * Copyright: Anki, Inc. 2016
 *
 **/
#include "behaviorExploreLookAroundInPlace.h"

#include "anki/cozmo/basestation/actions/basicActions.h"
#include "anki/cozmo/basestation/actions/animActions.h"
#include "anki/cozmo/basestation/events/animationTriggerHelpers.h"
#include "anki/cozmo/basestation/robot.h"
#include "anki/common/basestation/jsonTools.h"
#include "anki/common/basestation/math/point_impl.h"

#include "util/console/consoleInterface.h"
#include "util/logging/logging.h"
#include "util/math/numericCast.h"
#include "util/random/randomGenerator.h"
#include "anki/cozmo/basestation/viz/vizManager.h"

namespace Anki {
namespace Cozmo {

namespace {
CONSOLE_VAR(bool, kVizConeOfFocus, "Behavior.LookAroundInPlace", false);
static const char* kConfigParamsKey = "params";
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorExploreLookAroundInPlace::BehaviorExploreLookAroundInPlace(Robot& robot, const Json::Value& config)
: IBehavior(robot, config)
, _configParams{}
, _iterationStartingBodyFacing_rad(0.0f)
, _behaviorBodyFacingDone_rad(0.0f)
, _coneSidesReached(0)
, _mainTurnDirection(EClockDirection::CW)
, _s4HeadMovesRolled(0)
, _s4HeadMovesLeft(0)
{
  SetDefaultName("BehaviorExploreLookAroundInPlace");

  SubscribeToTags({
    EngineToGameTag::RobotOffTreadsStateChanged
  });
  
  // parse all parameters now
  LoadConfig(config[kConfigParamsKey]);

  if( !_configParams.behavior_ShouldResetTurnDirection ) {
    // we won't be resetting this, so set it once now
    DecideTurnDirection();
  }

  // We init the body direction to zero because behaviors are init before robot pose is set to anything real
  _initialBodyDirection = 0.f;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorExploreLookAroundInPlace::~BehaviorExploreLookAroundInPlace()
{  
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorExploreLookAroundInPlace::IsRunnableInternal(const Robot& robot) const
{
  // Probably want to run if I don't have any other exploration behavior that wants to, unless I have completely
  // mapped the floor around me 'recently'.
  // Now this is the case for exploration, but some other supergroup that uses the same behavior would have different
  // conditions. Maybe conditions should be datadriven?

  // NOTE: if _configParams.behavior_RecentLocationsMax == 0, we never add anything to _visitedLocations, so
  // this will return true
  
  bool nearRecentLocation = false;
  for( const auto& recentLocation : _visitedLocations )
  {
    // check distance to recent location (if can wrt robot)
    Pose3d distancePose;
    if( recentLocation.GetWithRespectTo(robot.GetPose(), distancePose) )
    {
      // if close to any recent location, flag
      const float distSQ = distancePose.GetTranslation().LengthSq();
      const float maxDistSq = _configParams.behavior_DistanceFromRecentLocationMin_mm*_configParams.behavior_DistanceFromRecentLocationMin_mm;
      if ( distSQ < maxDistSq ) {
        nearRecentLocation = true;
        break;
      }
    }
  }

  // TODO consider asking memory map for info and decide upon it instead of just based on recent locations?
  const bool canRun = !nearRecentLocation;
  return canRun;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorExploreLookAroundInPlace::LoadConfig(const Json::Value& config)
{
  using namespace JsonTools;
  const std::string& debugName = GetName() + ".BehaviorExploreLookAroundInPlace.LoadConfig";

  _configParams.behavior_DistanceFromRecentLocationMin_mm = ParseFloat(config, "behavior_DistanceFromRecentLocationMin_mm", debugName);
  _configParams.behavior_RecentLocationsMax = ParseUint8(config, "behavior_RecentLocationsMax", debugName);
  _configParams.behavior_ShouldResetTurnDirection = ParseBool(config, "behavior_ShouldResetTurnDirection", debugName);
  _configParams.behavior_ResetBodyFacingOnStart = ParseBool(config, "behavior_ResetBodyFacingOnStart", debugName);
  _configParams.behavior_ShouldLowerLift = ParseBool(config, "behavior_ShouldLowerLift", debugName);
  _configParams.behavior_AngleOfFocus_deg = ParseFloat(config, "behavior_AngleOfFocus_deg", debugName);
  // scans before stop (only if there's an angle of focus)
  if ( !Util::IsNearZero(_configParams.behavior_AngleOfFocus_deg) ) {
    _configParams.behavior_NumberOfScansBeforeStop = ParseUint8(config, "behavior_NumberOfScansBeforeStop", debugName);
  } else {
    _configParams.behavior_NumberOfScansBeforeStop = 0;
  }
  // turn speed
  _configParams.sx_BodyTurnSpeed_degPerSec = ParseFloat(config, "sx_BodyTurnSpeed_degPerSec", debugName);
  _configParams.sxt_HeadTurnSpeed_degPerSec = ParseFloat(config, "sxt_HeadTurnSpeed_degPerSec", debugName);
  _configParams.sxh_HeadTurnSpeed_degPerSec = ParseFloat(config, "sxh_HeadTurnSpeed_degPerSec", debugName);
  // chance that the main turn will be counter clockwise (vs ccw)
  _configParams.s0_MainTurnCWChance = ParseFloat(config, "s0_MainTurnCWChance", debugName);
  // [min,max] range for random turn angles for step 1
  _configParams.s1_BodyAngleRangeMin_deg = ParseFloat(config, "s1_BodyAngleRangeMin_deg", debugName);
  _configParams.s1_BodyAngleRangeMax_deg = ParseFloat(config, "s1_BodyAngleRangeMax_deg", debugName);
  _configParams.s1_HeadAngleRangeMin_deg = ParseFloat(config, "s1_HeadAngleRangeMin_deg", debugName);
  _configParams.s1_HeadAngleRangeMax_deg = ParseFloat(config, "s1_HeadAngleRangeMax_deg", debugName);
  // [min,max] range for pause for step 2
  _configParams.s2_WaitMin_sec = ParseFloat(config, "s2_WaitMin_sec", debugName);
  _configParams.s2_WaitMax_sec = ParseFloat(config, "s2_WaitMax_sec", debugName);
  _configParams.s2_WaitAnimTrigger = ParseString(config, "s2_WaitAnimTrigger", debugName);
  // [min,max] range for random angle turns for step 3
  _configParams.s3_BodyAngleRangeMin_deg = ParseFloat(config, "s3_BodyAngleRangeMin_deg", debugName);
  _configParams.s3_BodyAngleRangeMax_deg = ParseFloat(config, "s3_BodyAngleRangeMax_deg", debugName);
  _configParams.s3_HeadAngleRangeMin_deg = ParseFloat(config, "s3_HeadAngleRangeMin_deg", debugName);
  _configParams.s3_HeadAngleRangeMax_deg = ParseFloat(config, "s3_HeadAngleRangeMax_deg", debugName);
  // [min,max] range for head move for step 4
  _configParams.s4_BodyAngleRelativeRangeMin_deg = ParseFloat(config, "s4_BodyAngleRelativeRangeMin_deg", debugName);
  _configParams.s4_BodyAngleRelativeRangeMax_deg = ParseFloat(config, "s4_BodyAngleRelativeRangeMax_deg", debugName);
  _configParams.s4_HeadAngleRangeMin_deg = ParseFloat(config, "s4_HeadAngleRangeMin_deg", debugName);
  _configParams.s4_HeadAngleRangeMax_deg = ParseFloat(config, "s4_HeadAngleRangeMax_deg", debugName);
  _configParams.s4_HeadAngleChangesMin = ParseUint8(config, "s4_HeadAngleChangesMin", debugName);
  _configParams.s4_HeadAngleChangesMax = ParseUint8(config, "s4_HeadAngleChangesMax", debugName);
  _configParams.s4_WaitBetweenChangesMin_sec = ParseFloat(config, "s4_WaitBetweenChangesMin_sec", debugName);
  _configParams.s4_WaitBetweenChangesMax_sec = ParseFloat(config, "s4_WaitBetweenChangesMax_sec", debugName);
  _configParams.s4_WaitAnimTrigger = ParseString(config, "s4_WaitAnimTrigger", debugName);
  // [min,max] range for head move  for step 5
  _configParams.s5_BodyAngleRelativeRangeMin_deg = ParseFloat(config, "s5_BodyAngleRelativeRangeMin_deg", debugName);
  _configParams.s5_BodyAngleRelativeRangeMax_deg = ParseFloat(config, "s5_BodyAngleRelativeRangeMax_deg", debugName);
  _configParams.s5_HeadAngleRangeMin_deg = ParseFloat(config, "s5_HeadAngleRangeMin_deg", debugName);
  _configParams.s5_HeadAngleRangeMax_deg = ParseFloat(config, "s5_HeadAngleRangeMax_deg", debugName);
  // [min,max] range for random angle turns for step 6
  _configParams.s6_BodyAngleRangeMin_deg = ParseFloat(config, "s6_BodyAngleRangeMin_deg", debugName);
  _configParams.s6_BodyAngleRangeMax_deg = ParseFloat(config, "s6_BodyAngleRangeMax_deg", debugName);
  _configParams.s6_HeadAngleRangeMin_deg = ParseFloat(config, "s6_HeadAngleRangeMin_deg", debugName);
  _configParams.s6_HeadAngleRangeMax_deg = ParseFloat(config, "s6_HeadAngleRangeMax_deg", debugName);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Result BehaviorExploreLookAroundInPlace::InitInternal(Robot& robot)
{
  PRINT_CH_INFO("Behaviors", (GetName() + ".InitInternal").c_str(), "Starting first iteration");
  
  // grab run values
  _behaviorBodyFacingDone_rad = 0;
  _coneSidesReached = 0;
  
  // initial body direction is used to compare against the cone of focus. Demo behaviors always have a fixed cone
  // in front of where the robot is putdown, but freeplay behaviors need to restart the cone with the current facing
  if ( _configParams.behavior_ResetBodyFacingOnStart ) {
    _initialBodyDirection = robot.GetPose().GetRotationAngle<'Z'>();
  }
  
  // decide rotation direction at the beginning of the behavior if needed
  if( _configParams.behavior_ShouldResetTurnDirection ) {
    DecideTurnDirection();
  }

  // if we should lower the lift, do that now
  if( _configParams.behavior_ShouldLowerLift ) {
    IActionRunner* lowerLiftAction = new MoveLiftToHeightAction(robot, MoveLiftToHeightAction::Preset::LOW_DOCK);
    StartActing(lowerLiftAction, &BehaviorExploreLookAroundInPlace::BeginStateMachine);
  }
  else {
    BeginStateMachine(robot);
  }

  return Result::RESULT_OK;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorExploreLookAroundInPlace::BeginStateMachine(Robot& robot)
{

  if( kVizConeOfFocus ) {
    const bool hasConeOfFocus = !Util::IsNearZero(_configParams.behavior_AngleOfFocus_deg);
    if( hasConeOfFocus ) {
      robot.GetContext()->GetVizManager()->EraseSegments("BehaviorLookInPlace.FocusCone");

      Point3f center = robot.GetPose().GetWithRespectToOrigin().GetTranslation();
      float theta = _initialBodyDirection.ToFloat();
      float halfTurn = 0.5f * DEG_TO_RAD(_configParams.behavior_AngleOfFocus_deg);
      const float coneLength_mm = 200.0f;
      Point3f coneCenter {center.x() + coneLength_mm * cosf(theta),
                          center.y() + coneLength_mm * sinf(theta),
                          center.z()};
      Point3f coneLeft   {center.x() + coneLength_mm * cosf(theta + halfTurn),
                          center.y() + coneLength_mm * sinf(theta + halfTurn),
                          center.z()};
      Point3f coneRight  {center.x() + coneLength_mm * cosf(theta - halfTurn),
                          center.y() + coneLength_mm * sinf(theta - halfTurn),
                          center.z()};

      const float zOffset_mm = 20.0f;

      robot.GetContext()->GetVizManager()->DrawSegment("BehaviorLookInPlace.FocusCone",
                                                       center, coneCenter,
                                                       Anki::NamedColors::WHITE, false, zOffset_mm);
      robot.GetContext()->GetVizManager()->DrawSegment("BehaviorLookInPlace.FocusCone",
                                                       center, coneLeft,
                                                       Anki::NamedColors::YELLOW, false, zOffset_mm);
      robot.GetContext()->GetVizManager()->DrawSegment("BehaviorLookInPlace.FocusCone",
                                                       center, coneRight,
                                                       Anki::NamedColors::YELLOW, false, zOffset_mm);
    }
  }
  
  TransitionToS1_OppositeTurn(robot);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorExploreLookAroundInPlace::AlwaysHandle(const EngineToGameEvent& event, const Robot& robot)
{
  switch (event.GetData().GetTag())
  {
    case EngineToGameTag::RobotOffTreadsStateChanged:
    {
      if(event.GetData().Get_RobotOffTreadsStateChanged().treadsState == OffTreadsState::OnTreads){
        _initialBodyDirection = robot.GetPose().GetRotationAngle<'Z'>();
      }
      break;
    }
    default:
    {
      break;
    }
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorExploreLookAroundInPlace::TransitionToS1_OppositeTurn(Robot& robot)
{
  SetDebugStateName("TransitionToS1_OppositeTurn");
  
  // cache iteration values
  _iterationStartingBodyFacing_rad = robot.GetPose().GetRotationAngle<'Z'>();
  
  // create turn action for this state
  const EClockDirection turnDir = GetOpposite(_mainTurnDirection);
  IAction* turnAction = CreateBodyAndHeadTurnAction(robot, turnDir,
        _configParams.s1_BodyAngleRangeMin_deg, _configParams.s1_BodyAngleRangeMax_deg,
        _configParams.s1_HeadAngleRangeMin_deg, _configParams.s1_HeadAngleRangeMax_deg,
        _configParams.sx_BodyTurnSpeed_degPerSec, _configParams.sxt_HeadTurnSpeed_degPerSec);

  // request action with transition to proper state
  StartActing( turnAction, &BehaviorExploreLookAroundInPlace::TransitionToS2_Pause );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorExploreLookAroundInPlace::TransitionToS2_Pause(Robot& robot)
{
  SetDebugStateName("TransitionToS2_Pause");
  
  IAction* pauseAction = nullptr;
  
  const std::string& animGroupName = _configParams.s2_WaitAnimTrigger;
  AnimationTrigger trigger = animGroupName.empty()  ? AnimationTrigger::Count : AnimationTriggerFromString(animGroupName.c_str());
  if ( trigger != AnimationTrigger::Count )
  {
    pauseAction = new TriggerLiftSafeAnimationAction(robot,trigger);
  }
  else
  {
    // create wait action
    const double waitTime_sec = GetRNG().RandDblInRange(_configParams.s2_WaitMin_sec, _configParams.s2_WaitMax_sec);
    pauseAction = new WaitAction( robot, waitTime_sec );
  }
  
  // request action with transition to proper state
  ASSERT_NAMED( nullptr!=pauseAction, "BehaviorExploreLookAroundInPlace::TransitionToS2_Pause.NullAction");
  StartActing( pauseAction, &BehaviorExploreLookAroundInPlace::TransitionToS3_MainTurn );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorExploreLookAroundInPlace::TransitionToS3_MainTurn(Robot& robot)
{
  SetDebugStateName("TransitionToS3_MainTurn");
  
  // create turn action for this state
  const EClockDirection turnDir = _mainTurnDirection;
  IAction* turnAction = CreateBodyAndHeadTurnAction(robot, turnDir,
        _configParams.s3_BodyAngleRangeMin_deg, _configParams.s3_BodyAngleRangeMax_deg,
        _configParams.s3_HeadAngleRangeMin_deg, _configParams.s3_HeadAngleRangeMax_deg,
        _configParams.sx_BodyTurnSpeed_degPerSec, _configParams.sxt_HeadTurnSpeed_degPerSec);
  
  // calculate head moves now
  const int randMoves = GetRNG().RandIntInRange(_configParams.s4_HeadAngleChangesMin, _configParams.s4_HeadAngleChangesMax);
  _s4HeadMovesRolled = Util::numeric_cast<uint8_t>(randMoves);
  _s4HeadMovesLeft = _s4HeadMovesRolled;
  
  // request action with transition to proper state
  if( _s4HeadMovesLeft != 0 )
  {
    StartActing( turnAction, &BehaviorExploreLookAroundInPlace::TransitionToS4_HeadOnlyUp );
  }
  else // avoid uint overflow and skip to turning back.
  {
    StartActing( turnAction, &BehaviorExploreLookAroundInPlace::TransitionToS6_MainTurnFinal );
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorExploreLookAroundInPlace::TransitionToS4_HeadOnlyUp(Robot& robot)
{
  {
    std::string stateName = "TransitionToS4_HeadOnlyUp (" + std::to_string(_s4HeadMovesLeft) + "/" +
      std::to_string(_s4HeadMovesRolled) + ")";
    SetDebugStateName(stateName);
  }

  // cache the rotation the first time that we run S4
  const bool isFirstMove = (_s4HeadMovesLeft == _s4HeadMovesRolled);
  if ( isFirstMove )  {
    // set current facing for the next state
    _s4_s5StartingBodyFacing_rad = robot.GetPose().GetRotationAngle<'Z'>();
  }

  // count the action we are going to queue as a move
  --_s4HeadMovesLeft;
  const bool isLastMove = (_s4HeadMovesLeft == 0);

  // check which transition method to call after the head move is done, S5 or S4 again?
  using TransitionCallback = void(BehaviorExploreLookAroundInPlace::*)(Robot&);
  TransitionCallback nextCallback = isLastMove ?
    &BehaviorExploreLookAroundInPlace::TransitionToS5_HeadOnlyDown :
    &BehaviorExploreLookAroundInPlace::TransitionToS4_HeadOnlyUp;

  // this is the lambda that will run after the wait action finishes
  auto runAfterPause = [this, &robot, nextCallback](const ExternalInterface::RobotCompletedAction& actionRet)
  {
    PRINT_CH_INFO("Behaviors", (GetName() + ".S4.AfterPause").c_str(),
      "Previous action finished with code [%s]. Creating HeadTurnAction:",
      EnumToString(actionRet.result)
    );
    
    // create head move action
    IAction* moveHeadAction = CreateHeadTurnAction(robot,
          _configParams.s4_BodyAngleRelativeRangeMin_deg,
          _configParams.s4_BodyAngleRelativeRangeMax_deg,
          _s4_s5StartingBodyFacing_rad.getDegrees(),
          _configParams.s4_HeadAngleRangeMin_deg,
          _configParams.s4_HeadAngleRangeMax_deg,
          _configParams.sx_BodyTurnSpeed_degPerSec,
          _configParams.sxh_HeadTurnSpeed_degPerSec);
  
    // do head action and transition to next state or same (depending on callback)
    StartActing( moveHeadAction, nextCallback );
  };
  
  IAction* pauseAction = nullptr;
  const std::string& animGroupName = _configParams.s4_WaitAnimTrigger;
  AnimationTrigger trigger = animGroupName.empty()  ? AnimationTrigger::Count : AnimationTriggerFromString(animGroupName.c_str());
  if ( trigger != AnimationTrigger::Count )
  {
    pauseAction = new TriggerLiftSafeAnimationAction(robot, trigger);
  }
  else
  {
    // create wait action
    const double waitTime_sec = GetRNG().RandDblInRange(_configParams.s4_WaitBetweenChangesMin_sec,
                                                      _configParams.s4_WaitBetweenChangesMax_sec);
    pauseAction = new WaitAction( robot, waitTime_sec );
  }
  
  PRINT_CH_INFO("Behaviors", (GetName() + ".S4.StartingPauseAnimAction").c_str(), "Triggering %s",
    (trigger != AnimationTrigger::Count) ? animGroupName.c_str() : "pause" );
  
  // request action with transition to proper state
  ASSERT_NAMED( nullptr!=pauseAction, "BehaviorExploreLookAroundInPlace::TransitionToS4_HeadOnlyUp.NullPauseAction");
  StartActing( pauseAction, runAfterPause );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorExploreLookAroundInPlace::TransitionToS5_HeadOnlyDown(Robot& robot)
{
  SetDebugStateName("TransitionToS5_HeadOnlyDown");
  
  // create head move action for this state
  IAction* moveHeadAction = CreateHeadTurnAction(robot,
        _configParams.s5_BodyAngleRelativeRangeMin_deg,
        _configParams.s5_BodyAngleRelativeRangeMax_deg,
        _s4_s5StartingBodyFacing_rad.getDegrees(),
        _configParams.s5_HeadAngleRangeMin_deg,
        _configParams.s5_HeadAngleRangeMax_deg,
        _configParams.sx_BodyTurnSpeed_degPerSec,
        _configParams.sxh_HeadTurnSpeed_degPerSec);

  // request action with transition to proper state
  StartActing( moveHeadAction, &BehaviorExploreLookAroundInPlace::TransitionToS6_MainTurnFinal );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorExploreLookAroundInPlace::TransitionToS6_MainTurnFinal(Robot& robot)
{
  SetDebugStateName("TransitionToS6_MainTurnFinal");
  // create turn action for this state
  const EClockDirection turnDir = _mainTurnDirection;
  IAction* turnAction = CreateBodyAndHeadTurnAction(robot, turnDir,
        _configParams.s6_BodyAngleRangeMin_deg, _configParams.s6_BodyAngleRangeMax_deg,
        _configParams.s6_HeadAngleRangeMin_deg, _configParams.s6_HeadAngleRangeMax_deg,
        _configParams.sx_BodyTurnSpeed_degPerSec, _configParams.sxt_HeadTurnSpeed_degPerSec);

  // request action with transition to proper state
  StartActing( turnAction, &BehaviorExploreLookAroundInPlace::TransitionToS7_IterationEnd );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorExploreLookAroundInPlace::TransitionToS7_IterationEnd(Robot& robot)
{
  SetDebugStateName("TransitionToS7_IterationEnd");

  _numIterationsCompleted++;
  
  Radians currentZ_rad = robot.GetPose().GetRotationAngle<'Z'>();
  float doneThisIteration_rad = (currentZ_rad - _iterationStartingBodyFacing_rad).ToFloat();
  _behaviorBodyFacingDone_rad += doneThisIteration_rad;

  // assert we are not turning more than PI in one iteration (because of Radian rescaling)
  if( FLT_GT(doneThisIteration_rad,0.0f) != FLT_GT(GetTurnSign(_mainTurnDirection), 0.0f) ) {
    // this can happen if the robot gets turned / messed with. Eventually, we should handle this in a reaction
    PRINT_NAMED_WARNING("BehaviorExploreLookAroundInPlace.TransitionToS7_IterationEnd.BadSign",
                        "doneThisIterationRad = %f, TurnSign=%f",
                        doneThisIteration_rad,
                        GetTurnSign(_mainTurnDirection));
  }

  // check if we are done
  bool startAnotherIteration = true;
  
  // if we have a cone of focus
  const bool hasConeOfFocus = !Util::IsNearZero(_configParams.behavior_AngleOfFocus_deg);
  if ( hasConeOfFocus )
  {
    // check if we have reached one side of the cone
    const Radians curBodyDirection = robot.GetPose().GetRotationAngle<'Z'>();
    const float angleDiff_deg = (curBodyDirection - _initialBodyDirection).getDegrees() * GetTurnSign(_mainTurnDirection);
    const bool reachedConeSide = angleDiff_deg >= _configParams.behavior_AngleOfFocus_deg * 0.5f;
    if ( reachedConeSide )
    {
      // we did reach a side, note it down
      ++_coneSidesReached; // can overflow in infinite loops, but should not be an issue
      PRINT_CH_INFO("Behaviors", (GetName() + ".IterationEnd").c_str(), "Reached cone side %d", _coneSidesReached);
      
      // bounce if we are asked infinite scans or if we have not reached the desired number
      const bool bounce = (_configParams.behavior_NumberOfScansBeforeStop == 0) ||
                          ( (_coneSidesReached/2) < _configParams.behavior_NumberOfScansBeforeStop);
      if ( bounce ) {
        // change direction and flag to start another iteration
        _mainTurnDirection = GetOpposite(_mainTurnDirection);
      } else {
        // we don't want to bounce anymore, do not start another iteration
        startAnotherIteration = false;
      }
    }
  }
  else
  {
    PRINT_CH_INFO("Behaviors", (GetName() + ".IterationEnd").c_str(),
      "Done %.2f deg so far",
      fabsf(RAD_TO_DEG_F32(_behaviorBodyFacingDone_rad)));
    
    // no cone of focus
    // while not completed a whole turn start another iteration
    const bool hasDone360 = fabsf(_behaviorBodyFacingDone_rad) >= 2*PI;
    startAnotherIteration = !hasDone360;
  }
  
  // act depending on whether we have to do another iteration or not
  if ( startAnotherIteration )
  {
    PRINT_CH_INFO("Behaviors", (GetName() + ".IterationEnd").c_str(), "Starting another iteration");
    TransitionToS1_OppositeTurn(robot);
  }
  else
  {
    PRINT_CH_INFO("Behaviors", (GetName() + ".IterationEnd").c_str(), "Done (reached max iterations)");

    if( _configParams.behavior_RecentLocationsMax >= 0 ) {
      // we have finished at this location, note down as recent location (make room if necessary)
      if ( _visitedLocations.size() >= _configParams.behavior_RecentLocationsMax ) {
        assert( !_visitedLocations.empty() ); // otherwise the behavior would run forever
        _visitedLocations.pop_front();
      }
  
      // note down this location so that we don't do it again in the same place
      _visitedLocations.emplace_back( robot.GetPose() );
    }
  }

}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorExploreLookAroundInPlace::DecideTurnDirection()
{
  // decide main turn direction
  const double randomDirection = GetRNG().RandDbl();
  _mainTurnDirection = (randomDirection<=_configParams.s0_MainTurnCWChance) ? EClockDirection::CW : EClockDirection::CCW;
  // _mainTurnDirection = EClockDirection::CW;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
IAction* BehaviorExploreLookAroundInPlace::CreateBodyAndHeadTurnAction(Robot& robot, EClockDirection clockDirection,
  float bodyStartRelativeMin_deg, float bodyStartRelativeMax_deg,
  float headAbsoluteMin_deg, float headAbsoluteMax_deg,
  float bodyTurnSpeed_degPerSec, float headTurnSpeed_degPerSec)
{
  // [min,max] range for random body angle turn
  const EClockDirection turnDir = clockDirection;
  const double bodyTargetAngleRelative_deg =
    GetRNG().RandDblInRange(bodyStartRelativeMin_deg, bodyStartRelativeMax_deg) * GetTurnSign(turnDir);
  
  // [min,max] range for random head angle turn
  const double headTargetAngleAbs_deg =
    GetRNG().RandDblInRange(headAbsoluteMin_deg, headAbsoluteMax_deg);

  // create proper action for body & head turn
  const Radians bodyTargetAngleAbs_rad( _iterationStartingBodyFacing_rad + DEG_TO_RAD(bodyTargetAngleRelative_deg) );
  const Radians headTargetAngleAbs_rad( DEG_TO_RAD(headTargetAngleAbs_deg) );
  PanAndTiltAction* turnAction = new PanAndTiltAction(robot, bodyTargetAngleAbs_rad, headTargetAngleAbs_rad, true, true);
  turnAction->SetMaxPanSpeed( DEG_TO_RAD(bodyTurnSpeed_degPerSec) );
  turnAction->SetMaxTiltSpeed( DEG_TO_RAD(headTurnSpeed_degPerSec) );

// Code for debugging
//  PRINT_NAMED_WARNING("RSAM", "STATE %s (bh) set BODY %.2f, HEAD %.2f (curB %.2f, curH %.2f)",
//    GetStateName().c_str(),
//    RAD_TO_DEG(bodyTargetAngleAbs_rad.ToFloat()),
//    RAD_TO_DEG(headTargetAngleAbs_rad.ToFloat()),
//    RAD_TO_DEG(robot.GetPose().GetRotationAngle<'Z'>().ToFloat()),
//    robot.GetHeadAngle() );

  return turnAction;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
IAction* BehaviorExploreLookAroundInPlace::CreateHeadTurnAction(Robot& robot,
  float bodyRelativeMin_deg, float bodyRelativeMax_deg,
  float bodyReference_deg,
  float headAbsoluteMin_deg, float headAbsoluteMax_deg,
  float bodyTurnSpeed_degPerSec, float headTurnSpeed_degPerSec)
{
  // generate turn sign
  const EClockDirection turnDir = (GetRNG().RandInt(2) == 0) ? EClockDirection::CW : EClockDirection::CCW;

  // [min,max] range for random body angle turn
  const double bodyTargetAngleRelative_deg =
    GetRNG().RandDblInRange(bodyRelativeMin_deg, bodyRelativeMax_deg) * GetTurnSign(turnDir);

  // [min,max] range for random head angle turn
  const double headTargetAngleAbs_deg =
    GetRNG().RandDblInRange(headAbsoluteMin_deg, headAbsoluteMax_deg);
  
  // create proper action for body & head turn
  const Radians bodyTargetAngleAbs_rad( DEG_TO_RAD(bodyReference_deg + bodyTargetAngleRelative_deg) );
  const Radians headTargetAngleAbs_rad( DEG_TO_RAD(headTargetAngleAbs_deg) );
  PanAndTiltAction* turnAction = new PanAndTiltAction(robot, bodyTargetAngleAbs_rad, headTargetAngleAbs_rad, true, true);
  turnAction->SetMaxPanSpeed( DEG_TO_RAD(bodyTurnSpeed_degPerSec) );
  turnAction->SetMaxTiltSpeed( DEG_TO_RAD(headTurnSpeed_degPerSec) );

  PRINT_CH_INFO("Behaviors", (GetName() + ".PanAndTilt").c_str(), "Body %.2f, Head %.2f, BSpeed %.2f, HSpeed %.2f",
    bodyTargetAngleAbs_rad.getDegrees(),
    headTargetAngleAbs_rad.getDegrees(),
    bodyTurnSpeed_degPerSec,
    headTurnSpeed_degPerSec);
  
// code for debugging
//  PRINT_NAMED_WARNING("RSAM", "STATE %s (ho) set BODY %.2f, HEAD %.2f (curB %.2f, curH %.2f)",
//    GetStateName().c_str(),
//    RAD_TO_DEG(bodyTargetAngleAbs_rad.ToFloat()),
//    RAD_TO_DEG(headTargetAngleAbs_rad.ToFloat()),
//    RAD_TO_DEG(robot.GetPose().GetRotationAngle<'Z'>().ToFloat()),
//    robot.GetHeadAngle() );

  return turnAction;
}

} // namespace Cozmo
} // namespace Anki

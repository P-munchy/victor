/**
 * File: behaviorInteractWithFaces.cpp
 *
 * Author: Andrew Stein
 * Date:   7/30/15
 *
 * Description: Implements Cozmo's "InteractWithFaces" behavior, which tracks/interacts with faces if it finds one.
 *
 * Copyright: Anki, Inc. 2015
 **/

#include "anki/cozmo/basestation/behaviorSystem/behaviors/basicWorldInteractions/behaviorInteractWithFaces.h"

#include "anki/cozmo/basestation/actions/animActions.h"
#include "anki/cozmo/basestation/actions/basicActions.h"
#include "anki/cozmo/basestation/actions/trackFaceAction.h"
#include "anki/cozmo/basestation/aiComponent/AIWhiteboard.h"
#include "anki/cozmo/basestation/aiComponent/aiComponent.h"
#include "anki/cozmo/basestation/behaviorSystem/behaviorPreReqs/behaviorPreReqRobot.h"
#include "anki/cozmo/basestation/blockWorld/blockWorld.h"
#include "anki/cozmo/basestation/cozmoContext.h"
#include "anki/cozmo/basestation/events/ankiEvent.h"
#include "anki/cozmo/basestation/externalInterface/externalInterface.h"
#include "anki/cozmo/basestation/faceWorld.h"
#include "anki/cozmo/basestation/moodSystem/moodManager.h"
#include "anki/cozmo/basestation/navMemoryMap/iNavMemoryMap.h"
#include "anki/cozmo/basestation/navMemoryMap/navMemoryMapTypes.h"
#include "anki/cozmo/basestation/needsSystem/needsManager.h"
#include "anki/cozmo/basestation/robot.h"
#include "anki/cozmo/basestation/viz/vizManager.h"

#include "anki/common/basestation/jsonTools.h"
#include "anki/common/basestation/utils/timer.h"

#include "anki/vision/basestation/faceTracker.h"
#include "anki/vision/basestation/trackedFace.h"

#include "util/console/consoleInterface.h"


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
//
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
#define CONSOLE_GROUP "Behavior.InteractWithFaces"

namespace Anki {
namespace Cozmo {

namespace {

// how far forward to check and ideally drive
CONSOLE_VAR_RANGED(f32, kInteractWithFaces_DriveForwardIdealDist_mm, CONSOLE_GROUP, 40.0f, 0.0f, 200.0f);

// how far forward to move in case the check fails
CONSOLE_VAR_RANGED(f32, kInteractWithFaces_DriveForwardMinDist_mm, CONSOLE_GROUP, -15.0f, -100.0f, 100.0f);

// if true, do a glance down before the memory map check (only valid if we are doing the check)
// TODO:(bn) could check memory map for Unknown, and only glance down in that case
CONSOLE_VAR(bool, kInteractWithFaces_DoGlanceDown, CONSOLE_GROUP, false);

// if false, always drive the "ideal" distance without checking anything. If true, check memory map to
// determine which distance to drive
CONSOLE_VAR(bool, kInteractWithFaces_DoMemoryMapCheckForDriveForward, CONSOLE_GROUP, true);

CONSOLE_VAR(bool, kInteractWithFaces_VizMemoryMapCheck, CONSOLE_GROUP, false);

CONSOLE_VAR_RANGED(f32, kInteractWithFaces_DriveForwardSpeed_mmps, CONSOLE_GROUP, 40.0f, 0.0f, 200.0f);

// Minimum angles to turn during tracking to keep the robot moving and looking alive
CONSOLE_VAR_RANGED(f32, kInteractWithFaces_MinTrackingPanAngle_deg,  CONSOLE_GROUP, 4.0f, 0.0f, 30.0f);
CONSOLE_VAR_RANGED(f32, kInteractWithFaces_MinTrackingTiltAngle_deg, CONSOLE_GROUP, 4.0f, 0.0f, 30.0f);

// If we are doing the memory map check, these are the types which will prevent us from driving the ideal
// distance
constexpr NavMemoryMapTypes::FullContentArray typesToBlockDriving =
{
  {NavMemoryMapTypes::EContentType::Unknown               , false},
  {NavMemoryMapTypes::EContentType::ClearOfObstacle       , false},
  {NavMemoryMapTypes::EContentType::ClearOfCliff          , false},
  {NavMemoryMapTypes::EContentType::ObstacleCube          , true },
  {NavMemoryMapTypes::EContentType::ObstacleCubeRemoved   , false},
  {NavMemoryMapTypes::EContentType::ObstacleCharger       , true },
  {NavMemoryMapTypes::EContentType::ObstacleChargerRemoved, false},
  {NavMemoryMapTypes::EContentType::ObstacleProx          , true },
  {NavMemoryMapTypes::EContentType::ObstacleUnrecognized  , true },
  {NavMemoryMapTypes::EContentType::Cliff                 , true },
  {NavMemoryMapTypes::EContentType::InterestingEdge       , true },
  {NavMemoryMapTypes::EContentType::NotInterestingEdge    , true }
};
static_assert(NavMemoryMapTypes::IsSequentialArray(typesToBlockDriving),
  "This array does not define all types once and only once.");

}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorInteractWithFaces::BehaviorInteractWithFaces(Robot &robot, const Json::Value& config)
: IBehavior(robot, config)
{
  LoadConfig(config["params"]);

  SubscribeToTags({ EngineToGameTag::RobotChangedObservedFaceID });
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorInteractWithFaces::LoadConfig(const Json::Value& config)
{
  using namespace JsonTools;
  const std::string& debugName = "BehaviorInteractWithFaces.BehaviorInteractWithFaces.LoadConfig";

  _configParams.minTimeToTrackFace_s = ParseFloat(config, "minTimeToTrackFace_s", debugName);
  _configParams.maxTimeToTrackFace_s = ParseFloat(config, "maxTimeToTrackFace_s", debugName);

  if( ! ANKI_VERIFY(_configParams.maxTimeToTrackFace_s >= _configParams.minTimeToTrackFace_s,
                    "BehaviorInteractWithFaces.LoadConfig.InvalidTrackingTime",
                    "%s: minTrackTime = %f, maxTrackTime = %f",
                    GetIDStr().c_str(),
                    _configParams.minTimeToTrackFace_s,
                    _configParams.maxTimeToTrackFace_s) ) {
    _configParams.maxTimeToTrackFace_s = _configParams.minTimeToTrackFace_s;
  }

  _configParams.clampSmallAngles = ParseBool(config, "clampSmallAngles", debugName);
  if( _configParams.clampSmallAngles ) {
    _configParams.minClampPeriod_s = ParseFloat(config, "minClampPeriod_s", debugName);
    _configParams.maxClampPeriod_s = ParseFloat(config, "maxClampPeriod_s", debugName);

    if( ! ANKI_VERIFY(_configParams.maxClampPeriod_s >= _configParams.minClampPeriod_s,
                      "BehaviorInteractWithFaces.LoadConfig.InvalidClampPeriod",
                      "%s: minPeriod = %f, maxPeriod = %f",
                      GetIDStr().c_str(),
                      _configParams.minClampPeriod_s,
                      _configParams.maxClampPeriod_s) ) {
      _configParams.maxClampPeriod_s = _configParams.minClampPeriod_s;
    }

  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Result BehaviorInteractWithFaces::InitInternal(Robot& robot)
{
  // reset the time to stop tracking (in the tracking state)
  _trackFaceUntilTime_s = -1.0f;

  if( _targetFace != Vision::UnknownFaceID ) {
    TransitionToInitialReaction(robot);
    return RESULT_OK;
  }
  else {
    PRINT_NAMED_WARNING("BehaviorInteractWithFaces.Init.NoValidTarget",
                        "Decided to run, but don't have valid target when Init is called. This shouldn't happen");
    return RESULT_FAIL;
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
IBehavior::Status BehaviorInteractWithFaces::UpdateInternal(Robot& robot)
{
  if( _trackFaceUntilTime_s >= 0.0f ) {
    const float currTime_s = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
    if( currTime_s >= _trackFaceUntilTime_s ) {
      BehaviorObjectiveAchieved(BehaviorObjective::InteractedWithFace);
      StopActing();
      robot.GetContext()->GetNeedsManager()->RegisterNeedsActionCompleted(NeedsActionId::SeeFace);
    }
  }
  
  return BaseClass::UpdateInternal(robot);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorInteractWithFaces::IsRunnableInternal(const BehaviorPreReqRobot& preReqData) const
{
  _targetFace = Vision::UnknownFaceID;
  SelectFaceToTrack(preReqData.GetRobot());

  return _targetFace != Vision::UnknownFaceID;
}

  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorInteractWithFaces::StopInternal(Robot& robot)
{
  _lastImageTimestampWhileRunning = robot.GetLastImageTimeStamp();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorInteractWithFaces::CanDriveIdealDistanceForward(const Robot& robot)
{
  if( kInteractWithFaces_DoMemoryMapCheckForDriveForward ) {

    const INavMemoryMap* memoryMap = robot.GetBlockWorld().GetNavMemoryMap();
    DEV_ASSERT(nullptr != memoryMap, "BehaviorInteractWithFaces.CanDriveIdealDistanceForward.NeedMemoryMap");

    const Vec3f& fromRobot = robot.GetPose().GetTranslation();

    const Vec3f ray{kInteractWithFaces_DriveForwardIdealDist_mm, 0.0f, 0.0f};
    const Vec3f toGoal = robot.GetPose() * ray;
    
    const bool hasCollision = memoryMap->HasCollisionRayWithTypes(fromRobot, toGoal, typesToBlockDriving);

    if( kInteractWithFaces_VizMemoryMapCheck ) {
      const char* vizID = "BehaviorInteractWithFaces.MemMapCheck";
      const float zOffset_mm = 15.0f;
      robot.GetContext()->GetVizManager()->EraseSegments(vizID);
      robot.GetContext()->GetVizManager()->DrawSegment(vizID,
                                                       fromRobot, toGoal,
                                                       hasCollision ? Anki::NamedColors::YELLOW
                                                                    : Anki::NamedColors::BLUE,
                                                       false,
                                                       zOffset_mm);
    }

    const bool canDriveIdealDist = !hasCollision;
    return canDriveIdealDist;
  }
  else {
    // always drive ideal distance
    return true;
  }
}

#pragma mark -
#pragma mark State Machine

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorInteractWithFaces::TransitionToInitialReaction(Robot& robot)
{
  DEBUG_SET_STATE(VerifyFace);

  CompoundActionSequential* action = new CompoundActionSequential(robot);

  {
    TurnTowardsFaceAction* turnAndAnimateAction = new TurnTowardsFaceAction(robot, _targetFace, M_PI_F, true);
    turnAndAnimateAction->SetSayNameAnimationTrigger(AnimationTrigger::InteractWithFacesInitialNamed);
    turnAndAnimateAction->SetNoNameAnimationTrigger(AnimationTrigger::InteractWithFacesInitialUnnamed);
    turnAndAnimateAction->SetRequireFaceConfirmation(true);
    action->AddAction(turnAndAnimateAction);
  }
  
  StartActing(action, [this, &robot](ActionResult ret ) {
      if( ret == ActionResult::SUCCESS ) {
        TransitionToGlancingDown(robot);
      }
      else {
        // one possible cause of failure is that the face id we tried to track wasn't there (but another face
        // was). So, see if there is a new "best face", and if so, track that one. This will only run if a new
        // face is observed.

        // increase frustration to avoid loops
        robot.GetMoodManager().TriggerEmotionEvent("InteractWithFaceRetry", MoodManager::GetCurrentTimeInSeconds());
        
        _lastImageTimestampWhileRunning = robot.GetLastImageTimeStamp();
        FaceID_t oldTargetFace = _targetFace;
        SelectFaceToTrack(robot);
        if( _targetFace != oldTargetFace ) {
          // only retry a max of one time to avoid loops
          PRINT_CH_INFO("Behaviors","BehaviorInteractWithFaces.InitialReactionFailed.TryAgain",
                        "tracking face %d failed, but will try again with face %d",
                        oldTargetFace,
                        _targetFace);

          TransitionToInitialReaction(robot);
        }
        else {
          PRINT_CH_INFO("Behaviors","BehaviorInteractWithFaces.InitialReactionFailed",
                        "compound action failed with result '%s', not retrying",
                        ActionResultToString(ret));
        }
      }
    });
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorInteractWithFaces::TransitionToGlancingDown(Robot& robot)
{
  DEBUG_SET_STATE(GlancingDown);

  if( kInteractWithFaces_DoGlanceDown && kInteractWithFaces_DoMemoryMapCheckForDriveForward ) {
    // TODO:(bn) get a better measurement for this and put it in cozmo config
    const float kLowHeadAngle_rads = DEG_TO_RAD(-10.0f);
    StartActing(new MoveHeadToAngleAction(robot, kLowHeadAngle_rads),
                &BehaviorInteractWithFaces::TransitionToDrivingForward);
  }
  else {
    TransitionToDrivingForward(robot);
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorInteractWithFaces::TransitionToDrivingForward(Robot& robot)
{
  DEBUG_SET_STATE(DrivingForward);
  
  // check if we should do the long or short distance
  const bool doLongDrive = CanDriveIdealDistanceForward(robot);
  const float distToDrive_mm = doLongDrive ?
    kInteractWithFaces_DriveForwardIdealDist_mm :
    kInteractWithFaces_DriveForwardMinDist_mm;

  // drive straight while keeping the head tracking the (players) face
  CompoundActionParallel* action = new CompoundActionParallel(robot);

  // the head tracking action normally loops forever, so set up the drive action first, tell it to emit
  // completion signals, then pass it's tag in to the tracking action so the tracking action can stop itself
  // when the driving action finishes

  u32 driveActionTag = 0;
  {
    // don't play driving animations (to avoid sounds which don't make sense here)
    // TODO:(bn) custom driving animations for this action?
    DriveStraightAction* driveAction = new DriveStraightAction(robot,
                                                               distToDrive_mm,
                                                               kInteractWithFaces_DriveForwardSpeed_mmps,
                                                               false);
    const bool ignoreFailure = false;
    const bool emitCompletionSignal = true;
    action->AddAction( driveAction, ignoreFailure, emitCompletionSignal );
    driveActionTag = driveAction->GetTag();
  }

  {
    TrackFaceAction* trackWithHeadAction = new TrackFaceAction(robot, _targetFace);
    trackWithHeadAction->SetMode(ITrackAction::Mode::HeadOnly);
    trackWithHeadAction->StopTrackingWhenOtherActionCompleted( driveActionTag );
    trackWithHeadAction->SetTiltTolerance(DEG_TO_RAD(kInteractWithFaces_MinTrackingPanAngle_deg));
    trackWithHeadAction->SetPanTolerance(DEG_TO_RAD(kInteractWithFaces_MinTrackingTiltAngle_deg));
    trackWithHeadAction->SetClampSmallAnglesToTolerances(_configParams.clampSmallAngles);
    trackWithHeadAction->SetClampSmallAnglesPeriod(_configParams.minClampPeriod_s, _configParams.maxClampPeriod_s);

    action->AddAction( trackWithHeadAction );
  }
  
  // TODO:(bn) alternate driving animations?
  StartActing(action, &BehaviorInteractWithFaces::TransitionToTrackingFace);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorInteractWithFaces::TransitionToTrackingFace(Robot& robot)
{
  DEBUG_SET_STATE(TrackingFace);

  const float randomTimeToTrack_s = Util::numeric_cast<float>(
    GetRNG().RandDblInRange(_configParams.minTimeToTrackFace_s, _configParams.maxTimeToTrackFace_s));
  PRINT_CH_INFO("Behaviors", "BehaviorInteractWithFaces.TrackTime", "will track for %f seconds", randomTimeToTrack_s);
  _trackFaceUntilTime_s = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + randomTimeToTrack_s;

  CompoundActionParallel* action = new CompoundActionParallel(robot);

  {
    TrackFaceAction* trackAction = new TrackFaceAction(robot, _targetFace);
    trackAction->SetTiltTolerance(DEG_TO_RAD(kInteractWithFaces_MinTrackingPanAngle_deg));
    trackAction->SetPanTolerance(DEG_TO_RAD(kInteractWithFaces_MinTrackingTiltAngle_deg));
    trackAction->SetClampSmallAnglesToTolerances(_configParams.clampSmallAngles);
    trackAction->SetClampSmallAnglesPeriod(_configParams.minClampPeriod_s, _configParams.maxClampPeriod_s);
    action->AddAction(trackAction);
  }
  
  // loop animation forever to keep the eyes moving
  action->AddAction(new TriggerAnimationAction(robot, AnimationTrigger::InteractWithFaceTrackingIdle, 0));
  
  StartActing(action, &BehaviorInteractWithFaces::TransitionToTriggerEmotionEvent);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorInteractWithFaces::TransitionToTriggerEmotionEvent(Robot& robot)
{
  DEBUG_SET_STATE(TriggerEmotionEvent);

  const Vision::TrackedFace* face = robot.GetFaceWorld().GetFace( _targetFace );
  if( nullptr != face && face->HasName() ) {
    robot.GetMoodManager().TriggerEmotionEvent("InteractWithNamedFace", MoodManager::GetCurrentTimeInSeconds());
  }
  else {
    robot.GetMoodManager().TriggerEmotionEvent("InteractWithUnnamedFace", MoodManager::GetCurrentTimeInSeconds());
  }
}
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorInteractWithFaces::SelectFaceToTrack(const Robot& robot) const
{  
  const bool considerTrackingOnlyFaces = false;
  std::set< FaceID_t > faces = robot.GetFaceWorld().GetFaceIDsObservedSince(_lastImageTimestampWhileRunning,
                                                                                 considerTrackingOnlyFaces);

  const AIWhiteboard& whiteboard = robot.GetAIComponent().GetWhiteboard();
  const bool preferName = true;
  _targetFace = whiteboard.GetBestFaceToTrack(faces, preferName);
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorInteractWithFaces::AlwaysHandle(const EngineToGameEvent& event, const Robot& robot)
{
  if( event.GetData().GetTag() == EngineToGameTag::RobotChangedObservedFaceID )
  {
    auto const& msg = event.GetData().Get_RobotChangedObservedFaceID();
    if( msg.oldID == _targetFace ) {
      _targetFace = msg.newID;
    }
  }
}


} // namespace Cozmo
} // namespace Anki

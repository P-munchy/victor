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

#include "engine/aiComponent/behaviorComponent/behaviors/basicWorldInteractions/behaviorInteractWithFaces.h"

#include "engine/actions/animActions.h"
#include "engine/actions/basicActions.h"
#include "engine/actions/trackFaceAction.h"
#include "engine/aiComponent/aiComponent.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/behaviorExternalInterface.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/beiRobotInfo.h"
#include "engine/aiComponent/faceSelectionComponent.h"
#include "engine/cozmoContext.h"
#include "engine/events/ankiEvent.h"
#include "engine/externalInterface/externalInterface.h"
#include "engine/faceWorld.h"
#include "engine/moodSystem/moodManager.h"
#include "engine/navMap/mapComponent.h"
#include "engine/navMap/memoryMap/memoryMapTypes.h"
#include "engine/needsSystem/needsManager.h"
#include "engine/viz/vizManager.h"

#include "anki/common/basestation/jsonTools.h"
#include "anki/common/basestation/utils/timer.h"

#include "coretech/vision/engine/faceTracker.h"
#include "coretech/vision/engine/trackedFace.h"

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
constexpr MemoryMapTypes::FullContentArray typesToBlockDriving =
{
  {MemoryMapTypes::EContentType::Unknown               , false},
  {MemoryMapTypes::EContentType::ClearOfObstacle       , false},
  {MemoryMapTypes::EContentType::ClearOfCliff          , false},
  {MemoryMapTypes::EContentType::ObstacleCube          , true },
  {MemoryMapTypes::EContentType::ObstacleCubeRemoved   , false},
  {MemoryMapTypes::EContentType::ObstacleCharger       , true },
  {MemoryMapTypes::EContentType::ObstacleChargerRemoved, false},
  {MemoryMapTypes::EContentType::ObstacleProx          , true },
  {MemoryMapTypes::EContentType::ObstacleUnrecognized  , true },
  {MemoryMapTypes::EContentType::Cliff                 , true },
  {MemoryMapTypes::EContentType::InterestingEdge       , true },
  {MemoryMapTypes::EContentType::NotInterestingEdge    , true }
};
static_assert(MemoryMapTypes::IsSequentialArray(typesToBlockDriving),
  "This array does not define all types once and only once.");

}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorInteractWithFaces::BehaviorInteractWithFaces(const Json::Value& config)
: ICozmoBehavior(config)
{
  LoadConfig(config["params"]);

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
void BehaviorInteractWithFaces::OnBehaviorActivated(BehaviorExternalInterface& behaviorExternalInterface)
{
  // reset the time to stop tracking (in the tracking state)
  _trackFaceUntilTime_s = -1.0f;

  if( _targetFace.IsValid() ) {
    TransitionToInitialReaction(behaviorExternalInterface);
  }
  else {
    PRINT_NAMED_WARNING("BehaviorInteractWithFaces.Init.NoValidTarget",
                        "Decided to run, but don't have valid target when Init is called. This shouldn't happen");
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorInteractWithFaces::BehaviorUpdate(BehaviorExternalInterface& behaviorExternalInterface)
{
  if(!IsActivated()){
    return;
  }

  if( _trackFaceUntilTime_s >= 0.0f ) {
    const float currTime_s = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
    if( currTime_s >= _trackFaceUntilTime_s ) {
      BehaviorObjectiveAchieved(BehaviorObjective::InteractedWithFace);
      CancelDelegates();
      
      if(behaviorExternalInterface.HasNeedsManager()){
        auto& needsManager = behaviorExternalInterface.GetNeedsManager();
        needsManager.RegisterNeedsActionCompleted(NeedsActionId::SeeFace);
      }
    }
  }  
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorInteractWithFaces::WantsToBeActivatedBehavior(BehaviorExternalInterface& behaviorExternalInterface) const
{
  _targetFace.Reset();
  SelectFaceToTrack(behaviorExternalInterface);

  return _targetFace.IsValid();
}

  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorInteractWithFaces::OnBehaviorDeactivated(BehaviorExternalInterface& behaviorExternalInterface)
{
  _lastImageTimestampWhileRunning = behaviorExternalInterface.GetRobotInfo().GetLastImageTimeStamp();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorInteractWithFaces::CanDriveIdealDistanceForward(BehaviorExternalInterface& behaviorExternalInterface)
{
  if( kInteractWithFaces_DoMemoryMapCheckForDriveForward && 
      behaviorExternalInterface.HasMapComponent()) {
    const auto& robotInfo = behaviorExternalInterface.GetRobotInfo();

    const INavMap* memoryMap = behaviorExternalInterface.GetMapComponent().GetCurrentMemoryMap();
    
    DEV_ASSERT(nullptr != memoryMap, "BehaviorInteractWithFaces.CanDriveIdealDistanceForward.NeedMemoryMap");

    const Vec3f& fromRobot = robotInfo.GetPose().GetTranslation();

    const Vec3f ray{kInteractWithFaces_DriveForwardIdealDist_mm, 0.0f, 0.0f};
    const Vec3f toGoal = robotInfo.GetPose() * ray;
    
    const bool hasCollision = memoryMap->HasCollisionRayWithTypes(fromRobot, toGoal, typesToBlockDriving);

    if( kInteractWithFaces_VizMemoryMapCheck ) {
      const char* vizID = "BehaviorInteractWithFaces.MemMapCheck";
      const float zOffset_mm = 15.0f;
      robotInfo.GetContext()->GetVizManager()->EraseSegments(vizID);
      robotInfo.GetContext()->GetVizManager()->DrawSegment(vizID,
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
void BehaviorInteractWithFaces::TransitionToInitialReaction(BehaviorExternalInterface& behaviorExternalInterface)
{
  DEBUG_SET_STATE(VerifyFace);

  CompoundActionSequential* action = new CompoundActionSequential();

  {
    TurnTowardsFaceAction* turnAndAnimateAction = new TurnTowardsFaceAction(_targetFace, M_PI_F, true);
    turnAndAnimateAction->SetSayNameAnimationTrigger(AnimationTrigger::InteractWithFacesInitialNamed);
    turnAndAnimateAction->SetNoNameAnimationTrigger(AnimationTrigger::InteractWithFacesInitialUnnamed);
    turnAndAnimateAction->SetRequireFaceConfirmation(true);
    action->AddAction(turnAndAnimateAction);
  }
  
  DelegateIfInControl(action, [this, &behaviorExternalInterface](ActionResult ret ) {
      if( ret == ActionResult::SUCCESS ) {
        TransitionToGlancingDown(behaviorExternalInterface);
      }
      else {
        // one possible cause of failure is that the face id we tried to track wasn't there (but another face
        // was). So, see if there is a new "best face", and if so, track that one. This will only run if a new
        // face is observed.

        if(behaviorExternalInterface.HasMoodManager()){
          // increase frustration to avoid loops
          auto& moodManager = behaviorExternalInterface.GetMoodManager();
          moodManager.TriggerEmotionEvent("InteractWithFaceRetry",
                                          MoodManager::GetCurrentTimeInSeconds());
        }
        
        _lastImageTimestampWhileRunning =  behaviorExternalInterface.GetRobotInfo().GetLastImageTimeStamp();
        
        SmartFaceID oldTargetFace = _targetFace;
        SelectFaceToTrack(behaviorExternalInterface);
        if(_targetFace != oldTargetFace) {
          // only retry a max of one time to avoid loops
          PRINT_CH_INFO("Behaviors","BehaviorInteractWithFaces.InitialReactionFailed.TryAgain",
                        "tracking face %s failed, but will try again with face %s",
                        oldTargetFace.GetDebugStr().c_str(),
                        _targetFace.GetDebugStr().c_str());

          TransitionToInitialReaction(behaviorExternalInterface);
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
void BehaviorInteractWithFaces::TransitionToGlancingDown(BehaviorExternalInterface& behaviorExternalInterface)
{
  DEBUG_SET_STATE(GlancingDown);

  if( kInteractWithFaces_DoGlanceDown && kInteractWithFaces_DoMemoryMapCheckForDriveForward ) {
    // TODO:(bn) get a better measurement for this and put it in cozmo config
    const float kLowHeadAngle_rads = DEG_TO_RAD(-10.0f);
    DelegateIfInControl(new MoveHeadToAngleAction(kLowHeadAngle_rads),
                &BehaviorInteractWithFaces::TransitionToDrivingForward);
  }
  else {
    TransitionToDrivingForward(behaviorExternalInterface);
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorInteractWithFaces::TransitionToDrivingForward(BehaviorExternalInterface& behaviorExternalInterface)
{
  DEBUG_SET_STATE(DrivingForward);
  
  // check if we should do the long or short distance
  const bool doLongDrive = CanDriveIdealDistanceForward(behaviorExternalInterface);
  const float distToDrive_mm = doLongDrive ?
    kInteractWithFaces_DriveForwardIdealDist_mm :
    kInteractWithFaces_DriveForwardMinDist_mm;

  // drive straight while keeping the head tracking the (players) face
  CompoundActionParallel* action = new CompoundActionParallel();

  // the head tracking action normally loops forever, so set up the drive action first, tell it to emit
  // completion signals, then pass it's tag in to the tracking action so the tracking action can stop itself
  // when the driving action finishes

  u32 driveActionTag = 0;
  {
    // don't play driving animations (to avoid sounds which don't make sense here)
    // TODO:(bn) custom driving animations for this action?
    DriveStraightAction* driveAction = new DriveStraightAction(distToDrive_mm,
                                                               kInteractWithFaces_DriveForwardSpeed_mmps,
                                                               false);
    const bool ignoreFailure = false;
    const bool emitCompletionSignal = true;
    action->AddAction( driveAction, ignoreFailure, emitCompletionSignal );
    driveActionTag = driveAction->GetTag();
  }

  {
    TrackFaceAction* trackWithHeadAction = new TrackFaceAction(_targetFace);
    trackWithHeadAction->SetMode(ITrackAction::Mode::HeadOnly);
    trackWithHeadAction->StopTrackingWhenOtherActionCompleted( driveActionTag );
    trackWithHeadAction->SetTiltTolerance(DEG_TO_RAD(kInteractWithFaces_MinTrackingPanAngle_deg));
    trackWithHeadAction->SetPanTolerance(DEG_TO_RAD(kInteractWithFaces_MinTrackingTiltAngle_deg));
    trackWithHeadAction->SetClampSmallAnglesToTolerances(_configParams.clampSmallAngles);
    trackWithHeadAction->SetClampSmallAnglesPeriod(_configParams.minClampPeriod_s, _configParams.maxClampPeriod_s);

    action->AddAction( trackWithHeadAction );
  }
  
  // TODO:(bn) alternate driving animations?
  DelegateIfInControl(action, &BehaviorInteractWithFaces::TransitionToTrackingFace);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorInteractWithFaces::TransitionToTrackingFace(BehaviorExternalInterface& behaviorExternalInterface)
{
  DEBUG_SET_STATE(TrackingFace);

  const float randomTimeToTrack_s = Util::numeric_cast<float>(
    GetRNG().RandDblInRange(_configParams.minTimeToTrackFace_s, _configParams.maxTimeToTrackFace_s));
  PRINT_CH_INFO("Behaviors", "BehaviorInteractWithFaces.TrackTime", "will track for %f seconds", randomTimeToTrack_s);
  _trackFaceUntilTime_s = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + randomTimeToTrack_s;


  CompoundActionParallel* action = new CompoundActionParallel();

  {
    TrackFaceAction* trackAction = new TrackFaceAction(_targetFace);
    trackAction->SetTiltTolerance(DEG_TO_RAD(kInteractWithFaces_MinTrackingPanAngle_deg));
    trackAction->SetPanTolerance(DEG_TO_RAD(kInteractWithFaces_MinTrackingTiltAngle_deg));
    trackAction->SetClampSmallAnglesToTolerances(_configParams.clampSmallAngles);
    trackAction->SetClampSmallAnglesPeriod(_configParams.minClampPeriod_s, _configParams.maxClampPeriod_s);
    action->AddAction(trackAction);
  }
  
  // loop animation forever to keep the eyes moving
  action->AddAction(new TriggerAnimationAction(AnimationTrigger::InteractWithFaceTrackingIdle, 0));
  
  DelegateIfInControl(action, &BehaviorInteractWithFaces::TransitionToTriggerEmotionEvent);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorInteractWithFaces::TransitionToTriggerEmotionEvent(BehaviorExternalInterface& behaviorExternalInterface)
{
  DEBUG_SET_STATE(TriggerEmotionEvent);

  if(behaviorExternalInterface.HasMoodManager()){
    auto& moodManager = behaviorExternalInterface.GetMoodManager();
    const Vision::TrackedFace* face = behaviorExternalInterface.GetFaceWorld().GetFace( _targetFace );
    
    if( nullptr != face && face->HasName() ) {
      moodManager.TriggerEmotionEvent("InteractWithNamedFace", MoodManager::GetCurrentTimeInSeconds());
    }
    else {
      moodManager.TriggerEmotionEvent("InteractWithUnnamedFace", MoodManager::GetCurrentTimeInSeconds());
    }
  }
}
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorInteractWithFaces::SelectFaceToTrack(BehaviorExternalInterface& behaviorExternalInterface) const
{  
  const bool considerTrackingOnlyFaces = false;
  std::set< Vision::FaceID_t > faces = behaviorExternalInterface.GetFaceWorld().GetFaceIDsObservedSince(_lastImageTimestampWhileRunning,
                                                                                 considerTrackingOnlyFaces);
  
  std::set<SmartFaceID> smartFaces;
  for(auto& entry : faces){
    smartFaces.insert(behaviorExternalInterface.GetFaceWorld().GetSmartFaceID(entry));
  }
  const auto& faceSelection = behaviorExternalInterface.GetAIComponent().GetFaceSelectionComponent();
  FaceSelectionComponent::FaceSelectionFactorMap criteriaMap;
  criteriaMap.insert(std::make_pair(FaceSelectionComponent::FaceSelectionPenaltyMultiplier::UnnamedFace, 1000));
  criteriaMap.insert(std::make_pair(FaceSelectionComponent::FaceSelectionPenaltyMultiplier::RelativeHeadAngleRadians, 1));
  criteriaMap.insert(std::make_pair(FaceSelectionComponent::FaceSelectionPenaltyMultiplier::RelativeBodyAngleRadians, 3));
  _targetFace = faceSelection.GetBestFaceToUse(criteriaMap, smartFaces);
}


} // namespace Cozmo
} // namespace Anki

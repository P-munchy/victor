/**
 * File: behaviorReactToCliff.cpp
 *
 * Author: Kevin
 * Created: 10/16/15
 *
 * Description: Behavior for immediately responding to a detected cliff. This behavior actually handles both
 *              the stop and cliff events
 *
 * Copyright: Anki, Inc. 2015
 *
 **/

#include "engine/actions/animActions.h"
#include "engine/actions/basicActions.h"
#include "engine/aiComponent/behaviorComponent/behaviorContainer.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/beiRobotInfo.h"
#include "engine/aiComponent/behaviorComponent/behaviors/reactions/behaviorReactToCliff.h"
#include "engine/components/movementComponent.h"
#include "engine/components/robotStatsTracker.h"
#include "engine/components/sensors/cliffSensorComponent.h"
#include "engine/navMap/mapComponent.h"
#include "engine/events/ankiEvent.h"
#include "engine/externalInterface/externalInterface.h"
#include "engine/moodSystem/moodManager.h"
#include "engine/cozmoContext.h"
#include "engine/robot.h"

#include "clad/externalInterface/messageEngineToGame.h"
#include "clad/types/animationTrigger.h"
#include "clad/types/behaviorComponent/behaviorIDs.h"
#include "clad/types/behaviorComponent/behaviorStats.h"
#include "clad/types/proxMessages.h"

#include "coretech/common/engine/utils/timer.h"

#include "util/console/consoleInterface.h"
#include "util/logging/DAS.h"

#include <limits>

#define CONSOLE_GROUP "Behavior.ReactToCliff"

namespace Anki {
namespace Vector {

using namespace ExternalInterface;

namespace {
  const char* kCliffBackupDistKey = "cliffBackupDistance_mm";
  const char* kCliffBackupSpeedKey = "cliffBackupSpeed_mmps";
  const char* kEventFlagTimeoutKey = "eventFlagTimeout_sec";
  
  // If the robot is at a steep pitch, it's possible it's been put down
  // purposefully on a slope, so this behavior won't activate until enough time
  // has passed with the robot on its treads in order to give `ReactToPlacedOnSlope`
  // time to activate and run.
  const f32 kMinPitchToCheckForIncline_rad = DEG_TO_RAD(10.f);
  
  // When the robot is at a steep pitch, there is an additional requirement that the
  // robot should consider itself "OnTreads" for at least this number of ms to
  // prevent interrupting the `ReactToPlacedOnSlope` behavior.
  const u16 kMinTimeSinceOffTreads_ms = 1000;

  // If the value of the cliff when it started stopping is
  // this much less than the value when it stopped, then
  // the cliff is considered suspicious (i.e. something like
  // carpet) and the reaction is aborted.
  // In general you'd expect the start value to be _higher_
  // that the stop value if it's true cliff, but we
  // use some margin here to account for sensor noise.
  static const u16   kSuspiciousCliffValDiff = 20;

  // minimum number of images with edge detection activated
  const int kNumImagesToWaitForEdges = 1;

  // rate of turning the robot while searching for cliffs with vision
  const f32 kBodyTurnSpeedForCliffSearch_degPerSec = 120.0f;

  // If this many RobotStopped messages are received
  // while activated, then just give up and go to StuckOnEdge.
  // It's probably too dangerous to keep trying anything
  CONSOLE_VAR(uint32_t, kMaxNumRobotStopsBeforeGivingUp, CONSOLE_GROUP, 5);

  // whether this experimental feature whereby the robot uses vision
  // to extend known cliffs via edge detection is active
  CONSOLE_VAR(bool, kEnableVisualCliffExtension, CONSOLE_GROUP, true);

  CONSOLE_VAR(float, kMinViewingDistanceToCliff_mm, CONSOLE_GROUP, 80.0f);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToCliff::GetBehaviorJsonKeys(std::set<const char*>& expectedKeys) const
{
  const char* list[] = {
    kCliffBackupDistKey,
    kCliffBackupSpeedKey,
    kEventFlagTimeoutKey
  };
  expectedKeys.insert( std::begin(list), std::end(list) );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorReactToCliff::InstanceConfig::InstanceConfig()
{
  stuckOnEdgeBehavior = nullptr;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorReactToCliff::InstanceConfig::InstanceConfig(const Json::Value& config, const std::string& debugName)
{
  cliffBackupDist_mm = JsonTools::ParseFloat(config, kCliffBackupDistKey, debugName);
  // Verify that backup distance is valid.
  ANKI_VERIFY(Util::IsFltGTZero(cliffBackupDist_mm), (debugName + ".InvalidCliffBackupDistance").c_str(),
              "Value should be greater than 0.0 (not %.2f).", cliffBackupDist_mm);
  
  cliffBackupSpeed_mmps = JsonTools::ParseFloat(config, kCliffBackupSpeedKey, debugName);
  // Verify that backup speed is valid.
  ANKI_VERIFY(Util::IsFltGTZero(cliffBackupSpeed_mmps), (debugName + ".InvalidCliffBackupSpeed").c_str(),
              "Value should be greater than 0.0 (not %.2f).", cliffBackupSpeed_mmps);
  
  eventFlagTimeout_sec = JsonTools::ParseFloat(config, kEventFlagTimeoutKey, debugName);
  // Verify that the stop event timeout limit is valid.
  if (!ANKI_VERIFY(Util::IsFltGEZero(eventFlagTimeout_sec),
                   (debugName + ".InvalidEventFlagTimeout").c_str(),
                   "Value should always be greater than or equal to 0.0 (not %.2f). Making positive.",
                   eventFlagTimeout_sec))
  {
    eventFlagTimeout_sec *= -1.0;
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorReactToCliff::DynamicVariables::DynamicVariables()
{
  quitReaction = false;
  gotStop = false;
  wantsToBeActivated = false;
  hasTargetCliff = false;

  cliffPose.ClearParent();
  cliffPose = Pose3d(); // identity

  const auto kInitVal = std::numeric_limits<u16>::max();
  std::fill(persistent.cliffValsAtStart.begin(), persistent.cliffValsAtStart.end(), kInitVal);
  persistent.numStops = 0;
  persistent.lastStopTime_sec = 0.f;
  persistent.putDownOnCliff = false;
  persistent.lastPutDownOnCliffTime_sec = 0.f;
}
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorReactToCliff::BehaviorReactToCliff(const Json::Value& config)
: ICozmoBehavior(config)
{
  const std::string& debugName = "Behavior" + GetDebugLabel() + ".LoadConfig";
  _iConfig = InstanceConfig(config, debugName);
  
  SubscribeToTags({{
    EngineToGameTag::RobotStopped,
    EngineToGameTag::RobotOffTreadsStateChanged,
  }});
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToCliff::InitBehavior()
{
  const auto& BC = GetBEI().GetBehaviorContainer();
  _iConfig.stuckOnEdgeBehavior = BC.FindBehaviorByID(BEHAVIOR_ID(StuckOnEdge));
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorReactToCliff::WantsToBeActivatedBehavior() const
{
  if (_dVars.gotStop || _dVars.wantsToBeActivated || _dVars.persistent.putDownOnCliff)
  {
    if( GetBEI().GetOffTreadsState() == OffTreadsState::OnTreads ) {
      const Radians& pitch =  GetBEI().GetRobotInfo().GetPitchAngle();
      if( pitch >= DEG_TO_RAD(kMinPitchToCheckForIncline_rad) ) {
        const EngineTimeStamp_t currTime_ms = BaseStationTimer::getInstance()->GetCurrentTimeStamp();
        const EngineTimeStamp_t lastChangedTime_ms = GetBEI().GetRobotInfo().GetOffTreadsStateLastChangedTime_ms();
        return (currTime_ms - lastChangedTime_ms >= kMinTimeSinceOffTreads_ms);
      }
      return true;
    }
  }
  return false;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToCliff::OnBehaviorActivated()
{
  // reset dvars
  const auto persistent = _dVars.persistent;
  _dVars = DynamicVariables();
  _dVars.persistent = persistent;
  
  if(GetBEI().HasMoodManager()){
    auto& moodManager = GetBEI().GetMoodManager();
    moodManager.TriggerEmotionEvent("CliffReact", MoodManager::GetCurrentTimeInSeconds());
  } 
    
  auto& robotInfo = GetBEI().GetRobotInfo();

  // Wait function for determining if the cliff is suspicious
  auto waitForStopLambda = [this, &robotInfo](Robot& robot) {
    if ( robotInfo.GetMoveComponent().AreWheelsMoving() ) {
      return false;
    }

    const auto& cliffComp = robotInfo.GetCliffSensorComponent();
    const auto& cliffData = cliffComp.GetCliffDataRaw();

    PRINT_CH_INFO("Behaviors", "BehaviorReactToCliff.CliffValsAtEnd", 
                  "%u %u %u %u (%x)", 
                  cliffData[0], cliffData[1], cliffData[2], cliffData[3], cliffComp.GetCliffDetectedFlags());

    for (int i=0; i<CliffSensorComponent::kNumCliffSensors; ++i) {  
      if (cliffComp.IsCliffDetected((CliffSensor)(i))) {
        if (_dVars.persistent.cliffValsAtStart[i] + kSuspiciousCliffValDiff < cliffData[i]) {
          // There was a sufficiently large increase in cliff value since cliff was 
          /// first detected so assuming it was a false cliff and aborting reaction
          PRINT_CH_INFO("Behaviors", "BehaviorReactToCliff.QuittingDueToSuspiciousCliff", 
                        "index: %d, startVal: %u, currVal: %u",
                        i, _dVars.persistent.cliffValsAtStart[i], cliffData[i]);
          _dVars.quitReaction = true;    
        }
      }
    }

    // compute the pose of the detected cliff, 
    // and cache it for determining lookat positions
    if(cliffComp.GetCliffPoseRelativeToRobot(cliffComp.GetCliffDetectedFlags(), _dVars.cliffPose)) {
      _dVars.cliffPose.SetParent(GetBEI().GetRobotInfo().GetPose());
      if (_dVars.cliffPose.GetWithRespectTo(GetBEI().GetRobotInfo().GetWorldOrigin(), _dVars.cliffPose)) {
        _dVars.hasTargetCliff = true;
      } else {
        PRINT_NAMED_WARNING("BehaviorReactToCliff.OnBehaviorActivated.OriginMismatch",
                            "cliffPose and WorldOrigin do not share the same origin!");
      }
    } else {
      PRINT_NAMED_WARNING("BehaviorReactToCliff.OnBehaviorActivated.NoPoseForCliffFlags",
                          "flags=%hhu", cliffComp.GetCliffDetectedFlags());
    }

    return true;
  };

  // skip the "huh" animation if in severe energy or repair
  WaitForLambdaAction* waitForStopAction = new WaitForLambdaAction(waitForStopLambda);
  DelegateIfInControl(waitForStopAction, &BehaviorReactToCliff::TransitionToPlayingCliffReaction);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToCliff::TransitionToStuckOnEdge()
{
  DEBUG_SET_STATE(StuckOnEdge);

  const auto& cliffComp = GetBEI().GetRobotInfo().GetCliffSensorComponent();
  DASMSG(behavior_cliff_stuck_on_edge, "behavior.cliff_stuck_on_edge", "The robot appears to be stuck on the edge of a surface");
  DASMSG_SET(i1, cliffComp.GetCliffDetectedFlags(), "Cliff detected flags");
  DASMSG_SEND();
  
  ANKI_VERIFY(_iConfig.stuckOnEdgeBehavior->WantsToBeActivated(),
              "BehaviorReactToCliff.TransitionToStuckOnEdge.DoesNotWantToBeActivated", 
              "");
  DelegateIfInControl(_iConfig.stuckOnEdgeBehavior.get());
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToCliff::TransitionToPlayingCliffReaction()
{
  DEBUG_SET_STATE(PlayingCliffReaction);

  if(_dVars.quitReaction) {
    return;
  }
  
  // send DAS event for activation. don't send it if quitReaction is false, because the user likely didn't notice
  {
    DASMSG(behavior_cliffreaction, "behavior.cliffreaction", "The robot reacted to a cliff");
    DASMSG_SET(i1, _dVars.persistent.cliffValsAtStart[0], "Sensor value 1 (front left)");
    DASMSG_SET(i2, _dVars.persistent.cliffValsAtStart[1], "Sensor value 2 (front right)");
    DASMSG_SET(i3, _dVars.persistent.cliffValsAtStart[2], "Sensor value 3 (back left)");
    DASMSG_SET(i4, _dVars.persistent.cliffValsAtStart[3], "Sensor value 4 (back right)");
    DASMSG_SEND();
  }

  GetBehaviorComp<RobotStatsTracker>().IncrementBehaviorStat(BehaviorStat::ReactedToCliff);
  
  if(ShouldStreamline()){
    TransitionToRecoveryBackup();
  } else {
    Anki::Util::sInfo("robot.cliff_detected", {}, "");

    auto cliffDetectedFlags = GetBEI().GetRobotInfo().GetCliffSensorComponent().GetCliffDetectedFlags();
    if (cliffDetectedFlags == 0) {
      // For some reason no cliffs are detected. Just leave the reaction.
      PRINT_CH_INFO("Behaviors", "BehaviorReactToCliff.TransitionToPlayingCliffReaction.NoCliffs", "");
      CancelSelf();
      return;
    }

    // Did we get too many RobotStopped messages for this
    // activation of the behavior? Must be in a very "cliffy" area.
    // Just go to StuckOnEdge to be safe.
    if (_dVars.persistent.numStops > kMaxNumRobotStopsBeforeGivingUp) {
      PRINT_CH_INFO("Behaviors", "BehaviorReactToCliff.TransitionToPlayingCliffReaction.TooManyRobotStops", "");
      TransitionToStuckOnEdge();
      return;
    }

    // Get the pre-react action/animation to play
    auto action = GetCliffReactAction(cliffDetectedFlags);
    if (action == nullptr) {
      // No action was returned because the detected cliffs represent 
      // a precarious situation so just delegate to StuckOnEdge.
      PRINT_CH_INFO("Behaviors", "BehaviorReactToCliff.TransitionToPlayingCliffReaction.StuckOnEdge", 
                       "%x", 
                       cliffDetectedFlags);
      TransitionToStuckOnEdge();
    } else {
      DelegateIfInControl(action, &BehaviorReactToCliff::TransitionToFaceAndBackAwayCliff);
    }
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToCliff::TransitionToRecoveryBackup()
{
  auto& robotInfo = GetBEI().GetRobotInfo();

  // if the animation doesn't drive us backwards enough, do it manually
  if( robotInfo.GetCliffSensorComponent().IsCliffDetected() ) {
    
    // Determine whether to backup or move forward based on triggered sensor
    f32 direction = 1.f;
    if (robotInfo.GetCliffSensorComponent().IsCliffDetected(CliffSensor::CLIFF_FL) ||
        robotInfo.GetCliffSensorComponent().IsCliffDetected(CliffSensor::CLIFF_FR) ) {
      direction = -1.f;
    }

    PRINT_CH_INFO("Behaviors", "BehaviorReactToCliff.TransitionToRecoveryBackup.DoingExtraRecoveryMotion", "");
    IActionRunner* backupAction = new DriveStraightAction(direction * _iConfig.cliffBackupDist_mm, _iConfig.cliffBackupSpeed_mmps);
    BehaviorSimpleCallback callback = [this](void)->void{
      PRINT_CH_INFO("Behaviors", "BehaviorReactToCliff.TransitionToRecoveryBackup.ExtraRecoveryMotionComplete", "");
      auto& cliffComponent = GetBEI().GetRobotInfo().GetCliffSensorComponent();
      if ( cliffComponent.IsCliffDetected() ) {
        PRINT_CH_INFO("Behaviors", "BehaviorReactToCliff.TransitionToRecoveryBackup.StillStuckOnEdge", 
                          "%x", 
                          cliffComponent.GetCliffDetectedFlags());
        TransitionToStuckOnEdge();
      } else if (_dVars.persistent.putDownOnCliff) {
          TransitionToHeadCalibration();
      } else {
        TransitionToVisualExtendCliffs();
      }
    };
    DelegateIfInControl(backupAction, callback);
  } else if (_dVars.persistent.putDownOnCliff) {
    TransitionToHeadCalibration();
  } else {
    TransitionToVisualExtendCliffs();
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToCliff::TransitionToHeadCalibration()
{
  DEBUG_SET_STATE(CalibratingHead);
  // The `putDownOnCliff` flag is what triggers the calling of this method.
  // To avoid causing a loop, reset it here, since we're about to calibrate the head motor.
  _dVars.persistent.putDownOnCliff = false;
  // Force all motors to recalibrate since it's possible that Vector may have been put down too aggressively,
  // resulting in gear slippage for a motor, or the user might have forced one of the motors into a different
  // position while in the air or while sensors were disabled.
  DelegateIfInControl(new CalibrateMotorAction(true, true,
                                               EnumToString(MotorCalibrationReason::BehaviorReactToCliff)),
                      &BehaviorReactToCliff::TransitionToVisualExtendCliffs);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Pose3d BehaviorReactToCliff::GetCliffPoseToLookAt() const
{
  Pose3d cliffLookAtPose;
  if(_dVars.hasTargetCliff) {
    bool result = _dVars.cliffPose.GetWithRespectTo(GetBEI().GetRobotInfo().GetWorldOrigin(), cliffLookAtPose);
    if(result) {
      PRINT_CH_INFO("Behaviors", "BehaviorReactToCliff.GetCliffLookAtPose.CliffAt", 
                      "x=%4.2f y=%4.2f",
                      _dVars.cliffPose.GetTranslation().x(),
                      _dVars.cliffPose.GetTranslation().y());
    } else {
      PRINT_NAMED_WARNING("BehaviorReactToCliff.GetCliffLookAtPose.CliffPoseNotInSameTreeAsCurrentWorldOrigin","");
    }
  } else {
    // no previously set target cliff pose to look at
    // instead look at arbitrary position in front
    PRINT_NAMED_WARNING("BehaviorReactToCliff.GetCliffLookAtPose.CliffDefaultPoseAssumed","");
    cliffLookAtPose = Pose3d(0.f, Z_AXIS_3D(), {60.f, 0.f, 0.f});
    cliffLookAtPose.SetParent(GetBEI().GetRobotInfo().GetPose());
    cliffLookAtPose.GetWithRespectTo(GetBEI().GetRobotInfo().GetWorldOrigin(), cliffLookAtPose);
  }

  return cliffLookAtPose;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToCliff::TransitionToVisualExtendCliffs()
{
  if(!kEnableVisualCliffExtension) {
    BehaviorObjectiveAchieved(BehaviorObjective::ReactedToCliff);
    return;
  }
  
  DEBUG_SET_STATE(VisuallyExtendingCliffs);
  Pose3d cliffLookAtPose = GetCliffPoseToLookAt();

  
  CompoundActionSequential* compoundAction = new CompoundActionSequential();

  compoundAction->AddAction(new MoveLiftToHeightAction(MoveLiftToHeightAction::Preset::LOW_DOCK)); // move lift to be out of the FOV

  // sometimes the animation will have us slightly off from the pose
  auto turnAction = new TurnTowardsPoseAction(cliffLookAtPose);
  turnAction->SetMaxPanSpeed(DEG_TO_RAD(kBodyTurnSpeedForCliffSearch_degPerSec));
  compoundAction->AddAction(turnAction);

  // if we're too close to the cliff, we need to backup to view it better
  float dist = ComputeDistanceBetween(cliffLookAtPose.GetTranslation(), GetBEI().GetRobotInfo().GetPose().GetTranslation());
  if(dist < kMinViewingDistanceToCliff_mm) {
    PRINT_CH_INFO("Behaviors", "BehaviorReactToCliff.TransitionToVisualCliffExtend.FurtherBackupNeeded", "%6.6fmm", dist);
    // note: we will always be facing the cliff, so the backup direction is set at this point
    compoundAction->AddAction(new DriveStraightAction(-(kMinViewingDistanceToCliff_mm-dist), _iConfig.cliffBackupSpeed_mmps));
    // secondary look-at action to ensure we're facing the cliff point
    // note: this will correct any offset introduced by the backup action
    auto turnAction2 = new TurnTowardsPoseAction(cliffLookAtPose);
    turnAction2->SetMaxPanSpeed(DEG_TO_RAD(kBodyTurnSpeedForCliffSearch_degPerSec));
    compoundAction->AddAction(turnAction2);
  }
  compoundAction->AddAction(new WaitForImagesAction(kNumImagesToWaitForEdges, VisionMode::DetectingOverheadEdges));
  
  BehaviorSimpleCallback callback = [this] (void) -> void {
    PRINT_CH_INFO("Behaviors", "BehaviorReactToCliff.TransitionToVisualCliffExtend.ObservationFinished", "");
    BehaviorObjectiveAchieved(BehaviorObjective::ReactedToCliff);
  };

  DelegateIfInControl(compoundAction, callback);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToCliff::OnBehaviorDeactivated()
{
  // reset dvars
  _dVars = DynamicVariables();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToCliff::GetAllDelegates(std::set<IBehavior*>& delegates) const
{
  delegates.insert(_iConfig.stuckOnEdgeBehavior.get());
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToCliff::BehaviorUpdate()
{
  if(!IsActivated()){
    const auto& currentTime_sec = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
    if (_dVars.gotStop) {
      const auto& timeSinceLastStop_sec = currentTime_sec - _dVars.persistent.lastStopTime_sec;
      if (timeSinceLastStop_sec > _iConfig.eventFlagTimeout_sec) {
        _dVars.gotStop = false;
        PRINT_CH_INFO("Behaviors", "BehaviorReactToCliff.Update.IgnoreLastStopEvent", "");
      }
    }
    if (_dVars.persistent.putDownOnCliff) {
      const auto& timeSinceLastPutDownOnCliff_sec = currentTime_sec - _dVars.persistent.lastPutDownOnCliffTime_sec;
      if (timeSinceLastPutDownOnCliff_sec > _iConfig.eventFlagTimeout_sec) {
        _dVars.persistent.putDownOnCliff = false;
        PRINT_CH_INFO("Behaviors", "BehaviorReactToCliff.Update.IgnoreLastPossiblePutDownOnCliffEvent", "");
      }
    }
    // Set wantsToBeActivated to effectively give the activation conditions
    // an extra tick to be evaluated.
    _dVars.wantsToBeActivated = _dVars.gotStop || _dVars.persistent.putDownOnCliff;
    _dVars.gotStop = false;
    return;
  }

  // TODO: This exit on unexpected movement is probably good to have, 
  // but it appears the cliff reactions cause unexpected movement to 
  // trigger falsely, so commenting out until unexpected movement
  // is modified to have fewer false positives.
  //  
  // // Delegate to StuckOnEdge if unexpected motion detected while
  // // cliff is still detected since it means treads are spinning
  // const bool unexpectedMovement = GetBEI().GetMovementComponent().IsUnexpectedMovementDetected();
  // const bool cliffDetected = GetBEI().GetRobotInfo().GetCliffSensorComponent().IsCliffDetected();
  // if (unexpectedMovement && cliffDetected) {
  //   PRINT_CH_INFO("Behaviors", "BehaviorReactToCliff.Update.StuckOnEdge", "");
  //   _iConfig.stuckOnEdgeBehavior->WantsToBeActivated();
  //   DelegateNow(_iConfig.stuckOnEdgeBehavior.get());
  // }

  // Cancel if picked up
  const bool isPickedUp = GetBEI().GetRobotInfo().IsPickedUp();
  // Often, when the robot gets too close to a curved edge, the robot can teeter and trigger a false
  // positive for pick-up detection. To counter this we wait for more than half of the cliff sensors
  // to also report that they are detecting "cliffs", due to the robot getting picked up. Otherwise,
  // we wait until the next engine tick to check all conditions

  u8 cliffsDetected = CliffSensorComponent::kNumCliffSensors;
  const auto& cliffComp = GetBEI().GetRobotInfo().GetCliffSensorComponent();
  for (int i=0; i<CliffSensorComponent::kNumCliffSensors; ++i) {
    if (!cliffComp.IsCliffDetected((CliffSensor)(i))) {
      // Robot is reporting that it has been picked up, but not all cliff sensors have reported
      // seing cliffs yet, which would be expected if the robot is teetering on an edge for
      // some reason, so it might lead to a false-positive detection of a pick-up event.
      --cliffsDetected;
    }
  }
  
  if (isPickedUp) {
    if (cliffsDetected > 2) {
      PRINT_CH_INFO("Behaviors", "BehaviorReactToCliff.Update.CancelDueToPickup", "");
      CancelSelf();
    } else {
      PRINT_PERIODIC_CH_INFO(5, "Behaviors",
                             "BehaviorReactToCliff.Update.PossibleFalsePickUpDetected",
                             "Only %u cliff sensors are detecting cliffs, but pick-up detected.",
                             cliffsDetected);
    }
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToCliff::AlwaysHandleInScope(const EngineToGameEvent& event)
{
  auto& robotInfo = GetBEI().GetRobotInfo();

  const bool alreadyActivated = IsActivated();
  switch( event.GetData().GetTag() ) {
    case EngineToGameTag::RobotStopped: {
      if (event.GetData().Get_RobotStopped().reason != StopReason::CLIFF) {
        break;
      }
      
      _dVars.quitReaction = false;
      _dVars.gotStop = true;
      _dVars.persistent.lastStopTime_sec = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
      ++_dVars.persistent.numStops;

      // Record triggered cliff sensor value(s) and compare to what they are when wheels
      // stop moving. If the values are higher, the cliff is suspicious and we should quit.
      const auto& cliffComp = robotInfo.GetCliffSensorComponent();
      const auto& cliffData = cliffComp.GetCliffDataRaw();
      std::copy(cliffData.begin(), cliffData.end(), _dVars.persistent.cliffValsAtStart.begin());
      PRINT_CH_INFO("Behaviors", "BehaviorReactToCliff.CliffValsAtStart", 
                    "%u %u %u %u (alreadyActivated: %d)", 
                    _dVars.persistent.cliffValsAtStart[0], 
                    _dVars.persistent.cliffValsAtStart[1],
                    _dVars.persistent.cliffValsAtStart[2],
                    _dVars.persistent.cliffValsAtStart[3],
                    alreadyActivated);

      if (alreadyActivated) {
        CancelDelegates(false);
        OnBehaviorActivated();
      }
      break;
    }
    case EngineToGameTag::RobotOffTreadsStateChanged:
    {
      const auto treadsState = event.GetData().Get_RobotOffTreadsStateChanged().treadsState;
      const bool cliffsDetected = GetBEI().GetRobotInfo().GetCliffSensorComponent().IsCliffDetected();
      
      if (treadsState == OffTreadsState::OnTreads && cliffsDetected) {
        PRINT_CH_INFO("Behaviors", "BehaviorReactToCliff.AlwaysHandleInScope", "Possibly put down on cliff");
        _dVars.persistent.putDownOnCliff = true;
        _dVars.persistent.lastPutDownOnCliffTime_sec = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
      } else {
        _dVars.persistent.putDownOnCliff = false;
      }
      break;
    }
    default:
      PRINT_NAMED_ERROR("BehaviorReactToCliff.ShouldRunForEvent.BadEventType",
                        "Calling ShouldRunForEvent with an event we don't care about, this is a bug");
      break;

  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
IActionRunner* BehaviorReactToCliff::GetCliffReactAction(uint8_t cliffDetectedFlags)
{
  // Bit flags for each of the cliff sensors:
  const uint8_t FL = (1<<Util::EnumToUnderlying(CliffSensor::CLIFF_FL));
  const uint8_t FR = (1<<Util::EnumToUnderlying(CliffSensor::CLIFF_FR));
  const uint8_t BL = (1<<Util::EnumToUnderlying(CliffSensor::CLIFF_BL));
  const uint8_t BR = (1<<Util::EnumToUnderlying(CliffSensor::CLIFF_BR));

  IActionRunner* action = nullptr;

  PRINT_CH_INFO("Behaviors", "ReactToCliff.GetCliffReactAction.CliffsDetected", "0x%x", cliffDetectedFlags);

  // Play reaction animation based on triggered sensor(s)
  // Possibly supplement with "dramatic" reaction which involves
  // turning towards the cliff and backing up in a scared/shocked fashion.
  switch (cliffDetectedFlags) {
    case (FL | FR):
      // Hit cliff straight-on. Play stop reaction and move on
      action = new TriggerLiftSafeAnimationAction(AnimationTrigger::ReactToCliffFront);
      break;
    case FL:
      // Play stop reaction animation and turn CCW a bit
      action = new TriggerLiftSafeAnimationAction(AnimationTrigger::ReactToCliffFrontLeft);
      break;
    case FR:
      // Play stop reaction animation and turn CW a bit
      action = new TriggerLiftSafeAnimationAction(AnimationTrigger::ReactToCliffFrontRight);
      break;
    case BL:
      // Drive forward and turn CCW to face the cliff
      action = new TriggerLiftSafeAnimationAction(AnimationTrigger::ReactToCliffBackLeft);
      break;
    case BR:
      // Drive forward and turn CW to face the cliff
      action = new TriggerLiftSafeAnimationAction(AnimationTrigger::ReactToCliffBackRight);
      break;  
    case (BL | BR):
      // Hit cliff straight-on driving backwards. Flip around to face the cliff.
      action = new TriggerLiftSafeAnimationAction(AnimationTrigger::ReactToCliffBack);
      break;
    default:
      // This is some scary configuration that we probably shouldn't move from.
      delete(action);
      return nullptr;
  }

  return action;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToCliff::TransitionToFaceAndBackAwayCliff()
{
  auto action = new CompoundActionSequential();

  // Turn to face cliff
  auto turnAction = new TurnTowardsPoseAction(GetCliffPoseToLookAt());
  turnAction->SetMaxPanSpeed(DEG_TO_RAD(kBodyTurnSpeedForCliffSearch_degPerSec)); // no fast turning near cliffs
  action->AddAction(turnAction);

  // Cliff reaction animation that causes the robot to backup about 8cm
  AnimationTrigger reactionAnim = AnimationTrigger::ReactToCliff;
  action->AddAction(new TriggerLiftSafeAnimationAction(reactionAnim));

  DelegateIfInControl(action, &BehaviorReactToCliff::TransitionToRecoveryBackup);
}

} // namespace Vector
} // namespace Anki

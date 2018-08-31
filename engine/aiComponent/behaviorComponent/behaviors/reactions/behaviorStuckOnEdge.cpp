/**
 * File: behaviorStuckOnEdge.cpp
 *
 * Author: Kevin Yoon
 * Created: 2018-05-08
 *
 * Description: Behavior that periodically plays a "distressed" animation because
 *              he's stuck and needs help from the user to get out of his situation.
 *
 * Copyright: Anki, Inc. 2018
 *
 **/


#include "engine/aiComponent/behaviorComponent/behaviors/reactions/behaviorStuckOnEdge.h"

#include "engine/actions/animActions.h"
#include "engine/actions/basicActions.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/beiRobotInfo.h"
#include "engine/components/powerStateManager.h"
#include "engine/components/sensors/cliffSensorComponent.h"

#include "coretech/common/engine/utils/timer.h"

namespace Anki {
namespace Vector {

namespace {
  const u8 kTracksToLock = (u8)AnimTrackFlag::BODY_TRACK | (u8)AnimTrackFlag::LIFT_TRACK;

  const float kMotionDetectGyroThresh_radps         = DEG_TO_RAD(5.f);
  const float kMotionDetectDurationThresh_sec       = 0.15f;
  const float kDisablePowerSaveOnMotionDuration_sec = 2.f;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorStuckOnEdge::DynamicVariables::DynamicVariables()
{
  startOfMotionDetectedTime_s = 0.f;
  enablePowerSaveModeTime_s   = 0.f;
  getInTrigger = AnimationTrigger::StuckOnEdgeGetIn;
  idleTrigger  = AnimationTrigger::StuckOnEdgeIdle;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorStuckOnEdge::BehaviorStuckOnEdge(const Json::Value& config)
: ICozmoBehavior(config)
{
  _iConfig.stuckOnEdgeCondition = BEIConditionFactory::CreateBEICondition(BEIConditionType::StuckOnEdge, GetDebugLabel());
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorStuckOnEdge::WantsToBeActivatedBehavior() const
{
  return _iConfig.stuckOnEdgeCondition->AreConditionsMet(GetBEI());
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorStuckOnEdge::InitBehavior()
{
  _iConfig.stuckOnEdgeCondition->Init(GetBEI());
  _iConfig.stuckOnEdgeCondition->SetActive(GetBEI(), true);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorStuckOnEdge::OnBehaviorActivated()
{
  _dVars = DynamicVariables();
  SetAnimTriggers();
  TriggerGetInAnim();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorStuckOnEdge::BehaviorUpdate()
{
  if (!IsActivated()) {
    return;
  }

  // Check if gyro motion detected this tic
  const GyroData gyroData = GetBEI().GetRobotInfo().GetHeadGyroData();
  const bool gyroMotionDetected = std::fabs(gyroData.x) > kMotionDetectGyroThresh_radps ||
                                  std::fabs(gyroData.y) > kMotionDetectGyroThresh_radps ||
                                  std::fabs(gyroData.z) > kMotionDetectGyroThresh_radps;

  const float currTime_s = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
  const auto& powerSaveManager = GetBehaviorComp<PowerStateManager>();
  const bool isPowerSaveRequestPending = powerSaveManager.IsPowerSaveRequestPending();
  const bool inPowerSaveMode = powerSaveManager.InPowerSaveMode();
  const bool inSysconCalmMode = powerSaveManager.InSysconCalmMode();

  // Check if we need to toggle power save mode but only when no motors are moving
  const bool motorsMoving = GetBEI().GetRobotInfo().GetMoveComponent().IsMoving();
  if (!motorsMoving && !isPowerSaveRequestPending) {
    if (inPowerSaveMode) {
      // If motion detected while in power save mode, temporarily deactivated power save mode
      // so that we can check cliff sensors to see if we're on solid ground again.
      if (gyroMotionDetected) {
        if (_dVars.startOfMotionDetectedTime_s == 0.f) {
          _dVars.startOfMotionDetectedTime_s = currTime_s;
        }
        if (currTime_s - _dVars.startOfMotionDetectedTime_s > kMotionDetectDurationThresh_sec) {
          PRINT_CH_INFO("Behaviors", "BehaviorStuckOnEdge.BehaviorUpdate.RemovePowerSaveModeRequest","");
          ICozmoBehavior::SmartRemovePowerSaveModeRequest();
          _dVars.startOfMotionDetectedTime_s = 0.f;
          _dVars.enablePowerSaveModeTime_s = currTime_s + kDisablePowerSaveOnMotionDuration_sec;
        }
      } else {
        _dVars.startOfMotionDetectedTime_s = 0.f;
      }
    } else if (!inSysconCalmMode && !gyroMotionDetected && (currTime_s > _dVars.enablePowerSaveModeTime_s)) {
      PRINT_CH_INFO("Behaviors", "BehaviorStuckOnEdge.BehaviorUpdate.RequestPowerSaveMode","");
      ICozmoBehavior::SmartRequestPowerSaveMode();
    }
  }

  // Check if we should cancel behavior based on pickup or no more cliffs detected
  const bool isPickedUp = GetBEI().GetRobotInfo().IsPickedUp();
  const bool noCliffs   = !inSysconCalmMode && !GetBEI().GetRobotInfo().GetCliffSensorComponent().IsCliffDetected();
  if (isPickedUp || noCliffs) {
    CancelSelf();
  }

}


void BehaviorStuckOnEdge::SetAnimTriggers()
{
  const auto& powerSaveManager = GetBehaviorComp<PowerStateManager>();
  const bool inSysconCalmMode = powerSaveManager.InSysconCalmMode();

  _dVars.getInTrigger = AnimationTrigger::StuckOnEdgeGetIn;
  _dVars.idleTrigger  = AnimationTrigger::StuckOnEdgeIdle;
  if (!inSysconCalmMode) {
    const auto& cliffComp = GetBEI().GetRobotInfo().GetCliffSensorComponent();
    bool leftCliffs = cliffComp.IsCliffDetected(CliffSensor::CLIFF_FL) && cliffComp.IsCliffDetected(CliffSensor::CLIFF_BL);
    bool rightCliffs = cliffComp.IsCliffDetected(CliffSensor::CLIFF_FR) && cliffComp.IsCliffDetected(CliffSensor::CLIFF_BR);

    // Only pick a "sided" animation if we're seeing cliffs on one side only
    if (leftCliffs != rightCliffs) {
      if (leftCliffs) {
        _dVars.getInTrigger = AnimationTrigger::StuckOnEdgeLeftGetIn;
        _dVars.idleTrigger  = AnimationTrigger::StuckOnEdgeLeftIdle;
      } else {
        _dVars.getInTrigger = AnimationTrigger::StuckOnEdgeRightGetIn;
        _dVars.idleTrigger  = AnimationTrigger::StuckOnEdgeRightIdle;
      }
    }
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorStuckOnEdge::TriggerGetInAnim()
{
  PRINT_CH_INFO("Behaviors", "BehaviorStuckOnEdge.TriggerGetInAnim", "%s", EnumToString(_dVars.getInTrigger));
  auto action = new TriggerAnimationAction(_dVars.getInTrigger, 1, true, kTracksToLock);
  DelegateIfInControl(action, &BehaviorStuckOnEdge::TriggerIdleAnim);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorStuckOnEdge::TriggerIdleAnim()
{
  PRINT_CH_INFO("Behaviors", "BehaviorStuckOnEdge.TriggerIdleAnim", "%s", EnumToString(_dVars.idleTrigger));
  auto action = new TriggerAnimationAction(_dVars.idleTrigger, 1, true, kTracksToLock);
  DelegateIfInControl(action, &BehaviorStuckOnEdge::TriggerIdleAnim);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorStuckOnEdge::OnBehaviorDeactivated()
{
}

} // namespace Vector
} // namespace Anki

/**
 * File: behaviorPlaypenEndChecks.cpp
 *
 * Author: Al Chaussee
 * Created: 08/09/17
 *
 * Description: Checks any final things playpen is interested in like battery voltage and that we have heard
 *              from an active object
 *
 * Copyright: Anki, Inc. 2017
 *
 **/

#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/playpen/behaviorPlaypenEndChecks.h"

#include "engine/actions/basicActions.h"
#include "engine/actions/compoundActions.h"
#include "engine/components/batteryComponent.h"
#include "engine/components/cubes/cubeCommsComponent.h"
#include "engine/factory/factoryTestLogger.h"
#include "engine/robot.h"

#include "util/fileUtils/fileUtils.h"

namespace Anki {
namespace Cozmo {

BehaviorPlaypenEndChecks::BehaviorPlaypenEndChecks(const Json::Value& config)
: IBehaviorPlaypen(config)
{
  SubscribeToTags({
    EngineToGameTag::ObjectAvailable
  });
}


void BehaviorPlaypenEndChecks::OnBehaviorEnteredActivatableScope()
{
  Robot& robot = GetBEI().GetRobotInfo()._robot;
  
  // Tell cube comms to broadcast object available messages so we can
  // hear from advertising cubes
  robot.GetCubeCommsComponent().SetBroadcastObjectAvailable();
}


Result BehaviorPlaypenEndChecks::OnBehaviorActivatedInternal()
{
  // DEPRECATED - Grabbing robot to support current cozmo code, but this should
  // be removed
  Robot& robot = GetBEI().GetRobotInfo()._robot;

  if(robot.GetBatteryVoltage() < PlaypenConfig::kMinBatteryVoltage)
  {
    PRINT_NAMED_WARNING("BehaviorPlaypenEndChecks.OnActivated.BatteryTooLow", "%fv", robot.GetBatteryVoltage());
    PLAYPEN_SET_RESULT_WITH_RETURN_VAL(FactoryTestResultCode::BATTERY_TOO_LOW, RESULT_FAIL);
  }
  
  if(!PlaypenConfig::kSkipActiveObjectCheck && !_heardFromLightCube)
  {
    PLAYPEN_SET_RESULT_WITH_RETURN_VAL(FactoryTestResultCode::NO_ACTIVE_OBJECTS_DISCOVERED, RESULT_FAIL);
  }

  if(!DidReceiveFFTResult())
  {
    PLAYPEN_SET_RESULT_WITH_RETURN_VAL(FactoryTestResultCode::NO_FFT_RESULT, RESULT_FAIL);
  }

  if(PlaypenConfig::kCheckForCert &&
     Util::FileUtils::GetFileSize(PlaypenConfig::kCertPath) < PlaypenConfig::kMinCertSize_bytes)
  {
    PLAYPEN_SET_RESULT_WITH_RETURN_VAL(FactoryTestResultCode::CERT_CHECK_FAILED, RESULT_FAIL);
  }

  TurnInPlaceAction* turn = new TurnInPlaceAction(DEG_TO_RAD(90), false);
  MoveHeadToAngleAction* head = new MoveHeadToAngleAction(MAX_HEAD_ANGLE);
  CompoundActionParallel* action = new CompoundActionParallel({turn, head});

  DelegateIfInControl(action, [this](){ PLAYPEN_SET_RESULT(FactoryTestResultCode::SUCCESS); });

  return RESULT_OK;
}

void BehaviorPlaypenEndChecks::OnBehaviorDeactivated()
{
  _heardFromLightCube = false;
}

void BehaviorPlaypenEndChecks::AlwaysHandleInScope(const EngineToGameEvent& event)
{
  const auto& eventData = event.GetData();
  if (eventData.GetTag() == EngineToGameTag::ObjectAvailable) {
    if(IsValidLightCube(eventData.Get_ObjectAvailable().objectType, false))
    {
      _heardFromLightCube = true;
    }
  }
}

}
}



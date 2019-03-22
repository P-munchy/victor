/**
 * File: BehaviorSDKInterface.cpp
 *
 * Author: Michelle Sintov
 * Created: 2018-05-21
 *
 * Description: Interface for SDKs including C# and Python
 *
 * Copyright: Anki, Inc. 2018
 *
 **/


#include "coretech/common/engine/utils/timer.h"
#include "engine/aiComponent/behaviorComponent/behaviorContainer.h"
#include "engine/aiComponent/behaviorComponent/behaviors/sdkBehaviors/behaviorSDKInterface.h"
#include "engine/aiComponent/behaviorComponent/behaviors/basicWorldInteractions/behaviorDriveOffCharger.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/beiRobotInfo.h"
#include "engine/components/movementComponent.h"
#include "engine/components/sdkComponent.h"
#include "engine/components/settingsManager.h"
#include "engine/cozmoContext.h"
#include "engine/externalInterface/externalMessageRouter.h"
#include "engine/externalInterface/gatewayInterface.h"

namespace Anki {
namespace Vector {

namespace {
const char* const kBehaviorControlLevelKey = "behaviorControlLevel";
const char* const kDisableCliffDetection = "disableCliffDetection";
const char* const kDriveOffChargerBehaviorKey = "driveOffChargerBehavior";
const char* const kFindAndGoToHomeBehaviorKey = "findAndGoToHomeBehavior";
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorSDKInterface::InstanceConfig::InstanceConfig()
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorSDKInterface::DynamicVariables::DynamicVariables()
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorSDKInterface::BehaviorSDKInterface(const Json::Value& config)
 : ICozmoBehavior(config)
{
  const std::string& debugName = "Behavior" + GetDebugLabel() + ".LoadConfig";
  _iConfig.behaviorControlLevel = JsonTools::ParseInt32(config, kBehaviorControlLevelKey, debugName);
  ANKI_VERIFY(external_interface::ControlRequest_Priority_IsValid(_iConfig.behaviorControlLevel),
              "BehaviorSDKInterface::BehaviorSDKInterface", "Invalid behaviorControlLevel %u", _iConfig.behaviorControlLevel);
  _iConfig.disableCliffDetection = JsonTools::ParseBool(config, kDisableCliffDetection, debugName);
  _iConfig.driveOffChargerBehaviorStr = JsonTools::ParseString(config, kDriveOffChargerBehaviorKey, debugName);
  _iConfig.findAndGoToHomeBehaviorStr = JsonTools::ParseString(config, kFindAndGoToHomeBehaviorKey, debugName);

  SubscribeToTags({
    EngineToGameTag::RobotCompletedAction,
  });

  SubscribeToAppTags({
    AppToEngineTag::kDriveOffChargerRequest,
    AppToEngineTag::kDriveOnChargerRequest,
  });
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorSDKInterface::WantsToBeActivatedBehavior() const
{
  // Check whether the SDK wants control for the control level that this behavior instance is for.
  auto& robotInfo = GetBEI().GetRobotInfo();
  auto& sdkComponent = robotInfo.GetSDKComponent();
  return sdkComponent.SDKWantsControl() && (sdkComponent.SDKControlLevel()==_iConfig.behaviorControlLevel);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorSDKInterface::GetBehaviorOperationModifiers(BehaviorOperationModifiers& modifiers) const
{
  modifiers.wantsToBeActivatedWhenCarryingObject  = true;
  modifiers.wantsToBeActivatedWhenOnCharger       = true;
  modifiers.wantsToBeActivatedWhenOffTreads       = true;
  modifiers.behaviorAlwaysDelegates               = false;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorSDKInterface::GetAllDelegates(std::set<IBehavior*>& delegates) const
{
  delegates.insert(_iConfig.driveOffChargerBehavior.get());
  delegates.insert(_iConfig.findAndGoToHomeBehavior.get());
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorSDKInterface::InitBehavior()
{
  const auto& BC = GetBEI().GetBehaviorContainer();
  _iConfig.driveOffChargerBehavior = BC.FindBehaviorByID(BehaviorTypesWrapper::BehaviorIDFromString(_iConfig.driveOffChargerBehaviorStr));
  DEV_ASSERT(_iConfig.driveOffChargerBehavior != nullptr,
             "BehaviorFindFaces.InitBehavior.NullDriveOffChargerBehavior");

  _iConfig.findAndGoToHomeBehavior = BC.FindBehaviorByID(BehaviorTypesWrapper::BehaviorIDFromString(_iConfig.findAndGoToHomeBehaviorStr));
  DEV_ASSERT(_iConfig.findAndGoToHomeBehavior != nullptr,
             "BehaviorFindFaces.InitBehavior.NullFindAndGoToHomeBehavior");
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorSDKInterface::GetBehaviorJsonKeys(std::set<const char*>& expectedKeys) const
{
  const char* list[] = {
    kBehaviorControlLevelKey,
    kDisableCliffDetection,
    kDriveOffChargerBehaviorKey,
    kFindAndGoToHomeBehaviorKey,
  };
  expectedKeys.insert( std::begin(list), std::end(list) );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorSDKInterface::OnBehaviorActivated() 
{
  // reset dynamic variables
  _dVars = DynamicVariables();

  auto& robotInfo = GetBEI().GetRobotInfo();
  
  // Permit low level movement commands/actions to run since SDK behavior is now active.
  SetAllowExternalMovementCommands(true);

  if (_iConfig.disableCliffDetection) {
    robotInfo.EnableStopOnCliff(false);
  }

  // Tell the robot component that the SDK has been activated
  auto& sdkComponent = robotInfo.GetSDKComponent();
  sdkComponent.SDKBehaviorActivation(true);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorSDKInterface::OnBehaviorDeactivated()
{
  // Tell the robot component that the SDK has been deactivated
  auto& robotInfo = GetBEI().GetRobotInfo();
  auto& sdkComponent = robotInfo.GetSDKComponent();
  sdkComponent.SDKBehaviorActivation(false);

  // Unsets eye color. Also unsets any other changes though SDK only changes eye color but this is good future proofing.
  SettingsManager& settings = GetBEI().GetSettingsManager();
  settings.ApplyAllCurrentSettings();

  // Release all track locks which may have been acquired by an SDK user
  robotInfo.GetMoveComponent().UnlockAllTracks();
  // Do not permit low level movement commands/actions to run since SDK behavior is no longer active.
  SetAllowExternalMovementCommands(false);
  // Re-enable cliff detection that SDK may have disabled
  if (_iConfig.disableCliffDetection) {
    robotInfo.EnableStopOnCliff(true);
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorSDKInterface::BehaviorUpdate() 
{
  if (!IsActivated()) {
    return;
  }

  // TODO Consider which slot should be deactivated once SDK occupies multiple slots.
  auto& robotInfo = GetBEI().GetRobotInfo();
  auto& sdkComponent = robotInfo.GetSDKComponent();
  if (!sdkComponent.SDKWantsControl())
  {
    CancelSelf();
  }
}

void BehaviorSDKInterface::HandleDriveOffChargerComplete() {
  SetAllowExternalMovementCommands(true);
  auto* gi = GetBEI().GetRobotInfo().GetGatewayInterface();
  if( gi != nullptr ) {
    auto* driveOffChargerResponse = new external_interface::DriveOffChargerResponse;
    driveOffChargerResponse->set_result(external_interface::BehaviorResults::BEHAVIOR_COMPLETE_STATE);
    gi->Broadcast( ExternalMessageRouter::WrapResponse(driveOffChargerResponse) );
  }
}  

void BehaviorSDKInterface::HandleDriveOnChargerComplete() {
  SetAllowExternalMovementCommands(true);
  auto* gi = GetBEI().GetRobotInfo().GetGatewayInterface();
  if( gi != nullptr ) {
    auto* driveOnChargerResponse = new external_interface::DriveOnChargerResponse;
    driveOnChargerResponse->set_result(external_interface::BehaviorResults::BEHAVIOR_COMPLETE_STATE);
    gi->Broadcast( ExternalMessageRouter::WrapResponse(driveOnChargerResponse) );
  }
}

// Reports back to gateway that requested actions have been completed.
// E.g., the Python SDK ran play_animation and wants to know when the animation
// action was completed.
void BehaviorSDKInterface::HandleWhileActivated(const EngineToGameEvent& event)
{
  if (IsControlDelegated()) {
    // The SDK behavior has delegated to another behavior, and that
    // behavior requested an action. Don't inform gateway that the
    // action has completed because it wasn't requested by the SDK.
    //
    // If necessary, can delegate to actions from the behavior instead
    // of running them via CLAD request from gateway.
    return;
  }

  if (event.GetData().GetTag() != EngineToGameTag::RobotCompletedAction) {
    return;
  }

  auto& robotInfo = GetBEI().GetRobotInfo();
  auto& sdkComponent = robotInfo.GetSDKComponent();

  ExternalInterface::RobotCompletedAction msg = event.GetData().Get_RobotCompletedAction();
  sdkComponent.OnActionCompleted(msg);
}

void BehaviorSDKInterface::SetAllowExternalMovementCommands(const bool allow) {
  auto& robotInfo = GetBEI().GetRobotInfo();
  robotInfo.GetMoveComponent().AllowExternalMovementCommands(allow, GetDebugLabel());
}

void BehaviorSDKInterface::HandleWhileActivated(const AppToEngineEvent& event) {
  switch(event.GetData().GetTag())
  {
    case external_interface::GatewayWrapperTag::kDriveOffChargerRequest:
      DriveOffChargerRequest(event.GetData().drive_off_charger_request());
      break;

    case external_interface::GatewayWrapperTag::kDriveOnChargerRequest:
      DriveOnChargerRequest(event.GetData().drive_on_charger_request());
      break;

    default:
    {
      PRINT_NAMED_WARNING("BehaviorSDKInterface.HandleWhileActivated.NoMatch", "No match for action tag so no response sent: [Tag=%d]", (int)event.GetData().GetTag());
      return;
    }
  }
}

// Delegate to the DriveOffCharger behavior
void BehaviorSDKInterface::DriveOffChargerRequest(const external_interface::DriveOffChargerRequest& driveOffChargerRequest) {
  if (_iConfig.driveOffChargerBehavior->WantsToBeActivated()) {
    if (DelegateIfInControl(_iConfig.driveOffChargerBehavior.get(), &BehaviorSDKInterface::HandleDriveOffChargerComplete)) {
      SetAllowExternalMovementCommands(false);
      return;
    }
  }

  // If we got this far, we failed to activate the requested behavior.
  auto* gi = GetBEI().GetRobotInfo().GetGatewayInterface();
  if( gi != nullptr ) {
    auto* driveOffChargerResponse = new external_interface::DriveOffChargerResponse;
    driveOffChargerResponse->set_result(external_interface::BehaviorResults::BEHAVIOR_WONT_ACTIVATE_STATE);
    gi->Broadcast( ExternalMessageRouter::WrapResponse(driveOffChargerResponse) );
  }
}

// Delegate to FindAndGoToHome
void BehaviorSDKInterface::DriveOnChargerRequest(const external_interface::DriveOnChargerRequest& driveOnChargerRequest) {
  if (_iConfig.findAndGoToHomeBehavior->WantsToBeActivated()) {
    if (DelegateIfInControl(_iConfig.findAndGoToHomeBehavior.get(), &BehaviorSDKInterface::HandleDriveOnChargerComplete)) {
      SetAllowExternalMovementCommands(false);
      return;
    }
  }

  // If we got this far, we failed to activate the requested behavior.
  auto* gi = GetBEI().GetRobotInfo().GetGatewayInterface();
  if( gi != nullptr ) {
    auto* driveOnChargerResponse = new external_interface::DriveOnChargerResponse;
    driveOnChargerResponse->set_result(external_interface::BehaviorResults::BEHAVIOR_WONT_ACTIVATE_STATE);
    gi->Broadcast( ExternalMessageRouter::WrapResponse(driveOnChargerResponse) );
  }
}

} // namespace Vector
} // namespace Anki

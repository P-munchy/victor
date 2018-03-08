/**
 * File: behaviorRequestToGoHome.cpp
 *
 * Author: Matt Michini
 * Created: 01/23/18
 *
 * Description: Find a face, turn toward it, and request to be taken back
 *              to the charger.
 *
 * Copyright: Anki, Inc. 2018
 *
 **/

#include "engine/aiComponent/behaviorComponent/behaviors/basicWorldInteractions/behaviorRequestToGoHome.h"

#include "engine/actions/animActions.h"
#include "engine/actions/basicActions.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/behaviorExternalInterface.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/beiRobotInfo.h"
#include "engine/aiComponent/behaviorComponent/behaviorTypesWrapper.h"
#include "engine/aiComponent/behaviorComponent/behaviorContainer.h"
#include "engine/blockWorld/blockWorld.h"
#include "engine/events/animationTriggerHelpers.h"
#include "engine/faceWorld.h"

#include "coretech/common/engine/jsonTools.h"

namespace Anki {
namespace Cozmo {

namespace {
const char* kNumRequestsKey                = "numRequests";
const char* kRequestAnimTriggerKey         = "requestAnimTrigger";
const char* kRequestGetoutAnimTriggerKey   = "getoutAnimTrigger";
const char* kRequestWaitLoopAnimTriggerKey = "waitLoopAnimTrigger";
const char* kRequestIdleWaitTimeKey        = "idleWaitTime_sec";
const char* kPickupAnimTriggerKey          = "pickupAnimTrigger";
const char* kMaxFaceAgeKey                 = "maxFaceAge_sec";
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorRequestToGoHome::InstanceConfig::InstanceConfig()
{
  pickupAnimTrigger = AnimationTrigger::Count;
  maxFaceAge_sec = 0.f;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorRequestToGoHome::DynamicVariables::DynamicVariables(const InstanceConfig& iConfig)
{
  currRequestParamsPtr = &iConfig.normalRequest;
  currRequestType = ERequestType::Normal;
  state = EState::Init;
  numNormalRequests = 0;
  numSevereRequests = 0;
  imageTimestampWhenActivated = 0;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorRequestToGoHome::BehaviorRequestToGoHome(const Json::Value& config)
: ICozmoBehavior(config)
, _dVars(_iConfig)
{
  LoadConfig(config["params"]);
}
 

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorRequestToGoHome::WantsToBeActivatedBehavior() const
{
  // Should not run if we already know where a charger is
  BlockWorldFilter filter;
  filter.AddAllowedType(ObjectType::Charger_Basic);
  return (nullptr == GetBEI().GetBlockWorld().FindLocatedMatchingObject(filter));
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorRequestToGoHome::GetAllDelegates(std::set<IBehavior*>& delegates) const
{
  delegates.insert(_iConfig.findFacesBehavior.get());
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorRequestToGoHome::InitBehavior()
{
  const auto& BC = GetBEI().GetBehaviorContainer();
  _iConfig.findFacesBehavior = BC.FindBehaviorByID(BEHAVIOR_ID(ObservingFindFaces));
  DEV_ASSERT(_iConfig.findFacesBehavior != nullptr, "BehaviorRequestToGoHome.InitBehavior.NullFindFacesBehavior");
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorRequestToGoHome::OnBehaviorActivated()
{
  _dVars.numNormalRequests = 0;
  _dVars.numSevereRequests = 0;
  _dVars.state = EState::Init;
  UpdateCurrRequestTypeAndLoadParams();
  
  // Do we have any known faces?
  _dVars.imageTimestampWhenActivated = GetBEI().GetRobotInfo().GetLastImageTimeStamp();
  TimeStamp_t maxFaceAge_ms = Util::numeric_cast<TimeStamp_t>(1000.f * _iConfig.maxFaceAge_sec);
  maxFaceAge_ms = std::min(maxFaceAge_ms, _dVars.imageTimestampWhenActivated);
  TimeStamp_t oldestFaceTimestamp = _dVars.imageTimestampWhenActivated - maxFaceAge_ms;
  
  const bool hasFace = GetBEI().GetFaceWorld().HasAnyFaces(oldestFaceTimestamp);
  if (hasFace) {
    // Turn to last known face. Even if it fails, still jump to playing the animation
    DelegateIfInControl(new TurnTowardsLastFacePoseAction(),
                        &BehaviorRequestToGoHome::TransitionToRequestAnim);
  } else {
    TransitionToSearchingForFaces();
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorRequestToGoHome::BehaviorUpdate()
{
  if (!IsActivated()) {
    return;
  }
  
  // Update the current request type and swap in the appropriate parameters
  UpdateCurrRequestTypeAndLoadParams();
      
  if (_dVars.state == EState::FindingFaces) {
    if (IsControlDelegated()) {
      // Check if we've found a face since being activated
      const bool hasFace = GetBEI().GetFaceWorld().HasAnyFaces(_dVars.imageTimestampWhenActivated);
      if (hasFace) {
        DEV_ASSERT(_iConfig.findFacesBehavior->IsActivated(), "BehaviorRequestToGoHome.BehaviorUpdate.FindFacesNotActive");
        CancelDelegates();
        TransitionToRequestAnim();
      }
    } else {
      // FindFaces behavior must have ended without successfully
      // finding any faces. Transition to low power mode.
      TransitionToLowPowerMode();
    }
  } else if ((_dVars.state == EState::Requesting) &&
             !IsControlDelegated()) {
    // The request animations have stopped - determine the next action to take.
    if (_dVars.currRequestType == ERequestType::LowPower) {
      TransitionToLowPowerMode();
    } else {
      TransitionToRequestAnim();
    }
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorRequestToGoHome::TransitionToSearchingForFaces()
{
  // Delegate to the find faces behavior, but if it doesn't want to run,
  // then just skip to playing the animations.
  if (_iConfig.findFacesBehavior->WantsToBeActivated()) {
    _dVars.state = EState::FindingFaces;
    DelegateIfInControl(_iConfig.findFacesBehavior.get());
  } else {
    TransitionToRequestAnim();
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorRequestToGoHome::TransitionToRequestAnim()
{
  _dVars.state = EState::Requesting;
  
  auto* action = new CompoundActionSequential();
  // Turn toward face, but only for non-severe requests
  if (_dVars.currRequestType != ERequestType::Severe) {
    action->AddAction(new TurnTowardsLastFacePoseAction());
  }
  action->AddAction(new TriggerAnimationAction(_dVars.currRequestParamsPtr->requestAnimTrigger));

  DelegateIfInControl(action, &BehaviorRequestToGoHome::TransitionToRequestWaitLoopAnim);
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorRequestToGoHome::TransitionToRequestWaitLoopAnim()
{
  const auto animTimeout = _dVars.currRequestParamsPtr->idleWaitTime_sec;
  const auto& animTrigger = _dVars.currRequestParamsPtr->waitLoopAnimTrigger;
  auto* action = new TriggerAnimationAction(animTrigger,
                                            0,    // numLoops
                                            true, // interrupt running
                                            (u8)AnimTrackFlag::NO_TRACKS,
                                            animTimeout);
  
  DelegateIfInControl(action, &BehaviorRequestToGoHome::TransitionToRequestGetoutAnim);
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorRequestToGoHome::TransitionToRequestGetoutAnim()
{
  auto* action = new TriggerAnimationAction(_dVars.currRequestParamsPtr->getoutAnimTrigger);
  
  DelegateIfInControl(action,
                      [this]() {
                        // Increment the appropriate request counters now that a request has completed
                        if (_dVars.currRequestType == ERequestType::Normal) {
                          ++_dVars.numNormalRequests;
                        } else if (_dVars.currRequestType == ERequestType::Severe) {
                          ++_dVars.numSevereRequests;
                        }
                      });
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorRequestToGoHome::TransitionToLowPowerMode()
{
  // Remain in "low power mode", occasionally asking for help.
  // NOTE: Low power mode is not quite defined yet, so simply end the behavior.
  PRINT_NAMED_WARNING("BehaviorRequestToGoHome.TransitionToLowPowerMode.LowPowerModeUndefined",
                      "Should transition to \"low power mode\" here");
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorRequestToGoHome::UpdateCurrRequestTypeAndLoadParams()
{
  if (_dVars.numNormalRequests < _iConfig.normalRequest.numRequests) {
    _dVars.currRequestType = ERequestType::Normal;
    _dVars.currRequestParamsPtr = &_iConfig.normalRequest;
  } else if (_dVars.numSevereRequests < _iConfig.severeRequest.numRequests) {
    _dVars.currRequestType = ERequestType::Severe;
    _dVars.currRequestParamsPtr = &_iConfig.severeRequest;
  } else {
    _dVars.currRequestType = ERequestType::LowPower;
    _dVars.currRequestParamsPtr = &_iConfig.severeRequest;
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorRequestToGoHome::LoadConfig(const Json::Value& config)
{
  const std::string& debugName = "Behavior" + GetDebugLabel() + ".LoadConfig";
  
  const std::map<std::string, RequestParams*> configEntryMap {
    {"normal", &_iConfig.normalRequest},
    {"severe", &_iConfig.severeRequest},
  };
  
  for (auto& pair : configEntryMap) {
    const auto& json = config[pair.first];
    auto* params = pair.second;
    
    params->numRequests         = JsonTools::ParseUint8(json, kNumRequestsKey, debugName);
    params->requestAnimTrigger  = JsonTools::ParseAnimationTrigger(json, kRequestAnimTriggerKey, debugName);
    params->getoutAnimTrigger   = JsonTools::ParseAnimationTrigger(json, kRequestGetoutAnimTriggerKey, debugName);
    params->waitLoopAnimTrigger = JsonTools::ParseAnimationTrigger(json, kRequestWaitLoopAnimTriggerKey, debugName);
    params->idleWaitTime_sec    = JsonTools::ParseFloat(json, kRequestIdleWaitTimeKey, debugName);
  }
  
  _iConfig.pickupAnimTrigger = JsonTools::ParseAnimationTrigger(config, kPickupAnimTriggerKey, debugName);
  _iConfig.maxFaceAge_sec    = JsonTools::ParseFloat(config, kMaxFaceAgeKey, debugName);
}


} // namespace Cozmo
} // namespace Anki

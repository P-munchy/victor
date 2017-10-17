/**
 * File: reactionTriggerStrategyFistBump.cpp
 *
 * Author: Kevin Yoon
 * Created: 01/26/17
 *
 * Description: Reaction Trigger strategy for doing fist bump
 *              both at the end of sparks or during freeplay as a celebratory behavior.
 *
 * Copyright: Anki, Inc. 2017
 *
 *
 **/


#include "engine/aiComponent/behaviorSystem/reactionTriggerStrategies/reactionTriggerStrategyFistBump.h"
#include "engine/aiComponent/behaviorSystem/behaviors/iBehavior.h"
#include "engine/robot.h"
#include "anki/common/basestation/utils/timer.h"
#include "util/console/consoleInterface.h"

namespace Anki {
namespace Cozmo {

namespace{
  
// Effectively make cooldown time = 0 and triggerProbability = 1 for all BehaviorObjective triggers
CONSOLE_VAR(bool, kAlwaysTrigger, "Behavior.FistBump", false);
  
static const char* kTriggerStrategyName   = "Trigger Strategy Fist Bump";
  
static const char* kReactionConfigKey     = "behaviorObjectiveTriggerParams";
static const char* kBehaviorObjectiveKey  = "behaviorObjective";
static const char* kCooldownTime_sKey     = "triggerCooldownTime_s";
static const char* kTriggerProbabilityKey = "triggerProbability";
static const char* kTriggerExpirationKey  = "triggerExpiration_s";
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
ReactionTriggerStrategyFistBump::ReactionTriggerStrategyFistBump(BehaviorExternalInterface& behaviorExternalInterface, const Json::Value& config)
: IReactionTriggerStrategy(behaviorExternalInterface, config, kTriggerStrategyName)
  , _shouldTrigger(false)
  , _shouldTriggerExpirationTime_sec(0)
  , _lastFistBumpCompleteTime_sec(0)
{
  LoadJson(config);
  
  SubscribeToTags({
    ExternalInterface::MessageEngineToGameTag::BehaviorObjectiveAchieved
  });
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ReactionTriggerStrategyFistBump::LoadJson(const Json::Value& config)
{
  const Json::Value& behaviorObjectiveTriggerParams = config[kReactionConfigKey];
  if(behaviorObjectiveTriggerParams.isArray()){
    
    for (auto& params : behaviorObjectiveTriggerParams)
    {
      if (params.isNull()) {
        PRINT_NAMED_WARNING("ReactionTriggerStrategyFistBump.LoadJson.NullParam", "");
        continue;
      }
      
      // Get BehaviorObjective trigger
      const std::string& objectiveStr = JsonTools::ParseString(params, kBehaviorObjectiveKey,
                                                               "ReactionTriggerStrategyFistBump.LoadJson.NullBehaviorObjective");
      BehaviorObjective objective = BehaviorObjectiveFromString(objectiveStr.c_str());
      if (objective == BehaviorObjective::Count) {
        PRINT_NAMED_ERROR("ReactionTriggerStrategyFistBump.LoadJson.UnknownBehaviorObjective", "%s", objectiveStr.c_str());
        continue;
      }
      
      // Get cooldown time
      float cooldownTime_s = params.get(kCooldownTime_sKey, -1.f).asFloat();
      if (cooldownTime_s < 0.f) {
        PRINT_NAMED_ERROR("ReactionTriggerStrategyFistBump.LoadJson.UnspecifiedCooldownTime", "%s", objectiveStr.c_str());
        continue;
      }
      
      // Get probability of triggering
      float triggerProbability = params.get(kTriggerProbabilityKey, -1.f).asFloat();
      if (triggerProbability <= 0.f || triggerProbability > 1.f) {
        PRINT_NAMED_ERROR("ReactionTriggerStrategyFistBump.LoadJson.InvalidTriggerProbability", "%s", objectiveStr.c_str());
        continue;
      }
      
      // Get trigger expiration
      float triggerExpiration_s = params.get(kTriggerExpirationKey, 0.f).asFloat();
      if (triggerExpiration_s <= 0.f) {
        PRINT_NAMED_ERROR("ReactionTriggerStrategyFistBump.LoadJson.UnspecifiedTriggerExpirationTime", "%s", objectiveStr.c_str());
        continue;
      }
      
      // Add to trigger map
      PRINT_NAMED_INFO("ReactionTriggerStrategyFistBump.LoadJson.AddingTrigger",
                       "%s: cooldownTime_s %f, triggerProb %f, triggerExpiration_s %f",
                       objectiveStr.c_str(), cooldownTime_s, triggerProbability, triggerExpiration_s);
      _triggerParamsMap[objective] = {
        .cooldownTime_s = cooldownTime_s,
        .triggerProbability = triggerProbability,
        .triggerExpiration_s = triggerExpiration_s
      };
 
    }
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ReactionTriggerStrategyFistBump::SetupForceTriggerBehavior(BehaviorExternalInterface& behaviorExternalInterface, const IBehaviorPtr behavior)
{
  behavior->IsRunnable(behaviorExternalInterface);
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool ReactionTriggerStrategyFistBump::ShouldTriggerBehaviorInternal(BehaviorExternalInterface& behaviorExternalInterface, const IBehaviorPtr behavior)
{
  const float now = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
  
  if (_shouldTrigger) {
    
    // If behavior is running then reset
    if (behavior->IsRunning()) {
      _shouldTrigger = false;
    }
    
    // Has FistBump not triggered for a while since shouldTrigger became true?
    // Then reset shouldTrigger since FistBump shouldn't play too long after the thing that triggers it.
    else if (now > _shouldTriggerExpirationTime_sec) {
      LOG_EVENT("robot.trigger_fist_bump_expired", "");
      _shouldTrigger = false;
    }
  }
  
  return _shouldTrigger &&
         (behaviorExternalInterface.GetOffTreadsState() == OffTreadsState::OnTreads) &&
         behavior->IsRunnable(behaviorExternalInterface);
}


// Listen for achieved objectives
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ReactionTriggerStrategyFistBump::AlwaysHandleInternal(const EngineToGameEvent& event, BehaviorExternalInterface& behaviorExternalInterface)
{
  switch (event.GetData().GetTag())
  {
    case EngineToGameTag::BehaviorObjectiveAchieved:
    {
      const float now = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
      
      // Is this a BehaviorObjective that should trigger fist bump?
      BehaviorObjective objective = event.GetData().Get_BehaviorObjectiveAchieved().behaviorObjective;
      auto paramIt = _triggerParamsMap.find(objective);
      if (paramIt != _triggerParamsMap.end()) {
        
        // Debug override for testing fist bump
        if (kAlwaysTrigger) {
          _shouldTrigger = true;
          _shouldTriggerExpirationTime_sec = now + paramIt->second.triggerExpiration_s;
          break;
        }
        
        // Has cooldown condition been met?
        if ((_lastFistBumpCompleteTime_sec == 0) || (now - _lastFistBumpCompleteTime_sec > paramIt->second.cooldownTime_s)) {

          // Roll dice on triggering
          if (GetRNG().RandDbl(1.f) < paramIt->second.triggerProbability) {
            LOG_EVENT("robot.trigger_fist_bump_response", "%s", EnumToString(paramIt->first));
            _shouldTrigger = true;
            _shouldTriggerExpirationTime_sec = now + paramIt->second.triggerExpiration_s;
          }
        }
        
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
void ReactionTriggerStrategyFistBump::BehaviorThatStrategyWillTriggerInternal(IBehaviorPtr behavior)
{
  behavior->AddListener(this);
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ReactionTriggerStrategyFistBump::ResetTrigger(bool updateLastCompletionTime)
{
  _shouldTrigger = false;
  if (updateLastCompletionTime) {
    _lastFistBumpCompleteTime_sec = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
  }
}
  
} // namespace Cozmo
} // namespace Anki

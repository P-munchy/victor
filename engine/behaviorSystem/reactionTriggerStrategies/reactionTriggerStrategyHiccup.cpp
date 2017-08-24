/**
 * File: reactionTriggerStrategyHiccup.cpp
 *
 * Author: Al Chaussee
 * Created: 3/2/17
 *
 * Description: Reaction Trigger strategy for responding to hiccups
 *              Hiccups can be "cured" by putting Cozmo on his back/face
 *
 * Copyright: Anki, Inc. 2017
 *
 *
 **/

#include "engine/behaviorSystem/reactionTriggerStrategies/reactionTriggerStrategyHiccup.h"

#include "anki/common/basestation/utils/timer.h"
#include "engine/behaviorSystem/behaviorManager.h"
#include "engine/behaviorSystem/behaviors/animationWrappers/behaviorPlayAnimSequence.h"
#include "engine/behaviorSystem/behaviors/iBehavior.h"
#include "engine/aiComponent/aiComponent.h"
#include "engine/aiComponent/AIWhiteboard.h"
#include "engine/behaviorSystem/behaviorPreReqs/behaviorPreReqAnimSequence.h"
#include "engine/components/dockingComponent.h"
#include "engine/components/progressionUnlockComponent.h"
#include "engine/cozmoContext.h"
#include "engine/events/animationTriggerHelpers.h"
#include "engine/externalInterface/externalInterface.h"
#include "engine/needsSystem/needsManager.h"
#include "engine/needsSystem/needsState.h"
#include "engine/robot.h"
#include "engine/utils/cozmoFeatureGate.h"
#include "util/console/consoleInterface.h"


#define DEBUG_HICCUPS 0

namespace Anki {
namespace Cozmo {

CONSOLE_VAR(bool, kCanHiccupWhileDocking, "Hiccups", true);

ReactionTriggerStrategyHiccup* _this = nullptr;
void ForceHiccups( ConsoleFunctionContextRef context )
{
  if(_this != nullptr)
  {
    _this->ForceHiccups();
  }
  else
  {
    PRINT_NAMED_WARNING("ReactionTriggerStrategyHiccup", "No hiccup strategy");
  }
}
CONSOLE_FUNC(ForceHiccups, "Hiccups");

namespace {
  static const char* kTriggerStrategyName = "Trigger strategy hiccups";
  
  static const char* kConfigParamsKey = "hiccupParams";
  
  static const char* kMinHiccupOccurrenceFrequencyKey = "minHiccupOccurrenceFrequency_s";
  static const char* kMaxHiccupOccurrenceFrequencyKey = "maxHiccupOccurrenceFrequency_s";
  
  static const char* kMinNumberOfHiccupsToDoKey = "minNumberOfHiccupsToDo";
  static const char* kMaxNumberOfHiccupsToDoKey = "maxNumberOfHiccupsToDo";
  
  static const char* kMinHiccupSpacingKey = "minHiccupSpacing_ms";
  static const char* kMaxHiccupSpacingKey = "maxHiccupSpacing_ms";
  
  static const char* kHiccupsWontOccurAfterBeingCuredKey = "hiccupsWontOccurAfterBeingCuredTime_s";
  
  static const char* kHiccupUnlockId = "hiccupsUnlockId";
}

ReactionTriggerStrategyHiccup::ReactionTriggerStrategyHiccup(Robot& robot, const Json::Value& config)
: IReactionTriggerStrategy(robot, config, kTriggerStrategyName)
, _externalInterface((robot.HasExternalInterface() ? robot.GetExternalInterface() : nullptr))
, _rng(robot.GetRNG())
, _whiteboard(robot.GetAIComponent().GetWhiteboard())
{
  ParseConfig(config[kConfigParamsKey]);

  ResetHiccups();
  
  SubscribeToTags({
    EngineToGameTag::RobotOffTreadsStateChanged
  });
  
  SubscribeToTags({
    GameToEngineTag::NotifyOverfeedingShouldTriggerHiccups
  });
  
  // For debug purposes so we can use a console function to give Cozmo the hiccups
  _this = this;
}

ReactionTriggerStrategyHiccup::~ReactionTriggerStrategyHiccup()
{
  if(HasHiccups())
  {
    SendDasEvent(false);
  }
  
  _this = nullptr;
}

void ReactionTriggerStrategyHiccup::SetupForceTriggerBehavior(const Robot& robot, const IBehaviorPtr behavior)
{
  BehaviorPreReqAnimSequence req(robot, GetHiccupAnim());
  behavior->IsRunnable(req);
}

  
bool ReactionTriggerStrategyHiccup::ShouldTriggerBehaviorInternal(const Robot& robot, const IBehaviorPtr behavior)
{
  // If Hiccups are not enabled then do nothing
  if(!robot.GetContext()->GetFeatureGate()->IsFeatureEnabled(FeatureType::Hiccups))
  {
    return false;
  }
  
  // If the unlock specified in the config is not unlocked then don't hiccup
  if(!robot.GetProgressionUnlockComponent().IsUnlocked(_hiccupsUnlockId))
  {
    ResetHiccups();
    return false;
  }
  
  const bool wasDisabledSinceLastCall = _reactionDisabled;
  // We must be enabled (the reactionTrigger) if this function is being called
  _reactionDisabled = false;
  
  // If our hiccups have been cured then play the appropriate get out animation
  if(_hiccupsCured != HiccupsCured::NotCured &&
     _hiccupsCured != HiccupsCured::PendingCure)
  {
    std::vector<AnimationTrigger> anim;
    if(_hiccupsCured == HiccupsCured::PlayerCured)
    {
      anim = {AnimationTrigger::HiccupPlayerCure};
    }
    else if(_hiccupsCured == HiccupsCured::SelfCured)
    {
      anim = {AnimationTrigger::HiccupSelfCure};
    }
    
    // Make sure that we only consider ourselves cured once the get out animation plays
    // Otherwise we could be cured but the player never saw the get out
    BehaviorPreReqAnimSequence req(robot, anim);
    if(behavior->IsRunnable(req))
    {
      _hiccupsCured = HiccupsCured::NotCured;
      const_cast<Robot&>(robot).GetAnimationStreamer().ResetKeepFaceAliveLastStreamTimeout();
      return true;
    }
    
    return false;
  }
  
  
  const TimeStamp_t curTime = BaseStationTimer::getInstance()->GetCurrentTimeStamp();
  if(HasHiccups())
  {
    // Hiccups can't be cured by the player in severe Need state, so cure them
    const auto expressedNeed = robot.GetAIComponent().GetWhiteboard().GetSevereNeedExpression();
    if((expressedNeed == NeedId::Energy) ||
       (expressedNeed == NeedId::Repair)){
      CureHiccups(false);
      return false;
    }
    
    _whiteboard.SetHasHiccups(true);
    
    if(curTime > _nextHiccupInBoutTime)
    {
      const u32 nextHiccupTime = _rng.RandIntInRange(_minHiccupSpacing_ms,
                                                     _maxHiccupSpacing_ms);
      _nextHiccupInBoutTime = curTime + nextHiccupTime;
      
      // If we can't hiccup right now or we were disabled then update the next hiccupInBoutTime but don't decrement
      // numHiccupsLeftInBout
      // The wasDisabledSinceLastCall is to prevent immediately hiccuping after being reenabled
      if(!CanHiccup(robot) || wasDisabledSinceLastCall)
      {
        return false;
      }
    
      // If there are no more hiccups to do then self cure
      if(_numHiccupsLeftInBout-- == 0)
      {
        // Self cure hiccups
        CureHiccups(false);
        return false;
      }
      
      // If we haven't yet broadcasted that we have the hiccups do so now
      if(!_hasBroadcasted)
      {
        _hasBroadcasted = true;
        if(_externalInterface != nullptr)
        {
          _externalInterface->Broadcast(ExternalInterface::MessageEngineToGame(ExternalInterface::RobotHiccupsChanged(true)));
        }
      }
      
      BehaviorPreReqAnimSequence req(robot, GetHiccupAnim());
      const bool isRunnable = behavior->IsRunnable(req);

      if(!isRunnable)
      {
        PRINT_NAMED_INFO("ReactionTriggerStrategyHiccup.BehaviorNotRunnable",
                         "Trying to hiccup but behavior is not runnable");
      }
      // Hiccup behavior is runnable and this is the first hiccup
      else if(_firstHiccupStartTime == 0)
      {
        _firstHiccupStartTime = curTime;
      }
      
      if (isRunnable)
      {
        NeedActionCompleted(NeedsActionId::IndividualHiccup);
      }
      
      return isRunnable;
    }
  }
  return false;
}

void ReactionTriggerStrategyHiccup::ResetHiccups()
{
  const TimeStamp_t curTime = BaseStationTimer::getInstance()->GetCurrentTimeStamp();

  _numHiccupsLeftInBout = _rng.RandIntInRange(_minNumberOfHiccupsToDo,
                                              _maxNumberOfHiccupsToDo);
  _totalHiccupsInBout = _numHiccupsLeftInBout;
  
  _shouldGetHiccupsAtTime = curTime + _rng.RandIntInRange(_minHiccupOccurrenceFrequency_ms,
                                                          _maxHiccupOccurrenceFrequency_ms);
  
  _nextHiccupInBoutTime = _shouldGetHiccupsAtTime;
  
  _firstHiccupStartTime = 0;
  
  if(DEBUG_HICCUPS)
  {
    PRINT_CH_INFO("Behaviors", "ReactionTriggerStrategyHiccup.ResetHiccups",
                  "Next bout of %u hiccups occuring in %ums",
                  _numHiccupsLeftInBout,
                  _shouldGetHiccupsAtTime - curTime);
  }
  
  // If we broadcasted that we have the hiccups then we should broadcast that we no longer have the hiccups
  // since we are reseting
  if(_hasBroadcasted)
  {
    _hasBroadcasted = false;
    
    if(_externalInterface != nullptr)
    {
      _externalInterface->Broadcast(ExternalInterface::MessageEngineToGame(ExternalInterface::RobotHiccupsChanged(false)));
    }
  }
  
  _whiteboard.SetHasHiccups(false);
}

void ReactionTriggerStrategyHiccup::ForceHiccups()
{
  // Force hiccups to occur now
  const TimeStamp_t curTime = BaseStationTimer::getInstance()->GetCurrentTimeStamp();
  _shouldGetHiccupsAtTime = curTime;
  _nextHiccupInBoutTime = _shouldGetHiccupsAtTime;
}

bool ReactionTriggerStrategyHiccup::CanHiccup(const Robot& robot) const
{
  const bool isPickingOrPlacing = robot.GetDockingComponent().IsPickingOrPlacing();
  
  if(isPickingOrPlacing)
  {
    return kCanHiccupWhileDocking;
  }
  
  return true;
}

void ReactionTriggerStrategyHiccup::CureHiccups(bool playerCured)
{
  SendDasEvent(playerCured);

  ResetHiccups();
  
  _hiccupsCured = HiccupsCured::SelfCured;
  
  // If the player cured us, add the hiccups won't occur after being cured time
  if(playerCured)
  {
    _hiccupsCured = HiccupsCured::PlayerCured;
    _shouldGetHiccupsAtTime += _hiccupsWontOccurAfterBeingCuredTime_ms;
    NeedActionCompleted(NeedsActionId::HiccupsEndGood);
  }
  else
  {
    NeedActionCompleted(NeedsActionId::HiccupsEndBad);
  }
}

void ReactionTriggerStrategyHiccup::SendDasEvent(bool playerCured)
{
  // Log an event to DAS before resetting
  const TimeStamp_t curTime = BaseStationTimer::getInstance()->GetCurrentTimeStamp();
  TimeStamp_t hiccupsDuration = curTime - _firstHiccupStartTime;

  // If for some reason we have not yet actually hiccupped but are being cured report a duration of 0
  // I don't think this is possible during normal execution but can happen when force triggering hiccups
  if(_firstHiccupStartTime == 0)
  {
    hiccupsDuration = 0;
  }

  Util::sEventF("robot.hiccups.ended",
                {{DDATA, std::to_string(hiccupsDuration).c_str()}},
                "%s", (playerCured ? "PLAYER_CURED" : "SELF_CURED"));
}

void ReactionTriggerStrategyHiccup::AlwaysHandleInternal(const EngineToGameEvent& event, const Robot& robot)
{
  if(robot.GetBehaviorManager().IsReactionTriggerEnabled(ReactionTrigger::Hiccup))
  {
    switch(event.GetData().GetTag())
    {
      case EngineToGameTag::RobotOffTreadsStateChanged:
      {
        const auto& payload = event.GetData().Get_RobotOffTreadsStateChanged();
        // If Cozmo is put on his face or back mark as a cure is pending
        // so once we get back OnTreads we can actually cure and play an animation
        if(payload.treadsState == OffTreadsState::OnFace ||
           payload.treadsState == OffTreadsState::OnBack)
        {
          if(HasHiccups() && _hiccupsCured == HiccupsCured::NotCured)
          {
            const f32 kTimeout_s = 5.f;
            const_cast<Robot&>(robot).GetAnimationStreamer().SetKeepFaceAliveLastStreamTimeout(kTimeout_s);
            _hiccupsCured = HiccupsCured::PendingCure;
          }
        }
        else if(payload.treadsState == OffTreadsState::OnTreads)
        {
          if(HasHiccups() && _hiccupsCured == HiccupsCured::PendingCure)
          {
            // Player cure
            CureHiccups(true);
          }
        }
        break;
      }
      default:
        break;
    }
  }
}
  
void ReactionTriggerStrategyHiccup::AlwaysHandleInternal(const GameToEngineEvent& event, const Robot& robot)
{
  if(robot.GetBehaviorManager().IsReactionTriggerEnabled(ReactionTrigger::Hiccup)){
    switch(event.GetData().GetTag()){
      case GameToEngineTag::NotifyOverfeedingShouldTriggerHiccups:
      {
        ForceHiccups();
        break;
      }
      default:
        break;
    }
  }else{
    PRINT_NAMED_ERROR("ReactionTriggerStrategyHiccup.AlwaysHandleInternal.OverfedButHiccupDisabled",
                      "Cozmo was overfed and should get hiccups, but hiccups are disabled");
  }
}

void ReactionTriggerStrategyHiccup::EnabledStateChanged(const Robot& robot, bool enabled)
{
  if(!enabled)
  {
    _reactionDisabled = true;
    // Hiccups can't be cured by the player in severe Need state, so cure them now
    NeedsState& currNeedState = robot.GetContext()->GetNeedsManager()->GetCurNeedsStateMutable();
    const NeedBracketId energyBracket = currNeedState.GetNeedBracket(NeedId::Energy);
    const NeedBracketId repairBracket = currNeedState.GetNeedBracket(NeedId::Repair);
    if(HasHiccups() &&
       ((energyBracket == NeedBracketId::Critical) ||
        (repairBracket == NeedBracketId::Critical))){
         CureHiccups(false);
       }
  }
}

void ReactionTriggerStrategyHiccup::ParseConfig(const Json::Value& config)
{
  bool res = true;
  
  // These are defined in seconds in json but stored as ms here
  res &= JsonTools::GetValueOptional(config, kMinHiccupOccurrenceFrequencyKey, _minHiccupOccurrenceFrequency_ms);
  res &= JsonTools::GetValueOptional(config, kMaxHiccupOccurrenceFrequencyKey, _maxHiccupOccurrenceFrequency_ms);
  _minHiccupOccurrenceFrequency_ms *= 1000;
  _maxHiccupOccurrenceFrequency_ms *= 1000;
  
  res &= JsonTools::GetValueOptional(config, kMinNumberOfHiccupsToDoKey, _minNumberOfHiccupsToDo);
  res &= JsonTools::GetValueOptional(config, kMaxNumberOfHiccupsToDoKey, _maxNumberOfHiccupsToDo);
  
  res &= JsonTools::GetValueOptional(config, kMinHiccupSpacingKey, _minHiccupSpacing_ms);
  res &= JsonTools::GetValueOptional(config, kMaxHiccupSpacingKey, _maxHiccupSpacing_ms);
  
  // Also defined in seconds in json but stored in ms
  res &= JsonTools::GetValueOptional(config,
                                     kHiccupsWontOccurAfterBeingCuredKey,
                                     _hiccupsWontOccurAfterBeingCuredTime_ms);
  _hiccupsWontOccurAfterBeingCuredTime_ms *= 1000;
  
  std::string unlockId = "";
  res &= JsonTools::GetValueOptional(config, kHiccupUnlockId, unlockId);
  _hiccupsUnlockId = UnlockIdFromString(unlockId.c_str());
  DEV_ASSERT(_hiccupsUnlockId != UnlockId::Count, "ReactionTriggerStrategyHiccup.InvalidUnlock");
  
  DEV_ASSERT(res, "ReactionTriggerStrategyHiccup.MissingParamFromJson");
}

std::vector<AnimationTrigger> ReactionTriggerStrategyHiccup::GetHiccupAnim() const
{
  // This is the first hiccup so play GetIn
  if(_numHiccupsLeftInBout == _totalHiccupsInBout-1)
  {
    return {AnimationTrigger::HiccupGetIn};
  }
  // Normal hiccup
  else
  {
    return {AnimationTrigger::Hiccup};
  }
}

bool ReactionTriggerStrategyHiccup::HasHiccups() const
{
  const TimeStamp_t curTime = BaseStationTimer::getInstance()->GetCurrentTimeStamp();
  return curTime > _shouldGetHiccupsAtTime;
}

}
}

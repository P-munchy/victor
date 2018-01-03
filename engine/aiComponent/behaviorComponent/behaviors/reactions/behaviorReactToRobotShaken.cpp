/**
 * File: behaviorReactToRobotShaken.cpp
 *
 * Author: Matt Michini
 * Created: 2017-01-11
 *
 * Description: 
 *
 * Copyright: Anki, Inc. 2017
 *
 **/


#include "engine/aiComponent/behaviorComponent/behaviors/reactions/behaviorReactToRobotShaken.h"

#include "engine/aiComponent/aiComponent.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/beiRobotInfo.h"
#include "engine/aiComponent/severeNeedsComponent.h"
#include "engine/actions/animActions.h"
#include "engine/actions/basicActions.h"

#include "anki/common/basestation/utils/timer.h"

namespace Anki {
namespace Cozmo {

namespace{  
// Accelerometer magnitude threshold corresponding to "no longer shaking"
const float kAccelMagnitudeShakingStoppedThreshold = 13000.f;
// Dizzy factor thresholds for playing the soft, medium, or hard reactions
const float kShakenDurationThresholdHard   = 5.0f;
const float kShakenDurationThresholdMedium = 2.5f;

}
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorReactToRobotShaken::BehaviorReactToRobotShaken(const Json::Value& config)
: ICozmoBehavior(config)
{
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToRobotShaken::OnBehaviorActivated(BehaviorExternalInterface& behaviorExternalInterface)
{  
  // Clear severe needs expression since eyes are being re-set
  if(behaviorExternalInterface.GetAIComponent().GetSevereNeedsComponent().HasSevereNeedExpression())
  {
    behaviorExternalInterface.GetAIComponent().GetSevereNeedsComponent().ClearSevereNeedExpression();
  }
  
  // Reset variables:
  _maxShakingAccelMag = 0.f;
  _shakingStartedTime_s = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
  _shakenDuration_s = 0.f;
  _reactionPlayed = EReaction::None;
  
  // Start the animations:
  DelegateIfInControl(new TriggerAnimationAction(AnimationTrigger::DizzyShakeLoop, 0));
  
  // Kick off the state machine:
  _state = EState::Shaking;
  
  
}

  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToRobotShaken::BehaviorUpdate(BehaviorExternalInterface& behaviorExternalInterface)
{
  if(!IsActivated()){
    return;
  }

  // Master state machine:
  switch(_state) {
    case EState::Shaking:
    {
      const auto& robotInfo = behaviorExternalInterface.GetRobotInfo();
      const float accMag = robotInfo.GetHeadAccelMagnitudeFiltered();
      _maxShakingAccelMag = std::max(_maxShakingAccelMag, accMag);
      
      // Done shaking? Then transition to the next state.
      if (accMag < kAccelMagnitudeShakingStoppedThreshold) {
        // Now that shaking has ended, determine how long it lasted:
        const float now_s = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
        _shakenDuration_s = now_s - _shakingStartedTime_s;
        _state = EState::DoneShaking;
      }
      
      break;
    }
    case EState::DoneShaking:
    {
      CancelDelegates(false);
      CompoundActionSequential* action = new CompoundActionSequential({new TriggerAnimationAction(AnimationTrigger::DizzyShakeStop),
                                                                       new TriggerAnimationAction(AnimationTrigger::DizzyStillPickedUp)});
      DelegateIfInControl(action);
      
      _state = EState::WaitTilOnTreads;
      
      break;
    }
    case EState::WaitTilOnTreads:
    {
      // Wait until on treads or the animations that are playing finish (timeout).
      if (behaviorExternalInterface.GetOffTreadsState() == OffTreadsState::OnTreads) {
        _state = EState::ActDizzy;
      } else if (!IsControlDelegated()) {
        // The "DizzyStillPickedUp" reaction played to completion, so log that as the played reaction:
        _reactionPlayed = EReaction::StillPickedUp;
        _state = EState::Finished;
      }
      
      break;
    }
    case EState::ActDizzy:
    {
      CancelDelegates(false);
      // Play appropriate reaction based on duration of shaking:
      if (_shakenDuration_s > kShakenDurationThresholdHard) {
        DelegateIfInControl(new TriggerAnimationAction(AnimationTrigger::DizzyReactionHard));
        _reactionPlayed = EReaction::Hard;
        NeedActionCompleted(NeedsActionId::DizzyHard);
      } else if (_shakenDuration_s > kShakenDurationThresholdMedium) {
        DelegateIfInControl(new TriggerAnimationAction(AnimationTrigger::DizzyReactionMedium));
        _reactionPlayed = EReaction::Medium;
        NeedActionCompleted(NeedsActionId::DizzyMedium);
      } else {
        DelegateIfInControl(new TriggerAnimationAction(AnimationTrigger::DizzyReactionSoft));
        _reactionPlayed = EReaction::Soft;
        NeedActionCompleted(NeedsActionId::DizzySoft);
      }
      
      _state = EState::Finished;
      break;
    }
    case EState::Finished:
    {
      if (!IsControlDelegated()) {
        // Done
        BehaviorObjectiveAchieved(BehaviorObjective::ReactedToRobotShaken);
        CancelSelf();
        return;
      }
    }
    default:
      break;
  }  
}
  

void BehaviorReactToRobotShaken::OnBehaviorDeactivated(BehaviorExternalInterface& behaviorExternalInterface)
{
  // Log some DAS stuff:
  const int shakenDuration_ms = std::round(_shakenDuration_s * 1000.f);
  const int maxShakenAccelMag = std::round(_maxShakingAccelMag);
  // DAS event string: "<shakenDuration_ms>:<maxShakenAccelMag>"
  const std::string& data = std::to_string(shakenDuration_ms) + ":" + std::to_string(maxShakenAccelMag);
  Anki::Util::sEvent("robot.dizzy_reaction",
                     {{DDATA, data.c_str()}},
                     EReactionToString(_reactionPlayed));
  
  // Log human-readable completion info:
  PRINT_NAMED_INFO("BehaviorReactToRobotShaken.DizzyReaction",
                   "shakenDuration = %.3fs, maxShakingAccelMag = %.1f, reactionPlayed = '%s'",
                   _shakenDuration_s,
                   _maxShakingAccelMag,
                   EReactionToString(_reactionPlayed));
}
  
  
const char* BehaviorReactToRobotShaken::EReactionToString(EReaction reaction) const
{
  switch(reaction) {
    case EReaction::None:
      return "None";
    case EReaction::Soft:
      return "Soft";
    case EReaction::Medium:
      return "Medium";
    case EReaction::Hard:
      return "Hard";
    case EReaction::StillPickedUp:
      return "StillPickedUp";
    default:
      return "";
  }
}
        
  
} // namespace Cozmo
} // namespace Anki

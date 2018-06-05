/**
 * File: behaviorReactToRobotOnSide.h
 *
 * Author: Kevin M. Karol
 * Created: 2016-07-18
 *
 * Description: Cozmo reacts to being placed on his side
 *
 * Copyright: Anki, Inc. 2016
 *
 **/

#include "engine/aiComponent/behaviorComponent/behaviors/reactions/behaviorReactToRobotOnSide.h"

#include "coretech/common/engine/utils/timer.h"
#include "engine/aiComponent/beiConditions/conditions/conditionOffTreadsState.h"
#include "engine/actions/animActions.h"
#include "engine/actions/basicActions.h"
#include "engine/externalInterface/externalInterface.h"
#include "clad/externalInterface/messageEngineToGame.h"

namespace Anki {
namespace Cozmo {
  
using namespace ExternalInterface;

static const float kWaitTimeBeforeRepeatAnim_s = 15.f;


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorReactToRobotOnSide::BehaviorReactToRobotOnSide(const Json::Value& config)
: ICozmoBehavior(config)
{
  _offTreadsConditions.emplace_back( std::make_shared<ConditionOffTreadsState>(OffTreadsState::OnLeftSide, GetDebugLabel()) );
  _offTreadsConditions.emplace_back( std::make_shared<ConditionOffTreadsState>(OffTreadsState::OnRightSide,GetDebugLabel()) );
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorReactToRobotOnSide::WantsToBeActivatedBehavior() const
{
  const bool wantsToBeActivated = std::any_of(_offTreadsConditions.begin(),
                                              _offTreadsConditions.end(),
                                              [this](const IBEIConditionPtr& condition) {
                                                return condition->AreConditionsMet(GetBEI());
                                              });
  return wantsToBeActivated;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToRobotOnSide::InitBehavior()
{
  for (auto& condition : _offTreadsConditions) {
    condition->Init(GetBEI());
  }
}
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToRobotOnSide::OnBehaviorEnteredActivatableScope() {
  for (auto& condition : _offTreadsConditions) {
    condition->SetActive(GetBEI(), true);
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToRobotOnSide::OnBehaviorLeftActivatableScope()
{
  for (auto& condition : _offTreadsConditions) {
    condition->SetActive(GetBEI(), false);
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToRobotOnSide::OnBehaviorActivated()
{
  // clear bored animation timer
  _timeToPerformBoredAnim_s = -1.0f;
  
  ReactToBeingOnSide();
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToRobotOnSide::ReactToBeingOnSide()
{
  AnimationTrigger anim = AnimationTrigger::Count;
  
  if( GetBEI().GetOffTreadsState() == OffTreadsState::OnLeftSide){
    anim = AnimationTrigger::ReactToOnLeftSide;
  }
  
  if(GetBEI().GetOffTreadsState() == OffTreadsState::OnRightSide) {
    anim = AnimationTrigger::ReactToOnRightSide;
  }
  
  if(anim != AnimationTrigger::Count){
    DelegateIfInControl(new TriggerAnimationAction(anim),
                        &BehaviorReactToRobotOnSide::AskToBeRighted);
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToRobotOnSide::AskToBeRighted()
{
  AnimationTrigger anim = AnimationTrigger::Count;
  
  if( GetBEI().GetOffTreadsState() == OffTreadsState::OnLeftSide){
    anim = AnimationTrigger::DEPRECATED_AskToBeRightedLeft;
  }
  
  if(GetBEI().GetOffTreadsState() == OffTreadsState::OnRightSide) {
    anim = AnimationTrigger::DEPRECATED_AskToBeRightedRight;
  }
  
  if(anim != AnimationTrigger::Count){
    DelegateIfInControl(new TriggerAnimationAction(anim),
                &BehaviorReactToRobotOnSide::HoldingLoop);
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToRobotOnSide::HoldingLoop()
{
  if( GetBEI().GetOffTreadsState() == OffTreadsState::OnRightSide
     || GetBEI().GetOffTreadsState() == OffTreadsState::OnLeftSide) {

    const float currTime_s = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
    
    if( _timeToPerformBoredAnim_s < 0.0f ) {
      // set timer for when to perform the bored animations
      _timeToPerformBoredAnim_s = currTime_s + kWaitTimeBeforeRepeatAnim_s;
    }
    
    if( currTime_s >= _timeToPerformBoredAnim_s ) {
      // reset timer
      _timeToPerformBoredAnim_s = -1.0f;

      // play bored animation sequence, then return to holding

      // note: NothingToDoBored anims can move the robot, so Intro/Outro may not work here well, should
      // we be playing a specific loop here?
      DelegateIfInControl(new CompoundActionSequential({
                    new TriggerAnimationAction(AnimationTrigger::DEPRECATED_NothingToDoBoredIntro),
                    new TriggerAnimationAction(AnimationTrigger::DEPRECATED_NothingToDoBoredEvent),
                    new TriggerAnimationAction(AnimationTrigger::DEPRECATED_NothingToDoBoredOutro) }),
                  &BehaviorReactToRobotOnSide::HoldingLoop);
    }
    else {
      // otherwise, we just loop this animation
      DelegateIfInControl(new TriggerAnimationAction(AnimationTrigger::WaitOnSideLoop),
                  &BehaviorReactToRobotOnSide::HoldingLoop);
    }
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToRobotOnSide::OnBehaviorDeactivated()
{
}

} // namespace Cozmo
} // namespace Anki

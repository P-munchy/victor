/**
 * File: behaviorSleeping.cpp
 *
 * Author: Brad Neuman
 * Created: 2017-11-01
 *
 * Description: State to nap and stir occasionally in sleep
 *
 * Copyright: Anki, Inc. 2017
 *
 **/

#include "engine/aiComponent/behaviorComponent/behaviors/sleeping/behaviorSleeping.h"

#include "coretech/common/engine/utils/timer.h"
#include "engine/actions/animActions.h"
#include "engine/actions/basicActions.h"
#include "util/console/consoleInterface.h"

namespace Anki {
namespace Cozmo {

namespace {

#define CONSOLE_GROUP "Sleeping"

CONSOLE_VAR_RANGED(f32, kSleepingStirSpacing_min_s, CONSOLE_GROUP, 20.0f, 0.0f, 7200.0f);
CONSOLE_VAR_RANGED(f32, kSleepingStirSpacing_max_s, CONSOLE_GROUP, 40.0f, 0.0f, 7200.0f);

CONSOLE_VAR_RANGED(f32, kSleepingBoutSpacing_min_s, CONSOLE_GROUP, 1.5f, 0.0f, 30.0f);
CONSOLE_VAR_RANGED(f32, kSleepingBoutSpacing_max_s, CONSOLE_GROUP, 5.0f, 0.0f, 7200.0f);

// note: the anim group includes a very subtle animation and a more noticeable one, so these numbers should be
// a bit higher than they might otherwise be
CONSOLE_VAR_RANGED(u32, kSleepingBoutNumStirs_min, CONSOLE_GROUP, 5, 1, 10);
CONSOLE_VAR_RANGED(u32, kSleepingBoutNumStirs_max, CONSOLE_GROUP, 10, 1, 10);

constexpr const char* kEnablePowerSaveKey = "enablePowerSave";

}

BehaviorSleeping::BehaviorSleeping(const Json::Value& config)
  : ICozmoBehavior(config)
{
  // defaults to true
  _shouldEnterPowerSave = config.get(kEnablePowerSaveKey, true).asBool();
}

void BehaviorSleeping::GetBehaviorJsonKeys(std::set<const char*>& expectedKeys) const
{
  expectedKeys.insert(kEnablePowerSaveKey);
}

bool BehaviorSleeping::CanBeGentlyInterruptedNow() const
{
  return !_animIsPlaying;
}

void BehaviorSleeping::OnBehaviorActivated()
{
  _animIsPlaying = false;

  if( _shouldEnterPowerSave ) {
    SmartRequestPowerSaveMode();
  }

  SmartDisableKeepFaceAlive();

  // always start with one round of sleeping to make sure the face is in a good state
  DelegateIfInControl(new TriggerAnimationAction(AnimationTrigger::GoToSleepSleeping),
                      &BehaviorSleeping::TransitionToSleeping);
}
  
void BehaviorSleeping::OnBehaviorDeactivated()
{
  PlayEmergencyGetOut(AnimationTrigger::WakeupGetout);
}

void BehaviorSleeping::TransitionToSleeping()
{
  SetDebugStateName("sleeping");
  
  _numRemainingInBout = GetRNG().RandIntInRange(kSleepingBoutNumStirs_min, kSleepingBoutNumStirs_max);
  
  const float waitTime_s = GetRNG().RandDblInRange(kSleepingStirSpacing_min_s, kSleepingStirSpacing_max_s);
  HoldFaceForTime(waitTime_s, &BehaviorSleeping::TransitionToBoutOfStirring);
}

void BehaviorSleeping::TransitionToBoutOfStirring()
{
  SetDebugStateName("inBout");

  _animIsPlaying = false;

  if( _numRemainingInBout-- >= 0 ) {
    // start bout (wait first, then animate)    
    const float waitTime_s = GetRNG().RandDblInRange(kSleepingBoutSpacing_min_s, kSleepingBoutSpacing_max_s);
    HoldFaceForTime(waitTime_s, &BehaviorSleeping::TransitionToPlayStirAnim);
  }
  else {
    // back to sleep
    TransitionToSleeping();
  }

}

void BehaviorSleeping::TransitionToPlayStirAnim()
{
  SetDebugStateName("stirring");
  _animIsPlaying = true;

  DelegateIfInControl(new TriggerAnimationAction(AnimationTrigger::GoToSleepSleeping),
                      &BehaviorSleeping::TransitionToBoutOfStirring);  
}


void BehaviorSleeping::HoldFaceForTime(
  const float waitTime_s,
  void(BehaviorSleeping::*callback)())
{
  DelegateIfInControl(new WaitAction(waitTime_s), callback);
}

}
}

/**
 * File: BehaviorSleepCycle.h
 *
 * Author: Brad
 * Created: 2018-08-13
 *
 * Description: Top level behavior to coordinate sleep / wake cycles of the robot
 *
 * Copyright: Anki, Inc. 2018
 *
 **/

#ifndef __Engine_AiComponent_BehaviorComponent_Behaviors_BehaviorSleepCycle__
#define __Engine_AiComponent_BehaviorComponent_Behaviors_BehaviorSleepCycle__
#pragma once

#include "engine/aiComponent/behaviorComponent/behaviors/iCozmoBehavior.h"

#include "coretech/common/engine/robotTimeStamp.h"

#include "clad/types/behaviorComponent/sleepingTypes.h"

namespace Anki {
namespace Vector {

class BehaviorSleepCycle : public ICozmoBehavior
{
public:
  virtual ~BehaviorSleepCycle();

protected:

  // Enforce creation through BehaviorFactory
  friend class BehaviorFactory;
  explicit BehaviorSleepCycle(const Json::Value& config);

  virtual void InitBehavior() override;
  virtual void GetBehaviorOperationModifiers(BehaviorOperationModifiers& modifiers) const override;
  virtual void GetAllDelegates(std::set<IBehavior*>& delegates) const override;
  virtual void GetBehaviorJsonKeys(std::set<const char*>& expectedKeys) const override;

  virtual bool WantsToBeActivatedBehavior() const override;
  virtual void OnBehaviorActivated() override;
  virtual void OnBehaviorDeactivated() override;
  virtual void BehaviorUpdate() override;


private:

  class SleepState;

  void ParseWakeReasonConditions(const Json::Value& config);
  void CreateCustomWakeReasonConditions();
  void CheckWakeReasonConfig();
  void ParseWakeReasons(const Json::Value& config);

  // transition to being awake, for the given reason (reason sent to DAS)
  void WakeUp(const WakeReason& reason, bool playWakeUp = true);

  void TransitionToCharger();
  void TransitionToCheckingForPerson();
  void TransitionToComatose();
  void TransitionToDeepSleep();
  void TransitionToLightSleep();

  void RespondToPersonCheck();

  void TransitionToSayingGoodnight();

  // simple helper to delegate to light or deep depending on time of day
  void TransitionToLightOrDeepSleep();

  bool GoToSleepIfNeeded();

  void SendToGoSleepDasEvent(const SleepReason& reason);

  void SleepIfInControl(bool playGetIn = true);

  bool WakeIfNeeded(const WakeReason& forReason);

  void SetConditionsActiveForState(SleepStateID state, bool active);

  void SetState(SleepStateID state);

  static bool ShouldReactToSoundInState(const SleepStateID& state);

  bool ShouldWiggleOntoChargerFromSleep();

  void PopulateWebVizJson(Json::Value& data) const;

  struct InstanceConfig {
    std::string awakeDelegateName;
    std::string findChargerBehaviorName;

    ICozmoBehaviorPtr awakeDelegate;
    ICozmoBehaviorPtr goToSleepBehavior;
    ICozmoBehaviorPtr asleepBehavior;
    ICozmoBehaviorPtr wakeUpBehavior;
    ICozmoBehaviorPtr personCheckBehavior;
    ICozmoBehaviorPtr findChargerBehavior;
    ICozmoBehaviorPtr sleepingSoundReactionBehavior;
    ICozmoBehaviorPtr sleepingWakeWordBehavior;
    ICozmoBehaviorPtr wiggleBackOntoChargerBehavior;

    std::map< WakeReason, IBEIConditionPtr > wakeConditions;

    std::vector< WakeReason > alwaysWakeReasons;
    std::map< SleepStateID, std::vector< WakeReason > > wakeReasonsPerState;
  };

  enum class SleepReactionType : uint8_t {
    None = 0,
    Sound,
    TriggerWord,
    WiggleOntoCharger
  };

  struct DynamicVariables {
    DynamicVariables() = default;
    SleepStateID currState = SleepStateID::Awake;
    RobotTimeStamp_t personCheckStartTimestamp = 0;
    float nextPersonCheckTime_s = -1.0f;
    float lastWakeUpTime_s = -1.0f;
    float comatoseStartTime_s = -1.0f;

    SleepReactionType reactionState = SleepReactionType::None;
    bool wasOnChargerContacts = false;

#if ANKI_DEV_CHEATS
    WakeReason lastWakeReason = WakeReason::Invalid;
    SleepReason lastSleepReason = SleepReason::Invalid;
#endif
  };

  InstanceConfig _iConfig;
  DynamicVariables _dVars;

  // for webviz debug subscriptions
  std::vector<::Signal::SmartHandle> _eventHandles;

};

} // namespace Vector
} // namespace Anki

#endif // __Engine_AiComponent_BehaviorComponent_Behaviors_BehaviorSleepCycle__

/**
 * File: BehaviorAlexa.h
 *
 * Author: ross
 * Created: 2018-10-05
 *
 * Description: plays animations for various alexa UX states
 *
 * Copyright: Anki, Inc. 2018
 *
 **/

#ifndef __Engine_AiComponent_BehaviorComponent_Behaviors_BehaviorAlexa__
#define __Engine_AiComponent_BehaviorComponent_Behaviors_BehaviorAlexa__
#pragma once

#include "engine/aiComponent/behaviorComponent/behaviors/iCozmoBehavior.h"
#include "clad/types/alexaUXState.h"

namespace Anki {
namespace Vector {

class BehaviorAlexa : public ICozmoBehavior
{
public: 
  virtual ~BehaviorAlexa();

protected:

  // Enforce creation through BehaviorFactory
  friend class BehaviorFactory;
  explicit BehaviorAlexa(const Json::Value& config);  

  virtual void GetBehaviorOperationModifiers(BehaviorOperationModifiers& modifiers) const override;
  virtual void GetAllDelegates(std::set<IBehavior*>& delegates) const override;
  virtual void GetBehaviorJsonKeys(std::set<const char*>& expectedKeys) const override;
  
  virtual bool WantsToBeActivatedBehavior() const override;
  virtual void OnBehaviorActivated() override;
  virtual void BehaviorUpdate() override;
  
  virtual void InitBehavior() override;
  
  virtual void OnBehaviorEnteredActivatableScope() override;
  
  
  virtual void AlwaysHandleInScope(const RobotToEngineEvent& event) override;

private:
  
  // listening get-in is handled by anim process
  void TransitionToListeningLoop();
  void TransitionToListeningGetOut();
  
  void TransitionToSpeakingGetIn();
  void TransitionToSpeakingLoop();
  void TransitionToSpeakingGetOut();
  
  void TransitionFromListeningToSpeaking();
  void TransitionFromSpeakingToListening();
  
  
  void CheckForExit();
  
  enum class State : uint8_t {
    ListeningGetIn,
    ListeningLoop,
    ListeningGetOut,
    
    // speaking or audio playing
    SpeakingGetIn, // from nothing, e.g., when an alert/notification goes off
    SpeakingLoop,
    SpeakingGetOut,
    
    ListeningToSpeaking,
    SpeakingToListening,
    
    TransitioningToWeather,
  };

  struct InstanceConfig {
    InstanceConfig();
    ICozmoBehaviorPtr dttb;
    ICozmoBehaviorPtr weatherBehavior;
  };

  struct DynamicVariables {
    DynamicVariables();
    // note: if the robot starts and there's an alarm pending, it will sometimes start with it "speaking"
    // instead of idle. that will mess everything up
    AlexaUXState uxState = AlexaUXState::Idle;
    State state = State::ListeningGetIn;
    
    int shouldExitForWeatherWithinTicks = 0;
    
    int lastReceivedSpeak = 0;
    int lastReceivedWeather = 0;
    float nextTimeCanDance_s = 0.0f;
  };

  InstanceConfig _iConfig;
  DynamicVariables _dVars;
  
  
  
};

} // namespace Vector
} // namespace Anki

#endif // __Engine_AiComponent_BehaviorComponent_Behaviors_BehaviorAlexa__

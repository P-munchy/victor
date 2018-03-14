/**
* File: behaviorReactToVoiceCommand.h
*
* Author: Lee Crippen
* Created: 2/16/2017
*
* Description: Simple behavior to immediately respond to the voice command keyphrase, while waiting for further commands.
*
* Copyright: Anki, Inc. 2017
*
**/

#ifndef __Cozmo_Basestation_Behaviors_BehaviorReactToVoiceCommand_H__
#define __Cozmo_Basestation_Behaviors_BehaviorReactToVoiceCommand_H__

#include "engine/aiComponent/behaviorComponent/behaviors/iCozmoBehavior.h"
#include "engine/components/bodyLightComponentTypes.h"
#include "engine/micDirectionTypes.h"


namespace Anki {
namespace Cozmo {

class BehaviorReactToMicDirection;
  
class BehaviorReactToVoiceCommand : public ICozmoBehavior
{
private:
  using super = ICozmoBehavior;

  friend class BehaviorContainer;
  friend class BehaviorFactory;
  BehaviorReactToVoiceCommand(const Json::Value& config);
  
public:
  virtual bool WantsToBeActivatedBehavior() const override;
  virtual void GetBehaviorOperationModifiers( BehaviorOperationModifiers& modifiers ) const override;
  
  // Empty override of AddListener because the strategy that controls this behavior is a listener
  // The strategy controls multiple different behaviors and listeners are necessary for the other behaviors
  // since they are generic PlayAnim behaviors (reactToVoiceCommand_Wakeup)
  virtual void AddListener(ISubtaskListener* listener) override {};


protected:

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  enum class EState : uint8_t
  {
    Positioning,
    Listening,
    Thinking,
    IntentReceived,
  };

  enum class EIntentStatus : uint8_t
  {
    IntentHeard,
    IntentUnknown,
    NoIntentHeard,
  };

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

  // specific default values can be used to easily set all of our different
  // playtest options.  "Lee Happiness" refers to how happy/sad each of the
  // different settings make Lee feel ... more noise == Lee sad
  void LoadLeeHappinessValues( const Json::Value& config );
  
  virtual void InitBehavior() override;
  virtual void GetAllDelegates( std::set<IBehavior*>& delegates ) const override;
  virtual void AlwaysHandleInScope( const RobotToEngineEvent& event ) override;

  virtual void OnBehaviorActivated() override;
  virtual void OnBehaviorDeactivated() override;
  virtual void BehaviorUpdate() override;

  // reaction direction functions ...

  // cache the direction we want to react to
  void ComputeReactionDirection();
  // get the direction we want to react to
  MicDirectionIndex GetReactionDirection() const;
  // get the "selected direction" from the mic history
  // this should be the "locked direction" upon trigger word detected
  MicDirectionIndex GetSelectedDirectionFromMicHistory() const;
  
  void SetUserIntentStatus();

  // state / transition functions
  void StartListening();
  void StopListening();

  void TransitionToThinking();
  void TransitionToIntentReceived();

  // coincide with the begin/end of the anim process recording the intent audio
  void OnStreamingBegin();
  void OnStreamingEnd();

  // this is the state when victor is "listening" for the users intent
  // and should therefore cue the user to speak
  void OnVictorListeningBegin();
  void OnVictorListeningEnd();

private:

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  struct InstanceConfig
  {
    InstanceConfig();

    bool earConBegin; // earcon is an audible cue to tell the user victor is listening
    bool earConEnd;
    bool turnOnTrigger; // do we turn to the user when we hear the trigger word
    bool turnOnIntent; // do we turn to the user when we hear the intent
    bool playListeningGetInAnim; // do we want to play the get-in to listening loop
    bool exitOnIntents; // do we bail as soon as we have an intent from the cloud

    bool backpackLights;

    // response behavior to hearing the trigger word (or intent)
    std::string reactionBehaviorString;
    std::shared_ptr<BehaviorReactToMicDirection> reactionBehavior;

  } _instanceVars;

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  struct DynamicVariables
  {
    DynamicVariables();

    EState                    state;
    MicDirectionIndex         reactionDirection;
    BackpackLightDataLocator  lightsHandle;
    float                     streamingBeginTime;
    EIntentStatus             intentStatus;

  } _dynamicVars;

  // these are dynamic vars that live beyond the activation scope ...
  MicDirectionIndex         _triggerDirection;

}; // class BehaviorReactToVoiceCommand

} // namespace Cozmo
} // namespace Anki

#endif // __Cozmo_Basestation_Behaviors_BehaviorReactToVoiceCommand_H__

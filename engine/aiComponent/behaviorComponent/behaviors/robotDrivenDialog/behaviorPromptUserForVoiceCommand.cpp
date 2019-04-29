/**
 * File: BehaviorPromptUserForVoiceCommand.cpp
 *
 * Author: Sam Russell
 * Created: 2018-04-30
 *
 * Description: This behavior prompts the user for a voice command, then puts Victor into "wake-wordless streaming".
 *              To keep the prompt behavior simple, resultant UserIntents should be handled by the delegating behavior,
 *              or elsewhere in the behaviorStack.
 *
 * Copyright: Anki, Inc. 2018
 *
 **/

#include "engine/aiComponent/behaviorComponent/behaviors/robotDrivenDialog/behaviorPromptUserForVoiceCommand.h"

#include "audioEngine/multiplexer/audioCladMessageHelper.h"
#include "coretech/common/engine/jsonTools.h"
#include "coretech/common/engine/utils/timer.h"
#include "engine/actions/animActions.h"
#include "engine/actions/basicActions.h"
#include "engine/aiComponent/behaviorComponent/behaviorContainer.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/behaviorExternalInterface.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/beiRobotInfo.h"
#include "engine/aiComponent/behaviorComponent/behaviors/animationWrappers/behaviorTextToSpeechLoop.h"
#include "engine/aiComponent/behaviorComponent/userIntentComponent.h"
#include "engine/audio/engineRobotAudioClient.h"
#include "engine/components/localeComponent.h"
#include "engine/components/mics/micComponent.h"
#include "micDataTypes.h"
#include "util/cladHelpers/cladFromJSONHelpers.h"


namespace Anki {
namespace Vector {

namespace {
  const char* kDefaultTTSBehaviorID = "DefaultTextToSpeechLoop";

  // JSON keys
  const char* kStreamType                         = "streamType";
  const char* kEarConSuccess                      = "earConAudioEventSuccess";
  const char* kEarConFail                         = "earConAudioEventNeutral";
  // TODO:(str) Currently not in use. Rework this to use a smarter TurnToFace structure, perhaps LookAtFaceInFront
  const char* kShouldTurnToFaceKey                = "shouldTurnToFaceBeforePrompting";
  const char* kTextToSpeechBehaviorKey            = "textToSpeechBehaviorID";
  const char* kStopListeningOnIntentsKey          = "stopListeningOnIntents";
  const char* kPlayListeningGetInKey              = "playListeningGetIn";
  const char* kPlayListeningGetOutKey             = "playListeningGetOut";
  const char* kMaxRepromptKey                     = "maxNumberOfReprompts";

  // Configurable localization keys
  const char* kVocalPromptKey                     = "vocalPromptKey";
  const char* kVocalResponseToIntentKey           = "vocalResponseToIntentKey";
  const char* kVocalResponseToBadIntentKey        = "vocalResponseToBadIntentKey";
  const char* kVocalRepromptKey                   = "vocalRepromptKey";

  constexpr float kMaxRecordTime_s                = 10.0f; // matches timeouts for TriggerWord and KnowledgeGraph

  static_assert( kMaxRecordTime_s >= ( ( MicData::kStreamingTimeout_ms + 2000 ) / 1000.f ),
                 "kMaxRecordTime_s should be >= kStreamingTimeout_ms by about 2 seconds to give chipper time to respond" );

  // when we heard something but don't have a matching intent, do we want to stop immediately or wait for animation timeout?
  const bool kStopListeningOnUnknownIntent        = false;

  const std::string empty = "";
}

#define SET_STATE(s) do{ \
                          _dVars.state = EState::s; \
                          PRINT_CH_INFO("Behaviors", "BehaviorPromptUserForVoiceCommand.State", "State = %s", #s); \
                        } while(0);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorPromptUserForVoiceCommand::InstanceConfig::InstanceConfig()
: streamType( CloudMic::StreamType::Normal )
, earConSuccess( AudioMetaData::GameEvent::GenericEvent::Invalid )
, earConFail( AudioMetaData::GameEvent::GenericEvent::Invalid )
, ttsBehaviorID(kDefaultTTSBehaviorID)
, ttsBehavior(nullptr)
, maxNumReprompts(0)
, shouldTurnToFace(false)
, stopListeningOnIntents(true)
, backpackLights(true)
, playListeningGetIn(true)
, playListeningGetOut(true)
{

}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorPromptUserForVoiceCommand::DynamicVariables::DynamicVariables()
: state(EState::TurnToFace)
, intentStatus(EIntentStatus::NoIntentHeard)
, repromptCount(0)
{

}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorPromptUserForVoiceCommand::BehaviorPromptUserForVoiceCommand(const Json::Value& config)
: ICozmoBehavior(config)
{
  // we must have a stream type supplied, else the cloud doesn't know what to do with it
  // * defaults don't make much sense at this point either
  const std::string streamTypeString = JsonTools::ParseString(config,
                                                              kStreamType,
                                                              "BehaviorPromptUserForVoiceCommand.MissingStreamType");
  _iConfig.streamType = CloudMic::StreamTypeFromString( streamTypeString );

  // ear-con vars
  {
    std::string earConString;
    if(JsonTools::GetValueOptional(config, kEarConSuccess, earConString)){
      _iConfig.earConSuccess = AudioMetaData::GameEvent::GenericEventFromString(earConString);
    }

    if(JsonTools::GetValueOptional(config, kEarConFail, earConString)){
      _iConfig.earConFail = AudioMetaData::GameEvent::GenericEventFromString(earConString);
    }
  }

  JsonTools::GetValueOptional(config, kShouldTurnToFaceKey, _iConfig.shouldTurnToFace);

  // Set up the TextToSpeech Behavior
  JsonTools::GetValueOptional(config, kTextToSpeechBehaviorKey, _iConfig.ttsBehaviorID);

  // Configurable localization keys
  JsonTools::GetValueOptional(config, kVocalPromptKey, _iConfig.vocalPromptKey);
  JsonTools::GetValueOptional(config, kVocalResponseToIntentKey, _iConfig.vocalResponseToIntentKey);
  JsonTools::GetValueOptional(config, kVocalResponseToBadIntentKey, _iConfig.vocalResponseToBadIntentKey);
  JsonTools::GetValueOptional(config, kVocalRepromptKey, _iConfig.vocalRepromptKey);

  // Should we exit the behavior as soon as an intent is pending, or finish what we're doing first?
  JsonTools::GetValueOptional(config, kStopListeningOnIntentsKey, _iConfig.stopListeningOnIntents);
  // play the getIn animation for the listening anim?
  JsonTools::GetValueOptional(config, kPlayListeningGetInKey, _iConfig.playListeningGetIn);
  // play the getOut animation for the listening anim?
  JsonTools::GetValueOptional(config, kPlayListeningGetOutKey, _iConfig.playListeningGetOut);
  // Should we repeat the prompt if it fails? If so, how many times
  JsonTools::GetValueOptional(config, kMaxRepromptKey, _iConfig.maxNumReprompts);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorPromptUserForVoiceCommand::~BehaviorPromptUserForVoiceCommand()
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorPromptUserForVoiceCommand::WantsToBeActivatedBehavior() const
{
  const bool hasPrompt = !GetVocalPromptString().empty();

  return hasPrompt;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPromptUserForVoiceCommand::GetAllDelegates(std::set<IBehavior*>& delegates) const
{
  delegates.insert(_iConfig.ttsBehavior.get());
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPromptUserForVoiceCommand::GetBehaviorOperationModifiers(BehaviorOperationModifiers& modifiers) const
{
  modifiers.wantsToBeActivatedWhenCarryingObject = true;
  modifiers.wantsToBeActivatedWhenOffTreads = true;
  modifiers.wantsToBeActivatedWhenOnCharger = true;
  modifiers.behaviorAlwaysDelegates = false;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPromptUserForVoiceCommand::GetBehaviorJsonKeys(std::set<const char*>& expectedKeys) const
{
  const char* list[] = {
    kEarConSuccess,
    kEarConFail,
    kShouldTurnToFaceKey,
    kTextToSpeechBehaviorKey,
    kVocalPromptKey,
    kVocalResponseToIntentKey,
    kVocalResponseToBadIntentKey,
    kVocalRepromptKey,
    kStopListeningOnIntentsKey,
    kMaxRepromptKey,
    kPlayListeningGetInKey,
    kPlayListeningGetOutKey,
    kStreamType
  };

  expectedKeys.insert( std::begin(list), std::end(list) );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPromptUserForVoiceCommand::SetPromptString(const std::string &text)
{
  _dVars.useDynamicPromptString = true;
  _dVars.dynamicPromptString = text;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPromptUserForVoiceCommand::SetRepromptString(const std::string &text)
{
  _dVars.useDynamicRepromptString = true;
  _dVars.dynamicRepromptString = text;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPromptUserForVoiceCommand::InitBehavior(){
  BehaviorID ttsID = BehaviorTypesWrapper::BehaviorIDFromString(_iConfig.ttsBehaviorID);
  GetBEI().GetBehaviorContainer().FindBehaviorByIDAndDowncast(ttsID,
                                                              BEHAVIOR_CLASS(TextToSpeechLoop),
                                                              _iConfig.ttsBehavior);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPromptUserForVoiceCommand::OnBehaviorActivated()
{
  // _dVars are reset on deactivation so that the effects of SetPrompt/SetReprompt persist

  // Configure streaming params with defaults in case they're not set due to behaviorStack state
  SmartPushResponseToTriggerWord( "default" );

  if(_iConfig.shouldTurnToFace){
    TransitionToTurnToFace();
  } else {
    TransitionToPrompting();
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPromptUserForVoiceCommand::OnBehaviorDeactivated()
{
  // Any resultant intents should be handled by external behaviors or transitions, let 'em roll
  GetBehaviorComp<UserIntentComponent>().SetUserIntentTimeoutEnabled(true);

  // reset dynamic variables
  _dVars = DynamicVariables();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPromptUserForVoiceCommand::BehaviorUpdate()
{
  if( !IsActivated() ) {
    return;
  }

  if(EState::Listening == _dVars.state){
    if(!IsControlDelegated()){
      bool waitingOnGetIn = _iConfig.playListeningGetIn &&
                            GetBehaviorComp<UserIntentComponent>().WaitingForTriggerWordGetInToFinish();
      if(!waitingOnGetIn){
        DelegateIfInControl(new ReselectingLoopAnimationAction(AnimationTrigger::VC_ListeningLoop,
                                                               0, true, (uint8_t)AnimTrackFlag::NO_TRACKS,
                                                               std::max(kMaxRecordTime_s, 1.0f)),
                            &BehaviorPromptUserForVoiceCommand::TransitionToThinking);
      }
    }

    CheckForPendingIntents();
    if(_iConfig.stopListeningOnIntents){
      const bool intentHeard = (EIntentStatus::IntentHeard == _dVars.intentStatus);
      const bool intentUnknown = (EIntentStatus::IntentUnknown == _dVars.intentStatus) && kStopListeningOnUnknownIntent;
      const bool intentSilence = (EIntentStatus::IntentSilence == _dVars.intentStatus);
      if(intentHeard || intentUnknown || intentSilence){
        // End the listening anim, which should push us into Thinking
        CancelDelegates(false);
        TransitionToThinking();
      }
    }
  } else if(EState::Thinking == _dVars.state){
    CheckForPendingIntents();
  }

}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPromptUserForVoiceCommand::CheckForPendingIntents()
{
  UserIntentComponent& uic = GetBehaviorComp<UserIntentComponent>();
  if ((EIntentStatus::NoIntentHeard == _dVars.intentStatus) && uic.IsAnyUserIntentPending()) {

    // Don't dismiss unclaimed intents until this behavior exits, or other behaviors may miss their
    // chance to claim the pending intents
    uic.SetUserIntentTimeoutEnabled(false);

    _dVars.intentStatus = EIntentStatus::IntentHeard;

    // If robot heard an unmatched intent, note it so we can respond appropriately, then clear it.
    static const UserIntentTag unmatched = USER_INTENT(unmatched_intent);
    if (uic.IsUserIntentPending(unmatched)) {
      uic.DropUserIntent(unmatched);
      _dVars.intentStatus = EIntentStatus::IntentUnknown;
      return;
    }

    // If robot heard silence, record the outcome for proper handling, then clear it.
    static const UserIntentTag silence = USER_INTENT(silence);
    if (uic.IsUserIntentPending(silence)) {
      uic.DropUserIntent(silence);
      _dVars.intentStatus = EIntentStatus::IntentSilence;
      return;
    }

  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPromptUserForVoiceCommand::TransitionToTurnToFace()
{
  SET_STATE(TurnToFace);
  DelegateIfInControl(new TurnTowardsLastFacePoseAction(), &BehaviorPromptUserForVoiceCommand::TransitionToPrompting);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPromptUserForVoiceCommand::TransitionToPrompting()
{
  SET_STATE(Prompting);
  _iConfig.ttsBehavior->SetTextToSay( GetVocalPromptString() );
  if(_iConfig.ttsBehavior->WantsToBeActivated()){
    DelegateIfInControl(_iConfig.ttsBehavior.get(), &BehaviorPromptUserForVoiceCommand::TransitionToListening);
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPromptUserForVoiceCommand::TransitionToListening()
{
  SET_STATE(Listening);
  GetBehaviorComp<UserIntentComponent>().StartWakeWordlessStreaming(_iConfig.streamType, _iConfig.playListeningGetIn);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPromptUserForVoiceCommand::TransitionToThinking()
{
  SET_STATE(Thinking);

  // Play the Listening getOut, then close out the streaming stuff in case an intent was just a little late
  auto callback = [this](){
    // Play "earCon end"
    Audio::EngineRobotAudioClient& rac = GetBEI().GetRobotAudioClient();

    if(EIntentStatus::IntentHeard == _dVars.intentStatus){
      if(AudioMetaData::GameEvent::GenericEvent::Invalid != _iConfig.earConSuccess){
        rac.PostEvent(_iConfig.earConSuccess, AudioMetaData::GameObjectType::Behavior);
      }
    } else {
      if(AudioMetaData::GameEvent::GenericEvent::Invalid != _iConfig.earConFail){
        rac.PostEvent(_iConfig.earConFail, AudioMetaData::GameObjectType::Behavior);
      }
    }

    TransitionToIntentReceived();
  };

  if(_iConfig.playListeningGetOut){
    DelegateIfInControl(new TriggerAnimationAction(AnimationTrigger::VC_ListeningGetOut), callback);
  } else {
    callback();
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPromptUserForVoiceCommand::TransitionToIntentReceived()
{
  // Two ways we make it all the way here:
  //  1. Any resultant intent is handled by a non-interrupting behavior && !ExitOnIntents
  //  2. Any resultant intents have gone unclaimed

  SET_STATE(Thinking)

  // Play intent response anim and voice, if set
  if(EIntentStatus::IntentHeard == _dVars.intentStatus) {
    const auto & vocalResponseToIntentString = GetVocalResponseToIntentString();
    if (vocalResponseToIntentString.empty()) {
      // No prompts specified, exit so the intent can be handled elsewhere
      CancelSelf();
      return;
    } else {
      _iConfig.ttsBehavior->SetTextToSay(vocalResponseToIntentString);
      if (_iConfig.ttsBehavior->WantsToBeActivated()) {
        DelegateIfInControl(_iConfig.ttsBehavior.get(), [this](){ CancelSelf(); });
      }
    }
  } else if(EIntentStatus::IntentSilence == _dVars.intentStatus) {
    TransitionToReprompt();
  } else {
    const auto & vocalResponseToBadIntentString = GetVocalResponseToBadIntentString();
    if (vocalResponseToBadIntentString.empty()) {
      // No prompts specified, either reprompt or exit
      TransitionToReprompt();
      return;
    } else {
      _iConfig.ttsBehavior->SetTextToSay(vocalResponseToBadIntentString);
      if(_iConfig.ttsBehavior->WantsToBeActivated()){
        DelegateIfInControl(_iConfig.ttsBehavior.get(), &BehaviorPromptUserForVoiceCommand::TransitionToReprompt);
      }
    }
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorPromptUserForVoiceCommand::TransitionToReprompt()
{
  if(_dVars.repromptCount < _iConfig.maxNumReprompts){
    _dVars.repromptCount++;

    // Reset necessary vars
    _dVars.intentStatus = EIntentStatus::NoIntentHeard;

    const auto & vocalRepromptString = GetVocalRepromptString();
    if (vocalRepromptString.empty()) {
      // If we don't have any Reprompt anims or vocalizations, just reuse the prompting state
      PRINT_CH_INFO("Behaviors", "BehaviorPromptUserForVoiceCommand.RepromptGeneric",
                       "Reprompting user %d of %d times with original prompt action",
                       _dVars.repromptCount,
                       _iConfig.maxNumReprompts);
      TransitionToPrompting();
      return;
    } else {
      PRINT_CH_INFO("Behaviors", "BehaviorPromptUserForVoiceCommand.RepromptSpecialized",
                      "Reprompting user %d of %d times with specialized reprompt action.",
                      _dVars.repromptCount,
                      _iConfig.maxNumReprompts);
      SET_STATE(Reprompt);
      _iConfig.ttsBehavior->SetTextToSay(vocalRepromptString);
      if(_iConfig.ttsBehavior->WantsToBeActivated()){
        DelegateIfInControl(_iConfig.ttsBehavior.get(), &BehaviorPromptUserForVoiceCommand::TransitionToListening);
      }
      return;
    }
  }

  CancelSelf();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
std::string BehaviorPromptUserForVoiceCommand::GetVocalPromptString() const
{
  if (_dVars.useDynamicPromptString) {
    return _dVars.dynamicPromptString;
  }
  const auto & vocalPromptKey = _iConfig.vocalPromptKey;
  if (vocalPromptKey != empty) {
    const auto & localeComponent = GetBEI().GetRobotInfo().GetLocaleComponent();
    return localeComponent.GetString(vocalPromptKey);
  }
  return empty;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
std::string BehaviorPromptUserForVoiceCommand::GetVocalRepromptString() const
{
  if (_dVars.useDynamicRepromptString) {
    return _dVars.dynamicRepromptString;
  }
  const auto & vocalRepromptKey = _iConfig.vocalRepromptKey;
  if (vocalRepromptKey != empty) {
    const auto & localeComponent = GetBEI().GetRobotInfo().GetLocaleComponent();
    return localeComponent.GetString(vocalRepromptKey);
  }
  return empty;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
std::string BehaviorPromptUserForVoiceCommand::GetVocalResponseToIntentString() const
{
  const auto & vocalResponseToIntentKey = _iConfig.vocalResponseToIntentKey;
  if (vocalResponseToIntentKey != empty) {
    const auto & localeComponent = GetBEI().GetRobotInfo().GetLocaleComponent();
    return localeComponent.GetString(vocalResponseToIntentKey);
  }
  return empty;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
std::string BehaviorPromptUserForVoiceCommand::GetVocalResponseToBadIntentString() const
{
  const auto & vocalResponseToBadIntentKey = _iConfig.vocalResponseToBadIntentKey;
  if (vocalResponseToBadIntentKey != empty) {
    const auto & localeComponent = GetBEI().GetRobotInfo().GetLocaleComponent();
    return localeComponent.GetString(vocalResponseToBadIntentKey);
  }
  return empty;
}

} // namespace Vector
} // namespace Anki

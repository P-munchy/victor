/**
 * File: sayTextAction.cpp
 *
 * Author: Andrew Stein
 * Date:   8/29/2014
 *
 * Description: Implements animation and audio cozmo-specific actions, derived from the IAction interface.
 *
 *
 * Copyright: Anki, Inc. 2014
 **/

// TODO: Update this to work with Text to Speech in Anim Process

#include "engine/actions/sayTextAction.h"
#include "engine/animations/animationGroup/animationGroup.h"
#include "engine/animations/animationGroup/animationGroupContainer.h"
#include "engine/audio/engineRobotAudioClient.h"
#include "engine/cozmoContext.h"
#include "engine/robot.h"
#include "engine/robotManager.h"

#include "anki/common/basestation/utils/data/dataPlatform.h"

#include "util/fileUtils/fileUtils.h"
#include "util/math/math.h"
#include "util/random/randomGenerator.h"


#define DEBUG_SAYTEXT_ACTION 0
// Max duration of generated animation
//const float kMaxAnimationDuration_ms = 60000;  // 1 min


namespace Anki {
namespace Cozmo {

const char* kLocalLogChannel = "Actions";

// Static Method
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool SayTextAction::LoadMetadata(Util::Data::DataPlatform& dataPlatform)
{
  if (!_intentConfigs.empty()) {
    PRINT_NAMED_WARNING("SayTextAction.LoadMetadata.AttemptToReloadStaticData", "_intentConfigs");
    return false;
  }
  
  // Check for file
  static const std::string filePath = "config/engine/sayTextintentConfig.json";
  if(!Util::FileUtils::FileExists(dataPlatform.pathToResource(Util::Data::Scope::Resources, filePath))) {
    PRINT_NAMED_ERROR("SayTextAction.LoadMetadata.FileNotFound", "sayTextintentConfig.json");
    return false;
  }
  
  // Read file
  Json::Value json;
  if(!dataPlatform.readAsJson(Util::Data::Scope::Resources, filePath, json)) {
    PRINT_NAMED_ERROR("SayTextAction.LoadMetadata.CanNotRead", "sayTextintentConfig.json");
    return false;
  }
  
  // Load Intent Config
  if (json.isNull() || !json.isObject()) {
    PRINT_NAMED_ERROR("SayTextAction.LoadMetadata.json.IsNull", "or.NotIsObject");
    return false;
  }
  
  // Create Cozmo Says Voice Style map
  SayTextVoiceStyleMap voiceStyleMap;
  for (uint8_t aStyleIdx = 0; aStyleIdx <  Util::numeric_cast<uint8_t>(SayTextVoiceStyle::Count); ++aStyleIdx) {
    const SayTextVoiceStyle aStyle = static_cast<SayTextVoiceStyle>(aStyleIdx);
    voiceStyleMap.emplace( EnumToString(aStyle), aStyle );
  }

  // Create Say Text Intent Map
  std::unordered_map<std::string, SayTextIntent> sayTextIntentMap;
  for (uint8_t anIntentIdx = 0; anIntentIdx < SayTextIntentNumEntries; ++anIntentIdx) {
    const SayTextIntent anIntent = static_cast<SayTextIntent>(anIntentIdx);
    sayTextIntentMap.emplace( EnumToString(anIntent), anIntent );
  }
  
  // Store metadata's Intent objects
  for (auto intentJsonIt = json.begin(); intentJsonIt != json.end(); ++intentJsonIt) {
    const std::string& name = intentJsonIt.key().asString();
    const auto intentEnumIt = sayTextIntentMap.find( name );
    DEV_ASSERT(intentEnumIt != sayTextIntentMap.end(), "SayTextAction.LoadMetadata.CanNotFindSayTextIntent");
    if (intentEnumIt != sayTextIntentMap.end()) {
      // Store Intent into STATIC var
      const SayTextIntentConfig config(name, *intentJsonIt, voiceStyleMap);
      _intentConfigs.emplace( intentEnumIt->second, std::move( config ) );
    }
  }
  
  return true;
}
 
// Public Methods
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
SayTextAction::SayTextAction(Robot& robot,
                             const std::string& text,
                             const SayTextVoiceStyle style,
                             const float durationScalar,
                             const float voicePitch)
: IAction(robot,
          "SayText",
          RobotActionType::SAY_TEXT,
          (u8)AnimTrackFlag::NO_TRACKS)
, _text(text)
, _style(style)
, _durationScalar(durationScalar)
, _voicePitch(voicePitch)
//, _ttsOperationId(TextToSpeechComponent::kInvalidOperationId)
//, _animation("SayTextAnimation") // TODO: SayTextAction is broken (VIC-360)
{
  PRINT_CH_INFO(kLocalLogChannel,
                "SayTextAction.InitWithStyle",
                "Text '%s' Style '%s' DurScalar %f Pitch %f",
                Util::HidePersonallyIdentifiableInfo(_text.c_str()),
                EnumToString(_style),
                _durationScalar,
                _voicePitch);
  
  GenerateTtsAudio();
} // SayTextAction()

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
SayTextAction::SayTextAction(Robot& robot, const std::string& text, const SayTextIntent intent)
: IAction(robot,
          "SayText",
          RobotActionType::SAY_TEXT,
          (u8)AnimTrackFlag::NO_TRACKS)
, _text(text)
//, _ttsOperationId(TextToSpeechComponent::kInvalidOperationId)
// , _animation("SayTextAnimation") // TODO: SayTextAction is broken (VIC-360)
{
  // Get metadata
  const auto it = _intentConfigs.find( intent );
  if ( it != _intentConfigs.end() ) {
    // Set intent values
    const SayTextIntentConfig& config = it->second;
    
    // Set audio processing style type
    _style = config.style;
    
    // Get Duration val
    const SayTextIntentConfig::ConfigTrait& durationTrait = config.FindDurationTraitTextLength(Util::numeric_cast<uint>(text.length()));
    _durationScalar = durationTrait.GetDuration( robot.GetRNG() );
    
    // Get Pitch val
    const SayTextIntentConfig::ConfigTrait& pitchTrait = config.FindPitchTraitTextLength(Util::numeric_cast<uint>(text.length()));
    _voicePitch = pitchTrait.GetDuration( robot.GetRNG() );
  }
  else {
    PRINT_NAMED_ERROR("SayTextAction.CanNotFind.SayTextIntentConfig", "%s", EnumToString(intent));
  }
  
  PRINT_CH_INFO(kLocalLogChannel,
                "SayTextAction.InitWithIntent",
                "Text '%s' Intent '%s' Style '%s' DurScalar %f Pitch %f",
                Util::HidePersonallyIdentifiableInfo(_text.c_str()),
                EnumToString(intent),
                EnumToString(_style),
                _durationScalar,
                _voicePitch);
  
  GenerateTtsAudio();
} // SayTextAction()

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
SayTextAction::~SayTextAction()
{
  // Now that we're all done, cleanup possible audio data leaks caused by action or animations being aborted. This is
  // safe to call for success as well.
  //_robot.GetTextToSpeechComponent().CleanupAudioEngine(_ttsOperationId);
  
  if(_playAnimationAction != nullptr) {
    _playAnimationAction->PrepForCompletion();
  }
} // ~SayTextAction()

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void SayTextAction::SetAnimationTrigger(AnimationTrigger trigger, u8 ignoreTracks)
{
  _animationTrigger = trigger;
  _ignoreAnimTracks = ignoreTracks;
}
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
ActionResult SayTextAction::Init()
{
  PRINT_NAMED_WARNING("SayTextAction.Init.Disabled", "TTS disabled");

  #ifdef notdef
  using namespace AudioMetaData;
  TextToSpeechComponent::AudioCreationState state = _robot.GetTextToSpeechComponent().GetOperationState(_ttsOperationId);
  switch (state) {
    case TextToSpeechComponent::AudioCreationState::Preparing:
    {
      // Can't initialize until text to speech is ready
      if (DEBUG_SAYTEXT_ACTION) {
        PRINT_CH_INFO(kLocalLogChannel, "SayTextAction.Init.LoadingTextToSpeech", "");
      }
      return ActionResult::RUNNING;
    }
      break;
      
    case TextToSpeechComponent::AudioCreationState::Ready:
    {
      // Set Audio data right before action runs
      float duration_ms = 0.0f;
      // FIXME: Need to way to get other Audio GameObjs
      const bool success = _robot.GetTextToSpeechComponent().PrepareAudioEngine(_ttsOperationId,
                                                                                duration_ms);
      if (success) {
        // Don't need to be responsible for audio data after successfully TextToSpeechComponent().PrepareAudioEngine()
        _ttsOperationId = TextToSpeechComponent::kInvalidOperationId;
      }
      else {
        PRINT_NAMED_ERROR("SayTextAction.Init.PrepareAudioEngine.Failed", "");
        return ActionResult::ABORT;
      }
      
      if (duration_ms * 0.001f > _timeout_sec) {
        PRINT_NAMED_ERROR("SayTextAction.Init.PrepareAudioEngine.DurationTooLong", "Duration: %f", duration_ms);
      }
      
      const bool useBuiltInAnim = (AnimationTrigger::Count == _animationTrigger);
      if (useBuiltInAnim) {
        // Make our animation a "live" animation with a single audio keyframe at the beginning
        if (DEBUG_SAYTEXT_ACTION) {
          PRINT_CH_INFO(kLocalLogChannel, "SayTextAction.Init.CreatingAnimation", "");
        }
        // Get appropriate audio event for style and insert key frame
        // TODO: Deprecate this, we are going to change the processing
        // TODO: SayTextAction is broken (VIC-360)
        /*
        const GameEvent::GenericEvent audioEvent = _robot.GetTextToSpeechComponent().GetAudioEvent(_style);
        
        _animation.AddKeyFrameToBack(RobotAudioKeyFrame(RobotAudioKeyFrame::AudioRef(audioEvent), 0));
        _animation.SetIsLive(true);
        _playAnimationAction.reset(new PlayAnimationAction(_robot, &_animation));
         */
      }
      else {
        if (DEBUG_SAYTEXT_ACTION) {
          PRINT_CH_INFO(kLocalLogChannel,
                        "SayTextAction.Init.UsingAnimationGroup", "GameEvent=%d (%s) fitToDuration %c",
                        _animationTrigger, EnumToString(_animationTrigger), _fitToDuration ? 'Y' : 'N');
        }
        // Either create an animation for the duration of the generated audio or play specific animation group
        if (_fitToDuration) {
          // Get appropriate audio event for style and insert key frame
          // TODO: Deprecate this, we are going to change the processing
          //const GameEvent::GenericEvent audioEvent = _robot.GetTextToSpeechComponent().GetAudioEvent(_style);
          
          // TODO: SayTextAction is broken (VIC-360)
          //_animation.AddKeyFrameToBack(RobotAudioKeyFrame(RobotAudioKeyFrame::AudioRef(audioEvent), 0));
          
          // Generate animation
          UpdateAnimationToFitDuration(duration_ms);
          
          // TODO: SayTextAction is broken (VIC-360)
          //_playAnimationAction.reset(new PlayAnimationAction(_robot, &_animation, 1, true, _ignoreAnimTracks));
        }
        else {
          // Use current animation trigger
          _playAnimationAction.reset(new TriggerLiftSafeAnimationAction(_robot,
                                                                        _animationTrigger,
                                                                        1,
                                                                        true,
                                                                        _ignoreAnimTracks));
        }
      }
      
      // Set Audio Engine Say Text processing parameters
      // Map SayTextVoice style to Audio Engine SwitchState
      // NOTE: Need to manually map SayTextVoiceStyle to SwitchState::Cozmo_Voice_Processing enum
      const std::unordered_map<SayTextVoiceStyle, SwitchState::Cozmo_Voice_Processing, Util::EnumHasher> processingStateMap {
        { SayTextVoiceStyle::Unprocessed, SwitchState::Cozmo_Voice_Processing::Unprocessed },
        { SayTextVoiceStyle::CozmoProcessing_Name, SwitchState::Cozmo_Voice_Processing::Name },
        { SayTextVoiceStyle::CozmoProcessing_Name_Question, SwitchState::Cozmo_Voice_Processing::Name },
        { SayTextVoiceStyle::CozmoProcessing_Sentence, SwitchState::Cozmo_Voice_Processing::Sentence }
      };
      DEV_ASSERT(processingStateMap.size() == Util::numeric_cast<uint32_t>(SayTextVoiceStyle::Count),
                 "SayTextAction.Init.processingStateMap.InvalidSize");
      
      const auto it = processingStateMap.find(_style);
      DEV_ASSERT(it != processingStateMap.end(), "SayTextAction.Init.processingStateMap.StyleNotFound");
      const SwitchState::GenericSwitch processingState = static_cast<const SwitchState::GenericSwitch>( it->second );
      // Set voice Pitch
      // Set Cozmo Says Switch State
      // TODO: JIRA VIC-23 - Migrate Text to Speech component to Victor
      _robot.GetAudioClient()->PostSwitchState(SwitchState::SwitchGroupType::Cozmo_Voice_Processing,
                                               processingState,
                                               GameObjectType::Default /* FIXME: Not correct game object */);
      // Set Cozmo Says Pitch RTPC Parameter
      _robot.GetAudioClient()->PostParameter(GameParameter::ParameterType::External_Process_Pitch,
                                             _voicePitch,
                                             GameObjectType::Default /* FIXME: Not correct game object */);
      
      _isAudioReady = true;
      
      return ActionResult::SUCCESS;
    }
      break;
      
    case TextToSpeechComponent::AudioCreationState::None:
    {
      // Audio load failed
      if (DEBUG_SAYTEXT_ACTION) {
        PRINT_CH_INFO(kLocalLogChannel, "SayTextAction.Init.TextToSpeechFailed", "");
      }
      return ActionResult::ABORT;
    }
      break;
  }
  #endif
  return ActionResult::SUCCESS;
} // Init()

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
ActionResult SayTextAction::CheckIfDone()
{
  PRINT_NAMED_WARNING("SayTextAction.CheckIfDone.Disabled", "TTS disabled");

  #ifdef notdef
  DEV_ASSERT(_isAudioReady, "SayTextAction.CheckIfDone.TextToSpeechNotReady");
  
  if (DEBUG_SAYTEXT_ACTION) {
    PRINT_CH_INFO(kLocalLogChannel, "SayTextAction.CheckIfDone.UpdatingAnimation", "");
  }
  
  return _playAnimationAction->Update();
  #endif

  return ActionResult::SUCCESS;
} // CheckIfDone()
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void SayTextAction::GenerateTtsAudio()
{
  #ifdef notdef
  // Be careful with putting text in the action name because it could be a player name, which is PII
  SetName(std::string("SayText_") + Util::HidePersonallyIdentifiableInfo(_text.c_str()));
  
  // Create speech data
  _ttsOperationId = _robot.GetTextToSpeechComponent().CreateSpeech(_text, _style, _durationScalar);
  if (TextToSpeechComponent::kInvalidOperationId == _ttsOperationId) {
    PRINT_NAMED_ERROR("SayTextAction.SayTextAction.CreateSpeech", "SpeechState is None");
  }
  #endif
} // GenerateTtsAudio()

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Helper method
// TODO: Is there a better way to do this?
  // TODO: SayTextAction is broken (VIC-360)
  /*
const Animation* GetAnimation(const AnimationTrigger& animTrigger, Robot& robot)
{
  RobotManager* robot_mgr = robot.GetContext()->GetRobotManager();
  const Animation* anim = nullptr;
  if (robot_mgr->HasAnimationForTrigger(animTrigger)) {
    std::string animationGroupName = robot_mgr->GetAnimationForTrigger(animTrigger);
    if (animationGroupName.empty()) {
      PRINT_NAMED_ERROR("SayTextAction.GetAnimation.TriggerAnimationAction.EmptyAnimGroupNameForTrigger",
                          "Event: %s", EnumToString(animTrigger));
    }
    // Get AnimationGroup for animation group name
    const AnimationGroup* group = robot.GetContext()->GetRobotManager()->GetAnimationGroups().GetAnimationGroup(animationGroupName);
    if (group != nullptr && !group->IsEmpty()) {
      // Get Random animation in group
      const std::string& animName = group->GetAnimationName(robot.GetMoodManager(),
                                                            robot.GetContext()->GetRobotManager()->GetAnimationGroups(),
                                                            robot.GetHeadAngle());
      anim = robot.GetContext()->GetRobotManager()->GetCannedAnimations().GetAnimation(animName);
    }
  }
  else {
    PRINT_NAMED_ERROR("SayTextAction.GetAnimation.TriggerAnimationAction.NoAnimationForTrigger",
                        "Event: %s", EnumToString(animTrigger));
  }
  return anim;
}
   */
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void SayTextAction::UpdateAnimationToFitDuration(const float duration_ms)
{
  if (AnimationTrigger::Count != _animationTrigger) {
    // TODO: SayTextAction is broken (VIC-360)
    /*
    while (_animation.GetLastKeyFrameTime_ms() < duration_ms && duration_ms <= kMaxAnimationDuration_ms ) {
      const Animation* nextAnim = GetAnimation(_animationTrigger, _robot);
      if (nullptr != nextAnim) {
        _animation.AppendAnimation(*nextAnim);
      }
      else {
        PRINT_NAMED_ERROR("SayTextAction.UpdateAnimationToFitDuration.GetAnimationFailed",
                          "AnimationTrigger: %s", EnumToString(_animationTrigger));
        break;
      }
    }
     */
  }
  else {
    PRINT_NAMED_WARNING("SayTextAction.UpdateAnimationToFitDuration.InvalidAnimationTrigger", "AnimationTrigger::Count");
  }
} // UpdateAnimationToFitDuration()

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Static Var
SayTextAction::SayIntentConfigMap SayTextAction::_intentConfigs;

// SayTextIntentConfig methods
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
SayTextAction::SayTextIntentConfig::SayTextIntentConfig()
{ }

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
SayTextAction::SayTextIntentConfig::SayTextIntentConfig(const std::string& intentName,
                                                        const Json::Value& json,
                                                        const SayTextVoiceStyleMap& styleMap)
: name(intentName)
{
  // Set Voice Style
  const auto styleKey = json.get("style", Json::Value::null);
  if (!styleKey.isNull()) {
    const auto it = styleMap.find(styleKey.asString());
    DEV_ASSERT(it != styleMap.end(), "SayTextAction.LoadMetadata.IntentStyleNotFound");
    if (it != styleMap.end()) {
      style = it->second;
    }
  }
  
  // Duration Traits
  const auto durationTraitJson = json.get("durationTraits", Json::Value::null);
  if (!durationTraitJson.isNull()) {
    for (auto traitIt = durationTraitJson.begin(); traitIt != durationTraitJson.end(); ++traitIt) {
      durationTraits.emplace_back(*traitIt);
    }
  }
  
  // Pitch Traits
  const auto pitchTraitJson = json.get("pitchTraits", Json::Value::null);
  if (!pitchTraitJson.isNull()) {
    for (auto traitIt = pitchTraitJson.begin(); traitIt != pitchTraitJson.end(); ++traitIt) {
      pitchTraits.emplace_back(*traitIt);
    }
  }
  
  DEV_ASSERT(!name.empty(), "SayTextAction.LoadMetadata.Intent.name.IsEmpty");
  DEV_ASSERT(!durationTraitJson.empty(), "SayTextAction.LoadMetadata.Intent.durationTraits.IsEmpty");
  DEV_ASSERT(!pitchTraitJson.empty(), "SayTextAction.LoadMetadata.Intent.pitchTraits.IsEmpty");
} // SayTextIntentConfig()
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
const SayTextAction::SayTextIntentConfig::ConfigTrait& SayTextAction::SayTextIntentConfig::FindDurationTraitTextLength(uint textLength) const
{
  for ( const auto& aTrait : durationTraits ) {
    if (aTrait.textLengthMin <= textLength && aTrait.textLengthMax >= textLength) {
      return aTrait;
    }
  }
  return durationTraits.front();
} // FindDurationTraitTextLength()

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
const SayTextAction::SayTextIntentConfig::ConfigTrait& SayTextAction::SayTextIntentConfig::FindPitchTraitTextLength(uint textLength) const
{
  for ( const auto& aTrait : pitchTraits ) {
    if (aTrait.textLengthMin <= textLength && aTrait.textLengthMax >= textLength) {
      return aTrait;
    }
  }
  return pitchTraits.front();
} // FindPitchTraitTextLength()

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
SayTextAction::SayTextIntentConfig::ConfigTrait::ConfigTrait()
{ }

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
SayTextAction::SayTextIntentConfig::ConfigTrait::ConfigTrait(const Json::Value& json)
{
  textLengthMin = json.get("textLengthMin", Json::Value(std::numeric_limits<uint>::min())).asUInt();
  textLengthMax = json.get("textLengthMax", Json::Value(std::numeric_limits<uint>::max())).asUInt();
  rangeMin = json.get("rangeMin", Json::Value(std::numeric_limits<float>::min())).asFloat();
  rangeMax = json.get("rangeMax", Json::Value(std::numeric_limits<float>::max())).asFloat();
  rangeStepSize = json.get("stepSize", Json::Value(0.f)).asFloat(); // If No step size use Range Min and don't randomize
} // ConfigTrait()
  
float SayTextAction::SayTextIntentConfig::ConfigTrait::GetDuration(Util::RandomGenerator& randomGen) const
{
  // TODO: Move this into Random Util class
  float resultVal;
  if (Util::IsFltGTZero( rangeStepSize )) {
    // (Scalar Range / stepSize) + 1 = number of total possible steps
    const int stepCount = ((rangeMax - rangeMin) / rangeStepSize) + 1;
    const auto randStep = randomGen.RandInt( stepCount );
    resultVal = rangeMin + (rangeStepSize * randStep);
  }
  else {
    resultVal = rangeMin;
  }
  return resultVal;
} // GetRange()
  
} // namespace Cozmo
} // namespace Anki

/*
 * File: robotAudioClient.cpp
 *
 * Author: Jordan Rivas
 * Created: 11/09/2015
 *
 * Description: This Client handles the Robot’s specific audio needs. It is a subclass of AudioEngineClient.
 *
 * Copyright: Anki, Inc. 2015
 */


#include "audioEngine/multiplexer/audioMultiplexer.h"
#include "engine/audio/cozmoAudioController.h"
#include "engine/audio/robotAudioClient.h"
#include "engine/audio/robotAudioAnimationOnDevice.h"
#include "engine/audio/robotAudioAnimationOnRobot.h"
#include "engine/cozmoContext.h"
#include "engine/externalInterface/externalInterface.h"
#include "engine/robot.h"
#include "engine/robotManager.h"
#include "engine/robotInterface/messageHandler.h"
#include "clad/audio/messageAudioClient.h"
#include "clad/robotInterface/messageEngineToRobot.h"
#include "audioEngine/audioCallback.h"
#include "util/dispatchQueue/dispatchQueue.h"
#include "util/helpers/templateHelpers.h"
#include "util/logging/logging.h"


// Always play audio on device
#define OVERRIDE_ON_DEVICE_OUTPUT_SOURCE 1


namespace Anki {
namespace Cozmo {
namespace Audio {
using namespace AudioEngine;
  
using namespace AudioMetaData;
  
const char* RobotAudioClient::kRobotAudioLogChannelName = CozmoAudioController::kLogChannelName;
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
RobotAudioClient::RobotAudioClient( Robot* robot )
: _robot( robot )
, _dispatchQueue(Util::Dispatch::Create("RobotAudioClient"))
{
  if (_robot == nullptr) {
    return;
  }
  const CozmoContext* context = _robot->GetContext();
  // For Unit Test bail out if there is no Audio Mux
  if ( context->GetAudioMultiplexer() == nullptr ) {
    return;
  }

  _audioController = static_cast<CozmoAudioController*>( context->GetAudioMultiplexer()->GetAudioController() );
  
  // Add Listeners to Game to Engine events
  auto robotVolumeCallback = [this] ( const AnkiEvent<ExternalInterface::MessageGameToEngine>& message ) {
    const ExternalInterface::SetRobotVolume& msg = message.GetData().Get_SetRobotVolume();
    SetRobotVolume( msg.volume );
  };

  auto robotAudioOutputSourceCallback = [this] ( const AnkiEvent<ExternalInterface::MessageGameToEngine>& message ){
    const ExternalInterface::SetRobotAudioOutputSource& msg = message.GetData().Get_SetRobotAudioOutputSource();

    RobotAudioOutputSource outputSource;


    // Switch case is needed to "cast" the CLAD generated RobotAudioOutputSource enum into
    // RobotAudioClient::RobotAudioOutputSource. This allows RobotAudioOutputSource to stay in
    // RobotAudioClient (instead of solely referencing the enum from the CLAD generated headers, in
    // order to limit CLAD facing code in the rest of the audio codebase).

    switch (msg.source)
    {
      case ExternalInterface::RobotAudioOutputSourceCLAD::NoDevice:
        outputSource = RobotAudioOutputSource::None;
        break;

      case ExternalInterface::RobotAudioOutputSourceCLAD::PlayOnDevice:
        outputSource = RobotAudioOutputSource::PlayOnDevice;
        break;

      case ExternalInterface::RobotAudioOutputSourceCLAD::PlayOnRobot:
        outputSource = RobotAudioOutputSource::PlayOnRobot;
        break;
    }

    SetOutputSource( outputSource );
    PRINT_CH_DEBUG(kRobotAudioLogChannelName,
                   "RobotAudioClient.RobotAudioClient.RobotAudioOutputSourceCallback",
                   "outputSource: %s", EnumToString(msg.source));
  };
  
  IExternalInterface* gameToEngineInterface = context->GetExternalInterface();
  if ( gameToEngineInterface ) {
    PRINT_CH_DEBUG(kRobotAudioLogChannelName,
                   "RobotAudioClient.RobotAudioClient", "gameToEngineInterface exists");

    _signalHandles.push_back(gameToEngineInterface->Subscribe(ExternalInterface::MessageGameToEngineTag::SetRobotVolume,
                                                              robotVolumeCallback));
    
    _signalHandles.push_back(gameToEngineInterface->Subscribe(ExternalInterface::MessageGameToEngineTag::SetRobotAudioOutputSource,
                                                              robotAudioOutputSourceCallback));
  }

  // Configure Robot Audio buffers with Wwise buses. PlugIn Ids are set in Wwise project
  // Setup Robot Buffers
  // Note: This is only configured to work with a single robot
  RegisterRobotAudioBuffer( GameObjectType::CozmoBus_1, 1, Bus::BusType::Robot_Bus_1 );
  RegisterRobotAudioBuffer( GameObjectType::CozmoBus_2, 2, Bus::BusType::Robot_Bus_2 );
  RegisterRobotAudioBuffer( GameObjectType::CozmoBus_3, 3, Bus::BusType::Robot_Bus_3 );
  RegisterRobotAudioBuffer( GameObjectType::CozmoBus_4, 4, Bus::BusType::Robot_Bus_4 );
  RegisterRobotAudioBuffer( GameObjectType::Cozmo_OnDevice );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
RobotAudioClient::~RobotAudioClient()
{
  Util::Dispatch::Stop(_dispatchQueue);
  Util::Dispatch::Release(_dispatchQueue);
  
  if (nullptr != _audioController) {
    if ( _currentAnimation != nullptr ) {
      _currentAnimation->AbortAnimation();
      ClearCurrentAnimation();
    }
    UnregisterRobotAudioBuffer( GameObjectType::CozmoBus_1 );
    UnregisterRobotAudioBuffer( GameObjectType::CozmoBus_2 );
    UnregisterRobotAudioBuffer( GameObjectType::CozmoBus_3 );
    UnregisterRobotAudioBuffer( GameObjectType::CozmoBus_4 );
    UnregisterRobotAudioBuffer( GameObjectType::Cozmo_OnDevice );
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
RobotAudioBuffer* RobotAudioClient::GetRobotAudioBuffer( AudioMetaData::GameObjectType gameObject )
{
  DEV_ASSERT(_audioController != nullptr, "RobotAudioClient.GetRobotAudioBuffer.AudioControllerNull");
  const AudioGameObject aGameObject = static_cast<const AudioGameObject>( gameObject );
  return _audioController->GetRobotAudioBufferWithGameObject( aGameObject );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
RobotAudioClient::CozmoPlayId RobotAudioClient::PostCozmoEvent( AudioMetaData::GameEvent::GenericEvent event,
                                                                AudioMetaData::GameObjectType gameObjId,
                                                                CozmoEventCallbackFunc callbackFunc ) const
{
  const auto audioEventId = Util::numeric_cast<AudioEventId>( event );
  const auto audioGameObjId = static_cast<AudioGameObject>( gameObjId );
  AudioCallbackContext* audioCallbackContext = nullptr;
  
  if ( callbackFunc != nullptr ) {
    audioCallbackContext = new AudioCallbackContext();
    // Set callback flags
    audioCallbackContext->SetCallbackFlags( AudioCallbackFlag::AllCallbacks );
    // Execute callbacks synchronously (on main thread)
    audioCallbackContext->SetExecuteAsync( false );
    // Register callbacks for event
    audioCallbackContext->SetEventCallbackFunc ( [ callbackFunc = std::move(callbackFunc) ]
    ( const AudioCallbackContext* thisContext, const AudioCallbackInfo& callbackInfo )
    {
      callbackFunc( callbackInfo );
    } );
  }
  
  return _audioController->PostAudioEvent( audioEventId, audioGameObjId, audioCallbackContext );
}

void RobotAudioClient::PostRobotParameter(const AudioMetaData::GameParameter::ParameterType parameter,
                                          const float parameterValue) const
{
  PostParameter(parameter, parameterValue, GetGameObjectType());
}

void RobotAudioClient::PostRobotSwitchState(const AudioMetaData::SwitchState::SwitchGroupType switchGroup,
                                            const AudioMetaData::SwitchState::GenericSwitch switchState)
{
  PostSwitchState(switchGroup, switchState, GetGameObjectType());
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool RobotAudioClient::SetCozmoEventParameter( CozmoPlayId playId, AudioMetaData::GameParameter::ParameterType parameter, float value ) const
{
  const AudioParameterId parameterId = Util::numeric_cast<AudioParameterId>( parameter );
  const AudioRTPCValue rtpcVal = Util::numeric_cast<AudioRTPCValue>( value );
  const AudioPlayingId audioPlayId = Util::numeric_cast<AudioPlayingId>( playId );
  return _audioController->SetParameterWithPlayingId( parameterId, rtpcVal, audioPlayId );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void RobotAudioClient::StopCozmoEvent(AudioMetaData::GameObjectType gameObjId)
{
  const auto audioGameObjId = static_cast<AudioGameObject>( gameObjId );
  _audioController->StopAllAudioEvents(audioGameObjId);
  _audioController->ProcessAudioQueue();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void RobotAudioClient::ProcessEvents() const
{
  _audioController->ProcessAudioQueue();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void RobotAudioClient::FlushAudioCallbackQueue()
{
  _audioController->FlushCallbackQueue();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// vvvvvvvvvvvv Deprecated vvvvvvvvvvvvvvvv
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void RobotAudioClient::CreateAudioAnimation( Animation* anAnimation )
{
  // Check if there is a current animation, if so abort that animation and clean up correctly
  if ( _currentAnimation != nullptr ) {
    PRINT_CH_INFO( kRobotAudioLogChannelName,
                   "RobotAudioClient.CreateAudioAnimation",
                   "CurrentAnimation '%s' state: %s is NOT Null when creating a new animation",
                   _currentAnimation->GetAnimationName().c_str(),
                   RobotAudioAnimation::GetStringForAnimationState(_currentAnimation->GetAnimationState()).c_str() );
    _currentAnimation->AbortAnimation();
    ClearCurrentAnimation();
  }

  // Create appropriate animation type for mode
  RobotAudioAnimation* audioAnimation = nullptr;
  switch ( _outputSource ) {
  
    case RobotAudioOutputSource::PlayOnDevice:
    {
      audioAnimation = static_cast<RobotAudioAnimation*>( new RobotAudioAnimationOnDevice( anAnimation,
                                                                                           this,
                                                                                           GameObjectType::Cozmo_OnDevice,
                                                                                           &_robot->GetRNG() ) );
      break;
    }
      
    case RobotAudioOutputSource::PlayOnRobot:
    {
      audioAnimation = static_cast<RobotAudioAnimation*>( new RobotAudioAnimationOnRobot( anAnimation,
                                                                                          this,
                                                                                          GameObjectType::CozmoBus_1,
                                                                                          &_robot->GetRNG() ) );
      break;
    }
      
    case RobotAudioOutputSource::None:
    {
      // We have no audio device, but we need the audio track to appear to be "done", so
      // fast forward it to the end.
      Animations::Track<RobotAudioKeyFrame>& audioTrack = anAnimation->GetTrack<RobotAudioKeyFrame>();
      audioTrack.MoveToEnd();
      break;
    }
  }
  
  // Did not create animation
  if ( audioAnimation == nullptr ) {
    return;
  }

  // Check if animation is valid
  const RobotAudioAnimation::AnimationState animationState = audioAnimation->GetAnimationState();
  if ( animationState != RobotAudioAnimation::AnimationState::AnimationCompleted &&
       animationState != RobotAudioAnimation::AnimationState::AnimationError ) {
    _currentAnimation = audioAnimation;
  }
  else {
    // Audio is not needed for this animation
    Util::SafeDelete( audioAnimation );
    _currentAnimation = nullptr;
  }
}
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void RobotAudioClient::ClearCurrentAnimation()
{
  std::string animationState = "No Current Audio Animation";
  if ( _currentAnimation != nullptr ) {
    animationState = "Current Audio Animation '" + _currentAnimation->GetAnimationName() + "' State: " +
                     _currentAnimation->GetStringForAnimationState( _currentAnimation->GetAnimationState() );
  }
  PRINT_CH_INFO(RobotAudioClient::kRobotAudioLogChannelName,
                "RobotAudioClient.ClearCurrentAnimation", "%s", animationState.c_str());
  Util::SafeDelete(_currentAnimation);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool RobotAudioClient::UpdateAnimationIsReady( TimeStamp_t startTime_ms, TimeStamp_t streamingTime_ms )
{
  // No Animation allow animation to proceed
  if ( !HasAnimation() ) {
    return true;
  }
  
  // Buffer is ready to get the next frame from or we're inbetween audio streams
  if ( _currentAnimation->GetAnimationState() == RobotAudioAnimation::AnimationState::AudioFramesReady ||
       _currentAnimation->GetAnimationState() == RobotAudioAnimation::AnimationState::LoadingStream ) {
    return true;
  }
  
  // Animation is completed or has error, clear it and proceed
  if ( _currentAnimation->GetAnimationState() == RobotAudioAnimation::AnimationState::AnimationCompleted ||
       _currentAnimation->GetAnimationState() == RobotAudioAnimation::AnimationState::AnimationError ) {
    // Clear animation
    ClearCurrentAnimation();
    return true;
  }
  
  return false;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool RobotAudioClient::AnimationIsComplete()
{
  // There is no animation
  if ( !HasAnimation() ) {
    return true;
  }
  // Animation state is completed or it has error
  else if ( _currentAnimation->GetAnimationState() == RobotAudioAnimation::AnimationState::AnimationCompleted ||
            _currentAnimation->GetAnimationState() == RobotAudioAnimation::AnimationState::AnimationError ) {
    return true;
  }
  
  return false;
}
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// ^^^^^^^^^^^^ Deprecated ^^^^^^^^^^^^^
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void RobotAudioClient::SetRobotVolume(float volume)
{
  // Update robot volume
  _robotVolume = volume;
  const uint16_t vol = Util::numeric_cast<uint16_t>(UINT16_MAX * _robotVolume);
  // Send volume message to Robot (Play on Robot)
  _robot->SendMessage(RobotInterface::EngineToRobot(SetAudioVolume(vol)));
  // Set volume in Audio Engine (Play on Device)
  PostParameter(GameParameter::ParameterType::Robot_Volume, _robotVolume, GameObjectType::Invalid);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
float RobotAudioClient::GetRobotVolume() const
{
  return _robotVolume;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void RobotAudioClient::SetOutputSource( RobotAudioOutputSource outputSource )
{
  DEV_ASSERT(_audioController != nullptr, "RobotAudioClient.SetOutputSource.AudioControllerNull");
  
  if ( _outputSource == outputSource ) {
    // Do Nothing
    return;
  }
  _outputSource = outputSource;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool RobotAudioClient::GetGameObjectAndAudioBufferFromPool(AudioMetaData::GameObjectType& out_gameObj, RobotAudioBuffer*& out_buffer)
{
  // Find appropriate game object and buffer for output source
  bool hasBuffer = false;
  switch (_outputSource) {
    case RobotAudioOutputSource::None:
    {
      PRINT_CH_INFO(RobotAudioClient::kRobotAudioLogChannelName,
                   "RobotAudioClient.GetGameObjectAndAudioBuffer.RobotAudioOutputSource.None", "");
      out_gameObj = GameObjectType::Invalid;
      out_buffer = nullptr;
      break;
    }
      
    case RobotAudioOutputSource::PlayOnDevice:
    {
      // FIXME: Need to use Cozmo Device Specific Game Object
      out_gameObj = GameObjectType::Cozmo_OnDevice;
      out_buffer = nullptr;
      // Don't need buffer
      hasBuffer = true;
      break;
    }
      
    case RobotAudioOutputSource::PlayOnRobot:
    {
      // Get GameObj & Buffer from pool
      if (_robotBufferGameObjectPool.empty()) {
        // No buffer available
        out_gameObj = GameObjectType::Invalid;
        out_buffer = nullptr;
      }
      else {
        const auto gameObj = _robotBufferGameObjectPool.front();
        const auto buffer = GetRobotAudioBuffer(gameObj);
        DEV_ASSERT(buffer != nullptr, "RobotAudioClient.GetGameObjectAndAudioBufferFromPool.BufferIsNull");
        
        out_gameObj = gameObj;
        out_buffer = buffer;
        hasBuffer = true;
      }
      
      break;
    }
  }
  return hasBuffer;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void RobotAudioClient::ReturnGameObjectToPool(AudioMetaData::GameObjectType gameObject)
{
  switch (gameObject) {
    case GameObjectType::CozmoBus_1:
    case GameObjectType::CozmoBus_2:
    case GameObjectType::CozmoBus_3:
    case GameObjectType::CozmoBus_4:
      // Add Valide Game Object to Pool
      _robotBufferGameObjectPool.push(gameObject);
      break;
      
    case GameObjectType::Cozmo_OnDevice:
      // We expect to get this when playing animation audio on device
      
    default:
      // We should never get here!!
      DEV_ASSERT(false, "RobotAudioClient.ReturnGameObjectToPool.Invalid.GameObjectType");
      break;
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Util::RandomGenerator& RobotAudioClient::GetRandomGenerator() const
{
  return _robot->GetRNG();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
RobotAudioBuffer* RobotAudioClient::RegisterRobotAudioBuffer( AudioMetaData::GameObjectType gameObject,
                                                              PluginId_t pluginId, // make default
                                                              AudioMetaData::Bus::BusType audioBus) // make default
{
  DEV_ASSERT(_audioController != nullptr, "RobotAudioClient.RegisterRobotAudioBuffer.AudioControllerNull");
  
  // Create Configuration Struct
  RobotBusConfiguration busConfiguration = { gameObject, pluginId, audioBus };
  const auto it = _busConfigurationMap.emplace( gameObject, busConfiguration );
  if ( !it.second ) {
    // Bus configuration already exist
    PRINT_NAMED_ERROR("RobotAudioClient.RegisterRobotAudioBuffer", "Bus configuration already exist for GameObject: %d",
                      static_cast<uint32_t>(gameObject));
  }
  
  // Add Game Object to pool
  _robotBufferGameObjectPool.push( busConfiguration.gameObject );
  
  // Create Buffer for buses
  if ( (pluginId != kInvalidPluginId) && (audioBus != AudioMetaData::Bus::BusType::Invalid) ) {
    // Setup GameObject with Bus
    AudioGameObject audioGameObject = static_cast<const AudioGameObject>( busConfiguration.gameObject );
    
    // Set Aux send settings in Audio Engine
    CozmoAudioController::AuxSendList sendList {
      AudioAuxBusValue( static_cast<AudioAuxBusId>( busConfiguration.bus ), 1.0f )
    };
    // Set Aux send settings in Audio Engine
    _audioController->SetGameObjectAuxSendValues( audioGameObject, sendList );
    _audioController->SetGameObjectOutputBusVolume( audioGameObject, 0.0f );
    
    return _audioController->RegisterRobotAudioBuffer( audioGameObject, pluginId );
  }
  
  return nullptr;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void RobotAudioClient::UnregisterRobotAudioBuffer( AudioMetaData::GameObjectType gameObject )
{
  DEV_ASSERT(_audioController != nullptr, "RobotAudioClient.UnregisterRobotAudioBuffer.AudioControllerNull");
  
  // Remove Configuration Struct
  PluginId_t pluginId = kInvalidPluginId;
  const auto it = _busConfigurationMap.find(gameObject);
  if ( it != _busConfigurationMap.end() ) {
    pluginId = it->second.pluginId;
    _busConfigurationMap.erase(it);
  } else {
    // Bus doesn't exist
    PRINT_NAMED_ERROR("RobotAudioClient.UnregisterRobotAudioBuffer", "Bus configuration doesn't exist for GameObject: %d",
                      static_cast<uint32_t>(gameObject));
  }
  
  // Destroy buffer
  if (pluginId != kInvalidPluginId ) {
    const AudioGameObject aGameObject = static_cast<const AudioGameObject>( gameObject );
    _audioController->UnregisterRobotAudioBuffer( aGameObject, pluginId );
  }
}

AudioMetaData::GameObjectType RobotAudioClient::GetGameObjectType() const
{
  // Figure out which audio buffer we should be posting the parameter to
  using AudioSource = Audio::RobotAudioClient::RobotAudioOutputSource;
  const AudioSource source = GetOutputSource();
  const AudioMetaData::GameObjectType gameObject = (source == AudioSource::PlayOnRobot ?
                                                    AudioMetaData::GameObjectType::CozmoBus_1 :
                                                    AudioMetaData::GameObjectType::Cozmo_OnDevice);
  return gameObject;
}

} // Audio
} // Cozmo
} // Anki

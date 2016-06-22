/*
 * File: robotAudioClient.cpp
 *
 * Author: Jordan Rivas
 * Created: 11/09/2015
 *
 * Description: This Client handles the Robot’s specific audio needs. It is a sub-class of AudioEngineClient.
 *
 * Copyright: Anki, Inc. 2015
 */

#include "anki/cozmo/basestation/audio/robotAudioClient.h"
#include "anki/cozmo/basestation/audio/audioController.h"
#include "anki/cozmo/basestation/audio/audioServer.h"
#include "anki/cozmo/basestation/audio/robotAudioAnimationOnDevice.h"
#include "anki/cozmo/basestation/audio/robotAudioAnimationOnRobot.h"
#include "anki/cozmo/basestation/cozmoContext.h"
#include "anki/cozmo/basestation/externalInterface/externalInterface.h"
#include "anki/cozmo/basestation/robot.h"
#include "anki/cozmo/basestation/robotManager.h"
#include "anki/cozmo/basestation/robotInterface/messageHandler.h"
#include "clad/audio/messageAudioClient.h"
#include "util/helpers/templateHelpers.h"
#include "util/logging/logging.h"

// Always play audio on device
#define OVERRIDE_ON_DEVICE_OUTPUT_SOURCE 0


namespace Anki {
namespace Cozmo {
namespace Audio {
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
RobotAudioClient::RobotAudioClient( Robot* robot )
: _robot( robot )
{
  if (_robot == nullptr) {
    return;
  }
  const CozmoContext* context = _robot->GetContext();
  // For Unit Test bale out if there is no Audio Server
  if ( context->GetAudioServer() == nullptr ) {
    return;
  }

  _audioController = context->GetAudioServer()->GetAudioController();

  // Add listener to robot messages
  // This only helps to determine if we should play sound on Webots or on the Robot
  auto robotSyncCallback = [this] ( const AnkiEvent<RobotInterface::RobotToEngine>& message ) {
    RobotAudioOutputSource outputSource = RobotAudioOutputSource::PlayOnDevice;

    if ( ! OVERRIDE_ON_DEVICE_OUTPUT_SOURCE ) {
      const RobotInterface::SyncTimeAck msg = message.GetData().Get_syncTimeAck();
      outputSource = msg.isPhysical ? RobotAudioOutputSource::PlayOnRobot : RobotAudioOutputSource::PlayOnDevice;
    }

    // Rely on casting to convert between the RobotAudioClient enum and the CLAD generated enum for
    // the easy ToString function

    PRINT_NAMED_DEBUG(
      "RobotAudioClient.RobotAudioClient.RobotSyncCallback",
      "outputSource: %s",
      ExternalInterface::RobotAudioOutputSourceCLADToString(
        (ExternalInterface::RobotAudioOutputSourceCLAD)outputSource
      )
    );

    SetOutputSource( outputSource );
  };
  
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

    PRINT_NAMED_DEBUG("RobotAudioClient.RobotAudioClient.RobotAudioOutputSourceCallback", "outputSource: %hhu", msg.source);
  };
  
  RobotInterface::MessageHandler* robotMsgHandler = context->GetRobotManager()->GetMsgHandler();
  if ( robotMsgHandler) {
    _signalHandles.push_back( robotMsgHandler->Subscribe( _robot->GetID(),
                                                          RobotInterface::RobotToEngineTag::syncTimeAck,
                                                          robotSyncCallback ) );
  }

  IExternalInterface* gameToEngineInterface = context->GetExternalInterface();
  if ( gameToEngineInterface ) {
    PRINT_NAMED_DEBUG("RobotAudioClient.RobotAudioClient", "gameToEngineInterface exists");

    _signalHandles.push_back(
      gameToEngineInterface->Subscribe(ExternalInterface::MessageGameToEngineTag::SetRobotVolume,
                                       robotVolumeCallback)
    );

    _signalHandles.push_back(
      gameToEngineInterface->Subscribe(
        ExternalInterface::MessageGameToEngineTag::SetRobotAudioOutputSource,
        robotAudioOutputSourceCallback
      )
    );
  }

  // Configure Robot Audio buffers with Wwise buses. PlugIn Ids are set in Wwise project
  // Setup Robot Buffers
  // Note: This is only configured to work with a single robot
  RegisterRobotAudioBuffer( GameObjectType::CozmoAnimation, 1, Bus::BusType::Robot_Bus_1 );
  
  // TEMP: Setup other buses
  RegisterRobotAudioBuffer( GameObjectType::CozmoBus_2, 2, Bus::BusType::Robot_Bus_2 );
  RegisterRobotAudioBuffer( GameObjectType::CozmoBus_3, 3, Bus::BusType::Robot_Bus_3 );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
RobotAudioBuffer* RobotAudioClient::GetRobotAudiobuffer( GameObjectType gameObject )
{
  ASSERT_NAMED( _audioController != nullptr, "RobotAudioClient.GetRobotAudiobuffer.AudioControllerNull" );
  const AudioEngine::AudioGameObject aGameObject = static_cast<const AudioEngine::AudioGameObject>( gameObject );
  return _audioController->GetRobotAudioBufferWithGameObject( aGameObject );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
AudioEngineClient::CallbackIdType RobotAudioClient::PostCozmoEvent( GameEvent::GenericEvent event, AudioEngineClient::CallbackFunc callback )
{
  const CallbackIdType callbackId = PostEvent( event, GameObjectType::CozmoAnimation, callback );
  
  return callbackId;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void RobotAudioClient::CreateAudioAnimation( Animation* anAnimation )
{
  // Check if there is a current animation, if so abort that animation and clean up correctly
  if ( _currentAnimation != nullptr ) {
    PRINT_NAMED_ERROR("RobotAudioClient.CreateAudioAnimation",
                      "CurrentAnimation '%s' state: %s is NOT Null when creating a new animation",
                      _currentAnimation->GetName().c_str(),
                      RobotAudioAnimation::GetStringForAnimationState(_currentAnimation->GetAnimationState()).c_str() );
    _currentAnimation->AbortAnimation();
    ClearCurrentAnimation();
  }

  // Create appropriate animation type for mode
  RobotAudioAnimation* audioAnimation = nullptr;
  switch ( _outputSource ) {
  
    case RobotAudioOutputSource::PlayOnDevice:
      audioAnimation = dynamic_cast<RobotAudioAnimation*>( new RobotAudioAnimationOnDevice( anAnimation, this ) );
      break;
      
    case RobotAudioOutputSource::PlayOnRobot:
      audioAnimation = dynamic_cast<RobotAudioAnimation*>( new RobotAudioAnimationOnRobot( anAnimation, this ) );
      break;
      
    default:
      // Do Nothing
      break;
  }
  
  // Did not create animation
  if ( audioAnimation == nullptr ) {
    return;
  }
  
  // FIXME: This is a temp fix, will remove once we have an Audio Mixer
    audioAnimation->SetRobotVolume( _robotVolume );

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
  Util::SafeDelete(_currentAnimation);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool RobotAudioClient::UpdateAnimationIsReady( TimeStamp_t startTime_ms, TimeStamp_t streamingTime_ms )
{
  // No Animation allow animation to proceed
  if ( !HasAnimation() ) {
    return true;
  }
  
  // Buffer is ready to get the next frame from
  if ( _currentAnimation->GetAnimationState() == RobotAudioAnimation::AnimationState::AudioFramesReady ) {
    return true;
  }
  
  if ( _currentAnimation->GetAnimationState() == RobotAudioAnimation::AnimationState::LoadingStream ) {
    const TimeStamp_t relavantTime_ms = streamingTime_ms - startTime_ms;
    const TimeStamp_t nextEventTime_ms = _currentAnimation->GetNextEventTime_ms();
    if ( relavantTime_ms < nextEventTime_ms ) {
      return true;
    }
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
void RobotAudioClient::SetRobotVolume(float volume)
{
  // Keep On device robot volume (Wwise) in sync with robot volume
  PostParameter(GameParameter::ParameterType::Robot_Volume, volume, GameObjectType::Invalid);
  _robotVolume = volume;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
float RobotAudioClient::GetRobotVolume() const
{
  return _robotVolume;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void RobotAudioClient::SetOutputSource( RobotAudioOutputSource outputSource )
{
  ASSERT_NAMED( _audioController != nullptr, "RobotAudioClient.SetOutputSource.AudioControllerNull" );
  using namespace AudioEngine;
  
  if ( _outputSource == outputSource ) {
    // Do Nothing
    return;
  }
  _outputSource = outputSource;
  
  switch ( _outputSource ) {
    case RobotAudioOutputSource::None:
    case RobotAudioOutputSource::PlayOnDevice:
    {
      // Setup Audio engine to play audio through device
      // Remove GameObject Aux sends
      AudioController::AuxSendList emptyList;
      for ( auto& aKVP : _busConfigurationMap ) {
        const AudioGameObject aGameObject = static_cast<const AudioGameObject>( aKVP.second.gameObject );
        // Set Aux send settings in Audio Engine
        _audioController->SetGameObjectAuxSendValues( aGameObject, emptyList );
      }
    }
      break;
      
    case RobotAudioOutputSource::PlayOnRobot:
    {
      // Setup Audio engine to play audio through device
      // Setup GameObject Aux Sends
      for ( auto& aKVP : _busConfigurationMap ) {
        RobotBusConfiguration& busConfig = aKVP.second;
        const AudioGameObject aGameObject = static_cast<const AudioGameObject>( busConfig.gameObject );
        AudioAuxBusValue aBusValue( static_cast<AudioAuxBusId>( busConfig.bus ), 1.0f );
        AudioController::AuxSendList sendList;
        sendList.emplace_back( aBusValue );
        // Set Aux send settings in Audio Engine
        _audioController->SetGameObjectAuxSendValues( aGameObject, sendList );
        _audioController->SetGameObjectOutputBusVolume( aGameObject, 0.0f );
      }
    }
      break;
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
RobotAudioBuffer* RobotAudioClient::RegisterRobotAudioBuffer( GameObjectType gameObject,
                                                             PluginId_t pluginId,
                                                             Bus::BusType audioBus )
{
  ASSERT_NAMED( _audioController != nullptr, "RobotAudioClient.RegisterRobotAudioBuffer.AudioControllerNull" );
  
  // Create Configuration Struct
  RobotBusConfiguration busConfiguration = { gameObject, pluginId, audioBus };
  const auto it = _busConfigurationMap.emplace( gameObject, busConfiguration );
  if ( !it.second ) {
    // Bus configuration already exist
    PRINT_NAMED_ERROR("RobotAudioClient.RegisterRobotAudioBuffer", "Buss configuration already exist for GameObject: %d",
                      static_cast<uint32_t>(gameObject));
  }
  
  // Setup GameObject with Bus
  AudioEngine::AudioGameObject aGameObject = static_cast<const AudioEngine::AudioGameObject>( gameObject );
  
  // Create Buffer for buses
  return _audioController->RegisterRobotAudioBuffer( aGameObject, pluginId );
}


} // Audio
} // Cozmo
} // Anki

/*
 * File: audioServer.cpp
 *
 * Author: Jordan Rivas
 * Created: 11/09/2015
 *
 * Description: The server multiplexes AudioClientConnections messages to perform AudioController functionality. The
 *              connection’s Audio Post Messages is decoded to perform audio tasks. When the AudioController performs a
 *              callback it is encoded into Audio Callback Messages and is passed to the appropriate connection to
 *              transport to its client. The AudioServer owns the Audio Connection.
 *
 * Copyright: Anki, Inc. 2015
 */

#include "anki/cozmo/basestation/audio/audioServer.h"
#include "anki/cozmo/basestation/audio/audioController.h"
#include "anki/cozmo/basestation/audio/audioClientConnection.h"
#include "clad/audio/audioMessage.h"
#include "clad/audio/audioCallbackMessage.h"
#include <util/helpers/templateHelpers.h>
#include <util/logging/logging.h>
#include <unordered_map>


namespace Anki {
namespace Cozmo {
namespace Audio {

using namespace AudioEngine;
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
AudioServer::AudioServer( AudioController* audioController ) :
  _audioController( audioController )
{
  ASSERT_NAMED( nullptr != _audioController, "AudioServer Audio Controller is NULL");

  // Register CLAD Game Objects
  RegisterCladGameObjectsWithAudioController();
}
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
AudioServer::~AudioServer()
{
  Util::SafeDelete( _audioController );
  
  for ( auto& kvp : _clientConnections ) {
    Util::SafeDelete( kvp.second );
  }
  _clientConnections.clear();
}
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
AudioServer::ConnectionIdType AudioServer::RegisterClientConnection( AudioClientConnection* clientConnection )
{
  // Setup Client Connection with Server
  ASSERT_NAMED( nullptr != clientConnection, "AudioServer.RegisterClientConnection Client Connection is NULL!");
  ConnectionIdType connectionId = GetNewClientConnectionId();
  clientConnection->SetConnectionId( connectionId );
  clientConnection->SetAudioServer( this );

  _clientConnections.emplace( connectionId, clientConnection );
  
  return connectionId;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
const AudioClientConnection* AudioServer::GetConnection( uint8_t connectionId ) const
{
  const auto it = _clientConnections.find( connectionId );
  if ( it != _clientConnections.end() ) {
    return it->second;
  }
  return nullptr;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void AudioServer::ProcessMessage( const PostAudioEvent& message, ConnectionIdType connectionId )
{
  // Decode Event Message
  const AudioEventID eventId = static_cast<AudioEventID>( message.audioEvent );
  const AudioGameObject objectId = static_cast<AudioGameObject>( message.gameObject );
  const AudioEngine::AudioCallbackFlag callbackFlags = ConvertCallbackFlagType( message.callbackFlag );
  const uint16_t callbackId = message.callbackId;
  // Perform Event
  AudioPlayingID playingId = kInvalidAudioPlayingID;
  if ( AudioEngine::AudioCallbackFlag::NoCallback != callbackFlags ) {
    AudioCallbackContext* callbackContext = new AudioCallbackContext();
    // Set callback flags
    callbackContext->SetCallbackFlags( callbackFlags );
    // Register callbacks for event
    callbackContext->SetEventCallbackFunc( [this, connectionId, callbackId]
                                           ( const AudioCallbackContext* thisContext,
                                             const AudioCallbackInfo& callbackInfo )
    {
      PerformCallback( connectionId, callbackId, callbackInfo );
    } );
    
    playingId = _audioController->PostAudioEvent( eventId, objectId, callbackContext );
  }
  else {
    playingId = _audioController->PostAudioEvent( eventId, objectId );
  }
  
  if ( playingId == kInvalidAudioPlayingID ) {
    PRINT_NAMED_ERROR( "AudioServer.ProcessMessage", "Unable To Play Event %s on GameObject %d",
                       EnumToString( message.audioEvent ), message.gameObject );
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void AudioServer::ProcessMessage( const PostAudioGameState& message, ConnectionIdType connectionId )
{
  // Decode Game State Message
  const AudioStateGroupId groupId = static_cast<AudioStateGroupId>( message.gameStateGroup );
  const AudioStateId stateId = static_cast<AudioStateId>( message.gameState );
  // Perform Game State
  const bool success = _audioController->SetState( groupId, stateId );
  if ( !success ) {
    PRINT_NAMED_ERROR( "AudioServer.ProcessMessage", "Unable To Set State %s : %s",
                       EnumToString( message.gameStateGroup ), EnumToString( message.gameState ) );
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void AudioServer::ProcessMessage( const PostAudioSwitchState& message, ConnectionIdType connectionId )
{
  // Decode Switch State Message
  const AudioSwitchGroupId groupId = static_cast<AudioStateGroupId>( message.switchStateGroup);
  const AudioSwitchStateId stateId = static_cast<AudioSwitchStateId>( message.switchState );
  const AudioGameObject objectId = static_cast<AudioGameObject>( message.gameObject );
  // Perform Switch State
  const bool success = _audioController->SetSwitchState( groupId, stateId, objectId );
  if ( !success ) {
    PRINT_NAMED_ERROR( "AudioServer.ProcessMessage", "Unable To Set Switch State %s : %s on GameObject %s",
                       EnumToString( message.switchStateGroup ), EnumToString( message.switchState ),
                       EnumToString( message.gameObject ) );
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void AudioServer::ProcessMessage( const PostAudioParameter& message, ConnectionIdType connectionId )
{
  // Decode Parameter Message
  const AudioParameterId parameterId = static_cast<AudioParameterId>( message.parameter );
  const AudioRTPCValue value = static_cast<AudioRTPCValue>( message.parameterValue );
  const AudioGameObject objectId = static_cast<AudioGameObject>( message.gameObject );
  const AudioTimeMs duration = static_cast<AudioTimeMs>( message.timeInMilliSeconds );
  
  // Translate Curve Enum types
  static const std::unordered_map< Anki::Cozmo::Audio::CurveType,
                                   AudioEngine::AudioCurveType,
                                   Util::EnumHasher > curveTypeMap
  {
    { CurveType::Linear,            AudioCurveType::Linear },
    { CurveType::SCurve,            AudioCurveType::SCurve },
    { CurveType::InversedSCurve,    AudioCurveType::InversedSCurve },
    { CurveType::Sine,              AudioCurveType::Sine },
    { CurveType::SineReciprocal,    AudioCurveType::SineReciprocal },
    { CurveType::Exp1,              AudioCurveType::Exp1 },
    { CurveType::Exp3,              AudioCurveType::Exp3 },
    { CurveType::Log1,              AudioCurveType::Log1 },
    { CurveType::Log3,              AudioCurveType::Log3 },
  };
  
  AudioCurveType curve = AudioCurveType::Linear;
  const auto& it = curveTypeMap.find( message.curve );
  if ( it != curveTypeMap.end() ) {
    curve = it->second;
  }
  else {
    PRINT_NAMED_ERROR("AudioServer.ProcessMessage", "Can NOT find Parameter Curve Type %s", EnumToString( message.curve ));
  }
  
  // Perform Parameter
  const bool success = _audioController->SetParameter( parameterId, value, objectId, duration, curve );
  if ( !success ) {
    PRINT_NAMED_ERROR( "AudioServer.ProcessMessage",
                       "Unable To Set Parameter %s to Value %f on GameObject %s with duration %d milliSeconds with \
                       curve type %s",
                       EnumToString( message.parameter ), message.parameterValue, EnumToString( message.gameObject ),
                       message.timeInMilliSeconds, EnumToString( message.curve ) );
  }
}
  
  
// Private
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
AudioServer::ConnectionIdType AudioServer::GetNewClientConnectionId()
{
  // This only works for 255 Clients
  ++_previousClientConnectionId;
  ASSERT_NAMED( 0 != _previousClientConnectionId,
                "AudioServer.GetNewClientConnectionId Invalid ConnectionId, this can be caused by adding more then 255 \
                clients");
  
  return _previousClientConnectionId;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void AudioServer::PerformCallback( ConnectionIdType connectionId,
                                   uint16_t callbackId,
                                   const AudioEngine::AudioCallbackInfo& callbackInfo )
{
  PRINT_NAMED_INFO( "AudioServer.PerformCallback", "Event Callback ClientId %d CalbackId: %d - %s",
                    connectionId, callbackId, callbackInfo.GetDescription().c_str());
  
  const AudioClientConnection* connection = GetConnection( connectionId );
  if ( nullptr != connection ) {
    using namespace AudioEngine;
    
    switch ( callbackInfo.callbackType ) {
        
      case AudioEngine::AudioCallbackType::Invalid:
        // Do nothing
        PRINT_NAMED_ERROR( "AudioServer.PerformCallback", "Invalid Callback" );
        break;
        
      case AudioCallbackType::Duration:
      {
        const AudioDurationCallbackInfo& info = static_cast<const AudioDurationCallbackInfo&>( callbackInfo );
        AudioCallbackDuration msg( callbackId,
                                   info.duration,
                                   info.estimatedDuration,
                                   info.audioNodeId,
                                   info.isStreaming );
        connection->PostCallback( msg );
      }
        break;
        
      case AudioCallbackType::Marker:
      {
        const AudioMarkerCallbackInfo& info = static_cast<const AudioMarkerCallbackInfo&>( callbackInfo );
        AudioCallbackMarker msg( callbackId,
                                 info.identifier,
                                 info.position,
                                 info.labelStr );
        connection->PostCallback( msg );
      }
        break;
        
      case AudioCallbackType::Complete:
      {
        AudioCallbackComplete msg( callbackId );
        connection->PostCallback( msg );
      }
        break;
        
      case AudioCallbackType::Error:
      {
        const AudioErrorCallbackInfo& info = static_cast<const AudioErrorCallbackInfo&>( callbackInfo );
        AudioCallbackError msg( callbackId,
                                ConvertErrorCallbackType( info.error ) );
        connection->PostCallback( msg );
      }
        break;
      
    }
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void AudioServer::RegisterCladGameObjectsWithAudioController()
{
  // Enumerate through GameObjectType Enums
  for ( uint32_t aGameObj = static_cast<uint32_t>(GameObjectType::Default);
        aGameObj < static_cast<uint32_t>(GameObjectType::End);
        ++aGameObj) {
    // Register GameObjectType
    bool success = _audioController->RegisterGameObject( static_cast<AudioGameObject>(aGameObj),
                                                         std::string(EnumToString(static_cast<GameObjectType>(aGameObj)) ));
    if (!success) {
      PRINT_NAMED_ERROR( "AudioServer.RegisterCladGameObjectsWithAudioController",
                         "Registering GameObjectId: %ul - %s was unsuccessful",
                         aGameObj, EnumToString(static_cast<GameObjectType>(aGameObj)) );
    }
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
AudioEngine::AudioCallbackFlag AudioServer::ConvertCallbackFlagType( const Anki::Cozmo::Audio::AudioCallbackFlag flags ) const
{
  AudioEngine::AudioCallbackFlag engineFlags = AudioEngine::AudioCallbackFlag::NoCallback;
  
  if ( ((uint8_t)AudioCallbackFlag::EventDuration & (uint8_t)flags) == (uint8_t)AudioCallbackFlag::EventDuration ) {
    engineFlags =  (AudioEngine::AudioCallbackFlag)( engineFlags | AudioEngine::AudioCallbackFlag::Duration );
  }
  
  if ( ((uint8_t)AudioCallbackFlag::EventMarker & (uint8_t)flags) == (uint8_t)AudioCallbackFlag::EventMarker ) {
    engineFlags =  (AudioEngine::AudioCallbackFlag)( engineFlags | AudioEngine::AudioCallbackFlag::Marker );
  }
  
  if ( ((uint8_t)AudioCallbackFlag::EventComplete & (uint8_t)flags) == (uint8_t)AudioCallbackFlag::EventComplete ) {
    engineFlags =  (AudioEngine::AudioCallbackFlag)( engineFlags | AudioEngine::AudioCallbackFlag::Complete );
  }
  return engineFlags;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Anki::Cozmo::Audio::CallbackErrorType AudioServer::ConvertErrorCallbackType( const AudioEngine::AudioCallbackErrorType errorType ) const
{
  CallbackErrorType error = CallbackErrorType::Invalid;
  
  switch ( errorType ) {
      
    case AudioEngine::AudioCallbackErrorType::Invalid:
      // Do nothing
      break;
      
    case AudioEngine::AudioCallbackErrorType::EventFailed:
      error = CallbackErrorType::EventFailed;
      break;
      
    case AudioEngine::AudioCallbackErrorType::Starvation:
      error = CallbackErrorType::Starvation;
      break;
  }
  
  return error;
}

  
} // Audio
} // Cozmo
} // Anki

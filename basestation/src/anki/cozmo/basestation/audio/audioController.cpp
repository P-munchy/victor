/*
 * File: audioController.cpp
 *
 * Author: Jordan Rivas
 * Created: 11/09/2015
 *
 * Description: This is responsible for instantiating the Audio Engine and handling basic and app level audio
 *              functionality.
 *
 * Copyright: Anki, Inc. 2015
 *
 */

#include "anki/cozmo/basestation/audio/audioController.h"
#include "anki/cozmo/basestation/audio/robotAudioBuffer.h"
#include "anki/common/basestation/utils/data/dataPlatform.h"
#include "clad/types/animationKeyFrames.h"
#include "clad/audio/audioParameterTypes.h"
#include <util/dispatchQueue/dispatchQueue.h>
#include <util/helpers/templateHelpers.h>
#include <util/logging/logging.h>
#include <unordered_map>

#if CozmoPlugInDebugLogs
#include <util/time/universalTime.h>
#endif
// Allow the build to include/exclude the audio libs
//#define EXCLUDE_ANKI_AUDIO_LIBS 0

#ifndef EXCLUDE_ANKI_AUDIO_LIBS

#define USE_AUDIO_ENGINE 1
#include <DriveAudioEngine/audioEngineController.h>
#include <DriveAudioEngine/PlugIns/cozmoPlugIn.h>
#else

// If we're excluding the audio libs, don't link any of the audio engine
#define USE_AUDIO_ENGINE 0

#endif


namespace Anki {
namespace Cozmo {
namespace Audio {
  
using namespace AudioEngine;
  

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
AudioController::AudioController( Util::Data::DataPlatform* dataPlatfrom )
{
#if USE_AUDIO_ENGINE
  {
    _audioEngine = new AudioEngineController();
    const std::string assetPath = dataPlatfrom->pathToResource(Util::Data::Scope::Resources, "sound/" );

    // Set Language Local
    const AudioLocaleType localeType = AudioLocaleType::EnglishUS;
    
    _audioEngine->SetLanguageLocale( localeType );
    
    _isInitialized = _audioEngine->Initialize( assetPath );
    
    // If we're using the audio engine, assert that it was successfully initialized.
    ASSERT_NAMED(_isInitialized, "AudioController.Initialize Audio Engine fail");
  }
#endif // USE_AUDIO_ENGINE
  
  // The audio engine was initialized correctly, so now let's setup everything else
  if ( _isInitialized )
  {
    ASSERT_NAMED( !_taskHandle, "AudioController.Initialize Invalid Task Handle" );
#if USE_AUDIO_ENGINE
    
    // Setup CozmoPlugIn & RobotAudioBuffer
    _cozmoPlugIn = new CozmoPlugIn( static_cast<uint32_t>( AnimConstants::AUDIO_SAMPLE_RATE ), static_cast<uint16_t>( AnimConstants::AUDIO_SAMPLE_SIZE ) );
    _robotAudioBuffer = new RobotAudioBuffer();
    
    // Setup Callbacks
    _cozmoPlugIn->SetCreatePlugInCallback( [this] () {
      PRINT_NAMED_INFO( "AudioController.Initialize", "Create PlugIn Callback!" );
      assert( nullptr != _robotAudioBuffer );
      _robotAudioBuffer->PrepareAudioBuffer();
      
#if CozmoPlugInDebugLogs
      _plugInLog.emplace_back( TimeLog( LogEnumType::CreatePlugIn, "", Util::Time::UniversalTime::GetCurrentTimeInNanoseconds() ));
#endif
    } );

    _cozmoPlugIn->SetDestroyPluginCallback( [this] () {
      PRINT_NAMED_INFO( "AudioController.Initialize", "Create Destroy Callback!" );
      assert( nullptr != _robotAudioBuffer );
      // Done with voice clear audio buffer
      _robotAudioBuffer->ClearCache();
      
#if CozmoPlugInDebugLogs
      _plugInLog.emplace_back( TimeLog( LogEnumType::DestoryPlugIn, "", Util::Time::UniversalTime::GetCurrentTimeInNanoseconds() ));
      PrintPlugInLog();
#endif
    } );
    
    _cozmoPlugIn->SetProcessCallback( [this] ( const AudioEngine::CozmoPlugIn::CozmoPlugInAudioBuffer& buffer )
    {
      _robotAudioBuffer->UpdateBuffer( buffer.frames, buffer.frameCount );

#if CozmoPlugInDebugLogs
      _plugInLog.emplace_back( TimeLog( LogEnumType::Update, "FrameCount: " + std::to_string(buffer.frameCount) , Util::Time::UniversalTime::GetCurrentTimeInNanoseconds() ));
#endif
    });
    
    const bool success = _cozmoPlugIn->RegisterPlugin();
    if ( ! success ) {
      PRINT_NAMED_ERROR( "AudioController.Initialize", "Fail to Regist Cozmo PlugIn");
    }
    
    // FIXME: Temp fix to load audio banks
    AudioBankList bankList = {
      "Init.bnk",
      "Music.bnk",
      "UI.bnk",
      "VO_Cozmo.bnk",
      "Cozmo_Screen",
      "Cozmo_Servo",
      "Dev_Debug.bnk",
    };
    const std::string sceneTitle = "InitScene";
    AudioScene initScene = AudioScene( sceneTitle, AudioEventList(), bankList );
    
    _audioEngine->RegisterAudioScene( std::move(initScene) );
    
    _audioEngine->LoadAudioScene( sceneTitle );
    
    StartUpSetDefaults();

#endif
  
    // Setup our update method to be called periodically
    _dispatchQueue = Util::Dispatch::Create();
    const std::chrono::milliseconds sleepDuration = std::chrono::milliseconds(10);
    _taskHandle = Util::Dispatch::ScheduleCallback( _dispatchQueue, sleepDuration, std::bind( &AudioController::Update, this ) );
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
AudioController::~AudioController()
{
  if ( _taskHandle && _taskHandle->IsValid() )
  {
    _taskHandle->Invalidate();
  }
  Util::Dispatch::Release( _dispatchQueue );
  
  ClearGarbageCollector();
  
#if USE_AUDIO_ENGINE
  {
    Util::SafeDelete( _audioEngine );
  }
#endif
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
AudioEngine::AudioPlayingID AudioController::PostAudioEvent( const std::string& eventName,
                                                             AudioEngine::AudioGameObject gameObjectId,
                                                             AudioEngine::AudioCallbackContext* callbackContext )
{
  AudioPlayingID playingId = kInvalidAudioPlayingID;
#if USE_AUDIO_ENGINE
  if ( _isInitialized ) {
    playingId = _audioEngine->PostEvent( eventName, gameObjectId, callbackContext );
    if ( kInvalidAudioPlayingID != playingId &&
        nullptr != callbackContext ) {
      callbackContext->SetPlayId( playingId );
      callbackContext->SetDestroyCallbackFunc( [this] ( const AudioCallbackContext* thisContext )
                                              {
                                                MoveCallbackContextToGarbageCollector( thisContext );
                                              } );
      _eventCallbackContexts.emplace( playingId, callbackContext );
    }
    else if ( kInvalidAudioPlayingID == playingId &&
             nullptr != callbackContext ) {
      // Event Failed and there is a callback
      // Perform Error callback for Event Failed and delete callbackContext
      const AudioEventID eventId = _audioEngine->GetAudioHashFromString( eventName );
      callbackContext->HandleCallback( AudioErrorCallbackInfo( gameObjectId,
                                                               kInvalidAudioPlayingID,
                                                               eventId,
                                                               AudioEngine::AudioCallbackErrorType::EventFailed ) );
      Util::SafeDelete( callbackContext );
    }
  }
#endif
  return playingId;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
AudioEngine::AudioPlayingID AudioController::PostAudioEvent( AudioEngine::AudioEventID eventId,
                                                             AudioEngine::AudioGameObject gameObjectId,
                                                             AudioEngine::AudioCallbackContext* callbackContext  )
{
  AudioPlayingID playingId = kInvalidAudioPlayingID;
#if USE_AUDIO_ENGINE
  if ( _isInitialized ) {
    playingId = _audioEngine->PostEvent( eventId, gameObjectId, callbackContext );
    if ( kInvalidAudioPlayingID != playingId &&
         nullptr != callbackContext ) {
      callbackContext->SetPlayId( playingId );
      callbackContext->SetDestroyCallbackFunc( [this] ( const AudioCallbackContext* thisContext )
      {
        MoveCallbackContextToGarbageCollector( thisContext );
      } );
      _eventCallbackContexts.emplace( playingId, callbackContext );
    }
    else if ( kInvalidAudioPlayingID == playingId &&
              nullptr != callbackContext ) {
      // Event Failed and there is a callback
      // Perform Error callback for Event Failed and delete callbackContext
      callbackContext->HandleCallback( AudioErrorCallbackInfo( gameObjectId,
                                                               kInvalidAudioPlayingID,
                                                               eventId,
                                                               AudioEngine::AudioCallbackErrorType::EventFailed ) );
      Util::SafeDelete( callbackContext );
    }
    
#if CozmoPlugInDebugLogs
    _plugInLog.emplace_back( TimeLog( LogEnumType::Post, "EventId: " + std::to_string(eventId) , Util::Time::UniversalTime::GetCurrentTimeInNanoseconds() ));
#endif
  }
#endif
  return playingId;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void AudioController::StopAllAudioEvents( AudioEngine::AudioGameObject gameObject )
{
#if USE_AUDIO_ENGINE
  if ( _isInitialized ) {
    _audioEngine->StopAllAudioEvents( gameObject );
  }
#endif
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool AudioController::SetState( AudioEngine::AudioStateGroupId stateGroupId,
                                AudioEngine::AudioStateId stateId ) const
{
  bool success = false;
#if USE_AUDIO_ENGINE
  if ( _isInitialized ) {
    _audioEngine->SetState( stateGroupId, stateId );
  }
#endif
  return success;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool AudioController::SetSwitchState( AudioEngine::AudioSwitchGroupId switchGroupId,
                                      AudioEngine::AudioSwitchStateId switchStateId,
                                      AudioEngine::AudioGameObject gameObject ) const
{
  bool success = false;
#if USE_AUDIO_ENGINE
  if ( _isInitialized ) {
    _audioEngine->SetSwitch( switchGroupId, switchStateId, gameObject );
  }
#endif
  return success;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool AudioController::SetParameter( AudioEngine::AudioParameterId parameterId,
                                    AudioEngine::AudioRTPCValue rtpcValue,
                                    AudioEngine::AudioGameObject gameObject,
                                    AudioEngine::AudioTimeMs valueChangeDuration,
                                    AudioEngine::AudioCurveType curve ) const
{
  bool success = false;
#if USE_AUDIO_ENGINE
  if ( _isInitialized ) {
    if ( AudioEngine::kInvalidAudioGameObject == gameObject ) {
      // Set Global RTPC values
      gameObject = _audioEngine->GetDefaultGameObjectId();
    }
    _audioEngine->SetRTPCValue( parameterId, rtpcValue, gameObject, valueChangeDuration, curve );
  }
#endif
  return success;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool AudioController::RegisterGameObject( AudioEngine::AudioGameObject gameObjectId, std::string gameObjectName )
{
  bool success = false;
  
#if USE_AUDIO_ENGINE
  if ( _isInitialized ) {
    success = _audioEngine->RegisterAudioGameObject( gameObjectId, gameObjectName );
  }
#endif
  
  return success;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// THIS IS TEMP
void AudioController::StartUpSetDefaults()
{
  SetParameter( static_cast<AudioEngine::AudioParameterId>( GameParameter::ParameterType::Robot_Volume ), 0.2, kInvalidAudioGameObject );
}


// Private
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void AudioController::Update()
{
#if USE_AUDIO_ENGINE
  // NOTE: Don't need time delta
  _audioEngine->Update( 0.0 );
#endif
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void AudioController::MoveCallbackContextToGarbageCollector( const AudioEngine::AudioCallbackContext* callbackContext )
{
  ASSERT_NAMED( nullptr != callbackContext, "AudioController.MoveCallbackContextToGarbageCollector Callback Context is \
                NULL");
  PRINT_NAMED_INFO( "AudioController.MoveCallbackContextToGarbageCollector", "Add PlayId: %d Callback Context to \
                    garbagecollector", callbackContext->GetPlayId() );
  // FIXME: Is there a better way of doing this?
  ClearGarbageCollector();
  
  // Move context from EventCallbackMap to CallbackGarbageCollector
  const auto it = _eventCallbackContexts.find( callbackContext->GetPlayId() );
  if ( it != _eventCallbackContexts.end() ) {
    ASSERT_NAMED( it->second == callbackContext, "AudioController.MoveCallbackContextToGarbageCollector PlayId dose \
                  NOT match Callback Context" );
    // Move to GarbageCollector
    it->second->ClearCallbacks();
    _callbackGarbageCollector.emplace_back( it->second );
    _eventCallbackContexts.erase( it );
  }
  else {
    ASSERT_NAMED( it != _eventCallbackContexts.end(), ( "AudioController.MoveCallbackContextToGarbageCollector Can NOT \
                  find PlayId: " + std::to_string( callbackContext->GetPlayId() )).c_str() );
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void AudioController::ClearGarbageCollector()
{
  std::for_each(_callbackGarbageCollector.begin(),
                _callbackGarbageCollector.end(),
                [](AudioEngine::AudioCallbackContext* aContext){ Util::SafeDelete( aContext ); } );
  _callbackGarbageCollector.clear();
}


// Debug Cozmo PlugIn Logs
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
#if CozmoPlugInDebugLogs

double ConvertToMiliSec(unsigned long long int miliSeconds) {
  return (double)miliSeconds / 1000000.0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void AudioController::PrintPlugInLog() {
  
  unsigned long long int postTime = 0, createTime = 0, updateTime = 0;
  bool isFirstUpdateLog = false;
  for (auto& aLog : _plugInLog) {
    switch (aLog.LogType) {
      case LogEnumType::Post:
      {
        postTime = aLog.TimeInNanoSec;
        
        printf("----------------------------------------------\n \
               Post Event %s - time: %f ms\n", aLog.Msg.c_str(), ConvertToMiliSec( aLog.TimeInNanoSec ));
      }
        break;
        
      case LogEnumType::CreatePlugIn:
      {
        createTime = aLog.TimeInNanoSec;
        isFirstUpdateLog = true;
        
        printf("Create PlugIn %s - time: %f ms\n \
               - Post -> Create time delta = %f ms\n", aLog.Msg.c_str(), ConvertToMiliSec( aLog.TimeInNanoSec ), ConvertToMiliSec( createTime - postTime ));
      }
        break;
        
      case LogEnumType::Update:
      {
        printf("Update %s - time: %f ms\n", aLog.Msg.c_str(), ConvertToMiliSec( aLog.TimeInNanoSec ));
        
        
        if ( isFirstUpdateLog ) {
          printf("- Post -> Update time delta = %f ms\n \
                  - Create -> Update time delta = %f ms\n", ConvertToMiliSec( aLog.TimeInNanoSec - postTime ), ConvertToMiliSec( aLog.TimeInNanoSec - createTime ));
        }
        else {
          printf("- Previous Update -> Update time delta = %f ms\n", ConvertToMiliSec( aLog.TimeInNanoSec - updateTime ));
          
        }
        
        updateTime = aLog.TimeInNanoSec;
        isFirstUpdateLog = false;
      }
        break;
        
      case LogEnumType::DestoryPlugIn:
      {
        printf("Destory Plugin %s - time: %f ms\n \
               ----------------------------------------------\n", aLog.Msg.c_str(), ConvertToMiliSec( aLog.TimeInNanoSec ));
      }
        break;
        
      default:
        break;
    }
  }
  _plugInLog.clear();
}
#endif

  
} // Audio
} // Cozmo
} // Anki

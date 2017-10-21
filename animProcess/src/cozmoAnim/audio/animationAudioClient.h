/*
 * File: animationAudioClient.h
 *
 * Author: Jordan Rivas
 * Created: 09/12/17
 *
 * Description: Animation Audio Client is the interface to perform animation audio specific tasks. Provided a
 *              RobotAudioKeyFrame to handle the necessary audio functionality for that frame. It also provides an
 *              interface to abort animation audio and update (a.k.a. “tick”) the Audio Engine each frame.
 *
 * Copyright: Anki, Inc. 2017
 */


#ifndef __Anki_Cozmo_AnimationAudioClient_H__
#define __Anki_Cozmo_AnimationAudioClient_H__

#include "audioEngine/audioTypeTranslator.h"
#include <set>
#include <mutex>


namespace Anki {
namespace AudioEngine {
struct AudioCallbackInfo;
}
namespace Util {
class RandomGenerator;
}
namespace Cozmo {
class RobotAudioKeyFrame;

namespace Audio {
class CozmoAudioController;


class AnimationAudioClient {

public:

  static const char* kAudioLogChannelName;

  AnimationAudioClient( CozmoAudioController* audioController );

  ~AnimationAudioClient();
  
  // Tick Audio Engine each animation frame
  void Update() const;

  // Perform functionality for frame
  void PlayAudioKeyFrame( const RobotAudioKeyFrame& keyFrame );
  
  // Stop all animation audio
  void StopCozmoEvent();
  
  // Check if there is an event being performed
  bool HasActiveEvents() const;
  
  // Control Robot's master volume
  // Volume is [0.0 - 1.0]
  void SetRobotMasterVolume( AudioEngine::AudioRTPCValue volume,
                             AudioEngine::AudioTimeMs timeInMilliSeconds = 0,
                             AudioEngine::AudioCurveType curve = AudioEngine::AudioCurveType::Linear );


private:
  
  CozmoAudioController*  _audioController = nullptr;
  std::set<AudioEngine::AudioPlayingId> _activeEvents;
  mutable std::mutex      _lock;
  
  // Perform an event
  AudioEngine::AudioPlayingId PostCozmoEvent( AudioMetaData::GameEvent::GenericEvent event );

  // Update parameters for a event play id
  bool SetCozmoEventParameter( AudioEngine::AudioPlayingId playId,
                               AudioMetaData::GameParameter::ParameterType parameter,
                               AudioEngine::AudioRTPCValue value ) const;
  
  // Perform Event callback, used by "PostCozmoEvent()"
  void CozmoEventCallback( const AudioEngine::AudioCallbackInfo& callbackInfo );
  
  // Track current playing events
  void AddActiveEvent( AudioEngine::AudioPlayingId playId );
  void RemoveActiveEvent( AudioEngine::AudioPlayingId playId );

};


}
}
}

#endif /* __Anki_Cozmo_AnimationAudioClient_H__ */

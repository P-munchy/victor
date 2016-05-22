/*
 * File: robotAudioClient.h
 *
 * Author: Jordan Rivas
 * Created: 11/09/2015
 *
 * Description: This Client handles the Robot’s specific audio needs. It is a sub-class of AudioEngineClient.
 *
 * Copyright: Anki, Inc. 2015
 */

#ifndef __Basestation_Audio_RobotAudioClient_H__
#define __Basestation_Audio_RobotAudioClient_H__

#include "anki/cozmo/basestation/audio/audioEngineClient.h"
#include "anki/cozmo/basestation/audio/robotAudioAnimation.h"
#include "util/helpers/templateHelpers.h"
#include <unordered_map>


namespace Anki {
namespace Cozmo {

class Robot;
class Animation;
  
namespace Audio {
  
class AudioController;
  
class RobotAudioBuffer;
  
class RobotAudioClient : public AudioEngineClient
{
public:

  // Animation audio modes
  enum class RobotAudioOutputSource : uint8_t {
    None,           // No audio
    PlayOnDevice,   // Play on Device - This is not perfectly synced to animations
    PlayOnRobot     // Play on Robot by using Hijack Audio plug-in to get audio stream from Wwise
  };

  // Default Constructor
  RobotAudioClient( Robot* robot );
  
  // The the audio buffer for the corresponding Game Object
  RobotAudioBuffer* GetRobotAudiobuffer( GameObjectType gameObject );

  // Post Cozmo specific Audio events
  CallbackIdType PostCozmoEvent( GameEvent::GenericEvent event, AudioEngineClient::CallbackFunc callback = nullptr );

   // Create an Audio Animation for a specific animation. Only one animation can be played at a time
  void CreateAudioAnimation( Animation* anAnimation );

  RobotAudioAnimation* GetCurrentAnimation() { return _currentAnimation; }

  // Delete audio animation
  // Note: This Does not Abort the animation
  void ClearCurrentAnimation();

  bool HasAnimation() const { return _currentAnimation != nullptr; }

  // Return true if there is no animation or animation is ready
  bool UpdateAnimationIsReady();

  // Check Animation States to see if it's completed
  bool AnimationIsComplete();
  
  // Robot Volume Value is between ( 0.0 - 1.0 )
  void SetRobotVolume(float volume);
  float GetRobotVolume() const;

  // Must be called after RegisterRobotAudioBuffer() to properly setup robot audio signal flow
  void SetOutputSource( RobotAudioOutputSource outputSource );
  RobotAudioOutputSource GetOutputSource() const { return _outputSource; }

private:
  
  using PluginId_t = uint32_t;
  struct RobotBusConfiguration {
    GameObjectType  gameObject;
    PluginId_t      pluginId;
    Bus::BusType    bus;
  };
  
  // Handle to parent Robot
  Robot* _robot = nullptr;
  
  // Provides robot audio buffer
  AudioController* _audioController = nullptr;
  
  // Audio Animation Object to provide audio frames to Animation
  RobotAudioAnimation* _currentAnimation = nullptr;
  
  // Current Output source
  RobotAudioOutputSource _outputSource = RobotAudioOutputSource::None;
  
  // Store Bus configurations
  std::unordered_map<GameObjectType, RobotBusConfiguration, Util::EnumHasher> _busConfigurationMap;
  
  // Create Audio Buffer for the corresponding Game Object
  RobotAudioBuffer* RegisterRobotAudioBuffer( GameObjectType gameObject, PluginId_t pluginId, Bus::BusType bus );
  
  // Keep current robot volume
  float _robotVolume = 0.0f;
  
};
  
} // Audio
} // Cozmo
} // Anki



#endif /* __Basestation_Audio_RobotAudioClient_H__ */

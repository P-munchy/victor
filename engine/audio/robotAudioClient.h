/*
 * File: robotAudioClient.h
 *
 * Author: Jordan Rivas
 * Created: 11/09/2015
 *
 * Description: This Client handles the Robot’s specific audio needs. It is a subclass of AudioEngineClient.
 *
 * Copyright: Anki, Inc. 2015
 */

#ifndef __Basestation_Audio_RobotAudioClient_H__
#define __Basestation_Audio_RobotAudioClient_H__


#include "engine/audio/audioEngineClient.h"
#include "engine/audio/robotAudioAnimation.h"
#include "util/helpers/templateHelpers.h"
#include <unordered_map>
#include <queue>


namespace Anki {
  
namespace Util {
class RandomGenerator;
namespace Dispatch {
class Queue;
}
}
namespace Cozmo {
class Robot;
class Animation;
  
namespace Audio {  
class CozmoAudioController;
class RobotAudioBuffer;
  
class RobotAudioClient : public AudioEngineClient {

public:

  // !!! Be sure to update RobotAudioOutputSourceCLAD in messageGameToEngine.clad if this is changed !!!
  // Animation audio modes
  enum class RobotAudioOutputSource : uint8_t {
    None,           // No audio
    PlayOnDevice,   // Play on Device - This is not perfectly synced to animations
    PlayOnRobot     // Play on Robot by using Hijack Audio plug-in to get audio stream from Wwise
  };
  
  static const char* kRobotAudioLogChannelName;

  // Default Constructor
  RobotAudioClient( Robot* robot );

  // Destructor
  ~RobotAudioClient();
    
  // Audio buffer for the corresponding Game Object
  virtual RobotAudioBuffer* GetRobotAudioBuffer( AudioMetaData::GameObjectType gameObject );

  // Post Cozmo specific Audio events
  using CozmoPlayId = uint32_t;
  static constexpr CozmoPlayId kInvalidCozmoPlayId = 0;
  
  using CozmoEventCallbackFunc = std::function<void( const AudioEngine::AudioCallbackInfo& callbackInfo )>;
  CozmoPlayId PostCozmoEvent( AudioMetaData::GameEvent::GenericEvent event,
                              AudioMetaData::GameObjectType GameObjId = AudioMetaData::GameObjectType::Invalid,
                              CozmoEventCallbackFunc callbackFunc = nullptr ) const;
  
  bool SetCozmoEventParameter( CozmoPlayId playId, AudioMetaData::GameParameter::ParameterType parameter, float value ) const;
  
  void StopCozmoEvent( AudioMetaData::GameObjectType gameObjId );
  
  // Process all events in Audio Engine
  void ProcessEvents() const;

  // Perform all callbacks in queue
  void FlushAudioCallbackQueue();
  
  void PostRobotSwitchState(const AudioMetaData::SwitchState::SwitchGroupType switchGroup,
                            const AudioMetaData::SwitchState::GenericSwitch switchState);
  
  void PostRobotParameter(const AudioMetaData::GameParameter::ParameterType parameter,
                          const float parameterValue) const;
  
  
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// vvvvvvvvvvvv Deprecated vvvvvvvvvvvvvvvv
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  
  // Create an Audio Animation for a specific animation. Only one animation can be played at a time.
  // FIXME: Remove after new animation code goes in
  void CreateAudioAnimation( Animation* anAnimation );

  // FIXME: Remove after new animation code goes in
  RobotAudioAnimation* GetCurrentAnimation() { return _currentAnimation; }

  // Delete audio animation
  // Note: This Does not Abort the animation
  // FIXME: Remove after new animation code goes in
  void ClearCurrentAnimation();

  // FIXME: Remove after new animation code goes in
  bool HasAnimation() const { return _currentAnimation != nullptr; }

  // Return true if there is no animation or animation is ready
  // FIXME: Remove after new animation code goes in
  bool UpdateAnimationIsReady( TimeStamp_t startTime_ms, TimeStamp_t streamingTime_ms );

  // Check Animation States to see if it's completed
  // FIXME: Remove after new animation code goes in
  bool AnimationIsComplete();

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// ^^^^^^^^^^^^ Deprecated ^^^^^^^^^^^^^
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  
  // Robot Volume Value is between ( 0.0 - 1.0 )
  void SetRobotVolume(float volume);
  float GetRobotVolume() const;

  // Must be called after RegisterRobotAudioBuffer() to properly setup robot audio signal flow
  void SetOutputSource( RobotAudioOutputSource outputSource );
  RobotAudioOutputSource GetOutputSource() const { return _outputSource; }
  
  
  bool AvailableGameObjectAndAudioBufferInPool() const { return !_robotBufferGameObjectPool.empty(); }
  
  // Set gameObj & audio buffer out_vars for current output source
  // Return false if buffer is not available
  // Remove gameObj/buffer from pool
  bool GetGameObjectAndAudioBufferFromPool( AudioMetaData::GameObjectType& out_gameObj, RobotAudioBuffer*& out_buffer );

  // Add gameObj/buffer back into pool
  void ReturnGameObjectToPool( AudioMetaData::GameObjectType gameObject );
  
  Util::Dispatch::Queue* GetAudioQueue() const { return _dispatchQueue; }

  // Get shared random generator
  Util::RandomGenerator& GetRandomGenerator() const;
  
private:
  
  using PluginId_t = uint32_t;
  static constexpr PluginId_t kInvalidPluginId = 0;
  struct RobotBusConfiguration {
    AudioMetaData::GameObjectType gameObject;
    PluginId_t                    pluginId;
    AudioMetaData::Bus::BusType   bus;
  };
  
  // Handle to parent Robot
  Robot* _robot = nullptr;
  
  // Provides robot audio buffer
  CozmoAudioController* _audioController = nullptr;
  
  // Animation Audio Event queue
  Util::Dispatch::Queue* _dispatchQueue = nullptr;
  
  // Audio Animation Object to provide audio frames to Animation
  RobotAudioAnimation* _currentAnimation = nullptr;
  
  // Current Output source
  RobotAudioOutputSource _outputSource = RobotAudioOutputSource::None;
  
  // Store Bus configurations
  std::unordered_map<AudioMetaData::GameObjectType, RobotBusConfiguration, Util::EnumHasher> _busConfigurationMap;
  
  // Keep track of available Game Objects with Audio Buffers
  std::queue<AudioMetaData::GameObjectType> _robotBufferGameObjectPool;
  
  // Create Audio Buffer for the corresponding Game Object
  // Use Default value to regester a gameObj without a bus
  // Return: Bus pointer or Null if pluginId is kInvalidPluginId or busType is Invalid
  RobotAudioBuffer* RegisterRobotAudioBuffer( AudioMetaData::GameObjectType gameObject,
                                              PluginId_t pluginId = kInvalidPluginId,
                                              AudioMetaData::Bus::BusType bus = AudioMetaData::Bus::BusType::Invalid );
  
  void UnregisterRobotAudioBuffer( AudioMetaData::GameObjectType gameObject );
  
  AudioMetaData::GameObjectType GetGameObjectType() const;
  
  // Keep current robot volume
  float _robotVolume = 0.0f;
  
};
  
} // Audio
} // Cozmo
} // Anki



#endif /* __Basestation_Audio_RobotAudioClient_H__ */

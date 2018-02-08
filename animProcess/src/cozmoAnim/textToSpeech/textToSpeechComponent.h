/**
* File: textToSpeechComponent.h
*
* Author: Molly Jameson
* Created: 03/21/16
*
* Overhaul: Andrew Stein / Jordan Rivas, 08/18/16
*
* Description: Component wrapper to generate, cache and use wave data from a given string and style.
*
* Copyright: Anki, Inc. 2016
*
*/

#ifndef __Anki_cozmo_cozmoAnim_textToSpeech_textToSpeechComponent_H__
#define __Anki_cozmo_cozmoAnim_textToSpeech_textToSpeechComponent_H__

#include "audioEngine/audioTools/standardWaveDataContainer.h"
#include "coretech/common/shared/types.h"
#include "clad/audio/audioEventTypes.h"
#include "clad/audio/audioGameObjectTypes.h"
#include "clad/types/sayTextStyles.h"
#include "util/helpers/templateHelpers.h"
#include <mutex>
#include <unordered_map>

// Forward declarations
namespace Anki {
  namespace AudioEngine {
    class AudioEngineController;
  }
  namespace Util {
    namespace Dispatch {
      class Queue;
    }
  }
  namespace Cozmo {
    class AnimContext;
    namespace RobotInterface {
      struct TextToSpeechStart;
      struct TextToSpeechStop;
    }
    namespace TextToSpeech {
      class TextToSpeechProvider;
    }
  }
}

namespace Anki {
namespace Cozmo {
  
class TextToSpeechComponent
{
public:
  
  // Text to Speech data state
  enum class AudioCreationState {
    None,       // Does NOT exist
    Preparing,  // In process of creating data
    Ready       // Data is ready to use
  };
  
  
  TextToSpeechComponent(const AnimContext* context);
  ~TextToSpeechComponent();

  using OperationId = uint8_t;
  static const OperationId kInvalidOperationId = 0;
  
  // Asynchronous create the wave data for the given text and style, to be played later
  // Use GetOperationState() to check if wave data is Ready
  // Return OperationId, if equal to kInvalidOperation there was an error creating speech
  OperationId CreateSpeech(const std::string& text, const SayTextVoiceStyle style, const float durationScalar);
  
  // Get the current state of the create speech operation
  AudioCreationState GetOperationState(const OperationId operationId) const;
  
  // Set up Audio Engine to play text's audio data
  // out_duration_ms provides approximate duration of event before processing in audio engine
  // Return false if the audio has NOT been created or is not yet ready, out_duration_ms will NOT be valid.
  // NOTE: If this method is able to pass speech audio data ownership to plugin it will call ClearOperationData()
  // TODO: Currently there is only 1 source plugin for inserting audio it would be nice to have more
  bool PrepareAudioEngine(const OperationId operationId, float& out_duration_ms);
  
  // Clear Speech audio data from audio engine and clear operation data
  // TODO: Currently there is only 1 source plugin for inserting audio it would be nice to have more
  void CleanupAudioEngine(const OperationId operationId);
  
  // Clear speech operation audio data from memory
  void ClearOperationData(const OperationId operationId);
  
  // Clear ALL loaded text audio data from memory
  void ClearAllLoadedAudioData();
  
  // Get appropriate Audio Event for SayTextStyle
  AudioMetaData::GameEvent::GenericEvent GetAudioEvent(SayTextVoiceStyle style) const;

  // CLAD message handlers
  void HandleMessage(const RobotInterface::TextToSpeechStart& msg);
  void HandleMessage(const RobotInterface::TextToSpeechStop& msg);

private:

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Private Vars
  
  struct TtsBundle {
    AudioCreationState state                          = AudioCreationState::None;
    AudioEngine::StandardWaveDataContainer* waveData  = nullptr;
    ~TtsBundle() { Util::SafeDelete(waveData); }
  };
  
//  AudioEngine::AudioEngineController* _audioController  = nullptr;
  Util::Dispatch::Queue* _dispatchQueue                 = nullptr;
  
  std::unordered_map<OperationId, TtsBundle> _ttsWaveDataMap;
  
  OperationId                     _prevOperationId = kInvalidOperationId;
  
  mutable std::mutex _lock;
  
  std::unique_ptr<Anki::Cozmo::TextToSpeech::TextToSpeechProvider> _pvdr;
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Private Methods
  
  // Use Text to Speech lib to create audio data & reformat into StandardWaveData format
  // Return nullptr if Text to Speech lib fails to create audio data
  AudioEngine::StandardWaveDataContainer* CreateAudioData(const std::string& text,
                                                          SayTextVoiceStyle style,
                                                          float durationScalar);
  
  // Helpers
  // Find TtsBundle for operation
  const TtsBundle* GetTtsBundle(const OperationId operationId) const;
  TtsBundle* GetTtsBundle(const OperationId operationId);
  
  // Increment the operation Id
  OperationId GetNextOperationId();
  
}; // class TextToSpeechComponent


} // end namespace Cozmo
} // end namespace Anki


#endif //__Anki_cozmo_cozmoAnim_textToSpeech_textToSpeechComponent_H__

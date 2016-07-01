/*
 * File: audioEngineClientCunnection.h
 *
 * Author: Jordan Rivas
 * Created: 11/09/2015
 *
 * Description: This is a sub-class of AudioClientConnection which provides communication between its self and an
 *              AudioEngineClient by means of AudioEngineMessageHandler.
 *
 * Copyright: Anki, Inc. 2015
 */

#ifndef __Basestation_Audio_AudioEngineClientCunnection_H__
#define __Basestation_Audio_AudioEngineClientCunnection_H__

#include "anki/cozmo/basestation/audio/audioClientConnection.h"
#include "anki/cozmo/basestation/events/ankiEvent.h"
#include "util/signals/simpleSignal_fwd.h"
#include <vector>


namespace Anki {
namespace Cozmo {
namespace Audio {
  
class MessageAudioClient;
class AudioEngineMessageHandler;
  
class AudioEngineClientConnection : public AudioClientConnection {
  
public:
  // Transfer Message Handler Ownership
  AudioEngineClientConnection( AudioEngineMessageHandler* messageHandler );
  ~AudioEngineClientConnection() override;
  
  AudioEngineMessageHandler* GetMessageHandler() const { return _messageHandler; }
  
  virtual void PostCallback( const AudioCallback& callbackMessage ) const override;
  
  
private:
  
  AudioEngineMessageHandler* _messageHandler;
  std::vector<Signal::SmartHandle> _signalHandles;
  
  
  void HandleEvents(const AnkiEvent<MessageAudioClient>& event);
  
};


} // Audio
} // Cozmo
} // Anki


#endif /* __Basestation_Audio_AudioEngineClientCunnection_H__ */

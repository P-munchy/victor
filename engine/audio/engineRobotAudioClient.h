/*
 * File: engineRobotAudioClient.h
 *
 * Author: Jordan Rivas
 * Created: 9/14/2017
 *
 * Description: This is a subclass of AudioMuxClient which provides communication between itself and an
 *              EndingRobotAudioInput by means of EngineToRobot and RobotToEngine messages. It's purpose is to provide
 *              an interface to perform audio tasks and respond to audio callbacks sent from the audio engine in the
 *              animation process to engine process.
 *
 *
 * Copyright: Anki, Inc. 2017
 */


#ifndef __Cozmo_Basestation_EngineRobotAudioClient_H__
#define __Cozmo_Basestation_EngineRobotAudioClient_H__


#include "audioEngine/multiplexer/audioMuxClient.h"
#include "engine/events/ankiEvent.h"
#include <vector>


namespace Anki {
namespace Cozmo {
namespace RobotInterface {
class RobotToEngine;
}
class Robot;

namespace Audio {


class EngineRobotAudioClient : public AudioEngine::Multiplexer::AudioMuxClient {
  
  using CurveType = AudioEngine::Multiplexer::CurveType;
  
public:
  
  EngineRobotAudioClient() {}
  
  // Perform event
  // Provide a callback lambda to get all event callbacks; Duration, Marker, Complete & Error.
  virtual CallbackIdType PostEvent( AudioMetaData::GameEvent::GenericEvent event,
                                    AudioMetaData::GameObjectType gameObject = AudioMetaData::GameObjectType::Invalid,
                                    CallbackFunc&& callback = nullptr ) override;
  
  virtual void StopAllEvents( AudioMetaData::GameObjectType gameObject = AudioMetaData::GameObjectType::Invalid ) override;
  
  virtual void PostGameState( AudioMetaData::GameState::StateGroupType gameStateGroup,
                              AudioMetaData::GameState::GenericState gameState ) override;
  
  virtual  void PostSwitchState( AudioMetaData::SwitchState::SwitchGroupType switchGroup,
                                 AudioMetaData::SwitchState::GenericSwitch switchState,
                                 AudioMetaData::GameObjectType gameObject = AudioMetaData::GameObjectType::Invalid ) override;
  
  virtual void PostParameter( AudioMetaData::GameParameter::ParameterType parameter,
                              float parameterValue,
                              AudioMetaData::GameObjectType gameObject = AudioMetaData::GameObjectType::Invalid,
                              int32_t timeInMilliSeconds = 0,
                              CurveType curve = CurveType::Linear ) const override;

  // When the Robot's message handle setup is complete use robot to send message and subscribe audio callback messages
  void SubscribeAudioCallbackMessages( Robot* robot );


private:
  
  Robot* _robot = nullptr;
  std::vector<Signal::SmartHandle> _signalHandles;
  
  void HandleRobotEngineMessage( const AnkiEvent<RobotInterface::RobotToEngine>& message );

};


} // Audio
} // Cozmo
} // Anki



#endif /* __Cozmo_Basestation_EngineRobotAudioClient_H__ */

/**
 * File: engineMessagingClient.h
 *
 * Author: shawnb
 * Created: 3/08/2018
 *
 * Description: Communication point for message coming from / 
 *              going to the engine process. Currently this is
 *              using a tcp connection where engine acts as the
 *              server, and this is the client.
 *
 * Copyright: Anki, Inc. 2018
 *
 **/

#ifndef PLATFORM_SWITCHBOARD_SWITCHBOARDD_ENGINEMESSAGINGCLIENT_H_
#define PLATFORM_SWITCHBOARD_SWITCHBOARDD_ENGINEMESSAGINGCLIENT_H_

#include <string>
#include <signals/simpleSignal.hpp>
#include "libev/libev.h"
#include "coretech/messaging/shared/TcpClient.h"
#include "clad/externalInterface/messageEngineToGame.h"
#include "clad/externalInterface/messageGameToEngine.h"

namespace Anki {
namespace Switchboard {

class EngineMessagingClient {
public:
  using EngineMessageSignal = Signal::Signal<void (Anki::Cozmo::ExternalInterface::MessageEngineToGame)>;
  explicit EngineMessagingClient(struct ev_loop* loop);
  bool Init();
  bool Connect();
  bool Disconnect();
  void SendMessage(const Anki::Cozmo::ExternalInterface::MessageGameToEngine& message);
  void SetPairingPin(std::string pin);
  void ShowPairingStatus(Anki::Cozmo::SwitchboardInterface::ConnectionStatus status);
  EngineMessageSignal& OnReceivePairingStatus() {
    return _pairingStatusSignal;
  }
  EngineMessageSignal& OnReceiveEngineMessage() {
    return _engineMessageSignal;
  }
  static void sEvEngineMessageHandler(struct ev_loop* loop, struct ev_timer* w, int revents);

private:
  TcpClient client;
  EngineMessageSignal _pairingStatusSignal;
  EngineMessageSignal _engineMessageSignal;
  // anything that isn't pairing status should be attached to a different signal

  struct ev_loop* loop_;
  struct ev_EngineMessageTimerStruct {
    ev_timer timer;
    TcpClient* client;
    EngineMessageSignal* signal;
    EngineMessageSignal* websocketSignal;
  } _handleEngineMessageTimer;

  static uint8_t sMessageData[2048];
  static const unsigned int kMessageHeaderLength = 2;
  static const char* kEngineAddress;
  static const unsigned short kEnginePort = 5107;
  const float kEngineMessageFrequency_s = 0.1;
};
} // Switchboard
} // Anki

#endif // PLATFORM_SWITCHBOARD_SWITCHBOARDD_ENGINEMESSAGINGCLIENT_H_

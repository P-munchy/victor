/**
 * File: directGameComms
 *
 * Author: baustin
 * Created: 6/17/16
 *
 * Description: Implements comms interface for direct communication w/ the game
 *              (doesn't actually use sockets)
 *
 * Copyright: Anki, Inc. 2016
 *
 **/

#include "anki/cozmo/game/comms/directGameComms.h"
#include "anki/cozmo/game/comms/gameMessagePort.h"
#include "util/logging/logging.h"
#include <anki/messaging/basestation/IComms.h>

namespace Anki {
namespace Cozmo {

DirectGameComms::DirectGameComms(GameMessagePort* messagePort, ISocketComms::DeviceId hostId)
: _connectState(ConnectionState::Disconnected)
, _messagePort(messagePort)
, _hostId(hostId)
{
  ASSERT_NAMED(messagePort != nullptr, "DirectGameComms.NullMessagePipe");
}

bool DirectGameComms::Init(UiConnectionType connectionType, const Json::Value& config)
{
  return true;
}

void DirectGameComms::Update()
{
  std::list<std::vector<uint8_t>> newMessages = _messagePort->PullFromGameMessages();
  if (newMessages.empty()) {
    return;
  }

  _receivedMessages.splice(_receivedMessages.end(), std::move(newMessages));
}

bool DirectGameComms::SendMessage(const Comms::MsgPacket& msgPacket)
{
  if (_connectState == ConnectionState::Connected) {
    _messagePort->PushToGameMessage(msgPacket.data, msgPacket.dataLen);
    return true;
  }
  else {
    return false;
  }
}

bool DirectGameComms::RecvMessage(std::vector<uint8_t>& outBuffer)
{
  // if "connecting", we're connected when the UI sends us something
  if (_connectState == ConnectionState::Connecting && !_receivedMessages.empty()) {
    _connectState = ConnectionState::Connected;
  }

  if (_connectState != ConnectionState::Connected || _receivedMessages.empty()) {
    return false;
  }

  outBuffer = std::move(_receivedMessages.front());
  _receivedMessages.pop_front();
  return true;
}

bool DirectGameComms::ConnectToDeviceByID(ISocketComms::DeviceId deviceId)
{
  if (_connectState != ConnectionState::Disconnected) {
    return false;
  }
  _connectState = ConnectionState::Connecting;
  return true;
}

bool DirectGameComms::DisconnectDeviceByID(ISocketComms::DeviceId deviceId)
{
  _connectState = ConnectionState::Disconnected;
  return true;
}

void DirectGameComms::GetAdvertisingDeviceIDs(std::vector<ISocketComms::DeviceId>& outDeviceIds)
{
  outDeviceIds.clear();
  if (_connectState != ConnectionState::Disconnected) {
    return;
  }
  outDeviceIds.push_back(_hostId);
}

uint32_t DirectGameComms::GetNumConnectedDevices() const
{
  return _connectState == ConnectionState::Connected ? 1 : 0;
}

}
}

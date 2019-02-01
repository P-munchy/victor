/**
 * File: engineMessagingClient.cpp
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

#include <stdio.h>
#include <chrono>
#include <thread>
#include <iomanip>
#include "anki-ble/log.h"
#include "anki-wifi/wifi.h"
#include "switchboardd/engineMessagingClient.h"
#include "clad/externalInterface/messageEngineToGame.h"
#include "clad/externalInterface/messageGameToEngine.h"
#include "switchboardd/log.h"

using GMessageTag = Anki::Cozmo::ExternalInterface::MessageGameToEngineTag;
using GMessage = Anki::Cozmo::ExternalInterface::MessageGameToEngine;
using EMessageTag = Anki::Cozmo::ExternalInterface::MessageEngineToGameTag;
using EMessage = Anki::Cozmo::ExternalInterface::MessageEngineToGame;
namespace Anki {
namespace Switchboard {

uint8_t EngineMessagingClient::sMessageData[2048];

EngineMessagingClient::EngineMessagingClient(struct ev_loop* evloop)
: loop_(evloop)
{}

bool EngineMessagingClient::Init() {
  ev_timer_init(&_handleEngineMessageTimer.timer,
                &EngineMessagingClient::sEvEngineMessageHandler,
                kEngineMessageFrequency_s, 
                kEngineMessageFrequency_s);
  _handleEngineMessageTimer.client = this;
  return true;
}

bool Anki::Switchboard::EngineMessagingClient::Connect() {
  bool connected = _client.Connect(Anki::Victor::ENGINE_SWITCH_CLIENT_PATH, Anki::Victor::ENGINE_SWITCH_SERVER_PATH);

  if(connected) {
    ev_timer_start(loop_, &_handleEngineMessageTimer.timer);
  }

  // Send connection message
  static uint8_t connectionByte = 0;
  _client.Send((char*)(&connectionByte), sizeof(connectionByte));

  return connected;
}

bool EngineMessagingClient::Disconnect() {
  bool disconnected = true;
  ev_timer_stop(loop_, &_handleEngineMessageTimer.timer);
  if (_client.IsConnected()) {
    disconnected = _client.Disconnect();
  }
  return disconnected;
}

void EngineMessagingClient::sEvEngineMessageHandler(struct ev_loop* loop, struct ev_timer* w, int revents) {
  struct ev_EngineMessageTimerStruct *wData = (struct ev_EngineMessageTimerStruct*)w;

  int recvSize;
  
  while((recvSize = wData->client->_client.Recv((char*)sMessageData, sizeof(sMessageData))) > kMessageHeaderLength) {
    // Get message tag, and adjust for header size
    const uint8_t* msgPayload = (const uint8_t*)&sMessageData[kMessageHeaderLength];

    const EMessageTag messageTag = (EMessageTag)*msgPayload;

    switch(messageTag) {
      case EMessageTag::EnterPairing:
      case EMessageTag::ExitPairing:
      {
        EMessage message;
        uint16_t msgSize = *(uint16_t*)sMessageData;
        size_t unpackedSize = message.Unpack(msgPayload, msgSize);

        if(unpackedSize != (size_t)msgSize) {
          Log::Error("Received message from engine but had mismatch size when unpacked.");
          continue;
        } 

        // Emit signal for message
        wData->client->_pairingStatusSignal.emit(message);
      }
        break;
      case EMessageTag::WifiScanRequest:
      {
        // Handle request to Run WiFi scan
        wData->client->HandleWifiScanRequest();
      }
        break;
      case EMessageTag::WifiConnectRequest:
      {
        EMessage message;
        uint16_t msgSize = *(uint16_t*)sMessageData;
        size_t unpackedSize = message.Unpack(msgPayload, msgSize);

        if(unpackedSize != (size_t)msgSize) {
          Log::Error("Received message from engine but had mismatch size when unpacked.");
          continue;
        } 

        // Handle request to Run WiFi scan
        wData->client->HandleWifiConnectRequest(message.Get_WifiConnectRequest().ssid);
      }
        break;
      default:
        break;
    }
  }
}

void EngineMessagingClient::HandleWifiScanRequest() {
  std::vector<Anki::WiFiScanResult> wifiResults;
  WifiScanErrorCode code = Anki::ScanForWiFiAccessPoints(wifiResults);

  const uint8_t statusCode = (uint8_t)code;

  Anki::Cozmo::SwitchboardInterface::WifiScanResponse rsp;
  rsp.status_code = statusCode;
  rsp.ssid_count = wifiResults.size();

  Log::Write("Sending wifi scan results.");
  SendMessage(GMessage::CreateWifiScanResponse(std::move(rsp)));
}

void EngineMessagingClient::HandleWifiConnectRequest(const std::string& ssid) {
  std::stringstream ss;
  for(char c : ssid)
  {
    ss << std::hex << std::setfill('0') << std::setw(2) << (int)c;
  }
  const std::string ssidHex = ss.str();
  Log::Write("%s %s", ssid.c_str(), ssidHex.c_str());

  Anki::Cozmo::SwitchboardInterface::WifiConnectResponse rcp;
  rcp.status_code = 255;
    
  std::vector<Anki::WiFiScanResult> wifiResults;
  Anki::WifiScanErrorCode code = Anki::ScanForWiFiAccessPoints(wifiResults);
  
  if(code == Anki::WifiScanErrorCode::SUCCESS)
  {
    bool ssidInRange = false;
    for(const auto& result : wifiResults)
    {
      Log::Write("SSID %s", result.ssid.c_str());
      if(strcmp(ssidHex.c_str(), result.ssid.c_str()) == 0)
      {
        ssidInRange = true;
        
        Log::Write("HandleWifiConnectRequest: Found requested ssid from scan, attempting to connect");
        bool res = Anki::ConnectWiFiBySsid(result.ssid,
                                           "srw1JWOnjq;$Y\B,",
                                           (uint8_t)result.auth,
                                           result.hidden,
                                           nullptr,
                                           nullptr);

        if(!res)
        {
          Log::Write("HandleWifiConnectRequest: Failed to connect to ssid");
        }
        
        rcp.status_code = (uint8_t)(res ? 0 : 1);
        break;
      }
    }
    
    if(!ssidInRange)
    {
      Log::Write("HandleWifiConnectRequest: Requested ssid not in range");
    }
  }
  else
  {
    Log::Write("HandleWifiConnectRequest: Wifi scan failed");
    rcp.status_code = (uint8_t)code;
  }

  (void)Anki::RemoveWifiService(ssidHex);
  
  SendMessage(GMessage::CreateWifiConnectResponse(std::move(rcp)));
}

void EngineMessagingClient::SendMessage(const GMessage& message) {
  uint16_t message_size = message.Size();
  uint8_t buffer[message_size + kMessageHeaderLength];
  message.Pack(buffer + kMessageHeaderLength, message_size);
  memcpy(buffer, &message_size, kMessageHeaderLength);

  _client.Send((char*)buffer, sizeof(buffer));
}

void EngineMessagingClient::SetPairingPin(std::string pin) {
  Anki::Cozmo::SwitchboardInterface::SetBLEPin sbp;
  sbp.pin = std::stoul(pin);
  SendMessage(GMessage::CreateSetBLEPin(std::move(sbp)));
}

void EngineMessagingClient::ShowPairingStatus(Anki::Cozmo::SwitchboardInterface::ConnectionStatus status) {
  Anki::Cozmo::SwitchboardInterface::SetConnectionStatus scs;
  scs.status = status;

  SendMessage(GMessage::CreateSetConnectionStatus(std::move(scs)));
}

} // Switchboard
} // Anki

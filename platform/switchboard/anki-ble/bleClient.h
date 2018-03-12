/**
 * File: bleClient.h
 *
 * Author: paluri
 * Created: 2/20/2018
 *
 * Description: ble Client for ankibluetoothd
 *
 * Copyright: Anki, Inc. 2018
 *
 **/
#ifndef __BleClient_H__
#define  __BleClient_H__

#include "anki-ble/ipc-client.h"
#include "signals/simpleSignal.hpp"
#include "anki-ble/ipcBleStream.h"

#include <string>

namespace Anki {
namespace Switchboard { 
  class BleClient : public Anki::BluetoothDaemon::IPCClient {
  public:
    // Constructor
    BleClient(struct ev_loop* loop)
      : IPCClient(loop)
      , _connectionId(-1)
      , _stream(nullptr) {
    }

    // Types
    using ConnectionSignal = Signal::Signal<void (int connectionId, IpcBleStream* stream)>;
    using AdvertisingSignal = Signal::Signal<void (bool advertising)>;

    AdvertisingSignal& OnAdvertisingUpdateEvent() {
      return _advertisingUpdateSignal;
    }

    ConnectionSignal& OnConnectedEvent() {
      return _connectedSignal;
    }

    ConnectionSignal& OnDisconnectedEvent() {
      return _disconnectedSignal;
    }

  protected:
    virtual void OnInboundConnectionChange(int connection_id, int connected);
    virtual void OnReceiveMessage(const int connection_id,
                                  const std::string& characteristic_uuid,
                                  const std::vector<uint8_t>& value);
    virtual void OnPeripheralStateUpdate(const bool advertising,
                                        const int connection_id,
                                        const int connected,
                                        const bool congested);

  private:
    bool Send(uint8_t* msg, size_t length, std::string charUuid);
    bool SendPlainText(uint8_t* msg, size_t length);
    bool SendEncrypted(uint8_t* msg, size_t length);
    
    int _connectionId;
    IpcBleStream* _stream;

    AdvertisingSignal _advertisingUpdateSignal;

    ConnectionSignal _connectedSignal;
    ConnectionSignal _disconnectedSignal;
  };
} // Switchboard
} // Anki

#endif // __BleClient_H__
//
//  AnkiSwitchboard.hpp
//  ios-server
//
//  Created by Paul Aluri on 2/5/18.
//  Copyright © 2018 Paul Aluri. All rights reserved.
//

#ifndef AnkiSwitchboard_hpp
#define AnkiSwitchboard_hpp

#include <stdio.h>
#include <memory>
#include <NetworkExtension/NetworkExtension.h>
#include <CoreBluetooth/CoreBluetooth.h>
#include "simpleSignal.hpp"
#include "libev.h"
#include "securePairing.h"
#include "BLEPairingController.h"
#include "taskExecutor.h"

#define SB_WIFI_PORT 3291
#define SB_LOOP_TIME 30

namespace Anki {
  class Switchboard {
  public:
    static void Start();
    
    // Pin Updated Event
    using PinUpdatedSignal = Signal::Signal<void (std::string)>;
    static PinUpdatedSignal& OnPinUpdatedEvent() {
      return _PinUpdatedSignal;
    }
    
    static void SetQueue(dispatch_queue_t q) {
      switchboardQueue = q;
    }
    
  private:
    static struct ev_loop* sLoop;
    static Anki::Networking::SecurePairing* securePairing;
    static Signal::SmartHandle pinHandle;
    static Signal::SmartHandle wifiHandle;
    static dispatch_queue_t switchboardQueue;
    static Anki::TaskExecutor* _sTaskExecutor;
    
    static void HandleStartPairing();
    
    static void StartBleComms();
    static void StartWifiComms();
    
    static void OnConnected(Anki::Networking::INetworkStream* stream);
    static void OnDisconnected(Anki::Networking::INetworkStream* stream);
    static void OnPinUpdated(std::string pin);
    static void OnReceiveWifiCredentials(std::string ssid, std::string pw);
    static void sEvTimerHandler(struct ev_loop* loop, struct ev_timer* w, int revents);
    
    static PinUpdatedSignal _PinUpdatedSignal;
  };
}

#endif /* AnkiSwitchboard_h */

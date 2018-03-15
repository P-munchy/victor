/**
 * File: securePairing.h
 *
 * Author: paluri
 * Created: 1/16/2018
 *
 * Description: Secure Pairing controller for ankiswitchboardd
 *
 * Copyright: Anki, Inc. 2018
 *
 **/

#ifndef SecurePairing_h
#define SecurePairing_h

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "switchboardd/log.h"
#include "libev/libev.h"
#include "switchboardd/INetworkStream.h"
#include "switchboardd/keyExchange.h"
#include "switchboardd/pairingMessages.h"
#include "switchboardd/taskExecutor.h"
#include "switchboardd/externalCommsCladHandler.h"
#include "switchboardd/engineMessagingClient.h"

namespace Anki {
namespace Switchboard {
  enum PairingState : uint8_t {
    Initial,
    AwaitingHandshake,
    AwaitingPublicKey,
    AwaitingNonceAck,
    AwaitingChallengeResponse,
    ConfirmedSharedSecret
  };

  enum CommsState : uint8_t {
    Raw,
    Clad,
    SecureClad
  };
  
  class SecurePairing {
  public:
    // Types
    using ReceivedWifiCredentialsSignal = Signal::Signal<void (std::string, std::string)>;
    using UpdatedPinSignal = Signal::Signal<void (std::string)>;
    using OtaUpdateSignal = Signal::Signal<void (std::string)>;
    
    // Constructors
    SecurePairing(INetworkStream* stream, struct ev_loop* evloop, std::shared_ptr<EngineMessagingClient> engineClient, bool isPairing, bool isOtaUpdating);
    ~SecurePairing();
    
    // Methods
    void BeginPairing();
    void StopPairing();
    void SendOtaProgress(int status, int progress, int expectedTotal);
    
    std::string GetPin() {
      return _pin;
    }

    void SetOtaUpdating(bool updating) {
      _isOtaUpdating = updating;
    }

    void SetIsPairing(bool pairing) {
      Log::Write("Set isPairing:%s", pairing?"true":"false");
      _isPairing = pairing;
    }
    
    // WiFi Receive Event
    ReceivedWifiCredentialsSignal& OnReceivedWifiCredentialsEvent() {
      return _receivedWifiCredentialSignal;
    }
    
    // PIN Update Event
    UpdatedPinSignal& OnUpdatedPinEvent() {
      return _updatedPinSignal;
    }

    OtaUpdateSignal& OnOtaUpdateRequestEvent() {
      return _otaUpdateRequestSignal;
    }
    
  private:
    // Statics
    static long long sTimeStarted;
    static void sEvTimerHandler(struct ev_loop* loop, struct ev_timer* w, int revents);
    
    // Types
    using PairingTimeoutSignal = Signal::Signal<void ()>;
    
    // Template Methods
    template<typename T, typename... Args>
    int SendRtsMessage(Args&&... args);
    
    // Methods
    void Init();
    void Reset(bool forced=false);
    
    void HandleMessageReceived(uint8_t* bytes, uint32_t length);
    void HandleDecryptionFailed();
    bool HandleHandshake(uint16_t version);
    void HandleInitialPair(uint8_t* bytes, uint32_t length);
    void HandleCancelSetup();
    void HandleNonceAck();
    void HandleTimeout();
    void HandleInternetTimerTick();
    void HandleOtaRequest();
    void HandleChallengeResponse(uint8_t* bytes, uint32_t length);

    void SubscribeToCladMessages();
    
    inline bool AssertState(CommsState state) {
      return state == _commsState;
    }
    
    void SendHandshake();
    void SendPublicKey();
    void SendNonce();
    void SendChallenge();
    void SendCancelPairing();
    void SendChallengeSuccess();
    void SendWifiScanResult();
    void SendWifiConnectResult(bool connect);
    void SendWifiAccessPointResponse(bool success, std::string ssid, std::string pw);
    
    void IncrementAbnormalityCount();
    void IncrementChallengeCount();

    Signal::SmartHandle _rtsConnResponseHandle;
    void HandleRtsConnResponse(const Victor::ExternalComms::RtsConnection& msg);

    Signal::SmartHandle _rtsChallengeMessageHandle;
    void HandleRtsChallengeMessage(const Victor::ExternalComms::RtsConnection& msg);

    Signal::SmartHandle _rtsWifiConnectRequestHandle;
    void HandleRtsWifiConnectRequest(const Victor::ExternalComms::RtsConnection& msg);

    Signal::SmartHandle _rtsWifiIpRequestHandle;
    void HandleRtsWifiIpRequest(const Victor::ExternalComms::RtsConnection& msg);

    Signal::SmartHandle _rtsRtsStatusRequestHandle;
    void HandleRtsStatusRequest(const Victor::ExternalComms::RtsConnection& msg);

    Signal::SmartHandle _rtsWifiScanRequestHandle;
    void HandleRtsWifiScanRequest(const Victor::ExternalComms::RtsConnection& msg);

    Signal::SmartHandle _rtsOtaUpdateRequestHandle;
    void HandleRtsOtaUpdateRequest(const Victor::ExternalComms::RtsConnection& msg);

    Signal::SmartHandle _rtsWifiAccessPointRequestHandle;
    void HandleRtsWifiAccessPointRequest(const Victor::ExternalComms::RtsConnection& msg);

    Signal::SmartHandle _rtsCancelPairingHandle;
    void HandleRtsCancelPairing(const Victor::ExternalComms::RtsConnection& msg);

    Signal::SmartHandle _rtsAckHandle;
    void HandleRtsAck(const Victor::ExternalComms::RtsConnection& msg);

    Signal::SmartHandle _rtsSshHandle;
    void HandleRtsSsh(const Victor::ExternalComms::RtsConnection& msg);
    
    // Variables
    const uint8_t kMaxMatchAttempts = 5;
    const uint8_t kMaxPairingAttempts = 3;
    const uint32_t kMaxAbnormalityCount = 5;
    const uint16_t kPairingTimeout_s = 60;
    const uint8_t kNumPinDigits = 6;
    const uint8_t kMinMessageSize = 2;
    const uint8_t kWifiApPasswordSize = 8;
    const uint8_t kWifiConnectMinTimeout_s = 1;
    const uint8_t kWifiConnectInterval_s = 1;
    
    std::string _pin;
    uint8_t _challengeAttempts;
    uint8_t _totalPairingAttempts;
    uint8_t _numPinDigits;
    uint32_t _pingChallenge;
    uint32_t _abnormalityCount;
    uint8_t _inetTimerCount;
    uint8_t _wifiConnectTimeout_s;
    
    CommsState _commsState;
    INetworkStream* _stream;
    PairingState _state = PairingState::Initial;
    
    std::unique_ptr<KeyExchange> _keyExchange;
    std::unique_ptr<TaskExecutor> _taskExecutor;
    std::unique_ptr<ExternalCommsCladHandler> _cladHandler;
    
    Signal::SmartHandle _onReceivePlainTextHandle;
    Signal::SmartHandle _onReceiveEncryptedHandle;
    Signal::SmartHandle _onFailedDecryptionHandle;

    PairingTimeoutSignal _pairingTimeoutSignal;
    PairingTimeoutSignal _internetTimerSignal;
    
    struct ev_loop* _loop;
    ev_timer _timer;
    
    struct ev_TimerStruct {
      ev_timer timer;
      PairingTimeoutSignal* signal;
    } _handleTimeoutTimer;

    struct ev_TimerStruct _handleInternet;
    
    UpdatedPinSignal _updatedPinSignal;
    ReceivedWifiCredentialsSignal _receivedWifiCredentialSignal;
    std::shared_ptr<EngineMessagingClient> _engineClient;
    bool _isPairing = false;
    bool _isOtaUpdating = false;
    OtaUpdateSignal _otaUpdateRequestSignal;
  };
} // Switchboard
} // Anki

#endif /* SecurePairing_h */

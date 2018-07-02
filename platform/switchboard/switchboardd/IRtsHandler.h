#pragma once

#include <stdint.h>
#include "switchboardd/keyExchange.h"
#include "switchboardd/savedSessionManager.h"
#include "switchboardd/pairingMessages.h"
#include "switchboardd/log.h"

namespace Anki {
namespace Switchboard {

enum RtsPairingPhase : uint8_t {
  Initial,
  AwaitingHandshake,
  AwaitingPublicKey,
  AwaitingNonceAck,
  AwaitingChallengeResponse,
  ConfirmedSharedSecret
};

enum RtsCommsType : uint8_t {
  Handshake,
  Unencrypted,
  Encrypted
};

class IRtsHandler {
public:
  virtual ~IRtsHandler() {
    Log::Write("@@@ Destroyed interface");
  }
  
  virtual bool StartRts() = 0;
  virtual void StopPairing() = 0;
  virtual void SendOtaProgress(int status, uint64_t progress, uint64_t expectedTotal) = 0;
  virtual void HandleTimeout() = 0;
  
  void SetIsPairing(bool pairing) { _isPairing = pairing; }
  void SetOtaUpdating(bool updating) { _isOtaUpdating = updating; }

private:
  bool _isPairing = false;
  bool _isOtaUpdating = false;

protected:
  inline bool AssertState(RtsCommsType state) {
    return state == _type;
  }

  bool LoadKeys() {
    // Try to load keys
    _rtsKeys = SavedSessionManager::LoadRtsKeys();

    bool validKeys = _keyExchange->ValidateKeys((uint8_t*)&(_rtsKeys.keys.id.publicKey), (uint8_t*)&(_rtsKeys.keys.id.privateKey));

    if(!validKeys) {
      Log::Write("Keys loaded from file are corrupt.");
    } else {
      Log::Write("Stored keys are good to go.");
    }

    if(validKeys && (_rtsKeys.keys.version == SB_PAIRING_PROTOCOL_VERSION)) {
      _keyExchange->SetKeys((uint8_t*)&(_rtsKeys.keys.id.publicKey), (uint8_t*)&(_rtsKeys.keys.id.privateKey));

      Log::Write("Loading key pair from file.");
      return true;
    } else {
      uint8_t* publicKey = (uint8_t*)_keyExchange->GenerateKeys();

      // Save keys to file
      memcpy(&_rtsKeys.keys.id.publicKey, publicKey, sizeof(_rtsKeys.keys.id.publicKey));
      memcpy(&_rtsKeys.keys.id.privateKey, _keyExchange->GetPrivateKey(), sizeof(_rtsKeys.keys.id.privateKey));

      SaveKeys();
      Log::Write("Generating new key pair.");
      return false;
    }
  }

  void SaveKeys() {
    SavedSessionManager::SaveRtsKeys(_rtsKeys);
  }

  std::unique_ptr<KeyExchange> _keyExchange;

  RtsPairingPhase _state = RtsPairingPhase::AwaitingHandshake;
  RtsCommsType _type = RtsCommsType::Unencrypted;
  RtsKeys _rtsKeys;
};

} // Switchboard
} // Anki
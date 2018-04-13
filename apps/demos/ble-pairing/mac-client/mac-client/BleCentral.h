//
//  BleClient.h
//  mac-client
//
//  Created by Paul Aluri on 2/28/18.
//  Copyright © 2018 Paul Aluri. All rights reserved.
//

#ifndef BleClient_h
#define BleClient_h

#include <CoreBluetooth/CoreBluetooth.h>
#include <stdlib.h>
#include "bleMessageProtocol.h"
#include "messageExternalComms.h"
#include <sodium.h>
#include <arpa/inet.h>

enum RtsState {
  Raw         = 0,
  Clad        = 1,
  CladSecure  = 2,
};

enum WiFiAuth : uint8_t {
  AUTH_NONE_OPEN       = 0,
  AUTH_NONE_WEP        = 1,
  AUTH_NONE_WEP_SHARED = 2,
  AUTH_IEEE8021X       = 3,
  AUTH_WPA_PSK         = 4,
  AUTH_WPA_EAP         = 5,
  AUTH_WPA2_PSK        = 6,
  AUTH_WPA2_EAP        = 7
};

@interface BleCentral : NSObject <CBCentralManagerDelegate, CBPeripheralDelegate> {
  NSString* _localName;
  
  CBCentralManager* _centralManager;
  CBUUID* _victorService;
  CBUUID* _readUuid;
  CBUUID* _writeUuid;
  CBUUID* _readSecureUuid;
  CBUUID* _writeSecureUuid;
  
  CBPeripheral* _peripheral;
  
  NSMutableDictionary* _characteristics;
  
  std::unique_ptr<Anki::Switchboard::BleMessageProtocol> _bleMessageProtocol;
  
  uint8_t _publicKey [crypto_kx_PUBLICKEYBYTES];
  uint8_t _secretKey [crypto_kx_SECRETKEYBYTES];
  uint8_t _encryptKey [crypto_kx_SESSIONKEYBYTES];
  uint8_t _decryptKey [crypto_kx_SESSIONKEYBYTES];
  uint8_t _remotePublicKey [crypto_kx_PUBLICKEYBYTES];
  uint8_t _nonceIn [crypto_aead_xchacha20poly1305_ietf_NPUBBYTES];
  uint8_t _nonceOut [crypto_aead_xchacha20poly1305_ietf_NPUBBYTES];
  
  enum RtsState _rtsState;
  bool _reconnection;
  
  NSMutableDictionary* _victorsDiscovered;
  bool _connecting;
  
  NSString* _filter;
  
  NSMutableDictionary* _wifiAuth;
  dispatch_queue_t _commandQueue;
  
  std::string _currentCommand;
  bool _readyForNextCommand;
  
  uint8_t _otaStatusCode;
  uint64_t _otaProgress;
  uint64_t _otaExpected;
  
  bool _verbose;
  int _commVersion;
  
  NSArray* _colorArray;
}

- (std::string)hexStr:(char*)data length:(int)len;
- (std::string)asciiStr:(char*)data length:(int)size;
- (uint8_t)nibbleToNumber:(uint8_t)nibble;

- (void) handleSend:(const void*)bytes length:(int)n;
- (void) handleReceive:(const void*)bytes length:(int)n;
- (void) handleReceiveSecure:(const void*)bytes length:(int)n;
- (void) printHelp;

- (void) SendSshPublicKey:(std::string)filename;

- (void) HandleReceiveHandshake:(const void*)bytes length:(int)n;
- (void) HandleReceivePublicKey:(const Anki::Victor::ExternalComms::RtsConnRequest&)msg;
- (void) HandleReceiveNonce:(const Anki::Victor::ExternalComms::RtsNonceMessage&)msg;
- (void) HandleChallengeMessage:(const Anki::Victor::ExternalComms::RtsChallengeMessage&)msg;
- (void) HandleChallengeSuccessMessage:(const Anki::Victor::ExternalComms::RtsChallengeSuccessMessage&)msg;
- (void) HandleWifiScanResponse:(const Anki::Victor::ExternalComms::RtsWifiScanResponse&)msg;
- (void) HandleReceiveAccessPointResponse:(const Anki::Victor::ExternalComms::RtsWifiAccessPointResponse&)msg;

- (void) send:(const void*)bytes length:(int)n;
- (void) sendSecure:(const void*)bytes length:(int)n;

- (void) StartScanning;
- (void) StartScanning:(NSString*)nameFilter;
- (void) StopScanning;
- (void) interrupt;

- (std::vector<std::string>) GetWordsFromLine: (std::string)line;

- (void) printSuccess:(const char*) txt;

// for reconnection
- (bool) HasSavedPublicKey;
- (bool) HasSavedSession: (NSString*)key;
- (NSData*) GetPublicKey;
- (NSArray*) GetSession: (NSString*)key;
- (void)resetDefaults;
- (void)setVerbose:(bool)enabled;

@end

class Clad {
public:
  template<typename T, typename... Args>
  static void SendRtsMessage(BleCentral* central, int commVersion, Args&&... args) {
    Anki::Victor::ExternalComms::ExternalComms msg;
    
    switch(commVersion) {
      case 1:
        msg = Anki::Victor::ExternalComms::ExternalComms(Anki::Victor::ExternalComms::RtsConnection_1(T(std::forward<Args>(args)...)));
        break;
      case 2:
        msg = Anki::Victor::ExternalComms::ExternalComms(Anki::Victor::ExternalComms::RtsConnection(Anki::Victor::ExternalComms::RtsConnection_2(T(std::forward<Args>(args)...))));
        break;
      default:
        NSLog(@"The mac client is trying to speak a version we do not know about.");
        break;
    }
    std::vector<uint8_t> messageData(msg.Size());
    const size_t packedSize = msg.Pack(messageData.data(), msg.Size());
    [central send:messageData.data() length:(int)packedSize];
  }
  
  template<typename T, typename... Args>
  static void SendRtsMessage_2(BleCentral* central, int commVersion, Args&&... args) {
    Anki::Victor::ExternalComms::ExternalComms msg = Anki::Victor::ExternalComms::ExternalComms(Anki::Victor::ExternalComms::RtsConnection(Anki::Victor::ExternalComms::RtsConnection_2(T(std::forward<Args>(args)...))));

    std::vector<uint8_t> messageData(msg.Size());
    const size_t packedSize = msg.Pack(messageData.data(), msg.Size());
    [central send:messageData.data() length:(int)packedSize];
  }
};

#endif /* BleClient_h */

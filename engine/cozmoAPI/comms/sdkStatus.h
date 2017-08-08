/**
 * File: sdkStatus
 *
 * Author: Mark Wesley
 * Created: 08/30/16
 *
 * Description: Status of the SDK connection and usage
 *
 * Copyright: Anki, Inc. 2016
 *
 **/


#ifndef __Cozmo_Game_Comms_SdkStatus_H__
#define __Cozmo_Game_Comms_SdkStatus_H__


#include "clad/types/sdkStatusTypes.h"
#include "util/container/circularBuffer.h"
#include <stddef.h>
#include <stdint.h>
#include <string>


namespace Anki {
namespace Cozmo {

  
class IExternalInterface;

namespace ExternalInterface
{
  class MessageGameToEngine;
  struct UiDeviceConnectionWrongVersion;
  struct UiDeviceConnectionSuccess;
  enum class MessageEngineToGameTag : uint8_t;
  enum class MessageGameToEngineTag : uint8_t;
} // namespace ExternalInterface

  
class ISocketComms;
  

class SdkStatus
{
public:
  
  SdkStatus(IExternalInterface* externalInterface);
  ~SdkStatus() {}
  
  static double GetCurrentTime_s();
  
  void EnterMode(bool isExternalSdkMode);
  void ExitMode(bool isExternalSdkMode);
  
  void OnConnectionSuccess(const ExternalInterface::UiDeviceConnectionSuccess& message);
  void OnWrongVersion(const ExternalInterface::UiDeviceConnectionWrongVersion& message);
  void OnDisconnect(bool isExitingSDKMode);
  void SetStopRobotOnDisconnect(bool newVal);
  void SetShouldAutoConnectToCubes(bool newVal) { _shouldAutoConnectToCubes = newVal; }
  void SetShouldAutoDisconnectFromCubes(bool newVal) { _shouldAutoDisconnectFromCubes = newVal; }
  
  void OnRecvMessage(const ExternalInterface::MessageGameToEngine& message, size_t messageSize);
  
  void UpdateConnectionStatus(const ISocketComms* sdkSocketComms);
  
  double TimeInMode_s(double timeNow_s) const;
  double TimeInCurrentConnection_s(double timeNow_s, bool activeTime=false) const;
  double TimeSinceLastSdkMessage_s(double timeNow_s) const;
  double TimeSinceLastSdkCommand_s(double timeNow_s) const; // excludes pings and other non-user messages
  
  uint32_t NumTimesConnected() const { return _numTimesConnected; }
  uint32_t NumCommandsOverConnection() const { return _numCommandsSentOverConnection; }
  
  bool IsInExternalSdkMode() const { return _isInExternalSdkMode; }
  bool IsInInternalSdkMode() const { return _isInInternalSdkMode; }
  bool IsInAnySdkMode() const { return (_isInExternalSdkMode || _isInInternalSdkMode); }
    
  bool IsConnected() const { return _isConnected; }
  
  static constexpr const double kInvalidTime_s = -1.0;
  
  size_t      GetRecentCommandCount() const { return _recentCommands.size(); }
  const char* GetRecentCommandName(size_t index) const;
  
  const std::string& GetSdkBuildVersion() const { return _connectedSdkBuildVersion; }
  bool IsWrongSdkVersion() const { return _isWrongSdkVersion; }
  
  void SetStatus(SdkStatusType statusType, std::string&& statusText)
  {
    const uint32_t idx = (uint32_t)statusType;
    _sdkStatusStrings[idx] = std::move(statusText);
  }
  
  const std::string& GetStatus(SdkStatusType statusType) const
  {
    const uint32_t idx = (uint32_t)statusType;
    return _sdkStatusStrings[idx];
  }
  
  bool WillStopRobotOnDisconnect() const { return _stopRobotOnDisconnect; }

private:
  
  void ResetRobot(bool isExitingSDKMode);
  
  Util::CircularBuffer<ExternalInterface::MessageGameToEngineTag>  _recentCommands;

  std::string   _connectedSdkBuildVersion;
  std::string   _sdkStatusStrings[SdkStatusTypeNumEntries];
  
  IExternalInterface* _externalInterface = nullptr;
  
  double    _enterSdkModeTime_s    = kInvalidTime_s;
  double    _connectionStartTime_s = kInvalidTime_s;
  double    _lastSdkMessageTime_s  = kInvalidTime_s;
  double    _lastSdkCommandTime_s  = kInvalidTime_s;
  
  uint32_t  _numTimesConnected  = 0;
  uint32_t  _numCommandsSentOverConnection = 0;
  bool      _isConnected = false;
    
  bool      _isInExternalSdkMode = false;
  bool      _isInInternalSdkMode = false;
    
  bool      _isWrongSdkVersion = false;
  bool      _stopRobotOnDisconnect = true;
  
  bool      _shouldAutoConnectToCubes = false;
  bool      _shouldAutoDisconnectFromCubes = false;
};
  
  
} // namespace Cozmo
} // namespace Anki


#endif // __Cozmo_Game_Comms_SdkStatus_H__


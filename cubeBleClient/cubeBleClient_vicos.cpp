/**
 * File: cubeBleClient_vicos.cpp
 *
 * Author: Matt Michini
 * Created: 12/1/2017
 *
 * Description:
 *               Defines interface to BLE central process which communicates with cubes (vic-os specific implementations)
 *
 * Copyright: Anki, Inc. 2017
 *
 **/

#include "cubeBleClient.h"

#include "anki-ble/common/anki_ble_uuids.h"
#include "bleClient/bleClient.h"

#include "clad/externalInterface/messageCubeToEngine.h"
#include "clad/externalInterface/messageEngineToCube.h"

#include "util/logging/logging.h"
#include "util/math/numericCast.h"
#include "util/string/stringUtils.h"
#include "util/fileUtils/fileUtils.h"
#include "util/time/universalTime.h"

#include <queue>
#include <thread>

#ifdef SIMULATOR
#error SIMULATOR should NOT be defined by any target using cubeBleClient_vicos.cpp
#endif

namespace Anki {
namespace Cozmo {

namespace {
  
  // Flag indicating whether we've already flashed one cube on connection
  bool _checkedCubeFirmwareVersion = false;
  
  struct ev_loop* _loop = nullptr;
  
  std::unique_ptr<BleClient> _bleClient = nullptr;
  
  // For detecting connection state changes
  bool _wasConnectedToCube = false;
  
  // shared queue for buffering cube messages received on the client thread
  using CubeMsgRecvBuffer = std::queue<std::vector<uint8_t>>;
  CubeMsgRecvBuffer _cubeMsgRecvBuffer;
  std::mutex _cubeMsgRecvBuffer_mutex;
  
  // shared queue for buffering advertisement messages received on the client thread
  struct CubeAdvertisementInfo {
    CubeAdvertisementInfo(const std::string& addr, const int rssi) : addr(addr), rssi(rssi) { }
    std::string addr;
    int rssi;
  };
  using CubeAdvertisementBuffer = std::queue<CubeAdvertisementInfo>;
  CubeAdvertisementBuffer _cubeAdvertisementBuffer;
  std::mutex _cubeAdvertisementBuffer_mutex;

  // Flag indicating when scanning for cubes has completed
  std::atomic<bool> _scanningFinished{false};

  // Flag indicating whether the connected cube's firmware version is correct
  std::atomic<bool> _cubeFirmwareVersionMatch{true};
  
  // Time after which we consider a connection attempt to have failed.
  // Always less than 0 if there is no pending connection attempt.
  float _connectionAttemptFailTime_sec = -1.f;
  
  // Max time a connection attempt is allowed to take before timing out
  const float kConnectionAttemptTimeout_sec = 10.f;
}

  
CubeBleClient::CubeBleClient()
{
  _loop = ev_default_loop(EVBACKEND_SELECT);
  _bleClient = std::make_unique<BleClient>(_loop);
  
  _bleClient->RegisterAdvertisementCallback([](const std::string& addr, const int rssi) {
    std::lock_guard<std::mutex> lock(_cubeAdvertisementBuffer_mutex);
    _cubeAdvertisementBuffer.emplace(addr, rssi);
  });
  
  _bleClient->RegisterReceiveDataCallback([](const std::string& addr, const std::vector<uint8_t>& data){
    std::lock_guard<std::mutex> lock(_cubeMsgRecvBuffer_mutex);
    _cubeMsgRecvBuffer.push(data);
  });
  
  _bleClient->RegisterScanFinishedCallback([](){
    _scanningFinished = true;
  });

  _bleClient->RegisterReceiveFirmwareVersionCallback([this](const std::string& addr, const std::string& connectedCubeFirmwareVersion) {
    std::string versionOnDisk = "";
    std::vector<uint8_t> firmware = Util::FileUtils::ReadFileAsBinary(_cubeFirmwarePath);
    size_t offset = 0x10; // The first 16 bytes of the firmware data are the version string
    if(firmware.size() > offset) {
      versionOnDisk = std::string(firmware.begin(), firmware.begin() + offset);
    }
    _cubeFirmwareVersionMatch = connectedCubeFirmwareVersion.compare(versionOnDisk)==0;
  });
}


CubeBleClient::~CubeBleClient()
{
  _bleClient->Stop();
  _bleClient.reset();
  ev_loop_destroy(_loop);
  _loop = nullptr;
}

bool CubeBleClient::InitInternal()
{
  DEV_ASSERT(!_inited, "CubeBleClient.Init.AlreadyInitialized");

  _bleClient->Start();
  return true;
}

bool CubeBleClient::UpdateInternal()
{
  // Check bleClient's connection to the bluetooth daemon
  if (!_bleClient->IsConnectedToServer() &&
      _cubeConnectionState != CubeConnectionState::UnconnectedIdle) {
    const auto prevConnnectionState = _cubeConnectionState;
    _cubeConnectionState = CubeConnectionState::UnconnectedIdle;
    if (prevConnnectionState == CubeConnectionState::Connected) {
      // inform callbacks that we've been disconnected
      for (const auto& callback : _cubeConnectionCallbacks) {
        callback(_currentCube, false);
      }
    }
    _currentCube.clear();
    PRINT_NAMED_WARNING("CubeBleClient.UpdateInternal.NotConnectedToDaemon",
                        "We are not connected to the bluetooth daemon - setting connection state to %s. "
                        "Previous connection state: %s.",
                        CubeConnectionStateToString(_cubeConnectionState),
                        CubeConnectionStateToString(prevConnnectionState));
  }
  
  
  // Check for connection attempt timeout
  if (_cubeConnectionState == CubeConnectionState::PendingConnect) {
    const float now_sec = static_cast<float>(Util::Time::UniversalTime::GetCurrentTimeInSeconds());
    if (now_sec > _connectionAttemptFailTime_sec) {
      // Connection attempt has timed out
      PRINT_NAMED_WARNING("CubeBleClient.UpdateInternal.ConnectionAttemptTimeout",
                          "Connection attempt has taken more than %.2f seconds - aborting.",
                          kConnectionAttemptTimeout_sec);
      // Inform callbacks that the connection attempt has failed
      for (const auto& callback : _connectionFailedCallbacks) {
        callback(_currentCube);
      }
      // Tell BleClient to disconnect from cube. This will cancel the
      // connection attempt.
      RequestDisconnectInternal();
    }
  } else {
    _connectionAttemptFailTime_sec = -1.f;
  }
  
  
  // Check for connection state changes
  const bool connectedToCube = _bleClient->IsConnectedToCube();
  if (connectedToCube != _wasConnectedToCube) {
    if (connectedToCube) {
      PRINT_NAMED_INFO("CubeBleClient.UpdateInternal.ConnectedToCube",
                       "Connected to cube %s",
                       _currentCube.c_str());
      if (_cubeConnectionState != CubeConnectionState::PendingConnect) {
        PRINT_NAMED_WARNING("CubeBleClient.UpdateInternal.UnexpectedConnection",
                            "Received unexpected connection. Previous connection state: %s",
                            CubeConnectionStateToString(_cubeConnectionState));
      }
      _cubeConnectionState = CubeConnectionState::Connected;
      for (const auto& callback : _cubeConnectionCallbacks) {
        callback(_currentCube, true);
      }
    } else {
      PRINT_NAMED_INFO("CubeBleClient.UpdateInternal.DisconnectedFromCube",
                       "Disconnected from cube %s",
                       _currentCube.c_str());
      if (_cubeConnectionState != CubeConnectionState::PendingDisconnect) {
        PRINT_NAMED_WARNING("CubeBleClient.UpdateInternal.UnexpectedDisconnection",
                            "Received unexpected disconnection. Previous connection state: %s",
                            CubeConnectionStateToString(_cubeConnectionState));
      }
      _cubeConnectionState = CubeConnectionState::UnconnectedIdle;
      for (const auto& callback : _cubeConnectionCallbacks) {
        callback(_currentCube, false);
      }
      _currentCube.clear();
    }
    
    _wasConnectedToCube = connectedToCube;
  }
  
  // Pull advertisement messages from queue into a temp queue,
  // to avoid holding onto the mutex for too long.
  CubeAdvertisementBuffer swapCubeAdvertisementBuffer;
  {
    std::lock_guard<std::mutex> lock(_cubeAdvertisementBuffer_mutex);
    swapCubeAdvertisementBuffer.swap(_cubeAdvertisementBuffer);
  }
  
  while (!swapCubeAdvertisementBuffer.empty()) {
    const auto& data = swapCubeAdvertisementBuffer.front();
    ExternalInterface::ObjectAvailable msg;
    msg.factory_id = data.addr;
    msg.objectType = ObjectType::Block_LIGHTCUBE1; // TODO - update this with the Victor cube type once it's defined
    msg.rssi = Util::numeric_cast_clamped<decltype(msg.rssi)>(data.rssi);
    if (_cubeConnectionState == CubeConnectionState::ScanningForCubes) {
      for (const auto& callback : _objectAvailableCallbacks) {
        callback(msg);
      }
    } else {
      PRINT_NAMED_WARNING("CubeBleClient.UpdateInternal.IgnoringAdvertisement",
                          "Ignoring cube advertisement message from %s since we are not scanning for cubes. "
                          "Current connection state: %s",
                          msg.factory_id.c_str(),
                          CubeConnectionStateToString(_cubeConnectionState));
    }
    swapCubeAdvertisementBuffer.pop();
  }

  // check firmware versions -- if no match, prepare to flash the cube
  // note: only do this once after connecting to a cube
  if(!_cubeFirmwareVersionMatch && !_checkedCubeFirmwareVersion) {
    std::vector<uint8_t> firmware = Util::FileUtils::ReadFileAsBinary(_cubeFirmwarePath);
    _bleClient->FlashCube(std::move(firmware));
    _checkedCubeFirmwareVersion = true;
    _cubeFirmwareVersionMatch = true;
  }
  
  // Pull cube messages from queue into a temp queue,
  // to avoid holding onto the mutex for too long.
  CubeMsgRecvBuffer swapCubeMsgRecvBuffer;
  {
    std::lock_guard<std::mutex> lock(_cubeMsgRecvBuffer_mutex);
    swapCubeMsgRecvBuffer.swap(_cubeMsgRecvBuffer);
  }
  
  while (!swapCubeMsgRecvBuffer.empty()) {
    const auto& data = swapCubeMsgRecvBuffer.front();
    if (_cubeConnectionState == CubeConnectionState::Connected) {
      MessageCubeToEngine cubeMessage(data.data(), data.size());
      for (const auto& callback : _cubeMessageCallbacks) {
        callback(_currentCube, cubeMessage);
      }
    } else {
      PRINT_NAMED_WARNING("CubeBleClient.UpdateInternal.IgnoringCubeMsg",
                          "Ignoring cube messages since we are not connected to a cube. "
                          "Current connection state: %s",
                          CubeConnectionStateToString(_cubeConnectionState));
    }
    swapCubeMsgRecvBuffer.pop();
  }

  // Check to see if scanning for cubes has finished
  if (_scanningFinished) {
    _cubeConnectionState = CubeConnectionState::UnconnectedIdle;
    for (const auto& callback : _scanFinishedCallbacks) {
      callback();
    }
    _scanningFinished = false;
  }
  
  return true;
}


void CubeBleClient::SetScanDuration(const float duration_sec)
{
  _bleClient->SetScanDuration(duration_sec);
}


void CubeBleClient::StartScanInternal()
{
  PRINT_NAMED_INFO("CubeBleClient.StartScanInternal",
                   "Starting to scan for available cubes");
  
  // Sending from this thread for now. May need to queue this and
  // send it on the client thread if ipc client is not thread safe.
  _bleClient->StartScanForCubes();
  _cubeConnectionState = CubeConnectionState::ScanningForCubes;
}


void CubeBleClient::StopScanInternal()
{
  PRINT_NAMED_INFO("CubeBleClient.StopScanInternal",
                   "Stopping scan for available cubes");
  
  // Sending from this thread for now. May need to queue this and
  // send it on the client thread if ipc client is not thread safe.
  _bleClient->StopScanForCubes();
  _cubeConnectionState = CubeConnectionState::UnconnectedIdle;
}


bool CubeBleClient::SendMessageInternal(const MessageEngineToCube& msg)
{
  u8 buff[msg.Size()];
  msg.Pack(buff, msg.Size());
  const auto& msgVec = std::vector<u8>(buff, buff + msg.Size());

  // Sending from this thread for now. May need to queue this and
  // send it on the client thread if ipc client is not thread safe.
  return _bleClient->Send(msgVec);
}


bool CubeBleClient::RequestConnectInternal(const BleFactoryId& factoryId)
{
  if (_bleClient->IsConnectedToCube()) {
    PRINT_NAMED_WARNING("CubeBleClient.RequestConnectInternal.AlreadyConnected",
                        "We are already connected to a cube (address %s)!",
                        _currentCube.c_str());
    return false;
  }
  
  DEV_ASSERT(_currentCube.empty(), "CubeBleClient.RequestConnectInternal.CubeAddressNotEmpty");
  
  _currentCube = factoryId;
  _cubeConnectionState = CubeConnectionState::PendingConnect;
  
  PRINT_NAMED_INFO("CubeBleClient.RequestConnectInternal.AttemptingToConnect",
                   "Attempting to connect to cube %s",
                   _currentCube.c_str());
  
  DEV_ASSERT(_connectionAttemptFailTime_sec < 0.f, "CubeBleClient.RequestConnectInternal.UnexpectedConnectionAttemptFailTime");
  const float now_sec = static_cast<float>(Util::Time::UniversalTime::GetCurrentTimeInSeconds());
  _connectionAttemptFailTime_sec = now_sec + kConnectionAttemptTimeout_sec;
  
  // Sending from this thread for now. May need to queue this and
  // send it on the client thread if ipc client is not thread safe.
  _bleClient->ConnectToCube(_currentCube);
  return true;
}


bool CubeBleClient::RequestDisconnectInternal()
{
  if (!_bleClient->IsConnectedToCube()) {
    PRINT_NAMED_WARNING("CubeBleClient.RequestDisconnectInternal.NotConnected",
                        "We are not connected to any cubes! Telling BleClient to disconnect anyway to be safe. "
                        "Current connection state: %s. Setting connection state to Unconnected.",
                        CubeConnectionStateToString(_cubeConnectionState));
    _cubeConnectionState = CubeConnectionState::UnconnectedIdle;
    _currentCube.clear();
    _bleClient->DisconnectFromCube();
    return false;
  }
  
  _cubeConnectionState = CubeConnectionState::PendingDisconnect;
  
  // Sending from this thread for now. May need to queue this and
  // send it on the client thread if ipc client is not thread safe.
  _bleClient->DisconnectFromCube();
  return true;
}

} // namespace Cozmo
} // namespace Anki

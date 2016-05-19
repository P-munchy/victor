/**
 * File: TcpSocketComms
 *
 * Author: Mark Wesley
 * Created: 05/14/16
 *
 * Description: TCP implementation for socket-based communications from e.g. Game/SDK to Engine
 *
 * Copyright: Anki, Inc. 2016
 *
 **/


#include "anki/cozmo/game/comms/tcpSocketComms.h"
#include "anki/cozmo/basestation/utils/parsingConstants/parsingConstants.h"
#include "anki/messaging/basestation/IComms.h"
#include "anki/messaging/shared/TcpServer.h"
#include "json/json.h"
#include "util/helpers/templateHelpers.h"
#include "util/logging/logging.h"


namespace Anki {
namespace Cozmo {

  
using MessageSizeType = uint16_t; // Must match on Engine and Python SDK side


TcpSocketComms::TcpSocketComms(UiConnectionType connectionType)
  : ISocketComms(connectionType)
  , _tcpServer( new TcpServer() )
  , _connectedId( kDeviceIdInvalid )
  , _hasClient(false)
{
  _receivedBuffer.reserve(4096); // Big enough to hold several messages without reallocating
}


TcpSocketComms::~TcpSocketComms()
{
  Util::SafeDelete(_tcpServer);
}

  
bool TcpSocketComms::Init(UiConnectionType connectionType, const Json::Value& config)
{
  assert(connectionType == UiConnectionType::SdkOverTcp);
  
  const Json::Value& portValue = config[AnkiUtil::kP_SDK_ON_DEVICE_TCP_PORT];
  
  if (portValue.isNumeric())
  {
    const uint32_t port = portValue.asUInt();
    PRINT_NAMED_INFO("TcpSocketComms.StartListening", "Start Listening on port %u", port);
    _tcpServer->StartListening(port);
  }
  else
  {
    PRINT_NAMED_ERROR("TcpSocketComms.Init",
                      "Missing/Invalid '%s' entry in Json config file.", AnkiUtil::kP_SDK_ON_DEVICE_TCP_PORT);
    return false;
  }
  
  return true;
}


void TcpSocketComms::HandleDisconnect()
{
  _receivedBuffer.clear();
  _connectedId = kDeviceIdInvalid;
  _hasClient   = false;
}
  
  
void TcpSocketComms::Update()
{
  // See if we lost the client since last upate
  if (_hasClient && !_tcpServer->HasClient())
  {
    PRINT_NAMED_INFO("TcpSocketComms.Update.ClientLost", "Client Connection to Device %d lost", _connectedId);
    HandleDisconnect();
  }
  
  if (!_hasClient)
  {
    if (_tcpServer->Accept())
    {
      _hasClient = _tcpServer->HasClient();
      if (_hasClient)
      {
        PRINT_NAMED_INFO("TcpSocketComms.Update.ClientAccepted", "Client Connected to server");
      }
    }
  }
}


bool TcpSocketComms::SendMessage(const Comms::MsgPacket& msgPacket)
{
  if (IsConnected())
  {
    // Send the size of the message, followed by the message itself
    // so that messages can be re-assembled on the other side:
    // Send as 2 consecutive sends rather than copy into a new larger packet
    // TCP should stream it all together anyway, and both sides handle receiving partial data anyway
    
    static_assert(sizeof(msgPacket.dataLen) == 2, "size mismatch");
    
    int res = _tcpServer->Send((const char*)&msgPacket.dataLen, sizeof(msgPacket.dataLen));
    if (res < 0)
    {
      return false;
    }
    res = _tcpServer->Send((const char*)msgPacket.data, msgPacket.dataLen);
    if (res < 0)
    {
      return false;
    }
  }

  return false;
}


bool TcpSocketComms::ReadFromSocket()
{
  // Resize _receivedBuffer big enough to read into, then resize back to fit the size of bytes actually read
  // Buffer is reserved initially so the resize shouldn't allocate and therefore be fast
  
  const size_t kMaxReadSize = 2048;
  const size_t oldBufferSize  = _receivedBuffer.size();
  _receivedBuffer.resize( oldBufferSize + kMaxReadSize );
  
  const int bytesRecv = _tcpServer->Recv((char*)&_receivedBuffer[oldBufferSize], kMaxReadSize);
  
  if (bytesRecv > 0)
  {
    const size_t newBufferSize = oldBufferSize + bytesRecv;
    _receivedBuffer.resize(newBufferSize);
    return true;
  }
  else
  {
    _receivedBuffer.resize(oldBufferSize);
    return false;
  }
}

  
bool TcpSocketComms::ExtractNextMessage(Comms::MsgPacket& outMsgPacket)
{
  if (_receivedBuffer.size() >= sizeof(MessageSizeType))
  {
    MessageSizeType sizeofMessage;
    memcpy(&sizeofMessage, &_receivedBuffer[0], sizeof(MessageSizeType));
    MessageSizeType sizeofMessageAndHeader = sizeofMessage + sizeof(MessageSizeType);
    
    if (sizeofMessageAndHeader <= _receivedBuffer.size())
    {
      outMsgPacket.CopyFrom( sizeofMessage, &_receivedBuffer[sizeof(MessageSizeType)] );
      _receivedBuffer.erase(_receivedBuffer.begin(), _receivedBuffer.begin() + sizeofMessageAndHeader);
      return true;
    }
  }
  
  return false;
}
  
  
bool TcpSocketComms::RecvMessage(Comms::MsgPacket& outMsgPacket)
{
  if (IsConnected())
  {
    // Try to extract a message from already received bytes first (to avoid overfilling the recv buffer)
    if (ExtractNextMessage(outMsgPacket))
    {
      return true;
    }
    
    // See if there's anything else in the socket, and if that is enough to extract the next message
    
    if (ReadFromSocket())
    {
      return ExtractNextMessage(outMsgPacket);
    }
  }
  
  return false;
}


bool TcpSocketComms::ConnectToDeviceByID(DeviceId deviceId)
{
  assert(deviceId != kDeviceIdInvalid);
  
  if (_connectedId == kDeviceIdInvalid)
  {
    _connectedId = deviceId;
    return true;
  }
  else
  {
    PRINT_NAMED_WARNING("TcpSocketComms.ConnectToDeviceByID.Failed",
                        "Cannot connect to device %d, already connected to %d", deviceId, _connectedId);
    return false;
  }
}


bool TcpSocketComms::DisconnectDeviceByID(DeviceId deviceId)
{
  assert(deviceId != kDeviceIdInvalid);
  
  if ((_connectedId != kDeviceIdInvalid) && (_connectedId == deviceId))
  {
    _tcpServer->DisconnectClient();
    HandleDisconnect();
    return true;
  }
  else
  {
    return false;
  }
}


void TcpSocketComms::GetAdvertisingDeviceIDs(std::vector<ISocketComms::DeviceId>& outDeviceIds)
{
  if (_tcpServer->HasClient())
  {
    if (!IsConnected())
    {
      // Advertising doesn't really make sense for TCP, just pretend we have Id 1 whenever a client connection is made
      outDeviceIds.push_back(1);
    }
  }
}


bool TcpSocketComms::IsConnected() const
{
  if ((kDeviceIdInvalid != _connectedId) && _tcpServer->HasClient())
  {
    return true;
  }
  
  return false;
}


uint32_t TcpSocketComms::GetNumConnectedDevices() const
{
  if (IsConnected())
  {
    return 1;
  }
  
  return 0;
}


} // namespace Cozmo
} // namespace Anki


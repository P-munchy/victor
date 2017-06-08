/**
 * File: gameComms.h
 *
 * Author: Kevin Yoon
 * Created: 12/16/2014
 *
 * Description: Interface class to allow UI to communicate with game
 *
 * Copyright: Anki, Inc. 2014
 *
 **/
#ifndef BASESTATION_COMMS_GAME_COMMS_H_
#define BASESTATION_COMMS_GAME_COMMS_H_

#include <deque>
#include <anki/messaging/basestation/IComms.h>
#include "anki/messaging/basestation/advertisementService.h"
#include "anki/messaging/shared/TcpServer.h"
#include "anki/messaging/shared/UdpClient.h"
#include "anki/cozmo/shared/cozmoConfig.h"
#include "clad/externalInterface/messageShared.h"


namespace Anki {
namespace Cozmo {

  
  class GameComms : public Comms::IComms {
  public:
    
    // Default constructor
    GameComms(int deviceID, int serverListenPort, const char* advertisementRegIP_, int advertisementRegPort_);
    
    // The destructor will automatically cleans up
    virtual ~GameComms();
    
    // Returns true if we are ready to use TCP
    virtual bool IsInitialized();
    
    // Returns 0 if no messages are available.
    virtual u32 GetNumPendingMsgPackets();
  
    virtual size_t Send(const Comms::MsgPacket &p);

    virtual bool GetNextMsgPacket(std::vector<uint8_t> &buf);
    
    
    // when game is unpaused we need to dump old messages
    virtual void ClearMsgPackets();
    
    virtual u32 GetNumMsgPacketsInSendQueue(int devID);
    
    // Updates the list of advertising robots
    virtual void Update(bool send_queued_msgs = true);
    
    bool HasClient();
    void DisconnectClient();
    
  private:
    
    // For connection from game
#if(USE_UDP_UI_COMMS)
    UdpServer server_;
#else
    TcpServer server_;
#endif
    
    // For connecting to advertisement service
    UdpClient regClient_;
    AdvertisementRegistrationMsg regMsg_;
    void AdvertiseToService();
    
    void ReadAllMsgPackets();
    
    void PrintRecvBuf();
    
    // 'Queue' of received messages from all connected user devices with their received times.
    std::deque<Comms::MsgPacket> recvdMsgPackets_;

    bool           isInitialized_;

    // Device ID to use for registering with advertisement service
    int            deviceID_;
    
    int            serverListenPort_;
    const char*    advertisementRegIP_;
    int            advertisementRegPort_;
    
    static const int MAX_RECV_BUF_SIZE = 1920000; // [TODO] 1.9MB seems excessive?
    u8  _recvBuf[MAX_RECV_BUF_SIZE];
    int recvDataSize = 0;
    
  };

}  // namespace Cozmo
}  // namespace Anki

#endif  // #ifndef BASESTATION_COMMS_TCPCOMMS_H_


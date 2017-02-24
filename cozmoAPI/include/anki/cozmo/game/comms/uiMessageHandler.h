/**
 * File: uiMessageHandler.h
 *
 * Author: Kevin Yoon
 * Date:   7/11/2014
 *
 * Description: Handles messages between UI and basestation just as 
 *              MessageHandler handles messages between basestation and robot.
 *
 * Copyright: Anki, Inc. 2014
 **/

#ifndef COZMO_UI_MESSAGEHANDLER_H
#define COZMO_UI_MESSAGEHANDLER_H

#include "anki/common/types.h"
#include "anki/cozmo/basestation/events/ankiEventMgr.h"
#include "anki/cozmo/basestation/externalInterface/externalInterface.h"
#include "anki/cozmo/game/comms/iSocketComms.h"
#include "anki/cozmo/game/comms/sdkStatus.h"
#include "clad/externalInterface/messageEngineToGame.h"
#include "clad/externalInterface/messageGameToEngine.h"
#include "clad/types/uiConnectionTypes.h"
#include "util/signals/simpleSignal_fwd.h"

#include <memory>

// Forward declarations
namespace Anki {
namespace Comms {
  class AdvertisementService;
  class MsgPacket;
}
namespace Util {
  namespace Stats {
    class StatsAccumulator;
  }
}
}

namespace Anki {
  namespace Cozmo {
    

    class CozmoContext;
    class Robot;
    class RobotManager;
    class GameMessagePort;
    
    class UiMessageHandler : public IExternalInterface
    {
    public:
      
      UiMessageHandler(u32 hostUiDeviceID, GameMessagePort* messagePipe); // Force construction with stuff in Init()?
      virtual ~UiMessageHandler();
      
      Result Init(CozmoContext* context, const Json::Value& config);
      
      Result Update();
      
      virtual void Broadcast(const ExternalInterface::MessageGameToEngine& message) override;
      virtual void Broadcast(ExternalInterface::MessageGameToEngine&& message) override;
      virtual void BroadcastDeferred(const ExternalInterface::MessageGameToEngine& message) override;
      virtual void BroadcastDeferred(ExternalInterface::MessageGameToEngine&& message) override;
      
      virtual void Broadcast(const ExternalInterface::MessageEngineToGame& message) override;
      virtual void Broadcast(ExternalInterface::MessageEngineToGame&& message) override;
      virtual void BroadcastDeferred(const ExternalInterface::MessageEngineToGame& message) override;
      virtual void BroadcastDeferred(ExternalInterface::MessageEngineToGame&& message) override;
      
      virtual Signal::SmartHandle Subscribe(const ExternalInterface::MessageEngineToGameTag& tagType, std::function<void(const AnkiEvent<ExternalInterface::MessageEngineToGame>&)> messageHandler) override;
      
      virtual Signal::SmartHandle Subscribe(const ExternalInterface::MessageGameToEngineTag& tagType, std::function<void(const AnkiEvent<ExternalInterface::MessageGameToEngine>&)> messageHandler) override;
      
      inline u32 GetHostUiDeviceID() const { return _hostUiDeviceID; }
      
      AnkiEventMgr<ExternalInterface::MessageEngineToGame>& GetEventMgrToGame() { return _eventMgrToGame; }
      AnkiEventMgr<ExternalInterface::MessageGameToEngine>& GetEventMgrToEngine() { return _eventMgrToEngine; }
      
      const Util::Stats::StatsAccumulator& GetLatencyStats(UiConnectionType type) const;
      
      bool HasDesiredNumUiDevices() const;
      
      virtual void OnRobotDisconnected(uint32_t robotID) override;
      
      virtual bool IsInSdkMode() const override { return _sdkStatus.IsInAnySdkMode(); }
      
      virtual void SetSdkStatus(SdkStatusType statusType, std::string&& statusText) override
      {
        _sdkStatus.SetStatus(statusType, std::move(statusText));
      }

    private:
      
      // ============================== Private Member Functions ==============================
      
      const ISocketComms* GetSocketComms(UiConnectionType type) const
      {
        const uint32_t typeIndex = (uint32_t)type;
        const bool inRange = (typeIndex < uint32_t(UiConnectionType::Count));
        assert(inRange);
        return inRange ? _socketComms[typeIndex] : nullptr;
      }
      
      ISocketComms* GetSocketComms(UiConnectionType type)
      {
        return const_cast<ISocketComms*>( const_cast<const UiMessageHandler*>(this)->GetSocketComms(type) );
      }
      
      const ISocketComms* GetSdkSocketComms() const
      {
        const ISocketComms* socketComms = GetSocketComms(UiConnectionType::SdkOverTcp);
        return socketComms ? socketComms : GetSocketComms(UiConnectionType::SdkOverUdp);
      }
      
      ISocketComms* GetSdkSocketComms()
      {
        return const_cast<ISocketComms*>( const_cast<const UiMessageHandler*>(this)->GetSdkSocketComms() );
      }
      
      uint32_t GetNumConnectedDevicesOnAnySocket() const;
      
      bool ShouldHandleMessagesFromConnection(UiConnectionType type) const;
      
      bool IsSdkCommunicationEnabled() const;
      
      void OnEnterSdkMode(const AnkiEvent<ExternalInterface::MessageGameToEngine>& event);
      void OnExitSdkMode(const AnkiEvent<ExternalInterface::MessageGameToEngine>& event);
      void DoExitSdkMode();
      
      void UpdateSdk();
      void UpdateIsSdkCommunicationEnabled();
      
      // As long as there are messages available from the comms object,
      // process them and pass them along to robots.
      Result ProcessMessages();
      
      // Process a raw byte buffer as a GameToEngine CLAD message and broadcast it
      Result ProcessMessageBytes(const uint8_t* packetBytes, size_t packetSize,
                                 UiConnectionType connectionType, bool isSingleMessage, bool handleMessagesFromConnection);
      void HandleProcessedMessage(const ExternalInterface::MessageGameToEngine& message, UiConnectionType connectionType,
                                  size_t messageSize, bool handleMessagesFromConnection);
      
      // Send a message to a specified ID
      virtual void DeliverToGame(const ExternalInterface::MessageEngineToGame& message, DestinationId = kDestinationIdEveryone) override;
      
      bool ConnectToUiDevice(ISocketComms::DeviceId deviceId, UiConnectionType connectionType);
      void HandleEvents(const AnkiEvent<ExternalInterface::MessageGameToEngine>& event);
      
      // Some events need to be sent from Unity -> SDK or from SDK -> Unity
      void HandleGameToGameEvents(const AnkiEvent<ExternalInterface::MessageGameToEngine>& event);
      
      // ============================== Private Types ==============================
      
      using MessageEngineToGame = ExternalInterface::MessageEngineToGame;
      using MessageGameToEngine = ExternalInterface::MessageGameToEngine;
      
      // ============================== Private Member Vars ==============================

      ISocketComms* _socketComms[(size_t)UiConnectionType::Count];
      
      std::vector<Signal::SmartHandle>    _signalHandles;
      
      AnkiEventMgr<MessageEngineToGame>   _eventMgrToGame;
      AnkiEventMgr<MessageGameToEngine>   _eventMgrToEngine;
      
      std::vector<MessageGameToEngine>    _threadedMsgsToEngine;
      std::vector<MessageEngineToGame>    _threadedMsgsToGame;
      std::mutex                          _mutex;
      
      SdkStatus                           _sdkStatus;
      
      uint32_t                            _hostUiDeviceID = 0;
      
      uint32_t                            _updateCount = 0;
      
      bool                                _isInitialized = false;

      CozmoContext*                       _context = nullptr;
      
    }; // class MessageHandler
    
    
#undef MESSAGE_BASECLASS_NAME
    
  } // namespace Cozmo
} // namespace Anki


#endif // COZMO_MESSAGEHANDLER_H

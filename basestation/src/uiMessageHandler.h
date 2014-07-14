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
#include "anki/cozmo/basestation/ui/messaging/uiMessages.h"
#include "anki/cozmo/basestation/uiTcpComms.h"

// Enable this if you want to receive/send messages via socket connection.
// Eventually, this should be disabled by default once the UI layer starts
// handling the comms and communication with the basestation is purely through messageQueue
// TODO: MessageQueue mode not yet supported!!!
//       Will do this after messageDefinitions auto-generation tool has been created.
#define RUN_UI_MESSAGE_TCP_SERVER 1



namespace Anki {
  namespace Cozmo {
    
#define MESSAGE_BASECLASS_NAME UiMessage
#include "anki/cozmo/basestation/ui/messaging/UiMessageDefinitions.h"
    
    class Robot;
    class RobotManager;
    class BlockWorld;
    class BehaviorManager;
    
    class IUiMessageHandler
    {
    public:
      
      // TODO: Change these to interface references so they can be stubbed as well
      virtual Result Init(Comms::IComms* comms,
                          RobotManager*  robotMgr,
                          BlockWorld*    blockWorld,
                          BehaviorManager* behaviorMgr) = 0;
      
      virtual Result ProcessMessages() = 0;
      
      virtual Result SendMessage(const UserDeviceID_t devID, const UiMessage& msg) = 0;
      
    }; // IMessageHandler
    
    
    class UiMessageHandler : public IUiMessageHandler
    {
    public:
      
      UiMessageHandler(); // Force construction with stuff in Init()?
      
      // Set the message handler's communications manager
      virtual Result Init(Comms::IComms* comms,
                          RobotManager*  robotMgr,
                          BlockWorld*    blockWorld,
                          BehaviorManager* behaviorMgr);
      
      // As long as there are messages available from the comms object,
      // process them and pass them along to robots.
      virtual Result ProcessMessages();
      
      // Send a message to a specified ID
      Result SendMessage(const UserDeviceID_t devID, const UiMessage& msg);
      
    protected:
      
      Comms::IComms* comms_;
      RobotManager* robotMgr_;
      BlockWorld*   blockWorld_;
      BehaviorManager* behaviorMgr_;
      
      bool isInitialized_;
      
      // Process a raw byte buffer as a message and send it to the specified
      // robot
      Result ProcessPacket(const Comms::MsgPacket& packet);
      
      // Auto-gen the ProcessBufferAs_MessageX() method prototypes using macros:
#define MESSAGE_DEFINITION_MODE MESSAGE_PROCESS_METHODS_MODE
#include "anki/cozmo/basestation/ui/messaging/UiMessageDefinitions.h"
      
      // Fill in the message information lookup table for getting size and
      // ProcesBufferAs_MessageX function pointers according to enumerated
      // message ID.
      struct {
        u8 priority;
        u8 size;
        Result (UiMessageHandler::*ProcessPacketAs)(Robot*, const u8*);
      } lookupTable_[NUM_UI_MSG_IDS+1] = {
        {0, 0, 0}, // Empty entry for NO_MESSAGE_ID
#define MESSAGE_DEFINITION_MODE MESSAGE_TABLE_DEFINITION_MODE
#define MESSAGE_HANDLER_CLASSNAME UiMessageHandler
#include "anki/cozmo/basestation/ui/messaging/UiMessageDefinitions.h"
#undef MESSAGE_HANDLER_CLASSNAME
        {0, 0, 0} // Final dummy entry without comma at end
      };
      
    }; // class MessageHandler
    
    
    class UiMessageHandlerStub : public IUiMessageHandler
    {
    public:
      UiMessageHandlerStub() { }
      
      Result Init(Comms::IComms* comms,
                  RobotManager*  robotMgr,
                  BlockWorld*    blockWorld,
                  BehaviorManager* behaviorMgr)
      {
        return RESULT_OK;
      }
      
      // As long as there are messages available from the comms object,
      // process them and pass them along to robots.
      Result ProcessMessages() {
        return RESULT_OK;
      }
      
      // Send a message to a specified ID
      Result SendMessage(const UserDeviceID_t devID, const UiMessage& msg) {
        return RESULT_OK;
      }
      
    }; // MessageHandlerStub
    
    
#undef MESSAGE_BASECLASS_NAME
    
  } // namespace Cozmo
} // namespace Anki


#endif // COZMO_MESSAGEHANDLER_H
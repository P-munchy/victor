/*
 * usb_radio.cpp
 *
 *   Implemenation of HAL radio functionality that actually uses UART.
 *   To be used in conjunction with CozmoCommsTranslator.
 *
 *   Use this OR radio.cpp. Not both!
 *
 *   Author: Kevin Yoon
 *
 */

#include "anki/cozmo/robot/cozmoConfig.h"
#include "anki/cozmo/robot/hal.h"
#include <stdio.h>
#include <string>

#include "anki/messaging/shared/utilMessaging.h"

// Hack to prevent linker error
// Once the intertwining between HAL and supervisor is fixed, this won't be required
//#define RUN_EMBEDDED_TESTS

namespace Anki {
  namespace Cozmo {
    
    namespace { // "Private members"
      const u16 RECV_BUFFER_SIZE = 1024;
      
      u8 recvBuf_[RECV_BUFFER_SIZE];
      s32 recvBufSize_ = 0;
    }
    
    Result InitSimRadio(s32 robotID)
    {
      return RESULT_OK;
    }
    
    bool HAL::RadioIsConnected(void)
    {
      // Always assumes radio is connected
      return true;
    }

    void DisconnectRadio(void)
    {
      recvBufSize_ = 0;
    }

#ifndef RUN_EMBEDDED_TESTS    
    bool HAL::RadioSendMessage(const Messages::ID msgID, const void *buffer, TimeStamp_t ts)
    {
#if(USING_UART_RADIO)

        // Send the message header (0xBEEF + timestamp + robotID + msgID)
        // For TCP comms, send timestamp immediately after the header.
        // This is needed on the basestation side to properly order messages.

        // Send header and message content - return false if message was discarded (full buffer)
        const u8 size = Messages::GetSize(msgID);
        return UARTPutMessage(msgID, ts, (u8*)buffer, size);
#else
        return true;
#endif      
      
    } // RadioSendMessage()
#endif // #ifndef RUN_EMBEDDED_TESTS

    u32 RadioGetNumBytesAvailable(void)
    {
#if(USING_UART_RADIO)			
      // Pull as many inbound chars as we can into our local buffer
      while (recvBufSize_ < RECV_BUFFER_SIZE)
      {
        int c = HAL::UARTGetChar(0);
        if (c < 0)    // Nothing more to grab
          return recvBufSize_;
        recvBuf_[recvBufSize_++] = c;
      }
#endif      
      return recvBufSize_;
      
    } // RadioGetNumBytesAvailable()
    

#ifndef RUN_EMBEDDED_TESTS    
    // TODO: would be nice to implement this in a way that is not specific to
    //       hardware vs. simulated radio receivers, and just calls lower-level
    //       radio functions.
    Messages::ID HAL::RadioGetNextMessage(u8 *buffer)
    {
      Messages::ID retVal = Messages::NO_MESSAGE_ID;

#if(USING_UART_RADIO)      
//      if (server.HasClient()) {
        const u32 bytesAvailable = RadioGetNumBytesAvailable();
        if(bytesAvailable > 0) {
    
          const int headerSize = sizeof(RADIO_PACKET_HEADER);
          
          // Look for valid header
          const char* hPtr = std::strstr((const char*)recvBuf_,(const char*)RADIO_PACKET_HEADER);
          if (hPtr == NULL) {
            // Header not found at all
            // Delete everything
            recvBufSize_ = 0;
            return retVal;
          }
					
          const s32 n = (s32)hPtr - (s32)recvBuf_;
          if (n != 0) {
            // Header was not found at the beginning.
            // Delete everything up until the header.
            recvBufSize_ -= n;
            memcpy(recvBuf_, hPtr, recvBufSize_); 
          }
          
          // Check if expected number of bytes are in the msg
          if (recvBufSize_ > headerSize) {
            u32 dataLen = recvBuf_[headerSize] +
                          (recvBuf_[headerSize+1] << 8) +
                          (recvBuf_[headerSize+2] << 16) +
                          (recvBuf_[headerSize+3] << 24);
						
            if (dataLen > 255) {
              // We shouldn't be sending huge messages to the robot
              PRINT("WARNING(RecvdMsgTooBig): %d bytes\n", dataLen);
              dataLen = 255;
            }
						
            if (recvBufSize_ >= headerSize + 4 + dataLen) {
            
              // Check that message size is correct
              Messages::ID msgID = static_cast<Messages::ID>(recvBuf_[headerSize+4]);
              const u8 size = Messages::GetSize(msgID);
              u32 msgLen = dataLen - 1;  // Doesn't include msgID
              
              if (msgLen != size) {
                PRINT("WARNING: Message size mismatch: ID %d, expected %d bytes, but got %d bytes\n", msgID, size, msgLen);
              }
              
              // Copy message contents to buffer
              std::memcpy((void*)buffer, recvBuf_ + headerSize + 4 + 1, msgLen);
              retVal = msgID;
              
              // Shift recvBuf contents down
              const u32 entireMsgSize = headerSize + 4 + dataLen;
              memcpy(recvBuf_, recvBuf_ + entireMsgSize, recvBufSize_ - entireMsgSize);
              recvBufSize_ -= entireMsgSize;
              
            }
          }
          
        } // if bytesAvailable > 0
//      }
#endif      
      return retVal;
    } // RadioGetNextMessage()
#endif // #ifndef RUN_EMBEDDED_TESTS    
    
    void RadioUpdate()
    {
    }    
  } // namespace Cozmo
} // namespace Anki

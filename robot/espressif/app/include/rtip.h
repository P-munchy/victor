/** @file Header file for C++ interface to RTIP on the Espressif
 * @author Daniel Casner <daniel@anki.com>
 */
#ifndef __RTIP_h
#define __RTIP_h

#include "clad/robotInterface/messageEngineToRobot.h"

namespace Anki {
  namespace Cozmo {
    namespace RTIP {
      // Send a message to the RTIP
      bool SendMessage(RobotInterface::EngineToRobot& msg);
      
      // Accept a message from the RTIP and relay or process it
      extern "C" bool AcceptRTIPMessage(uint8_t* payload, uint8_t length);
    }
  }
}

#endif

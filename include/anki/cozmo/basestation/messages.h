/**
 * File: messages.h  (Basestation)
 *
 * Author: Andrew Stein
 * Date:   1/21/2014
 *
 * Description: Defines a base Message class from which all the other messages
 *              inherit.  The inherited classes are auto-generated via multiple
 *              re-includes of the MessageDefinitions.h file, with specific
 *              "mode" flags set.
 *
 * Copyright: Anki, Inc. 2014
 **/

#ifndef COZMO_MESSAGE_BASESTATION_H
#define COZMO_MESSAGE_BASESTATION_H

#include "json/json.h"

#include "anki/common/types.h"

namespace Anki {

  namespace Cozmo {
    
    // 1. Initial include just defines the definition modes for use below
#undef MESSAGE_DEFINITION_MODE
#include "anki/cozmo/MessageDefinitions.h"
    
    // Create the enumerated message IDs from the MessageDefinitions file:
    typedef enum {
      NO_MESSAGE_ID = 0,
#define MESSAGE_DEFINITION_MODE MESSAGE_ENUM_DEFINITION_MODE
#include "anki/cozmo/MessageDefinitions.h"
      NUM_MSG_IDS // Final entry without comma at end
    } ID;
    
    // Base message class
    class Message
    {
    public:
      
      virtual u8 GetID() const = 0;
      
      virtual void GetBytes(u8* buffer) const = 0;
      
      virtual u8 GetSize() const = 0; 
      
    }; // class Message


    // 2. Define all the message classes:
#define MESSAGE_DEFINITION_MODE MESSAGE_CLASS_DEFINITION_MODE
#include "anki/cozmo/MessageDefinitions.h"


  } // namespace Cozmo
} // namespace Anki


#endif  // #ifndef COZMO_MESSAGE_BASESTATION_H


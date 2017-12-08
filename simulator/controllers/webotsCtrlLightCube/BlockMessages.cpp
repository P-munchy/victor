#include "clad/externalInterface/lightCubeMessage.h"
#include "BlockMessages.h"
#include <stdio.h>

namespace Anki {
  namespace Cozmo {
    
    namespace ActiveBlock {
      // Auto-gen the ProcessBufferAs_MessageX() method prototypes using macros:
      #include "clad/externalInterface/lightCubeMessage_declarations.def"

      void ProcessBadTag_LightCubeMessage(const BlockMessages::LightCubeMessage::Tag tag);
    }
    
    namespace BlockMessages {

      Result ProcessMessage(const u8* buffer, const u8 bufferSize)
      {
        using namespace ActiveBlock;
        
        LightCubeMessage msg(buffer, (size_t) bufferSize);
        #include "clad/externalInterface/lightCubeMessage_switch.def"
        
        return RESULT_OK;
      } // ProcessBuffer()      
      
    } // namespace Messages
  } // namespace Cozmo
} // namespace Anki

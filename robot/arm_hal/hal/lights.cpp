#include "lib/stm32f4xx.h"
#include "anki/cozmo/robot/hal.h"
#include "anki/cozmo/robot/spineData.h"
#include "hal/portable.h"

namespace Anki
{
  namespace Cozmo
  {
    namespace HAL
    {
      static const uint32_t DROP_LEVEL = 20;

      extern volatile GlobalDataToBody g_dataToBody;

      GPIO_PIN_SOURCE(IRLED, GPIOE, 0);

      // Initialize LED head/face light hardware
      void LightsInit()
      {
        RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE, ENABLE);

        // IR LED is controlled by N-FET so positive polarity unlike everything else
        GPIO_RESET(GPIO_IRLED, PIN_IRLED);
        PIN_PULLDOWN(GPIO_IRLED, SOURCE_IRLED);
        PIN_PP(GPIO_IRLED, SOURCE_IRLED);
        PIN_OUT(GPIO_IRLED, SOURCE_IRLED);

      }

      

      // Light up one of the backpack LEDs to the specified 24-bit RGB color
      void SetLED(LEDId led_id, u32 color)
      {
        u8 led_channel = led_id & 0x3; // so that LED_BACKPACK_RIGHT maps to 0
        volatile u32* channel = &g_dataToBody.backpackColors[ led_channel ];
        if (led_id == LED_BACKPACK_LEFT) {
          // Use red color channel for intensity
          *channel = (*channel & 0xffff00ff) | ((color & 0x00ff0000) >> 8);
        } else if (led_id == LED_BACKPACK_RIGHT) {
          // Use red color channel for intensity
          *channel = (*channel & 0xff00ffff) |  (color & 0x00ff0000);
        } else {
          // RGB -> BGR
          *channel = ((color & 0xff) << 16) |  (color & 0xff00) | ((color & 0xff0000) >> 16) ;
        }
      }

      // Turn headlights on (true) and off (false)
      void SetHeadlights(bool state)
      {
        if (state)
          GPIO_SET(GPIO_IRLED, PIN_IRLED);
        else
          GPIO_RESET(GPIO_IRLED, PIN_IRLED);
      }
      
      extern volatile GlobalDataToHead g_dataToHead;      
      bool IsCliffDetected()
      {
        switch(HAL::GetID()) {
          // Cliff sensors not working on these robots
          case 0x3AA7:
          case 0x3A94:
            return false;
          default:
            return g_dataToHead.cliffLevel < DROP_LEVEL;
        }
      }
      
      // This will eventually go away when real prox detection is working.
      u8 GetForwardProxSensor()
      {
        // Physical robot never detects obstacles for now.
        return 0;
      }
    }
  }
}

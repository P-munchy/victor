#include <string.h>

#include "nrf.h"
#include "nrf_gpio.h"

#include "hardware.h"
#include "battery.h"
#include "motors.h"
#include "head.h"
#include "debug.h"
#include "timer.h"
#include "lights.h"
#include "tests.h"
#include "radio.h"

#include "anki/cozmo/robot/spineData.h"

#define SET_GREEN(v, b)  (b ? (v |= 0x00FF00) : (v &= ~0x00FF00))

static const u32 MAX_FAILED_TRANSFER_COUNT = 18000; // 1.5m for auto shutdown (if not on charger)

GlobalDataToHead g_dataToHead;
GlobalDataToBody g_dataToBody;

extern void EnterRecovery(void) {
  __enable_irq();
  __asm { SVC 0 };
}

int main(void)
{
  u32 failedTransferCount = 0;

  // Initialize the hardware peripherals
  Battery::init();
  TimerInit();
  Motors::init();   // Must init before power goes on
  Head::init();
  Lights::init();

  UART::print("\r\nUnbrick me now...");
  u32 t = GetCounter();
  while ((GetCounter() - t) < 500 * COUNT_PER_MS)  // 0.5 second unbrick time
    ;
  UART::print("too late!\r\n");

  Radio::init();
  Battery::powerOn();

  TestFixtures::run();

  u32 timerStart = GetCounter();
  for (;;)
  {
    // Only call every loop through - not all the time
    Head::manage();
    Motors::update();
    Battery::update();

    #ifndef BACKPACK_DEMO
    // TEMPORARY FACTORY TEST CODE
    SET_GREEN(g_dataToBody.backpackColors[1], Battery::onContacts);
    SET_GREEN(g_dataToBody.backpackColors[2], true);
    
    Lights::manage(g_dataToBody.backpackColors);
    #endif

    // Update at 200Hz (5ms delay) - with unsigned subtract to handle wraparound
    const u32 DELAY = CYCLES_MS(5.0f);
    while (GetCounter() - timerStart < DELAY)
      ;
    timerStart += DELAY;

    // Verify the source
    if (!Head::spokenTo)
    {
      // Turn off the system if it hasn't talked to the head for a minute
      if(++failedTransferCount > MAX_FAILED_TRANSFER_COUNT)
      {
        #ifndef RADIO_TIMING_TEST
        Battery::powerOff();
        return -1;
        #endif
      }
    } else {
      failedTransferCount = 0;
      // Copy (valid) data to update motors
      for (int i = 0; i < MOTOR_COUNT; i++)
      {
        Motors::setPower(i, g_dataToBody.motorPWM[i]);
      }
    }
  }
}

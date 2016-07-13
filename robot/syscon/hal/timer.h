#ifndef TIMER_H
#define TIMER_H

#include "nrf.h"
#include "portable.h"

// Each count is 1/2^23 seconds, so 8,388.608 counts equals one millisecond
// The counter is quantized to 256 counts, so it updates only every 30.51uS
// Wraps every 512 seconds - use unsigned overflow math to hide wrapping
#define COUNT_PER_MS (8389)
#define CYCLES_MS(ms) (int)(32768 * 256.0f * ms / 1000.0f)
#define CYCLES_TO_COUNT(c) (((c) + 128) / 256)

// Initialize the RTC peripheral
#ifdef __cplusplus
namespace Timer {
  void init();
  void lowPowerMode(bool power);
}
#endif

// Get the counter - with each tick being ~120 ns (see COUNT_PER_MS)
// XXX: Mike observed the counter jumping back, sometimes.  Is it still happening?
#define GetCounter() ((u32)(NRF_RTC1->COUNTER << 8))
#define GetFrame() (NRF_RTC1->COUNTER * (NRF_RTC1->PRESCALER + 1) / (int)(32768 / 30.00f)) // 33ms

// Wait in a loop for the specified amount of microseconds
void MicroWait(u32 microseconds);

#endif

#include <string.h>
#include <stdint.h>

#include "MK02F12810.h"

#include "spi.h"
#include "timer.h"
#include "portable.h"
#include "../../k02_hal/hal/hardware.h"
#include "../../include/anki/cozmo/robot/rec_protocol.h"

uint16_t Anki::Cozmo::HAL::SPI::readWord(void) {
  while(~SPI0_SR & SPI_SR_RFDF_MASK) ;  // Wait for a byte
  uint16_t ret = SPI0_POPR;
 
  SPI0_SR = SPI0_SR;
  return ret;
}

void Anki::Cozmo::HAL::SPI::sync(void) {
  for (;;) {
    SPI0_SR = SPI0_SR;

    SPI0_PUSHR_SLAVE = STATE_SYNC;
    PORTE_PCR17 = PORT_PCR_MUX(2);    // SPI0_SCK (enabled)
    
    // Make sure we are bit syncronized to the espressif
    {
      const int loops = 16;

      for (int i = 0; i < 100; i++) readWord();

      bool restart = false;
      for(int i = 0; i < loops && !restart; i++) {
        uint16_t t = readWord();
        if (t && t != 0x8000) restart = true;
      }
      
      if (!restart) return ;
    }
    
    PORTE_PCR17 = PORT_PCR_MUX(0);    // SPI0_SCK (disabled)
  }
}

void Anki::Cozmo::HAL::SPI::espInit(void) {
  // Wait for Espressif to toggle out 4 words of I2SPI
  for (int i = 0; i < 32; i++)
  {
    while (GPIO_READ(GPIO_WS) & PIN_WS)     ;
    while (!(GPIO_READ(GPIO_WS) & PIN_WS))  ;
  }
  
  // Switch to 10MHz Espressif/external reference and 100MHz clock
  MCG_C1 &= ~MCG_C1_IREFS_MASK;
  // Wait for IREF to turn off
  while((MCG->S & MCG_S_IREFST_MASK))   ;
  // Wait for FLL to lock
  while((MCG->S & MCG_S_CLKST_MASK))    ;

  MicroWait(100);     // Because of erratum e7735: Wait 2 IRC cycles (or 2/32.768KHz)
}

void Anki::Cozmo::HAL::SPI::init(void) {
  // Start vby increasing our clock speed, and getting synced up with the espressif
	espInit();
	
	// Turn on power to DMA, PORTC and SPI0
  SIM_SCGC6 |= SIM_SCGC6_SPI0_MASK;
  SIM_SCGC5 |= SIM_SCGC5_PORTD_MASK | SIM_SCGC5_PORTE_MASK;

  // Configure SPI pins
  PORTD_PCR0  = PORT_PCR_MUX(2) |  // SPI0_PCS0 (internal)
                PORT_PCR_PE_MASK;

  PORTD_PCR4  = PORT_PCR_MUX(1);
  GPIOD_PDDR &= ~(1 << 4);

  PORTE_PCR18 = PORT_PCR_MUX(2); // SPI0_SOUT
  PORTE_PCR19 = PORT_PCR_MUX(2); // SPI0_SIN

  // Configure SPI perf to the magical value of magicalness
  SPI0_MCR = SPI_MCR_DCONF(0) |
             SPI_MCR_SMPL_PT(0) |
             SPI_MCR_CLR_TXF_MASK |
             SPI_MCR_CLR_RXF_MASK;

  SPI0_CTAR0_SLAVE = SPI_CTAR_FMSZ(15);

  SPI0_RSER = SPI_RSER_TFFF_RE_MASK |
              SPI_RSER_TFFF_DIRS_MASK |
              SPI_RSER_RFDF_RE_MASK |
              SPI_RSER_RFDF_DIRS_MASK;

  // Clear all status flags
  SPI0_SR = SPI0_SR;

  sync();
}

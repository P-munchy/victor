#include <string.h>
#include <stdint.h>

#include "MK02F12810.h"

#include "anki/cozmo/robot/hal.h"
#include "anki/cozmo/robot/drop.h"
#include "hal/portable.h"
#include "clad/robotInterface/messageEngineToRobot.h"
#include "clad/robotInterface/messageEngineToRobot_send_helper.h"

#include "spi.h"
#include "uart.h"
#include "dac.h"
#include "oled.h"
#include "i2c.h"
#include "wifi.h"
#include "watchdog.h"

typedef uint16_t transmissionWord;
const int RX_OVERFLOW = 8;  // Adjust this to fix screen - possibly at expense of camera
const int TX_SIZE = DROP_TO_WIFI_SIZE / sizeof(transmissionWord);
const int RX_SIZE = DROP_TO_RTIP_SIZE / sizeof(transmissionWord) + RX_OVERFLOW;

static DropToWiFi spi_backbuff[2];

extern DropToWiFi* spi_write_buff = &spi_backbuff[0];
static DropToWiFi* spi_tx_buff = &spi_backbuff[1];

transmissionWord spi_rx_buff[RX_SIZE];

static int totalDrops = 0;

static bool audioUpdated = false;
static uint8_t AudioBackBuffer[MAX_AUDIO_BYTES_PER_DROP];

void Anki::Cozmo::HAL::SPI::ManageDrop(void) {
  // This should probably bias
  DAC::Feed(AudioBackBuffer);
  DAC::EnableAudio(audioUpdated);
}

static bool ProcessDrop(void) {
  using namespace Anki::Cozmo::HAL;
  static int pwmCmdCounter = 0;

  // Process drop receive
  transmissionWord *target = spi_rx_buff;
  for (int i = 0; i < RX_OVERFLOW; i++, target++) {
    if (*target != TO_RTIP_PREAMBLE) continue ;
    
    DropToRTIP* drop = (DropToRTIP*)target;
    //Watchdog::kick(WDOG_WIFI_COMMS);
    
    // Buffer the data that needs to be fed into the devices for next cycle
    audioUpdated = drop->droplet & audioDataValid;
    memcpy(AudioBackBuffer, drop->audioData, MAX_AUDIO_BYTES_PER_DROP);

    if (drop->droplet & screenDataValid) {
      OLED::FeedFace(drop->droplet & screenRectData, drop->screenData);
    }

    uint8_t *payload_data = (uint8_t*) drop->payload;
    totalDrops++;
    
    if (drop->payloadLen)
    {
      uint8_t *payload_data = (uint8_t*)drop->payload;
      switch(payload_data[0])
      {
        // Handle OTA related messages here rather than in message dispatch loop so it's harder to break
        case Anki::Cozmo::RobotInterface::EngineToRobot::Tag_bootloadRTIP:
        {
          SPI::EnterRecoveryMode();
          break;
        }
        default:
        {
          Anki::Cozmo::HAL::WiFi::ReceiveMessage(drop->payload, drop->payloadLen);
        }
      }
    }
    
    return true;
  }
  
  return false;
}

void Anki::Cozmo::HAL::SPI::StartDMA(void) {
  // Start sending out junk
  SPI0_MCR |= SPI_MCR_CLR_RXF_MASK;
  do  // Per erratum e8011: Repeat writes to SADDR, DADDR, or NBYTES until they stick
    DMA_TCD3_SADDR = (uint32_t)spi_write_buff;
  while (DMA_TCD3_SADDR != (uint32_t)spi_write_buff);
  DMA_ERQ |= DMA_ERQ_ERQ2_MASK | DMA_ERQ_ERQ3_MASK;
  
  // Swap buffers
  DropToWiFi *tmp = spi_write_buff;
  spi_write_buff = spi_tx_buff;
  spi_tx_buff = tmp;    // write_buff from last time is being sent right now
}

void Anki::Cozmo::HAL::SPI::FinalizeDrop(int jpeglen, const bool eof, const uint32_t frameNumber) { 
  DropToWiFi *drop_tx = spi_write_buff;

  drop_tx->preamble = TO_WIFI_PREAMBLE;
  
  while (jpeglen & 0x3) drop_tx->payload[jpeglen++] = 0xff;
  if (eof)
  {
    *((u32*)(drop_tx->payload + jpeglen)) = GetTimeStamp() - 70;
    jpeglen += 4;
    *((u32*)(drop_tx->payload + jpeglen)) = frameNumber;
    jpeglen += 4;
  }
	drop_tx->droplet = JPEG_LENGTH(jpeglen) | (eof ? jpegEOF : 0);
  uint8_t *drop_addr = drop_tx->payload + jpeglen;
  
  const int remainingSpace = DROP_TO_WIFI_MAX_PAYLOAD - jpeglen;
	drop_tx->payloadLen = Anki::Cozmo::HAL::WiFi::GetTxData(drop_addr, remainingSpace);
}

// This is Thors hammer.  Forces recovery mode
void Anki::Cozmo::HAL::SPI::EnterRecoveryMode(void) {
  static uint32_t* recovery_word = (uint32_t*) 0x20001FFC;
  static const uint32_t recovery_value = 0xCAFEBABE;

  *recovery_word = recovery_value;
  NVIC_SystemReset();
}

// This is the nice version, leave espressif synced and running
void Anki::Cozmo::HAL::SPI::EnterOTAMode(void) {
  // Disable watchdog
  __disable_irq();
  WDOG_UNLOCK = 0xC520;
  WDOG_UNLOCK = 0xD928;
  WDOG_STCTRLH = 0;

  // Start turning the lights off of all the things we will no longer be using
  SIM_SCGC6 &= ~(SIM_SCGC6_DMAMUX_MASK | SIM_SCGC6_FTM1_MASK | SIM_SCGC6_FTM2_MASK | SIM_SCGC6_PDB_MASK | SIM_SCGC6_DAC0_MASK);
  SIM_SCGC7 &= ~SIM_SCGC7_DMA_MASK;
  SIM_SCGC4 &= ~SIM_SCGC4_I2C0_MASK;
  
  // Flush our UART, and set it to idle
  UART0_C2 = 0;
  UART0_CFIFO = UART_CFIFO_TXFLUSH_MASK | UART_CFIFO_RXFLUSH_MASK ;

  // Fire the SVC handler in the boot-loader force the SVC to have a high priority because otherwise this will fault
  void (* const call)(void) = (void (* const)(void)) (*(uint32_t*) 0x2C);
  
  SCB->VTOR = 0;
  call();
};

extern "C"
void DMA2_IRQHandler(void) {
  using namespace Anki::Cozmo::HAL;
  DMA_CDNE = DMA_CDNE_CDNE(2);
  DMA_CINT = 2;

  I2C::Disable();
  
  // Don't check for silence, we had a drop
  if (ProcessDrop()) {
    return ;
  }

  // Check for silence in the 
  static const int MaximumSilence = 32;
  static transmissionWord lastWord = 0;
  static int SilentDrops = 0;

  if (lastWord != spi_rx_buff[0]) {
    lastWord = spi_rx_buff[0];
    SilentDrops = 0;
  } else if (++SilentDrops == MaximumSilence) {
    switch (lastWord) {
      case 0x8001:
        NVIC_SystemReset();
        break ;
      case 0x8002:
        SPI::EnterRecoveryMode();
        break ;
      case 0x8004:
        __disable_irq();
        break ;
    }
  }
}

extern "C"
void DMA3_IRQHandler(void) {
  DMA_CDNE = DMA_CDNE_CDNE(3);
  DMA_CINT = 3;
}

void Anki::Cozmo::HAL::SPI::InitDMA(void) {
  // Disable DMA
  DMA_ERQ &= ~DMA_ERQ_ERQ3_MASK & ~DMA_ERQ_ERQ2_MASK;

  // Configure receive buffer
  DMAMUX_CHCFG2 = (DMAMUX_CHCFG_ENBL_MASK | DMAMUX_CHCFG_SOURCE(14)); 

  DMA_TCD2_SADDR          = (uint32_t)&(SPI0_POPR);
  DMA_TCD2_SOFF           = 0;
  DMA_TCD2_SLAST          = 0;

  DMA_TCD2_DADDR          = (uint32_t)spi_rx_buff;
  DMA_TCD2_DOFF           = sizeof(transmissionWord);
  DMA_TCD2_DLASTSGA       = -sizeof(spi_rx_buff);

  DMA_TCD2_NBYTES_MLNO    = sizeof(transmissionWord);                   // The minor loop moves 32 bytes per transfer
  DMA_TCD2_BITER_ELINKNO  = RX_SIZE;                                    // Major loop iterations
  DMA_TCD2_CITER_ELINKNO  = RX_SIZE;                                    // Set current interation count  
  DMA_TCD2_ATTR           = (DMA_ATTR_SSIZE(1) | DMA_ATTR_DSIZE(1));    // Source/destination size (8bit)
 
  DMA_TCD2_CSR            = DMA_CSR_DREQ_MASK | DMA_CSR_INTMAJOR_MASK;  // clear ERQ @ end of major iteration

  // Configure transfer buffer
  DMAMUX_CHCFG3 = (DMAMUX_CHCFG_ENBL_MASK | DMAMUX_CHCFG_SOURCE(15)); 

  DMA_TCD3_SOFF           = sizeof(transmissionWord);
  DMA_TCD3_SLAST          = -sizeof(spi_tx_buff);

  DMA_TCD3_DADDR          = (uint32_t)&(SPI0_PUSHR_SLAVE);
  DMA_TCD3_DOFF           = 0;
  DMA_TCD3_DLASTSGA       = 0;

  DMA_TCD3_NBYTES_MLNO    = sizeof(transmissionWord);                   // The minor loop moves 32 bytes per transfer
  DMA_TCD3_BITER_ELINKNO  = TX_SIZE;                          // Major loop iterations
  DMA_TCD3_CITER_ELINKNO  = TX_SIZE;                          // Set current interation count
  DMA_TCD3_ATTR           = (DMA_ATTR_SSIZE(1) | DMA_ATTR_DSIZE(1));    // Source/destination size (8bit)
 
  DMA_TCD3_CSR            = DMA_CSR_DREQ_MASK | DMA_CSR_INTMAJOR_MASK;  // clear ERQ @ end of major iteration

  NVIC_EnableIRQ(DMA2_IRQn);
  NVIC_EnableIRQ(DMA3_IRQn);
}

inline uint16_t WaitForByte(void) {
  while(~SPI0_SR & SPI_SR_RFDF_MASK) ;  // Wait for a byte    
  uint16_t ret = SPI0_POPR;
  SPI0_SR = SPI0_SR;
  return ret;
}

void SyncSPI(void) {
  // Syncronize SPI to WS
  __disable_irq();
  
  for (;;) {
    // Flush SPI
    SPI0_MCR = SPI_MCR_CLR_TXF_MASK |
               SPI_MCR_CLR_RXF_MASK;
    SPI0_SR = SPI0_SR;

    SPI0_PUSHR_SLAVE = 0xAAA0;
    PORTE_PCR17 = PORT_PCR_MUX(2);    // SPI0_SCK (enabled)

    WaitForByte();
    
    // Make sure we are talking to the perf
    {
      const int loops = 3;
      bool success = true;

      for(int i = 0; i < loops; i++) {
        WaitForByte();
        if (WaitForByte() != 0x8000) success = false ;
      }

      if (success) break ;
    }
    
    PORTE_PCR17 = PORT_PCR_MUX(0);    // SPI0_SCK (disabled)
  }
  
  __enable_irq();
}

void Anki::Cozmo::HAL::SPI::Init(void) {
	// Turn on power to DMA, PORTC and SPI0
  SIM_SCGC6 |= SIM_SCGC6_SPI0_MASK | SIM_SCGC6_DMAMUX_MASK;
  SIM_SCGC7 |= SIM_SCGC7_DMA_MASK;

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
  

  SPI::InitDMA();
  SyncSPI();
}

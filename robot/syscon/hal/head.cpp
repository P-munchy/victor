#include "string.h"

#include "debug.h"
#include "head.h"
#include "timer.h"
#include "nrf.h"
#include "nrf_gpio.h"

#include "radio.h"
#include "rtos.h"
#include "hardware.h"

#include "anki/cozmo/robot/spineData.h"

#define MAX(a, b) ((a > b) ? a : b)

uint8_t txRxBuffer[MAX(sizeof(GlobalDataToBody), sizeof(GlobalDataToHead))];

enum TRANSMIT_MODE {
  TRANSMIT_SEND,
  TRANSMIT_RECEIVE,
  TRANSMIT_DEBUG
};

static const int DEBUG_BYTES = 32;

static int txRxIndex;
static int debugSafeWords;
static TRANSMIT_MODE uart_mode;

bool Head::spokenTo;

extern GlobalDataToHead g_dataToHead;
extern GlobalDataToBody g_dataToBody;

extern void EnterRecovery(void);
static void setTransmitMode(TRANSMIT_MODE mode);

void Head::init() 
{
  // Sync pattern
  memset(&g_dataToBody, 0, sizeof(g_dataToBody));
  g_dataToHead.source = SPI_SOURCE_BODY;
  Head::spokenTo = false;
  txRxIndex = 0;

  // Power on the peripheral
  NRF_UART0->POWER = 1;

  // Disable parity and hardware flow-control
  NRF_UART0->CONFIG = 0;

  // Enable the peripheral and start the tasks
  NRF_UART0->ENABLE = UART_ENABLE_ENABLE_Enabled << UART_ENABLE_ENABLE_Pos;
  NRF_UART0->TASKS_STARTTX = 1;
  NRF_UART0->TASKS_STARTRX = 1;

  // Initialize the UART for the specified baudrate
  NRF_UART0->BAUDRATE = NRF_BAUD(spine_baud_rate);

  // Extremely low priorty IRQ
  NRF_UART0->INTENSET = UART_INTENSET_TXDRDY_Msk | UART_INTENSET_RXDRDY_Msk;
  NVIC_SetPriority(UART0_IRQn, 1);
  NVIC_EnableIRQ(UART0_IRQn);

  // We begin in receive mode (slave)
  setTransmitMode(TRANSMIT_RECEIVE);
  MicroWait(80);

  RTOS::schedule(Head::manage);
}

static void setTransmitMode(TRANSMIT_MODE mode) {
  switch (mode) {
    case TRANSMIT_SEND:
      // Prevent debug words from transmitting
      debugSafeWords = 0;

      NRF_UART0->PSELRXD = 0xFFFFFFFF;
      MicroWait(10);
      NRF_UART0->PSELTXD = PIN_TX_HEAD;

      // Configure pin so it is open-drain
      nrf_gpio_cfg_output(PIN_TX_HEAD);
      break ;
    case TRANSMIT_RECEIVE:
      nrf_gpio_cfg_input(PIN_TX_HEAD, NRF_GPIO_PIN_NOPULL);

      NRF_UART0->PSELTXD = 0xFFFFFFFF;
      MicroWait(10);
      NRF_UART0->PSELRXD = PIN_TX_HEAD;
      break ;
    case TRANSMIT_DEBUG:
      if (!UART::DebugQueue()) return ;

      NRF_UART0->PSELRXD = 0xFFFFFFFF;
      NRF_UART0->PSELTXD = PIN_TX_VEXT;

      // Configure pin so it is open-drain
      nrf_gpio_cfg_output(PIN_TX_HEAD);
      
      // We are in debug transmit mode, these are the safe bytes
      debugSafeWords = DEBUG_BYTES;
      uart_mode = TRANSMIT_DEBUG;
      
      NRF_UART0->TXD = UART::DebugChar();
      break;
  }
  
  // Clear our UART interrupts
  NRF_UART0->EVENTS_RXDRDY = 0;
  NRF_UART0->EVENTS_TXDRDY = 0;
  uart_mode = mode;
  txRxIndex = 0;
}

inline void transmitByte() { 
  NRF_UART0->TXD = txRxBuffer[txRxIndex++];
}

void Head::manage(void* userdata) {
  memcpy(txRxBuffer, &g_dataToHead, sizeof(GlobalDataToHead));
  txRxIndex = 0;

  setTransmitMode(TRANSMIT_SEND);
  transmitByte();
}

extern "C"
void UART0_IRQHandler()
{
  // We received a byte
  if (NRF_UART0->EVENTS_RXDRDY) {
    NRF_UART0->EVENTS_RXDRDY = 0;

    txRxBuffer[txRxIndex] = NRF_UART0->RXD;

    // Re-sync to header
    if (txRxIndex < 4) {
      const uint32_t head_target = SPI_SOURCE_HEAD;
      const uint8_t header = head_target >> (txRxIndex * 8);

      if(txRxBuffer[txRxIndex] != header) {
        txRxIndex = 0;
        return ;
      }
    }

    // We received a full packet
    if (++txRxIndex >= sizeof(GlobalDataToBody)) {
      memcpy(&g_dataToBody, txRxBuffer, sizeof(GlobalDataToBody));
      Head::spokenTo = true;
      
      // Secret recovery flag, set dark byte to zero, and set secret to a magic number
      if (g_dataToBody.recover == recovery_secret_code) {
        EnterRecovery();
      } else {
        setTransmitMode(TRANSMIT_DEBUG);
      }
    }
  }

  // We transmitted a byte
  if (NRF_UART0->EVENTS_TXDRDY) {
    NRF_UART0->EVENTS_TXDRDY = 0;

    switch(uart_mode) {
      case TRANSMIT_RECEIVE:
      case TRANSMIT_SEND:
        // We are in regular head transmission mode
        if (txRxIndex >= sizeof(GlobalDataToHead)) {
          setTransmitMode(TRANSMIT_RECEIVE);
        } else {
          transmitByte();
        }
        break ;
      case TRANSMIT_DEBUG:
        if (debugSafeWords-- > 0) {
          // We are stuffing debug words
          if (UART::DebugQueue()) {
            NRF_UART0->TXD = UART::DebugChar();
            return ;
          }
        }

        break ;
    }
  }
}

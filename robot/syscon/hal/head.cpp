#include "string.h"

#include "debug.h"
#include "head.h"
#include "timer.h"
#include "nrf.h"
#include "nrf_gpio.h"
#include "lights.h"

#include "radio.h"
#include "rtos.h"
#include "hardware.h"
#include "backpack.h"

#include "anki/cozmo/robot/spineData.h"
#include "anki/cozmo/robot/logging.h"
#include "clad/robotInterface/messageEngineToRobot.h"

#include "spine.h"

using namespace Anki::Cozmo;

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

bool Head::spokenTo = false;

extern GlobalDataToHead g_dataToHead;
extern GlobalDataToBody g_dataToBody;

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
  NVIC_SetPriority(UART0_IRQn, UART_PRIORITY);
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
      #ifndef DUMP_DISCOVER
      nrf_gpio_cfg_input(PIN_TX_HEAD, NRF_GPIO_PIN_NOPULL);

      NRF_UART0->PSELTXD = 0xFFFFFFFF;
      MicroWait(10);
      NRF_UART0->PSELRXD = PIN_TX_HEAD;
      break ;
      #endif
    case TRANSMIT_DEBUG:
      if (!UART::DebugQueue()) return ;

      NRF_UART0->PSELRXD = 0xFFFFFFFF;
      NRF_UART0->PSELTXD = PIN_TX_VEXT;

      // Configure pin so it is open-drain
      nrf_gpio_cfg_output(PIN_TX_HEAD);
      
      // We are in debug transmit mode, these are the safe bytes
      debugSafeWords = DEBUG_BYTES;
      uart_mode = TRANSMIT_DEBUG;
      
      UART::DebugChar();
      break;
  }
  
  // Clear our UART interrupts
  NRF_UART0->EVENTS_RXDRDY = 0;
  NRF_UART0->EVENTS_TXDRDY = 0;
  uart_mode = mode;
  txRxIndex = 0;
}

static inline void transmitByte() { 
  NVIC_DisableIRQ(UART0_IRQn);
  NRF_UART0->TXD = txRxBuffer[txRxIndex++];
  NVIC_EnableIRQ(UART0_IRQn);
}

void Head::manage(void* userdata) {
  Spine::Dequeue(&(g_dataToHead.cladBuffer));
  memcpy(txRxBuffer, &g_dataToHead, sizeof(GlobalDataToHead));
  g_dataToHead.cladBuffer.length = 0;
  txRxIndex = 0;

  setTransmitMode(TRANSMIT_SEND);
  transmitByte();
}

extern void EnterRecovery(void);

static void Process_bootloadBody(const RobotInterface::BootloadBody& msg)
{
  EnterRecovery();
}
static void Process_setBackpackLights(const RobotInterface::BackpackLights& msg)
{
  Backpack::setLights(msg.lights);
}
static void Process_setCubeLights(const CubeLights& msg)
{
  Radio::setPropLights(msg.objectID, msg.lights);
}

static void Process_setPropSlot(const SetPropSlot& msg)
{
  Radio::assignProp(msg.slot, msg.factory_id);
}

static void Process_killBodyCode(const KillBodyCode& msg)
{
  // This will destroy the first sector in the application layer
  NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Een << NVMC_CONFIG_WEN_Pos;
  while (NRF_NVMC->READY == NVMC_READY_READY_Busy) ;
  NRF_NVMC->ERASEPAGE = 0x18000;
  while (NRF_NVMC->READY == NVMC_READY_READY_Busy) ;
  NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren << NVMC_CONFIG_WEN_Pos;
  while (NRF_NVMC->READY == NVMC_READY_READY_Busy) ;
}

static void ProcessMessage()
{
  using namespace Anki::Cozmo;
  
  const u8 tag = g_dataToBody.cladBuffer.data[0];
  if (g_dataToBody.cladBuffer.length == 0 || tag == RobotInterface::GLOBAL_INVALID_TAG)
  {
    // pass
  }
  else if (tag > RobotInterface::TO_BODY_END)
  {
    AnkiError( 139, "Spine.ProcessMessage", 384, "Body received message %x that seems bound above", 1, tag);
  }
  else
  {
    RobotInterface::EngineToRobot& msg = *reinterpret_cast<RobotInterface::EngineToRobot*>(&g_dataToBody.cladBuffer);
    switch(tag)
    {
      #include "clad/robotInterface/messageEngineToRobot_switch_from_0x01_to_0x2F.def"
      default:
        AnkiError( 140, "Head.ProcessMessage.BadTag", 385, "Message to body, unhandled tag 0x%x", 1, tag);
    }
  }
}

extern "C"
void UART0_IRQHandler()
{
  static int header_shift = 0;

  // We received a byte
  if (NRF_UART0->EVENTS_RXDRDY) {
    NRF_UART0->EVENTS_RXDRDY = 0;

    // Re-sync to header
    if (txRxIndex < 4) {
      header_shift = (header_shift >> 8) | (NRF_UART0->RXD << 24);
      
      if (header_shift == SPI_SOURCE_HEAD) {
        txRxIndex = 4;
        return ;
      }
    } else {
      txRxBuffer[txRxIndex] = NRF_UART0->RXD;
    }

    // We received a full packet
    if (++txRxIndex >= sizeof(GlobalDataToBody)) {
      RTOS::kick(WDOG_UART);

      memcpy(&g_dataToBody, txRxBuffer, sizeof(GlobalDataToBody));
      ProcessMessage();
      Head::spokenTo = true;
      
      setTransmitMode(TRANSMIT_DEBUG);
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
          #ifdef DUMP_DISCOVER
          setTransmitMode(TRANSMIT_DEBUG);
          #else
          setTransmitMode(TRANSMIT_RECEIVE);
          header_shift = 0;
          #endif
        } else {
          transmitByte();
        }
        break ;
      case TRANSMIT_DEBUG:
        if (debugSafeWords-- > 0) {
          // We are stuffing debug words
          if (UART::DebugQueue()) {
            UART::DebugChar();
            return ;
          }
        }

        break ;
    }
  }
}

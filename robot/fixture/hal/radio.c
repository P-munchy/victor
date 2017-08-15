#include <string.h>
#include "hal/board.h"
#include "hal/portable.h"
#include "hal/uart.h"
#include "hal/testport.h"
#include "hal/display.h"
#include "hal/timers.h"
#include "../app/fixture.h"
#include "hal/cube.h"
#include "hal/flash.h"
#include "app/binaries.h"
#include "radio.h"

bool UpdateNRF(bool forceupdate);

const int BAUDRATE = 115200;
#define NRF_UART USART3

// Initialize the UART link to the radio
static void InitRadio()
{
  GPIO_InitTypeDef GPIO_InitStructure;
  USART_InitTypeDef USART_InitStructure;

  // Clock configuration
  RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);
  
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStructure.GPIO_Pin =  GPIOC_NRF_TX;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
  GPIO_Init(GPIOC, &GPIO_InitStructure);
  GPIO_PinAFConfig(GPIOC, PINC_NRF_TX, GPIO_AF_USART3);
  
  GPIO_InitStructure.GPIO_Pin =  GPIOC_NRF_RX;
  GPIO_Init(GPIOC, &GPIO_InitStructure);
  GPIO_PinAFConfig(GPIOC, PINC_NRF_RX, GPIO_AF_USART3);
  
  // TX/RX config
  USART_Cmd(NRF_UART, DISABLE);
  USART_InitStructure.USART_BaudRate = BAUDRATE;
  USART_InitStructure.USART_WordLength = USART_WordLength_8b;
  USART_InitStructure.USART_StopBits = USART_StopBits_1;
  USART_InitStructure.USART_Parity = USART_Parity_No;
  USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
  USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
  USART_Init(NRF_UART, &USART_InitStructure);  
  USART_Cmd(NRF_UART, ENABLE);
}

// Receive a byte from the radio if there is one, or return -1 if not
int GetChar()
{
  // Check for overrun
  if (NRF_UART->SR & USART_SR_ORE)
  {
    volatile int v;
    v = NRF_UART->SR;
    v = NRF_UART->DR;
  }
  
  if (NRF_UART->SR & USART_SR_RXNE)
    return NRF_UART->DR & 0xFF;
  return -1;
}

// Receive a byte from the radio, blocking until it arrives, or return -1 after timeout microseconds
int GetCharWait(int timeout)
{
  volatile u32 v;
  u32 status;
  u32 startTime;
  int value;
  
  // Check for overrun
  status = NRF_UART->SR;
  if (status & USART_SR_ORE)
  {
    v = NRF_UART->SR;
    v = NRF_UART->DR;
  }
  
  status = 0;
  value = -1;
  startTime = getMicroCounter();
  while (getMicroCounter() - startTime < timeout)
  {
    if (NRF_UART->SR & USART_SR_RXNE)
    {
      value = NRF_UART->DR & 0xFF;
      break;
    }
  }
  return value;
}

// Send a character to the radio
static void PutChar(u8 c)
{    
  NRF_UART->DR = c;
  while (!(NRF_UART->SR & USART_FLAG_TXE))
    ;
}

char g_mode = 'X';
static int8_t m_rssidat[9];
static bool rssi_valid = 0;

static u32 m_cubescan_id = 0;
static u8  m_cubescan_type = 0;
bool RadioGetCubeScan(u32 *out_id, u8 *out_type)
{
  if( !m_cubescan_id ) //have not received a new id
    return false;
  
  if( out_id != NULL )
    *out_id = m_cubescan_id;
  if( out_type != NULL )
    *out_type = m_cubescan_type;
  
  m_cubescan_id = 0;
  return true;
}

static int argbytes = 0;
void RadioPurgeBuffer(void)
{
  while( GetChar() >= 0 )
  {}
  argbytes = 0;
}

// Process incoming bytes from the radio - must call at least 12,000 times/second
void RadioProcess()
{
  // Just continue if no character
  int c = GetChar();
  if (-1 == c)
    return;
  
  // Otherwise, process messages or grab arguments
  static int msg = 0, argCnt = 0, cubeArg = 0, cubeType = 0;
  
  if (!argbytes) // Process messages
  {
    msg = c;
    if( g_mode >= 'S' && g_mode <= 'V' ) //cubescan modes. Ignore all other msg chars - we sometimes purge & resync rx; requires unique start char (not present in data fields).
    {
      if( msg == 'S' ) { // Cubescan sync character
        argbytes = 5; // type(1) + id(4)
        cubeArg = 0;  // reset arguments
        cubeType = 0; // "
      }
    }
    else
    {
      switch( msg ) {
        case 'C':       // Print cube ID
          argbytes = 4; // Cube message has 4 bytes
          cubeArg = 0;  // reset argument
          break;
        case 'R':       // Print RSSI data
          argbytes = 9; // RSSI message size
          argCnt = 0;   // reset counter
          break;
      case '1':
          PutChar(g_mode);  // Watchdogged - restore mode
          break;
      }
    }
  }
  else // Grab arguments
  {
    switch( msg )
    {
      case 'C':
        argbytes--;
        cubeArg |= (c << (8*argbytes)); // XXX: This byteswaps because I'm used to IDs being byteswapped
        if (!argbytes) // If we have a whole argument, print it out
          ConsolePrintf("cube,%c,%08x\r\n", msg, cubeArg);
        break;
        
      case 'S':
        argbytes--;
        if( argbytes >= 4 )
          cubeType = c;
        else
          cubeArg |= (c << (8*argbytes));
        
        //After rx complete, latch data for async readout
        if (!argbytes) {
          m_cubescan_id = cubeArg;
          m_cubescan_type = cubeType;
          //ConsolePrintf("cubescan,%C%,%u,%08x\r\n", msg, cubeType, cubeArg);
        }
        break;
        
      case 'R':
        argbytes--;
        m_rssidat[argCnt++] = (s8)c; //signed RSSI value
        if( !argbytes ) {
          rssi_valid = 1;
          /*
          ConsolePrintf("rssi");
          for(int i=0; i<9; i++)
            ConsolePrintf(",%02i", m_rssidat[i] );
          ConsolePrintf("\r\n");
          */
        }
        break;
      default:
        argbytes=0;
        break;
    }
  }
}

// Put the radio into a specific test mode
void SetRadioMode(char mode, bool forceupdate )
{
#ifndef FCC
  InitRadio();
  // Try 5 times, since a buggy ISR in the NRF clobbers the update attempt
  for (int i = 5; i >= 0; i--)
    try {
      GetChar();
      bool isRadioOK = UpdateNRF(forceupdate || i == 0);   // Always force-update on last attempt
      // Wait for sign-on message
      int c;
      do {
        c = GetCharWait(1000000);
        if (-1 == c)
          throw ERROR_RADIO_TIMEOUT;
      } while ('!' != c);
      break;
    } catch (int e) {
      if (i == 0)
        throw e;
    }

  g_mode = mode;
  PutChar(g_mode);
#endif
}

void RadioGetRssi( int8_t out_rssi[9] )
{
  //Must be in idle mode for rssi reading
  if( g_mode != 'I' )
    SetRadioMode('I');
  
  rssi_valid = 0;
  PutChar('R'); //initiate an RSSI read (reverts to idle when complete)
  
  //Wait for response packet
  u32 start = getMicroCounter();
  while( !rssi_valid ) { //spin on rx
    RadioProcess();
    if( getMicroCounter() - start >= 1000*1000 )
      throw ERROR_RADIO_TIMEOUT;
  }

  if( out_rssi != NULL )
    memcpy( out_rssi, m_rssidat, sizeof(m_rssidat) );
}


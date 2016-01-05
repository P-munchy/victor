#ifndef HAL_H__
#define HAL_H__

//#include <string.h>
#include "nrf24le1.h"
#include "hal_clk.h"
#include "hal_delay.h"
#include "portable.h"
#include <absacc.h>    /* Include Macro Definitions */

// XXX: if timeout, don't send data back

#define TAP_THRESH 10
#define ADV_CHANNEL 81
#define CHANNEL_RATE 	HAL_NRF_1MBPS
#define RADIO_PAYLOAD_LENGTH 17

// Tests
//#define DO_SIMPLE_LED_TEST
//#define DO_LED_TEST
//#define DO_TAPS_TEST
//#define STREAM_ACCELEROMETER

// Debug
//#define VERIFY_TRANSMITTER
//#define DEBUG_PAYLOAD
//#define DO_MISSED_PACKET_TEST

//#define SNIFFER

// Board definition
//#define IS_CHARGER
//#define EMULATE_BODY
#define COMPATIBILITY_MODE_4P0

//#define RELEASE

#if defined(SNIFFER)
#define USE_EVAL_BOARD
#define USE_UART
#endif

#if defined(EMULATE_BODY)
#define USE_EVAL_BOARD
#define USE_UART
#endif

#if defined(STREAM_ACCELEROMETER) || defined(DO_TAPS_TEST) || defined(DEBUG_PAYLOAD) || defined(DO_MISSED_PACKET_TEST) || defined(VERIFY_TRANSMITTER) || defined(EMULATE_BODY)
#define USE_UART
#endif

#ifdef EMULATE_BODY
static enum cubeState
{
  eScan,
  eSync, // dummy state
  eRespond,
  eMainLoop,
  eAdvertise // dummy state
};
#else
static enum cubeState
{
  eAdvertise, 
  eSync,
  eInitializeMain,
  eMainLoop
};
#endif

// lights.c

static enum DebugLights
{
  debugLedAdvertise = 3, // red
  debugLedI2CError = 4, // green
  debugLedAccError = 5 // blue
};

void SetLedValues(char *newValues);
void SetLedValuesByDelta();
void SetLedValue(char led, char value);
void InitTimer2();
void LightsOff();
void LightOn(char i);
void StartTimer2();
void StopTimer2();


#ifndef USE_EVAL_BOARD
// acc.c
void InitAcc();
void ReadAcc(u8 *accData);
u8 GetTaps();
#endif

#ifdef USE_UART
// uart.c
void InitUart();
void PutDec(char i)reentrant;
char PutChar(char c);
void PutString(char *s);
void PutHex(char h);
#endif

// tests.c
void RunTests();

// radio.c
#ifdef COMPATIBILITY_MODE_4P0
#define CHANNEL_4P0 	83
#undef RADIO_PAYLOAD_LENGTH
#undef CHANNEL_RATE
#define RADIO_PAYLOAD_LENGTH 13
#define CHANNEL_RATE 	HAL_NRF_250KBPS
/*
Cube PCB Num      ID
1                 0xB4
2                 0xB3
3                 0xB2
4                 0xB5
*/
#define BLOCK_ID 0xB4
static const u8 code ADDRESS_4P0[5] = {BLOCK_ID, 0xC2, 0xC2, 0xC2, 0xC2}; // default
#endif

#ifdef EMULATE_BODY
static const u8 code ADDRESS_X[5] =  {0xB2, 0xC2, 0xC2, 0xC2, 0xC2};
#endif

//static const u8 code ADDRESS_TX[5] = {0x52, 0x41, 0x43, 0x48, 0x4C}; // RACHL
//static const u8 code ADDRESS_RX_ADV[5] = {0x42, 0x52, 0x59, 0x41, 0x4E}; // BRYAN
static const u8 code ADDRESS_TX[5] = {0x52, 0xC2, 0xC2, 0xC2, 0xC2}; // RACHL
static const u8 code ADDRESS_RX_ADV[5] = {0x42, 0xC2, 0xC2, 0xC2, 0xC2}; // BRYAN


  
static struct RadioStruct
{
  volatile u8 COMM_CHANNEL;
  volatile u8 RADIO_INTERVAL_DELAY;
  volatile u8 RADIO_TIMEOUT_MSB;
  volatile u8 RADIO_WAKEUP_OFFSET;
  volatile const u8* ADDRESS_TX_PTR;
  volatile const u8* ADDRESS_RX_PTR;
};


static const u8 code MAX_MISSED_PACKETS = 3;

typedef enum eRadioTimerState
{
  radioSleep,
  radioWakeup
};

void InitTimer0();
bool ReceiveDataSync(u8 timeout50msTicks);
void ReceiveData(u8 msTimeout);
void TransmitData();

/* simple_string */
void simple_memset(s8* s1, s8 val, u8 n);
void simple_memcpy(s8* s1, s8* s2, u8 n);

#endif /* HAL_H__ */
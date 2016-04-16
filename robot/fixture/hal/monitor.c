// Based on Drive Testfix, updated for Cozmo EP1 Testfix
#include "hal/monitor.h"
#include "hal/timers.h"
#include "lib/stm32f2xx.h"
#include "lib/stm32f2xx_rcc.h"
#include "hal/console.h"

// These addresses are shifted left by 1 for the R/nW bit in the LSB
#define CHARGE_CONTACT_ADDRESS  (0x80)  // 8'b10000000
#define BATTERY_ADDRESS         (0x88)  // 8'b10001000
#define SET_VBAT_ADDRESS        (0x5E)  // 7'b0101111. (see MCP4018T datasheeet)

#define READ                    1

#define CLOCK_WAIT              5
#define GPIOB_SCL               9   // XXX: Backward for digital pot
#define GPIOB_SDA               8

static void I2C_Pulse(void)
{  
  GPIO_SET(GPIOB, GPIOB_SCL);
  MicroWait(CLOCK_WAIT);
  GPIO_RESET(GPIOB, GPIOB_SCL);
  MicroWait(CLOCK_WAIT);
}

static void I2C_Start(void)
{
  PIN_OUT(GPIOB, GPIOB_SDA);
  
  GPIO_SET(GPIOB, GPIOB_SDA);
  GPIO_SET(GPIOB, GPIOB_SCL);
  MicroWait(CLOCK_WAIT);
  GPIO_RESET(GPIOB, GPIOB_SDA);
  MicroWait(CLOCK_WAIT);
  GPIO_RESET(GPIOB, GPIOB_SCL);
  MicroWait(CLOCK_WAIT);
}

static void I2C_Stop(void)
{
  PIN_OUT(GPIOB, GPIOB_SDA);
  
  GPIO_RESET(GPIOB, GPIOB_SDA);
  MicroWait(CLOCK_WAIT);
  GPIO_SET(GPIOB, GPIOB_SCL);
  MicroWait(CLOCK_WAIT);
  GPIO_SET(GPIOB, GPIOB_SDA);
  MicroWait(CLOCK_WAIT);
}

static void I2C_ACK(void)
{
  PIN_OUT(GPIOB, GPIOB_SDA);
  
  GPIO_RESET(GPIOB, GPIOB_SDA);
  I2C_Pulse();
}

static void I2C_NACK(void)
{
  PIN_OUT(GPIOB, GPIOB_SDA);
  
  GPIO_SET(GPIOB, GPIOB_SDA);
  I2C_Pulse();
}

static void I2C_Put8(u8 data)
{
  for (int i = 7; i >= 0; i--)
  {
    if (data & (1 << i))
      I2C_NACK();
    else
      I2C_ACK();
  }
}

static u8 I2C_Get8(void)
{
  u8 value = 0;
  
  PIN_IN(GPIOB, GPIOB_SDA);
  
  for (int i = 0; i < 8; i++)
  {
    GPIO_SET(GPIOB, GPIOB_SCL);
    MicroWait(CLOCK_WAIT);
    
    value <<= 1;
    value |= (GPIO_READ(GPIOB) >> GPIOB_SDA) & 1;
    
    GPIO_RESET(GPIOB, GPIOB_SCL);
    MicroWait(CLOCK_WAIT);
  }
  
  return value;
}

static void I2C_Send8(u8 address, u8 data)
{
  I2C_Start();
  I2C_Put8(address);
  I2C_Pulse();  // Skip device ACK
  I2C_Put8(data);
  I2C_Pulse();  // Skip device ACK
  I2C_Stop();
}

static void I2C_Send16(u8 address, u8 reg, u16 data)
{
  I2C_Start();
  I2C_Put8(address);
  I2C_Pulse();  // Skip device ACK
  I2C_Put8(reg);
  I2C_Pulse();  // Skip device ACK
  I2C_Put8(data >> 8);
  I2C_Pulse();  // Skip device ACK
  I2C_Put8(data & 0xff);
  I2C_Pulse();  // Skip device ACK
  I2C_Stop();
}

static u16 I2C_Receive16(u8 address)
{
  u16 value = 0;
  
  I2C_Start();
  I2C_Put8(address | READ);
  I2C_Pulse();  // Skip device ACK
  value = I2C_Get8() << 8;
  I2C_ACK();
  value |= I2C_Get8();
  I2C_NACK();
  I2C_Stop();
  
  return value;
}

void InitMonitor(void)
{
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);
  
  // Setup PB8 and PB9 for I2C1
  // SCL
  GPIO_InitTypeDef   GPIO_InitStructure;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_OD;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
  GPIO_InitStructure.GPIO_Pin =  GPIO_Pin_8;
  GPIO_Init(GPIOB, &GPIO_InitStructure);
  
  // SDA
  GPIO_InitStructure.GPIO_Pin =  GPIO_Pin_9;
  GPIO_Init(GPIOB, &GPIO_InitStructure);
  
  // Let the lines float high
  GPIO_SET(GPIOB, GPIOB_SDA);
  GPIO_SET(GPIOB, GPIOB_SCL);
  
  // Setup the calibration register
  I2C_Send16(CHARGE_CONTACT_ADDRESS, 5, 0x75A5);  // Setup by TI's app... LSB = 20u
}

s32 MonitorGetCurrent(void)
{
  I2C_Send8(CHARGE_CONTACT_ADDRESS, 4);
  s16 value = I2C_Receive16(CHARGE_CONTACT_ADDRESS);
  return (s32)value * 20;
}

s32 MonitorGetVoltage(void)
{
  I2C_Send8(CHARGE_CONTACT_ADDRESS, 2);
  s16 value = I2C_Receive16(CHARGE_CONTACT_ADDRESS);
  return (s32)value;
}

void VBATMillivolts(int mv)
{
  // Make this fast to call
  static int currentmv = 2500;
  if (mv == currentmv)
    return;
  
  // Funky calculation goes like this:
  // Regulator wants:
  //    R1 = R2 [(VOUT / 1.25) � 1]
  //    R2 >= 25K
  //    Thus, VOUT = ((R1/R2)+1) * 1.25
  // VBAT controller gives:
  //    R1 = 100K - 787.4*value
  //    R2 = 787.4*value
  // "value" is between 32 (75&25K/5V) and 127 (0&100K/1.25V)
  // Or integer terms, mv = ((127-value)*1250/value) + 1250
  int minError = 10000, bestValue = 0;
  for (int i = 32; i < 128; i++)
  {
    int error = mv - (((127-i)*1250/i)+1250);
    if (error < 0) error = -error;    // Absolute value
    if (error < minError)
    {
      minError = error;
      bestValue = i;
    }
  }
  I2C_Send8(SET_VBAT_ADDRESS, bestValue);
  currentmv = mv;
}

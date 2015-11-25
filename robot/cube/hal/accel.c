#include "hal.h"
#include "hal_delay.h"
#include <intrins.h>  // for _nop_() delay

#define SOURCE_PWR      0
#define PIN_PWR         (1 << SOURCE_PWR)
#define GPIO_PWR        P0

#define SOURCE_SDA      2
#define PIN_SDA         (1 << SOURCE_SDA)
#define GPIO_SDA        P0

#define SOURCE_SCL      3
#define PIN_SCL         (1 << SOURCE_SCL)
#define GPIO_SCL        P0

// I2C addresses
#define I2C_ADDR      0x18 // 7-bit slave address
#define I2C_ADDR_ALT  0x19 // 7-bit slave address

// IMU Chip IDs
#define CHIPID          0xF8

// Accelerometer Register Map
#define BGW_CHIPID      0x00
#define ACCD_X_LSB      0x02
#define ACCD_X_MSB      0x03
#define ACCD_Y_LSB      0x04
#define ACCD_Y_MSB      0x05
#define ACCD_Z_LSB      0x06
#define ACCD_Z_MSB      0x07
#define ACCD_TEMP       0x08
#define INT_STATUS_0    0x09
#define INT_STATUS_1    0x0A
#define INT_STATUS_2    0x0B
#define INT_STATUS_3    0x0C
#define ACC_FIFO_STATUS     0x0E
#define PMU_RANGE       0x0F
#define PMU_BW          0x10
#define PMU_LPW         0x11
#define PMU_LOW_POWER   0x12
#define ACCD_HBW        0x13
#define BGW_SOFTRESET   0x14
#define INT_EN_0        0x16
#define INT_EN_1        0x17
#define INT_EN_2        0x18
#define INT_MAP_0       0x19
#define INT_MAP_1       0x1A
#define INT_MAP_2       0x1B
#define INT_SRC         0x1E
#define INT_OUT_CTRL    0x20
#define INT_RST_LATCH   0x21
#define INT_0           0x22
#define INT_1           0x23
#define INT_2           0x24
#define INT_3           0x25
#define INT_4           0x26
#define INT_5           0x27
#define INT_6           0x28
#define INT_7           0x29
#define INT_8           0x2A
#define INT_9           0x2B
#define INT_A           0x2C
#define INT_B           0x2D
#define INT_C           0x2E
#define INT_D           0x2F
#define FIFO_CONFIG_0   0x30
#define PMU_SELF_TEST   0x32
#define TRIM_NVM_CTRL   0x33
#define BGW_SPI3_WDT    0x34
#define OFC_CTRL        0x36
#define OFC_SETTING     0x37
#define OFC_OFFSET_X    0x38
#define OFC_OFFSET_Y    0x39
#define OFC_OFFSET_Z    0x3A
#define TRIM_GP0        0x3B
#define TRIM_GP1        0x3C
#define FIFO_CONFIG_1   0x3E
#define FIFO_DATA       0x3F


// Accelerometer Register values  // XXX check these values
#define RANGE_2G            0x03
#define RANGE_4G            0x05
#define RANGE_8G            0x08
#define RANGE_16G           0x0B

#define BW_7_81             0x08
#define BW_15_63            0x09
#define BW_31_25            0x0A
#define BW_62_5             0x0B
#define BW_125              0x0C
#define BW_250              0x0D
#define BW_500              0x0E
#define BW_1000             0x0F

#define FIFO_BYPASS         0 << 6
#define FIFO_FIFO           1 << 6
#define FIFO_STREAM         2 << 6
#define FIFO_XYZ            0
#define FIFO_X              1
#define FIFO_Y              2
#define FIFO_Z              3

#define ACC_INT_OPEN_DRAIN  0x0F  // Active high, open drain

extern volatile u8 gCubeState; // XXX fix this global mess

const u8 code I2C_ACK  = 0;
const u8 code I2C_NACK  = 1;
const u8 code I2C_READ_BIT = 1;    
const u8 code I2C_WRITE_BIT = 0;

//const u8 I2C_WAIT = 1;    

static s8 newAcc[3];




static void DriveSCL(u8 b)
{
  if(b)
    GPIO_SET(GPIO_SCL, PIN_SCL);
  else
    GPIO_RESET(GPIO_SCL, PIN_SCL);
  _nop_();
}


static void DriveSDA(u8 b)
{
  if(b)
    GPIO_SET(GPIO_SDA, PIN_SDA);
  else
    GPIO_RESET(GPIO_SDA, PIN_SDA);
  _nop_();
}

// Read SDA bit
static u8 ReadSDA()
{ 
  return !!(GPIO_READ(GPIO_SDA) & PIN_SDA);
}


// Issue a Start condition for I2C address with Read/Write bit
static void Start()
{
  DriveSDA(0);
  DriveSCL(0);
}

// Issue a Stop condition
static void Stop()
{
  DriveSDA(0);
  DriveSCL(1);
  DriveSDA(1);
}

static u8 Read(u8 ack)
{
  u8 b = 0, i;
  PIN_IN(P0DIR, PIN_SDA);
  _nop_();
  for (i = 0; i < 8; i++)
  {
    b <<= 1;
    DriveSCL(1);
    b |= ReadSDA();
    DriveSCL(0);
  }
  PIN_OUT(P0DIR, PIN_SDA);
  _nop_();
  // send Ack or Nak
  DriveSDA(ack);
  DriveSCL(1);
  DriveSCL(0);

  return b;
}

// Write byte and return true for Ack or false for Nak
static u8 Write(u8 b)
{
  u8 m;
  // Write byte
  for (m = 0x80; m != 0; m >>= 1)
  {
    DriveSDA(m & b);
    DriveSCL(1);
    DriveSCL(0);
  }
  
  DriveSDA(0);
  PIN_IN(P0DIR, PIN_SDA);
  _nop_();
  DriveSCL(1); 
  b = ReadSDA();
  PIN_OUT(P0DIR, PIN_SDA);
  _nop_();
  DriveSCL(0);
  
  return b;
}



static void VerifyAck(ack)
{
  if (ack == I2C_NACK)
  {
    // last ditch try
    if(ReadSDA() == I2C_ACK)
    {
      return;
    }
    while(1)
    {
      LightOn(0); // ERROR (RED)
      delay_ms(50);
      LightsOff();
      LightOn(1);
      delay_ms(50);
      LightsOff();
    }
  }
  return;
}

static u8 DataRead(u8 addr)
{
  u8 readData = 0x00;
  Start(); //start condition
  VerifyAck(Write(I2C_ADDR << 1 | I2C_WRITE_BIT)); //slave address send
  VerifyAck(Write(addr)); //word address send
  Stop();
  Start();
  VerifyAck(Write(I2C_ADDR << 1 | I2C_READ_BIT)); //slave address send
  readData = Read(I2C_NACK); //nack for last read
  Stop();
  
  return readData;
} //End of DataRead function

static u8 DataReadPrime(u8 addr)
{
  u8 readData = 0x00;
  Start(); //start condition
  Write(I2C_ADDR << 1 | I2C_WRITE_BIT); //slave address send
  Write(addr); //word address send
  Stop();
  Start();
  Write(I2C_ADDR << 1 | I2C_READ_BIT); //slave address send
  readData = Read(I2C_NACK); //nack for last read
  Stop();
  
  return readData;
} //End of DataRead function

 
static void DataReadMultiple(u8 addr, u8 numBytes, u8* buffer)
{
  u8 i=0;
  Start(); //start condition
  VerifyAck(Write(I2C_ADDR << 1 | I2C_WRITE_BIT)); //slave address send
  VerifyAck(Write(addr)); //word address send
  Stop();
  Start();
  VerifyAck(Write(I2C_ADDR << 1 | I2C_READ_BIT)); //slave address send
  while(i < numBytes)
  {
    if (i == (numBytes-1))
    {
      buffer[i] = Read(I2C_NACK); //nack for last read
    }
    else
    {
      buffer[i] = Read(I2C_ACK); 
    }
    i++;
  }
  Stop();

} //End of DataRead function


// Read multiple, but only keep MSB bytes
static void DataReadMultipleMsb(u8 addr, u8 numBytes, u8* buffer) 
{
  u8 i=0;
  Start(); //start condition
  VerifyAck(Write(I2C_ADDR << 1 | I2C_WRITE_BIT)); //slave address send
  VerifyAck(Write(addr)); //word address send
  Stop();
  Start();
  VerifyAck(Write(I2C_ADDR << 1 | I2C_READ_BIT)); //slave address send
  while(i < numBytes)
  {
    if (i == (numBytes-1))
    {
      Read(I2C_ACK); 
      buffer[i] = Read(I2C_NACK); //nack for last read
    }
    else
    {
      Read(I2C_ACK); 
      buffer[i] = Read(I2C_ACK); 
    }
    i++;
  }
  Stop();

} //End of DataReadMSB function
 
// FIFO diff read
u8 DataReadFifoTaps(u8 addr, u8 numBytes) 
{
  u8 i=0, taps = 0;
  s8 current;
  static s8 last = 0;
  static u8 debounce = 0;
  static bool posFirst;
  
  Start(); //start condition
  VerifyAck(Write(I2C_ADDR << 1 | I2C_WRITE_BIT)); //slave address send
  VerifyAck(Write(addr)); //word address send
  Stop();
  Start();
  VerifyAck(Write(I2C_ADDR << 1 | I2C_READ_BIT)); //slave address send
  while(i < numBytes)
  {
    if (i == (numBytes-1))
    {
      Read(I2C_ACK); 
      current = Read(I2C_NACK); //nack for last read
    }
    else
    {
      Read(I2C_ACK); 
      current = Read(I2C_ACK); 
    }
    //putchar(',');
//    puthex(current);
//    putchar(',');
    current >>= 2;
    i++;
    if(debounce == 0)
    {
      if( current-last > TAP_THRESH) // XXX
      {
        debounce = 45;
        posFirst = true;
      }
      else if ( current-last < -TAP_THRESH)
      {
        debounce = 45;
        posFirst = false;
      }
//      puthex(0);
    }
    else if(debounce > 40)
    {
      if( current-last > TAP_THRESH && posFirst == false) // XXX
      {
        taps++;
 //       puthex(1);
        debounce = 40;
      }
      else if ( current-last < -TAP_THRESH && posFirst == true)
      {
        taps++;
//        puthex(1);
        debounce = 40;
      }
      else
      {
 //       puthex(0);
        debounce--;
      }
    }
    else
    {
 //     puthex(0);
      debounce--;
    }
    last = current;
//    putstring("\r\n");
  }
  Stop();
  return taps;
} //End of DataRead function


static void DataWrite(u8 ctrlByte, u8 dataByte)
{
  Start(); //start condition     
  VerifyAck(Write(I2C_ADDR << 1 | I2C_WRITE_BIT)); //slave address send      
  VerifyAck(Write(ctrlByte)); //control byte send
  VerifyAck(Write(dataByte)); //write data send
  Stop();
  return;
}


// Initialize I2C
static void InitI2C()
{ 
  // Set pull up on I2C
  PIN_OUT(P0DIR, PIN_PWR);
  GPIO_SET(GPIO_PWR, PIN_PWR);
  
  // SDA output, normal drive strength
  P0CON = (0x000 << 5) | (0 << 4) | (0 << 3) | (SOURCE_SDA << 0);
  // SDA input, pull up
//  P0CON = (0x010 << 5) | (1 << 4) | (0 << 3) | (SOURCE_SDA << 0);
  // SCL output, normal drive strength
  P0CON = (0x000 << 5) | (0 << 4) | (0 << 3) | (SOURCE_SCL << 0);

  GPIO_SET(GPIO_SCL, PIN_SCL);
  GPIO_SET(GPIO_SDA, PIN_SDA);
  
  PIN_OUT(P0DIR, PIN_SCL);
  PIN_OUT(P0DIR, PIN_SDA);
 
}

static void WriteVerify(u8 ctrlByte, u8 dataByte)
{
  DataWrite(ctrlByte, dataByte);
  if(DataRead(ctrlByte) != dataByte)
  {
    while(1)
    {
      LightOn(0); // ERROR (RED)
      delay_ms(250);
      LightsOff();
      delay_ms(250);
    }
  }
  return;
}

// Initialize accelerometer
void InitAcc()
{
  simple_memset(newAcc, 0, sizeof(newAcc));
  
  delay_ms(5);
  InitI2C();
  delay_ms(5);

  DataReadPrime(BGW_CHIPID);
  if(DataRead(BGW_CHIPID) != CHIPID)
  {
    LightOn(0); // ERROR (RED) // XXX fix this
  }
  delay_ms(1);

  // 2G range
  WriteVerify(PMU_RANGE, RANGE_2G);
  delay_ms(1);
  
#ifdef STREAM_ACCELEROMETER  
  // Shadowing
  WriteVerify(ACCD_HBW, 0);
  delay_ms(1);
#endif
  // 250 Hz bandwidth
  WriteVerify(PMU_BW, BW_250);
  delay_ms(1);
  
  // Configure and enable FIFO
  WriteVerify(FIFO_CONFIG_1, FIFO_STREAM | FIFO_Z);
 
}

/*
accData should be size 6
*/
void ReadAcc(s8 *accData)
{
  DataReadMultiple(ACCD_X_LSB, 6, accData);
  #ifdef STREAM_ACCELEROMETER
  return;
  #else
  accData[0] = (s8)accData[1];
  accData[1] = (s8)accData[3];
  accData[2] = (s8)accData[5];  
  #endif
}

u8 GetTaps()
{
  u8 dat = DataRead(ACC_FIFO_STATUS);
  static u8 howMany = 0;
//  PutHex(dat);
//  PutString("\r\n");
  if(!!(dat & 1<<7)) // overrun
  {
    // Reset FIFO
    DataWrite(FIFO_CONFIG_1, FIFO_STREAM | FIFO_Z);
    howMany++;
    if(howMany > 1) // reset if we've had more than 1 overrun in a row. expect 1 during startup
    {  
      gCubeState = eAdvertise;
      // SoftReset accelerometer
      DataWrite(BGW_SOFTRESET,0xB6);
      delay_ms(2);
    }
  }
  else
  {
    howMany = 0;
    return DataReadFifoTaps(FIFO_DATA, (dat & 0x7F));
  }
  return 0;  
}
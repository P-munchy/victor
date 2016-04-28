#include <string.h>
#include "hal/board.h"
#include "hal/portable.h"
#include "hal/uart.h"

#include "hal/display.h"
#include "hal/timers.h"
#include "../app/fixture.h"
#include "../app/binaries.h"

#include "hal/cube.h"

// PROG = PA9 (was PA8)
// RESET = PC5
// CS = PA4 (was PB7)

// SPI pins
static GPIO_TypeDef* MOSI_PORT = GPIOA;
static GPIO_TypeDef* MISO_PORT = GPIOA;
static GPIO_TypeDef* SCK_PORT = GPIOA;
static const uint32_t MOSI_PIN = GPIO_Pin_7;
static const uint32_t MISO_PIN = GPIO_Pin_6;
static const uint32_t SCK_PIN = GPIO_Pin_5;
static const uint32_t MOSI_SOURCE = GPIO_PinSource7;
static const uint32_t MISO_SOURCE = GPIO_PinSource6;
static const uint32_t SCK_SOURCE = GPIO_PinSource5;

static const int CUBE_PAGE_SIZE = 128;
static const int CUBE_MAX_PROGRAM_SIZE = 8192;

enum CUBE_FSR_FLAGS {
  CUBE_FSR_ENDEBUG = 0x80,
  CUBE_FSR_STP = 0x40,
  CUBE_FSR_WEN = 0x20,
  CUBE_FSR_RDYN = 0x10,
  CUBE_FSR_INFEN = 0x08,
  CUBE_FSR_RDISMB = 0x04
};

enum CUBE_COMMANDS {
  CUBE_WREN = 0x06,
  CUBE_WRDIS = 0x04,
  CUBE_RDSR = 0x05,
  CUBE_WRSR = 0x01,
  CUBE_READ = 0x03,
  CUBE_PROGRAM = 0x02,
  CUBE_ERASE_PAGE = 0x52,
  CUBE_ERASE_ALL = 0x62,
  CUBE_RDFPCR = 0x89,
  CUBE_RDISMB = 0x85,
  CUBE_ENDEBUG = 0x86,

  CUBE_DUMMY = 0x00
};

void InitCube(void) {
  // Clock configuration
  RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
  RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
  RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);

  GPIO_InitTypeDef GPIO_InitStructure;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_25MHz;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_NOPULL;

  // Configure the pins for SPI in AF mode
  GPIO_PinAFConfig(MOSI_PORT, MOSI_SOURCE, GPIO_AF_SPI1);
  GPIO_PinAFConfig(MISO_PORT, MISO_SOURCE, GPIO_AF_SPI1);
  GPIO_PinAFConfig(SCK_PORT, SCK_SOURCE, GPIO_AF_SPI1);

  // Configure the SPI pins
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
  GPIO_InitStructure.GPIO_Pin = MOSI_PIN | MISO_PIN; 
  GPIO_Init(SCK_PORT, &GPIO_InitStructure);
  GPIO_InitStructure.GPIO_Pin = SCK_PIN;
  GPIO_Init(SCK_PORT, &GPIO_InitStructure);
  
  // Setup outputs
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
  
  // VDD pin - PB0
  GPIO_ResetBits(GPIOB, GPIO_Pin_0);
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
  GPIO_Init(GPIOB, &GPIO_InitStructure);
  
  // Pull PA4 (CS#) high.
  GPIO_SetBits(GPIOA, GPIO_Pin_4);
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4;
  GPIO_Init(GPIOA, &GPIO_InitStructure);
  
  // Pull PC5 (Reset) low. 
  GPIO_ResetBits(GPIOC, GPIO_Pin_5);
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5;
  GPIO_Init(GPIOC, &GPIO_InitStructure);

  // High-voltage PROG is off (floating) - low voltage PROG doesn't work
  GPIO_InitStructure.GPIO_OType = GPIO_OType_OD;
  GPIO_SetBits(GPIOA, GPIO_Pin_9);
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
  GPIO_Init(GPIOA, &GPIO_InitStructure);
  
  // Initialize SPI in master mode
  SPI_I2S_DeInit(SPI1);
  SPI_InitTypeDef SPI_InitStructure;
  SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
  SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
  SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
  SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;
  SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;
  SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
  SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_256;
  SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
  SPI_InitStructure.SPI_CRCPolynomial = 7;
  SPI_Init(SPI1, &SPI_InitStructure);
  SPI_Cmd(SPI1, ENABLE);

  SPI1->SR = 0;
}

static inline int PageCount(int length) {
  return (length + CUBE_PAGE_SIZE - 1) / CUBE_PAGE_SIZE;
}

static uint8_t CubeWrite(uint8_t data)
{
  while (!(SPI1->SR & SPI_FLAG_TXE)) ;
  SPI1->DR = data;
  
  // Make sure SPI is totally drained
  while (!(SPI1->SR & SPI_FLAG_TXE)) ;
  while (SPI1->SR & SPI_FLAG_BSY) ;
  
  return SPI1->DR;
}

static inline void CubeAssert(bool assert) {
  MicroWait(10);
  if (assert) {
    GPIO_ResetBits(GPIOA, GPIO_Pin_4); // #CS
  } else {
    GPIO_SetBits(GPIOA, GPIO_Pin_4); // #CS
  }
  MicroWait(10);
}

static inline void CubeSend(const uint8_t *arg, int arg_size) {
  while(arg_size-- > 0) {
    CubeWrite(*(arg++));
  }
}

static inline void CubeRecv(uint8_t *arg, int arg_size) {
  while(arg_size-- > 0) {
    *(arg++) = CubeWrite(CUBE_DUMMY);
  }
}

static uint8_t CubeReadFSR() {
  uint8_t reply;

  CubeAssert(true);
  CubeWrite(CUBE_RDSR);
  CubeRecv(&reply, sizeof(reply));
  CubeAssert(false);

  return reply;
}

static void CubeWriteEn() {
  CubeAssert(true);
  CubeWrite(CUBE_WREN);
  CubeAssert(false);
}

static inline void CubeBlockBusy(void) {
  while (CubeReadFSR() & CUBE_FSR_RDYN) ;
}

static void CubeErasePage(uint8_t page) {
/* Only on nRF24
  CubeAssert(true);
  CubeWrite(CUBE_ERASE_PAGE);
  CubeWrite(page);
  CubeAssert(false);

  CubeBlockBusy(); // Wait for page to be erased
*/
}

static inline void CubeProgram(int address, const uint8_t *data, int length) {
  CubeAssert(true);
  CubeWrite(CUBE_PROGRAM);
  CubeWrite(address >> 8);
  CubeWrite(address);
  CubeSend(data, length);
  CubeAssert(false);
  
  CubeBlockBusy(); // Wait for page to be erased
}

static inline void CubeRead(int address, uint8_t *data, int length) {
  CubeAssert(true);
  CubeWrite(CUBE_READ);
  CubeWrite(address >> 8);
  CubeWrite(address);
  CubeRecv(data, length);
  CubeAssert(false);
}

void LoadRom(const uint8_t *rom, int length) {
  SlowPrintf("Programming Cube");

  if (length > CUBE_MAX_PROGRAM_SIZE) {
    throw ERROR_CUBE_ROM_OVERSIZE;
  }

  for (int i = 0; i < PageCount(length); i++) {
    SlowPrintf("\nErasing %i", i);

    CubeWriteEn();
    if (~CubeReadFSR() & CUBE_FSR_WEN) { throw ERROR_CUBE_CANNOT_WRITE; }

    CubeErasePage(i);
  }

  const uint8_t *mem = rom;
  for (int addr = 0; addr < length; addr += CUBE_PAGE_SIZE, mem += CUBE_PAGE_SIZE) {
    int left =  length - addr;
    SlowPrintf("\nWriting %i", addr / CUBE_PAGE_SIZE);

    MicroWait(25000);
    
    CubeWriteEn();
    if (~CubeReadFSR() & CUBE_FSR_WEN) { throw ERROR_CUBE_CANNOT_WRITE; }

    CubeProgram(addr, mem, (left > CUBE_PAGE_SIZE) ? CUBE_PAGE_SIZE : left);
  }

  mem = rom;
  for (int addr = 0; addr < length; addr += CUBE_PAGE_SIZE, mem += CUBE_PAGE_SIZE) {
    int left =  length - addr;
    int send = (left > CUBE_PAGE_SIZE) ? CUBE_PAGE_SIZE : left;
      
    SlowPrintf("\nVerifying %i", addr / CUBE_PAGE_SIZE);

    uint8_t verify[CUBE_PAGE_SIZE];
    CubeRead(addr, verify, send);
    
    for (int i = 0; i < send; i++) {
      if (verify[i] != mem[i]) {
        throw ERROR_CUBE_VERIFY_FAILED;
      }
    }
  }
  
  SlowPrintf("\nDone         ");
}

int GetSequence(void);
extern FixtureType g_fixtureType;

void ProgramCube(void) {
  GPIO_ResetBits(GPIOA, GPIO_Pin_9);  // High-voltage PROG
  MicroWait(2000);
  PIN_OUT(GPIOC, 5);
  GPIO_ResetBits(GPIOC, GPIO_Pin_5);  // #Reset
  MicroWait(2000);
  GPIO_SetBits(GPIOC, GPIO_Pin_5);  // #Reset
  MicroWait(100000);

  if (g_fixtureType == FIXTURE_CUBEFCC_TEST)
    LoadRom(g_CubeFCC, g_CubeFCCEnd - g_CubeFCC); // FCC cube firmware
  else
    LoadRom(g_Cube, g_CubeEnd - g_Cube);          // Standard cube firmware

  // Check serial number from (possibly) last time
  // We don't want to reserialize the same block
  u32 serial;
  CubeRead(0x3ff0, (u8*)&serial, 4);
  SlowPrintf("Serial was: %08x\n", serial);
  if (serial != 0xffffffff) {
    SlowPrintf("Serial already set, won't set again\n");
  } else {
    serial = GetSequence();
    if (g_fixtureType == FIXTURE_CHARGER_TEST)
      serial |= 0x80000000;
    SlowPrintf("Setting to: %08x\n", serial);
    CubeWriteEn();
    CubeProgram(0x3ff0, (u8*)&serial, 4);
    CubeRead(0x3ff0, (u8*)&serial, 4);
    SlowPrintf("Serial is now: %08x\n", serial);
  }
    
  GPIO_ResetBits(GPIOC, GPIO_Pin_5);  // Put in #Reset
  GPIO_SetBits(GPIOA, GPIO_Pin_9);    // Turn off high-voltage PROG
}

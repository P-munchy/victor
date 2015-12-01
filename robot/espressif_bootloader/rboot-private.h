#ifndef __RBOOT_PRIVATE_H__
#define __RBOOT_PRIVATE_H__

//////////////////////////////////////////////////
// rBoot open source boot loader for ESP8266.
// Copyright 2015 Richard A Burton
// richardaburton@gmail.com
// See license.txt for license terms.
//////////////////////////////////////////////////

typedef int int32;
typedef unsigned int uint32;
typedef unsigned char uint8;

#include "rboot.h"

#define NOINLINE __attribute__ ((noinline))

#define CRYSTAL_FREQ 26000000
#define CPU_CLK_FREQ (80*(CRYSTAL_FREQ)/16000000)

#define ROM_MAGIC	   0xe9
#define ROM_MAGIC_NEW1 0xea
#define ROM_MAGIC_NEW2 0x04

#define TRUE 1
#define FALSE 0

// buffer size, must be at least sizeof(rom_header_new)
#define BUFFER_SIZE 0x100

// Small read offset for header at beginning of image
#define IMAGE_READ_OFFSET (4)

// esp8266 built in rom functions
extern uint32 SPIRead(uint32 addr, void *outptr, uint32 len);
extern uint32 SPIEraseSector(int);
extern uint32 SPIWrite(uint32 addr, void *inptr, uint32 len);
extern void ets_printf(char*, ...);
extern void ets_delay_us(int);
extern void ets_memset(void*, uint8, uint32);
extern void ets_memcpy(void*, const void*, uint32);

// functions we'll call by address
typedef void usercode(void);

// standard rom header
typedef struct {
	// general rom header
	uint8 magic;
	uint8 count;
	uint8 flags1;
	uint8 flags2;
	usercode* entry;
} rom_header;

typedef struct {
	uint8* address;
	uint32 length;
} section_header;

// new rom header (irom section first) there is
// another 8 byte header straight afterward the
// standard header
typedef struct {
	// general rom header
	uint8 magic;
	uint8 count; // second magic for new header
	uint8 flags1;
	uint8 flags2;
	usercode* entry;
	// new type rom, lib header
	uint32 add; // zero
	uint32 len; // length of irom section
} rom_header_new;

typedef enum {
  SPI_FLASH_RESULT_OK,
  SPI_FLASH_RESULT_ERR,
  SPI_FLASH_RESULT_TIMEOUT,
} SpiFlashOpResult;

#endif

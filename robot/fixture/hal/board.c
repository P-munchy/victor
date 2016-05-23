// Based on Drive Testfix, updated for Cozmo Testfix
#include "hal/board.h"
#include "hal/portable.h"
#include "hal/timers.h"

GPIO_TypeDef* GPIO_PORT[LEDn] = {LED1_GPIO_PORT, LED2_GPIO_PORT};
const uint16_t GPIO_PIN[LEDn] = {LED1_PIN, LED2_PIN};
const uint32_t GPIO_CLK[LEDn] = {LED1_GPIO_CLK, LED2_GPIO_CLK};

/**
  * @brief  Configures LED GPIO.
  * @param  Led: Specifies the Led to be configured. 
  * @retval None
  */
void STM_EVAL_LEDInit(Led_TypeDef Led)
{
  GPIO_InitTypeDef  GPIO_InitStructure;
  
  /* Enable the GPIO_LED Clock */
  RCC_AHB1PeriphClockCmd(GPIO_CLK[Led], ENABLE);


  /* Configure the GPIO_LED pin */
  GPIO_InitStructure.GPIO_Pin = GPIO_PIN[Led];
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(GPIO_PORT[Led], &GPIO_InitStructure);
  
  STM_EVAL_LEDOff(Led);
}

/**
  * @brief  Turns selected LED On.
  * @param  Led: Specifies the Led to be set on. 
  * @retval None
  */
void STM_EVAL_LEDOff(Led_TypeDef Led)
{
  GPIO_PORT[Led]->BSRRL = GPIO_PIN[Led];
}

/**
  * @brief  Turns selected LED Off.
  * @param  Led: Specifies the Led to be set off. 
  * @retval None
  */
void STM_EVAL_LEDOn(Led_TypeDef Led)
{
  GPIO_PORT[Led]->BSRRH = GPIO_PIN[Led];
}

/**
  * @brief  Toggles the selected LED.
  * @param  Led: Specifies the Led to be toggled. 
  * @retval None
  */
void STM_EVAL_LEDToggle(Led_TypeDef Led)
{
  GPIO_PORT[Led]->ODR ^= GPIO_PIN[Led];
}

#include "hal/console.h"

int InitBoard(void)
{
  GPIO_InitTypeDef  GPIO_InitStructure;
   
  RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
  RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
  RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);
  
  /* Initialize LEDs */
  STM_EVAL_LEDInit(LEDRED);
  STM_EVAL_LEDInit(LEDGREEN);

  STM_EVAL_LEDOff(LEDRED);
  STM_EVAL_LEDOff(LEDGREEN);
  
  // Always enable charger/ENCHG - I don't know why this signal exists
  GPIO_SetBits(GPIOA, GPIOA_ENCHG);
  GPIO_InitStructure.GPIO_Pin = GPIOA_ENCHG;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
  GPIO_Init(GPIOA, &GPIO_InitStructure);
  
  // Initialize PB12-PB15 as the ID inputs with pullups
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
  GPIO_Init(GPIOB, &GPIO_InitStructure);
  MicroWait(100);

  // PINC_CHGTX - default low (VEXT disabled)
  PIN_RESET(GPIOC, PINC_CHGTX);
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
  GPIO_InitStructure.GPIO_Pin = PINC_CHGTX;
  GPIO_Init(GPIOC, &GPIO_InitStructure);  
  
  // ENBAT_LC, ENBAT
  GPIO_InitStructure.GPIO_OType = GPIO_OType_OD;
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_2;
  GPIO_Init(GPIOC, &GPIO_InitStructure);
  // NBATSINK
  GPIO_InitStructure.GPIO_OType = GPIO_OType_OD;
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
  GPIO_Init(GPIOD, &GPIO_InitStructure);
  
  DisableBAT();

  // Read the ID pins with pull-down resistors on GPIOB[15:12]
  return (~(GPIO_READ(GPIOB) >> 12) & 15);
}

void EnableVEXT(void)
{
  PIN_SET(GPIOC, PINC_CHGTX);
  PIN_OUT(GPIOC, PINC_CHGTX);
}

void DisableVEXT(void)
{
  PIN_RESET(GPIOC, PINC_CHGTX);
  PIN_OUT(GPIOC, PINC_CHGTX);  
}

static u8 isEnabled = 1;
void EnableBAT(void)
{  
  GPIO_SetBits(GPIOD, GPIO_Pin_2);    // Disable sink (to prevent blowing up the fixture)
  GPIO_ResetBits(GPIOC, GPIO_Pin_2);
  isEnabled = 1;
}

void DisableBAT(void)
{
  if (isEnabled)
  {
    GPIO_SetBits(GPIOC, GPIO_Pin_2);
    MicroWait(1);
    GPIO_ResetBits(GPIOD, GPIO_Pin_2);  // Enable sink to quickly discharge any remaining power
    GPIO_ResetBits(GPIOC, GPIO_Pin_1);  // Sink even more current (down to 0.3V at least)
    MicroWait(50000);
    GPIO_SetBits(GPIOD, GPIO_Pin_2);    // Disable sink (to prevent blowing up the fixture)  
    GPIO_SetBits(GPIOC, GPIO_Pin_1);
  }
  isEnabled = 0;
}


#if 0
extern u8 g_fixbootbin[], g_fixbootend[];

// Check if bootloader is outdated and update it with the latest
// This is stupidly risky and could easily brick a board - but it replaces an old bootloader that bricks boards
// Fight bricking with bricking!
void UpdateBootLoader(void)
{
  // Save the serial number, in case we have to restore it
  u32 serial = FIXTURE_SERIAL;
  
  // Spend a little while checking - no point in giving up on our first try, since the board will die if we do
  for (int i = 0; i < 100; i++)
  {
    // Byte by byte comparison
    bool matches = true;
    for (int j = 0; j < (g_fixbootend - g_fixbootbin); j++)
      if (g_fixbootbin[j] != ((u8*)FLASH_BOOTLOADER)[j])
        matches = false;
      
    // Bail out if all is good
    if (matches)
      return;
    
    SlowPutString("Mismatch!\r\n");
    
    // If not so good, check a few more times, leaving time for voltage to stabilize
    if (i > 50)
    {
      SlowPutString("Flashing...");
      
      // Gulp.. it's bricking time
      FLASH_Unlock();
      
      // Erase and reflash the boot code
      FLASH_EraseSector(FLASH_BLOCK_BOOT, VoltageRange_1);
      for (int j = 0; j < (g_fixbootend - g_fixbootbin); j++)
        FLASH_ProgramByte(FLASH_BOOTLOADER + j, g_fixbootbin[j]);
      
      // Recover the serial number
      FLASH_ProgramByte(FLASH_BOOTLOADER_SERIAL, serial & 255);
      FLASH_ProgramByte(FLASH_BOOTLOADER_SERIAL+1, serial >> 8);
      FLASH_ProgramByte(FLASH_BOOTLOADER_SERIAL+2, serial >> 16);
      FLASH_ProgramByte(FLASH_BOOTLOADER_SERIAL+3, serial >> 24);
      FLASH_Lock();
      
      SlowPutString("Done!\r\n");
    }
  }
  
  // If we get here, we are DEAD!
}
#endif


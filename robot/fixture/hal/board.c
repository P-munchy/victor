// Based on Drive Testfix, updated for Cozmo EP1 Testfix
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

#define PINC_VEXTEN 12  // Also TX, so don't use this on head fixtures!
    
void InitBAT(void)
{
  GPIO_InitTypeDef  GPIO_InitStructure;
  GPIO_SetBits(GPIOA, GPIO_Pin_9);
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_DOWN;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
  GPIO_Init(GPIOA, &GPIO_InitStructure);
  
  MicroWait(400);
  
  // PINC_VEXTEN - default low (VEXT disabled)
  GPIO_RESET(GPIOC, PINC_VEXTEN);
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
  GPIO_InitStructure.GPIO_Pin = PINC_VEXTEN;
  GPIO_Init(GPIOC, &GPIO_InitStructure);  
  
  // ENBAT_LC, ENBAT, NBATSINK
  GPIO_InitStructure.GPIO_OType = GPIO_OType_OD;
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3;
  GPIO_Init(GPIOC, &GPIO_InitStructure);
  
  DisableBAT();
}

void EnableVEXT(void)
{
  GPIO_SET(GPIOC, PINC_VEXTEN);
  PIN_OUT(GPIOC, PINC_VEXTEN);
}

void DisableVEXT(void)
{
  GPIO_RESET(GPIOC, PINC_VEXTEN);
  PIN_OUT(GPIOC, PINC_VEXTEN);  
}

void EnableBAT(void)
{  
  GPIO_SetBits(GPIOC, GPIO_Pin_3);    // Disable sink (to prevent blowing up the fixture)
  GPIO_ResetBits(GPIOC, GPIO_Pin_2);
}

void DisableBAT(void)
{
  GPIO_SetBits(GPIOC, GPIO_Pin_2);
  MicroWait(1);
  GPIO_ResetBits(GPIOC, GPIO_Pin_3);  // Enable sink to quickly discharge any remaining power
  GPIO_ResetBits(GPIOC, GPIO_Pin_1);  // Sink even more current (down to 0.3V at least)
  MicroWait(50000);
  GPIO_SetBits(GPIOC, GPIO_Pin_3);    // Disable sink (to prevent blowing up the fixture)  
  GPIO_SetBits(GPIOC, GPIO_Pin_1);
}

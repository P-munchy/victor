// Based on Drive Testfix, updated for Cozmo EP1 Testfix
#ifndef __BOARD_H
#define __BOARD_H

#include "stm32f2xx.h"

// Charge pins moved in rev 1
#ifdef REV1
#define PINC_CHGTX           6
#define PINC_CHGRX           7
#define PINB_SCL               8
#define PINB_SDA               9
#else
#define PINC_CHGTX           11
#define PINC_CHGRX           10
#define PINB_SCL               9   // XXX: Pre-rev1 is backward for digital pot
#define PINB_SDA               8
#endif

#define GPIOC_CHGTX          (1 << PINC_CHGTX)
#define GPIOC_CHGRX          (1 << PINC_CHGRX)
#define GPIOB_SCL         (1 << PINB_SCL)
#define GPIOB_SDA         (1 << PINB_SDA)

#define PINB_VDD   0
#define PINC_RESET 5

#define PINC_TRX 12
#define GPIOC_TRX (1 << PINC_TRX)

#define PINA_ENCHG 15
#define GPIOA_ENCHG (1 << PINA_ENCHG)

#define PINB_SWD  10
#define GPIOB_SWD (1 << PINB_SWD)
#define PINB_SWC  11
#define GPIOB_SWC (1 << PINB_SWC)

#define PINA_NRF_SWD  11
#define GPIOA_NRF_SWD (1 << PINA_NRF_SWD)
#define PINA_NRF_SWC  12
#define GPIOA_NRF_SWC (1 << PINA_NRF_SWC)

#define PINB_DEBUGTX 6
#define GPIOB_DEBUGTX (1 << PINB_DEBUGTX)

// Backpack LEDs/ADC channels
#define PINA_BPLED0 2
#define PINA_BPLED1 3
#define PINA_BPLED2 6
#define PINA_BPLED3 7

typedef enum 
{
  LEDRED = 0,
  LEDGREEN = 1
} Led_TypeDef;


#define LED1_PIN                         GPIO_Pin_8
#define LED1_GPIO_PORT                   GPIOC
#define LED1_GPIO_CLK                    RCC_AHB1Periph_GPIOC
  
#define LED2_PIN                         GPIO_Pin_9
#define LED2_GPIO_PORT                   GPIOC
#define LED2_GPIO_CLK                    RCC_AHB1Periph_GPIOC

#define LEDn                             2

void STM_EVAL_LEDInit(Led_TypeDef Led);
void STM_EVAL_LEDOn(Led_TypeDef Led);
void STM_EVAL_LEDOff(Led_TypeDef Led);
void STM_EVAL_LEDToggle(Led_TypeDef Led);

int InitBoard(void);
void EnableBAT(void);
void DisableBAT(void);
void EnableVEXT(void);
void DisableVEXT(void);

#endif 

#include "common.h"
#include "hardware.h"

#include "power.h"

#include "contacts.h"

extern "C" void SoftReset(const uint32_t reset);

static const uint32_t APB1_CLOCKS = 0
              | RCC_APB1ENR_USART2EN
              | RCC_APB1ENR_TIM3EN
              | RCC_APB1ENR_TIM6EN
              | RCC_APB1ENR_TIM14EN
              | RCC_APB1ENR_I2C2EN
              | RCC_APB1ENR_SPI2EN
              ;

static const uint32_t APB2_CLOCKS = 0
              | RCC_APB2ENR_USART1EN
              | RCC_APB2ENR_TIM1EN
              | RCC_APB2ENR_TIM15EN
              | RCC_APB2ENR_TIM16EN
              | RCC_APB2ENR_TIM17EN
              | RCC_APB2ENR_SPI1EN
              | RCC_APB2ENR_SYSCFGEN
              | RCC_APB2ENR_ADC1EN
              ;

static volatile bool ejectSystem = false;

void Power::init(void) {
  POWER_EN::pull(PULL_UP);

  nCHG_EN::set();
  nCHG_EN::mode(MODE_OUTPUT);

  nVDDs_EN::reset();
  nVDDs_EN::mode(MODE_OUTPUT);
}

void Power::setCharge(bool enable) {
  if (enable) {
    nCHG_EN::reset();
  } else {
    nCHG_EN::set();
  }
}

void Power::stop(void) {
  nVDDs_EN::set();
  nCHG_EN::set();
  POWER_EN::pull(PULL_DOWN);
}

void Power::enableClocking(void) {
  RCC->APB1ENR |= APB1_CLOCKS;
  RCC->APB2ENR |= APB2_CLOCKS;
}

void Power::softReset(void) {
  ejectSystem = true;
}

void Power::eject(void) {
  if (!ejectSystem) return ;

  __disable_irq();
  NVIC->ICER[0]  = ~0;  // Disable all interrupts

  // Power down accessessories
  nVDDs_EN::set();

  // Disable our DMA channels
  DMA1_Channel1->CCR = 0;
  DMA1_Channel2->CCR = 0;
  DMA1_Channel3->CCR = 0;
  DMA1_Channel4->CCR = 0;
  DMA1_Channel5->CCR = 0;

  // Reset all the perfs (including ones we are not using)
  RCC->APB1RSTR = 0
    | RCC_APB1RSTR_TIM2RST
    | RCC_APB1RSTR_TIM3RST
    | RCC_APB1RSTR_TIM6RST
    | RCC_APB1RSTR_TIM7RST
    | RCC_APB1RSTR_TIM14RST
    | RCC_APB1RSTR_SPI2RST
    | RCC_APB1RSTR_USART2RST
    | RCC_APB1RSTR_USART3RST
    | RCC_APB1RSTR_USART4RST
    | RCC_APB1RSTR_USART5RST
    | RCC_APB1RSTR_I2C1RST
    | RCC_APB1RSTR_I2C2RST
    | RCC_APB1RSTR_USBRST
    | RCC_APB1RSTR_CANRST
    | RCC_APB1RSTR_CRSRST
    | RCC_APB1RSTR_PWRRST
    | RCC_APB1RSTR_DACRST
    | RCC_APB1RSTR_CECRST
    ;

  RCC->APB2RSTR = 0
    | RCC_APB2RSTR_SYSCFGRST
    | RCC_APB2RSTR_ADCRST
    | RCC_APB2RSTR_USART8RST
    | RCC_APB2RSTR_USART7RST
    | RCC_APB2RSTR_USART6RST
    | RCC_APB2RSTR_TIM1RST
    | RCC_APB2RSTR_SPI1RST
    | RCC_APB2RSTR_USART1RST
    | RCC_APB2RSTR_TIM15RST
    | RCC_APB2RSTR_TIM16RST
    | RCC_APB2RSTR_TIM17RST
    | RCC_APB2RSTR_DBGMCURST
    | RCC_APB2RSTR_ADC1RST
    ;

  __asm("nop\nnop\nnop\nnop\nnop");

  RCC->APB1RSTR = 0;
  RCC->APB2RSTR = 0;

  // Disable clocking to everything but the GPIO
  RCC->APB1ENR &= ~APB1_CLOCKS;
  RCC->APB2ENR &= ~APB2_CLOCKS;

  // Set to flash handler
  SYSCFG->CFGR1 = 0;

  // Pass control back to the reset handler
  SoftReset(*(uint32_t*)0x08000004);
}

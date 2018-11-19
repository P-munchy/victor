#include <string.h>

#include "common.h"
#include "hardware.h"

#include "motors.h"
#include "encoders.h"
#include "timer.h"

static const int8_t QUAD_DECODE[4][4] = {
  {  0,  1, -1,  0 },
  { -1,  0,  0,  1 },
  {  1,  0,  0, -1 },
  {  0, -1,  1,  0 }
};

static int page;
static uint32_t time[2][MOTOR_COUNT];
static int32_t delta[2][MOTOR_COUNT];

static bool active = false;
int Encoders::stale_count = 0;
bool Encoders::disabled = false;
bool Encoders::head_invalid = false;
bool Encoders::lift_invalid = false;

void Encoders::init(void) {
  static const uint32_t EVENT_MASK =
    LENCA::mask | LENCB::mask |
    HENCA::mask | HENCB::mask |
    RTENC::mask | LTENC::mask;

  // Enable power to encoder LEDs
  nVENC_EN::mode(MODE_OUTPUT);

  // Setup gpio
  LENCA::mode(MODE_INPUT);
  LENCB::mode(MODE_INPUT);
  HENCA::mode(MODE_INPUT);
  HENCB::mode(MODE_INPUT);
  RTENC::mode(MODE_INPUT);
  LTENC::mode(MODE_INPUT);

  // When this pin changes, do some shizz
  SYSCFG->EXTICR[0] =
    SYSCFG_EXTICR1_EXTI0_PA |
    SYSCFG_EXTICR1_EXTI1_PA |
    SYSCFG_EXTICR1_EXTI2_PB |
    SYSCFG_EXTICR1_EXTI3_PB;

  SYSCFG->EXTICR[3] =
    SYSCFG_EXTICR4_EXTI14_PC |
    SYSCFG_EXTICR4_EXTI15_PC;

  EXTI->FTSR |= EVENT_MASK;
  EXTI->RTSR |= EVENT_MASK;
  EXTI->IMR  |= EVENT_MASK;

  NVIC_SetPriority(EXTI0_1_IRQn, PRIORITY_ENCODERS);
  NVIC_SetPriority(EXTI2_3_IRQn, PRIORITY_ENCODERS);
  NVIC_SetPriority(EXTI4_15_IRQn, PRIORITY_ENCODERS);
}

void Encoders::start() {
  nVENC_EN::reset();

  NVIC_EnableIRQ(EXTI0_1_IRQn);
  NVIC_EnableIRQ(EXTI2_3_IRQn);
  NVIC_EnableIRQ(EXTI4_15_IRQn);
  
  active = true;
}

void Encoders::stop() {
  NVIC_DisableIRQ(EXTI0_1_IRQn);
  NVIC_DisableIRQ(EXTI2_3_IRQn);
  NVIC_DisableIRQ(EXTI4_15_IRQn);

  nVENC_EN::set();

  active = false;
}

void Encoders::tick_start() {
  if (!active) return ;
  
  nVENC_EN::reset();
}

void Encoders::tick_end() {
  if (!active) return ;

  if (Motors::lift_driven) {
    lift_invalid = false;
    stale_count = 0;
  }
  if (Motors::head_driven) {
    head_invalid = false;
    stale_count = 0;
  }
  if (Motors::treads_driven) {
    stale_count = 0;
  }

  static const int STALE_TARGET = 40;
  
  static uint32_t gpio_last = ~0;
  uint32_t gpio_now = (HENCA::bank->IDR & (HENCB::mask | HENCA::mask))
                    | (LENCA::bank->IDR & (LENCB::mask | LENCA::mask))
                    | (RTENC::bank->IDR & (RTENC::mask | LTENC::mask));

  if (stale_count < STALE_TARGET) {
    if (++stale_count == STALE_TARGET) {
      NVIC_DisableIRQ(EXTI0_1_IRQn);
      NVIC_DisableIRQ(EXTI2_3_IRQn);
      NVIC_DisableIRQ(EXTI4_15_IRQn);
      disabled = true;
    } else {
      NVIC_EnableIRQ(EXTI0_1_IRQn);
      NVIC_EnableIRQ(EXTI2_3_IRQn);
      NVIC_EnableIRQ(EXTI4_15_IRQn);
      disabled = false;
    }
  } else if (gpio_last != gpio_now) {
    uint32_t change = gpio_now ^ gpio_last;

    if (change & (HENCB::mask | HENCA::mask)) {
      head_invalid = true;
    }

    if (change & (LENCB::mask | LENCA::mask)) {
      lift_invalid = true;
    }

    NVIC_EnableIRQ(EXTI0_1_IRQn);
    NVIC_EnableIRQ(EXTI2_3_IRQn);
    NVIC_EnableIRQ(EXTI4_15_IRQn);
    stale_count = 0;
    disabled = false;
  } else {
    nVENC_EN::set();
  }
  
  gpio_last = gpio_now;
}

void Encoders::flip(uint32_t* &time_last, int32_t* &delta_last) {
  const int next_page = page ^ 1;

  time_last = time[page];
  delta_last = delta[page];
  memset(&time[next_page], 0, sizeof(time[0]));
  memset(&delta[next_page], 0, sizeof(delta[0]));
  page = next_page;
}

// Head encoder
extern "C" void EXTI0_1_IRQHandler(void) {
  static uint32_t prev;
  const uint32_t now = (HENCA::bank->IDR >> HENCA::pin) & 0x3;

  time[page][MOTOR_HEAD] = Timer::getTime();
  delta[page][MOTOR_HEAD] += QUAD_DECODE[prev][now];
  prev = now;

  // Clear our interrupt
  EXTI->PR = HENCA::mask | HENCB::mask;

  Encoders::stale_count = 0;
}

// Lift encoder
extern "C" void EXTI2_3_IRQHandler(void) {
  static uint32_t prev;
  const uint32_t now = (LENCA::bank->IDR >> LENCA::pin) & 0x3;

  time[page][MOTOR_LIFT] = Timer::getTime();
  delta[page][MOTOR_LIFT] += QUAD_DECODE[prev][now];
  prev = now;

  // Clear our interrupt
  EXTI->PR = LENCA::mask | LENCB::mask;

  Encoders::stale_count = 0;
}

// Tread encoders
extern "C" void EXTI4_15_IRQHandler(void) {
  const uint32_t now = Timer::getTime();
  const uint32_t pins = RTENC::bank->IDR & (RTENC::mask | LTENC::mask);

  if (EXTI->PR & RTENC::mask) {
    delta[page][MOTOR_RIGHT]++;
    time[page][MOTOR_RIGHT] = now;
  }

  if (EXTI->PR & LTENC::mask) {
    delta[page][MOTOR_LEFT]++;
    time[page][MOTOR_LEFT] = now;
  }

  // Clear our interrupt
  EXTI->PR = RTENC::mask | LTENC::mask;

  Encoders::stale_count = 0;
}

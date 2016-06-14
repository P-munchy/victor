#include "battery.h"
#include "nrf.h"
#include "nrf_gpio.h"
#include "timer.h"
#include "anki/cozmo/robot/spineData.h"
#include "messages.h"
#include "clad/robotInterface/messageEngineToRobot.h"
#include "clad/robotInterface/messageEngineToRobot_send_helper.h"

#include "hardware.h"
#include "rtos.h"

static const int MaxContactTime = 90000; // (30min) 20ms per count
static const int MinContactTime = 100;   //

// Updated to 3.0
const u32 V_REFERNCE_MV = 1200; // 1.2V Bandgap reference
const u32 V_PRESCALE    = 3;
const u32 V_SCALE       = 0x3ff; // 10 bit ADC

const Fixed VEXT_SCALE  = TO_FIXED(2.0); // Cozmo 4.1 voltage divider
const Fixed VBAT_SCALE  = TO_FIXED(4.0); // Cozmo 4.1 voltage divider

const Fixed VBAT_CHGD_HI_THRESHOLD = TO_FIXED(4.05); // V
const Fixed VBAT_CHGD_LO_THRESHOLD = TO_FIXED(3.30); // V

const Fixed VEXT_DETECT_THRESHOLD  = TO_FIXED(4.40); // V

static int ContactTime = 0;

// Are we currently on charge contacts?
bool Battery::onContacts = false;

static Fixed vBat, vExt;
static bool isCharging;

// Which pin is currently being used in the ADC mux
AnalogInput m_pinIndex;

extern GlobalDataToHead g_dataToHead;

static inline void startADCsample(AnalogInput channel)
{
  m_pinIndex = channel; // This is super cheap
  
  NRF_ADC->CONFIG &= ~ADC_CONFIG_PSEL_Msk; // Clear any existing mux
  NRF_ADC->CONFIG |= channel << ADC_CONFIG_PSEL_Pos; // Activate analog input above
  NRF_ADC->EVENTS_END = 0;
  NRF_ADC->TASKS_START = 1;
}

static inline Fixed calcResult(const Fixed scale)
{
  return FIXED_MUL(FIXED_DIV(TO_FIXED(NRF_ADC->RESULT * V_REFERNCE_MV * V_PRESCALE / V_SCALE), TO_FIXED(1000)), scale);
}

static inline Fixed getADCsample(AnalogInput channel, const Fixed scale)
{
  startADCsample(channel);
  while (!NRF_ADC->EVENTS_END) ; // Wait for the conversion to finish
  NRF_ADC->TASKS_STOP = 1;
  return calcResult(scale);
}

uint8_t Battery::getLevel(void) {
  return (vBat - VBAT_CHGD_LO_THRESHOLD) * 100 / (VBAT_CHGD_HI_THRESHOLD - VBAT_CHGD_LO_THRESHOLD);
}

static void SendPowerStateUpdate(void *userdata)
{
  using namespace Anki::Cozmo;
  
  PowerState msg;
  msg.VBatFixed = vBat;
  msg.VExtFixed = vExt;
  msg.batteryLevel = Battery::getLevel();
  msg.onCharger  = ContactTime > MinContactTime;
  msg.isCharging = isCharging;
  RobotInterface::SendMessage(msg);
}

void Battery::init()
{
  // Configure charge pins
  nrf_gpio_pin_clear(PIN_CHARGE_EN);
  nrf_gpio_cfg_output(PIN_CHARGE_EN);
 
  // Configure cliff sensor pins
  nrf_gpio_cfg_output(PIN_IR_DROP);
  
  // Syscon power - this should always be on until battery fail
  nrf_gpio_pin_set(PIN_PWR_EN);
  nrf_gpio_cfg_output(PIN_PWR_EN);
  
  // Encoder and LED power
  nrf_gpio_pin_clear(PIN_VDDs_EN);
  nrf_gpio_cfg_output(PIN_VDDs_EN);

  // turn off headlight
  nrf_gpio_pin_clear(PIN_IR_FORWARD);
  nrf_gpio_cfg_output(PIN_IR_FORWARD);

  // Configure the analog sense pins
  nrf_gpio_cfg_input(PIN_V_BAT_SENSE, NRF_GPIO_PIN_NOPULL);
  nrf_gpio_cfg_input(PIN_V_EXT_SENSE, NRF_GPIO_PIN_PULLUP);
  nrf_gpio_cfg_input(PIN_CLIFF_SENSE, NRF_GPIO_PIN_NOPULL);

  // Just in case we need to power on the peripheral ourselves
  NRF_ADC->POWER = 1;

  NRF_ADC->CONFIG =
    (ADC_CONFIG_RES_10bit << ADC_CONFIG_RES_Pos) | // 10 bit resolution
    (ADC_CONFIG_INPSEL_AnalogInputOneThirdPrescaling << ADC_CONFIG_INPSEL_Pos) | // External inputs with 1/3rd analog prescaling
    (ADC_CONFIG_REFSEL_VBG << ADC_CONFIG_REFSEL_Pos) | // 1.2V Bandgap reference
    (ADC_CONFIG_EXTREFSEL_None << ADC_CONFIG_EXTREFSEL_None); // Disable external analog reference pins

  NRF_ADC->ENABLE = ADC_ENABLE_ENABLE_Enabled;

  vBat = getADCsample(ANALOG_V_BAT_SENSE, VBAT_SCALE); // Battery voltage
  vExt = getADCsample(ANALOG_V_EXT_SENSE, VEXT_SCALE); // External voltage
  int temp = getADCsample(ANALOG_CLIFF_SENSE, VEXT_SCALE);

  startADCsample(ANALOG_CLIFF_SENSE);

  RTOS::schedule(Battery::manage);
  RTOS::schedule(SendPowerStateUpdate, CYCLES_MS(60.0f));
}

void Battery::setHeadlight(bool status) {
  if (status) {
    nrf_gpio_pin_set(PIN_IR_FORWARD);
  } else {
    nrf_gpio_pin_clear(PIN_IR_FORWARD);
  }
}

void Battery::powerOn()
{
  nrf_gpio_pin_set(PIN_PWR_EN);
}

void Battery::powerOff()
{
  // Shutdown the extra things
  nrf_gpio_pin_clear(PIN_PWR_EN);
  MicroWait(10000);
}

static inline void sampleCliffSensor() {
  static bool ledOn = false;
  static int resultLedOn;
  static int resultLedOff;
  
  if (ledOn) {
    resultLedOn = NRF_ADC->RESULT;
    nrf_gpio_pin_clear(PIN_IR_DROP);

    g_dataToHead.cliffLevel = resultLedOn - resultLedOff;
    
    startADCsample(ANALOG_V_BAT_SENSE);
  } else {
    resultLedOff = NRF_ADC->RESULT;
    nrf_gpio_pin_set(PIN_IR_DROP);
    startADCsample(ANALOG_CLIFF_SENSE);
  }
  
  ledOn = !ledOn;
}

void Battery::manage(void* userdata)
{
  if (!NRF_ADC->EVENTS_END) {
    return ;
  }

  switch (m_pinIndex)
  {
    case ANALOG_V_BAT_SENSE:
      vBat = calcResult(VBAT_SCALE);
      startADCsample(ANALOG_V_EXT_SENSE);

      // after 1 minute of low battery, turn off
      static const int LOW_BAT_TIME = 1000 / 20; // 1 minute
      static int lowBatTimer = 0;
      if (vBat < VBAT_CHGD_LO_THRESHOLD) {
        if (++lowBatTimer >= LOW_BAT_TIME) {
          powerOff();
          NVIC_SystemReset();
        }
      } else {
        lowBatTimer = 0;
      }
    
      break ;

    case ANALOG_V_EXT_SENSE:
      {
        // Are our power pins shorted?
        static int ground_short = 0;
        if (NRF_ADC->RESULT < 0x30) {
          if (++ground_short > 30) {
            Battery::powerOff();
            NVIC_SystemReset();
          }
        } else {
          ground_short = 0;
        }

        vExt = calcResult(VEXT_SCALE);
        onContacts = vExt > VEXT_DETECT_THRESHOLD;
        startADCsample(ANALOG_CLIFF_SENSE);
        
        if (!onContacts) {
          ContactTime = 0;
        } else {
          ContactTime++;
        }

        if (ContactTime < MaxContactTime && ContactTime > MinContactTime) {
          nrf_gpio_pin_set(PIN_CHARGE_EN);
          isCharging = true;
        } else {
          nrf_gpio_pin_clear(PIN_CHARGE_EN);
          isCharging = false;
        }
      }
      break ;
    case ANALOG_CLIFF_SENSE:
      sampleCliffSensor();
      break ;
  }
}

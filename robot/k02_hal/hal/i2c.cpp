// Placeholder bit-banging I2C implementation
// Vandiver:  Replace me with a nice DMA version that runs 4 transactions at a time off HALExec
// For HAL use only - see i2c.h for instructions, imu.cpp and camera.cpp for examples 
#include "anki/cozmo/robot/hal.h"
#include "hal/portable.h"
#include "hal/i2c.h"
#include "MK02F12810.h"
#include "hal/hardware.h"

#define I2C_C1_COMMON (I2C_C1_IICEN_MASK | I2C_C1_IICIE_MASK)

// Internal Settings
enum I2C_Control {
  // These are top level modes
  I2C_CTRL_STOP = I2C_C1_COMMON,
  I2C_CTRL_READ = I2C_C1_COMMON | I2C_C1_MST_MASK,  
  I2C_CTRL_SEND = I2C_C1_COMMON | I2C_C1_MST_MASK | I2C_C1_TX_MASK,
    
  // These are modifiers
  I2C_CTRL_NACK = I2C_C1_TXAK_MASK,
  I2C_CTRL_RST  = I2C_C1_RSTA_MASK
};

static const uint8_t UNUSED_SLAVE = 0xFF;
static const int MAX_QUEUE = 128; // 256 bytes worth of i2c buffer (excessive)

static uint8_t _active_slave = UNUSED_SLAVE;

extern "C" void (*I2C0_Proc)(void);

// Queue registers
static uint16_t i2c_queue[MAX_QUEUE];
static volatile int _fifo_count;
static volatile int _fifo_start;
static volatile int _fifo_end;

// Random state values
static volatile bool _active = false;
static bool _enabled = false;
static bool _send_reset = false;

// Read buffer
static volatile void* _read_target = NULL;
static uint8_t* _read_buffer = NULL;
static volatile int _read_size = 0;
static volatile int _read_count = 0;
static volatile i2c_callback _read_callback = NULL;

static void Write_Handler(void);
static void Read_Handler(void);

static inline void write_queue(uint8_t mode, uint8_t data = 0) {
  i2c_queue[_fifo_start++] = (mode << 8) | data;
  if (_fifo_start >= MAX_QUEUE) { _fifo_start = 0; }
  _fifo_count++;
}

static inline void read_queue(uint8_t& mode, uint8_t& data) {
  uint16_t temp = i2c_queue[_fifo_end++];
  if (_fifo_end >= MAX_QUEUE) { _fifo_end = 0; }
  _fifo_count--;
  
  mode = temp >> 8;
  data = temp;
}

// Send a stop condition first thing to make sure perfs are not holding the bus
static inline void SendEmergencyStop(void) {
  using namespace Anki::Cozmo::HAL;

  GPIO_SET(GPIO_I2C_SCL, PIN_I2C_SCL);

  // Drive PWDN and RESET to safe defaults
  GPIO_OUT(GPIO_I2C_SCL, PIN_I2C_SCL);
  SOURCE_SETUP(GPIO_I2C_SCL, SOURCE_I2C_SCL, SourceGPIO);

  // GPIO, High drive, open drain
  GPIO_IN(GPIO_I2C_SDA, PIN_I2C_SDA);
  SOURCE_SETUP(GPIO_I2C_SDA, PIN_I2C_SDA, SourceGPIO);
  
  // Clock the output 100 times
  for (int i = 0; i < 100; i++) {
    GPIO_RESET(GPIO_I2C_SCL, PIN_I2C_SCL);
    MicroWait(1);
    GPIO_SET(GPIO_I2C_SCL, PIN_I2C_SCL);
    MicroWait(1);
  }
}

void Anki::Cozmo::HAL::I2C::Init()
{
  SendEmergencyStop();
  
  // Clear our FIFO
  _fifo_count = 0;
  _fifo_start = 0;
  _fifo_end = 0;

  I2C0_Proc = &Write_Handler;
  _active = false;
 
  // Enable clocking on I2C, PortB, PortE, and DMA
  SIM_SCGC4 |= SIM_SCGC4_I2C0_MASK;
  SIM_SCGC5 |= SIM_SCGC5_PORTA_MASK | SIM_SCGC5_PORTB_MASK | SIM_SCGC5_PORTE_MASK;

  // Configure port mux for i2c
  PORTB_PCR1 = PORT_PCR_MUX(2) |
               PORT_PCR_ODE_MASK |
               PORT_PCR_DSE_MASK |
               PORT_PCR_PE_MASK |
               PORT_PCR_PS_MASK;   //I2C0_SDA
  PORTE_PCR24 = PORT_PCR_MUX(5) |
                PORT_PCR_DSE_MASK;  //I2C0_SCL

  // Configure i2c
  I2C0_F  = I2C_F_ICR(0x1A);
  I2C0_C1 = I2C_C1_COMMON;
  
  // Enable IRQs
  NVIC_SetPriority(I2C0_IRQn, 0);
  Enable();
}

void Anki::Cozmo::HAL::I2C::Enable(void) {
  if (_enabled) return ;

  _enabled = true;
  NVIC_EnableIRQ(I2C0_IRQn);
  
  if (!_active && _fifo_count > 0) {
    _active = true;
    I2C0_Proc();
  }
}

void Anki::Cozmo::HAL::I2C::Disable(void) {
  _enabled = false;
  NVIC_DisableIRQ(I2C0_IRQn);
}

// Register calls
void Anki::Cozmo::HAL::I2C::WriteReg(uint8_t slave, uint8_t addr, uint8_t data) {
  uint8_t cmd[2] = { addr, data };

  Write(SLAVE_WRITE(slave), cmd, sizeof(cmd), I2C_FORCE_START);
}

uint8_t Anki::Cozmo::HAL::I2C::ReadReg(uint8_t slave, uint8_t addr) {
  uint8_t resp;

  SetupRead(&resp, sizeof(resp));

  Write(SLAVE_WRITE(slave), &addr, sizeof(addr), I2C_FORCE_START);
  Read(SLAVE_READ(slave));
  
  Flush();
  
  return resp;
}

void Anki::Cozmo::HAL::I2C::Flush(void) {
  while (_active || _fifo_count > 0) {
    Enable();
    __asm { WFI }
  }
}

void Anki::Cozmo::HAL::I2C::ForceStop(void) {
  _active_slave = UNUSED_SLAVE;
  _send_reset = false;
  write_queue(I2C_CTRL_STOP);
}

// This is a carpet bomb stop on the i2c bus that should not be used inside IRQs
void Anki::Cozmo::HAL::I2C::FullStop(void) {
  _active_slave = UNUSED_SLAVE;
  _send_reset = false;

  Flush();
  I2C0_C1 &= ~I2C_C1_MST_MASK;
  MicroWait(1);
  Enable();
}

void Anki::Cozmo::HAL::I2C::SetupRead(void* target, int size, i2c_callback cb) {
  _read_target = target;
  _read_size = size;
  _read_callback = cb;
}

static void Enqueue(uint8_t slave, const uint8_t *bytes, int len, uint8_t flags) {
  using namespace Anki::Cozmo::HAL::I2C;
  
  bool ena = _enabled;
  Disable();
    
  if (slave != _active_slave || flags & I2C_FORCE_START) {
    // Select the device
    _active_slave = slave;
    
    if (_send_reset) {
      write_queue(I2C_CTRL_RST | I2C_CTRL_SEND, slave);
    } else {
      write_queue(I2C_CTRL_SEND, slave);
    }
  } else if (flags & I2C_OPTIONAL) {
    return ;
  }
  
  while (len-- > 0) {
    write_queue(I2C_CTRL_SEND, *(bytes++));
  }

  if (ena) {
    Enable();

    if (!_active) {
      _active = true;
      I2C0_Proc();
    }
  }

  _send_reset = true;
}

void Anki::Cozmo::HAL::I2C::Write(uint8_t slave, const uint8_t *bytes, int len, uint8_t flags) {
  Enqueue(slave, bytes, len, flags);
}

void Anki::Cozmo::HAL::I2C::Read(uint8_t slave, uint8_t flags) {
  Enqueue(slave, NULL, 0, flags);
  
  // Send a nack on first transmission if size is 1
  if (_read_size == 1) {
    write_queue(I2C_CTRL_READ | I2C_CTRL_NACK);
  } else {
    write_queue(I2C_CTRL_READ);
  }
}

static void Read_Handler(void) {
  using namespace Anki::Cozmo::HAL;

  I2C0_S |= I2C_S_IICIF_MASK;
  
  bool complete = false;

  _read_count--;

  // Check how many bytes are left
  if (_read_count == 1) {
    // Send nack on last byte
    I2C0_C1 |= I2C_C1_TXAK_MASK;
  } if (_read_count <= 0) {
    I2C0_C1 |= I2C_C1_TX_MASK;
    I2C0_Proc = &Write_Handler;
    complete = true;
  } 

  *(_read_buffer++) = I2C0_D;

  if (complete) {
    if (_read_callback) {
      _read_callback();
    }
    Write_Handler();
  }
}

__attribute__((section("CODERAM")))
static void Write_Handler(void) {
  using namespace Anki::Cozmo::HAL;
  
  I2C0_S |= I2C_S_IICIF_MASK;

  if (!_fifo_count) {
    _active = false;
    return ;
  }

  uint8_t data;
  uint8_t mode;
  read_queue(mode, data);

  if (I2C0_C1 != mode) {
    I2C0_C1 = mode;
  }
  
  switch (mode) {
    default:
      I2C0_D = data;
      return ;

    case I2C_CTRL_READ | I2C_CTRL_NACK:
    case I2C_CTRL_READ:
      {
        _read_count = _read_size;
        _read_buffer = (uint8_t*) _read_target;
        I2C0_Proc = &Read_Handler;
      
        int ignore = I2C0_D;
      }
      return ;

    case I2C_CTRL_STOP:
      _active = false;
      break ;
  }
}

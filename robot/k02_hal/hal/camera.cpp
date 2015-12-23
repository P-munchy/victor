#include "anki/cozmo/robot/hal.h"
#include "MK02F12810.h"

#include "hal/portable.h"

#include "hal/i2c.h"
#include "hal/hardware.h"
#include "hal/spi.h"
#include "hal/uart.h"

//#define ENABLE_JPEG     // Comment this out to troubleshoot timing problems caused by JPEG encoder
//#define SERIAL_IMAGE    // Uncomment this to dump camera data over UART for camera debugging with SerialImageViewer
//#define ASCII_IMAGE     // Uncomment this to send an ASCII image for test purposes

namespace Anki
{
  namespace Cozmo
  {
    namespace HAL
    {

      // Configuration for GC0329 camera chip
      const u8 I2C_ADDR = 0x31;
      const u8 CAM_SCRIPT[] =
      {
        #include "gc0329.h"
        0, 0
      };
      
      const int TOTAL_COLS = 640, TOTAL_ROWS = 480, SWIZZLE_ROWS = 8, BYTES_PER_PIX = 2;
      CAMRAM static u8 dmaBuff_[TOTAL_COLS * BYTES_PER_PIX * 2];
      CAMRAM static u8 swizzle_[TOTAL_COLS * SWIZZLE_ROWS];

      // Camera exposure value
      u32 exposure_;
      volatile bool timingSynced_ = false;

      // For self-test purposes only.
      // XXX - restore this code for EP1
#if 0
      static void CamSetPulls(GPIO_TypeDef* GPIOx, u32 pin, int index)
      {
        GPIO_InitTypeDef GPIO_InitStructure;
        GPIO_InitStructure.GPIO_PuPd = index ? GPIO_PuPd_UP : GPIO_PuPd_DOWN;
        GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
        GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
        GPIO_InitStructure.GPIO_Pin =  pin;
        GPIO_Init(GPIOx, &GPIO_InitStructure);
      }
      static int CamGetPulls(GPIO_TypeDef* GPIOx, u32 pin, int index)
      {
        return (!!(GPIO_READ(GPIOx) & pin)) << index;
      }
      static int CamReadDB(int pullup)
      {
        CamSetPulls(GPIO_D0, PIN_D0, pullup & 1);
        CamSetPulls(GPIO_D1, PIN_D1, pullup & 2);
        CamSetPulls(GPIO_D2, PIN_D2, pullup & 4);
        CamSetPulls(GPIO_D3, PIN_D3, pullup & 8);
        CamSetPulls(GPIO_D4, PIN_D4, pullup & 16);
        CamSetPulls(GPIO_D5, PIN_D5, pullup & 32);
        CamSetPulls(GPIO_D6, PIN_D6, pullup & 64);
        CamSetPulls(GPIO_D7, PIN_D7, pullup & 128);
        
        MicroWait(25);
        
        return
          CamGetPulls(GPIO_D0, PIN_D0, 0) |
          CamGetPulls(GPIO_D1, PIN_D1, 1) |
          CamGetPulls(GPIO_D2, PIN_D2, 2) |
          CamGetPulls(GPIO_D3, PIN_D3, 3) |
          CamGetPulls(GPIO_D4, PIN_D4, 4) |
          CamGetPulls(GPIO_D5, PIN_D5, 5) |
          CamGetPulls(GPIO_D6, PIN_D6, 6) |
          CamGetPulls(GPIO_D7, PIN_D7, 7);
      }
#endif
      
      // Set up peripherals and GPIO for camera interface
      static void InitIO()
      {
        // Set up databus to all GPIO inputs
        SOURCE_SETUP(GPIO_CAM_D1, SOURCE_CAM_D1, SourceGPIO);
        SOURCE_SETUP(GPIO_CAM_D2, SOURCE_CAM_D2, SourceGPIO);
        SOURCE_SETUP(GPIO_CAM_D3, SOURCE_CAM_D3, SourceGPIO);
        SOURCE_SETUP(GPIO_CAM_D4, SOURCE_CAM_D4, SourceGPIO);
        SOURCE_SETUP(GPIO_CAM_D5, SOURCE_CAM_D5, SourceGPIO);
        SOURCE_SETUP(GPIO_CAM_D6, SOURCE_CAM_D6, SourceGPIO);
        SOURCE_SETUP(GPIO_CAM_D7, SOURCE_CAM_D7, SourceGPIO);

        // Drive PWDN and RESET to safe defaults
        GPIO_SET(GPIO_CAM_PWDN, PIN_CAM_PWDN);
        GPIO_OUT(GPIO_CAM_PWDN, PIN_CAM_PWDN);
        SOURCE_SETUP(GPIO_CAM_PWDN, SOURCE_CAM_PWDN, SourceGPIO);
        
        GPIO_RESET(GPIO_CAM_RESET_N, PIN_CAM_RESET_N);
        GPIO_OUT(GPIO_CAM_RESET_N, PIN_CAM_RESET_N);
        SOURCE_SETUP(GPIO_CAM_RESET_N, SOURCE_CAM_RESET_N, SourceGPIO);

        // Set up HSYNC to trigger DMA start on rising edge
        SOURCE_SETUP(GPIO_CAM_HSYNC, SOURCE_CAM_HSYNC, SourceGPIO | SourceDMARise);
        
        // Configure XCLK (on FTM1) for bus clock / 2 - fastest we can go (24 MHz)
        SIM_SCGC6 |= SIM_SCGC6_FTM1_MASK;   // Enable FTM1
        FTM1_SC = 0;              // Reset
        FTM1_MOD = 1;             // Minimum timer period
        FTM1_C0SC = FTM_CnSC_ELSB_MASK|FTM_CnSC_MSB_MASK;   // Edge-aligned PWM on CH0
        FTM1_C0V = 1;                                       // 50% duty cycle on CH0
        FTM1_SC = FTM_SC_CLKS(1); // Use bus clock with a /1 prescaler
        SOURCE_SETUP(GPIO_CAM_XCLK, SOURCE_CAM_XCLK, SourceAlt3);
      }
      
      volatile u8 eof_ = 0;
      
      void HALExec(u8* buf, int buflen, int eof);
      void HALInit(void);
      
#ifdef ENABLE_JPEG
      int JPEGStart(int quality);
      int JPEGCompress(u8* out, u8* in, int pitch);
      int JPEGEnd(u8* out);
#endif

      static void InitDMA();
      
      // Set up camera 
      static void InitCam()
      {
#ifdef ENABLE_JPEG
        JPEGStart(50);
#endif
        
        // Power-up/reset the camera
        MicroWait(50);
        GPIO_RESET(GPIO_CAM_PWDN, PIN_CAM_PWDN);
        MicroWait(50);
        GPIO_SET(GPIO_CAM_RESET_N, PIN_CAM_RESET_N);
          
        I2C::ReadReg(I2C_ADDR, 0xF0);
        I2C::ForceStop();
        I2C::ReadReg(I2C_ADDR, 0xF1);
        I2C::ForceStop();
        uint8_t id = I2C::ReadReg(I2C_ADDR, 0xFB);

        // Send command array to camera
        uint8_t* initCode = (uint8_t*) CAM_SCRIPT;

        for(;;) {
          uint8_t p1 = *(initCode++), p2 = *(initCode++);
          
          if (!p1 && !p2) break ;
          
          I2C::ForceStop();
          I2C::WriteReg(I2C_ADDR, p1, p2);
        }
        I2C::ForceStop();
        
        // TODO: Check that the GPIOs are okay
        //for (u8 i = 1; i; i <<= 1)
        //  printf("\r\nCam dbus: set %x, got %x", i, CamReadDB(i));
          
        InitDMA();
      }

      // Initialize DMA to row buffer, and fire an interrupt at end of each transfer
      static void InitDMA()
      {
        // Enable DMA clocks
        SIM_SCGC6 |= SIM_SCGC6_DMAMUX_MASK;
        SIM_SCGC7 |= SIM_SCGC7_DMA_MASK;
        
        // Enable interrupt
        NVIC_EnableIRQ(DMA0_IRQn);
        
        // Note:  Adjusting DMA crossbar priority doesn't help, since any peripheral I/O causes DMA-harming wait states
        // The only way that DMA works is to keep the CPU from touching registers or RAM block 0 (starting with 0x1fff)
        // MCM_PLACR = 0; // MCM_PLACR_ARB_MASK;

        // Set up DMA channel 0 to repeatedly move one line buffer worth of pixels
        DMA_CR = DMA_CR_CLM_MASK;   // Continuous loop mode? (Makes no difference?)  
        DMA_TCD0_CSR = DMA_CSR_INTMAJOR_MASK;     // Stop channel, set up interrupt on transfer complete
        DMA_TCD0_NBYTES_MLNO = sizeof(dmaBuff_);  // Number of transfers in minor loop
        DMA_TCD0_ATTR = DMA_ATTR_SSIZE(0)|DMA_ATTR_DSIZE(0);  // Source 8-bit, dest 8-bit
        DMA_TCD0_SOFF = 0;          // Source (register) doesn't increment
        DMA_TCD0_SADDR = (uint32_t)&CAMERA_DATA_GPIO;
        DMA_TCD0_DOFF = 1;          // Destination (buffer) increments
        DMA_TCD0_DADDR = (uint32_t)dmaBuff_;
        DMA_TCD0_CITER_ELINKNO = 1; // Current major loop iteration (1 per interrupt)
        DMA_TCD0_BITER_ELINKNO = 1; // Beginning major loop iteration (1 per interrupt)
        DMA_TCD0_DLASTSGA = -sizeof(dmaBuff_);    // Point back at start of buffer after each loop

        // Hook DMA start request 0 to HSYNC
        const int DMAMUX_PORTA = 49;    // This is not in the .h files for some reason
        DMAMUX_CHCFG0 = DMAMUX_CHCFG_ENBL_MASK | (DMAMUX_PORTA + PORT_INDEX(GPIO_CAM_HSYNC));     
        DMA_ERQ = DMA_ERQ_ERQ0_MASK;
       
        // Set up FTM IRQ to match hsync - must match gc0329.h timing!
        static const uint16_t CLOCK_MOD = (168 * 8) * (BUS_CLOCK / I2SPI_CLOCK) - 1;
        static const uint16_t DISABLE_MOD = (uint16_t)(CLOCK_MOD * 0.5f);

        SIM_SCGC6 |= SIM_SCGC6_FTM2_MASK;
        FTM2_SC = 0;    // Make sure timer is disabled

        /* Not currently used
        FTM2_C0V = 8 * (BUS_CLOCK / I2SPI_CLOCK) / 2; // 50% time disable I2C interrupt
        FTM2_C0SC = FTM_CnSC_CHIE_MASK |
                    //FTM_CnSC_ELSA_MASK |
                    //FTM_CnSC_ELSB_MASK |
                    //FTM_CnSC_MSA_MASK |
                    FTM_CnSC_MSB_MASK ;
        */

        FTM2_MOD = (168 * 8) * (BUS_CLOCK / I2SPI_CLOCK) - 1;   // 168 bytes at I2S_CLOCK
        FTM2_CNT = FTM2_CNTIN = 0; //8 * (BUS_CLOCK / I2SPI_CLOCK); // Place toward center of transition
        FTM2_CNTIN = 0;
        
        FTM2_SYNCONF = FTM_SYNCONF_SWRSTCNT_MASK;
        FTM2_SYNC = FTM_SYNC_SWSYNC_MASK;   // Force all registers to be loaded
      }

      // Initialize camera
      void CameraInit()
      {
        timingSynced_ = false;
        
        InitIO();
        InitCam();
        
        // Wait for everything to sync
        while (!timingSynced_)
        {}
      }
        
      void CameraSetParameters(f32 exposure, bool enableVignettingCorrection)
      {
        // TODO: vignetting correction? Why?
        const f32 maxExposure = 0xf00; // Determined empirically
        f32 correctedExposure = exposure;
        
        if(exposure < 0.0f)
        {
          correctedExposure = 0;
        } else if(exposure > 1.0f)
        {
          correctedExposure = 1.0f;
        } 
        
        //const u32 exposureU32 = (u32) floorf(correctedExposure * maxExposure + 0.5f);
        
        // Set exposure - let it get picked up during next vblank
      }
    }
  }
}

// This is triggered on camera DMA complete - but does not trigger during vblank
// So, we setup an FTM to trigger repeatedly at just the right time
extern "C"
void DMA0_IRQHandler(void)
{
  using namespace Anki::Cozmo::HAL;

  DMA_CDNE = DMA_CDNE_CDNE(0); // Clear done channel 0
  DMA_CINT = 0;   // Clear interrupt channel 0

  // The camera will send one entire frame (around 480 lines) at the wrong rate
  // So let that frame pass before we attempt to synchronize
  static u16 lineskip = 480;
  if (lineskip--)
    return;

  // Shut off DMA IRQ - we'll use FTM IRQ from now on
  DMA_TCD0_CSR = 0;
  
  // Sync to falling edge of I2SPI word select
  while(~GPIOD_PDIR & (1 << 4)) ;
  while( GPIOD_PDIR & (1 << 4)) ;

  // Turn on FTM right after sync
  FTM2_SC = FTM_SC_TOF_MASK |
          FTM_SC_TOIE_MASK |
          FTM_SC_CLKS(1) | // Select BUS_CLOCK - this enables counting
          FTM_SC_PS(0);

  timingSynced_ = true;
  NVIC_EnableIRQ(FTM2_IRQn);
  NVIC_SetPriority(FTM2_IRQn, 1);
}

extern "C"
void FTM2_IRQHandler(void)
{
  using namespace Anki::Cozmo::HAL;

  // THIS HAPPENS IN SPI.CPP FOR NOW UNTIL WE FIGURE OUT WHY THIS ISN'T FIRING
  /*
  if (FTM2_C0SC & FTM_CnSC_CHF_MASK) {
    FTM2_C0SC &= ~FTM_CnSC_CHF_MASK;
    I2C::Disable();
  }
  
  // Enable SPI DMA, Clear flag
  if (~FTM2_SC & FTM_SC_TOF_MASK) return ;
  */

  SPI::StartDMA();
  
  // Acknowledge timer interrupt now (we won't get time to later)
  FTM2_SC &= ~FTM_SC_TOF_MASK;

  static u16 line = 0;
  static int last = 0;

  CAMRAM static u8 buf[2][128];
  static u8 whichbuf = 0;
  static u8 buflen = 0;
  static u8 whichpitch = 0;    // Swizzle pitch (80 or 640)
  static u8 eof = 0;
  
  // Cheesy way to check if camera DMA buffer was updated - if it wasn't, this is a vblank line
  static u8 vblank = 0;
  if (1 == dmaBuff_[0])
    vblank++;
  else
    vblank = 0;
  if (vblank > 3)
    line = 478 + vblank;   // Set to start of vblank (adjusted for QVGA rate)
  dmaBuff_[0] = 1;

#ifdef ASCII_IMAGE
  static u8 test[] = "\n======..........................................................................................";
  static u8 hex[] = "0123456789abcdef";
  
  const int LINELEN = 96;
  static u16 frame = 0;
  test[2] = hex[frame & 15];
  test[3] = hex[(line>>8) & 15]; 
  test[4] = hex[(line>>4) & 15]; 
  test[5] = hex[line & 15];  
  if (line < 480) {
    HALExec(test, LINELEN, 0);
  } else if (line == 480) {
    HALExec(test, LINELEN, 1);
    frame++;
  } else {
    HALExec(test, 0, 0);
  }
#else
  HALExec(&buf[whichbuf][4], buflen, eof);
#endif
  
#ifdef SERIAL_IMAGE
  static int pclkoffset = 0;
  int hline = line;
  if (1) // !(line & 1))
  {
    // At 3mbaud, during 60% time, can send about 20 bytes per line, or 160x60
    if (hline < 480)
      for (int i = 0; i < 20; i++)
        UART::DebugPutc(dmaBuff_[((hline & 7) * 20 + i) * 16 + 3 + (pclkoffset >> 4)]);
    
    // Write header for start of next frame
    if (hline == 480)
    {
      UART::DebugPutc(0xBE);
      UART::DebugPutc(0xEF);
      UART::DebugPutc(0xF0);
      UART::DebugPutc(0xFF);
      UART::DebugPutc(0xBD);
      // pclkoffset++;
    }
  }
#endif

#ifdef ENABLE_JPEG
  // Fill next buffer
  whichbuf ^= 1;
  u8* p = &buf[whichbuf][4];  // Offset 4 chars to leave room for a UART header
  buflen = 0;
  
  // Compute swizzle buffer address - this rolling buffer holds exactly 8 lines of video, the minimum for JPEG
  // Addressing the rolling buffer is complicated since we write linearly (640x1) but read macroblocks (80x8)
  if (0 == (line & 7))    // Switch pitch every 8 lines
    whichpitch ^= 1;
  int pitch = whichpitch ? 80 : 640;
  u8* swizz = swizzle_ + (line & 7) * (whichpitch ? 640 : 80);

  // Encode 10 macroblocks (one strip)
  buflen += JPEGCompress(p + buflen, swizz, pitch);
  if (line == 239) {
    buflen += JPEGEnd(p + buflen);
    eof = 1;
  } else {
    eof = 0;
  }
  
  // Copy YUYV data from DMA buffer into swizzle buffer
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 80; x++)
      swizz[x + y*pitch] = dmaBuff_[(y * 80 + x) * 4 + 3];
#endif
  
  // Advance through the lines
  line++;
  if (line >= 496)
    line = 0;
}

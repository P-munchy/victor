#include "hal/board.h"
#include "hal/display.h"
#include "hal/flash.h"
#include "hal/monitor.h"
#include "hal/portable.h"
#include "hal/timers.h"
#include "hal/testport.h"
#include "hal/uart.h"
#include "hal/console.h"
#include "hal/cube.h"
#include "app/fixture.h"
#include "hal/espressif.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "app/tests.h"

u8 g_fixtureReleaseVersion = 34;
const char* BUILD_INFO = "PILOT ONLY";

BOOL g_isDevicePresent = 0;
const char* FIXTYPES[] = FIXTURE_TYPES;
FixtureType g_fixtureType = FIXTURE_NONE;
FlashParams g_flashParams;

char g_lotCode[15] = {0};
u32 g_time = 0;
u32 g_dateCode = 0;

static TestFunction* m_functions = 0;
static u8 m_functionCount = 0;

BOOL ToggleContacts(void);
static BOOL TryToRunTests(void);

// This sets up a log entry in the Device flash - showing a test was started
void WritePreTestData(void)
{
}
// This logs an error code in the Device flash - showing a test completed (maybe successfully)
void WriteFactoryBlockErrorCode(error_t errorCode)
{
}

// Not even sure why..
TestFunction* GetDebugTestFunctions()
{
  static TestFunction m_debugFunctions[] = 
  {
    NULL
  };
  return m_debugFunctions;
}

// This generates a unique ID per cycle of the test fixture
// This was meant to help the "big data" team see if the fixture was ever run but the log was lost (gaps in sequences)
int GetSequence(void)
{
  u32 sequence;
  u8 bit;
  
  {
    sequence = 0;
    u8* serialbase = (u8*)FLASH_SERIAL_BITS;
    while (serialbase[(sequence >> 3)] == 0)
    {
      sequence += 8;
      if (sequence > 0x7ffff)
      {
        ConsolePrintf("fixtureSequence,-1\r\n");
        throw ERROR_OUT_OF_SERIALS;
      }
    }
    
    u8 bitMask = serialbase[(sequence >> 3)];
    
    // Find which bit we're on
    bit = 0;
    while (!(bitMask & (1 << bit)))
    {
      bit++;
    }
    sequence += bit;
  }
  
  // Reserve this test sequence
  FLASH_Unlock();
  FLASH_ProgramByte(FLASH_SERIAL_BITS + (sequence >> 3), ~(1 << bit));
  FLASH_Lock();
  
  ConsolePrintf("fixtureSequence,%i,%i\r\n", FIXTURE_SERIAL, sequence);
  SlowPrintf("Allocated serial: %x\n", sequence);
  
  return sequence;
}

// Show the name of the fixture and version information
void SetFixtureText(void)
{
  DisplayClear();
  
  DisplayBigCenteredText(FIXTYPES[g_fixtureType]);
  
  // Show the version number in the corner
  DisplayTextHeightMultiplier(1);
  DisplayTextWidthMultiplier(1);
#ifdef FCC
  DisplayInvert(1);
  DisplayMoveCursor(55, 108);
  DisplayPutChar('c');
  DisplayPutChar('0' + ((g_fixtureReleaseVersion / 10) % 10));
  DisplayPutChar('0' + (g_fixtureReleaseVersion % 10));
  DisplayMoveCursor(55, 2);
  DisplayPutString("CERT/TEST ONLY");
#else
  DisplayMoveCursor(55, 110);
  DisplayPutChar('v');
  DisplayPutChar('0' + ((g_fixtureReleaseVersion / 10) % 10));
  DisplayPutChar('0' + (g_fixtureReleaseVersion % 10));
  DisplayMoveCursor(55, 0);
  DisplayPutString(BUILD_INFO);
#endif
  
  DisplayFlip();
}

// Clear the display and print (index / count)
void SetTestCounterText(u32 current, u32 count)
{
  DisplayClear();
  DisplayBigCenteredText("%02d/%02d", current, count);
  DisplayFlip();
  
//  SlowPrintf("Test %i/%i\r\n", current, count);
}

void SetErrorText(u16 error)
{
  STM_EVAL_LEDOn(LEDRED);  // Red
  
  DisplayClear();
  DisplayInvert(1);  
  DisplayBigCenteredText("%3i", error % 1000);
  DisplayFlip();
  
  // We want to force the red light to be seen for at least a second
  MicroWait(1000000);
}

void SetOKText(void)
{
  STM_EVAL_LEDOn(LEDGREEN);  // Green
  
  DisplayClear();
  DisplayBigCenteredText("OK");
  DisplayFlip();
}

// Return true if a device is detected (on the contacts)
bool DetectDevice(void)
{
  switch (g_fixtureType)
  {
    case FIXTURE_CHARGER_TEST:
    case FIXTURE_CUBE1_TEST:
    case FIXTURE_CUBE2_TEST:
    case FIXTURE_CUBE3_TEST:
      return CubeDetect();
    case FIXTURE_HEAD1_TEST:
      return HeadDetect();
    case FIXTURE_BODY1_TEST:
    case FIXTURE_BODY2_TEST:
    case FIXTURE_BODY3_TEST:
      return BodyDetect();
    case FIXTURE_INFO_TEST:
    case FIXTURE_ROBOT_TEST:
    case FIXTURE_PLAYPEN_TEST:
      return RobotDetect();
    case FIXTURE_MOTOR1A_TEST:
    case FIXTURE_MOTOR1B_TEST:
    case FIXTURE_MOTOR2A_TEST:      
    case FIXTURE_MOTOR2B_TEST:      
      return MotorDetect();
    case FIXTURE_FINISHC_TEST:
    case FIXTURE_FINISH1_TEST:
    case FIXTURE_FINISH2_TEST:
    case FIXTURE_FINISH3_TEST:
    case FIXTURE_FINISH_TEST:
      return FinishDetect();
  }

  // If we don't know what kind of device to look for, it's not there!
  return false;
}

// Wait until the Device has been pulled off the fixture
void WaitForDeviceOff(void)
{
  // In debug mode, keep device powered up so we can continue talking to it
  if (g_fixtureType == FIXTURE_DEBUG)
  {
    while (g_isDevicePresent)
    {
      // Note: We used to send DMC_ACK commands continuously here to prevent auto-power-off
      ConsoleUpdate();
      DisplayUpdate();
    }
    // ENBAT off
    DisableBAT();

  // In normal mode, just debounce the connection
  } else {
    // ENBAT off
    DisableBAT();
    
    u32 debounce = 0;
    while (g_isDevicePresent)
    {
      if (!DetectDevice())
      {
        // 500 checks * 1ms = 500ms delay showing error post removal
        if (++debounce >= 500)
          g_isDevicePresent = 0;
      }
      
      DisplayUpdate();  // While we wait, let screen saver kick in
    }
  }
  
  // When device is removed, restore fixture text
  SetFixtureText();
}

// Walk through tests one by one - logging to the PC and to the Device flash
int g_stepNumber;
static void RunTests()
{
  ConsoleWrite("[TEST:START]\r\n");
  
  ConsolePrintf("fixtureSerial,%i\r\n", FIXTURE_SERIAL);
  ConsolePrintf("fixtureVersion,%i\r\n", FIXTURE_VERSION);
  
  error_t error = ERROR_OK;
  try
  {
    // Write pre-test data to flash and update factory block
    WritePreTestData();
    
    for (g_stepNumber = 0; g_stepNumber < m_functionCount; g_stepNumber++)
    {      
      SetTestCounterText(g_stepNumber + 1, m_functionCount);
      m_functions[g_stepNumber]();
    }
    
    WriteFactoryBlockErrorCode(ERROR_OK);
  }
  catch (error_t e)
  {
    error = e;
  }
  
  try
  {
    // Attempt to log to the factory block...
    if (error != ERROR_OK && !IS_INTERNAL_ERROR(error))
      WriteFactoryBlockErrorCode(error);
  }
  catch (error_t e)
  {
    // ...
  }

  ConsolePrintf("[RESULT:%03i]\r\n[TEST:END]\r\n", error);
//  SlowPrintf("Test finished with error %03d\n", error);
  
  if (error != ERROR_OK)
  {
    SetErrorText(error);
  } else {
    SetOKText();
  }
  
  WaitForDeviceOff();
}

// This checks for a Device (even asleep) that is in contact with the fixture
static BOOL IsDevicePresent(void)
{
  g_isDevicePresent = 0;
  
  static u32 s_debounce = 0;
  
  if (DetectDevice())
  {
    // 300 checks * 1ms = 300ms to be sure the board is reliably in contact
    if (++s_debounce >= 300)
    {
      s_debounce = 0;
      return TRUE;
    }
  } else {
    s_debounce  = 0;
  }
  
  return FALSE;
}

// This function is meant to wake up a Device that is placed on a charger once it is detected
#if 0
BOOL ToggleContacts(void)
{
  TestEnable();
  TestEnableTx();
  MicroWait(100000);  // 100ms
  TestEnableRx();
  MicroWait(200000);  // 200ms
  return TRUE;
  
  /* 
   * The below needs to be redone for Cozmo
   *
  u32 i;
  BOOL sawPowerOn = FALSE;
  
  PIN_OD(GPIOC, 10);
  
  const u32 maxCycles = 5000;
  for (i = 0; i < maxCycles; i++)
  {
    PIN_SET(GPIOC, 10);
    PIN_OUT(GPIOC, 10);
    PIN_SET(GPIOC, 12);
    MicroWait(10);
    PIN_RESET(GPIOC, 12);
    PIN_RESET(GPIOC, 10);
    MicroWait(5);
    PIN_IN(GPIOC, 10);
    MicroWait(5);
    if (GPIO_READ(GPIOC) & (1 << 10))
    {
      sawPowerOn = TRUE;
      break;
    }
  }
  
  PIN_PULL_NONE(GPIOC, 10);
  PIN_OUT(GPIOC, 10);
  PIN_PP(GPIOC, 10);
  PIN_RESET(GPIOC, 10);
  PIN_SET(GPIOC, 12);
  PIN_OUT(GPIOC, 12);
  
  return sawPowerOn;*/
}
#endif

// Wake up the board and try to talk to it
static BOOL TryToRunTests(void)
{
  // PCB fixtures are a special case (no diagnostic mode)
  // If/when we add testport support - use ToggleContacts and then repeatedly call TryToEnterDiagnosticMode
  g_isDevicePresent = 1;
  RunTests();
  return TRUE;
}

// Repeatedly scan for a device, then run through the tests when it appears
static void MainExecution()
{
  int i;
    
  switch (g_fixtureType)
  {
    case FIXTURE_CHARGER_TEST:
    case FIXTURE_CUBE1_TEST:
    case FIXTURE_CUBE2_TEST:
    case FIXTURE_CUBE3_TEST:
      m_functions = GetCubeTestFunctions();
      break;
    case FIXTURE_HEAD1_TEST:
      m_functions = GetHeadTestFunctions();
      break;    
    case FIXTURE_BODY1_TEST:
    case FIXTURE_BODY2_TEST:
    case FIXTURE_BODY3_TEST:
      m_functions = GetBodyTestFunctions();
      break;
    case FIXTURE_INFO_TEST:
      m_functions = GetInfoTestFunctions();
      break;
    case FIXTURE_ROBOT_TEST:
      m_functions = GetRobotTestFunctions();
      break;
    case FIXTURE_PLAYPEN_TEST:
      m_functions = GetPlaypenTestFunctions();
      break;
    case FIXTURE_MOTOR1A_TEST:
    case FIXTURE_MOTOR1B_TEST:
      m_functions = GetMotor1TestFunctions();
      break;
    case FIXTURE_MOTOR2A_TEST:      
      m_functions = GetMotor2ATestFunctions();
      break;
    case FIXTURE_MOTOR2B_TEST:      
      m_functions = GetMotor2BTestFunctions();
      break;
    case FIXTURE_FINISHC_TEST:
    case FIXTURE_FINISH1_TEST:
    case FIXTURE_FINISH2_TEST:
    case FIXTURE_FINISH3_TEST:
    case FIXTURE_FINISH_TEST:
      m_functions = GetFinishTestFunctions();
      break;
    case FIXTURE_DEBUG:
      m_functions = GetDebugTestFunctions();
      break;
  }
  
  // Count the number of functions to test
  TestFunction* fn = m_functions;
  m_functionCount = 0;
  while (*fn++)
    m_functionCount++;

  STM_EVAL_LEDOff(LEDRED);
  STM_EVAL_LEDOff(LEDGREEN);
  
  ConsoleUpdate();
  
  u32 startTime = getMicroCounter();
  
  if (IsDevicePresent())
  {
    SetTestCounterText(0, m_functionCount);
    
    STM_EVAL_LEDOff(LEDRED);
    STM_EVAL_LEDOff(LEDGREEN);
    
    const int maxTries = 5;
    for (i = 0; i < maxTries; i++)
    {
      if (TryToRunTests())
        break;
    }
    
    if (i == maxTries)
    {
      error_t error = ERROR_OK;
      if (error != ERROR_OK)
      {
        SetErrorText(error);
        WaitForDeviceOff();
      }
    }
  }
}

// Fetch flash parameters - done once on boot up
void FetchParams(void)
{
  memcpy(&g_flashParams, (u8*)FLASH_PARAMS, sizeof(FlashParams));
}

// Store flash parameters
void StoreParams(void)
{
  FLASH_Unlock();
  FLASH_EraseSector(FLASH_BLOCK_PARAMS, VoltageRange_1);
  for (int i = 0; i < sizeof(FlashParams); i++)
    FLASH_ProgramByte(FLASH_PARAMS + i, ((u8*)(&g_flashParams))[i]);
  FLASH_Lock();
}
int main(void)
{
  __IO uint32_t i = 0;
 
  InitTimers();
  InitUART();
  FetchParams();
  InitConsole();
  
  SlowPutString("STARTUP!\r\n");

  // Figure out which fixture type we are
  g_fixtureType = (FixtureType)InitBoard();
  if (g_fixtureType == FIXTURE_NONE && g_flashParams.fixtureTypeOverride > 1 && g_flashParams.fixtureTypeOverride < FIXTURE_DEBUG)
    g_fixtureType = g_flashParams.fixtureTypeOverride;
  
  SlowPutString("Initializing Display...\r\n");
  
  InitCube();  
  InitDisplay();

  SetFixtureText();
  
  SlowPutString("Initializing Test Port...\r\n");
  //InitTestPort(0);

  SlowPutString("Initializing Monitor...\r\n");
  InitMonitor();
  
  SlowPutString("Ready...\r\n");

  InitEspressif();

  STM_EVAL_LEDOn(LEDRED);

  while (1)
  {  
    MainExecution();
    DisplayUpdate();
  }
}

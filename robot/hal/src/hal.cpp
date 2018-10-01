/**
 * File:        hal.cpp
 *
 * Description: Hardware Abstraction Layer for robot process
 *
 **/


// System Includes
#include <chrono>
#include <unordered_map>
#include <assert.h>

// Our Includes
#include "anki/cozmo/robot/DAS.h"
#include "anki/cozmo/robot/logging.h"
#include "anki/cozmo/robot/hal.h"
#include "anki/cozmo/robot/logEvent.h"
#include "anki/cozmo/shared/cozmoConfig.h"
#include "anki/cozmo/shared/factory/faultCodes.h"

#include "../spine/spine.h"
#include "../spine/cc_commander.h"

#include "schema/messages.h"
#include "clad/types/proxMessages.h"

#include <errno.h>

// will log all the touch sensor data to /data/misc/touch.csv
// disable when you aren't trying to debug the touch sensor
#define DEBUG_TOUCH_SENSOR 0

namespace Anki {
namespace Vector {

BodyToHead* bodyData_; //buffers are owned by the code that fills them. Spine owns this one
HeadToBody headData_;  //-we own this one.

namespace { // "Private members"

  s32 robotID_ = -1;

  // Whether or not there is a valid syscon application
  // Assume we do until we get a PAYLOAD_BOOT_FRAME
  bool haveValidSyscon_ = true;
  
#ifdef HAL_DUMMY_BODY
  BodyToHead dummyBodyData_ = {
    .cliffSense = {800, 800, 800, 800}
  };
#endif
  
  // update every tick of the robot:
  // some touch values are 0xFFFF, which we want to ignore
  // so we cache the last non-0xFFFF value and return this as the latest touch sensor reading
  u16 lastValidTouchIntensity_ = 0;
  
  // Counter for invalid prox sensor readings
  std::unordered_map<RangeStatus, u32> invalidProxSensorStatusCounts;
  TimeStamp_t nextInvalidProxDataReportSendTime_ms_ = 0;
  const u32 INVALID_PROX_DATA_REPORT_PERIOD_MS = 86400000; // Every 24 hours

  HAL::PowerState desiredPowerMode_;

  // Flag to prevent spamming of unexepected power mode warning
  bool reportUnexpectedPowerMode_ = false;

  // Time since the desired power mode was last set
  TimeStamp_t lastPowerSetModeTime_ms_ = 0;

  // Last time a HeadToBody frame was sent
  TimeStamp_t lastH2BSendTime_ms_ = 0;

  // The maximum time expected to elapse before we're sure that
  // syscon should have changed to the desired power mode,
  // indexed by desired power mode.
  static const TimeStamp_t MAX_POWER_MODE_SWITCH_TIME_MS[2] = {100,          // Calm->Active timeout
                                                               1000 + 100};  // Active->Calm timeout

  // Number of frames to skip sending to body when in calm power mode
  static const int NUM_CALM_MODE_SKIP_FRAMES = 12;  // Every 60ms
  int calmModeSkipFrameCount_ = 0;

  static const f32 kBatteryScale = 2.8f / 2048.f;
  struct spine_ctx spine_;
  uint8_t frameBuffer_[SPINE_B2H_FRAME_LEN];
  uint8_t readBuffer_[4096];
  BodyToHead BootBodyData_ = { //dummy data for boot stub frames
    .framecounter         = 0,
    .flags                = RUNNING_FLAGS_SENSORS_VALID,  // emulate active power mode
    .battery.flags        = POWER_ON_CHARGER,
    .battery.main_voltage = (int16_t)(5.0/kBatteryScale),
    .battery.charger      = (int16_t)(5.0/kBatteryScale),
  };
  
} // "private" namespace

// Forward Declarations
Result InitRadio();
void StopRadio();
void InitIMU();
void StopIMU();
void ProcessIMUEvents();
void ProcessFailureCode();
void ProcessTouchLevel(void);
void ProcessMicError();
void PrintConsoleOutput(void);


extern "C" {
  ssize_t spine_write_frame(spine_ctx_t spine, PayloadId type, const void* data, int len);
  void record_body_version( const struct VersionInfo* info);
  void request_version(void) {
    spine_write_frame(&spine_, PAYLOAD_VERSION, NULL, 0);
  }
}

// Tries to select from the spine fd
// If it times out too many times then
// syscon must be hosed or there is no spine
// connection
bool check_select_timeout(spine_ctx_t spine)
{
  int fd = spine_get_fd(spine);

  static u8 selectTimeoutCount = 0;
  if(selectTimeoutCount >= 5)
  {
    AnkiError("spine.check_select_timeout.timeoutCountReached","");
    FaultCode::DisplayFaultCode(FaultCode::SPINE_SELECT_TIMEOUT);
    return true;
  }

  static fd_set fdSet;
  FD_ZERO(&fdSet);
  FD_SET(fd, &fdSet);
  static timeval timeout;
  timeout.tv_sec = 1;
  timeout.tv_usec = 0;
  ssize_t s = select(FD_SETSIZE, &fdSet, NULL, NULL, &timeout);
  if(s == 0)
  {
    selectTimeoutCount++;
    AnkiWarn("spine.check_select_timeout.selectTimedout", "%u", selectTimeoutCount);
    return true;
  }
  return false;
}

ssize_t robot_io(spine_ctx_t spine)
{
  int fd = spine_get_fd(spine);

  EventStart(EventType::ROBOT_IO_READ);

  if(check_select_timeout(spine))
  {
    return -1;
  }

  ssize_t r = read(fd, readBuffer_, sizeof(readBuffer_));

  EventAddToMisc(EventType::ROBOT_IO_READ, (uint32_t)r);
  EventStop(EventType::ROBOT_IO_READ);

  if (r > 0)
  {
    EventStart(EventType::ROBOT_IO_RECEIVE);
    r = spine_receive_data(spine, (const void*)readBuffer_, r);
    EventStop(EventType::ROBOT_IO_RECEIVE);
  }
  else if (r < 0)
  {
    if (errno == EAGAIN) {
      r = 0;
    }
  }
  return r;
}

// Populate bodyData when there's no app
void populate_boot_body_data(const struct SpineMessageHeader* hdr)
{
  if (!haveValidSyscon_) {
    //extract button data from stub packet and put in fake full packet
    uint8_t button_pressed = ((struct MicroBodyToHead*)(hdr+1))->buttonPressed;
    BootBodyData_.touchLevel[1] = button_pressed ? 0xFFFF : 0x0000;
    bodyData_ = &BootBodyData_;
  }
}

void das_log_version_info(const VersionInfo* versionInfo)
{
  // Stringify ein in hex
  // "* 2" because 2 hex chars for each byte, "+ 1" for null byte
  char ein[sizeof(versionInfo->ein) * 2 + 1] = {0};
  const int ein_arr_len = sizeof(versionInfo->ein) / sizeof(versionInfo->ein[0]);
  for (int i=0; i < ein_arr_len; ++i) {
    sprintf(&ein[2*i], "%02x", versionInfo->ein[i]);
  }

  // Stringify app version in hex
  // "* 2" because 2 hex chars for each byte, "+ 1" for null byte
  char app_version[sizeof(versionInfo->app_version) * 2 + 1] = {0};
  const int app_version_arr_len = sizeof(versionInfo->app_version) / sizeof(versionInfo->app_version[0]);
  for (int i=0; i < app_version_arr_len; ++i) {
    sprintf(&app_version[2*i], "%02x", versionInfo->app_version[i]);
  }

  DASMSG(hal_body_version, "hal.body_version", "Body version info");
  DASMSG_SET(i1, versionInfo->hw_revision, "Hardware revision");
  DASMSG_SET(i2, versionInfo->hw_model,    "Hardware model");
  DASMSG_SET(s1, ein,                      "Electronic Identification Number");
  DASMSG_SET(s2, app_version,              "Application version");
  DASMSG_SEND();
}

Result spine_wait_for_first_frame(spine_ctx_t spine, const int * shutdownSignal)
{
  TimeStamp_t startWait_ms = HAL::GetTimeStamp();
  bool initialized = false;
  int read_count = 0;

  assert(shutdownSignal != nullptr);

  while (!initialized && *shutdownSignal == 0) {
    // If we spend more than 2 seconds waiting for the first frame,
    // something must be wrong. Likely there is no body or head to body
    // connection
    if(HAL::GetTimeStamp() - startWait_ms > 2000)
    {
      AnkiError("spine_wait_for_first_frame.timeout","");
      break;
    }

    ssize_t r = spine_parse_frame(spine, &frameBuffer_, sizeof(frameBuffer_), NULL);

    if (r < 0) {
      continue;
    } else if (r > 0) {
      const struct SpineMessageHeader* hdr = (const struct SpineMessageHeader*)frameBuffer_;
      if (hdr->payload_type == PAYLOAD_DATA_FRAME) {
        initialized = true;
        const struct spine_frame_b2h* frame = (const struct spine_frame_b2h*)frameBuffer_;
        bodyData_ = (BodyToHead*)&frame->payload;
      }
      else if (hdr->payload_type == PAYLOAD_CONT_DATA) {
        ccc_data_process( (ContactData*)(hdr+1) );
        continue;
      }
      else if (hdr->payload_type == PAYLOAD_VERSION) {
        const VersionInfo* versionInfo = (VersionInfo*)(hdr+1);
        record_body_version(versionInfo);
        das_log_version_info(versionInfo);
      }
      else if (hdr->payload_type == PAYLOAD_BOOT_FRAME) {

        // If the first frame we receive is a boot frame then
        // it means there is no valid syscon application
        haveValidSyscon_ = false;
        AnkiWarn("HAL.SpineWaitForFirstFrame.InvalidSyscon","");

        initialized = true;
        populate_boot_body_data(hdr);
      }
      else {
        LOGD("Unknown Frame Type %x\n", hdr->payload_type);
      }

    } else {
      // r == 0 (waiting)
      if (read_count > 50) {
        if (!initialized) {
          spine_set_mode(&spine_, RobotMode_RUN);
        }
        read_count = 0;
      }
    }

    robot_io(&spine_);
    read_count++;
  }

  // If we failed to initialize or we don't have valid syscon
  // display a fault code
  if(!initialized || !haveValidSyscon_)
  {
    FaultCode::DisplayFaultCode(FaultCode::NO_BODY);
  }

  return (initialized ? RESULT_OK : RESULT_FAIL_IO_TIMEOUT);
}

Result HAL::Init(const int * shutdownSignal)
{
  using Result = Anki::Result;
  
  // Set ID
  robotID_ = Anki::Vector::DEFAULT_ROBOT_ID;

  InitIMU();

  if (InitRadio() != RESULT_OK) {
    AnkiError("HAL.Init.InitRadioFailed", "");
    return RESULT_FAIL;
  }

#ifndef HAL_DUMMY_BODY
  {
    AnkiInfo("HAL.Init.StartingSpineHAL", "");
    
    nextInvalidProxDataReportSendTime_ms_ = GetTimeStamp() + INVALID_PROX_DATA_REPORT_PERIOD_MS;
    invalidProxSensorStatusCounts = {
      {RangeStatus::SIGMA_FAIL, 0},
      {RangeStatus::SIGNAL_FAIL, 0},
      {RangeStatus::MIN_RANGE_FAIL, 0},
      {RangeStatus::PHASE_FAIL, 0},
      {RangeStatus::HARDWARE_FAIL, 0},
      {RangeStatus::NO_UPDATE, 0}
    };

    desiredPowerMode_ = POWER_MODE_ACTIVE;

    spine_init(&spine_);
    struct spine_params params = {
      .devicename = SPINE_TTY,
      .baudrate = SPINE_BAUD
    };
    int errCode = spine_open(&spine_, params);

    if (errCode != err_OK) {
      return RESULT_FAIL;
    }
    AnkiDebug("HAL.Init.SettingRunMode", "");

    spine_set_mode(&spine_, RobotMode_RUN);

    AnkiDebug("HAL.Init.WaitingForDataFrame", "");

    const Result res =  spine_wait_for_first_frame(&spine_, shutdownSignal);
    if(res != RESULT_OK)
    {
      AnkiError("HAL.Init.NoFirstFrame", "");
      return res;
    }
    AnkiDebug("HAL.Init.GotFirstFrame", "");

    request_version();  //get version so we have it when we need it.
  }
#else
  bodyData_ = &dummyBodyData_;
#endif
  assert(bodyData_ != nullptr);


  for (int m = MOTOR_LIFT; m < MOTOR_COUNT; m++) {
    MotorResetPosition((MotorID)m);
  }
  AnkiInfo("HAL.Init.Success", "");

  return RESULT_OK;
}  // Init()

void handle_payload_data(const uint8_t frame_buffer[]) {

  memcpy(frameBuffer_, frame_buffer, sizeof(frameBuffer_));
  bodyData_ = (BodyToHead*)(frameBuffer_ + sizeof(struct SpineMessageHeader));

  if (ccc_commander_is_active()) {
    ccc_payload_process(bodyData_);
  }

}


Result spine_get_frame() {
  Result result = RESULT_FAIL_IO_TIMEOUT;
  uint8_t frame_buffer[SPINE_B2H_FRAME_LEN];

  ssize_t r = 0;
  do {
    EventStart(EventType::PARSE_FRAME);
    r = spine_parse_frame(&spine_, frame_buffer, sizeof(frame_buffer), NULL);
    EventStop(EventType::PARSE_FRAME);

    if (r < 0)
    {
      continue;
    }
    else if (r > 0)
    {
      const struct SpineMessageHeader* hdr = (const struct SpineMessageHeader*)frame_buffer;
      if (hdr->payload_type == PAYLOAD_DATA_FRAME) {
        handle_payload_data(frame_buffer);  //payload starts immediately after header
        result = RESULT_OK;
      }
      else if (hdr->payload_type == PAYLOAD_CONT_DATA) {
        LOGD("Handling CD payload type %x\n", hdr->payload_type);
        ccc_data_process( (ContactData*)(hdr+1) );
        result = RESULT_OK;
      }
      else if (hdr->payload_type == PAYLOAD_VERSION) {
        LOGD("Handling VR payload type %x\n", hdr->payload_type);
        const VersionInfo* versionInfo = (VersionInfo*)(hdr+1);
        record_body_version(versionInfo);
        das_log_version_info(versionInfo);
      }
      else if (hdr->payload_type == PAYLOAD_BOOT_FRAME) {
        populate_boot_body_data(hdr);
        result = RESULT_OK;
      }
      else {
        LOGD("Unknown Frame Type %x\n", hdr->payload_type);
      }
    }
    else
    {
      // get more data
      EventStart(EventType::ROBOT_IO);
      robot_io(&spine_);
      EventStop(EventType::ROBOT_IO);
    }

    if(result == RESULT_OK)
    {
      break;
    }

  } while (r != 0);

  return result;
}

void ReportRecentInvalidProxDataReadings()
{
  const TimeStamp_t timeSinceBoot_ms = HAL::GetTimeStamp();
  if ( (invalidProxSensorStatusCounts.at(RangeStatus::SIGMA_FAIL) +
        invalidProxSensorStatusCounts.at(RangeStatus::SIGNAL_FAIL) +
        invalidProxSensorStatusCounts.at(RangeStatus::PHASE_FAIL)) > 0) {
    DASMSG(hal_invalid_prox_reading_report, "hal.invalid_prox_reading_report", "Report the recent number of minor status failures");
    DASMSG_SET(i1, timeSinceBoot_ms, "Time (ms) since last boot")
    DASMSG_SET(i2, invalidProxSensorStatusCounts.at(RangeStatus::SIGMA_FAIL), "Number of recent sigma failures");
    DASMSG_SET(i3, invalidProxSensorStatusCounts.at(RangeStatus::SIGNAL_FAIL), "Number of recent signal failures");
    DASMSG_SET(i4, invalidProxSensorStatusCounts.at(RangeStatus::PHASE_FAIL), "Number of recent phase failures");
    DASMSG_SEND();
  }
  
  if ( (invalidProxSensorStatusCounts.at(RangeStatus::MIN_RANGE_FAIL) +
        invalidProxSensorStatusCounts.at(RangeStatus::HARDWARE_FAIL) +
        invalidProxSensorStatusCounts.at(RangeStatus::NO_UPDATE)) > 0) {
    DASMSG(hal_severe_invalid_prox_reading_report, "hal.severe_invalid_prox_reading_report", "Report of recent number of severe status failures");
    DASMSG_SET(i1, timeSinceBoot_ms, "Time (ms) since last boot")
    DASMSG_SET(i2, invalidProxSensorStatusCounts.at(RangeStatus::MIN_RANGE_FAIL), "Number of recent min range failures");
    DASMSG_SET(i3, invalidProxSensorStatusCounts.at(RangeStatus::HARDWARE_FAIL), "Number of recent hardware failures");
    DASMSG_SET(i4, invalidProxSensorStatusCounts.at(RangeStatus::NO_UPDATE), "Number of recent missing updates");
    DASMSG_SEND();
  }

  nextInvalidProxDataReportSendTime_ms_ += INVALID_PROX_DATA_REPORT_PERIOD_MS;
  for (auto& it : invalidProxSensorStatusCounts) {
    it.second = 0;
  }
}

extern "C"  ssize_t spine_write_ccc_frame(spine_ctx_t spine, const struct ContactData* ccc_payload);
#define MIN_CCC_XMIT_SPACING_US 5000

Result HAL::Step(void)
{
  EventStep();
  EventStart(EventType::HAL_STEP);

  static uint32_t last_packet_send = 0;
  uint32_t now = GetMicroCounter();

  Result result = RESULT_OK;
  bool commander_is_active = false;

#ifndef HAL_DUMMY_BODY

  headData_.framecounter++;

  //Packet throttle.
  if (now-last_packet_send >= MIN_CCC_XMIT_SPACING_US ) {
    //check if the charge contact commander is active,
    //if so, override normal operation
    // commander_is_active = ccc_commander_is_active();
    // struct HeadToBody* h2bp = (commander_is_active) ? ccc_data_get_response() : &headData_;
    struct HeadToBody* h2bp =  &headData_;

    const TimeStamp_t now_ms = GetTimeStamp();

    // Only send H2B message if there is actually a valid syscon application
    // otherwise bootloader will ack these messages and we don't handle those
    if(haveValidSyscon_)
    {
      EventStart(EventType::WRITE_SPINE);
      if (desiredPowerMode_ == POWER_MODE_CALM && !commander_is_active) {
        if (++calmModeSkipFrameCount_ > NUM_CALM_MODE_SKIP_FRAMES) {
          spine_set_lights(&spine_, &(h2bp->lightState));
          calmModeSkipFrameCount_ = 0;
        }
      } else {
        spine_write_h2b_frame(&spine_, h2bp);
        lastH2BSendTime_ms_ = now_ms;
      }
      EventStop(EventType::WRITE_SPINE);
    }
    
    // Print warning if power mode is unexpected
    const HAL::PowerState currPowerMode = PowerGetMode();
    if (currPowerMode != desiredPowerMode_) {
      if ( ((lastPowerSetModeTime_ms_ == 0) && reportUnexpectedPowerMode_) || 
           ((lastPowerSetModeTime_ms_ > 0) && ((now_ms - lastPowerSetModeTime_ms_ > MAX_POWER_MODE_SWITCH_TIME_MS[desiredPowerMode_])))
           ) {
        AnkiWarn("HAL.Step.UnexpectedPowerMode", 
                 "Curr mode: %u, Desired mode: %u, now: %ums, lastSetModeTime: %ums, lastH2BSendTime: %ums",
                 currPowerMode, desiredPowerMode_, now_ms, lastPowerSetModeTime_ms_, lastH2BSendTime_ms_);
        lastPowerSetModeTime_ms_ = 0;  // Reset time to avoid spamming warning
        reportUnexpectedPowerMode_ = false;
      }
    } else {
      reportUnexpectedPowerMode_ = true;
    }
    
    // Send DAS msg every 24 hours to report the number of invalid prox sensor readings
    if (now_ms > nextInvalidProxDataReportSendTime_ms_) {
      ReportRecentInvalidProxDataReadings();
    }
    
    struct ContactData* ccc_response = ccc_text_response();
    if (ccc_response) {
      spine_write_ccc_frame(&spine_, ccc_response);
    }
    last_packet_send = now;
  }

#if !PROCESS_IMU_ON_THREAD
  ProcessIMUEvents();
#endif

  EventStart(EventType::READ_SPINE);

  do {
    result = spine_get_frame();
  } while(result != RESULT_OK);

  EventStop(EventType::READ_SPINE);

#else // else have dummy body

#if !PROCESS_IMU_ON_THREAD
  ProcessIMUEvents();
#endif

#endif // #ifndef HAL_DUMMY_BODY

  ProcessFailureCode();

  ProcessMicError();
  
  ProcessTouchLevel(); // filter invalid values from touch sensor
  
#if(DEBUG_TOUCH_SENSOR)
  static FILE* fp = nullptr;
  if(fp == nullptr) {
    fp = fopen("/data/misc/touch.csv","w+");
  }
  fprintf(fp, "%d\n", lastValidTouchIntensity_);
#endif

  // Monitor body temperature (For debugging only)
  if (bodyData_ != nullptr) {
    static u32 lastReportedBodyTempTime_ms = 0;
    static s16 lastReportedBodyTemp_C = 0;
    TimeStamp_t currTime_ms = HAL::GetTimeStamp();
    u16 currTemp = bodyData_->battery.temperature;

    if (currTemp > 50 &&
        currTemp != lastReportedBodyTemp_C &&
        (currTime_ms - lastReportedBodyTempTime_ms > 5000)) {
      AnkiWarn("HAL.Step.BodyTemp", "%dC", currTemp);
      lastReportedBodyTempTime_ms = currTime_ms;
      lastReportedBodyTemp_C = currTemp;
    }
  }


  PrintConsoleOutput();

  EventStop(EventType::HAL_STEP);

  //return a fail code if commander is active to prevent robotics from getting confused
  return (commander_is_active) ? RESULT_FAIL_IO_UNSYNCHRONIZED : result;
}

void StopMotors()
{
  for (int m = 0; m < MOTOR_COUNT; m++) {
    HAL::MotorSetPower((MotorID)m, 0.f);
  }
}

void HAL::Stop()
{
  AnkiInfo("HAL.Stop", "");
  StopMotors();
  StopRadio();
  StopIMU();
  DisconnectRadio();
  ReportRecentInvalidProxDataReadings();
}

void ProcessTouchLevel(void)
{
  if(bodyData_->touchHires[HAL::BUTTON_CAPACITIVE] != 0xFFFF) {
    lastValidTouchIntensity_ = bodyData_->touchHires[HAL::BUTTON_CAPACITIVE];
  }
}

void ProcessFailureCode()
{
#define DRAW_FAULT(fault) \
  if(!faultCodeDrawn) { \
    faultCodeDrawn = true; \
    FaultCode::DisplayFaultCode(fault); \
  }
  
  static bool faultCodeDrawn = false;
  switch(bodyData_->failureCode)
  {
    case BOOT_FAIL_NONE:
      break;
    case BOOT_FAIL_TOF:
      DRAW_FAULT(FaultCode::TOF);
      break;
    case BOOT_FAIL_CLIFF1:
      DRAW_FAULT(FaultCode::CLIFF_FL);
      break;
    case BOOT_FAIL_CLIFF2:
      DRAW_FAULT(FaultCode::CLIFF_FR);
      break;
    case BOOT_FAIL_CLIFF3:
      DRAW_FAULT(FaultCode::CLIFF_BL);
      break;
    case BOOT_FAIL_CLIFF4:
      DRAW_FAULT(FaultCode::CLIFF_BR);
      break;
  }
#undef DRAW_FAULT
}

void ProcessMicError()
{
  static uint8_t sameBitsArr[32] = {0};
  static uint32_t prevMicError = 0;

  uint32_t micError = *((uint32_t*)bodyData_->micError);

  // Negation of XOR tells you which bits are the same
  uint32_t sameBits = ~(prevMicError ^ micError);

  static uint8_t whichChannelsStuck = 0;
  
  for(int i = 0; i < 32; ++i)
  {
    if((sameBits >> i) & 1)
    {
      sameBitsArr[i]++;
    }
    else
    {
      sameBitsArr[i] = 0;
    }

    if(sameBitsArr[i] > 254)
    {
      uint8_t iEven = (i % 2) + 1; // 0b01 if even, 0b10 if odd
      whichChannelsStuck |= (i >= 16 ? (iEven << 2): iEven) ;
    }
  }

  static bool sentDAS = false;
  if(whichChannelsStuck > 0)
  {
    sentDAS = true;
    
    AnkiError("HAL.ProcessMicError.StuckBitDetected", "0x%x", whichChannelsStuck);
    
    DASMSG(mic_stuck_bit,
           "robot.stuck_mic_bit",
           "Indicates that one or more of the microphones is not functioning properly");
    DASMSG_SET(i1,
               whichChannelsStuck,
               "Bit mask indicating which of the 4 mic channels have stuck bits");
    DASMSG_SEND();

  }
  
  prevMicError = micError;
}

// Get the number of microseconds since boot
u32 HAL::GetMicroCounter(void)
{
  auto currTime = std::chrono::steady_clock::now();
  return static_cast<TimeStamp_t>(std::chrono::duration_cast<std::chrono::microseconds>(currTime.time_since_epoch()).count());
}

void HAL::MicroWait(u32 microseconds)
{
  u32 now = GetMicroCounter();
  while ((GetMicroCounter() - now) < microseconds)
    ;
}

TimeStamp_t HAL::GetTimeStamp(void)
{
  auto currTime = std::chrono::steady_clock::now();
  return static_cast<TimeStamp_t>(std::chrono::duration_cast<std::chrono::milliseconds>(currTime.time_since_epoch()).count());
}

void HAL::SetLED(LEDId led_id, u32 color)
{
  assert(led_id >= 0 && led_id < LED_COUNT);

  // Light order is swapped in syscon
  const u32 ledIdx = LED_COUNT - led_id - 1;

  uint8_t r = (color >> LED_RED_SHIFT) & LED_CHANNEL_MASK;
  uint8_t g = (color >> LED_GRN_SHIFT) & LED_CHANNEL_MASK;
  uint8_t b = (color >> LED_BLU_SHIFT) & LED_CHANNEL_MASK;
  headData_.lightState.ledColors[ledIdx * LED_CHANEL_CT + LED0_RED] = r;
  headData_.lightState.ledColors[ledIdx * LED_CHANEL_CT + LED0_GREEN] = g;
  headData_.lightState.ledColors[ledIdx * LED_CHANEL_CT + LED0_BLUE] = b;
}

void HAL::SetSystemLED(u32 color)
{
  uint8_t r = (color >> LED_RED_SHIFT) & LED_CHANNEL_MASK;
  uint8_t g = (color >> LED_GRN_SHIFT) & LED_CHANNEL_MASK;
  uint8_t b = (color >> LED_BLU_SHIFT) & LED_CHANNEL_MASK;
  headData_.lightState.ledColors[LED3_RED] = r;
  // Technically have no control over green, it is always on
  headData_.lightState.ledColors[LED3_GREEN] = g;
  headData_.lightState.ledColors[LED3_BLUE] = b;
}

u32 HAL::GetID()
{
  return robotID_;
}

inline u16 FlipBytes(u16 v) {
  return ((((v) & 0x00FF)<<8) | ((v)>>8));
}

inline RangeStatus ConvertToApiRangeStatus(const u8 deviceRangeStatus)
{
  uint8_t internal_device_range_status = ((deviceRangeStatus & 0x78) >> 3);
  
  // For a more detailed explanation of the failure codes, refer to the VL53L0X API:
  // https://www.st.com/content/ccc/resource/technical/document/user_manual/group0/6b/4e/24/90/d8/05/47/a5/DM00279088/files/DM00279088.pdf/jcr:content/translations/en.DM00279088.pdf
  switch (internal_device_range_status)
  {
    // Mapping for internal values obtained from Adafruit VL53L0X library:
    // https://github.com/adafruit/Adafruit_VL53L0X/blob/89de1c2db61668c74d79a7201389e3bc8519cdf9/src/core/src/vl53l0x_api_core.cpp#L2025.
    // NOTE: This mapping assumes that all of the internal sensor checks (sigma limit, signal ref clip limit, etc.) are DISABLED.
    // If this assumption is not valid at some point in the future due to a firmware configuration change,
    // the results of these checks will need to be incorporated into this mapping.
    case 1:
    case 2:
    case 3:
      return RangeStatus::HARDWARE_FAIL;
    case 6:
    case 9:
      return RangeStatus::PHASE_FAIL;
    case 8:
    case 10:
      return RangeStatus::MIN_RANGE_FAIL;
    case 4:
      return RangeStatus::SIGNAL_FAIL;
    case 11:
      return RangeStatus::RANGE_VALID;
    default:
      // This error should NOT trigger
      // AnkiWarn("HAL.ConvertToApiRangeStatus.InvalidStatus", "0x%02x", deviceRangeStatus);
      return RangeStatus::NO_UPDATE;
  }
}

ProxSensorDataRaw HAL::GetRawProxData()
{
  ProxSensorDataRaw proxData;
  proxData.rangeStatus = ConvertToApiRangeStatus(bodyData_->proximity.rangeStatus);
  // Track the occurrences of invalid prox sensor readings, reported on a periodic basis
  if (proxData.rangeStatus != RangeStatus::RANGE_VALID) {
    ++invalidProxSensorStatusCounts.at(proxData.rangeStatus);
  }
  if (HAL::PowerGetMode() == POWER_MODE_ACTIVE) {
    proxData.distance_mm      = FlipBytes(bodyData_->proximity.rangeMM);
    // Signal/Ambient Rate are fixed point 9.7, so convert to float:
    proxData.signalIntensity  = static_cast<float>(FlipBytes(bodyData_->proximity.signalRate)) / 128.f;
    proxData.ambientIntensity = static_cast<float>(FlipBytes(bodyData_->proximity.ambientRate)) / 128.f;
    // SPAD count is fixed point 8.8, so convert to float:
    proxData.spadCount        = static_cast<float>(FlipBytes(bodyData_->proximity.spadCount)) / 256.f;
  } else {
    // Calm mode values
    proxData.distance_mm      = PROX_CALM_MODE_DIST_MM;
    proxData.signalIntensity  = 0.f;
    proxData.ambientIntensity = 0.f;
    proxData.spadCount        = 200.f;
  }
  return proxData;
}

u16 HAL::GetButtonState(const ButtonID button_id)
{
  assert(button_id >= 0 && button_id < BUTTON_COUNT);
  if(button_id==HAL::BUTTON_CAPACITIVE) {
    return lastValidTouchIntensity_;
  }
  return bodyData_->touchLevel[button_id];
}
  
u16 HAL::GetRawCliffData(const CliffID cliff_id)
{
  assert(cliff_id < DROP_SENSOR_COUNT);
  if (HAL::PowerGetMode() == POWER_MODE_ACTIVE) {
    return bodyData_->cliffSense[cliff_id];
  }

  return CLIFF_CALM_MODE_VAL;
}

bool HAL::HandleLatestMicData(SendDataFunction sendDataFunc)
{
  #if MICDATA_ENABLED
  {
    sendDataFunc(bodyData_->audio, MICDATA_SAMPLES_COUNT);
  }
  #endif
  return false;
}

f32 HAL::BatteryGetVoltage()
{
  // scale raw ADC counts to voltage (conversion factor from Vandiver)
  return kBatteryScale * bodyData_->battery.main_voltage;
}

bool HAL::BatteryIsCharging()
{
  // The POWER_IS_CHARGING flag is set whenever syscon has the charging
  // circuitry enabled. It does not necessarily mean the charging circuit
  // is actually charging the battery. It may remain true even after the
  // battery is fully charged.
  return bodyData_->battery.flags & POWER_IS_CHARGING;
}

bool HAL::BatteryIsOnCharger()
{
  // The POWER_ON_CHARGER flag is set whenever there is sensed voltage on
  // the charge contacts.
  return bodyData_->battery.flags & POWER_ON_CHARGER;
}

bool HAL::BatteryIsDisconnected()
{
  // The POWER_BATTERY_DISCONNECTED flag is set whenever the robot is on
  // the charge base, but the battery has been disconnected from the
  // charging circuit.
  return bodyData_->battery.flags & POWER_BATTERY_DISCONNECTED;
}

bool HAL::BatteryIsOverheated()
{
  return bodyData_->battery.flags & POWER_IS_OVERHEATED;
}

f32 HAL::ChargerGetVoltage()
{
  // scale raw ADC counts to voltage (conversion factor from Vandiver)
  return kBatteryScale * bodyData_->battery.charger;
}

u8 HAL::BatteryGetTemperature_C()
{
  if (bodyData_->battery.temperature > 0xff) {
    AnkiWarn("HAL.BatteryGetTemperature_C.InvalidTemp", "%u", bodyData_->battery.temperature);
    return 0;
  }
  return static_cast<u8>(bodyData_->battery.temperature);
}

u8 HAL::GetWatchdogResetCounter()
{
  // not (yet) implemented in HAL in V2
  return 0;//bodyData_->status.watchdogCount;
}

void HAL::Shutdown()
{
  HAL::Stop();
  spine_shutdown(&spine_);
}


void HAL::PowerSetDesiredMode(const PowerState state)
{
  AnkiInfo("HAL.PowerSetDesiredMode", "%d", state);
  DASMSG(hal_active_power_mode, "hal.active_power_mode", "Power mode status");
  DASMSG_SET(i1, state == POWER_MODE_ACTIVE, "Active mode (1) or calm mode (0)");
  DASMSG_SET(i2, HAL::BatteryGetTemperature_C(), "Battery temperature (C)");
  DASMSG_SEND();
  desiredPowerMode_ = state;
  lastPowerSetModeTime_ms_ = HAL::GetTimeStamp();
}

HAL::PowerState HAL::PowerGetDesiredMode()
{
  return desiredPowerMode_;
}

HAL::PowerState HAL::PowerGetMode()
{
  if(bodyData_ == nullptr)
  {
    return POWER_MODE_ACTIVE;
  }
  return (bodyData_->flags & RUNNING_FLAGS_SENSORS_VALID) ? POWER_MODE_ACTIVE : POWER_MODE_CALM;
}


} // namespace Vector
} // namespace Anki


extern "C" {

  u64 steady_clock_now(void) {
    return std::chrono::steady_clock::now().time_since_epoch().count();
  }

  void hal_terminate(void) {
    Anki::Vector::HAL::Shutdown();
  }

}

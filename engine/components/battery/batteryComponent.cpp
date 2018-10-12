/**
 * File: batteryComponent.cpp
 *
 * Author: Matt Michini
 * Created: 2/27/2018
 *
 * Description: Component for monitoring battery state-of-charge, time on charger,
 *              and related information.
 *
 * Copyright: Anki, Inc. 2018
 *
 **/

#include "engine/components/battery/batteryComponent.h"

#include "engine/aiComponent/aiComponent.h"

#include "engine/blockWorld/blockWorld.h"
#include "engine/charger.h"
#include "engine/components/battery/batteryStats.h"
#include "engine/components/cubes/cubeBatteryComponent.h"
#include "engine/components/movementComponent.h"
#include "engine/robot.h"

#include "anki/cozmo/shared/cozmoConfig.h"

#include "coretech/common/engine/utils/timer.h"

#include "clad/externalInterface/messageEngineToGame.h"
#include "clad/types/robotStatusAndActions.h"

#include "osState/osState.h"
#include "proto/external_interface/shared.pb.h"

#include "util/console/consoleInterface.h"
#include "util/filters/lowPassFilterSimple.h"
#include "util/logging/DAS.h"

#include <unistd.h>

namespace Anki {
namespace Vector {

namespace {
  // How often the filtered voltage reading is updated
  // (i.e. Rate of RobotState messages)
  const float kBatteryVoltsUpdatePeriod_sec =  STATE_MESSAGE_FREQUENCY * ROBOT_TIME_STEP_MS / 1000.f;
  const float kCalmModeBatteryVoltsUpdatePeriod_sec =  STATE_MESSAGE_FREQUENCY_CALM * ROBOT_TIME_STEP_MS / 1000.f;

  // Time constant of the low-pass filter for battery voltage
  const float kBatteryVoltsFilterTimeConstant_sec = 6.0f;

  // Voltage above which the battery is considered fully charged
  // after _saturationChargeTimeRemaining_sec expires.
  const float kSaturationChargingThresholdVolts = 4.1f;

  // Max time to wait after kSaturationChargingThresholdVolts is reached
  // before battery is considered "fully charged".
  const float kMaxSaturationTime_sec = 7 * 60.f;

  // Voltage below which battery is considered in a low charge state
  // At 3.6V, there is about 7 minutes of battery life left (if stationary, minimal processing, no wifi transmission, no sound)
  const float kLowBatteryThresholdVolts = 3.6f;

  // We apply a small hysteresis band when transitioning between Nominal and Low battery
  const float kLowBatteryHysteresisVolts = 0.05f;

  // Voltage below which battery is considered in a low charge state _when on charger_. When the robot is placed on the
  // charger, the voltage immediately increases by a step amount, so a different threshold is required. The value of
  // 4.0V was chosen because it takes about 5 minutes for the battery to reach 4.0V when placed on the charger at 3.6V
  // (the 'off-charger' low battery threshold).
  const float kOnChargerLowBatteryThresholdVolts = 4.0f;

  // Console var for faking low battery
  CONSOLE_VAR(bool, kFakeLowBattery, "BatteryComponent", false);
  const float kFakeLowBatteryVoltage = 3.5f;

  const float kInvalidPitchAngle = std::numeric_limits<float>::max();

  // The app needs an estimate of time-to-charge in a few cases (mostly onboarding/FTUE). When low
  // battery, the robot must sit on the charger for kRequiredChargeTime_s seconds. If the robot is
  // removed, the timer pauses. When being re-placed on the charger, the remaining time is increased
  // my an amount proportional to the time off charger (const is kExtraChargingTimePerDischargePeriod_s),
  // clamped at a max of kRequiredChargeTime_s.
  CONSOLE_VAR_RANGED(float, kRequiredChargeTime_s, "BatteryComponent", 5*60.0f, 10.0f, 9999.0f ); // must be set before low battery and then not changed
  const float kExtraChargingTimePerDischargePeriod_s = 1.0f; // if off the charger for 1 min, must charge an additional 1*X mins
}

BatteryComponent::BatteryComponent()
  : IDependencyManagedComponent<RobotComponentID>(this, RobotComponentID::Battery)
  , _saturationChargeTimeRemaining_sec(kMaxSaturationTime_sec)
  , _chargerFilter(std::make_unique<BlockWorldFilter>())
  , _batteryStatsAccumulator(std::make_unique<BatteryStats>())
{
  // setup battery voltage low pass filter (samples come in at kBatteryVoltsUpdatePeriod_sec)
  _batteryVoltsFilter = std::make_unique<Util::LowPassFilterSimple>(kBatteryVoltsUpdatePeriod_sec,
                                                                    kBatteryVoltsFilterTimeConstant_sec);

  // setup block world filter to find chargers:
  _chargerFilter->AddAllowedFamily(ObjectFamily::Charger);
  _chargerFilter->AddAllowedType(ObjectType::Charger_Basic);

  _lastOnChargerContactsPitchAngle.performRescaling(false);
  _lastOnChargerContactsPitchAngle = kInvalidPitchAngle;
}

void BatteryComponent::Init(Vector::Robot* robot)
{
  _robot = robot;
}

void BatteryComponent::NotifyOfRobotState(const RobotState& msg)
{
  _lastMsgTimestamp = msg.timestamp;
  const float now_sec = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();

  // Update raw voltage
  _batteryVoltsRaw = msg.batteryVoltage;
  _chargerVoltsRaw = msg.chargerVoltage;

  // Check if faking low battery
  static bool wasFakeLowBattery = false;
  if (kFakeLowBattery) {
    _batteryVoltsRaw  = kFakeLowBatteryVoltage;
    _batteryVoltsFilter->Reset();
  } else if (wasFakeLowBattery) {
    _batteryVoltsFilter->Reset();
  }
  wasFakeLowBattery = kFakeLowBattery;

  SetTemperature(msg.battTemp_C);

  // Check if battery is _really_ overheating, enough for a shutdown to be coming.
  // This is actually handled in vic-robot, but is recorded here for viz purposes.
  _battOverheated = msg.status & (uint32_t)RobotStatusFlag::IS_BATTERY_OVERHEATED;

  // Only update filtered value if the battery isn't disconnected
  bool wasDisconnected = _battDisconnected;
  _battDisconnected = (msg.status & (uint32_t)RobotStatusFlag::IS_BATTERY_DISCONNECTED)
                      || (_batteryVoltsRaw < 3);  // Just in case syscon doesn't report IS_BATTERY_DISCONNECTED for some reason.
                                                  // Anything under 3V doesn't make sense.

  // If in calm mode, RobotState messages are expected to come in at a slower rate
  // and we therefore need to adjust the sampling rate of the filter.
  static bool prevSysconCalmMode = false;
  bool currSysconCalmMode = msg.status & (uint32_t)RobotStatusFlag::CALM_POWER_MODE;
  if (currSysconCalmMode && !prevSysconCalmMode) {
    _batteryVoltsFilter->SetSamplePeriod(kCalmModeBatteryVoltsUpdatePeriod_sec);
  } else if (!currSysconCalmMode && prevSysconCalmMode) {
    _batteryVoltsFilter->SetSamplePeriod(kBatteryVoltsUpdatePeriod_sec);
  }
  prevSysconCalmMode = currSysconCalmMode;

  // If processes start while the battery is disconnected (because it's been on the charger for > 30min),
  // we make sure to set the battery voltage to a less wrong _batteryVoltsRaw.
  // Otherwise, the filtered value is only updated when the battery is connected.
  if (!_battDisconnected || NEAR_ZERO(_batteryVoltsFilt)) {
    if (_resetVoltageFilterWhenBatteryConnected) {
      DASMSG(battery_voltage_reset, "battery.voltage_reset", "Indicates that the battery voltage was reset following a change in onCharger state");  
      DASMSG_SET(i2, now_sec - _lastOnChargerContactsChange_sec, "Time since placed on charger (sec)");
      DASMSG_SET(i3, GetBatteryVoltsRaw_mV(), "New battery voltage (mV)");
      DASMSG_SET(i4, GetBatteryTemperature_C(), "Current temperature (C)");
      DASMSG_SEND();
      _batteryVoltsFilter->Reset();
      _resetVoltageFilterWhenBatteryConnected = false;
    }
    _batteryVoltsFilt = _batteryVoltsFilter->AddSample(_batteryVoltsRaw);
  }

  const bool wasCharging = IsCharging();
  const auto oldBatteryLevel = _batteryLevel;

  // Update isCharging / isOnChargerContacts / isOnChargerPlatform
  SetOnChargeContacts(msg.status & (uint32_t)RobotStatusFlag::IS_ON_CHARGER);
  SetIsCharging(msg.status & (uint32_t)RobotStatusFlag::IS_CHARGING);
  UpdateOnChargerPlatform();


  // DAS message for when battery is disconnected for cooldown
  if ((_battDisconnected != wasDisconnected) && IsCharging()) {
    const float now_sec = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
    DASMSG(battery_cooldown, "battery.cooldown", "Indicates that the battery was disconnected/reconnected in order to cool down the battery");
    DASMSG_SET(i1, _battDisconnected, "Whether we have started or stopped cooldown (1 if we have started, 0 if we have stopped)");
    DASMSG_SET(i2, now_sec - _lastOnChargerContactsChange_sec, "Time since placed on charger (sec)");
    DASMSG_SET(i3, GetBatteryVolts_mV(), "Current filtered battery voltage (mV)");
    DASMSG_SET(i4, GetBatteryTemperature_C(), "Current temperature (C)");
    DASMSG_SEND();
  }

  // Check if saturation charging
  bool isSaturationCharging = _isCharging && !_battDisconnected && _batteryVoltsFilt > kSaturationChargingThresholdVolts;
  bool isFullyCharged = false;
  bool saturationChargingStateChanged = false;
  if (isSaturationCharging) {
    if (_saturationChargingStartTime_sec <= 0.f) {
      // Saturation charging has started
      _saturationChargingStartTime_sec = now_sec;
      saturationChargingStateChanged = true;

      // The amount of time until fully charged is the (discounted) amount of time
      // that has elapsed since the last time it was saturation charging
      // plus the amount of saturation charge time that was left when it ended
      // to a max time of kMaxSaturationTime_sec.
      //
      // kSaturationTimeReplenishmentSpeed represents a heuristic that very roughly
      // approximates the amount of saturation charge time required to offset the
      // amount of time spent since it was last saturation charging (which obviously
      // depends on exactly what it was doing off charger).
      //
      // NOTE: This replenish rate of 60% seems to work fine based on a test
      //       where the wheels were run full speed right after coming fully charged
      //       off the charger and returning to charger after 7 minutes. This resulted in
      //       just over 4 minutes of saturation charge time and seemed to yield as much
      //       subsequent discharge time as a fully charged battery.
      const float kSaturationTimeReplenishmentSpeed = 0.6f;  //  1: Replenish at real-time rate
                                                             // >1: Replenish faster (i.e. Takes longer to reach full)
                                                             // <1: Replenish slower (i.e. Faster to reach full)
      float newPossibleSaturationChargeTimeRemaining_sec = _saturationChargeTimeRemaining_sec;

      // Add extra time if charging had actually stopped since the last time we were saturation charging
      if (_hasStoppedChargingSinceLastSaturationCharge) {
        newPossibleSaturationChargeTimeRemaining_sec += (kSaturationTimeReplenishmentSpeed *
                                                        (now_sec - _lastSaturationChargingEndTime_sec));
      }

      _saturationChargeTimeRemaining_sec = std::min(kMaxSaturationTime_sec, newPossibleSaturationChargeTimeRemaining_sec);
    }
    _lastSaturationChargingEndTime_sec = now_sec;

    isFullyCharged = now_sec > _saturationChargingStartTime_sec + _saturationChargeTimeRemaining_sec;

    // If transitioning to full, write DAS log
    if (isFullyCharged && !IsBatteryFull()) {
      DASMSG(battery_fully_charged_voltage, "battery.fully_charged_voltage", "Transitioning to Full battery after saturation charging");
      DASMSG_SET(i1, GetBatteryVolts_mV(), "Current filtered battery voltage (mV)");
      DASMSG_SEND();
    }

  } else if (_saturationChargingStartTime_sec > 0.f) {
    // Saturation charging has stopped so update the amount of
    // saturation charge time remaining by subtracting the amount of
    // time that has elapsed since saturation charging started
    const float newPossibleSaturationChargeTimeRemaining_sec = _saturationChargeTimeRemaining_sec -
                                                               (now_sec - _saturationChargingStartTime_sec);
    _saturationChargeTimeRemaining_sec = std::max(0.f, newPossibleSaturationChargeTimeRemaining_sec);
    _saturationChargingStartTime_sec = 0.f;
    saturationChargingStateChanged = true;

    // If saturation charging stopped because the robot moved off the contacts
    // We add more saturation time when we next saturation charge again.
    // If saturation charging stopped because the battery was disconnected due
    // to overheating, don't add any extra charging time.
    _hasStoppedChargingSinceLastSaturationCharge = !IsCharging();
  }

  // Send a DAS message if the state of saturation charging has changed
  if (saturationChargingStateChanged) {
    const bool saturationChargingStarted = _saturationChargingStartTime_sec > 0.f;
    DASMSG(battery_saturation_charging, "battery.saturation_charging", "Saturation charging has started/stopped");
    DASMSG_SET(i1, saturationChargingStarted, "Whether we have started or stopped saturation charging (1 if we have started, 0 if we have stopped)");
    DASMSG_SET(i2, _saturationChargeTimeRemaining_sec, "Saturation charging time remaining (sec)");
    DASMSG_SET(i3, GetBatteryVolts_mV(), "Current filtered battery voltage (mV)");
    DASMSG_SEND();
  }

  // Battery may sometimes disconnect to cool down an overheating
  // battery while on charger. In this situation, the IS_CHARGING bit
  // is still set. Otherwise, after 30 min of cumulative connected
  // charging time, the battery will disconnect and IS_CHARGING will
  // go low. By this time, the battery should always be full.
  // If it isn't, we may need to adjust kSaturationChargingThresholdVolts
  // or possibly the syscon cutoff time.
  // Current battery voltage should also be non-zero, otherwise it means
  // the engine started while the battery was already disconnected which
  // does not warrant a warning.
  if (_battDisconnected && !IsCharging()) {
    if (!wasDisconnected && !IsBatteryFull() && !NEAR_ZERO(_batteryVoltsFilt) ) {
      PRINT_NAMED_WARNING("BatteryComponent.NotifyOfRobotState.FullBatteryExpected", "%f", _batteryVoltsFilt);
    }

    // Force full battery state when disconnected.
    // It's not going to get anymore charged so might as well
    // pretend we're full.
    isFullyCharged = true;
  }

  // Update battery charge level
  BatteryLevel level = BatteryLevel::Nominal;
  auto lowBattThreshold = _isCharging ? kOnChargerLowBatteryThresholdVolts : kLowBatteryThresholdVolts;
  // Add a small hysteresis band if we are currently low battery, so that we do not flicker between
  // Low and Nominal if the voltage estimate is noisy
  if (oldBatteryLevel == BatteryLevel::Low) {
    lowBattThreshold += kLowBatteryHysteresisVolts;
  }
  if (isFullyCharged) {
    // NOTE: Given the dependence on isFullyCharged, this means BatteryLevel::Full is a state
    //       that can only be achieved while on charger
    level = BatteryLevel::Full;
  } else if (_batteryVoltsFilt < lowBattThreshold) {
    level = BatteryLevel::Low;
  }

  if (level != _batteryLevel) {
    PRINT_NAMED_INFO("BatteryComponent.BatteryLevelChanged",
                     "New battery level %s (previously %s for %f seconds)",
                     BatteryLevelToString(level),
                     BatteryLevelToString(_batteryLevel),
                     now_sec - _lastBatteryLevelChange_sec);

    DASMSG(battery_level_changed, "battery.battery_level_changed", "The battery level has changed");
    DASMSG_SET(s1, BatteryLevelToString(level), "New battery level");
    DASMSG_SET(s2, BatteryLevelToString(_batteryLevel), "Previous battery level");
    DASMSG_SET(i1, IsCharging(), "Is the battery currently charging? 1 if charging, 0 if not");
    DASMSG_SET(i2, now_sec - _lastBatteryLevelChange_sec, "Time spent at previous battery level (sec)");
    DASMSG_SET(i3, GetBatteryVolts_mV(), "Current filtered battery voltage (mV)");
    DASMSG_SET(i4, _battDisconnected, "Battery is (1) disconnected or (0) connected");
    DASMSG_SEND();

    _lastBatteryLevelChange_sec = now_sec;
    _batteryLevel = level;
  }

  static RobotInterface::BatteryStatus prevStatus;
  RobotInterface::BatteryStatus curStatus;
  curStatus.isLow             = IsBatteryLow();
  curStatus.isCharging        = IsCharging();
  curStatus.onChargerContacts = IsOnChargerContacts();
  curStatus.isBatteryFull     = IsBatteryFull();

  if(curStatus != prevStatus)
  {
    prevStatus = curStatus;
    _robot->SendMessage(RobotInterface::EngineToRobot(std::move(curStatus)));
  }

  const bool wasLowBattery = (oldBatteryLevel == BatteryLevel::Low);
  UpdateSuggestedChargerTime(wasLowBattery, wasCharging);

  _batteryStatsAccumulator->Update(_battTemperature_C, _batteryVoltsFilt);
}


float BatteryComponent::GetFullyChargedTimeSec() const
{
  float timeSinceFullyCharged_sec = 0.f;
  if (_batteryLevel == BatteryLevel::Full) {
    const float now_sec = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
    timeSinceFullyCharged_sec = now_sec - _lastBatteryLevelChange_sec;
  }
  return timeSinceFullyCharged_sec;
}


float BatteryComponent::GetLowBatteryTimeSec() const
{
  float timeSinceLowBattery_sec = 0.f;
  if (_batteryLevel == BatteryLevel::Low) {
    const float now_sec = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
    timeSinceLowBattery_sec = now_sec - _lastBatteryLevelChange_sec;
  }
  return timeSinceLowBattery_sec;
}


void BatteryComponent::SetOnChargeContacts(const bool onChargeContacts)
{

  // If we are being set on a charger, we can update the instance of the charger in the current world to
  // match the robot. If we don't have an instance, we can add an instance now
  if (onChargeContacts)
  {
    const Pose3d& poseWrtRobot = Charger::GetDockPoseRelativeToRobot(*_robot);

    // find instance in current origin
    ObservableObject* chargerInstance = _robot->GetBlockWorld().FindLocatedMatchingObject(*_chargerFilter);
    if (nullptr == chargerInstance)
    {
      // there's currently no located instance, we need to create one.
      chargerInstance = new Charger();
      chargerInstance->SetID();
    }

    // pretend the instance we created was an observation
    chargerInstance->SetLastObservedTime((TimeStamp_t)_robot->GetLastMsgTimestamp());
    _robot->GetObjectPoseConfirmer().AddRobotRelativeObservation(chargerInstance, poseWrtRobot, PoseState::Known);

    // Update the last OnChargeContacts pitch angle
    if (!_robot->GetMoveComponent().IsMoving()) {
      _lastOnChargerContactsPitchAngle = _robot->GetPitchAngle();
    }
  }

  // Log events and send message if state changed
  if (onChargeContacts != _isOnChargerContacts) {
    _isOnChargerContacts = onChargeContacts;

    // The voltage usually steps up or down by a few hundred millivolts when we start/stop charging, so reset the low
    // pass filter here to more closely track the actual battery voltage, but only if the battery isn't disconnected
    // otherwise the measured voltage doesn't reflect the actual battery voltage. We also delay the update by at least
    // one RobotState message delay to allow the voltage value to settle, but it will take longer if the battery is
    // disconnected (because it's too hot) since we don't want to reset the filter to a measurement taken while disconnected.
    _resetVoltageFilterWhenBatteryConnected = true;

    const float now_sec = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
    PRINT_NAMED_INFO(onChargeContacts ? "robot.on_charger" : "robot.off_charger", "");
    // Broadcast to game
    using namespace ExternalInterface;
    _robot->Broadcast(MessageEngineToGame(ChargerEvent(onChargeContacts)));
    // Broadcast to DAS
    DASMSG(battery_on_charger_changed, "battery.on_charger_changed", "The robot onChargerContacts state has changed");
    DASMSG_SET(i1, onChargeContacts, "On or off charge contacts (1 if on contacts, 0 if not)");
    DASMSG_SET(i2, now_sec - _lastOnChargerContactsChange_sec, "Time since previous change (sec)");
    DASMSG_SET(i3, GetBatteryVolts_mV(), "Current filtered battery voltage (mV)");
    DASMSG_SET(i4, _battDisconnected, "Battery is (1) disconnected or (0) connected");
    DASMSG_SEND();
    _lastOnChargerContactsChange_sec = now_sec;
  }

  OSState::getInstance()->SetOnChargeContacts(onChargeContacts);
}


void BatteryComponent::SetIsCharging(const bool isCharging)
{
  if (isCharging != _isCharging) {
    _isCharging = isCharging;

    DASMSG(battery_is_charging_changed, "battery.is_charging_changed", "The robot isCharging state has changed");
    DASMSG_SET(i1, IsCharging(), "Is charging (1) or not (0)");
    DASMSG_SET(i2, _battTemperature_C,  "Battery temperature (C)");
    DASMSG_SET(i3, GetBatteryVolts_mV(), "Current filtered battery voltage (mV)");
    DASMSG_SET(i4, _battDisconnected, "Battery is (1) disconnected or (0) connected");
    DASMSG_SEND();
  }
}

void BatteryComponent::SetTemperature(const u8 temp_C)
{
  _battTemperature_C = temp_C;

  // Print DAS if temperature crosses 50C
  // Mostly for dev to see if conditionHighTemperature is making him too narcoleptic
  const u8 kHotBatteryTemp_degC = 50;
  const u8 kNoLongerHotTemp_degC = 45; // Still hot, but using hysteresis to prevent spamming
  static bool exceededHotThreshold = false;

  auto dasFunc = [kHotBatteryTemp_degC,kNoLongerHotTemp_degC](const bool exceeded) {
    exceededHotThreshold = exceeded;
    DASMSG(battery_temp_crossed_threshold, "battery.temp_crossed_threshold", "Indicates battery temperature exceeded a specified temperature");
    DASMSG_SET(i1, exceeded, "Higher than threshold (1) or lower (0)");
    DASMSG_SET(i2, exceeded ? kHotBatteryTemp_degC : kNoLongerHotTemp_degC, "Temperature crossed (C)");
    DASMSG_SEND();
  };

  if (!exceededHotThreshold && _battTemperature_C >= kHotBatteryTemp_degC) {
    dasFunc(true);
  } else if (exceededHotThreshold && _battTemperature_C <= kNoLongerHotTemp_degC) {
    dasFunc(false);
  }
}


void BatteryComponent::UpdateOnChargerPlatform()
{
  bool onPlatform = _isOnChargerPlatform;

  if (IsOnChargerContacts()) {
    // If we're on the charger _contacts_, we are definitely on the charger _platform_
    onPlatform = true;
  } else if (onPlatform) {
    // Not on the charger contacts, but we still think we're on the charger platform.
    // Make a reasonable conjecture about our current OnChargerPlatform state.

    // Not on charger platform if we're off treads
    if (_robot->GetOffTreadsState() != OffTreadsState::OnTreads) {
      onPlatform = false;
    }

    // Check intersection between robot and charger bounding quads
    auto* charger = _robot->GetBlockWorld().FindLocatedObjectClosestTo(_robot->GetPose(),
                                                                       *_chargerFilter);
    if (charger == nullptr ||
        !charger->GetBoundingQuadXY().Intersects(_robot->GetBoundingQuadXY())) {
      onPlatform = false;
    }

    // Check to see if the robot's pitch angle indicates that it has
    // been moved from the charger platform onto the ground.
    const bool lastPitchAngleValid = (_lastOnChargerContactsPitchAngle.ToFloat() != kInvalidPitchAngle);
    const bool robotMoving = _robot->GetMoveComponent().IsMoving();
    if (lastPitchAngleValid && !robotMoving) {
      const auto& expectedPitchAngleOffPlatform = _lastOnChargerContactsPitchAngle + kChargerSlopeAngle_rad;
      if (_robot->GetPitchAngle() > expectedPitchAngleOffPlatform - DEG_TO_RAD(2.f)) {
        // Pitch angle change indicates that we've probably moved from the
        // charger platform onto the table. Update the robot's pose accordingly.
        onPlatform = false;
        if (charger != nullptr) {
          _robot->SetCharger(charger->GetID());
          _robot->SetPosePostRollOffCharger();
        }
      }
    }
  }

  // Has OnChargerPlatform state changed?
  if (onPlatform != _isOnChargerPlatform) {
    _isOnChargerPlatform = onPlatform;

    using namespace ExternalInterface;
    _robot->Broadcast(MessageEngineToGame(RobotOnChargerPlatformEvent(_isOnChargerPlatform)));

    PRINT_NAMED_INFO("BatteryComponent.UpdateOnChargerPlatform.OnChargerPlatformChange",
                     "robot is now %s the charger platform",
                     _isOnChargerPlatform ? "ON" : "OFF");

    // Reset last on charger pitch angle if we're no longer on the platform
    if (!onPlatform) {
      _lastOnChargerContactsPitchAngle = kInvalidPitchAngle;
    }
  }
}

external_interface::GatewayWrapper BatteryComponent::GetBatteryState(const external_interface::BatteryStateRequest& request)
{
  auto* cubeBatteryMsg = _robot->GetCubeBatteryComponent().GetCubeBatteryMsg().release();
  auto* response = new external_interface::BatteryStateResponse {
    nullptr,
    (external_interface::BatteryLevel) GetBatteryLevel(),
    GetBatteryVolts(),
    IsCharging(),
    IsOnChargerPlatform(),
    GetSuggestedChargerTime(),
    cubeBatteryMsg
  };
  external_interface::GatewayWrapper wrapper;
  wrapper.set_allocated_battery_state_response(response);
  return wrapper;
}

void BatteryComponent::UpdateSuggestedChargerTime(bool wasLowBattery, bool wasCharging)
{
  const float currTime_s = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
  const bool isLowBattery = (_batteryLevel == BatteryLevel::Low);

  if( isLowBattery && !wasLowBattery && (_timeRemovedFromCharger_s == 0.0f) ) {
    // Just became low battery. reset end time so the countdown starts below
    _suggestedChargeEndTime_s = 0.0f;
  }

  bool countdownStarted = (_suggestedChargeEndTime_s != 0.0f);

  if( countdownStarted && (currTime_s >= _suggestedChargeEndTime_s) ) {
    // countdown finished
    if( isLowBattery ) {
      // this is ok since the logic below will just kick in again, but it means we should change the time
      // params
      PRINT_NAMED_WARNING( "BatteryComponent.UpdateSuggestedChargerTime.NotCharged",
                           "Charge parameters did not fully charge the robot!" );
    }
    _suggestedChargeEndTime_s = 0.0f;
    _timeRemovedFromCharger_s = 0.0f;
    countdownStarted = false;
  }

  if( IsCharging() ) {
    // currently charging. Don't check for starting to charge (IsCharging() && !wasCharging) here in case
    // the countdown just finished, above, and the times were reset to 0, but wasCharging is still true

    if( isLowBattery && !countdownStarted ) {
      // was never on charger before. start the low battery countdown! it will continue even after the robot
      // is no longer low battery
      _suggestedChargeEndTime_s = currTime_s + kRequiredChargeTime_s;
    } else if( countdownStarted && !wasCharging ){
      // The robot was just placed on the charger, and not for the first time, so there should be a
      // _timeRemovedFromCharger_s. Note that it may not be low battery at this point, but there's
      // still a countdown since countdownStarted
      ANKI_VERIFY( _timeRemovedFromCharger_s != 0.0f, "BatteryComponent.UpdateSuggestedChargerTime.UnexpectedOffChargerTime",
                   "Off charger time was 0, expecting positive" );
      const float elapsedOffChargerTime_s = currTime_s - _timeRemovedFromCharger_s;
      _suggestedChargeEndTime_s += elapsedOffChargerTime_s * (1.0f + kExtraChargingTimePerDischargePeriod_s);
      _suggestedChargeEndTime_s = Anki::Util::Min( _suggestedChargeEndTime_s, currTime_s + kRequiredChargeTime_s);
    }
  } else if( !IsCharging() && wasCharging ) {
    _timeRemovedFromCharger_s = currTime_s;
  }
}

float BatteryComponent::GetSuggestedChargerTime() const
{
  const float currTime_s = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
  if( _suggestedChargeEndTime_s == 0.0f ) {
    if( IsBatteryLow() ) {
      // charging hasn't started yet
      return kRequiredChargeTime_s;
    } else {
      return 0.0f;
    }
  } else {
    if( !IsCharging()) {
      // currently off the charger. keep timer fixed
      const float fixedTime = (_suggestedChargeEndTime_s - _timeRemovedFromCharger_s);
      return Anki::Util::Clamp(fixedTime, 0.0f, kRequiredChargeTime_s);
    } else {
      return Anki::Util::Max( _suggestedChargeEndTime_s - currTime_s, 0.0f );
    }
  }
}


} // Vector namespace
} // Anki namespace

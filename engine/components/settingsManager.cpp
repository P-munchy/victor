/**
* File: settingsManager.cpp
*
* Author: Paul Terry
* Created: 6/8/18
*
* Description: Stores robot settings on robot; accepts and sets new settings
*
* Copyright: Anki, Inc. 2018
*
**/


#include "engine/components/settingsManager.h"

#include "engine/components/jdocsManager.h"
#include "engine/components/settingsCommManager.h"
#include "engine/robot.h"
#include "engine/robotDataLoader.h"
#include "engine/robotInterface/messageHandler.h"

#include "coretech/common/engine/utils/timer.h"
#include "util/console/consoleInterface.h"

#include "clad/robotInterface/messageEngineToRobot.h"

#include <sys/wait.h>

#define LOG_CHANNEL "SettingsManager"

namespace Anki {
namespace Vector {

namespace {
  static const char* kConfigDefaultValueKey = "defaultValue";
  //static const char* kConfigupdateCloudOnChangeKey = "updateCloudOnChange";
  const size_t kMaxTicksToClear = 3;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
SettingsManager::SettingsManager()
: IDependencyManagedComponent(this, RobotComponentID::SettingsManager)
, _hasPendingSettingsRequest( false )
{
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void SettingsManager::InitDependent(Robot* robot, const RobotCompMap& dependentComponents)
{
  _robot = robot;
  _jdocsManager = &robot->GetComponent<JdocsManager>();
  _audioClient = robot->GetAudioClient();

  _settingsConfig = &robot->GetContext()->GetDataLoader()->GetSettingsConfig();

  // let's register for callbacks we care about
  {
    using namespace RobotInterface;
    MessageHandler* messageHandler = robot->GetRobotMessageHandler();

    // eye color settings are triggered via the AnimEvent::CHANGE_EYE_COLOR animation event
    // listen for this event, and if we have a pending request for an eye_color change, we can trigger it
    // this allows us to trigger the request then play the animation in whatever way we'd like (vs. forcing other systems
    // to listen for the callback explicitly)
    AddSignalHandle(messageHandler->Subscribe(RobotToEngineTag::animEvent, [this]( const AnkiEvent<RobotToEngine>& event)
    {
      const AnimationEvent& animEvent = event.GetData().Get_animEvent();
      if (animEvent.event_id == AnimEvent::CHANGE_EYE_COLOR)
      {
        if (IsSettingsUpdateRequestPending(RobotSetting::eye_color))
        {
          ApplyPendingSettingsUpdate(RobotSetting::eye_color, false);
        }
      }
    }));
  }

  _platform = robot->GetContextDataPlatform();
  DEV_ASSERT(_platform != nullptr, "SettingsManager.InitDependent.DataPlatformIsNull");

  // Call the JdocsManager to see if our robot settings jdoc file exists
  bool settingsDirty = false;
  _currentSettings.clear();
  if (_jdocsManager->JdocNeedsCreation(external_interface::JdocType::ROBOT_SETTINGS))
  {
    LOG_INFO("SettingsManager.InitDependent.NoSettingsJdocFile",
             "Settings jdoc file not found; one will be created shortly");
    settingsDirty = true;
  }
  else
  {
    _currentSettings = _jdocsManager->GetJdocBody(external_interface::JdocType::ROBOT_SETTINGS);

    // Temporary migration code:  Since we're now saving proto enums as numbers, not strings
    // If the enum setting is a string, reset its value to [the default] number (below)
    std::string key = RobotSettingToString(RobotSetting::eye_color);
    if (_currentSettings.isMember(key) && (_currentSettings[key].isString()))
    {
      _currentSettings.removeMember(key);
    }
    key = RobotSettingToString(RobotSetting::master_volume);
    if (_currentSettings.isMember(key) && (_currentSettings[key].isString()))
    {
      _currentSettings.removeMember(key);
    }
  }

  // Ensure current settings has each of the defined settings;
  // if not, initialize each missing setting to default value
  for (Json::ValueConstIterator it = _settingsConfig->begin(); it != _settingsConfig->end(); ++it)
  {
    if (!_currentSettings.isMember(it.name()))
    {
      const Json::Value& item = (*it);
      _currentSettings[it.name()] = item[kConfigDefaultValueKey];
      settingsDirty = true;
      LOG_INFO("SettingsManager.InitDependent.AddDefaultItem", "Adding setting with key %s and default value %s",
               it.name().c_str(), item[kConfigDefaultValueKey].asString().c_str());
    }
  }

  // Now look through current settings, and remove any item
  // that is no longer defined in the config
  std::vector<std::string> keysToRemove;
  for (Json::ValueConstIterator it = _currentSettings.begin(); it != _currentSettings.end(); ++it)
  {
    if (!_settingsConfig->isMember(it.name()))
    {
      keysToRemove.push_back(it.name());
    }
  }
  for (const auto& key : keysToRemove)
  {
    LOG_INFO("SettingsManager.InitDependent.RemoveItem",
             "Removing setting with key %s", key.c_str());
    _currentSettings.removeMember(key);
    settingsDirty = true;
  }

  if (settingsDirty)
  {
    UpdateSettingsJdoc();
  }

  // Register the actual setting application methods, for those settings that want to execute code when changed:
  _settingSetters[RobotSetting::master_volume] = { false, &SettingsManager::ValidateSettingMasterVolume, &SettingsManager::ApplySettingMasterVolume };
  _settingSetters[RobotSetting::eye_color]     = { true,  &SettingsManager::ValidateSettingEyeColor,     &SettingsManager::ApplySettingEyeColor     };
  _settingSetters[RobotSetting::locale]        = { false, nullptr,                                       &SettingsManager::ApplySettingLocale       };
  _settingSetters[RobotSetting::time_zone]     = { false, nullptr,                                       &SettingsManager::ApplySettingTimeZone     };

  // Finally, set a flag so we will apply all of the settings
  // we just loaded and/or set, in the first update
  _applySettingsNextTick = true;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void SettingsManager::UpdateDependent(const RobotCompMap& dependentComps)
{
  if (_applySettingsNextTick)
  {
    _applySettingsNextTick = false;
    ApplyAllCurrentSettings();

    auto& settingsCommManager = _robot->GetComponent<SettingsCommManager>();
    settingsCommManager.RefreshConsoleVars();
  }

  if (_hasPendingSettingsRequest && !_settingsUpdateRequest.isClaimed)
  {
    const size_t currTick = BaseStationTimer::getInstance()->GetTickCount();
    const size_t dt = currTick - _settingsUpdateRequest.tickRequested;
    if (dt >= kMaxTicksToClear)
    {
      LOG_INFO("SettingsManager.UpdateDependent",
               "Setting update request '%s' has been pending for %zu ticks, forcing a clear",
               RobotSettingToString(_settingsUpdateRequest.setting),
               dt);

      OnSettingsUpdateNotClaimed(_settingsUpdateRequest.setting);
      ClearPendingSettingsUpdate();
    }
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool SettingsManager::SetRobotSetting(const RobotSetting robotSetting,
                                      const Json::Value& valueJson,
                                      const bool updateSettingsJdoc)
{
  const std::string key = RobotSettingToString(robotSetting);
  if (!_currentSettings.isMember(key))
  {
    LOG_ERROR("SettingsManager.SetRobotSetting.InvalidKey", "Invalid key %s; ignoring", key.c_str());
    return false;
  }

  const Json::Value prevValue = _currentSettings[key];
  _currentSettings[key] = valueJson;

  bool success = ApplyRobotSetting(robotSetting, false);
  if (!success)
  {
    _currentSettings[key] = prevValue;  // Restore previous good value
    return false;
  }

  if (updateSettingsJdoc)
  {
    success = UpdateSettingsJdoc();
  }

  return success;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
std::string SettingsManager::GetRobotSettingAsString(const RobotSetting key) const
{
  const std::string& keyString = EnumToString(key);
  if (!_currentSettings.isMember(keyString))
  {
    LOG_ERROR("SettingsManager.GetRobotSettingAsString.InvalidKey", "Invalid key %s", keyString.c_str());
    return "Invalid";
  }

  return _currentSettings[keyString].asString();
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool SettingsManager::GetRobotSettingAsBool(const RobotSetting key) const
{
  const std::string& keyString = EnumToString(key);
  if (!_currentSettings.isMember(keyString))
  {
    LOG_ERROR("SettingsManager.GetRobotSettingAsBool.InvalidKey", "Invalid key %s", keyString.c_str());
    return false;
  }

  return _currentSettings[keyString].asBool();
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uint32_t SettingsManager::GetRobotSettingAsUInt(const RobotSetting key) const
{
  const std::string& keyString = EnumToString(key);
  if (!_currentSettings.isMember(keyString))
  {
    LOG_ERROR("SettingsManager.GetRobotSettingAsUInt.InvalidKey", "Invalid key %s", keyString.c_str());
    return false;
  }
  
  return _currentSettings[keyString].asUInt();
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool SettingsManager::UpdateSettingsJdoc()
{
  static const bool saveToDiskImmediately = true;
  const bool success = _jdocsManager->UpdateJdoc(external_interface::JdocType::ROBOT_SETTINGS,
                                                 &_currentSettings, saveToDiskImmediately);
  return success;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void SettingsManager::ApplyAllCurrentSettings()
{
  for (Json::ValueConstIterator it = _currentSettings.begin(); it != _currentSettings.end(); ++it)
  {
    ApplyRobotSetting(RobotSettingFromString(it.name()));
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool SettingsManager::ApplyRobotSetting(const RobotSetting robotSetting, bool force)
{
  // Actually apply the setting; note that some things don't need to be "applied"
  bool success = true;
  const auto it = _settingSetters.find(robotSetting);
  if (it != _settingSetters.end())
  {
    const auto validationFunc = it->second.validationFunction;
    if (validationFunc != nullptr)
    {
      success = (this->*(validationFunc))();
      if (!success)
      {
        LOG_ERROR("SettingsManager.ApplyRobotSetting.ValidateFunctionFailed", "Error attempting to apply %s setting",
                  RobotSettingToString(robotSetting));
        return false;
      }
    }

    if (force || !it->second.isLatentApplication)
    {
      LOG_DEBUG("SettingsManager.ApplyRobotSetting", "Applying Robot Setting '%s'", RobotSettingToString(robotSetting));
      success = (this->*(it->second.applicationFunction))();
    }
    else
    {
      success = RequestLatentSettingsUpdate(robotSetting);
    }

    if (!success)
    {
      LOG_ERROR("SettingsManager.ApplyRobotSetting.ApplyFunctionFailed", "Error attempting to apply %s setting",
                RobotSettingToString(robotSetting));
    }
  }
  return success;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool SettingsManager::ValidateSettingMasterVolume()
{
  static const std::string& key = RobotSettingToString(RobotSetting::master_volume);
  const auto& value = _currentSettings[key].asUInt();
  if (!external_interface::Volume_IsValid(value))
  {
    LOG_ERROR("SettingsManager.ApplySettingMasterVolume.Invalid", "Invalid master volume value %i", value);
    return false;
  }
  return true;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool SettingsManager::ApplySettingMasterVolume()
{
  static const std::string& key = RobotSettingToString(RobotSetting::master_volume);
  const auto& value = _currentSettings[key].asUInt();
  const auto& volumeName = external_interface::Volume_Name(static_cast<external_interface::Volume>(value));
  LOG_INFO("SettingsManager.ApplySettingMasterVolume.Apply", "Setting robot master volume to %s", volumeName.c_str());

  const auto volume = static_cast<MasterVolume>(value); // Cast to CLAD enum (to be cleaned up later)
  _robot->GetAudioClient()->SetRobotMasterVolume(volume);

  return true;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool SettingsManager::ValidateSettingEyeColor()
{
  static const std::string& key = RobotSettingToString(RobotSetting::eye_color);
  const auto& value = _currentSettings[key].asUInt();
  if (!external_interface::EyeColor_IsValid(value))
  {
    LOG_ERROR("SettingsManager.ApplySettingEyeColor.Invalid", "Invalid eye color value %i", value);
    return false;
  }

  return true;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool SettingsManager::ApplySettingEyeColor()
{
  static const std::string& key = RobotSettingToString(RobotSetting::eye_color);
  const auto& value = _currentSettings[key].asUInt();
  const auto& eyeColorName = external_interface::EyeColor_Name(static_cast<external_interface::EyeColor>(value));
  LOG_INFO("SettingsManager.ApplySettingEyeColor.Apply", "Setting robot eye color to %s", eyeColorName.c_str());

  const auto& config = _robot->GetContext()->GetDataLoader()->GetEyeColorConfig();
  const auto& eyeColorData = config[eyeColorName];
  const float hue = eyeColorData["Hue"].asFloat();
  const float saturation = eyeColorData["Saturation"].asFloat();

  _robot->SendRobotMessage<RobotInterface::SetFaceHue>(hue);
  _robot->SendRobotMessage<RobotInterface::SetFaceSaturation>(saturation);

  return true;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool SettingsManager::ApplySettingLocale()
{
  static const std::string& key = RobotSettingToString(RobotSetting::locale);
  const std::string& value = _currentSettings[key].asString();
  DEV_ASSERT(_robot != nullptr, "SettingsManager.ApplySettingLocale.InvalidRobot");
  const bool success = _robot->SetLocale(value);
  if (!success)
  {
    LOG_ERROR("SettingsManager.ApplySettingLocale", "Error setting locale to %s", value.c_str());
  }
  return success;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool SettingsManager::ApplySettingTimeZone()
{
#if defined(ANKI_PLATFORM_VICOS)
  static const std::string key = RobotSettingToString(RobotSetting::time_zone);
  const std::string value = _currentSettings[key].asString();

  std::vector<std::string> command;
  command.push_back("/usr/bin/timedatectl");
  command.push_back("set-timezone");
  command.push_back(value);
  const bool success = ExecCommand(command);
  if (!success)
  {
    LOG_ERROR("SettingsManager.ApplySettingTimeZone.TimeZoneError",
              "Error setting time zone to %s ", value.c_str());
  }
  return success;
#else
  LOG_WARNING("SettingsManager.ApplySettingTimeZone.NotInWebots",
              "Applying time zone setting is not supported in webots");
  return true;
#endif
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool SettingsManager::RequestLatentSettingsUpdate(RobotSetting setting)
{
  bool success = false;

  // if we don't already have a request pending, or
  // if our current pending request hasn't been claimed yet, simply override it
  if (!IsSettingsUpdateRequestPending() || !_settingsUpdateRequest.isClaimed)
  {
    LOG_DEBUG("SettingsManager.RequestLatentSettingsUpdate", "Requesting update to setting '%s'", RobotSettingToString(setting));

    // create new request
    _settingsUpdateRequest =
    {
      .setting        = setting,
      .tickRequested  = BaseStationTimer::getInstance()->GetTickCount(),
      .isClaimed      = false
    };

    _hasPendingSettingsRequest = success = true;
  }
  else
  {
    LOG_ERROR("SettingsManager.RequestLatentSettingsUpdate",
              "Requesting to change setting '%s' while previous claimed request '%s' was pending",
              RobotSettingToString(setting), RobotSettingToString(_settingsUpdateRequest.setting));
  }

  return success;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool SettingsManager::IsSettingsUpdateRequestPending() const
{
  return _hasPendingSettingsRequest;
}

bool SettingsManager::IsSettingsUpdateRequestPending(RobotSetting setting) const
{
  return IsSettingsUpdateRequestPending() && (setting == _settingsUpdateRequest.setting);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
RobotSetting SettingsManager::GetPendingSettingsUpdate() const
{
  return _settingsUpdateRequest.setting;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void SettingsManager::ClearPendingSettingsUpdate()
{
  _hasPendingSettingsRequest = false;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool SettingsManager::ClaimPendingSettingsUpdate(RobotSetting setting)
{
  bool success = false;

  if (IsSettingsUpdateRequestPending(setting))
  {
    if (!_settingsUpdateRequest.isClaimed)
    {
      _settingsUpdateRequest.isClaimed = success = true;
    }
    else
    {
      LOG_ERROR("SettingsManager.ClaimPendingSettingsUpdate",
                "Attempted to consume setting '%s', but setting was previously consumed",
                RobotSettingToString(setting));
    }
  }
  else
  {
    LOG_ERROR("SettingsManager.ClaimPendingSettingsUpdate",
              "Attempted to consume setting '%s', but setting was not pending",
              RobotSettingToString(setting));
  }

  return success;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool SettingsManager::ApplyPendingSettingsUpdate(RobotSetting setting, bool clearRequest)
{
  bool success = false;

  if (IsSettingsUpdateRequestPending(setting))
  {
    success = ApplyRobotSetting(setting, true);

    // if we were told to clear the request, or it has never been claimed, go ahead and clear it
    if (clearRequest || !_settingsUpdateRequest.isClaimed)
    {
      ClearPendingSettingsUpdate();
    }
  }
  else
  {
    LOG_DEBUG("SettingsManager.ApplyPendingSettingsUpdate",
              "Attempted to apply latent setting '%s', but setting was not pending",
              RobotSettingToString(setting));
  }

  return success;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void SettingsManager::OnSettingsUpdateNotClaimed(RobotSetting setting)
{
  ApplyRobotSetting(setting, true);
}


#if defined(ANKI_PLATFORM_VICOS)
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool SettingsManager::ExecCommand(const std::vector<std::string>& args)
{
  LOG_INFO("SettingsManager.ExecCommand", "Called with cmd: %s (and %i arguments)",
           args[0].c_str(), (int)(args.size() - 1));

  const pid_t pID = fork();
  if (pID == 0) // child
  {
    char* argv_child[args.size() + 1];

    for (size_t i = 0; i < args.size(); i++)
    {
      argv_child[i] = (char *) malloc(args[i].size() + 1);
      strcpy(argv_child[i], args[i].c_str());
    }
    argv_child[args.size()] = nullptr;

    execv(argv_child[0], argv_child);

    // We'll only get here if execv fails
    for (size_t i = 0 ; i < args.size() + 1 ; ++i)
    {
      free(argv_child[i]);
    }
    exit(0);
  }
  else if (pID < 0) // fail
  {
    LOG_INFO("SettingsManager.ExecCommand.FailedFork", "Failed fork!");
    return false;
  }
  else  // parent
  {
    // Wait for child to complete so we can get an error code
    int status;
    pid_t w = TEMP_FAILURE_RETRY(waitpid(pID, &status, 0));
    if (w == -1)
    {
      return false;
    }
    if (!WIFEXITED(status))
    {
      return false;
    }
    LOG_INFO("SettingsManager.ExecCommand", "Status of forked child process is %i", status);
    return (WEXITSTATUS(status) == 0);
  }
  return true;
}
#endif


} // namespace Vector
} // namespace Anki

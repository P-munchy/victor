/**
 * File: needsManager
 *
 * Author: Paul Terry
 * Created: 04/12/2017
 *
 * Description: Manages the Needs for a Cozmo Robot
 *
 * Copyright: Anki, Inc. 2017
 *
 **/


#include "anki/common/basestation/utils/data/dataPlatform.h"
#include "anki/common/basestation/utils/timer.h"
#include "anki/common/types.h"
#include "anki/cozmo/basestation/ankiEventUtil.h"
#include "anki/cozmo/basestation/components/desiredFaceDistortionComponent.h"
#include "anki/cozmo/basestation/components/inventoryComponent.h"
#include "anki/cozmo/basestation/components/nvStorageComponent.h"
#include "anki/cozmo/basestation/components/progressionUnlockComponent.h"
#include "anki/cozmo/basestation/cozmoContext.h"
#include "anki/cozmo/basestation/events/ankiEvent.h"
#include "anki/cozmo/basestation/externalInterface/externalInterface.h"
#include "anki/cozmo/basestation/needsSystem/needsConfig.h"
#include "anki/cozmo/basestation/needsSystem/needsManager.h"
#include "anki/cozmo/basestation/robot.h"
#include "anki/cozmo/basestation/robotDataLoader.h"
#include "anki/cozmo/basestation/robotInterface/messageHandler.h"
#include "anki/cozmo/basestation/robotManager.h"
#include "anki/cozmo/basestation/utils/cozmoFeatureGate.h"
#include "anki/cozmo/basestation/viz/vizManager.h"
#include "clad/externalInterface/messageEngineToGame.h"
#include "clad/externalInterface/messageGameToEngine.h"
#include "util/console/consoleInterface.h"
#include "util/cpuProfiler/cpuProfiler.h"
#include "util/fileUtils/fileUtils.h"
#include "util/logging/logging.h"


namespace Anki {
namespace Cozmo {

const char* NeedsManager::kLogChannelName = "NeedsSystem";


static const std::string kNeedsStateFile = "needsState.json";

static const std::string kStateFileVersionKey = "_StateFileVersion";
static const std::string kDateTimeKey = "_DateTime";
static const std::string kSerialNumberKey = "_SerialNumber";

static const std::string kCurNeedLevelKey = "CurNeedLevel";
static const std::string kPartIsDamagedKey = "PartIsDamaged";
static const std::string kCurNeedsUnlockLevelKey = "CurNeedsUnlockLevel";
static const std::string kNumStarsAwardedKey = "NumStarsAwarded";
static const std::string kNumStarsForNextUnlockKey = "NumStarsForNextUnlock";
static const std::string kTimeLastStarAwardedKey = "TimeLastStarAwarded";

static const std::string kFreeplaySparksRewardStringKey = "needs.FreeplaySparksReward";

static const float kNeedLevelStorageMultiplier = 100000.0f;

static const int kMinimumTimeBetweenDeviceSaves_sec = 60;
static const int kMinimumTimeBetweenRobotSaves_sec = (60 * 10);  // Less frequently than device saves


using namespace std::chrono;
using Time = time_point<system_clock>;


#if ANKI_DEV_CHEATS
namespace {
  NeedsManager* g_DebugNeedsManager = nullptr;
  void DebugFillNeedMeters( ConsoleFunctionContextRef context )
  {
    if( g_DebugNeedsManager != nullptr)
    {
      g_DebugNeedsManager->DebugFillNeedMeters();
    }
  }
  // give stars, push back day
  void DebugGiveStar( ConsoleFunctionContextRef context )
  {
    if( g_DebugNeedsManager != nullptr)
    {
      g_DebugNeedsManager->DebugGiveStar();
    }
  }
  void DebugCompleteDay( ConsoleFunctionContextRef context )
  {
    if( g_DebugNeedsManager != nullptr)
    {
      g_DebugNeedsManager->DebugCompleteDay();
    }
  }
  void DebugResetNeeds( ConsoleFunctionContextRef context )
  {
    if( g_DebugNeedsManager != nullptr)
    {
      g_DebugNeedsManager->DebugResetNeeds();
    }
  }
  void DebugCompleteAction( ConsoleFunctionContextRef context )
  {
    if( g_DebugNeedsManager != nullptr)
    {
      const char* actionName = ConsoleArg_Get_String(context, "actionName");
      g_DebugNeedsManager->DebugCompleteAction(actionName);
    }
  }
  void DebugPredictActionResult( ConsoleFunctionContextRef context )
  {
    if( g_DebugNeedsManager != nullptr)
    {
      const char* actionName = ConsoleArg_Get_String(context, "actionName");
      g_DebugNeedsManager->DebugPredictActionResult(actionName);
    }
  }
  void DebugPauseDecayForNeed( ConsoleFunctionContextRef context )
  {
    if( g_DebugNeedsManager != nullptr)
    {
      const char* needName = ConsoleArg_Get_String(context, "needName");
      g_DebugNeedsManager->DebugPauseDecayForNeed(needName);
    }
  }
  void DebugPauseActionsForNeed( ConsoleFunctionContextRef context )
  {
    if( g_DebugNeedsManager != nullptr)
    {
      const char* needName = ConsoleArg_Get_String(context, "needName");
      g_DebugNeedsManager->DebugPauseActionsForNeed(needName);
    }
  }
  void DebugUnpauseDecayForNeed( ConsoleFunctionContextRef context )
  {
    if( g_DebugNeedsManager != nullptr)
    {
      const char* needName = ConsoleArg_Get_String(context, "needName");
      g_DebugNeedsManager->DebugUnpauseDecayForNeed(needName);
    }
  }
  void DebugUnpauseActionsForNeed( ConsoleFunctionContextRef context )
  {
    if( g_DebugNeedsManager != nullptr)
    {
      const char* needName = ConsoleArg_Get_String(context, "needName");
      g_DebugNeedsManager->DebugUnpauseActionsForNeed(needName);
    }
  }
  void DebugSetRepairLevel( ConsoleFunctionContextRef context )
  {
    if( g_DebugNeedsManager != nullptr)
    {
      const float level = ConsoleArg_Get_Float(context, "level");
      g_DebugNeedsManager->DebugSetNeedLevel(NeedId::Repair, level);
    }
  }
  void DebugSetEnergyLevel( ConsoleFunctionContextRef context )
  {
    if( g_DebugNeedsManager != nullptr)
    {
      const float level = ConsoleArg_Get_Float(context, "level");
      g_DebugNeedsManager->DebugSetNeedLevel(NeedId::Energy, level);
    }
  }
  void DebugSetPlayLevel( ConsoleFunctionContextRef context )
  {
    if( g_DebugNeedsManager != nullptr)
    {
      const float level = ConsoleArg_Get_Float(context, "level");
      g_DebugNeedsManager->DebugSetNeedLevel(NeedId::Play, level);
    }
  }
  void DebugPassTimeMinutes( ConsoleFunctionContextRef context )
  {
    if( g_DebugNeedsManager != nullptr)
    {
      const float minutes = ConsoleArg_Get_Float(context, "minutes");
      g_DebugNeedsManager->DebugPassTimeMinutes(minutes);
    }
  }
  CONSOLE_FUNC( DebugFillNeedMeters, "Needs" );
  CONSOLE_FUNC( DebugGiveStar, "Needs" );
  CONSOLE_FUNC( DebugCompleteDay, "Needs" );
  CONSOLE_FUNC( DebugResetNeeds, "Needs" );
  CONSOLE_FUNC( DebugCompleteAction, "Needs", const char* actionName );
  CONSOLE_FUNC( DebugPredictActionResult, "Needs", const char* actionName );
  CONSOLE_FUNC( DebugPauseDecayForNeed, "Needs", const char* needName );
  CONSOLE_FUNC( DebugPauseActionsForNeed, "Needs", const char* needName );
  CONSOLE_FUNC( DebugUnpauseDecayForNeed, "Needs", const char* needName );
  CONSOLE_FUNC( DebugUnpauseActionsForNeed, "Needs", const char* needName );
  CONSOLE_FUNC( DebugSetRepairLevel, "Needs", float level );
  CONSOLE_FUNC( DebugSetEnergyLevel, "Needs", float level );
  CONSOLE_FUNC( DebugSetPlayLevel, "Needs", float level );
  CONSOLE_FUNC( DebugPassTimeMinutes, "Needs", float minutes );
};
#endif

NeedsManager::NeedsManager(const CozmoContext* cozmoContext)
: _cozmoContext(cozmoContext)
, _robot(nullptr)
, _needsState()
, _needsStateFromRobot()
, _needsConfig()
, _actionsConfig()
, _starRewardsConfig()
, _savedTimeLastWrittenToDevice()
, _timeLastWrittenToRobot()
, _robotHadValidNeedsData(false)
, _deviceHadValidNeedsData(false)
, _robotNeedsVersionUpdate(false)
, _deviceNeedsVersionUpdate(false)
, _previousRobotSerialNumber(0)
, _robotOnboardingStageCompleted(0)
, _isPausedOverall(false)
, _timeWhenPausedOverall_s(0.0f)
, _isDecayPausedForNeed()
, _isActionsPausedForNeed()
, _lastDecayUpdateTime_s()
, _timeWhenPaused_s()
, _timeWhenCooldownStarted_s()
, _timeWhenCooldownOver_s()
, _queuedNeedDeltas()
, _actionCooldown_s()
, _onlyWhiteListedActionsEnabled(false)
, _currentTime_s(0.0f)
, _timeForNextPeriodicDecay_s(0.0f)
, _pausedDurRemainingPeriodicDecay(0.0f)
, _signalHandles()
, kPathToSavedStateFile((cozmoContext->GetDataPlatform() != nullptr ? cozmoContext->GetDataPlatform()->pathToResource(Util::Data::Scope::Persistent, GetNurtureFolder()) : ""))
, _robotStorageState(RobotStorageState::Inactive)
, _faceDistortionComponent(new DesiredFaceDistortionComponent(*this))
{
  for (int i = 0; i < static_cast<int>(NeedId::Count); i++)
  {
    _isDecayPausedForNeed[i] = false;
    _isActionsPausedForNeed[i] = false;

    _lastDecayUpdateTime_s[i] = 0.0f;
    _timeWhenPaused_s[i] = 0.0f;
    _timeWhenCooldownStarted_s[i] = 0.0f;
    _timeWhenCooldownOver_s[i] = 0.0f;

    _queuedNeedDeltas[i].clear();
  }

  for (int i = 0; i < static_cast<int>(NeedsActionId::Count); i++)
  {
    _actionCooldown_s[i] = 0.0f;
  }
}

NeedsManager::~NeedsManager()
{
  _signalHandles.clear();
#if ANKI_DEV_CHEATS
  g_DebugNeedsManager = nullptr;
#endif
}


void NeedsManager::Init(const float currentTime_s, const Json::Value& inJson,
                        const Json::Value& inStarsJson, const Json::Value& inActionsJson,
                        const Json::Value& inDecayJson, const Json::Value& inHandlersJson)
{
  PRINT_CH_INFO(kLogChannelName, "NeedsManager.Init", "Starting Init of NeedsManager");

  _needsConfig.Init(inJson);
  _needsConfig.InitDecay(inDecayJson);
  
  _starRewardsConfig = std::make_shared<StarRewardsConfig>();
  _starRewardsConfig->Init(inStarsJson);

  _actionsConfig.Init(inActionsJson);

  if( ANKI_VERIFY(_cozmoContext->GetRandom() != nullptr,
                  "NeedsManager.Init.NoRNG",
                  "Can't create needs handler for face glitches because there is no RNG in cozmo context") ) {
    _faceDistortionComponent->Init(inHandlersJson, _cozmoContext->GetRandom());
  }

  if (_cozmoContext->GetExternalInterface() != nullptr)
  {
    auto helper = MakeAnkiEventUtil(*_cozmoContext->GetExternalInterface(), *this, _signalHandles);
    using namespace ExternalInterface;
    helper.SubscribeGameToEngine<MessageGameToEngineTag::GetNeedsState>();
    helper.SubscribeGameToEngine<MessageGameToEngineTag::ForceSetNeedsLevels>();
    helper.SubscribeGameToEngine<MessageGameToEngineTag::ForceSetDamagedParts>();
    helper.SubscribeGameToEngine<MessageGameToEngineTag::SetNeedsActionWhitelist>();
    helper.SubscribeGameToEngine<MessageGameToEngineTag::RegisterOnboardingComplete>();
    helper.SubscribeGameToEngine<MessageGameToEngineTag::SetNeedsPauseState>();
    helper.SubscribeGameToEngine<MessageGameToEngineTag::GetNeedsPauseState>();
    helper.SubscribeGameToEngine<MessageGameToEngineTag::SetNeedsPauseStates>();
    helper.SubscribeGameToEngine<MessageGameToEngineTag::GetNeedsPauseStates>();
    helper.SubscribeGameToEngine<MessageGameToEngineTag::RegisterNeedsActionCompleted>();
    helper.SubscribeGameToEngine<MessageGameToEngineTag::SetGameBeingPaused>();
    helper.SubscribeGameToEngine<MessageGameToEngineTag::EnableDroneMode>();
    helper.SubscribeGameToEngine<MessageGameToEngineTag::GetWantsNeedsOnboarding>();
    helper.SubscribeGameToEngine<MessageGameToEngineTag::WipeDeviceNeedsData>();
    helper.SubscribeGameToEngine<MessageGameToEngineTag::WipeRobotGameData>();
    helper.SubscribeGameToEngine<MessageGameToEngineTag::WipeRobotNeedsData>();
  }

  InitInternal(currentTime_s);
}


void NeedsManager::InitReset(const float currentTime_s, const u32 serialNumber)
{
  _needsState.Init(_needsConfig, serialNumber, _starRewardsConfig, _cozmoContext->GetRandom());

  _timeForNextPeriodicDecay_s = currentTime_s + _needsConfig._decayPeriod;

  for (int i = 0; i < static_cast<int>(NeedId::Count); i++)
  {
    _lastDecayUpdateTime_s[i] = _currentTime_s;
    _timeWhenCooldownStarted_s[i] = 0.0f;
    _timeWhenCooldownOver_s[i] = 0.0f;

    _isDecayPausedForNeed[i] = false;
    _isActionsPausedForNeed[i] = false;
  }

  for (int i = 0; i < static_cast<int>(NeedsActionId::Count); i++)
  {
    _actionCooldown_s[i] = 0.0f;
  }
}


void NeedsManager::InitInternal(const float currentTime_s)
{
  const u32 uninitializedSerialNumber = 0;
  InitReset(currentTime_s, uninitializedSerialNumber);

  // Read needs data from device storage, if it exists
  _deviceHadValidNeedsData = false;
  _deviceNeedsVersionUpdate = false;
  bool appliedDecay = false;

  if (DeviceHasNeedsState())
  {
    _deviceHadValidNeedsData = ReadFromDevice(_deviceNeedsVersionUpdate);

    if (_deviceHadValidNeedsData)
    {
      // Save the time this save was made, for later comparison in InitAfterReadFromRobotAttempt
      _savedTimeLastWrittenToDevice = _needsState._timeLastWritten;

      ApplyDecayForUnconnectedTime();

      appliedDecay = true;
    }
  }

  SendNeedsStateToGame(appliedDecay ? NeedsActionId::Decay : NeedsActionId::NoAction);

  // Save to device, because we've either applied a bunch of unconnected decay,
  // or we never had valid needs data on this device yet
  WriteToDevice();

#if ANKI_DEV_CHEATS
  g_DebugNeedsManager = this;
#endif
}


void NeedsManager::InitAfterConnection()
{
  _robot = _cozmoContext->GetRobotManager()->GetFirstRobot();
}


void NeedsManager::InitAfterSerialNumberAcquired(u32 serialNumber)
{
  _previousRobotSerialNumber = _needsState._robotSerialNumber;
  _needsState._robotSerialNumber = serialNumber;

  PRINT_CH_INFO(kLogChannelName, "NeedsManager.InitAfterSerialNumberAcquired",
                      "Starting MAIN Init of NeedsManager, with serial number %d", serialNumber);

  // See if the robot has valid needs state, and if so load it
  _robotHadValidNeedsData = false;
  _robotNeedsVersionUpdate = false;
  if (!StartReadFromRobot())
  {
    // If the read from robot fails immediately, move on to post-robot-read init
    InitAfterReadFromRobotAttempt();
  }
}


void NeedsManager::InitAfterReadFromRobotAttempt()
{
  bool needToWriteToDevice = false;
  bool needToWriteToRobot = _robotNeedsVersionUpdate;

  // DAS Event: "needs.resolve_on_connection"
  // s_val: Whether device had valid needs data (1 or 0), and whether robot
  //        had valid needs data, separated by a colon
  // data: Serial number extracted from device storage, and serial number on
  //       robot, separated by colon
  std::ostringstream stream;
  stream << (_deviceHadValidNeedsData ? "1" : "0") << ":" << (_robotHadValidNeedsData ? "1" : "0");
  const std::string serialNumbers = std::to_string(_previousRobotSerialNumber) + ":" +
                                    std::to_string(_needsStateFromRobot._robotSerialNumber);
  Anki::Util::sEvent("needs.resolve_on_connection",
                     {{DDATA, serialNumbers.c_str()}},
                     stream.str().c_str());

  bool useStateFromRobot = false;

  if (!_robotHadValidNeedsData && !_deviceHadValidNeedsData)
  {
    PRINT_CH_INFO(kLogChannelName, "NeedsManager.InitAfterReadFromRobotAttempt", "Neither robot nor device has needs data");
    // Neither robot nor device has needs data
    needToWriteToDevice = true;
    needToWriteToRobot = true;
  }
  else if (_robotHadValidNeedsData && !_deviceHadValidNeedsData)
  {
    PRINT_CH_INFO(kLogChannelName, "NeedsManager.InitAfterReadFromRobotAttempt", "Robot has needs data, but device doesn't");
    // Robot has needs data, but device doesn't
    // (Use case:  Robot has been used with another device)
    needToWriteToDevice = true;
    useStateFromRobot = true;
  }
  else if (!_robotHadValidNeedsData && _deviceHadValidNeedsData)
  {
    PRINT_CH_INFO(kLogChannelName, "NeedsManager.InitAfterReadFromRobotAttempt", "Robot does NOT have needs data, but device does");
    // Robot does NOT have needs data, but device does
    // So just go with device data, and write that to robot
    needToWriteToRobot = true;
  }
  else
  {
    PRINT_CH_INFO(kLogChannelName, "NeedsManager.InitAfterReadFromRobotAttempt", "Both robot and device have needs data...");
    PRINT_CH_INFO(kLogChannelName, "NeedsManager.InitAfterReadFromRobotAttempt", "Serial numbers %x and %x", _previousRobotSerialNumber, _needsStateFromRobot._robotSerialNumber);
    // Both robot and device have needs data
    if (_previousRobotSerialNumber == _needsStateFromRobot._robotSerialNumber)
    {
      // DAS Event: "needs.resolve_on_connection_matched"
      // s_val: 0 if timestamps matched; -1 if device storage was newer; 1 if
      //        robot storage was newer
      // data: Unused
      std::string comparison = (_savedTimeLastWrittenToDevice < _needsStateFromRobot._timeLastWritten ? "1" :
                               (_savedTimeLastWrittenToDevice > _needsStateFromRobot._timeLastWritten ? "-1" : "0"));
      Anki::Util::sEvent("needs.resolve_on_connection_matched", {}, comparison.c_str());

      PRINT_CH_INFO(kLogChannelName, "NeedsManager.InitAfterReadFromRobotAttempt", "...and serial numbers MATCH");
      // This was the same robot the device had been connected to before
      if (_savedTimeLastWrittenToDevice < _needsStateFromRobot._timeLastWritten)
      {
        PRINT_CH_INFO(kLogChannelName, "NeedsManager.InitAfterReadFromRobotAttempt", "Robot data is newer");
        // Robot data is newer; possibly someone controlled this robot with another device
        // Go with the robot data
        needToWriteToDevice = true;
        useStateFromRobot = true;
      }
      else if (_savedTimeLastWrittenToDevice > _needsStateFromRobot._timeLastWritten)
      {
        PRINT_CH_INFO(kLogChannelName, "NeedsManager.InitAfterReadFromRobotAttempt", "Device data is newer");
        // Device data is newer; go with the device data
        needToWriteToRobot = true;
      }
      else
      {
        // (else the times are identical, which is the normal case...nothing to do)
        PRINT_CH_INFO(kLogChannelName, "NeedsManager.InitAfterReadFromRobotAttempt", "Timestamps are IDENTICAL");
      }
    }
    else
    {
      PRINT_CH_INFO(kLogChannelName, "NeedsManager.InitAfterReadFromRobotAttempt", "...and serial numbers DON'T match");
      // User has connected to a different robot that has used the needs feature.
      // Use the robot's state; copy it to the device.
      needToWriteToDevice = true;
      useStateFromRobot = true;

      // Notify the game, so it can put up a dialog to notify the user
      ExternalInterface::ConnectedToDifferentRobot message;
      const auto& extInt = _cozmoContext->GetExternalInterface();
      extInt->Broadcast(ExternalInterface::MessageEngineToGame(std::move(message)));
    }
  }

  if (useStateFromRobot)
  {
    // Copy the loaded robot needs state into our device needs state
    _needsState = _needsStateFromRobot;

    // Now apply decay for the unconnected time for THIS robot
    // (We did it earlier, in Init, but that was for a different robot)
    ApplyDecayForUnconnectedTime();
  }
  
  // Update Game on Robot's last state
  SendNeedsOnboardingToGame();

  const Time now = system_clock::now();

  if (needToWriteToDevice)
  {
    if (_deviceNeedsVersionUpdate)
    {
      _deviceNeedsVersionUpdate = false;
      PRINT_CH_INFO(kLogChannelName, "NeedsManager.InitAfterReadFromRobotAttempt", "Writing needs data to device due to storage version update");
    }
    else if (!_deviceHadValidNeedsData)
    {
      PRINT_CH_INFO(kLogChannelName, "NeedsManager.InitAfterReadFromRobotAttempt", "Writing needs data to device for the first time");
    }
    else
    {
      PRINT_CH_INFO(kLogChannelName, "NeedsManager.InitAfterReadFromRobotAttempt", "Writing needs data to device");
    }
    // Instead of having WriteToDevice do the time-stamping, we do it externally here
    // so that we can use the exact same timestamp in StartWriteToRobot below
    _needsState._timeLastWritten = now;
    WriteToDevice(false);
  }

  if (needToWriteToRobot)
  {
    if (_robotNeedsVersionUpdate)
    {
      _robotNeedsVersionUpdate = false;
      PRINT_CH_INFO(kLogChannelName, "NeedsManager.InitAfterReadFromRobotAttempt", "Writing needs data to robot due to storage version update");
    }
    else if (!_robotHadValidNeedsData)
    {
      PRINT_CH_INFO(kLogChannelName, "NeedsManager.InitAfterReadFromRobotAttempt", "Writing needs data to robot for the first time");
    }
    else
    {
      PRINT_CH_INFO(kLogChannelName, "NeedsManager.InitAfterReadFromRobotAttempt", "Writing needs data to robot");
    }
    _timeLastWrittenToRobot = now;
    StartWriteToRobot();
  }
}


void NeedsManager::OnRobotDisconnected()
{
  WriteToDevice();

  _savedTimeLastWrittenToDevice = _needsState._timeLastWritten;

  _robot = nullptr;
}


// This is called whether we are connected to a robot or not
void NeedsManager::Update(const float currentTime_s)
{
  _currentTime_s = currentTime_s;

  if (_isPausedOverall)
    return;

  // Handle periodic decay:
  if (currentTime_s >= _timeForNextPeriodicDecay_s)
  {
    _timeForNextPeriodicDecay_s += _needsConfig._decayPeriod;

    const bool connected = _robot != nullptr;
    ApplyDecayAllNeeds(connected);

    SendNeedsStateToGame(NeedsActionId::Decay);

    // Note that we don't want to write to robot at this point, as that
    // can take a long time (300 ms) and can interfere with animations.
    // So we generally only write to robot on actions completed.

    // However, it's quick to write to device, so we [possibly] do that here:
    PossiblyWriteToDevice();
  }
}


void NeedsManager::SetPaused(const bool paused)
{
  if (paused == _isPausedOverall)
  {
    DEV_ASSERT_MSG(paused != _isPausedOverall, "NeedsManager.SetPaused.Redundant",
                   "Setting paused to %s but already in that state",
                   paused ? "true" : "false");
    return;
  }

  _isPausedOverall = paused;

  if (_isPausedOverall)
  {
    // Calculate and record how much time was left until the next decay
    _pausedDurRemainingPeriodicDecay = _timeForNextPeriodicDecay_s - _currentTime_s;

    _timeWhenPausedOverall_s = _currentTime_s;

    // Send the current needs state to the game as soon as we pause
    //  (because the periodic decay won't happen during pause)
    SendNeedsStateToGame();

    // Now is a good time to save needs state; for example, in SDK mode we
    // will eventually disconnect when exiting SDK mode
    WriteToDevice();
  }
  else
  {
    // When unpausing, set the next 'time for periodic decay'
    _timeForNextPeriodicDecay_s = _currentTime_s + _pausedDurRemainingPeriodicDecay;

    // Then calculate how long we were paused
    const float durationOfPause = _currentTime_s - _timeWhenPausedOverall_s;

    // Adjust some timers accordingly, so that the overall pause is excluded from
    // things like decay time, and individual needs pausing
    for (int needIndex = 0; needIndex < static_cast<int>(NeedId::Count); needIndex++)
    {
      _lastDecayUpdateTime_s[needIndex] += durationOfPause;
      _timeWhenPaused_s[needIndex] += durationOfPause;

      if (_timeWhenCooldownOver_s[needIndex] != 0.0f)
      {
        _timeWhenCooldownOver_s[needIndex] += durationOfPause;
        _timeWhenCooldownStarted_s[needIndex] += durationOfPause;
      }
    }
  }

  SendNeedsPauseStateToGame();
}


NeedsState& NeedsManager::GetCurNeedsStateMutable()
{
  _needsState.UpdateCurNeedsBrackets(_needsConfig._needsBrackets);
  return _needsState;
}


const NeedsState& NeedsManager::GetCurNeedsState()
{
  return GetCurNeedsStateMutable();
}


void NeedsManager::RegisterNeedsActionCompleted(const NeedsActionId actionCompleted)
{
  if (_isPausedOverall) {
    return;
  }
  // Only accept certain types of events
  if (_onlyWhiteListedActionsEnabled)
  {
    if (_whiteListedActions.find(actionCompleted) == _whiteListedActions.end())
    {
      return;
    }
  }

  const int actionIndex = static_cast<int>(actionCompleted);
  if (_currentTime_s < _actionCooldown_s[actionIndex])
  {
    // DAS Event: "needs.action_completed_ignored"
    // s_val: The needs action being completed
    // data: Unused
    Anki::Util::sEvent("needs.action_completed_ignored", {},
                       NeedsActionIdToString(actionCompleted));
    return;
  }
  const auto& actionDelta = _actionsConfig._actionDeltas[actionIndex];
  if (!Util::IsNearZero(actionDelta._cooldown_s))
  {
    _actionCooldown_s[actionIndex] = _currentTime_s + actionDelta._cooldown_s;
  }

  NeedsState::CurNeedsMap prevNeedsLevels = _needsState._curNeedsLevels;
  _needsState.SetPrevNeedsBrackets();

  RegisterNeedsActionCompletedInternal(actionCompleted, _needsState, false);

  // DAS Event: "needs.action_completed"
  // s_val: The needs action being completed
  // data: The needs levels before the completion, followed by the needs levels after
  //       the completion, all colon-separated (e.g. "1.0000:0.6000:0.7242:0.6000:0.5990:0.7202"
  std::ostringstream stream;
  FormatStringOldAndNewLevels(stream, prevNeedsLevels);
  Anki::Util::sEvent("needs.action_completed",
                     {{DDATA, stream.str().c_str()}},
                     NeedsActionIdToString(actionCompleted));

  SendNeedsStateToGame(actionCompleted);

  bool starAwarded = UpdateStarsState();

  // If no daily star was awarded, possibly award sparks for freeplay activities
  if (!starAwarded)
  {
    if (!Util::IsNearZero(actionDelta._freeplaySparksRewardWeight))
    {
      if (ShouldRewardSparksForFreeplay())
      {
        const int sparksAwarded = RewardSparksForFreeplay();

        // Tell game that sparks were awarded, and how many, and the new total
        auto& ic = _robot->GetInventoryComponent();
        const int sparksTotal = ic.GetInventoryAmount(InventoryType::Sparks);
        ExternalInterface::FreeplaySparksAwarded msg;
        msg.sparksAwarded = sparksAwarded;
        msg.sparksTotal = sparksTotal;
        msg.needsActionId = actionCompleted;
        msg.sparksAwardedDisplayKey = kFreeplaySparksRewardStringKey;
        const auto& extInt = _cozmoContext->GetExternalInterface();
        extInt->Broadcast(ExternalInterface::MessageEngineToGame(std::move(msg)));
      }
    }
  }

  DetectBracketChangeForDas();

  PossiblyWriteToDevice();
  PossiblyStartWriteToRobot();
}


void NeedsManager::PredictNeedsActionResult(const NeedsActionId actionCompleted, NeedsState& outNeedsState)
{
  outNeedsState = _needsState;

  if (_isPausedOverall) {
    return;
  }

  // NOTE: Since an action's deltas can have a 'random uniform distribution', this code
  // is not fully accurate when that applies, since when the 'real' call is made, we'll
  // generate the random range again.  (We don't seem to have a random number generator
  // that allows us to extract the current seed, save it, and later restore it.)
  RegisterNeedsActionCompletedInternal(actionCompleted, outNeedsState, true);
}


void NeedsManager::RegisterNeedsActionCompletedInternal(const NeedsActionId actionCompleted,
                                                        NeedsState& needsState,
                                                        bool predictionOnly)
{
  PRINT_CH_INFO(kLogChannelName, "NeedsManager.RegisterNeedsActionCompletedInternal",
                "%s %s", predictionOnly ? "Predicted" : "Completed",
                NeedsActionIdToString(actionCompleted));
  const int actionIndex = static_cast<int>(actionCompleted);
  const auto& actionDelta = _actionsConfig._actionDeltas[actionIndex];

  switch (actionCompleted)
  {
    case NeedsActionId::RepairHead:
    {
      const auto partId = RepairablePartId::Head;
      needsState._partIsDamaged[partId] = false;
      if (!predictionOnly)
      {
        SendRepairDasEvent(needsState, actionCompleted, partId);
      }
      break;
    }
    case NeedsActionId::RepairLift:
    {
      const auto partId = RepairablePartId::Lift;
      needsState._partIsDamaged[partId] = false;
      if (!predictionOnly)
      {
        SendRepairDasEvent(needsState, actionCompleted, partId);
      }
      break;
    }
    case NeedsActionId::RepairTreads:
    {
      const auto partId = RepairablePartId::Treads;
      needsState._partIsDamaged[partId] = false;
      if (!predictionOnly)
      {
        SendRepairDasEvent(needsState, actionCompleted, partId);
      }
      break;
    }
    default:
      break;
  }

  for (int needIndex = 0; needIndex < static_cast<int>(NeedId::Count); needIndex++)
  {
    if (_isActionsPausedForNeed[needIndex])
    {
      if (!predictionOnly)
      {
        NeedDelta deltaToSave = actionDelta._needDeltas[needIndex];
        deltaToSave._cause = actionCompleted;
        _queuedNeedDeltas[needIndex].push_back(deltaToSave);
      }
    }
    else
    {
      const NeedId needId = static_cast<NeedId>(needIndex);
      if (needsState.ApplyDelta(needId,
                                actionDelta._needDeltas[needIndex],
                                actionCompleted))
      {
        StartFullnessCooldownForNeed(needId);
      }
    }
  }

  switch (actionCompleted)
  {
    case NeedsActionId::RepairHead:
    case NeedsActionId::RepairLift:
    case NeedsActionId::RepairTreads:
    {
      if (needsState.NumDamagedParts() == 0)
      {
        // If this was a 'repair' action and there are no more broken parts,
        // set Repair level to 100%
        needsState._curNeedsLevels[NeedId::Repair] = _needsConfig._maxNeedLevel;
        needsState.SetNeedsBracketsDirty();
      }
      break;
    }
    default:
      break;
  }
}


bool NeedsManager::ShouldRewardSparksForFreeplay()
{
  auto& ic = _robot->GetInventoryComponent();
  const int curSparks = ic.GetInventoryAmount(InventoryType::Sparks);
  const int level = _needsState._curNeedsUnlockLevel;
  const float targetRatio = (Util::numeric_cast<float>(curSparks) /
                             _starRewardsConfig->GetFreeplayTargetSparksTotalForLevel(level));
  float rewardChancePct = 1.0f - targetRatio;
  const float minPct = _starRewardsConfig->GetFreeplayMinSparksRewardPctForLevel(level);
  if (rewardChancePct < minPct)
  {
    rewardChancePct = minPct;
  }

  return (_cozmoContext->GetRandom()->RandDbl() < rewardChancePct);
}


int NeedsManager::RewardSparksForFreeplay()
{
  const int level = _needsState._curNeedsUnlockLevel;
  const int sparksAdded = AwardSparks(_starRewardsConfig->GetFreeplayTargetSparksTotalForLevel(level),
                                      _starRewardsConfig->GetFreeplayMinSparksPctForLevel(level),
                                      _starRewardsConfig->GetFreeplayMaxSparksPctForLevel(level),
                                      _starRewardsConfig->GetFreeplayMinSparksForLevel(level),
                                      _starRewardsConfig->GetFreeplayMinMaxSparksForLevel(level));

  return sparksAdded;
}


int NeedsManager::AwardSparks(int targetSparks, float minPct, float maxPct,
                              int minSparks, int minMaxSparks)
{
  auto& ic = _robot->GetInventoryComponent();
  const int curSparks = ic.GetInventoryAmount(InventoryType::Sparks);
  const int delta = targetSparks - curSparks;
  int min = ((delta * minPct) + 0.5f);
  int max = ((delta * maxPct) + 0.5f);

  if (min < minSparks)
  {
    min = minSparks;
  }
  if (max < minMaxSparks)
  {
    max = minMaxSparks;
  }
  const int sparksAdded = _cozmoContext->GetRandom()->RandIntInRange(min, max);

  ic.AddInventoryAmount(InventoryType::Sparks, sparksAdded);

  return sparksAdded;
}


void NeedsManager::SendRepairDasEvent(const NeedsState& needsState,
                                      const NeedsActionId cause,
                                      const RepairablePartId part)
{
  // DAS Event: "needs.part_repaired"
  // s_val: The name of the part repaired (RepairablePartId)
  // data: New number of damaged parts, followed by a colon, followed
  //       by the cause of repair (NeedsActionId)
  std::string data = std::to_string(needsState.NumDamagedParts()) + ":" +
                     NeedsActionIdToString(cause);
  Anki::Util::sEvent("needs.part_repaired",
                     {{DDATA, data.c_str()}},
                     RepairablePartIdToString(part));
}


void NeedsManager::FormatStringOldAndNewLevels(std::ostringstream& stream,
                                               NeedsState::CurNeedsMap& prevNeedsLevels)
{
  stream.precision(5);
  stream << std::fixed;
  for (int needIndex = 0; needIndex < static_cast<int>(NeedId::Count); needIndex++)
  {
    if (needIndex > 0)
    {
      stream << ":";
    }
    stream << prevNeedsLevels[static_cast<NeedId>(needIndex)];
  }
  for (int needIndex = 0; needIndex < static_cast<int>(NeedId::Count); needIndex++)
  {
    stream << ":" << _needsState.GetNeedLevelByIndex(needIndex);
  }
}


template<>
void NeedsManager::HandleMessage(const ExternalInterface::GetNeedsState& msg)
{
  SendNeedsStateToGame();
}

template<>
void NeedsManager::HandleMessage(const ExternalInterface::ForceSetNeedsLevels& msg)
{
  NeedsState::CurNeedsMap prevNeedsLevels = _needsState._curNeedsLevels;

  for (int needIndex = 0; needIndex < static_cast<int>(NeedId::Count); needIndex++)
  {
    float newLevel = msg.newNeedLevel[needIndex];
    newLevel = Util::Clamp(newLevel, _needsConfig._minNeedLevel, _needsConfig._maxNeedLevel);
    _needsState._curNeedsLevels[static_cast<NeedId>(needIndex)] = newLevel;
  }

  _needsState.SetNeedsBracketsDirty();

  // Note that we don't set the appropriate number of broken parts here, because we're
  // just using this to fake needs levels during onboarding, and we will fully initialize
  // after onboarding completes.  The ForceSetDamagedParts message below can be used to
  // set whether each part is damaged.

  SendNeedsStateToGame();

  // DAS Event: "needs.force_set_needs_levels"
  // s_val: The needs levels before the completion, followed by the needs levels after
  //       the completion, all colon-separated (e.g. "1.0000:0.6000:0.7242:0.6000:0.5990:0.7202"
  // data: Unused
  std::ostringstream stream;
  FormatStringOldAndNewLevels(stream, prevNeedsLevels);
  Anki::Util::sEvent("needs.force_set_needs_levels", {}, stream.str().c_str());
}

template<>
void NeedsManager::HandleMessage(const ExternalInterface::ForceSetDamagedParts& msg)
{
  for (size_t i = 0; i < RepairablePartIdNumEntries; i++)
  {
    _needsState._partIsDamaged[static_cast<RepairablePartId>(i)] = msg.partIsDamaged[i];
  }

  SendNeedsStateToGame();

  // DAS Event: "needs.force_set_damaged_parts"
  // s_val: Colon-separated list of bools (expressed as 1 or 0) for whether each
  //        repairable part is damaged
  // data: Unused
  std::ostringstream stream;
  for (size_t i = 0; i < RepairablePartIdNumEntries; i++)
  {
    if (i > 0)
    {
      stream << ":";
    }
    stream << (msg.partIsDamaged[i] ? "1" : "0");
  }
  Anki::Util::sEvent("needs.force_set_damaged_parts", {}, stream.str().c_str());
}
  
template<>
void NeedsManager::HandleMessage(const ExternalInterface::SetNeedsActionWhitelist& msg)
{
  _onlyWhiteListedActionsEnabled = msg.enable;
  _whiteListedActions.clear();
  if( _onlyWhiteListedActionsEnabled )
  {
    std::copy(msg.whitelistedActions.begin(), msg.whitelistedActions.end(),
              std::inserter(_whiteListedActions, _whiteListedActions.end()));
  }
}

template<>
void NeedsManager::HandleMessage(const ExternalInterface::RegisterOnboardingComplete& msg)
{
  bool forceWriteToRobot = false;

  _robotOnboardingStageCompleted = msg.onboardingStage;

  // phase 1 is just the first part showing the needs hub.
  if( msg.finalStage )
  {
    // Reset cozmo's need levels to their starting points, and reset timers
    InitReset(_currentTime_s, _needsState._robotSerialNumber);

    // onboarding unlocks one star.
    _needsState._numStarsAwarded = 1;
    const Time nowTime = system_clock::now();
    _needsState._timeLastStarAwarded = nowTime;

    // Un-pause the needs system if we are not already
    if (_isPausedOverall)
    {
      SetPaused(false);
    }

    SendNeedsStateToGame();

    // DAS Event: "needs.onboarding_completed"
    // s_val: Unused
    // data: Unused
    Anki::Util::sEvent("needs.onboarding_completed", {}, "");

    forceWriteToRobot = true;
  }

  PossiblyStartWriteToRobot(forceWriteToRobot);
}

template<>
void NeedsManager::HandleMessage(const ExternalInterface::SetNeedsPauseState& msg)
{
  SetPaused(msg.pause);
}

template<>
void NeedsManager::HandleMessage(const ExternalInterface::GetNeedsPauseState& msg)
{
  SendNeedsPauseStateToGame();
}

template<>
void NeedsManager::HandleMessage(const ExternalInterface::SetNeedsPauseStates& msg)
{
  if (_isPausedOverall)
  {
    PRINT_CH_DEBUG(kLogChannelName, "NeedsManager.HandleSetNeedsPauseStates",
                  "SetNeedsPauseStates message received but ignored because overall needs manager is paused");
    return;
  }

  _needsState.SetPrevNeedsBrackets();

  // Pause/unpause for decay
  NeedsMultipliers multipliers;
  bool multipliersSet = false;
  bool needToSendNeedsStateToGame = false;

  for (int needIndex = 0; needIndex < _isDecayPausedForNeed.size(); needIndex++)
  {
    if (!_isDecayPausedForNeed[needIndex] && msg.decayPause[needIndex])
    {
      // If pausing this need for decay, record the time we are pausing
      _timeWhenPaused_s[needIndex] = _currentTime_s;
    }
    else if (_isDecayPausedForNeed[needIndex] && !msg.decayPause[needIndex])
    {
      // If un-pausing this need for decay, OPTIONALLY apply queued decay for this need
      if (msg.decayDiscardAfterUnpause[needIndex])
      {
        // Throw away the decay for the period the need was paused
        // But we don't want to throw away (a) the time between the last decay and when
        // the pause started, and (b) the time between now and when the next periodic
        // decay will occur.  So set the 'time of last decay' to account for this:
        // (A key point here is that we want the periodic decay for needs to happen all
        // at the same time.)
        const float durationA_s = _timeWhenPaused_s[needIndex] - _lastDecayUpdateTime_s[needIndex];
        const float durationB_s = _timeForNextPeriodicDecay_s - _currentTime_s;
        _lastDecayUpdateTime_s[needIndex] = _timeForNextPeriodicDecay_s - (durationA_s + durationB_s);
      }
      else
      {
        // (But do nothing if we're in a 'fullness cooldown')
        if (_timeWhenCooldownOver_s[needIndex] == 0.0f)
        {
          // Apply the decay for the period the need was paused
          if (!multipliersSet)
          {
            // Set the multipliers only once even if we're applying decay to mulitiple needs at
            // once.  This is to make it "fair", as multipliers are set according to need levels
            multipliersSet = true;
            _needsState.SetDecayMultipliers(_needsConfig._decayConnected, multipliers);
          }
          const float duration_s = _currentTime_s - _lastDecayUpdateTime_s[needIndex];
          _needsState.ApplyDecay(_needsConfig._decayConnected, needIndex, duration_s, multipliers);
          _lastDecayUpdateTime_s[needIndex] = _currentTime_s;
          needToSendNeedsStateToGame = true;
        }
      }
    }

    _isDecayPausedForNeed[needIndex] = msg.decayPause[needIndex];
  }

  // Pause/unpause for actions
  for (int needIndex = 0; needIndex < _isActionsPausedForNeed.size(); needIndex++)
  {
    if (_isActionsPausedForNeed[needIndex] && !msg.actionPause[needIndex])
    {
      // If un-pausing this need for actions, apply all queued actions for this need
      auto& queuedDeltas = _queuedNeedDeltas[needIndex];
      for (int j = 0; j < queuedDeltas.size(); j++)
      {
        const NeedId needId = static_cast<NeedId>(needIndex);
        if (_needsState.ApplyDelta(needId, queuedDeltas[j], queuedDeltas[j]._cause))
        {
          StartFullnessCooldownForNeed(needId);
        }
        needToSendNeedsStateToGame = true;
      }
      queuedDeltas.clear();
    }

    _isActionsPausedForNeed[needIndex] = msg.actionPause[needIndex];
  }

  if (needToSendNeedsStateToGame)
  {
    SendNeedsStateToGame();

    UpdateStarsState();

    DetectBracketChangeForDas();
  }
}

template<>
void NeedsManager::HandleMessage(const ExternalInterface::GetNeedsPauseStates& msg)
{
  SendNeedsPauseStatesToGame();
}

template<>
void NeedsManager::HandleMessage(const ExternalInterface::GetWantsNeedsOnboarding& msg)
{
  SendNeedsOnboardingToGame();
}

template<>
void NeedsManager::HandleMessage(const ExternalInterface::WipeDeviceNeedsData& msg)
{
  Util::FileUtils::DeleteFile(kPathToSavedStateFile + kNeedsStateFile);

  if (msg.reinitializeNeeds)
  {
    InitInternal(_currentTime_s);
  }
}

template<>
void NeedsManager::HandleMessage(const ExternalInterface::WipeRobotGameData& msg)
{
  // When the debug 'erase everything' button is pressed, that means we also need
  // to re-initialize the needs levels
  InitInternal(_currentTime_s);
}

template<>
void NeedsManager::HandleMessage(const ExternalInterface::WipeRobotNeedsData& msg)
{
  if (!_robot->GetNVStorageComponent().Erase(NVStorage::NVEntryTag::NVEntry_NurtureGameData,
                                             [this](NVStorage::NVResult res)
                                             {
                                               bool success;
                                               if(res < NVStorage::NVResult::NV_OKAY)
                                               {
                                                 success = false;
                                                 PRINT_NAMED_WARNING("NeedsManager.WipeRobotNeedsData",
                                                                     "Erase of needs data failed with %s",
                                                                     EnumToString(res));
                                               }
                                               else
                                               {
                                                 success = true;
                                                 PRINT_NAMED_INFO("NeedsManager.WipeRobotNeedsData",
                                                                  "Erase of needs complete!");
                                               }
                                               const auto& extInt = _cozmoContext->GetExternalInterface();
                                               extInt->Broadcast(ExternalInterface::MessageEngineToGame(ExternalInterface::RestoreRobotStatus(true, success)));

                                               InitInternal(_currentTime_s);
                                             }))
  {
    PRINT_NAMED_ERROR("NeedsManager.WipeRobotNeedsData.EraseFailed", "Erase failed");
    _robotStorageState = RobotStorageState::Inactive;
  }
}

template<>
void NeedsManager::HandleMessage(const ExternalInterface::RegisterNeedsActionCompleted& msg)
{
  RegisterNeedsActionCompleted(msg.actionCompleted);
}

template<>
void NeedsManager::HandleMessage(const ExternalInterface::SetGameBeingPaused& msg)
{
  PRINT_CH_INFO(kLogChannelName, "NeedsManager.HandleSetGameBeingPaused",
                "Game being paused set to %s",
                msg.isPaused ? "TRUE" : "FALSE");

  // When app is backgrounded, we want to also pause the whole needs system
  // Note:  When pausing, we'll also call WriteToDevice
  SetPaused(msg.isPaused);

  if (msg.isPaused)
  {
    if (_robotStorageState == RobotStorageState::Inactive)
    {
      if (_robot != nullptr)
      {
        _timeLastWrittenToRobot = _needsState._timeLastWritten;
        StartWriteToRobot();
      }
    }
  }
}

template<>
void NeedsManager::HandleMessage(const ExternalInterface::EnableDroneMode& msg)
{
  // Pause the needs system during explorer mode
  SetPaused(msg.isStarted);
}


void NeedsManager::SendNeedsStateToGame(const NeedsActionId actionCausingTheUpdate /* = NeedsActionId::NoAction */)
{
  _needsState.UpdateCurNeedsBrackets(_needsConfig._needsBrackets);
  
  std::vector<float> needLevels;
  needLevels.reserve((size_t)NeedId::Count);
  for (size_t i = 0; i < (size_t)NeedId::Count; i++)
  {
    const float level = _needsState.GetNeedLevelByIndex(i);
    needLevels.push_back(level);
  }
  
  std::vector<NeedBracketId> needBrackets;
  needBrackets.reserve((size_t)NeedId::Count);
  for (size_t i = 0; i < (size_t)NeedId::Count; i++)
  {
    const NeedBracketId bracketId = _needsState.GetNeedBracketByIndex(i);
    needBrackets.push_back(bracketId);
  }
  
  std::vector<bool> partIsDamaged;
  partIsDamaged.reserve(RepairablePartIdNumEntries);
  for (size_t i = 0; i < RepairablePartIdNumEntries; i++)
  {
    const bool isDamaged = _needsState.GetPartIsDamagedByIndex(i);
    partIsDamaged.push_back(isDamaged);
  }

  ExternalInterface::NeedsState message(std::move(needLevels), std::move(needBrackets),
                                        std::move(partIsDamaged), _needsState._curNeedsUnlockLevel,
                                        _needsState._numStarsAwarded, _needsState._numStarsForNextUnlock,
                                        actionCausingTheUpdate);
  const auto& extInt = _cozmoContext->GetExternalInterface();
  extInt->Broadcast(ExternalInterface::MessageEngineToGame(std::move(message)));

  SendNeedsDebugVizString(actionCausingTheUpdate);
}

void NeedsManager::SendNeedsDebugVizString(const NeedsActionId actionCausingTheUpdate)
{
#if ANKI_DEV_CHEATS

  // Example string:
  // Eng:0.31-Warn Play:1.00-Full Repr:0.05-Crit HiccupsEndGood

  _cozmoContext->GetVizManager()->SetText(
    VizManager::NEEDS_STATE, NamedColors::ORANGE,
    "Eng:%04.2f-%.4s Play:%04.2f-%.4s Repr:%04.2f-%.4s %s",
    _needsState.GetNeedLevel(NeedId::Energy),
    NeedBracketIdToString(_needsState.GetNeedBracket(NeedId::Energy)),
    _needsState.GetNeedLevel(NeedId::Play),
    NeedBracketIdToString(_needsState.GetNeedBracket(NeedId::Play)),
    _needsState.GetNeedLevel(NeedId::Repair),
    NeedBracketIdToString(_needsState.GetNeedBracket(NeedId::Repair)),
    NeedsActionIdToString(actionCausingTheUpdate));
  
#endif

}

void NeedsManager::SendNeedsPauseStateToGame()
{
  ExternalInterface::NeedsPauseState message(_isPausedOverall);
  const auto& extInt = _cozmoContext->GetExternalInterface();
  extInt->Broadcast(ExternalInterface::MessageEngineToGame(std::move(message)));
}
  
void NeedsManager::SendNeedsPauseStatesToGame()
{
  std::vector<bool> decayPause;
  decayPause.reserve(_isDecayPausedForNeed.size());
  for (int i = 0; i < _isDecayPausedForNeed.size(); i++)
  {
    decayPause.push_back(_isDecayPausedForNeed[i]);
  }

  std::vector<bool> actionPause;
  actionPause.reserve(_isActionsPausedForNeed.size());
  for (int i = 0; i < _isActionsPausedForNeed.size(); i++)
  {
    actionPause.push_back(_isActionsPausedForNeed[i]);
  }
  ExternalInterface::NeedsPauseStates message(decayPause, actionPause);
  const auto& extInt = _cozmoContext->GetExternalInterface();
  extInt->Broadcast(ExternalInterface::MessageEngineToGame(std::move(message)));
}


void NeedsManager::ApplyDecayAllNeeds(bool connected)
{
  const DecayConfig& config = connected ? _needsConfig._decayConnected : _needsConfig._decayUnconnected;

  _needsState.SetPrevNeedsBrackets();

  NeedsMultipliers multipliers;
  _needsState.SetDecayMultipliers(config, multipliers);

  for (int needIndex = 0; needIndex < (size_t)NeedId::Count; needIndex++)
  {
    if (!_isDecayPausedForNeed[needIndex])
    {
      if (_timeWhenCooldownOver_s[needIndex] != 0.0f &&
          _currentTime_s > _timeWhenCooldownOver_s[needIndex])
      {
        // There was a 'fullness cooldown' for this need, and it has expired;
        // calculate the amount of decay time we need to adjust for
        const float cooldownDuration = _timeWhenCooldownOver_s[needIndex] -
                                       _timeWhenCooldownStarted_s[needIndex];
        _lastDecayUpdateTime_s[needIndex] += cooldownDuration;

        _timeWhenCooldownOver_s[needIndex] = 0.0f;
        _timeWhenCooldownStarted_s[needIndex] = 0.0f;
      }

      if (_timeWhenCooldownOver_s[needIndex] == 0.0f)
      {
        const float duration_s = _currentTime_s - _lastDecayUpdateTime_s[needIndex];
        _needsState.ApplyDecay(config, needIndex, duration_s, multipliers);
        _lastDecayUpdateTime_s[needIndex] = _currentTime_s;
      }
    }
  }

  DetectBracketChangeForDas();
}


void NeedsManager::ApplyDecayForUnconnectedTime()
{
  // Calculate time elapsed since last connection
  const Time now = system_clock::now();
  const float elasped_s = duration_cast<seconds>(now - _needsState._timeLastWritten).count();

  // Now apply decay according to unconnected config, and elapsed time
  // First, however, we set the timers as if that much time had elapsed:
  for (int i = 0; i < static_cast<int>(NeedId::Count); i++)
  {
    _lastDecayUpdateTime_s[i] = _currentTime_s - elasped_s;
  }

  static const bool connected = false;
  ApplyDecayAllNeeds(connected);
}


void NeedsManager::StartFullnessCooldownForNeed(const NeedId needId)
{
  const int needIndex = static_cast<int>(needId);

  _timeWhenCooldownOver_s[needIndex] = _currentTime_s +
                                       _needsConfig._fullnessDecayCooldownTimes_s[needId];

  if (_timeWhenCooldownStarted_s[needIndex] == 0.0f)
  {
    _timeWhenCooldownStarted_s[needIndex] = _currentTime_s;
  }
}


bool NeedsManager::UpdateStarsState(bool cheatGiveStar)
{
  bool starAwarded = false;

  // If "Play" level has transitioned to Full
  if (((_needsState.GetPrevNeedBracketByIndex((size_t)NeedId::Play) != NeedBracketId::Full) &&
       (_needsState.GetNeedBracketByIndex((size_t)NeedId::Play) == NeedBracketId::Full)) ||
       (cheatGiveStar))
  {
    // Now see if they've already received the star award today:
    const std::time_t lastStarTime = system_clock::to_time_t( _needsState._timeLastStarAwarded );
    std::tm lastLocalTime;
    localtime_r(&lastStarTime, &lastLocalTime);

    const Time nowTime = system_clock::now();
    const std::time_t nowTimeT = system_clock::to_time_t( nowTime );
    std::tm nowLocalTime;
    localtime_r(&nowTimeT, &nowLocalTime);
    
    PRINT_CH_INFO(kLogChannelName, "NeedsManager.UpdateStarsState",
                  "Local time gmt offset %ld",
                  nowLocalTime.tm_gmtoff);

    // Is it past midnight (a different day-of-year (0-365), or a different year)
    if (nowLocalTime.tm_yday != lastLocalTime.tm_yday || nowLocalTime.tm_year != lastLocalTime.tm_year)
    {
      starAwarded = true;

      PRINT_CH_INFO(kLogChannelName, "NeedsManager.UpdateStarsState",
                    "now: %d, lastsave: %d",
                    nowLocalTime.tm_yday, lastLocalTime.tm_yday);
      
      _needsState._timeLastStarAwarded = nowTime;
      _needsState._numStarsAwarded++;
      SendStarUnlockedToGame();
      
      // Completed a set
      if (_needsState._numStarsAwarded >= _needsState._numStarsForNextUnlock)
      {
        // resets the stars
        SendStarLevelCompletedToGame();
      }

      // Save that we've issued a star today
      PossiblyStartWriteToRobot(true);
    }

    // DAS Event: "needs.play_need_filled"
    // s_val: Whether a daily star was awarded (1 or 0)
    // data: New current level
    Anki::Util::sEvent("needs.play_need_filled",
                       {{DDATA, std::to_string(_needsState._curNeedsUnlockLevel).c_str()}},
                       starAwarded ? "1" : "0");
  }

  return starAwarded;
}

void NeedsManager::SendStarLevelCompletedToGame()
{
  // Since the rewards config can be changed after this feature is launched,
  // we want to be able to give users the unlocks they might have missed if
  // they are past a level that has been modified to have an unlock that they
  // don't have.  But we also limit the number of these 'prior level unlocks'
  // so we don't overwhelm them with a ton on any single level unlock.

  std::vector<NeedsReward> rewards;

  // First, see about any prior level unlocks that have not occurred due to a
  // change in the rewards config as described above:
  int allowedPriorUnlocks = _starRewardsConfig->GetMaxPriorUnlocksForLevel(_needsState._curNeedsUnlockLevel);
  static const bool unlocksOnly = true;
  for (int level = 0; level < _needsState._curNeedsUnlockLevel; level++)
  {
    if (allowedPriorUnlocks <= 0)
    {
      break;
    }
    ProcessLevelRewards(level, rewards, unlocksOnly, &allowedPriorUnlocks);
  }

  // Now get the rewards for the level they are unlocking
  ProcessLevelRewards(_needsState._curNeedsUnlockLevel, rewards);

  // level up
  _needsState.SetStarLevel(_needsState._curNeedsUnlockLevel + 1);

  ExternalInterface::StarLevelCompleted message(_needsState._curNeedsUnlockLevel,
                                                _needsState._numStarsForNextUnlock,
                                                std::move(rewards));
  const auto& extInt = _cozmoContext->GetExternalInterface();
  extInt->Broadcast(ExternalInterface::MessageEngineToGame(std::move(message)));

  PRINT_CH_INFO(kLogChannelName, "NeedsManager.SendStarLevelCompletedToGame","CurrUnlockLevel: %d, stars for next: %d, currStars: %d",
                      _needsState._curNeedsUnlockLevel,_needsState._numStarsForNextUnlock, _needsState._numStarsAwarded);
  
  // Save is forced after this function is called.
}


void NeedsManager::ProcessLevelRewards(int level, std::vector<NeedsReward>& rewards,
                                       bool unlocksOnly, int* allowedPriorUnlocks)
{
  std::vector<NeedsReward> rewardsThisLevel;
  _starRewardsConfig->GetRewardsForLevel(level, rewardsThisLevel);

  // Issue rewards in inventory
  for (int rewardIndex = 0; rewardIndex < rewardsThisLevel.size(); ++rewardIndex)
  {
    switch(rewardsThisLevel[rewardIndex].rewardType)
    {
      case NeedsRewardType::Sparks:
      {
        if (unlocksOnly)
        {
          continue;
        }

        const int sparksAdded = AwardSparks(_starRewardsConfig->GetTargetSparksTotalForLevel(level),
                                            _starRewardsConfig->GetMinSparksPctForLevel(level),
                                            _starRewardsConfig->GetMaxSparksPctForLevel(level),
                                            _starRewardsConfig->GetMinSparksForLevel(level),
                                            _starRewardsConfig->GetMinMaxSparksForLevel(level));

        rewards.push_back(rewardsThisLevel[rewardIndex]);

        // Put the actual number of sparks awarded into the rewards data that we're about
        // to send to the game, so game will know how many sparks were actually awarded
        rewards.back().data = std::to_string(sparksAdded);
        break;
      }
      // Songs are treated exactly the same as any other unlock
      case NeedsRewardType::Unlock:
      case NeedsRewardType::Song:
      {
        const UnlockId id = UnlockIdFromString(rewardsThisLevel[rewardIndex].data);
        if (id != UnlockId::Invalid)
        {
          auto& puc = _robot->GetProgressionUnlockComponent();
          if (!puc.IsUnlocked(id))
          {
            _robot->GetProgressionUnlockComponent().SetUnlock(id, true);
            rewards.push_back(rewardsThisLevel[rewardIndex]);
            if (allowedPriorUnlocks != nullptr)
            {
              (*allowedPriorUnlocks)--;
              if (*allowedPriorUnlocks <= 0)
              {
                break;
              }
            }
          }
          else
          {
            // This is probably not an error case, because of post-launch 'prior level'
            // unlocks that can occur if/when we change the reward level unlock config
            if (!unlocksOnly)
            {
              PRINT_NAMED_WARNING("NeedsManager.ProcessLevelRewards",
                                  "Level reward is already unlocked: %s",
                                  UnlockIdToString(id));
            }
          }
        }
        else
        {
          PRINT_NAMED_ERROR("NeedsManager.ProcessLevelRewards",
                            "Level reward has invalid ID: %s",
                            rewardsThisLevel[rewardIndex].data.c_str());
        }
        break;
      }
      case NeedsRewardType::MemoryBadge:
      {
        // TODO: support memory badges in the future
        rewards.push_back(rewardsThisLevel[rewardIndex]);
        break;
      }
      default:
        break;
    }
  }
}


void NeedsManager::SendStarUnlockedToGame()
{
  ExternalInterface::StarUnlocked message(_needsState._curNeedsUnlockLevel,
                                          _needsState._numStarsForNextUnlock,
                                          _needsState._numStarsAwarded);
  const auto& extInt = _cozmoContext->GetExternalInterface();
  extInt->Broadcast(ExternalInterface::MessageEngineToGame(std::move(message)));
}

void NeedsManager::SendNeedsOnboardingToGame()
{
  ExternalInterface::WantsNeedsOnboarding message(_robotOnboardingStageCompleted);
  const auto& extInt = _cozmoContext->GetExternalInterface();
  extInt->Broadcast(ExternalInterface::MessageEngineToGame(std::move(message)));
}

void NeedsManager::DetectBracketChangeForDas()
{
  for (int needIndex = 0; needIndex < static_cast<int>(NeedId::Count); needIndex++)
  {
    const auto oldBracket = _needsState.GetPrevNeedBracketByIndex(needIndex);
    const auto newBracket = _needsState.GetNeedBracketByIndex(needIndex);

    if (oldBracket != newBracket)
    {
      // DAS Event: "needs.bracket_changed"
      // s_val: The need whose bracket is changing (e.g. "Play")
      // data: Old bracket name, followed by new bracket name, separated by
      //       colon, e.g. "Normal:Full"
      std::string data = std::string(NeedBracketIdToString(oldBracket)) + ":" +
                         std::string(NeedBracketIdToString(newBracket));
      Anki::Util::sEvent("needs.bracket_changed",
                         {{DDATA, data.c_str()}},
                         NeedIdToString(static_cast<NeedId>(needIndex)));
    }
  }
}


bool NeedsManager::DeviceHasNeedsState()
{
  return Util::FileUtils::FileExists(kPathToSavedStateFile + kNeedsStateFile);
}

void NeedsManager::PossiblyWriteToDevice()
{
  const Time now = system_clock::now();
  const auto elapsed = now - _needsState._timeLastWritten;
  const auto secsSinceLastSave = duration_cast<seconds>(elapsed).count();
  if (secsSinceLastSave > kMinimumTimeBetweenDeviceSaves_sec)
  {
    _needsState._timeLastWritten = now;
    WriteToDevice(false);
  }
}

void NeedsManager::WriteToDevice(bool stampWithNowTime /* = true */)
{
  const auto startTime = system_clock::now();

  if (stampWithNowTime)
  {
    _needsState._timeLastWritten = system_clock::now();
  }

  Json::Value state;

  state[kStateFileVersionKey] = NeedsState::kDeviceStorageVersion;

  const auto time_s = duration_cast<seconds>(_needsState._timeLastWritten.time_since_epoch()).count();
  state[kDateTimeKey] = Util::numeric_cast<Json::LargestInt>(time_s);

  state[kSerialNumberKey] = _needsState._robotSerialNumber;

  state[kCurNeedsUnlockLevelKey] = _needsState._curNeedsUnlockLevel;
  state[kNumStarsAwardedKey] = _needsState._numStarsAwarded;
  state[kNumStarsForNextUnlockKey] = _needsState._numStarsForNextUnlock;

  for (const auto& need : _needsState._curNeedsLevels)
  {
    const int levelAsInt = Util::numeric_cast<int>((need.second * kNeedLevelStorageMultiplier) + 0.5f);
    state[kCurNeedLevelKey][EnumToString(need.first)] = levelAsInt;
  }
  for (const auto& part : _needsState._partIsDamaged)
  {
    state[kPartIsDamagedKey][EnumToString(part.first)] = part.second;
  }

  const auto timeStarAwarded_s = duration_cast<seconds>(_needsState._timeLastStarAwarded.time_since_epoch()).count();
  state[kTimeLastStarAwardedKey] = Util::numeric_cast<Json::LargestInt>(timeStarAwarded_s);

  const auto midTime = system_clock::now();
  if (!_cozmoContext->GetDataPlatform()->writeAsJson(Util::Data::Scope::Persistent, GetNurtureFolder() + kNeedsStateFile, state))
  {
    PRINT_NAMED_ERROR("NeedsManager.WriteToDevice.WriteStateFailed", "Failed to write needs state file");
  }
  const auto endTime = system_clock::now();
  const auto microsecsMid = duration_cast<microseconds>(endTime - midTime);
  const auto microsecs = duration_cast<microseconds>(endTime - startTime);
  PRINT_CH_INFO(kLogChannelName, "NeedsManager.WriteToDevice",
                "Write to device took %lld microseconds total; %lld microseconds for the actual write",
                microsecs.count(), microsecsMid.count());
}

bool NeedsManager::ReadFromDevice(bool& versionUpdated)
{
  versionUpdated = false;

  Json::Value state;
  if (!_cozmoContext->GetDataPlatform()->readAsJson(kPathToSavedStateFile + kNeedsStateFile, state))
  {
    PRINT_NAMED_ERROR("NeedsManager.ReadFromDevice.ReadStateFailed", "Failed to read %s", kNeedsStateFile.c_str());
    return false;
  }

  int versionLoaded = state[kStateFileVersionKey].asInt();
  if (versionLoaded > NeedsState::kDeviceStorageVersion)
  {
    ANKI_VERIFY(versionLoaded <= NeedsState::kDeviceStorageVersion, "NeedsManager.ReadFromDevice.StateFileVersionIsFuture",
                "Needs state file version read was %d but app thinks latest version is %d",
                versionLoaded, NeedsState::kDeviceStorageVersion);
    return false;
  }

  const seconds durationSinceEpoch_s(state[kDateTimeKey].asLargestInt());
  _needsState._timeLastWritten = time_point<system_clock>(durationSinceEpoch_s);

  _needsState._robotSerialNumber = state[kSerialNumberKey].asUInt();

  _needsState._curNeedsUnlockLevel = state[kCurNeedsUnlockLevelKey].asInt();
  _needsState._numStarsAwarded = state[kNumStarsAwardedKey].asInt();
  _needsState._numStarsForNextUnlock = state[kNumStarsForNextUnlockKey].asInt();

  for (auto& need : _needsState._curNeedsLevels)
  {
    const int levelAsInt = state[kCurNeedLevelKey][EnumToString(need.first)].asInt();
    need.second = (Util::numeric_cast<float>(levelAsInt) / kNeedLevelStorageMultiplier);
  }
  for (auto& part : _needsState._partIsDamaged)
  {
    part.second = state[kPartIsDamagedKey][EnumToString(part.first)].asBool();
  }

  if (versionLoaded >= 2)
  {
    const seconds durationSinceEpochLastStar_s(state[kTimeLastStarAwardedKey].asLargestInt());
    _needsState._timeLastStarAwarded = time_point<system_clock>(durationSinceEpochLastStar_s);
  }
  else
  {
    _needsState._timeLastStarAwarded = Time(); // For older versions, a sensible default
  }

  if (versionLoaded < NeedsState::kDeviceStorageVersion)
  {
    versionUpdated = true;
  }

  _needsState.SetNeedsBracketsDirty();
  _needsState.UpdateCurNeedsBrackets(_needsConfig._needsBrackets);

  return true;
}


void NeedsManager::PossiblyStartWriteToRobot(bool ignoreCooldown /* = false */)
{
  if (_robotStorageState != RobotStorageState::Inactive)
    return;

  if (_robot == nullptr)
    return;

  const Time now = system_clock::now();
  const auto elapsed = now - _timeLastWrittenToRobot;
  const auto secsSinceLastSave = duration_cast<seconds>(elapsed).count();
  if (ignoreCooldown || secsSinceLastSave > kMinimumTimeBetweenRobotSaves_sec)
  {
    _timeLastWrittenToRobot = now;
    StartWriteToRobot();
  }
}

void NeedsManager::StartWriteToRobot()
{
  if (_robot == nullptr)
    return;

  if (!ANKI_VERIFY(_robotStorageState == RobotStorageState::Inactive, "NeedsManager.StartWriteToRobot.RobotStorageConflict",
              "Attempting to write to robot but state is %d", _robotStorageState))
  {
    return;
  }

  if (_robotStorageState != RobotStorageState::Inactive)
    return;

  PRINT_CH_INFO(kLogChannelName, "NeedsManager.StartWriteToRobot", "Writing to robot...");
  const auto startTime = system_clock::now();

  _robotStorageState = RobotStorageState::Writing;

  const auto time_s = duration_cast<seconds>(_timeLastWrittenToRobot.time_since_epoch()).count();
  const auto timeLastWritten = Util::numeric_cast<uint64_t>(time_s);

  std::array<int32_t, MAX_NEEDS> curNeedLevels{};
  for (const auto& need : _needsState._curNeedsLevels)
  {
    const int32_t levelAsInt = Util::numeric_cast<int>((need.second * kNeedLevelStorageMultiplier) + 0.5f);
    curNeedLevels[(int)need.first] = levelAsInt;
  }

  std::array<bool, MAX_REPAIRABLE_PARTS> partIsDamaged{};
  for (const auto& part : _needsState._partIsDamaged)
  {
    partIsDamaged[(int)part.first] = part.second;
  }

  const auto timeLastStarAwarded_s = duration_cast<seconds>(_needsState._timeLastStarAwarded.time_since_epoch()).count();
  const auto timeLastStarAwarded = Util::numeric_cast<uint64_t>(timeLastStarAwarded_s);

  NeedsStateOnRobot stateForRobot(NeedsState::kRobotStorageVersion, timeLastWritten, curNeedLevels,
                                  _needsState._curNeedsUnlockLevel, _needsState._numStarsAwarded,
                                  partIsDamaged, timeLastStarAwarded, _robotOnboardingStageCompleted);

  std::vector<u8> stateVec(stateForRobot.Size());
  stateForRobot.Pack(stateVec.data(), stateForRobot.Size());
  if (!_robot->GetNVStorageComponent().Write(NVStorage::NVEntryTag::NVEntry_NurtureGameData, stateVec.data(), stateVec.size(),
                                           [this, startTime](NVStorage::NVResult res)
                                           {
                                             FinishWriteToRobot(res, startTime);
                                           }))
  {
    PRINT_NAMED_ERROR("NeedsManager.StartWriteToRobot.WriteFailed", "Write failed");
    _robotStorageState = RobotStorageState::Inactive;
  }
}

void NeedsManager::FinishWriteToRobot(const NVStorage::NVResult res, const Time startTime)
{
  ANKI_VERIFY(_robotStorageState == RobotStorageState::Writing, "NeedsManager.FinishWriteToRobot.RobotStorageConflict",
              "Robot storage state should be Writing but instead is %d", _robotStorageState);
  _robotStorageState = RobotStorageState::Inactive;

  auto endTime = system_clock::now();
  auto microsecs = duration_cast<microseconds>(endTime - startTime);
  PRINT_CH_INFO(kLogChannelName, "NeedsManager.FinishWriteToRobot", "Write to robot AFTER CALLBACK took %lld microseconds", microsecs.count());

  if (res < NVStorage::NVResult::NV_OKAY)
  {
    PRINT_NAMED_ERROR("NeedsManager.FinishWriteToRobot.WriteFailed", "Write failed with %s", EnumToString(res));
  }
  else
  {
    // The write was successful
    // Send a message to the game to indicate write was completed??
    //const auto& extInt = _cozmoContext->GetExternalInterface();
    //extInt->GetExternalInterface()->Broadcast(ExternalInterface::MessageEngineToGame(ExternalInterface::RequestSetUnlockResult(id, unlocked)));
  }
}


bool NeedsManager::StartReadFromRobot()
{
  if (!ANKI_VERIFY(_robotStorageState == RobotStorageState::Inactive, "NeedsManager.StartReadFromRobot.RobotStorageConflict",
              "Attempting to read from robot but state is %d", _robotStorageState))
  {
    return false;
  }

  _robotStorageState = RobotStorageState::Reading;

  if (!_robot->GetNVStorageComponent().Read(NVStorage::NVEntryTag::NVEntry_NurtureGameData,
                                          [this](u8* data, size_t size, NVStorage::NVResult res)
                                          {
                                            _robotHadValidNeedsData = FinishReadFromRobot(data, size, res);

                                            InitAfterReadFromRobotAttempt();
                                          }))
  {
    PRINT_NAMED_ERROR("NeedsManager.StartReadFromRobot.ReadFailed", "Failed to start read of needs system robot storage");
    _robotStorageState = RobotStorageState::Inactive;
    return false;
  }

  return true;
}

bool NeedsManager::FinishReadFromRobot(const u8* data, const size_t size, const NVStorage::NVResult res)
{
  ANKI_VERIFY(_robotStorageState == RobotStorageState::Reading, "NeedsManager.FinishReadFromRobot.RobotStorageConflict",
              "Robot storage state should be Reading but instead is %d", _robotStorageState);
  _robotStorageState = RobotStorageState::Inactive;

  if (res < NVStorage::NVResult::NV_OKAY)
  {
    // The tag doesn't exist on the robot indicating the robot is new or has been wiped
    if (res == NVStorage::NVResult::NV_NOT_FOUND)
    {
      PRINT_CH_INFO(kLogChannelName, "NeedsManager.FinishReadFromRobot", "No nurture metagame data on robot");
    }
    else
    {
      PRINT_NAMED_ERROR("NeedsManager.FinishReadFromRobot.ReadFailedFinish", "Read failed with %s", EnumToString(res));
    }
    return false;
  }

  // Read first 4 bytes of data as int32_t; this is the save format version
  const int32_t versionLoaded = static_cast<int32_t>(*data);

  if (versionLoaded > NeedsState::kRobotStorageVersion)
  {
    ANKI_VERIFY(versionLoaded <= NeedsState::kRobotStorageVersion, "NeedsManager.FinishReadFromRobot.StateFileVersionIsFuture",
                "Needs state robot storage version read was %d but app thinks latest version is %d",
                versionLoaded, NeedsState::kRobotStorageVersion);
    return false;
  }

  NeedsStateOnRobot stateOnRobot;

  if (versionLoaded == NeedsState::kRobotStorageVersion)
  {
    stateOnRobot.Unpack(data, size);
  }
  else
  {
    // This is an older version of the robot storage, so the data must be
    // migrated to the new format
    _robotNeedsVersionUpdate = true;

    switch (versionLoaded)
    {
      case 1:
      {
        NeedsStateOnRobot_v01 stateOnRobot_v01;
        stateOnRobot_v01.Unpack(data, size);

        stateOnRobot.version = NeedsState::kRobotStorageVersion;
        stateOnRobot.timeLastWritten = stateOnRobot_v01.timeLastWritten;
        for (int i = 0; i < MAX_NEEDS; i++)
          stateOnRobot.curNeedLevel[i] = stateOnRobot_v01.curNeedLevel[i];
        stateOnRobot.curNeedsUnlockLevel = stateOnRobot_v01.curNeedsUnlockLevel;
        stateOnRobot.numStarsAwarded = stateOnRobot_v01.numStarsAwarded;
        for (int i = 0; i < MAX_REPAIRABLE_PARTS; i++)
          stateOnRobot.partIsDamaged[i] = stateOnRobot_v01.partIsDamaged[i];

        // Version 2 added this variable:
        stateOnRobot.timeLastStarAwarded = 0;
        // Version 3 added this variable
        stateOnRobot.onboardingStageCompleted = 0;
        break;
      }
      case 2:
      {
        NeedsStateOnRobot_v02 stateOnRobot_v02;
        stateOnRobot_v02.Unpack(data, size);
        
        stateOnRobot.version = NeedsState::kRobotStorageVersion;
        stateOnRobot.timeLastWritten = stateOnRobot_v02.timeLastWritten;
        for (int i = 0; i < MAX_NEEDS; i++)
          stateOnRobot.curNeedLevel[i] = stateOnRobot_v02.curNeedLevel[i];
        stateOnRobot.curNeedsUnlockLevel = stateOnRobot_v02.curNeedsUnlockLevel;
        stateOnRobot.numStarsAwarded = stateOnRobot_v02.numStarsAwarded;
        for (int i = 0; i < MAX_REPAIRABLE_PARTS; i++)
          stateOnRobot.partIsDamaged[i] = stateOnRobot_v02.partIsDamaged[i];
        stateOnRobot.timeLastStarAwarded = stateOnRobot_v02.timeLastStarAwarded;
        // Version 3 added this variable
        stateOnRobot.onboardingStageCompleted = 0;
        break;
      }
      default:
      {
        PRINT_CH_DEBUG(kLogChannelName, "NeedsManager.FinishReadFromRobot.UnsupportedOldRobotStorageVersion",
                       "Version %d found on robot but not supported", versionLoaded);
        break;
      }
    }
  }

  // Now initialize _needsStateFromRobot from the loaded NeedsStateOnRobot:

  const seconds durationSinceEpoch_s(stateOnRobot.timeLastWritten);
  _needsStateFromRobot._timeLastWritten = time_point<system_clock>(durationSinceEpoch_s);

  _needsStateFromRobot._curNeedsUnlockLevel = stateOnRobot.curNeedsUnlockLevel;
  _needsStateFromRobot._numStarsAwarded = stateOnRobot.numStarsAwarded;
  _needsStateFromRobot._numStarsForNextUnlock = _starRewardsConfig->GetMaxStarsForLevel(stateOnRobot.curNeedsUnlockLevel);

  for (int i = 0; i < static_cast<int>(NeedId::Count); i++)
  {
    const auto& needId = static_cast<NeedId>(i);
    _needsStateFromRobot._curNeedsLevels[needId] = Util::numeric_cast<float>(stateOnRobot.curNeedLevel[i]) / kNeedLevelStorageMultiplier;
  }

  for (int i = 0; i < RepairablePartIdNumEntries; i++)
  {
    const auto& pardId = static_cast<RepairablePartId>(i);
    _needsStateFromRobot._partIsDamaged[pardId] = stateOnRobot.partIsDamaged[i];
  }

  const seconds durationSinceEpochLastStar_s(stateOnRobot.timeLastStarAwarded);
  _needsStateFromRobot._timeLastStarAwarded = time_point<system_clock>(durationSinceEpochLastStar_s);

  // Other initialization for things that do not come from storage:
  _needsStateFromRobot._robotSerialNumber = _robot->GetBodySerialNumber();
  _needsStateFromRobot._needsConfig = &_needsConfig;
  _needsStateFromRobot._starRewardsConfig = _starRewardsConfig;
  _needsStateFromRobot._rng = _cozmoContext->GetRandom();
  _needsStateFromRobot.SetNeedsBracketsDirty();
  _needsStateFromRobot.UpdateCurNeedsBrackets(_needsConfig._needsBrackets);
  _robotOnboardingStageCompleted = stateOnRobot.onboardingStageCompleted;
  
  return true;
}

#if ANKI_DEV_CHEATS
void NeedsManager::DebugFillNeedMeters()
{
  _needsState.SetPrevNeedsBrackets();

  _needsState.DebugFillNeedMeters();
  SendNeedsStateToGame();
  UpdateStarsState();
}

void NeedsManager::DebugGiveStar()
{
  PRINT_CH_INFO(kLogChannelName, "NeedsManager.DebugGiveStar","");
  DebugCompleteDay();
  UpdateStarsState(true);
}

void NeedsManager::DebugCompleteDay()
{
  // Push the last given star back 24 hours
  system_clock::time_point now = system_clock::now();
  std::time_t yesterdayTime = system_clock::to_time_t(now - hours(25));
  _needsState._timeLastStarAwarded = system_clock::from_time_t(yesterdayTime);

  PRINT_CH_INFO(kLogChannelName, "NeedsManager.DebugCompleteDay","");
}

void NeedsManager::DebugResetNeeds()
{
  if (_robot != nullptr)
  {
    _needsState.Init(_needsConfig, _robot->GetBodySerialNumber(), _starRewardsConfig, _cozmoContext->GetRandom());
    _robotHadValidNeedsData = false;
    _deviceHadValidNeedsData = false;
    InitAfterReadFromRobotAttempt();
  }
}

void NeedsManager::DebugCompleteAction(const char* actionName)
{
  const NeedsActionId actionId = NeedsActionIdFromString(actionName);
  RegisterNeedsActionCompleted(actionId);
}

void NeedsManager::DebugPredictActionResult(const char* actionName)
{
  const NeedsActionId actionId = NeedsActionIdFromString(actionName);
  NeedsState state;
  PredictNeedsActionResult(actionId, state);
}

void NeedsManager::DebugPauseDecayForNeed(const char* needName)
{
  DebugImplPausing(needName, true, true);
}

void NeedsManager::DebugPauseActionsForNeed(const char *needName)
{
  DebugImplPausing(needName, false, true);
}

void NeedsManager::DebugUnpauseDecayForNeed(const char *needName)
{
  DebugImplPausing(needName, true, false);
}

void NeedsManager::DebugUnpauseActionsForNeed(const char *needName)
{
  DebugImplPausing(needName, false, false);
}

void NeedsManager::DebugImplPausing(const char* needName, const bool isDecay, const bool isPaused)
{
  // First, make a copy of all the current pause flags
  std::vector<bool> decayPause;
  decayPause.reserve(_isDecayPausedForNeed.size());
  for (int i = 0; i < _isDecayPausedForNeed.size(); i++)
  {
    decayPause.push_back(_isDecayPausedForNeed[i]);
  }

  std::vector<bool> actionPause;
  actionPause.reserve(_isActionsPausedForNeed.size());
  for (int i = 0; i < _isActionsPausedForNeed.size(); i++)
  {
    actionPause.push_back(_isActionsPausedForNeed[i]);
  }

  // Now set or clear the single flag in question
  const NeedId needId = NeedIdFromString(needName);
  const int needIndex = static_cast<int>(needId);
  if (isDecay)
    decayPause[needIndex] = isPaused;
  else
    actionPause[needIndex] = isPaused;

  // Finally, set the flags for whether to discard decay
  // Note:  Just hard coding for now
  std::vector<bool> decayDiscardAfterUnpause;
  const auto numNeeds = static_cast<size_t>(NeedId::Count);
  decayDiscardAfterUnpause.reserve(numNeeds);
  for (int i = 0; i < numNeeds; i++)
  {
    decayDiscardAfterUnpause.push_back(true);
  }

  ExternalInterface::SetNeedsPauseStates m(std::move(decayPause),
                                           std::move(decayDiscardAfterUnpause),
                                           std::move(actionPause));
  HandleMessage(m);
}

void NeedsManager::DebugSetNeedLevel(const NeedId needId, const float level)
{
  _needsState.SetPrevNeedsBrackets();

  const float delta = level - _needsState._curNeedsLevels[needId];

  if ((needId == NeedId::Repair) && (delta > 0.0f))
  {
    // For the repair need, if we're going UP, we also need to repair enough
    // parts as needed so that the new level will be within the correct
    // threshold for 'number of broken parts'.
    // We don't need to do this when going DOWN because ApplyDelta will
    // break parts for us.
    int numDamagedParts = _needsState.NumDamagedParts();
    int newNumDamagedParts = _needsState.NumDamagedPartsForRepairLevel(level);
    while (newNumDamagedParts < numDamagedParts)
    {
      _needsState._partIsDamaged[_needsState.PickPartToRepair()] = false;
      numDamagedParts--;
    }
  }

  NeedDelta needDelta(delta, 0.0f, NeedsActionId::NoAction);
  if (_needsState.ApplyDelta(needId, needDelta, NeedsActionId::NoAction))
  {
    StartFullnessCooldownForNeed(needId);
  }

  SendNeedsStateToGame();

  UpdateStarsState();

  DetectBracketChangeForDas();

  PossiblyWriteToDevice();
  PossiblyStartWriteToRobot();
}

void NeedsManager::DebugPassTimeMinutes(const float minutes)
{
  const float timeElaspsed_s = minutes * 60.0f;
  for (int needIndex = 0; needIndex < (size_t)NeedId::Count; needIndex++)
  {
    if (!_isDecayPausedForNeed[needIndex])
    {
      _lastDecayUpdateTime_s[needIndex] -= timeElaspsed_s;
    }
    if (_timeWhenCooldownOver_s[needIndex] != 0.0f)
    {
      _timeWhenCooldownOver_s[needIndex] -= timeElaspsed_s;
      _timeWhenCooldownStarted_s[needIndex] -= timeElaspsed_s;
    }
  }

  const bool connected = _robot != nullptr;
  ApplyDecayAllNeeds(connected);

  SendNeedsStateToGame(NeedsActionId::Decay);

  WriteToDevice();
}
#endif

} // namespace Cozmo
} // namespace Anki


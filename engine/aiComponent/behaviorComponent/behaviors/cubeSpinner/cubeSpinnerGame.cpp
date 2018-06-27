/**
* File: cubeSpinnerGame.cpp
*
* Author: Kevin M. Karol
* Created: 2018-06-21
*
* Description: Manages the phases/light overlays for the cube spinner game
*
* Copyright: Anki, Inc. 2018
*
**/

#include "engine/aiComponent/behaviorComponent/behaviors/cubeSpinner/cubeSpinnerGame.h"

#include "coretech/common/engine/jsonTools.h"
#include "coretech/common/engine/math/point_impl.h"
#include "coretech/common/engine/utils/timer.h"
#include "engine/activeObject.h"
#include "engine/blockWorld/blockWorld.h"
#include "engine/components/backpackLights/backpackLightComponent.h"
#include "engine/components/cubes/cubeCommsComponent.h"
#include "engine/components/cubes/cubeLights/cubeLightAnimation.h"
#include "engine/components/cubes/cubeLights/cubeLightAnimationHelpers.h"
#include "engine/components/cubes/cubeLights/cubeLightComponent.h"
#include "util/console/consoleInterface.h"
#include "util/logging/logging.h"

namespace Anki {
namespace Cozmo {

namespace {
////////
// Light Keys
////////
const char* kStartGameCubeLightsKey   = "startGameCubeLights";
const char* kPlayerErrorCubeLightsKey = "playerErrorCubeLights";
const char* kLightsMapKey             = "lightMap";

// light map keys
const char* kDebugColorNameKey = "debugColorName";
const char* kBackpackLightsKey = "backpackLights";
const char* kCubeLightsKey     = "cubeLights";

// cube light keys
const char* kCubeCelebrationKey = "celebration";
const char* kCubeCycleKey       = "cycle";
const char* kCubeLockInKey      = "lockIn";
const char* kCubeLockedPulseKey = "lockedPulse";
const char* kCubeLockedKey      = "locked";

// backpack light keys
const char* kBackpackCelebrationKey = "celebration";
const char* kHoldTargetKey          = "holdTarget";
const char* kSelectTargetKey        = "selectTarget";

////////
// Game Config Keys
////////
const char* kMinWrongKey = "minWrongColorsBeetweenTargetPerRound";
const char* kMaxWrongKey = "maxWrongColorsBeetweenTargetPerRound";
const char* kSpeedMultipliersKey = "speedMultipliers";
const char* kGetInLengthKey = "getInLength_ms";
const char* kTimePerLEDKey = "timePerLED_ms";

}

CubeAnimationTrigger CubeTriggerFromJson(const Json::Value& trigger){
  return CubeAnimationTriggerFromString(trigger.asString());
}

BackpackAnimationTrigger BackpackTriggerFromJson(const Json::Value& trigger){
  return BackpackAnimationTriggerFromString(trigger.asString());
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
CubeSpinnerGame::LightMapEntry::LightMapEntry(const Json::Value& entryConfig)
{
  debugColorName = entryConfig[kDebugColorNameKey].asString();
  const Json::Value& backpackLights = entryConfig[kBackpackLightsKey];
  backpackCelebrationTrigger  = BackpackTriggerFromJson(backpackLights[kBackpackCelebrationKey]);
  backpackHoldTargetTrigger   = BackpackTriggerFromJson(backpackLights[kHoldTargetKey]);
  backpackSelectTargetTrigger = BackpackTriggerFromJson(backpackLights[kSelectTargetKey]);

  const Json::Value& cubeLights = entryConfig[kCubeLightsKey];
  cubeCelebrationTrigger = CubeTriggerFromJson(cubeLights[kCubeCelebrationKey]);
  cubeCycleTrigger       = CubeTriggerFromJson(cubeLights[kCubeCycleKey]);
  cubeLockInTrigger      = CubeTriggerFromJson(cubeLights[kCubeLockInKey]);
  cubeLockedPulseTrigger = CubeTriggerFromJson(cubeLights[kCubeLockedPulseKey]);
  cubeLockedTrigger      = CubeTriggerFromJson(cubeLights[kCubeLockedKey]);
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
CubeSpinnerGame::GameLightConfig::GameLightConfig(const Json::Value& entryConfig)
{
  startGameCubeTrigger   = CubeTriggerFromJson(entryConfig[kStartGameCubeLightsKey]);
  playerErrorCubeTrigger = CubeTriggerFromJson(entryConfig[kPlayerErrorCubeLightsKey]);
  const Json::Value& lightsMap = entryConfig[kLightsMapKey];
  for(const auto& entry : lightsMap){
    lights.push_back(LightMapEntry(entry));
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
CubeSpinnerGame::GameSettingsConfig::GameSettingsConfig(const Json::Value& settingsConfig)
{
  const std::string debugName = "CubeSpinnerGame.GameSettingsConfig.KeyIssue.";
  getInLength_ms = JsonTools::ParseUInt32(settingsConfig, kGetInLengthKey, (debugName + "GetInLength").c_str());
  timePerLED_ms  = JsonTools::ParseUInt32(settingsConfig, kTimePerLEDKey, (debugName + "TimePerLED").c_str());
  // Speed multipliers array
  if(settingsConfig[kSpeedMultipliersKey].isArray()){
    int i = 0;
    for(const auto& entry: settingsConfig[kSpeedMultipliersKey]){
      if(i >= CubeLightAnimation::kNumCubeLEDs){
        PRINT_NAMED_ERROR("CubeSpinnerGame.GameSettingsConfig.TooManyMultipliers", "");
        break;
      }
      speedMultipliers[i] = entry.asFloat();
      i++;
    }
  }

  // Min wrong array
  if(settingsConfig[kMinWrongKey].isArray()){
    int i = 0;
    for(const auto& entry: settingsConfig[kMinWrongKey]){
      if(i >= CubeLightAnimation::kNumCubeLEDs){
        PRINT_NAMED_ERROR("CubeSpinnerGame.GameSettingsConfig.TooManyWrongColorsMin", "");
        break;
      }
      minWrongColorsPerRound[i] = entry.asInt();
      i++;
    }
  }

  // Max wrong array
  if(settingsConfig[kMaxWrongKey].isArray()){
    int i = 0;
    for(const auto& entry: settingsConfig[kMaxWrongKey]){
      if(i >= CubeLightAnimation::kNumCubeLEDs){
        PRINT_NAMED_ERROR("CubeSpinnerGame.GameSettingsConfig.TooManyWrongColorsMax", "");
        break;
      }
      maxWrongColorsPerRound[i] = entry.asInt();
      i++;
    }
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
CubeSpinnerGame::CubeSpinnerGame(const Json::Value& gameConfig,
                                 const Json::Value& lightConfigs,
                                 CubeCommsComponent& cubeCommsComponent,
                                 CubeLightComponent& cubeLightComponent,
                                 BackpackLightComponent& backpackLightComponent,
                                 BlockWorld& blockWorld,
                                 Util::RandomGenerator& rng)
: _settingsConfig(gameConfig)
, _lightsConfig(lightConfigs)
, _cubeCommsComponent(cubeCommsComponent)
, _cubeLightComponent(cubeLightComponent)
, _backpackLightComponent(backpackLightComponent)
, _blockWorld(blockWorld)
, _rng(rng)
{

}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
CubeSpinnerGame::~CubeSpinnerGame()
{

}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void CubeSpinnerGame::RequestStartNewGame(GameReadyCallback callback)
{
  _currentGame.targetObject.SetToUnknown();
  if(_cubeCommsComponent.IsConnectedToCube()){
    const bool res = ResetGame();
    callback(res, _currentGame.targetObject);
  }else{
    auto internalCallback = [this, callback](bool success){
      if(success){
        success = ResetGame();
      }
      callback(success, _currentGame.targetObject);
    };
    if(!_cubeCommsComponent.RequestConnectToCube(internalCallback)){
      callback(false, _currentGame.targetObject);
    }
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void CubeSpinnerGame::StopGame()
{
  _backpackLightComponent.ClearAllBackpackLightConfigs();
  _cubeLightComponent.StopLightAnimAndResumePrevious(_currentGame.currentCubeHandle);
  _cubeCommsComponent.RequestDisconnectFromCube(100);
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool CubeSpinnerGame::ResetGame()
{
  _backpackLightComponent.ClearAllBackpackLightConfigs();
  _cubeLightComponent.StopLightAnimAndResumePrevious(_currentGame.currentCubeHandle);

  _currentGame = CurrentGame();
  _currentGame.targetLightIdx = GetNewLightColorIdx(true);
  const size_t currTick = BaseStationTimer::getInstance()->GetTickCount();
  _currentGame.lastUpdateTick = currTick;
  _currentGame.baseLightPattern = CubeLightAnimation::GetLightsOffPattern();
  _currentGame.baseLightPattern.canBeOverridden = false;
  
  BlockWorldFilter filter;
  filter.AddAllowedFamily(ObjectFamily::LightCube);
  const ActiveObject* obj = _blockWorld.FindConnectedActiveMatchingObject(filter);
  if(obj != nullptr){
    _currentGame.targetObject = obj->GetID();
  }else{
    return false;
  }
  
  return true;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void CubeSpinnerGame::Update()
{
  if(_currentGame.lastTimePhaseChanged_ms == kGameHasntStartedTick){
    TransitionToGamePhase(GamePhase::GameGetIn);
  }else if(ANKI_DEV_CHEATS){
    const size_t currTick = BaseStationTimer::getInstance()->GetTickCount();
    ANKI_VERIFY(_currentGame.lastUpdateTick == (currTick-1), 
                "CubeSpinnerGame.Update.TickCountIssue",
                "Game was last updated on tick %zu, but is now being called on tick %zu",
                _currentGame.lastUpdateTick, currTick);
    _currentGame.lastUpdateTick = currTick;
  }

  CheckForGamePhaseTransitions();

  if(_currentGame.gamePhase == GamePhase::CycleColorsUntilTap){
    CheckForNextLEDRotation();
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void CubeSpinnerGame::CheckForNextLEDRotation()
{
  const auto currTime_ms = BaseStationTimer::getInstance()->GetCurrentTimeStamp();
  if(_currentGame.timeNextAdvanceToLED_ms < currTime_ms){
    if(_currentGame.currentCycleLEDIdx < (CubeLightAnimation::kNumCubeLEDs - 1)){
      _currentGame.currentCycleLEDIdx++;
    }else{
      _currentGame.currentCycleLEDIdx = 0;
      _currentGame.currentCycleLightIdx = GetNewLightColorIdx();
    }
    ComposeAndSendLights();
    _currentGame.timeNextAdvanceToLED_ms = currTime_ms + MillisecondsBetweenLEDRotations();
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void CubeSpinnerGame::ComposeAndSendLights()
{
  // The composed pattern contains all previous locked lights
  auto composedPattern = _currentGame.baseLightPattern;

  CubeLightAnimation::LightPattern currentCyclePattern = GetCurrentCyclePattern();

  std::set<uint8_t> ledsToOverwrite;
  ledsToOverwrite.insert(GetCurrentCycleIdx());
  OverwriteLEDs(currentCyclePattern, composedPattern, ledsToOverwrite);

  // Turn the pattern into an animation and play it
  std::list<CubeLightAnimation::LightPattern> anim;
  anim.push_back(composedPattern);
  PlayCubeAnimation(anim);
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void CubeSpinnerGame::LockCurrentLightsIn()
{
  _currentGame.lightsLocked[GetCurrentCycleIdx()] = true;
  CubeLightAnimation::LightPattern currentCyclePattern = GetCurrentLockPattern();

  std::set<uint8_t> ledsToOverwrite;
  ledsToOverwrite.insert(GetCurrentCycleIdx());
  OverwriteLEDs(currentCyclePattern, _currentGame.baseLightPattern, ledsToOverwrite);
  
  _currentGame.lastLEDLockedIdx = _currentGame.currentCycleLEDIdx;
  // start one offset from the last light locked in
  _currentGame.currentCycleLightIdx = GetNewLightColorIdx();
  _currentGame.currentCycleLEDIdx = 0;

}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
CubeLightAnimation::LightPattern CubeSpinnerGame::GetCurrentCyclePattern() const
{
  // Build the new rotation pattern on top
  CubeLightAnimation::Animation* anim = nullptr;
  if(IsCurrentCycleIdxLocked()){
    anim = _cubeLightComponent.GetAnimation(_lightsConfig.lights[_currentGame.targetLightIdx].cubeLockedPulseTrigger);
  }else{
    anim = _cubeLightComponent.GetAnimation(_lightsConfig.lights[_currentGame.currentCycleLightIdx].cubeCycleTrigger);
  }
  
  auto copyAnim = *anim;
  auto copyPattern = copyAnim.front();

  CubeLightAnimation::RotateLightPatternCounterClockwise(copyPattern, GetCurrentCycleIdx());
  return copyPattern;
}

  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
CubeLightAnimation::LightPattern CubeSpinnerGame::GetCurrentLockPattern() const
{
  // Build the new rotation pattern on top
  auto copyAnim = *_cubeLightComponent.GetAnimation(_lightsConfig.lights[_currentGame.currentCycleLightIdx].cubeLockedTrigger);
  auto copyPattern = copyAnim.front();
  
  CubeLightAnimation::RotateLightPatternCounterClockwise(copyPattern, GetCurrentCycleIdx());
  return copyPattern;
}

  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uint8_t CubeSpinnerGame::GetCurrentCycleIdx() const
{
  return (_currentGame.currentCycleLEDIdx + _currentGame.lastLEDLockedIdx) % CubeLightAnimation::kNumCubeLEDs;
}

  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool CubeSpinnerGame::IsCurrentCycleIdxLocked() const
{
  return _currentGame.lightsLocked[GetCurrentCycleIdx()];
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uint32_t CubeSpinnerGame::MillisecondsBetweenLEDRotations() const
{
  return (_settingsConfig.timePerLED_ms/_settingsConfig.speedMultipliers[GetRoundNumber()]);
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uint8_t CubeSpinnerGame::GetRoundNumber() const
{
  auto i = 0;
  for(const auto& entry : _currentGame.lightsLocked){
    if(entry){
      i++;
    }
  }
  return i;
}



// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void CubeSpinnerGame::LockNow()
{
  LockResult result = LockResult::Error;

  const bool colorsMatch = (_currentGame.targetLightIdx == _currentGame.currentCycleLightIdx);
  const bool notAlreadyLocked = !IsCurrentCycleIdxLocked();

  if(colorsMatch && notAlreadyLocked){
    LockCurrentLightsIn();
    if(GetRoundNumber() == CubeLightAnimation::kNumCubeLEDs){
      result = LockResult::Complete;
      TransitionToGamePhase(GamePhase::Celebration);
    }else{
      result = LockResult::Locked;
    }
  }else{
    result = LockResult::Error;
    TransitionToGamePhase(GamePhase::ErrorTap);
  }

  for(auto& callback: _lightLockedCallbacks){
    callback(result);
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void CubeSpinnerGame::CheckForGamePhaseTransitions()
{
  const auto currTime_ms = BaseStationTimer::getInstance()->GetCurrentTimeStamp();
  switch(_currentGame.gamePhase){
    case GamePhase::GameGetIn:{
      if(currTime_ms > (_currentGame.lastTimePhaseChanged_ms + _settingsConfig.getInLength_ms)){
        TransitionToGamePhase(GamePhase::CycleColorsUntilTap);
      }
      break;
    }
    case GamePhase::CycleColorsUntilTap:{
      break;
    }
    case GamePhase::SuccessfulTap:{
      break;
    }
    case GamePhase::ErrorTap:{
      break;
    }
    case GamePhase::Celebration:{
      break;
    }
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void CubeSpinnerGame::TransitionToGamePhase(GamePhase phase)
{
  const auto& targetLightEntry = _lightsConfig.lights[_currentGame.targetLightIdx];
  switch(phase){
    case GamePhase::GameGetIn:{
      auto* anim = _cubeLightComponent.GetAnimation(_lightsConfig.startGameCubeTrigger);
      PlayCubeAnimation(*anim);
      break;
    }
    case GamePhase::CycleColorsUntilTap:{
      const bool shouldLoop = true;
      _backpackLightComponent.SetBackpackAnimation(targetLightEntry.backpackHoldTargetTrigger, shouldLoop);
      // Set the target light as the first cycle
      _currentGame.currentCycleLightIdx = _currentGame.targetLightIdx;
      break;
    }
    case GamePhase::SuccessfulTap:{
      break;
    }
    case GamePhase::ErrorTap:{
      auto* anim = _cubeLightComponent.GetAnimation(_lightsConfig.playerErrorCubeTrigger);
      PlayCubeAnimation(*anim);
      break;
    }
    case GamePhase::Celebration:{
      const bool shouldLoop = false;
      _backpackLightComponent.SetBackpackAnimation(targetLightEntry.backpackCelebrationTrigger, shouldLoop);
      break;
    }
  }
  _currentGame.gamePhase = phase;
  _currentGame.lastTimePhaseChanged_ms = BaseStationTimer::getInstance()->GetCurrentTimeStamp();
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uint8_t CubeSpinnerGame::GetNewLightColorIdx(bool forTargetLight)
{ 
  if(forTargetLight){
    return _rng.RandInt(static_cast<int>(_lightsConfig.lights.size()));
  }else{
    if(_currentGame.numberOfCyclesTillNextCorrectLight <= 0){
      _currentGame.numberOfCyclesTillNextCorrectLight = _rng.RandIntInRange(_settingsConfig.minWrongColorsPerRound[GetRoundNumber()], 
                                                                            _settingsConfig.maxWrongColorsPerRound[GetRoundNumber()]);
      return _currentGame.targetLightIdx;
    }else{
      _currentGame.numberOfCyclesTillNextCorrectLight--;
      uint8_t idx = _rng.RandInt(static_cast<int>(_lightsConfig.lights.size()));
      int safety = 0;
      while(idx == _currentGame.targetLightIdx){
        idx = _rng.RandInt(static_cast<int>(_lightsConfig.lights.size()));
        if(safety > 500000){
          break;
        }
        safety++;
      }
      return idx;
    }
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void CubeSpinnerGame::PlayCubeAnimation(CubeLightAnimation::Animation& animToPlay)
{
  if(_currentGame.hasSentLightPattern){
    _cubeLightComponent.StopAndPlayLightAnim(_currentGame.targetObject, _currentGame.currentCubeHandle, animToPlay, "CubeSpinnerGameLights");
  }else{
    _currentGame.hasSentLightPattern = true;
    _cubeLightComponent.PlayLightAnim(_currentGame.targetObject, animToPlay, {}, "CubeSpinnerGame.LightAnim", _currentGame.currentCubeHandle);
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void CubeSpinnerGame::GetGameSnapshot(bool& areLightsCycling, uint8_t& currentLitLEDIdx, bool& isCurrentLightTarget, 
                                      LightsLocked& lightsLocked, TimeStamp_t& timeUntilNextRotation) const
{
  areLightsCycling = (_currentGame.gamePhase == GamePhase::CycleColorsUntilTap);
  currentLitLEDIdx = GetCurrentCycleIdx();
  isCurrentLightTarget = (_currentGame.targetLightIdx == _currentGame.currentCycleLightIdx);
  lightsLocked = _currentGame.lightsLocked;
  const auto currTime_ms = BaseStationTimer::getInstance()->GetCurrentTimeStamp();
  const auto timeTillNextRotation = (_currentGame.timeNextAdvanceToLED_ms > currTime_ms) ? 
                                    _currentGame.timeNextAdvanceToLED_ms - currTime_ms : 0;
  timeUntilNextRotation = timeTillNextRotation ;
}


} // namespace Cozmo
} // namespace Anki

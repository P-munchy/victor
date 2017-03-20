/**
 * File: bodyLightComponent.h
 *
 * Author: Al Chaussee
 * Created: 1/23/2017
 *
 * Description: Manages various lights on Cozmo's body. 
 *              Currently this includes the backpack lights and headlight.
 *
 * Copyright: Anki, Inc. 2017
 *
 **/

#ifndef __Anki_Cozmo_Basestation_Components_BodyLightComponent_H__
#define __Anki_Cozmo_Basestation_Components_BodyLightComponent_H__

#include "anki/common/types.h"
#include "anki/cozmo/basestation/components/bodyLightComponentTypes.h"
#include "clad/types/ledTypes.h"
#include "json/json.h"
#include "util/helpers/noncopyable.h"
#include "util/signals/simpleSignal_fwd.h"

#include <array>
#include <list>
#include <map>
#include <memory>

namespace Anki {
namespace Cozmo {

using BackpackLEDArray = std::array<u32,(size_t)LEDId::NUM_BACKPACK_LEDS>;
struct BackpackLights {
  BackpackLEDArray onColors;
  BackpackLEDArray offColors;
  BackpackLEDArray onPeriod_ms;
  BackpackLEDArray offPeriod_ms;
  BackpackLEDArray transitionOnPeriod_ms;
  BackpackLEDArray transitionOffPeriod_ms;
  std::array<s32,(size_t)LEDId::NUM_BACKPACK_LEDS> offset;
};

class Robot;
class CozmoContext;
class BackpackLightAnimationContainer;

class BodyLightComponent : private Util::noncopyable
{
public:
  BodyLightComponent(Robot& robot, const CozmoContext* context);
  
  void Update();
  
  void SetHeadlight(bool on);
  
  template<typename T>
  void HandleMessage(const T& msg);
  
  static const BackpackLights& GetOffBackpackLights();
  
  void ClearAllBackpackLightConfigs() { _backpackLightMap.clear(); }
  
  // Allows the caller to start a looping light pattern on Cozmo's backpack and get a handle to cancel it later.
  // Note that the source specified will be used in determining which pattern should be used when multiple have
  // been triggered.
  void StartLoopingBackpackLights(BackpackLights lights, BackpackLightSource source, BackpackLightDataLocator& lightLocator_out);
  
  // Related to the above start looping call, allows the caller to cancel a currently looping light pattern
  bool StopLoopingBackpackLights(const BackpackLightDataLocator& lightDataLocator);
  
  // General purpose call to set backpack lights. The light pattern will persist until this function is called again.
  // Uses a private source value with lower priority than the types specified in the above StartLooping call.
  void SetBackpackLights(const BackpackLights& lights);
  
private:
  
  Robot&                          _robot;
  
  std::list<Signal::SmartHandle>  _eventHandles;

  // Contains overall mapping of light config sources to list of configurations
  BackpackLightMap                _backpackLightMap;
  
  // Reference to the most recently used light configuration
  BackpackLightDataRef            _curBackpackLightConfig;
  
  // Locator handle for the shared light configuration associated with SetBackpackLights above
  BackpackLightDataLocator        _sharedLightConfig{};
  
  // Reference to the container of backpack light animations
  BackpackLightAnimationContainer& _backpackLightAnimations;
  
  enum class BackpackLightsState {
    OffCharger,
    Charging,
    Charged,
    BadCharger,
  };
  
  BackpackLightsState             _curBackpackChargeState = BackpackLightsState::OffCharger;
  
  Result SetBackpackLightsInternal(const BackpackLights& lights);
  void StartLoopingBackpackLightsInternal(BackpackLights lights, BackpackLightSourceType source, BackpackLightDataLocator& lightLocator_out);
  
  const char* StateToString(const BackpackLightsState& state) const;
  
  void UpdateChargingLightConfig();
  static std::vector<BackpackLightSourceType> GetLightSourcePriority();
  BackpackLightDataRef GetBestLightConfig();
  
};

}
}

#endif


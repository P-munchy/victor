/**
 * File: cubeLightAnimationHelpers.h
 *
 * Author: Kevin M. Karol
 * Created: 6/19/18
 *
 * Description: Functions that help build/modify cube light animations
 *
 * Copyright: Anki, Inc. 2018
 *
 **/

#ifndef __Anki_Cozmo_Basestation_Components_CubeLightAnimationHelpers_H__
#define __Anki_Cozmo_Basestation_Components_CubeLightAnimationHelpers_H__

#include "engine/components/cubes/cubeLights/cubeLightAnimation.h"

#include "util/helpers/templateHelpers.h"

#include <set>

namespace Anki {
namespace Cozmo {
namespace CubeLightAnimation {

static constexpr int kNumCubeLEDs = Util::EnumToUnderlying(CubeConstants::NUM_CUBE_LEDS);

Json::Value AnimationToJSON(const std::string& animName, const Animation& animation);
bool ParseCubeAnimationFromJson(const std::string& animName, const Json::Value& jsonRoot, Animation& outAnimation);
bool ParseJsonToPattern(const Json::Value& json, LightPattern& pattern);


void RotateLightPatternCounterClockwise(LightPattern& pattern, uint8_t positionsToRotate = 1);
void OverwriteLEDs(const LightPattern& source, LightPattern& toOverwrite, const std::set<uint8_t> ledsToOverwrite);

// Returns a light object which will turn off all lights (alpha value is required)
const ObjectLights& GetLightsOffObjectLights();
const LightPattern& GetLightsOffPattern();

} // namespace CubeLightAnimation
} // namespace Cozmo
} // namespace Anki

#endif // __Anki_Cozmo_Basestation_Components_CubeLightAnimationHelpers_H__

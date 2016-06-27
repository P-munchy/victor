/**
 * File: AnimationTriggerHelpers
 *
 * Author: Molly Jameson
 * Created: 05/16/16
 *
 * Description: Helper functions for dealing with CLAD generated AnimationTrigger types
 *
 * Copyright: Anki, Inc. 2016
 *
 **/


#include "anki/cozmo/basestation/events/animationTriggerHelpers.h"
#include "util/enums/stringToEnumMapper.hpp"
#include "anki/common/basestation/jsonTools.h"
#include "util/logging/logging.h"


namespace Anki {
  
namespace Cozmo {

  
IMPLEMENT_ENUM_INCREMENT_OPERATORS(AnimationTrigger);


// One global instance, created at static initialization on app launch
static Anki::Util::StringToEnumMapper<AnimationTrigger> gStringToAnimationTriggerMapper;

// Unlike other Enums to string, this will assert on fails by default
AnimationTrigger AnimationTriggerFromString(const char* inString, bool assertOnInvalidEnum )
{
  return gStringToAnimationTriggerMapper.GetTypeFromString(inString,assertOnInvalidEnum);
}

bool IsAnimationTrigger(const char* inString)
{
  return gStringToAnimationTriggerMapper.HasType(inString);
}

} // namespace Cozmo
  
namespace JsonTools
{
  // Will not set anything if no value is found, however if something is filled in that key,
  // Will assert error if not found in list.
  bool GetValueOptional(const Json::Value& config, const std::string& key, Cozmo::AnimationTrigger& value)
  {
    const Json::Value& child(config[key]);
    if(child.isNull())
      return false;
    std::string str = GetValue<std::string>(child);
    value = Cozmo::AnimationTriggerFromString(str.c_str());
    
    return true;
  }
}
} // namespace Anki


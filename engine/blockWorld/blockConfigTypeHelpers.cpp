/**
 * File: blockConfigTypeHelpers.cpp
 *
 * Author: Kevin M. Karol
 * Created: 10/26/16
 *
 * Description: Helper functions for dealing with BlockConfigurations::ConfigurationType
 *
 * Copyright: Anki, Inc. 2016
 *
 **/


#include "engine/blockWorld/blockConfigTypeHelpers.h"
#include "engine/blockWorld/blockConfiguration.h"
#include "util/enums/stringToEnumMapper.hpp"
#include "anki/common/basestation/jsonTools.h"
#include "util/logging/logging.h"


namespace Anki {
namespace Cozmo {
namespace BlockConfigurations{
  
IMPLEMENT_ENUM_INCREMENT_OPERATORS(ConfigurationType);


// One global instance, created at static initialization on app launch
static Anki::Util::StringToEnumMapper<ConfigurationType> gStringToBlockConfigurationMapper;

// Unlike other Enums to string, this will assert on fails by default
ConfigurationType BlockConfigurationFromString(const char* inString, bool assertOnInvalidEnum )
{
  return gStringToBlockConfigurationMapper.GetTypeFromString(inString,assertOnInvalidEnum);
}

bool IsBlockConfiguration(const char* inString)
{
  return gStringToBlockConfigurationMapper.HasType(inString);
}
  
const char* EnumToString(const ConfigurationType t)
{
  switch(t){
    case ConfigurationType::StackOfCubes:
      return "StackOfCubes";
    case ConfigurationType::PyramidBase:
      return "PyramidBase";
    case ConfigurationType::Pyramid:
      return "Pyramid";
    case ConfigurationType::Count:
      DEV_ASSERT_MSG(false, "BlockConfigTypeHelpers.EnumToString.InvalidString",
                     "Attempted to convert unknown value %d to string", t);
      return nullptr;
  }
}


} // namespace BlockConfigurations
} // namespace Cozmo
  
namespace JsonTools
{
  // Will not set anything if no value is found, however if something is filled in that key,
  // Will assert error if not found in list.
  bool GetValueOptional(const Json::Value& config, const std::string& key, Cozmo::BlockConfigurations::ConfigurationType& value)
  {
    const std::string& str = ParseString(config, key.c_str(), "test");
    value = Cozmo::BlockConfigurations::BlockConfigurationFromString(str.c_str());
    
    return true;
  }
}
} // namespace Anki


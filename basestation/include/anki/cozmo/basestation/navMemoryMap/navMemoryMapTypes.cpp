/**
 * File: navMemoryMapTypes.cpp
 *
 * Author: Raul
 * Date:   01/11/2016
 *
 * Description: Type definitions for the navMemoryMap.
 *
 * Copyright: Anki, Inc. 2015
 **/
#include "navMemoryMapTypes.h"
#include "util/logging/logging.h"

namespace Anki {
namespace Cozmo {
namespace NavMemoryMapTypes {

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool ExpectsAdditionalData(EContentType type)
{
  ASSERT_NAMED(type != EContentType::_Count, "NavMemoryMapTypes.ExpectsAdditionalData.UsingControlTypeIsNotAllowed");
  
  // using switch to force at compilation type to decide on new types
  switch(type)
  {
    case EContentType::Unknown:
    case EContentType::ClearOfObstacle:
    case EContentType::ClearOfCliff:
    case EContentType::ObstacleCube:
    case EContentType::ObstacleCharger:
    case EContentType::ObstacleUnrecognized:
    case EContentType::InterestingEdge:
    case EContentType::NotInterestingEdge:
    {
      return false;
    }
  
    case EContentType::Cliff:
    {
      return true;
    }
    
    case EContentType::_Count:
    {
      return false;
    }
  }
}

} // namespace
} // namespace
} // namespace

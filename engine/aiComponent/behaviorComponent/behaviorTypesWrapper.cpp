/**
 * File: behaviorTypesWrapper.cpp
 *
 * Author: Brad Neuman
 * Created: 2017-10-18
 *
 * Description: This file provides a set of wrappers around the CLAD types defined in BehaviorTypes.clad. In
 *              almost all cases, these wrappers should be used instead of the raw clad files to avoid very
 *              slow incremental builds. If you directly use the CLAD enums, even in a .cpp file, that file
 *              will need to be re-built every time a behavior ID is added. By using these wrappers, the
 *              incremental build time after touching BehaviorTypes.clad will be much more manageable
 *
 * Copyright: Anki, Inc. 2017
 *
 **/

// only include this here in the cpp
#include "clad/types/behaviorComponent/behaviorTypes.h"

#include "engine/aiComponent/behaviorComponent/behaviorTypesWrapper.h"
#include "util/logging/logging.h"

namespace Anki {
namespace Vector {
namespace BehaviorTypesWrapper {

BehaviorID BehaviorIDFromString(const std::string& name)
{
  BehaviorID id;
  const bool success = Anki::Vector::BehaviorIDFromString(name, id);
  ANKI_VERIFY(success, 
              "BehaviorTypesWrapper.BehaviorIDFromString.FailedToParse",
              "Unable to find behaviorID for %s",
              name.c_str());
  return id;
}

BehaviorClass BehaviorClassFromString(const std::string& name)
{
  return Anki::Vector::BehaviorClassFromString(name);
}

bool BehaviorIDFromString(const std::string& name, BehaviorID& id)
{
  return Anki::Vector::BehaviorIDFromString(name, id);
}

bool IsValidBehaviorID(const std::string& name)
{
  BehaviorID waste;
  return Anki::Vector::BehaviorIDFromString(name, waste);
}

ExecutableBehaviorType ExecutableBehaviorTypeFromString(const std::string& name)
{
  return Anki::Vector::ExecutableBehaviorTypeFromString(name);
}

const char* BehaviorIDToString(const BehaviorID in)
{
  return Anki::Vector::BehaviorIDToString(in);
}
    
const char* BehaviorClassToString(const BehaviorClass in)
{
  return Anki::Vector::BehaviorClassToString(in);
}

const char* ExecutableBehaviorTypeToString(const ExecutableBehaviorType in)
{
  return Anki::Vector::ExecutableBehaviorTypeToString(in);
}
  
#if BEHAVIOR_ID_DEV_MODE
uint16_t GetBehaviorIDNumEntries()
{
  return BehaviorIDNumEntries;
}
#endif

ExecutableBehaviorType GetDefaultExecutableBehaviorType()
{
  return ExecutableBehaviorType::Count;
}

}
}
}

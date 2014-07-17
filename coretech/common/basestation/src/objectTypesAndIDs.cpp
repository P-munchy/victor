/**
 * File: objectTypesAndIDs.cpp
 *
 * Author: Andrew Stein
 * Date:   7/17/2014
 *
 * Description: Base classes for inheritable, strongly-typed, unique values.
 *              Not using regular enums allows them to be used more rigorously
 *              as keys for containers (without being interchangeable).  Not
 *              using C++11 enum classes allows them to be subclassed and
 *              extended with new "enumerated" values more easily.
 *
 *              - ObjectIDs are meant to be unique integer values for storing
 *                an identifier for an instance of an object.
 *
 *              - ObjectTypes are unique values from a given set (generally
 *                created by instantiating static const instances of the classes
 *                with a value coming from the current value of a static counter.
 *
 *
 * Copyright: Anki, Inc. 2014
 **/

#include "anki/common/basestation/objectTypesAndIDs.h"

#include <cassert>

namespace Anki {

  ObjectType::StorageType ObjectType::UniqueTypeCounter = 0;
  
  std::set<int>& ObjectType::GetValidTypes()
  {
    static std::set<int> ValidTypes;
    return ValidTypes;
  }
  
  ObjectID::StorageType ObjectID::UniqueIDCounter = 0;
  
  
  ObjectType::ObjectType() {
    const int newType = UniqueTypeCounter++;
    SetValue(newType);
    //printf("Adding new type %d (set size = %lu)\n", newType, GetValidTypes().size());
    GetValidTypes().insert(newType);
  }
  
  
  ObjectType::ObjectType(int value)
  {
    if(GetValidTypes().count(value) > 0) {
      SetValue(value);
    } else {
      assert(false); // TODO: Better failure
      SetToUnknown();
    }
  }
  
  int ObjectType::GetNumTypes() {
    return ObjectType::UniqueTypeCounter;
  }
  
  
  void ObjectID::Reset() {
    ObjectID::UniqueIDCounter = 0;
  }
  
  void ObjectID::Set() {
    SetValue(ObjectID::UniqueIDCounter++);
  }
  
} // namespace Anki
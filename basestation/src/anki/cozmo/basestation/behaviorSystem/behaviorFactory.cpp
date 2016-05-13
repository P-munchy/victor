/**
 * File: behaviorFactory
 *
 * Author: Mark Wesley
 * Created: 11/20/15
 *
 * Description: Factory for creating behaviors from data / messages
 *
 * Copyright: Anki, Inc. 2015
 *
 **/


#include "anki/cozmo/basestation/behaviorSystem/behaviorFactory.h"
#include "anki/cozmo/basestation/behaviorSystem/behaviorTypesHelpers.h"

// Behaviors:
#include "../behaviors/exploration/behaviorExploreBringCubeToBeacon.h"
#include "../behaviors/exploration/behaviorExploreCliff.h"
#include "../behaviors/exploration/behaviorExploreLookAroundInPlace.h"
#include "../behaviors/exploration/behaviorExploreMarkedCube.h"
#include "../behaviors/exploration/behaviorExploreVisitPossibleMarker.h"
#include "anki/cozmo/basestation/behaviors/behaviorAdmireStack.h"
#include "anki/cozmo/basestation/behaviors/behaviorDemoFearEdge.h"
#include "anki/cozmo/basestation/behaviors/behaviorFactoryTest.h"
#include "anki/cozmo/basestation/behaviors/behaviorFindFaces.h"
#include "anki/cozmo/basestation/behaviors/behaviorFlipDownFromWheelie.h"
#include "anki/cozmo/basestation/behaviors/behaviorInteractWithFaces.h"
#include "anki/cozmo/basestation/behaviors/behaviorLookAround.h"
#include "anki/cozmo/basestation/behaviors/behaviorNone.h"
#include "anki/cozmo/basestation/behaviors/behaviorPlayAnim.h"
#include "anki/cozmo/basestation/behaviors/behaviorPounceOnMotion.h"
#include "anki/cozmo/basestation/behaviors/behaviorReactToCliff.h"
#include "anki/cozmo/basestation/behaviors/behaviorReactToPickup.h"
#include "anki/cozmo/basestation/behaviors/behaviorReactToPoke.h"
#include "anki/cozmo/basestation/behaviors/behaviorReactToRobotOnBack.h"
#include "anki/cozmo/basestation/behaviors/behaviorReactToOnCharger.h"
#include "anki/cozmo/basestation/behaviors/behaviorReactToStop.h"
#include "anki/cozmo/basestation/behaviors/behaviorRollBlock.h"
#include "anki/cozmo/basestation/behaviors/behaviorStackBlocks.h"
#include "anki/cozmo/basestation/behaviors/behaviorUnityDriven.h"
#include "anki/cozmo/basestation/behaviors/gameRequest/behaviorRequestGameSimple.h"


namespace Anki {
namespace Cozmo {


BehaviorFactory::BehaviorFactory()
{
}


BehaviorFactory::~BehaviorFactory()
{
  // Delete all behaviors owned by the factory
  
  for (auto& it : _nameToBehaviorMap)
  {
    IBehavior* behavior = it.second;
    // we delete rather than destroy to avoid invalidating the map - it's emptied at the end anyway
    assert(behavior->IsOwnedByFactory());
    DeleteBehaviorInternal(behavior);
  }
  
  _nameToBehaviorMap.clear();
}


IBehavior* BehaviorFactory::CreateBehavior(BehaviorType behaviorType, Robot& robot, const Json::Value& config, NameCollisionRule nameCollisionRule)
{
  IBehavior* newBehavior = nullptr;
  
  switch (behaviorType)
  {
    case BehaviorType::NoneBehavior:
    {
      newBehavior = new BehaviorNone(robot, config);
      break;
    }
    case BehaviorType::LookAround:
    {
      newBehavior = new BehaviorLookAround(robot, config);
      break;
    }
    case BehaviorType::InteractWithFaces:
    {
      newBehavior = new BehaviorInteractWithFaces(robot, config);
      break;
    }
    case BehaviorType::ReactToStop:
    {
      newBehavior = new BehaviorReactToStop(robot, config);
      break;
    }
    case BehaviorType::ReactToPickup:
    {
      newBehavior = new BehaviorReactToPickup(robot, config);
      break;
    }
    case BehaviorType::ReactToCliff:
    {
      newBehavior = new BehaviorReactToCliff(robot, config);
      break;
    }
    case BehaviorType::ReactToPoke:
    {
      newBehavior = new BehaviorReactToPoke(robot, config);
      break;
    }
    case BehaviorType::PlayAnim:
    {
      newBehavior = new BehaviorPlayAnim(robot, config);
      break;
    }
    case BehaviorType::UnityDriven:
    {
      newBehavior = new BehaviorUnityDriven(robot, config);
      break;
    }
    case BehaviorType::PounceOnMotion:
    {
      newBehavior = new BehaviorPounceOnMotion(robot, config);
      break;
    }
    case BehaviorType::FindFaces:
    {
      newBehavior = new BehaviorFindFaces(robot, config);
      break;
    }
    case BehaviorType::ExploreMarkedCube:
    {
      newBehavior = new BehaviorExploreMarkedCube(robot, config);
      break;
    }
    case BehaviorType::ExploreCliff:
    {
      newBehavior = new BehaviorExploreCliff(robot, config);
      break;
    }
    case BehaviorType::RequestGameSimple:
    {
      newBehavior = new BehaviorRequestGameSimple(robot, config);
      break;
    }
    case BehaviorType::ExploreLookAroundInPlace:
    {
      newBehavior = new BehaviorExploreLookAroundInPlace(robot, config);
      break;
    }
    case BehaviorType::ExploreVisitPossibleMarker:
    {
      newBehavior = new BehaviorExploreVisitPossibleMarker(robot, config);
      break;
    }
    case BehaviorType::ExploreBringCubeToBeacon:
    {
      newBehavior = new BehaviorExploreBringCubeToBeacon(robot, config);
      break;
    }
    case BehaviorType::RollBlock:
    {
      newBehavior = new BehaviorRollBlock(robot, config);
      break;
    }
    case BehaviorType::FactoryTest:
    {
      newBehavior = new BehaviorFactoryTest(robot, config);
      break;
    }
    case BehaviorType::DemoFearEdge:
    {
      newBehavior = new BehaviorDemoFearEdge(robot, config);
      break;
    }
    case BehaviorType::FlipDownFromWheelie:
    {
      newBehavior = new BehaviorFlipDownFromWheelie(robot, config);
      break;
    }
    case BehaviorType::ReactToRobotOnBack:
    {
      newBehavior = new BehaviorReactToRobotOnBack(robot, config);
      break;     
    }
    case BehaviorType::StackBlocks:
    {
      newBehavior = new BehaviorStackBlocks(robot, config);
      break;
    }
    case BehaviorType::AdmireStack:
    {
      newBehavior = new BehaviorAdmireStack(robot, config);
      break;
    }
    case BehaviorType::ReactToOnCharger:
    {
      newBehavior = new BehaviorReactToOnCharger(robot, config);
      break;
    }
    case BehaviorType::Count:
    {
      PRINT_NAMED_ERROR("BehaviorFactory.CreateBehavior.BadType", "Unexpected type '%s'", EnumToString(behaviorType));
      break;
    }
  }
  
  if (newBehavior)
  {
    newBehavior = AddToFactory(newBehavior, nameCollisionRule);
  }
  
  if (newBehavior == nullptr)
  {
    PRINT_NAMED_ERROR("BehaviorFactory.CreateBehavior.Failed",
                      "Failed to create Behavior of type '%s'", BehaviorTypeToString(behaviorType));
  }
  
  return newBehavior;
}
  
  
IBehavior* BehaviorFactory::AddToFactory(IBehavior* newBehavior, NameCollisionRule nameCollisionRule)
{
  assert(newBehavior);
  assert(!newBehavior->_isOwnedByFactory);
  
  newBehavior->_isOwnedByFactory = true;
  const std::string& newBehaviorName = newBehavior->GetName();
  
  auto newEntry = _nameToBehaviorMap.insert( NameToBehaviorMap::value_type(newBehaviorName, newBehavior) );
  const bool addedNewEntry = newEntry.second;
  
  if (addedNewEntry)
  {
    PRINT_NAMED_INFO("BehaviorFactory::AddToFactory", "Added new behavior '%s' %p", newBehaviorName.c_str(), newBehavior);
  }
  else
  {
    // map insert failed (found an existing entry), handle this name collision
    
    IBehavior* oldBehavior = newEntry.first->second;
    
    switch(nameCollisionRule)
    {
      case NameCollisionRule::ReuseOld:
      {
        PRINT_NAMED_INFO("BehaviorFactory.AddToFactory.ReuseOld",
                         "Behavior '%s' already exists (%p) - reusing!", newBehaviorName.c_str(), oldBehavior);
        
        // use DeleteBehaviorInternal instead of Destroy - we never added newBehavior to the map
        DeleteBehaviorInternal(newBehavior);
        newBehavior = oldBehavior;
        break;
      }
      case NameCollisionRule::OverwriteWithNew:
      {
        PRINT_NAMED_INFO("BehaviorFactory.AddToFactory.Overwrite",
                         "Behavior '%s' already exists (%p) - overwriting with %p", newBehaviorName.c_str(), oldBehavior, newBehavior);
        
        // use DeleteBehaviorInternal instead of Destroy - we are replacing oldBehavior's map entry
        DeleteBehaviorInternal(oldBehavior);
        newEntry.first->second = newBehavior;
        break;
      }
      case NameCollisionRule::Fail:
      {
        PRINT_NAMED_ERROR("BehaviorFactory.AddToFactory.NameClashFail",
                          "Behavior '%s' already exists (%p) - fail!", newBehaviorName.c_str(), oldBehavior);
        
        // use DeleteBehaviorInternal instead of Destroy - we never added newBehavior to the map
        DeleteBehaviorInternal(newBehavior);
        newBehavior = nullptr;
        break;
      }
    }
  }
  
  return newBehavior;
}


static const char* kBehaviorTypeKey = "behaviorType";
  
  
IBehavior* BehaviorFactory::CreateBehavior(const Json::Value& behaviorJson, Robot& robot, NameCollisionRule nameCollisionRule)
{
  // Find the behavior type
  
  const Json::Value& behaviorTypeJson = behaviorJson[kBehaviorTypeKey];
  const char* behaviorTypeString = behaviorTypeJson.isString() ? behaviorTypeJson.asCString() : "";
  
  const BehaviorType behaviorType = BehaviorTypeFromString(behaviorTypeString);
  
  if (behaviorType == BehaviorType::Count)
  {
    PRINT_NAMED_WARNING("BehaviorFactory.CreateBehavior.UnknownType", "Unknown type '%s'", behaviorTypeString);
    return nullptr;
  }
  
  IBehavior* newBehavior = CreateBehavior(behaviorType, robot, behaviorJson, nameCollisionRule);
  
  return newBehavior;
}
 
  
void BehaviorFactory::DestroyBehavior(IBehavior* behavior)
{
  assert(behavior->IsOwnedByFactory()); // we assume all behaviors are created and owned by factory so this should always be true

  RemoveBehaviorFromMap(behavior);
  DeleteBehaviorInternal(behavior);
}

  
void BehaviorFactory::SafeDestroyBehavior(IBehavior*& behaviorPtrRef)
{
  DestroyBehavior(behaviorPtrRef);
  behaviorPtrRef = nullptr;
}
  

void BehaviorFactory::DeleteBehaviorInternal(IBehavior* behavior)
{
  behavior->_isOwnedByFactory = false;
  delete behavior;
}

  
bool BehaviorFactory::RemoveBehaviorFromMap(IBehavior* behavior)
{
  const auto& it = _nameToBehaviorMap.find(behavior->GetName());
  if (it != _nameToBehaviorMap.end())
  {
    // check it's the same pointer
    IBehavior* existingBehavior = it->second;
    if (existingBehavior == behavior)
    {
      _nameToBehaviorMap.erase(it);
      return true;
    }
  }
  
  return false;
}


IBehavior* BehaviorFactory::FindBehaviorByName(const std::string& inName)
{
  IBehavior* foundBehavior = nullptr;
  
  auto it = _nameToBehaviorMap.find(inName);
  if (it != _nameToBehaviorMap.end())
  {
    foundBehavior = it->second;
  }
  
  return foundBehavior;
}

  
} // namespace Cozmo
} // namespace Anki


/**
 * File: behaviorHelperFactory.cpp
 *
 * Author: Kevin M. Karol
 * Created: 2/13/17
 *
 * Description: Factory for creating behaviorHelpers
 *
 * Copyright: Anki, Inc. 2017
 *
 **/

#include "engine/behaviorSystem/behaviorHelpers/behaviorHelperFactory.h"

#include "engine/aiComponent/behaviorHelperComponent.h"
#include "engine/behaviorSystem/behaviorHelpers/driveToHelper.h"
#include "engine/behaviorSystem/behaviorHelpers/pickupBlockHelper.h"
#include "engine/behaviorSystem/behaviorHelpers/placeBlockHelper.h"
#include "engine/behaviorSystem/behaviorHelpers/placeRelObjectHelper.h"
#include "engine/behaviorSystem/behaviorHelpers/rollBlockHelper.h"
#include "engine/behaviorSystem/behaviorHelpers/searchForBlockHelper.h"

namespace Anki {
namespace Cozmo {

namespace{
  

}

  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorHelperFactory::BehaviorHelperFactory(BehaviorHelperComponent& component)
: _helperComponent(component)
{
  
}
  
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
HelperHandle BehaviorHelperFactory::CreateDriveToHelper(Robot& robot,
                                                        IBehavior& behavior,
                                                        const ObjectID& targetID,
                                                        const DriveToParameters& parameters)
{
  IHelper* helper = new DriveToHelper(robot, behavior, *this, targetID, parameters);
  return _helperComponent.AddHelperToComponent(helper);
}
  
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
HelperHandle BehaviorHelperFactory::CreatePickupBlockHelper(Robot& robot,
                                                            IBehavior& behavior,
                                                            const ObjectID& targetID,
                                                            const PickupBlockParamaters& parameters)
{
  IHelper* helper = new PickupBlockHelper(robot, behavior, *this, targetID, parameters);
  return _helperComponent.AddHelperToComponent(helper);
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
HelperHandle BehaviorHelperFactory::CreatePlaceBlockHelper(Robot& robot,
                                                           IBehavior& behavior)
{
  IHelper* helper = new PlaceBlockHelper(robot, behavior, *this);
  return _helperComponent.AddHelperToComponent(helper);
}

  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
HelperHandle BehaviorHelperFactory::CreatePlaceRelObjectHelper(Robot& robot,
                                                               IBehavior& behavior,
                                                               const ObjectID& targetID,
                                                               const bool placingOnGround,
                                                               const PlaceRelObjectParameters& parameters)
{
  IHelper* helper = new PlaceRelObjectHelper(robot, behavior, *this,
                                             targetID,
                                             placingOnGround,
                                             parameters);
  return _helperComponent.AddHelperToComponent(helper);
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
HelperHandle BehaviorHelperFactory::CreateRollBlockHelper(Robot& robot,
                                                          IBehavior& behavior,
                                                          const ObjectID& targetID,
                                                          bool rollToUpright,
                                                          const RollBlockParameters& parameters)
{
  IHelper* helper = new RollBlockHelper(robot, behavior, *this, targetID, rollToUpright, parameters);
  return _helperComponent.AddHelperToComponent(helper);
}

  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
HelperHandle BehaviorHelperFactory::CreateSearchForBlockHelper(Robot& robot,
                                                               IBehavior& behavior,
                                                               const SearchParameters& params)
{
  IHelper* helper = new SearchForBlockHelper(robot, behavior, *this, params);
  return _helperComponent.AddHelperToComponent(helper);
}
  
} // namespace Cozmo
} // namespace Anki


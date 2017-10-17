/**
 * File: ActivityStrategySpark.h
 *
 * Author: Raul
 * Created: 08/10/2016
 *
 * Description: Specific strategy for Spark activity.
 *
 * Copyright: Anki, Inc. 2016
 *
 **/
#ifndef __Cozmo_Basestation_BehaviorSystem_Activities_ActivityStrategies_ActivityStrategySpark_H__
#define __Cozmo_Basestation_BehaviorSystem_Activities_ActivityStrategies_ActivityStrategySpark_H__

#include "engine/aiComponent/behaviorComponent/activities/activityStrategies/iActivityStrategy.h"
#include "json/json-forwards.h"

namespace Anki {
namespace Cozmo {

class ActivityStrategySpark : public IActivityStrategy
{
public:

  // constructor
  ActivityStrategySpark(BehaviorExternalInterface& behaviorExternalInterface, const Json::Value& config);

  // true when this activity would be happy to start, false if it doens't want to be fired now
  virtual bool WantsToStartInternal(BehaviorExternalInterface& behaviorExternalInterface, float lastTimeActivityRanSec) const override { return true; };

  // true when this activity wants to finish, false if it would rather continue
  virtual bool WantsToEndInternal(BehaviorExternalInterface& behaviorExternalInterface, float lastTimeActivityStartedSec) const override { return false;};
};
  
} // namespace
} // namespace

#endif // endif __Cozmo_Basestation_BehaviorSystem_Activities_ActivityStrategies_ActivityStrategySpark_H__

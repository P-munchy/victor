/**
 * File: behaviorDance.h
 *
 * Author: Al Chaussee
 * Created: 05/11/17
 *
 * Description: Behavior to have Cozmo dance
 *              Plays dancing animation, triggers music from device, and 
 *              plays cube light animations
 *
 * Copyright: Anki, Inc. 2017
 *
 **/

#ifndef __Cozmo_Basestation_Behaviors_BehaviorDance_H__
#define __Cozmo_Basestation_Behaviors_BehaviorDance_H__

#include "engine/behaviorSystem/behaviors/animationWrappers/behaviorPlayAnimSequence.h"

namespace Anki {
namespace Cozmo {
  
class BehaviorDance : public BehaviorPlayAnimSequence
{
protected:
  
  // Enforce creation through BehaviorContainer
  friend class BehaviorContainer;
  BehaviorDance(Robot& robot, const Json::Value& config);
  
protected:

  virtual Result InitInternal(Robot& robot)   override;
  virtual Result ResumeInternal(Robot& robot) override { return RESULT_FAIL; }
  virtual void   StopInternal(Robot& robot)   override;
  
private:
  
  // Callback function used to pick a new random cube animation trigger
  // when the previous one completes
  void CubeAnimComplete(Robot& robot, const ObjectID& objectID);
  
  // Returns a random dancing related cube animation trigger that is not currently being played
  CubeAnimationTrigger GetRandomAnimTrigger(Robot& robot, const CubeAnimationTrigger& prevTrigger) const;
  
  // Map to store the last CubeAnimationTrigger that was played
  std::map<ObjectID, CubeAnimationTrigger> _lastAnimTrigger;
};
  
}
}

#endif //__Cozmo_Basestation_Behaviors_BehaviorDance_H__

/**
 *  File: BehaviorBuildPyramid.hpp
 *  cozmoEngine
 *
 *  Author: Kevin M. Karol
 *  Created: 2016-08-09
 *
 *  Description: Behavior which allows cozmo to build a pyramid from 3 blocks
 *
 *  Copyright: Anki, Inc. 2016
 *
 **/

#ifndef __Cozmo_Basestation_Behaviors_BehaviorBuildPyramid_H__
#define __Cozmo_Basestation_Behaviors_BehaviorBuildPyramid_H__

#include "anki/common/basestation/objectIDs.h"
#include "anki/common/basestation/math/pose.h"
#include "anki/cozmo/basestation/behaviorSystem/behaviors/iBehavior.h"
#include "anki/cozmo/basestation/behaviorSystem/behaviors/freeplay/buildPyramid/behaviorBuildPyramidBase.h"

namespace Anki {
//forward declaration
class Pose3d;
namespace Cozmo {
class ObservableObject;
enum class AnimationTrigger;
  
class BehaviorBuildPyramid : public BehaviorBuildPyramidBase
{
protected:
  // Enforce creation through BehaviorContainer
  friend class BehaviorContainer;
  BehaviorBuildPyramid(Robot& robot, const Json::Value& config);
  
public:
  
  virtual bool IsRunnableInternal(const BehaviorPreReqRobot& preReqData) const override;
  virtual bool CarryingObjectHandledInternally() const override {return true;}
  
protected:
  
  virtual Result InitInternal(Robot& robot) override;
  
private:
  typedef std::vector<const ObservableObject*> BlockList;
  
  void TransitionToDrivingToTopBlock(Robot& robot);
  void TransitionToPlacingTopBlock(Robot& robot);
  void TransitionToReactingToPyramid(Robot& robot);
        
}; //class BehaviorBuildPyramid

}//namespace Cozmo
}//namespace Anki


#endif // __Cozmo_Basestation_Behaviors_BehaviorBuildPyramid_H__

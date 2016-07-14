/**
 *  File: BehaviorDrivePath.hpp
 *  cozmoEngine
 *
 *  Author: Kevin M. Karol
 *  Created: 2016-06-16
 *
 *  Description: Behavior which allows Cozmo to drive around in a cricle
 *
 *  Copyright: Anki, Inc. 2016
 *
 **/

#ifndef __Cozmo_Basestation_Behaviors_BehaviorDrivePath_H__
#define __Cozmo_Basestation_Behaviors_BehaviorDrivePath_H__

#include "anki/common/basestation/math/pose.h"
#include "anki/cozmo/basestation/behaviors/behaviorInterface.h"
#include "anki/planning/shared/path.h"

namespace Anki {
//forward declaration
class Pose3d;
  
namespace Cozmo {
  
class BehaviorDrivePath : public IBehavior
{
protected:
  
  // Enforce creation through BehaviorFactory
  friend class BehaviorFactory;
  BehaviorDrivePath(Robot& robot, const Json::Value& config);
  
public:
  
  virtual bool IsRunnableInternal(const Robot& robot) const override;
  
protected:
  
  virtual Result InitInternal(Robot& robot) override;
  virtual float EvaluateScoreInternal(const Robot& robot) const override;
  
  //Functions and shared data for inheriting classes
  enum class State {
    FollowingPath,
  };
  State _state = State::FollowingPath;
  
  //The path to follow
  Planning::Path _path;
  
  //Building and selecting Path
  virtual void SelectPath(const Pose3d& startingPose, Planning::Path& path);
  virtual void BuildSquare(const Pose3d& startingPose, Planning::Path& path);
  virtual void BuildFigureEight(const Pose3d& startingPose, Planning::Path& path);
  virtual void BuildZ(const Pose3d& startingPose, Planning::Path& path);
  
  
private:
  void TransitionToFollowingPath(Robot& robot);
  void SetState_internal(State state, const std::string& stateName);
  
}; //class BehaviorDrivePath

}//namespace Cozmo
}//namespace Anki


#endif // __Cozmo_Basestation_Behaviors_BehaviorDrivePath_H__

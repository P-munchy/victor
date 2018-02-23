/**
 * File: behaviorReactToRobotOnFace.h
 *
 * Author: Kevin M. Karol
 * Created: 2016-07-18
 *
 * Description: 
 *
 * Copyright: Anki, Inc. 2016
 *
 **/

#ifndef __Cozmo_Basestation_Behaviors_BeahviorReactToRobotOnFace_H__
#define __Cozmo_Basestation_Behaviors_BeahviorReactToRobotOnFace_H__

#include "engine/aiComponent/behaviorComponent/behaviors/iCozmoBehavior.h"

namespace Anki {
namespace Cozmo {

class BehaviorReactToRobotOnFace : public ICozmoBehavior
{
private:
  // Enforce creation through BehaviorFactory
  friend class BehaviorFactory;
  BehaviorReactToRobotOnFace(const Json::Value& config);
  
public:
  virtual bool WantsToBeActivatedBehavior() const override;  

protected:
  virtual void GetBehaviorOperationModifiers(BehaviorOperationModifiers& modifiers) const override {
    modifiers.wantsToBeActivatedWhenCarryingObject = true;
    modifiers.wantsToBeActivatedWhenOffTreads = true;
  }

  virtual void OnBehaviorActivated() override;
  virtual void OnBehaviorDeactivated() override;

private:
  void FlipOverIfNeeded();
  void DelayThenCheckState();
  void CheckFlipSuccess();
  
  
};

}
}

#endif

/**
 * File: behaviorReactToRobotOnSide.h
 *
 * Author: Kevin M. Karol
 * Created: 2016-07-18
 *
 * Description: 
 *
 * Copyright: Anki, Inc. 2016
 *
 **/

#ifndef __Cozmo_Basestation_Behaviors_BeahviorReactToRobotOnSide_H__
#define __Cozmo_Basestation_Behaviors_BeahviorReactToRobotOnSide_H__

#include "engine/aiComponent/behaviorComponent/behaviors/iCozmoBehavior.h"

namespace Anki {
namespace Cozmo {

class BehaviorReactToRobotOnSide : public ICozmoBehavior
{
private:
  
  // Enforce creation through BehaviorContainer
  friend class BehaviorContainer;
  BehaviorReactToRobotOnSide(const Json::Value& config);
  
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

  void ReactToBeingOnSide();
  void AskToBeRighted();
  //Ensures no other behaviors run while Cozmo is still on his side
  void HoldingLoop();

  float _timeToPerformBoredAnim_s = -1.0f;
  
};

}
}

#endif

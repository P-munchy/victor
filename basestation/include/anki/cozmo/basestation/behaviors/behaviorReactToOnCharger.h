/**
 * File: behaviorReactToOnCharger.h
 *
 * Author: Molly
 * Created: 5/12/16
 *
 * Description: Behavior for going night night on charger
 *
 * Copyright: Anki, Inc. 2015
 *
 **/
#ifndef __Cozmo_Basestation_Behaviors_BehaviorReactToOnCharger_H__
#define __Cozmo_Basestation_Behaviors_BehaviorReactToOnCharger_H__

#include "anki/cozmo/basestation/behaviors/behaviorInterface.h"

namespace Anki {
namespace Cozmo {

class BehaviorReactToOnCharger : public IReactionaryBehavior
{
private:
  
  // Enforce creation through BehaviorFactory
  friend class BehaviorFactory;
  BehaviorReactToOnCharger(Robot& robot, const Json::Value& config);
  
public:
  
  virtual bool IsRunnableInternal(const Robot& robot) const override;
  virtual bool ShouldRunForEvent(const ExternalInterface::MessageEngineToGame& event, const Robot& robot) override;
  virtual bool ShouldResumeLastBehavior() const override { return true; }
  virtual void HandleWhileRunning(const GameToEngineEvent& event, Robot& robot) override;
  
  virtual void AlwaysHandleInternal(const GameToEngineEvent& event, const Robot& robot) override;
  virtual void HandleWhileNotRunning(const EngineToGameEvent& event, const Robot& robot) override;
  
protected:
    
  virtual Result InitInternal(Robot& robot) override;
  virtual Status UpdateInternal(Robot& robot) override;
  
  void TransitionToSleepLoop(Robot& robot);
  
private:
  
  bool _shouldStopBehavior = false;
  bool _isReactionEnabled = true;
  bool _isOnCharger = false;
}; // class BehaviorReactToOnCharger
  

} // namespace Cozmo
} // namespace Anki

#endif // __Cozmo_Basestation_Behaviors_BehaviorReactToOnCharger_H__

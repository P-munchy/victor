/**
 * File: behaviorUnityDriven
 *
 * Author: Mark Wesley
 * Created: 11/17/15
 *
 * Description: Unity driven behavior - a wrapper that allows Unity to drive behavior asynchronously via CLAD messages
 *
 * Copyright: Anki, Inc. 2015
 *
 **/

#ifndef __Cozmo_Basestation_Behaviors_BehaviorUnityDriven_H__
#define __Cozmo_Basestation_Behaviors_BehaviorUnityDriven_H__


#include "anki/cozmo/basestation/behaviors/behaviorInterface.h"


namespace Anki {
namespace Cozmo {

  
class BehaviorUnityDriven : public IBehavior
{
private:
  
  // Enforce creation through BehaviorFactory
  friend class BehaviorFactory;
  BehaviorUnityDriven(Robot& robot, const Json::Value& config);
  
public:
  
  virtual ~BehaviorUnityDriven();
  
  virtual bool IsRunnable(const Robot& robot) const override { return _isRunnable; }
  
  virtual float EvaluateScoreInternal(const Robot& robot) const override;
    
protected:
  
  virtual Result InitInternal(Robot& robot) override;
  virtual Status UpdateInternal(Robot& robot) override;
  virtual Result InterruptInternal(Robot& robot) override;
  virtual void   StopInternal(Robot& robot) override;
  
  virtual void HandleWhileRunning(const EngineToGameEvent& event, Robot& robot) override;
  
private:
  
  float _externalScore;
  
  bool  _isScoredExternally;
  
  bool  _isRunnable;
  bool  _wasInterrupted;
  bool  _isComplete;
};
  

} // namespace Cozmo
} // namespace Anki

#endif // __Cozmo_Basestation_Behaviors_BehaviorUnityDriven_H__

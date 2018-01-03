/**
 * File: BehaviorReactToMotorCalibration.h
 *
 * Author: Kevin Yoon
 * Created: 11/2/2016
 *
 * Description: Behavior for reacting to automatic motor calibration
 *
 * Copyright: Anki, Inc. 2016
 *
 **/

#ifndef __Cozmo_Basestation_Behaviors_BehaviorReactToMotorCalibration_H__
#define __Cozmo_Basestation_Behaviors_BehaviorReactToMotorCalibration_H__

#include "engine/aiComponent/behaviorComponent/behaviors/iCozmoBehavior.h"
#include "clad/robotInterface/messageRobotToEngine.h"
#include "util/signals/simpleSignal_fwd.h"

namespace Anki {
namespace Cozmo {

  
class BehaviorReactToMotorCalibration : public ICozmoBehavior
{
private:
  using super = ICozmoBehavior;
  
  friend class BehaviorContainer;
  BehaviorReactToMotorCalibration(const Json::Value& config);
  
public:  
  virtual bool WantsToBeActivatedBehavior(BehaviorExternalInterface& behaviorExternalInterface) const override;
  virtual bool CarryingObjectHandledInternally() const override {return true;}

protected:
  
  virtual void OnBehaviorActivated(BehaviorExternalInterface& behaviorExternalInterface) override;
  virtual void OnBehaviorDeactivated(BehaviorExternalInterface& behaviorExternalInterface) override { };

  virtual void HandleWhileActivated(const EngineToGameEvent& event, BehaviorExternalInterface& behaviorExternalInterface) override;

  constexpr static f32 _kTimeout_sec = 5.;
  
};
  
}
}

#endif // __Cozmo_Basestation_Behaviors_BehaviorReactToMotorCalibration_H__

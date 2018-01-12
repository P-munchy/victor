/**
 * File: behaviorReactToRobotOnBack.h
 *
 * Author: Brad Neuman
 * Created: 2016-05-06
 *
 * Description: 
 *
 * Copyright: Anki, Inc. 2016
 *
 **/

#include "engine/aiComponent/behaviorComponent/behaviors/reactions/behaviorReactToRobotOnBack.h"

#include "engine/actions/animActions.h"
#include "engine/actions/basicActions.h"
#include "engine/aiComponent/aiComponent.h"
#include "engine/aiComponent/aiWhiteboard.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/beiRobotInfo.h"
#include "engine/components/sensors/cliffSensorComponent.h"
#include "engine/externalInterface/externalInterface.h"
#include "clad/externalInterface/messageEngineToGame.h"

namespace Anki {
namespace Cozmo {
  
using namespace ExternalInterface;

static const float kWaitTimeBeforeRepeatAnim_s = 0.5f;


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorReactToRobotOnBack::BehaviorReactToRobotOnBack(const Json::Value& config)
: ICozmoBehavior(config)
{
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorReactToRobotOnBack::WantsToBeActivatedBehavior(BehaviorExternalInterface& behaviorExternalInterface) const
{
  return true;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToRobotOnBack::OnBehaviorActivated(BehaviorExternalInterface& behaviorExternalInterface)
{
  FlipDownIfNeeded(behaviorExternalInterface);
  
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToRobotOnBack::FlipDownIfNeeded(BehaviorExternalInterface& behaviorExternalInterface)
{
  if( behaviorExternalInterface.GetOffTreadsState() == OffTreadsState::OnBack ) {
    const auto& robotInfo = behaviorExternalInterface.GetRobotInfo();
    // Check if cliff detected
    // If not, then calibrate head because we're not likely to be on back if no cliff detected.
    if (robotInfo.GetCliffSensorComponent().IsCliffDetected()) {
      AnimationTrigger anim = AnimationTrigger::FlipDownFromBack;
      
      if(behaviorExternalInterface.GetAIComponent().GetWhiteboard().HasHiccups())
      {
        anim = AnimationTrigger::HiccupRobotOnBack;
      }
    
      DelegateIfInControl(new TriggerAnimationAction(anim),
                  &BehaviorReactToRobotOnBack::DelayThenFlipDown);
    } else {
      const auto cliffs = robotInfo.GetCliffSensorComponent().GetCliffDataRaw();
      LOG_EVENT("BehaviorReactToRobotOnBack.FlipDownIfNeeded.CalibratingHead", "%d %d %d %d", cliffs[0], cliffs[1], cliffs[2], cliffs[3]);
      DelegateIfInControl(new CalibrateMotorAction(true, false),
                  &BehaviorReactToRobotOnBack::DelayThenFlipDown);
    }
  }
  else {
    BehaviorObjectiveAchieved(BehaviorObjective::ReactedToRobotOnBack);
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToRobotOnBack::DelayThenFlipDown(BehaviorExternalInterface& behaviorExternalInterface)
{
  if( behaviorExternalInterface.GetOffTreadsState() == OffTreadsState::OnBack ) {
    DelegateIfInControl(new WaitAction(kWaitTimeBeforeRepeatAnim_s),
                &BehaviorReactToRobotOnBack::FlipDownIfNeeded);
  }
  else {
    BehaviorObjectiveAchieved(BehaviorObjective::ReactedToRobotOnBack);
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToRobotOnBack::OnBehaviorDeactivated(BehaviorExternalInterface& behaviorExternalInterface)
{
}

} // namespace Cozmo
} // namespace Anki

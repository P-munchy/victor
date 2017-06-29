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

#include "anki/cozmo/basestation/behaviorSystem/behaviors/reactions/behaviorReactToRobotOnBack.h"

#include "anki/cozmo/basestation/actions/animActions.h"
#include "anki/cozmo/basestation/actions/basicActions.h"
#include "anki/cozmo/basestation/aiComponent/aiComponent.h"
#include "anki/cozmo/basestation/aiComponent/AIWhiteboard.h"
#include "anki/cozmo/basestation/components/cliffSensorComponent.h"
#include "anki/cozmo/basestation/externalInterface/externalInterface.h"
#include "anki/cozmo/basestation/robot.h"
#include "clad/externalInterface/messageEngineToGame.h"

namespace Anki {
namespace Cozmo {
  
using namespace ExternalInterface;

static const float kWaitTimeBeforeRepeatAnim_s = 0.5f;
  
BehaviorReactToRobotOnBack::BehaviorReactToRobotOnBack(Robot& robot, const Json::Value& config)
: IBehavior(robot, config)
{
}


bool BehaviorReactToRobotOnBack::IsRunnableInternal(const BehaviorPreReqNone& preReqData) const
{
  return true;
}

Result BehaviorReactToRobotOnBack::InitInternal(Robot& robot)
{
  FlipDownIfNeeded(robot);
  return Result::RESULT_OK;
}

void BehaviorReactToRobotOnBack::FlipDownIfNeeded(Robot& robot)
{
  if( robot.GetOffTreadsState() == OffTreadsState::OnBack ) {
    
    // Check if cliff detected
    // If not, then calibrate head because we're not likely to be on back if no cliff detected.
    const auto cliffDataRaw = robot.GetCliffSensorComponent().GetCliffDataRaw();
    if (cliffDataRaw < CLIFF_SENSOR_DROP_LEVEL) {
      AnimationTrigger anim = AnimationTrigger::FlipDownFromBack;
      
      if(robot.GetAIComponent().GetWhiteboard().HasHiccups())
      {
        anim = AnimationTrigger::HiccupRobotOnBack;
      }
    
      StartActing(new TriggerAnimationAction(robot, anim),
                  &BehaviorReactToRobotOnBack::DelayThenFlipDown);
    } else {
      LOG_EVENT("BehaviorReactToRobotOnBack.FlipDownIfNeeded.CalibratingHead", "%d", cliffDataRaw);
      StartActing(new CalibrateMotorAction(robot, true, false),
                  &BehaviorReactToRobotOnBack::DelayThenFlipDown);
    }
  }
  else {
    BehaviorObjectiveAchieved(BehaviorObjective::ReactedToRobotOnBack);
  }
}

void BehaviorReactToRobotOnBack::DelayThenFlipDown(Robot& robot)
{
  if( robot.GetOffTreadsState() == OffTreadsState::OnBack ) {
    StartActing(new WaitAction(robot, kWaitTimeBeforeRepeatAnim_s),
                &BehaviorReactToRobotOnBack::FlipDownIfNeeded);
  }
  else {
    BehaviorObjectiveAchieved(BehaviorObjective::ReactedToRobotOnBack);
  }
}

void BehaviorReactToRobotOnBack::StopInternal(Robot& robot)
{
}

} // namespace Cozmo
} // namespace Anki

/**
 * File: behaviorPlayAnim
 *
 * Author: Mark Wesley
 * Created: 11/03/15
 *
 * Description: Simple Behavior to play an animation
 *
 * Copyright: Anki, Inc. 2015
 *
 **/

#include "anki/cozmo/basestation/behaviors/behaviorPlayAnim.h"
#include "anki/cozmo/basestation/actions/animActions.h"
#include "anki/cozmo/basestation/events/animationTriggerHelpers.h"

namespace Anki {
namespace Cozmo {
  
using namespace ExternalInterface;
  
static const char* kAnimTriggerKey = "animTrigger";
static const char* kLoopsKey = "num_loops";

BehaviorPlayAnim::BehaviorPlayAnim(Robot& robot, const Json::Value& config)
  : IBehavior(robot, config)
{
  SetDefaultName("PlayAnim");
  
  if (!config.isNull())
  {
    JsonTools::GetValueOptional(config,kAnimTriggerKey,_animTrigger);
  }

  _numLoops = config.get(kLoopsKey, 1).asInt();
}
    
BehaviorPlayAnim::~BehaviorPlayAnim()
{  
}
  
bool BehaviorPlayAnim::IsRunnableInternal(const Robot& robot) const
{
  const bool retVal = true;
  return retVal;
}

Result BehaviorPlayAnim::InitInternal(Robot& robot)
{
  StartActing(new TriggerAnimationAction(robot, _animTrigger, _numLoops));
  return Result::RESULT_OK;
}  

} // namespace Cozmo
} // namespace Anki

/**
 * File: conditionBeatDetected
 *
 * Author: Matt Michini
 * Created: 05/07/2018
 *
 * Description: Determine whether or not the beat detector algorithm running in the
 *              anim process is currently detecting a steady musical beat.
 *
 * Copyright: Anki, Inc. 2018
 *
 **/

#ifndef __Engine_BeiConditions_ConditionBeatDetected_H__
#define __Engine_BeiConditions_ConditionBeatDetected_H__

#include "engine/aiComponent/beiConditions/iBEICondition.h"

namespace Anki {
namespace Cozmo {

class ConditionBeatDetected : public IBEICondition
{
public:
  explicit ConditionBeatDetected(const Json::Value& config);
  virtual bool AreConditionsMetInternal(BehaviorExternalInterface& behaviorExternalInterface) const override;
};


} // namespace
} // namespace

#endif // __Engine_BeiConditions_ConditionBeatDetected_H__


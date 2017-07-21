/**
 * File: behaviorRamIntoBlock.cpp
 *
 * Author: Kevin M. Karol
 * Created: 2/21/17
 *
 * Description: Behavior to ram into a block
 *
 * Copyright: Anki, Inc. 2017
 *
 **/

#include "anki/cozmo/basestation/behaviorSystem/behaviors/reactions/behaviorRamIntoBlock.h"

#include "anki/cozmo/basestation/actions/animActions.h"
#include "anki/cozmo/basestation/actions/basicActions.h"
#include "anki/cozmo/basestation/actions/compoundActions.h"
#include "anki/cozmo/basestation/actions/dockActions.h"
#include "anki/cozmo/basestation/behaviorSystem/behaviorPreReqs/behaviorPreReqAcknowledgeObject.h"
#include "anki/cozmo/basestation/behaviorSystem/reactionTriggerStrategies/reactionTriggerHelpers.h"
#include "anki/cozmo/basestation/blockWorld/blockWorld.h"
#include "anki/cozmo/basestation/components/carryingComponent.h"
#include "anki/cozmo/basestation/robot.h"

#include "anki/common/basestation/math/pose.h"

namespace Anki {
namespace Cozmo {

namespace{
const float kDistancePastBlockToDrive_mm   = 100.0f;
const float kSpeedToDriveThroughBlock_mmps = 200.0f;
const float kDistanceBackUpFromBlock_mm    = 100.0f;
const float kSpeedBackUpFromBlock_mmps     = 100.0f;

  
constexpr ReactionTriggerHelpers::FullReactionArray kAffectTriggersRamIntoBlockArray = {
  {ReactionTrigger::CliffDetected,                true},
  {ReactionTrigger::CubeMoved,                    false},
  {ReactionTrigger::FacePositionUpdated,          true},
  {ReactionTrigger::FistBump,                     false},
  {ReactionTrigger::Frustration,                  false},
  {ReactionTrigger::Hiccup,                       false},
  {ReactionTrigger::MotorCalibration,             false},
  {ReactionTrigger::NoPreDockPoses,               false},
  {ReactionTrigger::ObjectPositionUpdated,        true},
  {ReactionTrigger::PlacedOnCharger,              false},
  {ReactionTrigger::PetInitialDetection,          false},
  {ReactionTrigger::RobotPickedUp,                false},
  {ReactionTrigger::RobotPlacedOnSlope,           false},
  {ReactionTrigger::ReturnedToTreads,             false},
  {ReactionTrigger::RobotOnBack,                  false},
  {ReactionTrigger::RobotOnFace,                  false},
  {ReactionTrigger::RobotOnSide,                  false},
  {ReactionTrigger::RobotShaken,                  false},
  {ReactionTrigger::Sparked,                      false},
  {ReactionTrigger::UnexpectedMovement,           true},
  {ReactionTrigger::VC,                           false}
};

static_assert(ReactionTriggerHelpers::IsSequentialArray(kAffectTriggersRamIntoBlockArray),
              "Reaction triggers duplicate or non-sequential");

} // end namespace
  
using namespace ExternalInterface;

BehaviorRamIntoBlock::BehaviorRamIntoBlock(Robot& robot, const Json::Value& config)
: IBehavior(robot, config)
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorRamIntoBlock::IsRunnableInternal(const BehaviorPreReqAcknowledgeObject& preReqData) const
{
  const auto& targets = preReqData.GetTargets();
  DEV_ASSERT(targets.size() == 1, "BehaviorRamIntoBlock.ImproperNumberOfTargets");
  if(targets.size() == 1){
    _targetID = *(targets.begin());
    return _targetID >= 0;
  }
  
  return false;
}

  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Result BehaviorRamIntoBlock::InitInternal(Robot& robot)
{
  if(robot.GetCarryingComponent().IsCarryingObject()){
    TransitionToPuttingDownBlock(robot);
  }else{
    TransitionToTurningToBlock(robot);
  }
  return Result::RESULT_OK;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Result BehaviorRamIntoBlock::ResumeInternal(Robot& robot)
{
  return Result::RESULT_FAIL;
}

  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorRamIntoBlock::StopInternal(Robot& robot)
{  
}

  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorRamIntoBlock::TransitionToPuttingDownBlock(Robot& robot)
{
  CompoundActionSequential* placeAction = new CompoundActionSequential(robot);
  if(robot.GetCarryingComponent().GetCarryingObject() != _targetID){
    ObservableObject* obj = robot.GetBlockWorld().GetLocatedObjectByID(robot.GetCarryingComponent().GetCarryingObject());
    if(obj != nullptr){
      Vec3f outVector;
      if(ComputeVectorBetween(robot.GetPose(), obj->GetPose(), outVector)){
        Radians angle = FLT_NEAR(outVector.x(), 0.0f) ?
                  Radians(0)                          :
                  Radians(atanf(outVector.y()/outVector.x()));
        const int offsetDirection = angle.ToFloat() > 0 ? 1 : -1;
        angle += DEG_TO_RAD(90 * offsetDirection);
        const bool isAbsolute = true;
        
        // Overshoot the angle to ram by a quarter turn and then place the block down
        placeAction->AddAction(new TurnInPlaceAction(robot, angle.ToFloat(), isAbsolute));
      }
    }
  }
  
  placeAction->AddAction(new PlaceObjectOnGroundAction(robot));
  StartActing(placeAction, &BehaviorRamIntoBlock::TransitionToTurningToBlock);
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorRamIntoBlock::TransitionToTurningToBlock(Robot& robot)
{
  CompoundActionParallel* action = new CompoundActionParallel(robot, {
    new TurnTowardsObjectAction(robot, _targetID),
    new MoveLiftToHeightAction(robot,  MoveLiftToHeightAction::Preset::CARRY)
  });
  StartActing(action, &BehaviorRamIntoBlock::TransitionToRammingIntoBlock);
}
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorRamIntoBlock::TransitionToRammingIntoBlock(Robot& robot)
{
  SmartDisableReactionsWithLock(GetIDStr(), kAffectTriggersRamIntoBlockArray);
  
  const ObservableObject* obj = robot.GetBlockWorld().GetLocatedObjectByID(_targetID);
  if(obj != nullptr){
    const f32 distToObj = ComputeDistanceBetween(robot.GetPose(), obj->GetPose());
    
    
    CompoundActionParallel* ramAction = new CompoundActionParallel(robot, {
      new DriveStraightAction(robot,
                              distToObj + kDistancePastBlockToDrive_mm,
                              kSpeedToDriveThroughBlock_mmps,
                              false),
      new TriggerAnimationAction(robot, AnimationTrigger::SoundOnlyRamIntoBlock)
    });
    

    IActionRunner* driveOut = new DriveStraightAction(robot,
                                                      -kDistanceBackUpFromBlock_mm,
                                                      kSpeedBackUpFromBlock_mmps);
    
    CompoundActionSequential* driveAction = new CompoundActionSequential(robot);
    driveAction->AddAction(ramAction);
    driveAction->AddAction(driveOut);
    StartActing(driveAction);
  }
}
  
} // namespace Cozmo
} // namespace Anki

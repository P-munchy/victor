/**
 * File: pickupBlockHelper.cpp
 *
 * Author: Kevin M. Karol
 * Created: 2/1/17
 *
 * Description: Handles picking up a block with a given ID
 *
 * Copyright: Anki, Inc. 2017
 *
 **/

#include "engine/behaviorSystem/behaviorHelpers/pickupBlockHelper.h"

#include "engine/actions/animActions.h"
#include "engine/actions/dockActions.h"
#include "engine/aiComponent/aiComponent.h"
#include "engine/aiComponent/AIWhiteboard.h"
#include "engine/behaviorSystem/behaviorHelpers/behaviorHelperParameters.h"
#include "engine/blockWorld/blockWorld.h"
#include "engine/robot.h"

namespace Anki {
namespace Cozmo {

namespace{
static const int kMaxDockRetries = 2;
}

  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
PickupBlockHelper::PickupBlockHelper(Robot& robot,
                                     IBehavior& behavior,
                                     BehaviorHelperFactory& helperFactory,
                                     const ObjectID& targetID,
                                     const PickupBlockParamaters& parameters)
: IHelper("PickupBlock", robot, behavior, helperFactory)
, _targetID(targetID)
, _params(parameters)
, _dockAttemptCount(0)
, _hasTriedOtherPose(false)
{
  
  if(_params.sayNameBeforePickup){
    DEV_ASSERT(!NEAR_ZERO(_params.maxTurnTowardsFaceAngle_rad.ToFloat()),
               "PickupBlockHelper.SayNameButNoTurnAngle");
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
PickupBlockHelper::~PickupBlockHelper()
{
  
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool PickupBlockHelper::ShouldCancelDelegates(const Robot& robot) const
{
  return false;
}
  

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorStatus PickupBlockHelper::Init(Robot& robot)
{
  _dockAttemptCount = 0;
  _hasTriedOtherPose = false;
  StartPickupAction(robot);
  return _status;
}
  
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorStatus PickupBlockHelper::UpdateWhileActiveInternal(Robot& robot)
{
  return _status;
}

  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void PickupBlockHelper::StartPickupAction(Robot& robot, bool ignoreCurrentPredockPose)
{
  ActionResult isAtPreAction;
  if( ignoreCurrentPredockPose ) {
    // if we are using second closest we always want to drive. Otherwise, check if we are already in place
    isAtPreAction = ActionResult::ABORT;
  }
  else {
    // if we are using second closest we always want to drive. Otherwise, check if we are already in place
    isAtPreAction = IsAtPreActionPoseWithVisualVerification(robot,
                                                            _targetID,
                                                            PreActionPose::ActionType::DOCKING);
  }
  
  if(isAtPreAction != ActionResult::SUCCESS){
    PRINT_CH_INFO("BehaviorHelpers", "PickupBlockHelper.StartPickupAction.DrivingToPreDockPose",
                  "Cozmo is not at pre-action pose for cube %d, delegating to driveToHelper",
                  _targetID.GetValue());
    DriveToParameters params;
    params.actionType = PreActionPose::ActionType::DOCKING;
    params.ignoreCurrentPredockPose = ignoreCurrentPredockPose;
    DelegateProperties properties;
    properties.SetDelegateToSet(CreateDriveToHelper(robot,
                                                    _targetID,
                                                    params));
    properties.SetOnSuccessFunction([this](Robot& robot){
                                      StartPickupAction(robot); return _status;                                      
                                   });
    properties.FailImmediatelyOnDelegateFailure();
    DelegateAfterUpdate(properties);
  }else{

    PRINT_CH_INFO("BehaviorHelpers", "PickupBlockHelper.StartPickupAction.PickingUpObject",
                  "Picking up target object %d",
                  _targetID.GetValue());
    CompoundActionSequential* action = new CompoundActionSequential(robot);
    if(_params.animBeforeDock != AnimationTrigger::Count){
      action->AddAction(new TriggerAnimationAction(robot, _params.animBeforeDock));
      // In case we repeat, null out anim
      _params.animBeforeDock = AnimationTrigger::Count;
    }
    
    if((_dockAttemptCount == 0) &&
       !NEAR_ZERO(_params.maxTurnTowardsFaceAngle_rad.ToFloat())){
      auto turnTowrdsFaceAction = new TurnTowardsLastFacePoseAction(robot,
                                                                    _params.maxTurnTowardsFaceAngle_rad,
                                                                    _params.sayNameBeforePickup);
      turnTowrdsFaceAction->SetSayNameAnimationTrigger(AnimationTrigger::PickupHelperPreActionNamedFace);
      turnTowrdsFaceAction->SetNoNameAnimationTrigger(AnimationTrigger::PickupHelperPreActionUnnamedFace);
      static const bool ignoreFailure = true;
      action->AddAction(turnTowrdsFaceAction,
                        ignoreFailure);
      
      action->AddAction(new TurnTowardsObjectAction(robot,
                                                    _targetID,
                                                    M_PI_F),
                        ignoreFailure);
    }

    {
      PickupObjectAction* pickupAction = new PickupObjectAction(robot, _targetID);
      // no need to do an extra check in the action
      pickupAction->SetDoNearPredockPoseCheck(false);
      
      action->AddAction(pickupAction);
      action->SetProxyTag(pickupAction->GetTag());
    }
    
    StartActingWithResponseAnim(action, &PickupBlockHelper::RespondToPickupResult,  [] (ActionResult result){
      switch(result){
        case ActionResult::SUCCESS:
        {
          return UserFacingActionResult::Count;
          break;
        }
        case ActionResult::MOTOR_STOPPED_MAKING_PROGRESS:
        case ActionResult::NOT_CARRYING_OBJECT_RETRY:
        case ActionResult::PICKUP_OBJECT_UNEXPECTEDLY_NOT_MOVING:
        case ActionResult::LAST_PICK_AND_PLACE_FAILED:
        {
          return UserFacingActionResult::InteractWithBlockDockingIssue;
          break;
        }
        default:
        {
          return UserFacingActionResult::DriveToBlockIssue;
          break;
        }
      }
    });
    _dockAttemptCount++;
  }
}
  
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void PickupBlockHelper::RespondToPickupResult(const ExternalInterface::RobotCompletedAction& rca, Robot& robot)
{
  const ActionResult& result = rca.result;
  PRINT_CH_DEBUG("BehaviorHelpers", (GetName() + ".PickupResult").c_str(),
                 "%s", ActionResultToString(result));
    
  switch(result){
    case ActionResult::SUCCESS:
    {
      _status = BehaviorStatus::Complete;
      break;
    }
    case ActionResult::VISUAL_OBSERVATION_FAILED:
    {
      DEV_ASSERT_MSG(rca.completionInfo.GetTag() == ActionCompletedUnionTag::objectInteractionCompleted,
                 "PickupBlockHelper.RespondToPickupResult.UnexpectedActionCompletedUnionTag",
                 "%hhu", rca.completionInfo.GetTag());
      
      if(rca.completionInfo.Get_objectInteractionCompleted().seeingUnexpectedObject)
      {
        PRINT_CH_DEBUG("BehaviorHelpers",
                       (GetName() + ".VisualObservationFailed.SeeingUnexpectedObject").c_str(),
                       "Marking target as failed to pickup");
        
        MarkTargetAsFailedToPickup(robot);
        _status = BehaviorStatus::Failure;
      }
      else
      {
        SearchParameters params;
        params.searchingForID = _targetID;
        params.searchIntensity = SearchIntensity::QuickSearch;
        DelegateProperties properties;
        properties.SetDelegateToSet(CreateSearchForBlockHelper(robot, params));
        properties.SetOnSuccessFunction([this](Robot& robot){
          StartPickupAction(robot); return _status;
        });
        properties.SetOnFailureFunction([this](Robot& robot){
          MarkTargetAsFailedToPickup(robot); return BehaviorStatus::Failure;
        });
        DelegateAfterUpdate(properties);
      }
      break;
    }
    case ActionResult::NO_PREACTION_POSES:
    {
      robot.GetAIComponent().GetWhiteboard().SetNoPreDockPosesOnObject(_targetID);
      break;
    }
    case ActionResult::MOTOR_STOPPED_MAKING_PROGRESS:
    case ActionResult::NOT_CARRYING_OBJECT_RETRY:
    case ActionResult::PICKUP_OBJECT_UNEXPECTEDLY_NOT_MOVING:
    case ActionResult::LAST_PICK_AND_PLACE_FAILED:
    {
      PRINT_CH_INFO("BehaviorHelpers", (GetName() + ".DockAttemptFailed").c_str(),
                    "Failed dock attempt %d / %d",
                    _dockAttemptCount,
                    kMaxDockRetries);                                        
      
      if( _dockAttemptCount < kMaxDockRetries ) {
        StartPickupAction(robot);
      }
      else if( _params.allowedToRetryFromDifferentPose && !_hasTriedOtherPose ) {
        PRINT_CH_INFO("BehaviorHelpers", (GetName() + ".RetryFromOtherPose").c_str(),
                      "Trying again with a different predock pose");
        _dockAttemptCount = 0;
        _hasTriedOtherPose = true;
        const bool ignoreCurrentPredockPose = true;
        StartPickupAction(robot, ignoreCurrentPredockPose);
      }
      else {
        PRINT_CH_INFO("BehaviorHelpers", (GetName() + ".PickupFailedTooManyTimes").c_str(),
                      "Failing helper because pickup was already attempted %d times",
                      _dockAttemptCount);
        MarkTargetAsFailedToPickup(robot);
        _status = BehaviorStatus::Failure;
      }
      break;
    }

    case ActionResult::CANCELLED_WHILE_RUNNING:
    {
      // leave the helper running, since it's about to be canceled
      break;
    }
    case ActionResult::BAD_OBJECT:
    {
      _status = BehaviorStatus::Failure;
      break;
    }

    case ActionResult::DID_NOT_REACH_PREACTION_POSE:
    {
      // DriveToHelper should handle this, shouldn't see it here
      PRINT_NAMED_ERROR("PickupBlockHelper.InvalidPickupResponse", "%s", ActionResultToString(result));
      _status = BehaviorStatus::Failure;
      break;
    }

    default:
    {
      //DEV_ASSERT(false, "HANDLE CASE!");
      if( IActionRunner::GetActionResultCategory(result) == ActionResultCategory::RETRY ) {
        StartPickupAction(robot);
      }
      else {
        MarkTargetAsFailedToPickup(robot);
        _status = BehaviorStatus::Failure;
      }
      break;
    }
  }
}

  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void PickupBlockHelper::MarkTargetAsFailedToPickup(Robot& robot)
{
  const ObservableObject* obj = robot.GetBlockWorld().GetLocatedObjectByID(_targetID);
  if(obj != nullptr){
    auto& whiteboard = robot.GetAIComponent().GetWhiteboard();
    whiteboard.SetFailedToUse(*obj, AIWhiteboard::ObjectActionFailure::PickUpObject);
  }
}


} // namespace Cozmo
} // namespace Anki


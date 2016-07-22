/**
 * File: behaviorDriveOffCharger.cpp
 *
 * Author: Molly Jameson
 * Created: 2016-05-19
 *
 * Description: Behavior to drive to the edge off a charger and deal with the firmware cliff stop
 *
 * Copyright: Anki, Inc. 2016
 *
 **/
#include "anki/cozmo/basestation/behaviors/behaviorDriveOffCharger.h"

#include "anki/cozmo/basestation/actions/basicActions.h"
#include "anki/cozmo/basestation/behaviorSystem/AIWhiteboard.h"
#include "anki/cozmo/basestation/charger.h"
#include "anki/cozmo/basestation/drivingAnimationHandler.h"
#include "anki/cozmo/basestation/robot.h"

#include "anki/common/basestation/utils/timer.h"

#include "clad/externalInterface/messageGameToEngine.h"



namespace Anki {
namespace Cozmo {

static const float kInitialDriveSpeed = 100.0f;
static const float kInitialDriveAccel = 40.0f;

static const char* const kExtraDriveDistKey = "extraDistanceToDrive_mm";

#define SET_STATE(s) SetState_internal(State::s, #s)

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorDriveOffCharger::BehaviorDriveOffCharger(Robot& robot, const Json::Value& config)
  : IBehavior(robot, config)
{
  SetDefaultName("DriveOffCharger");
  float extraDist_mm = config.get(kExtraDriveDistKey, 0.0f).asFloat();
  _distToDrive_mm = Charger::GetLength() + extraDist_mm;

  PRINT_NAMED_DEBUG("BehaviorDriveOffCharger.DriveDist",
                    "Driving %fmm off the charger (%f length + %f extra)",
                    _distToDrive_mm,
                    Charger::GetLength(),
                    extraDist_mm);
}
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorDriveOffCharger::IsRunnableInternal(const Robot& robot) const
{
  // can run any time we are on a platform
  const bool onChargerPlatform = robot.IsOnChargerPlatform();
  return onChargerPlatform;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Result BehaviorDriveOffCharger::InitInternal(Robot& robot)
{
  TransitionToDrivingForward(robot);
  _timesResumed = 0;
  
  //Disable Cliff Reaction
  robot.GetBehaviorManager().RequestEnableReactionaryBehavior(GetName(), BehaviorType::ReactToCliff, false);
  
  return Result::RESULT_OK;
}
  
void BehaviorDriveOffCharger::StopInternal(Robot& robot)
{
  robot.GetDrivingAnimationHandler().PopDrivingAnimations();
  //Enable Cliff Reaction
  robot.GetBehaviorManager().RequestEnableReactionaryBehavior(GetName(), BehaviorType::ReactToCliff, true);

}


Result BehaviorDriveOffCharger::ResumeInternal(Robot& robot)
{
  // We hit the end of the charger, just keep driving.
  TransitionToDrivingForward(robot);
  return Result::RESULT_OK;
}


  
IBehavior::Status BehaviorDriveOffCharger::UpdateInternal(Robot& robot)
{
  // Emergency counter for demo rare bug. Usually we just get the chargerplatform message.
  // HACK: figure out why IsOnChargerPlatform might be incorrect.
  if( robot.IsOnChargerPlatform() && _timesResumed <= 2)
  {
    if( ! IsActing() ) {
      // if we finished the last action, but are still on the charger, queue another one
      TransitionToDrivingForward(robot);
    }
    
    return Status::Running;
  }

  if( IsActing() ) {
    // let the action finish
    return Status::Running;
  }
  else {
  
    // store in whiteboard our success
    const float curTime = Util::numeric_cast<float>( BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() );
    robot.GetBehaviorManager().GetWhiteboard().GotOffChargerAtTime( curTime );
  
    return Status::Complete;
  }
}


void BehaviorDriveOffCharger::TransitionToDrivingForward(Robot& robot)
{
  SET_STATE(DrivingForward);
  if( robot.IsOnChargerPlatform() )
  {
    _timesResumed++;
    // Numbers shared with demoFearEdge but will move here since this is in freeplay
    robot.GetDrivingAnimationHandler().PushDrivingAnimations({AnimationTrigger::DriveStartLaunch,
                                                              AnimationTrigger::DriveLoopLaunch,
                                                              AnimationTrigger::DriveEndLaunch});
    // probably interrupted by getting off the charger platform
    DriveStraightAction* action = new DriveStraightAction(robot, _distToDrive_mm, kInitialDriveSpeed);
    action->SetAccel(kInitialDriveAccel);
    StartActing(action);
    // the Update function will transition back to this state (or out of the behavior) as appropriate
  }
}


void BehaviorDriveOffCharger::SetState_internal(State state, const std::string& stateName)
{
  _state = state;
  PRINT_NAMED_DEBUG("BehaviorDriveOffCharger.TransitionTo", "%s", stateName.c_str());
  SetStateName(stateName);
}

}
}


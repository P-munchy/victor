/**
 * File: behaviorAcknowledgeFace.cpp
 *
 * Author:  Andrew Stein
 * Created: 2016-06-16
 *
 * Description:  Simple quick reaction to a "new" face, just to show Cozmo has noticed you.
 *               Cozmo just turns towards the face and then plays a reaction animation.

 *
 * Copyright: Anki, Inc. 2016
 *
 **/

#include "engine/aiComponent/behaviorComponent/behaviors/reactions/behaviorAcknowledgeFace.h"

#include "anki/common/basestation/utils/timer.h"

#include "engine/actions/animActions.h"
#include "engine/actions/basicActions.h"
#include "engine/actions/visuallyVerifyActions.h"
#include "engine/aiComponent/behaviorComponent/behaviorManager.h"
#include "engine/aiComponent/AIWhiteboard.h"
#include "engine/aiComponent/aiComponent.h"
#include "engine/aiComponent/behaviorComponent/behaviorListenerInterfaces/iReactToFaceListener.h"
#include "engine/externalInterface/externalInterface.h"
#include "engine/faceWorld.h"
#include "engine/moodSystem/moodManager.h"
#include "engine/robot.h"
#include "clad/externalInterface/messageEngineToGame.h"
#include "clad/robotInterface/messageFromActiveObject.h"
#include "util/console/consoleInterface.h"

namespace Anki {
namespace Cozmo {

namespace AcknowledgeFaceConsoleVars{
CONSOLE_VAR(u32, kNumImagesToWaitFor, "AcknowledgementBehaviors", 3);

CONSOLE_VAR(f32, kMaxTimeForInitialGreeting_s, "AcknowledgementBehaviors", 60.0f);
}

using namespace AcknowledgeFaceConsoleVars;
  

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorAcknowledgeFace::BehaviorAcknowledgeFace(const Json::Value& config)
: super(config)
{
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorAcknowledgeFace::WantsToBeActivatedBehavior(BehaviorExternalInterface& behaviorExternalInterface) const
{
  return !_desiredTargets.empty();
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Result BehaviorAcknowledgeFace::OnBehaviorActivated(BehaviorExternalInterface& behaviorExternalInterface)
{
  // don't actually init until the first Update call. This gives other messages that came in this tick a
  // chance to be processed, in case we see multiple faces in the same tick.
  _shouldStart = true;

  return Result::RESULT_OK;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorAcknowledgeFace::OnBehaviorDeactivated(BehaviorExternalInterface& behaviorExternalInterface)
{
  for(auto& listener: _faceListeners){
    listener->ClearDesiredTargets();
  }
  _desiredTargets.clear();
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
IBehavior::Status BehaviorAcknowledgeFace::UpdateInternal_WhileRunning(BehaviorExternalInterface& behaviorExternalInterface)
{
  if( _shouldStart ) {
    _shouldStart = false;
    // now figure out which object to react to
    BeginIteration(behaviorExternalInterface);
  }

  return super::UpdateInternal_WhileRunning(behaviorExternalInterface);
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorAcknowledgeFace::UpdateBestTarget(BehaviorExternalInterface& behaviorExternalInterface)
{
  const AIWhiteboard& whiteboard = behaviorExternalInterface.GetAIComponent().GetWhiteboard();
  const bool preferName = false;  
  Vision::FaceID_t bestFace = whiteboard.GetBestFaceToTrack( _desiredTargets, preferName );
  
  if( bestFace == Vision::UnknownFaceID ) {
    return false;
  }
  else {
    _targetFace = bestFace;
    return true;
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorAcknowledgeFace::BeginIteration(BehaviorExternalInterface& behaviorExternalInterface)
{
  _targetFace = Vision::UnknownFaceID;
  if( !UpdateBestTarget(behaviorExternalInterface) ) {
    return;
  }

  // DEPRECATED - Grabbing robot to support current cozmo code, but this should
  // be removed
  Robot& robot = behaviorExternalInterface.GetRobot();

  const bool sayName = true;
  TurnTowardsFaceAction* turnAction = new TurnTowardsFaceAction(robot,
                                                                _targetFace,
                                                                M_PI_F,
                                                                sayName);

  const float freeplayStartedTime_s = robot.GetBehaviorManager().GetFirstTimeFreeplayStarted();    
  const float currTime_s = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();  
  const bool withinMinSessionTime = freeplayStartedTime_s >= 0.0f &&
    (currTime_s - freeplayStartedTime_s) <= kMaxTimeForInitialGreeting_s;
  const bool alreadyTurnedTowards = robot.GetFaceWorld().HasTurnedTowardsFace(_targetFace);
  const bool shouldPlayInitialGreeting = !_hasPlayedInitialGreeting && withinMinSessionTime && !alreadyTurnedTowards;

  PRINT_CH_INFO("Behaviors", "AcknowledgeFace.DoAcknowledgement",
                "currTime = %f, alreadyTurned:%d, shouldPlayGreeting:%d",
                currTime_s,
                alreadyTurnedTowards ? 1 : 0,
                shouldPlayInitialGreeting ? 1 : 0);
  
  if( shouldPlayInitialGreeting ) {
    auto& moodManager = robot.GetMoodManager();
    turnAction->SetSayNameTriggerCallback([this, &moodManager](const Robot& robot, const SmartFaceID& faceID){
        // only play the initial greeting once, so if we are going to use it, mark that here
        _hasPlayedInitialGreeting = true;
        moodManager.TriggerEmotionEvent("GreetingSayName", MoodManager::GetCurrentTimeInSeconds());
        return AnimationTrigger::NamedFaceInitialGreeting;
      });
  }
  else {
    turnAction->SetSayNameAnimationTrigger(AnimationTrigger::AcknowledgeFaceNamed);
  }
  
  // if it's not named, always play this one
  turnAction->SetNoNameAnimationTrigger(AnimationTrigger::AcknowledgeFaceUnnamed);
  
  turnAction->SetMaxFramesToWait(kNumImagesToWaitFor);

  StartActing(turnAction, &BehaviorAcknowledgeFace::FinishIteration);
} // InitInternalReactionary()


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorAcknowledgeFace::FinishIteration(BehaviorExternalInterface& behaviorExternalInterface)
{
  _desiredTargets.erase( _targetFace );
 
  // notify the listeners that a face reaction has completed fully
  for(auto& listener: _faceListeners){
    listener->FinishedReactingToFace(behaviorExternalInterface, _targetFace);
  }
  
  BehaviorObjectiveAchieved(BehaviorObjective::ReactedAcknowledgedFace);
  // move on to the next target, if there is one
  BeginIteration(behaviorExternalInterface);
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorAcknowledgeFace::AddListener(IReactToFaceListener* listener)
{
  _faceListeners.insert(listener);
}



} // namespace Cozmo
} // namespace Anki


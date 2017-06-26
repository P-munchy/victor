/**
* File: reactionTriggerStrategyVoiceCommand.cpp
*
* Author: Lee Crippen
* Created: 02/16/17
*
* Description: Reaction trigger strategy for hearing a voice command.
*
* Copyright: Anki, Inc. 2017
*
*
**/


#include "anki/common/basestation/utils/timer.h"
#include "anki/cozmo/basestation/behaviorSystem/behaviorPreReqs/behaviorPreReqAcknowledgeFace.h"
#include "anki/cozmo/basestation/behaviorSystem/reactionTriggerStrategies/reactionTriggerStrategyVoiceCommand.h"
#include "anki/cozmo/basestation/behaviorSystem/behaviors/iBehavior.h"
#include "anki/cozmo/basestation/cozmoContext.h"
#include "anki/cozmo/basestation/faceWorld.h"
#include "anki/cozmo/basestation/robot.h"
#include "anki/cozmo/basestation/robotIdleTimeoutComponent.h"
#include "anki/cozmo/basestation/voiceCommands/voiceCommandComponent.h"
#include "util/console/consoleInterface.h"

#define LOG_CHANNEL "VoiceCommands"
#define LOG_INFO(...) PRINT_CH_INFO(LOG_CHANNEL, ##__VA_ARGS__)

namespace Anki {
namespace Cozmo {

using namespace ExternalInterface;

namespace{
static const char* kTriggerStrategyName = "Trigger Strategy Voice Command";

static const char* kVoiceCommandParamsKey = "voiceCommandParams";
static const char* kIsWakeUpReaction      = "isWakeUpReaction";
}
  
ReactionTriggerStrategyVoiceCommand::ReactionTriggerStrategyVoiceCommand(Robot& robot, const Json::Value& config)
: IReactionTriggerStrategy(robot, config, kTriggerStrategyName)
{
  const auto& params = config[kVoiceCommandParamsKey];
  JsonTools::GetValueOptional(params, kIsWakeUpReaction, _isWakeUpReaction);
}

void ReactionTriggerStrategyVoiceCommand::SetupForceTriggerBehavior(const Robot& robot, const IBehaviorPtr behavior)
{
  std::set<Vision::FaceID_t> targets;
  targets.insert(GetDesiredFace(robot));
  BehaviorPreReqAcknowledgeFace acknowledgeFacePreReqs(targets, robot);
  
  behavior->IsRunnable(acknowledgeFacePreReqs);
}
  
bool ReactionTriggerStrategyVoiceCommand::ShouldTriggerBehaviorInternal(const Robot& robot, const IBehaviorPtr behavior)
{
  auto* voiceCommandComponent = robot.GetContext()->GetVoiceCommandComponent();
  if (!ANKI_VERIFY(voiceCommandComponent, "ReactionTriggerStrategyVoiceCommand.ShouldTriggerBehaviorInternal", "VoiceCommandComponent invalid"))
  {
    return false;
  }
  
  if(voiceCommandComponent->KeyPhraseWasHeard())
  {
    const bool robotHasIdleTimeout = robot.GetIdleTimeoutComponent().IdleTimeoutSet();
   
    // If the robot has an idle timeout set (game sets this when Cozmo is going to sleep) and this is
    // the strategy instance that is responsible for managing the "Hey Cozmo" wake up from/cancel sleep
    // behavior then that behavior should run
    if(robotHasIdleTimeout && _isWakeUpReaction)
    {
      voiceCommandComponent->ClearHeardCommand();
      
      DEV_ASSERT(behavior->GetID() == BehaviorID::ReactToVoiceCommand_Wakeup,
                 "ReactionTriggerStrategyVoiceCommand.ShouldTriggerBehaviorInternal.ExpectedWakeUpReaction");
    
      BehaviorPreReqNone req;
      return behavior->IsRunnable(req);
    }
    // Otherwise Cozmo is not going to sleep so the normal "Hey Cozmo" reaction can run
    else if(!robotHasIdleTimeout && !_isWakeUpReaction)
    {
      voiceCommandComponent->ClearHeardCommand();
      
      Vision::FaceID_t desiredFace = GetDesiredFace(robot);
      
      if (Vision::UnknownFaceID != desiredFace)
      {
        _lookedAtTimesMap[desiredFace] = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
      }
      
      std::set<Vision::FaceID_t> targets;
      targets.insert(desiredFace);
      BehaviorPreReqAcknowledgeFace acknowledgeFacePreReqs(targets, robot);
      
      LOG_INFO("ReactionTriggerStrategyVoiceCommand.ShouldTriggerBehaviorInternal.DesiredFace", "DesiredFaceID: %d", desiredFace);
      return behavior->IsRunnable(acknowledgeFacePreReqs);
    }
  }
  
  return false;
}

Vision::FaceID_t ReactionTriggerStrategyVoiceCommand::GetDesiredFace(const Robot& robot) const
{
  // All recently seen face IDs
  const auto& knownFaceIDs = robot.GetFaceWorld().GetFaceIDs();
  Vision::FaceID_t desiredFace = Vision::UnknownFaceID;
  auto oldestTimeLookedAt_s = std::numeric_limits<float>::max();
  
  for (const auto& faceID : knownFaceIDs)
  {
    // If we don't know where this face is right now, continue on
    const auto* face = robot.GetFaceWorld().GetFace(faceID);
    Pose3d pose;
    if(nullptr == face || !face->GetHeadPose().GetWithRespectTo(robot.GetPose(), pose))
    {
      continue;
    }
    
    auto dataIter = _lookedAtTimesMap.find(faceID);
    if (dataIter == _lookedAtTimesMap.end())
    {
      // If we don't have a time associated with looking at this face, break out now and use it cause it's new.
      desiredFace = faceID;
      break;
    }
    
    const auto& curLookedTime = dataIter->second;
    if (curLookedTime < oldestTimeLookedAt_s)
    {
      desiredFace = faceID;
      oldestTimeLookedAt_s = curLookedTime;
    }
  }
  
  return desiredFace;
}

} // namespace Cozmo
} // namespace Anki

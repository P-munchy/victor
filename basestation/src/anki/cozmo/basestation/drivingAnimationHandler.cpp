/**
 * File: drivingAnimationHandler.cpp
 *
 * Author: Al Chaussee
 * Date:   5/6/2016
 *
 * Description: Handles playing animations while driving
 *              Whatever tracks are locked by the action will stay locked while the start and loop
 *              animations but the tracks will be unlocked while the end animation plays
 *              The end animation will always play and will cancel the start/loop animations if needed
 *
 *
 * Copyright: Anki, Inc. 2016
 **/

#include "anki/cozmo/basestation/actions/animActions.h"
#include "anki/cozmo/basestation/components/movementComponent.h"
#include "anki/cozmo/basestation/components/pathComponent.h"
#include "anki/cozmo/basestation/drivingAnimationHandler.h"
#include "anki/cozmo/basestation/externalInterface/externalInterface.h"
#include "anki/cozmo/basestation/moodSystem/moodManager.h"
#include "anki/cozmo/basestation/robot.h"
#include "util/console/consoleInterface.h"

namespace Anki {
  namespace Cozmo {

    // Which docking method actions should use
    CONSOLE_VAR(bool, kEnableDrivingAnimations, "DrivingAnimationHandler", true);
    
    
    DrivingAnimationHandler::DrivingAnimationHandler(Robot& robot)
    : _robot(robot)
    , kDefaultDrivingAnimations({AnimationTrigger::DriveStartDefault,
                                 AnimationTrigger::DriveLoopDefault,
                                 AnimationTrigger::DriveEndDefault})
    , kAngryDrivingAnimations({AnimationTrigger::DriveStartAngry,
                               AnimationTrigger::DriveLoopAngry,
                               AnimationTrigger::DriveEndAngry})
    {

      _currDrivingAnimations = kDefaultDrivingAnimations;
      
      if(_robot.HasExternalInterface())
      {
        _signalHandles.push_back(_robot.GetExternalInterface()->Subscribe(
                                   ExternalInterface::MessageEngineToGameTag::RobotCompletedAction,
        [this](const AnkiEvent<ExternalInterface::MessageEngineToGame>& event)
        {
          DEV_ASSERT(event.GetData().GetTag() == ExternalInterface::MessageEngineToGameTag::RobotCompletedAction,
                     "Wrong event type from callback");
          HandleActionCompleted(event.GetData().Get_RobotCompletedAction());
        } ));
        
        _signalHandles.push_back(_robot.GetExternalInterface()->Subscribe(
                                   ExternalInterface::MessageGameToEngineTag::PushDrivingAnimations,
        [this](const AnkiEvent<ExternalInterface::MessageGameToEngine>& event)
        {
          auto const& payload = event.GetData().Get_PushDrivingAnimations();
          PushDrivingAnimations({payload.drivingStartAnim, payload.drivingLoopAnim, payload.drivingEndAnim});
        }));
        
        _signalHandles.push_back(_robot.GetExternalInterface()->Subscribe(
                                   ExternalInterface::MessageGameToEngineTag::PopDrivingAnimations,
        [this](const AnkiEvent<ExternalInterface::MessageGameToEngine>& event)
        {
          PopDrivingAnimations();
        }));
      }
    }
    
    void DrivingAnimationHandler::PushDrivingAnimations(const DrivingAnimations& drivingAnimations)
    {
      if(_state != AnimState::ActionDestroyed)
      {
        PRINT_NAMED_WARNING("DrivingAnimationHandler.PushDrivingAnimations",
                            "Pushing new animations while currently playing");
      }
      _drivingAnimationStack.push_back(drivingAnimations);
    }
    
    void DrivingAnimationHandler::PopDrivingAnimations()
    {
      if(_state != AnimState::ActionDestroyed)
      {
        PRINT_NAMED_WARNING("DrivingAnimationHandler.PopDrivingAnimations",
                            "Popping animations while currently playing");
      }
    
      if (_drivingAnimationStack.empty())
      {
        PRINT_NAMED_WARNING("DrivingAnimationHandler.PopDrivingAnimations",
                            "Tried to pop animations but the stack is empty!");
      }
      else
      {
        _drivingAnimationStack.pop_back();
      }
    }

    void DrivingAnimationHandler::ClearAllDrivingAnimations()
    {
      _drivingAnimationStack.clear();
    }

    void DrivingAnimationHandler::UpdateCurrDrivingAnimations()
    {
      if( _drivingAnimationStack.empty() ) {
        // use mood to determine which anims to play
        if( _robot.GetMoodManager().GetSimpleMood() == SimpleMoodType::Sad ) {
          _currDrivingAnimations = kAngryDrivingAnimations;
        }
        else {
          _currDrivingAnimations = kDefaultDrivingAnimations;
        }
      }
      else {
        _currDrivingAnimations = _drivingAnimationStack.back();
      }
    }

    void DrivingAnimationHandler::HandleActionCompleted(const ExternalInterface::RobotCompletedAction& msg)
    {
      // Only start playing drivingLoop if start successfully completes
      if(msg.idTag == _drivingStartAnimTag && msg.result == ActionResult::SUCCESS)
      {
        if(_currDrivingAnimations.drivingLoopAnim != AnimationTrigger::Count)
        {
          PlayDrivingLoopAnim();
        }
      }
      else if(msg.idTag == _drivingLoopAnimTag)
      {
        const bool keepLooping = (_keepLoopingWithoutPath || _robot.GetPathComponent().HasPathToFollow());
        if(keepLooping && msg.result == ActionResult::SUCCESS)
        {
          PlayDrivingLoopAnim();
        }
        else
        {
          // Unlock our tracks so that endAnim can use them
          // This should be safe since we have finished driving
          if(_isActionLockingTracks)
          {
            _robot.GetMoveComponent().UnlockTracks(_tracksToUnlock, _actionTag);
          }
          
          PlayEndAnim();
        }
      }
      else if(msg.idTag == _drivingEndAnimTag)
      {
        _state = AnimState::FinishedEnd;
        
        // Relock tracks like nothing ever happend
        if(_isActionLockingTracks)
        {
          _robot.GetMoveComponent().LockTracks(_tracksToUnlock, _actionTag, "DrivingAnimations");
        }
      }
    }
    
    void DrivingAnimationHandler::ActionIsBeingDestroyed()
    {
      _state = AnimState::ActionDestroyed;
      
      _robot.GetActionList().Cancel(_drivingStartAnimTag);
      _robot.GetActionList().Cancel(_drivingLoopAnimTag);
      _robot.GetActionList().Cancel(_drivingEndAnimTag);
    }
    
    void DrivingAnimationHandler::Init(const u8 tracksToUnlock,
                                       const u32 tag,
                                       const bool isActionSuppressingLockingTracks,
                                       const bool keepLoopingWithoutPath)
    {
      UpdateCurrDrivingAnimations();
      
      _state = AnimState::Waiting;
      _drivingStartAnimTag = ActionConstants::INVALID_TAG;
      _drivingLoopAnimTag = ActionConstants::INVALID_TAG;
      _drivingEndAnimTag = ActionConstants::INVALID_TAG;
      _tracksToUnlock = tracksToUnlock;
      _actionTag = tag;
      _isActionLockingTracks = !isActionSuppressingLockingTracks;
      _keepLoopingWithoutPath = keepLoopingWithoutPath;
    }
    
    void DrivingAnimationHandler::PlayStartAnim()
    {
      if (!kEnableDrivingAnimations) {
        return;
      }
      
      // Don't do anything until Init is called
      if(_state != AnimState::Waiting)
      {
        return;
      }

      if(_currDrivingAnimations.drivingStartAnim != AnimationTrigger::Count)
      {
        PlayDrivingStartAnim();
      }
      else if(_currDrivingAnimations.drivingLoopAnim != AnimationTrigger::Count)
      {
        PlayDrivingLoopAnim();
      }
    }
    
    bool DrivingAnimationHandler::PlayEndAnim()
    {
      if (!kEnableDrivingAnimations) {
        return false;
      }
      
      // The end anim can interrupt the start and loop animations
      // If we are currently playing the end anim or have already completed it don't play it again
      if(_state == AnimState::PlayingEnd ||
         _state == AnimState::FinishedEnd ||
         _state == AnimState::ActionDestroyed)
      {
        return false;
      }
      
      _robot.GetActionList().Cancel(_drivingStartAnimTag);
      _robot.GetActionList().Cancel(_drivingLoopAnimTag);
      
      if(_currDrivingAnimations.drivingEndAnim != AnimationTrigger::Count)
      {
        // Unlock our tracks so that endAnim can use them
        // This should be safe since we have finished driving
        if(_isActionLockingTracks)
        {
          _robot.GetMoveComponent().UnlockTracks(_tracksToUnlock, _actionTag);
        }
        
        PlayDrivingEndAnim();
        return true;
      }
      return false;;
    }
  
    void DrivingAnimationHandler::PlayDrivingStartAnim()
    {
      _state = AnimState::PlayingStart;
      IActionRunner* animAction = new TriggerAnimationAction(_robot, _currDrivingAnimations.drivingStartAnim, 1, true);
      _drivingStartAnimTag = animAction->GetTag();
      _robot.GetActionList().QueueAction(QueueActionPosition::IN_PARALLEL, animAction);
    }
    
    void DrivingAnimationHandler::PlayDrivingLoopAnim()
    {
      _state = AnimState::PlayingLoop;
      IActionRunner* animAction = new TriggerAnimationAction(_robot, _currDrivingAnimations.drivingLoopAnim, 1, true);
      _drivingLoopAnimTag = animAction->GetTag();
      _robot.GetActionList().QueueAction(QueueActionPosition::IN_PARALLEL, animAction);
    }
    
    void DrivingAnimationHandler::PlayDrivingEndAnim()
    {
      if(_state == AnimState::PlayingEnd ||
         _state == AnimState::FinishedEnd ||
         _state == AnimState::ActionDestroyed)
      {
        return;
      }
      
      _state = AnimState::PlayingEnd;
      IActionRunner* animAction = new TriggerAnimationAction(_robot, _currDrivingAnimations.drivingEndAnim, 1, true);
      _drivingEndAnimTag = animAction->GetTag();
      _robot.GetActionList().QueueAction(QueueActionPosition::IN_PARALLEL, animAction);
    }
  }
}

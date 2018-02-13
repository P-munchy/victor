/**
 * File: animActions.h
 *
 * Author: Andrew Stein
 * Date:   8/29/2014
 *
 * Description: Implements animation and audio cozmo-specific actions, derived from the IAction interface.
 *
 *
 * Copyright: Anki, Inc. 2014
 **/

#ifndef ANKI_COZMO_ANIM_ACTIONS_H
#define ANKI_COZMO_ANIM_ACTIONS_H

#include "engine/actions/actionInterface.h"
#include "engine/actions/compoundActions.h"
#include "engine/components/animationComponent.h"
#include "coretech/common/engine/math/pose.h"
#include "anki/cozmo/shared/cozmoConfig.h"
#include "anki/cozmo/shared/cozmoEngineConfig.h"
#include "clad/externalInterface/messageActions.h"
#include "clad/types/actionTypes.h"
#include "clad/types/animationTypes.h"
#include "clad/types/animationTrigger.h"


namespace Anki {
  
  namespace Cozmo {

    class PlayAnimationAction : public IAction
    {
    public:
    
      // Numloops 0 causes the action to loop forever
      // tracksToLock indicates tracks of the animation which should not play
      PlayAnimationAction(const std::string& animName,
                          u32 numLoops = 1,
                          bool interruptRunning = true,
                          u8 tracksToLock = (u8)AnimTrackFlag::NO_TRACKS,
                          float timeout_sec = _kDefaultTimeout_sec);
      
      virtual ~PlayAnimationAction();
      
      virtual void GetCompletionUnion(ActionCompletedUnion& completionUnion) const override;
      
      virtual f32 GetTimeoutInSeconds() const override { return _timeout_sec; }
      
    protected:
      
      virtual ActionResult Init() override;
      virtual ActionResult CheckIfDone() override;
      
      std::string               _animName;
      u32                       _numLoopsRemaining;
      bool                      _stoppedPlaying;
      bool                      _wasAborted;
      bool                      _interruptRunning;
      float                     _timeout_sec = _kDefaultTimeout_sec;
      
      static constexpr float _kDefaultTimeout_sec = 60.f;
      static constexpr float _kDefaultTimeoutForInfiniteLoops_sec = std::numeric_limits<f32>::max();

    }; // class PlayAnimationAction


    class TriggerAnimationAction : public PlayAnimationAction
    {
    public:
      // Preferred constructor, used by the factory CreatePlayAnimationAction
      // Numloops 0 causes the action to loop forever
      explicit TriggerAnimationAction(AnimationTrigger animEvent,
                                      u32 numLoops = 1,
                                      bool interruptRunning = true,
                                      u8 tracksToLock = (u8)AnimTrackFlag::NO_TRACKS,
                                      float timeout_sec = _kDefaultTimeout_sec,
                                      bool strictCooldown = false);
      
    protected:
      virtual ActionResult Init() override;

      void SetAnimGroupFromTrigger(AnimationTrigger animTrigger);

      bool HasAnimTrigger() const { return _animTrigger != AnimationTrigger::Count; }
      virtual void OnRobotSet() override final;
      virtual void OnRobotSetInternalTrigger() {};


    private:
      AnimationTrigger _animTrigger;
      std::string _animGroupName;
      bool _strictCooldown;
      
    }; // class TriggerAnimationAction
    
    
    // A special subclass of TriggerAnimationAction which checks to see
    // if the robot is holding a cube and locks the tracks
    class TriggerLiftSafeAnimationAction : public TriggerAnimationAction
    {
    public:
      // Preferred constructor, used by the factory CreatePlayAnimationAction
      // Numloops 0 causes the action to loop forever
      explicit TriggerLiftSafeAnimationAction(AnimationTrigger animEvent,
                                              u32 numLoops = 1,
                                              bool interruptRunning = true,
                                              u8 tracksToLock = (u8)AnimTrackFlag::NO_TRACKS,
                                              float timeout_sec = _kDefaultTimeout_sec,
                                              bool strictCooldown = false);
      static u8 TracksToLock(Robot& robot, u8 tracksCurrentlyLocked);
    protected:
        virtual void OnRobotSetInternalTrigger() override final;
      
    };

    #pragma mark ---- DeviceAudioAction ----
    // TODO: JIRA VIC-29 - Reimplement DeviceAudioAction
    
    #pragma mark ---- RobotAudioAction ----
    // TODO: VIC-30 - Implement RobotAudioAction

  }
}

#endif /* ANKI_COZMO_ANIM_ACTIONS_H */

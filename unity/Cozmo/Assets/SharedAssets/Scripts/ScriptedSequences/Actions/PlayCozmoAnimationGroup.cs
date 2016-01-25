﻿using System;
using System.ComponentModel;

namespace ScriptedSequences.Actions {
  public class PlayCozmoAnimationGroup : ScriptedSequenceAction {

    public string AnimationGroupName;

    public bool LoopForever;

    [DefaultValue(true)]
    public bool WaitToEnd = true;

    public override ISimpleAsyncToken Act() {
      var robot = RobotEngineManager.Instance.CurrentRobot;

      SimpleAsyncToken token = new SimpleAsyncToken();

      if (robot == null) {
        token.Fail(new Exception("No Robot set!"));
        return token;
      }
      if (LoopForever) {
        bool loopComplete = false;
        token.OnAbort += () => { 
          loopComplete = true;
          robot.CancelAction(Anki.Cozmo.RobotActionType.PLAY_ANIMATION); 
        };
        Action playAnimation = null;

        playAnimation = () => {          
          robot.SendAnimationGroup(AnimationGroupName, (s) => { 
            if(!loopComplete) {
              playAnimation();
            }
          });
        };

        playAnimation();
      }
      else if (WaitToEnd) {
        token.OnAbort += () => { robot.CancelAction(Anki.Cozmo.RobotActionType.PLAY_ANIMATION); };
        robot.SendAnimationGroup(AnimationGroupName, (s) => { 
          // Do we want to fail the action if playing the animation failed?
          token.Succeed();
        });
      }
      else {
        robot.SendAnimationGroup(AnimationGroupName);
        token.Succeed();
      }
      return token;
    }
  }
}


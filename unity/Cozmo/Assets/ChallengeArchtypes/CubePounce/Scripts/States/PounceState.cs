﻿using UnityEngine;
using System.Collections;

namespace CubePounce {
  public class PounceState : State {

    private CubePounceGame _CubeSlapGame;
    private float _SlapDelay;
    private float _FirstTimestamp = -1;
    //private float _LastSeenTimeStamp = -1;
    public bool _SlapTriggered = false;

    public override void Enter() {
      base.Enter();
      _CubeSlapGame = (_StateMachine.GetGame() as CubePounceGame);
      _SlapDelay = _CubeSlapGame.GetSlapDelay();
      _CurrentRobot.SetHeadAngle(CozmoUtil.kIdealBlockViewHeadValue);
      _CurrentRobot.SetLiftHeight(1.0f);
      _FirstTimestamp = Time.time;
      _SlapTriggered = false;
      _CubeSlapGame.GetCurrentTarget();
      _CubeSlapGame.SharedMinigameView.InfoTitleText = Localization.Get(LocalizationKeys.kCubePounceHeaderWaitForPounce);
      _CubeSlapGame.SharedMinigameView.ShowInfoTextSlideWithKey(LocalizationKeys.kCubePounceInfoWaitForPounce);
      LightCube.OnMovedAction += HandleCubeMoved;
    }

    public override void Update() {
      base.Update();
      // Check to see if there's been a change of state to make sure that Cozmo hasn't been tampered with
      // and that players haven't already pulled the cube too early. If they have, return to the Seek state and automatically
      // trigger a failure on the player's part.
      if (!_SlapTriggered) {
        /*
        // If the slap hasn't been triggered, check to make sure the Cube hasn't been tampered with.
        // If the cube is not visible or it has moved outside of the ideal range, trigger a failure.
        LightCube target = _CubeSlapGame.GetCurrentTarget();
        if (target != null) {
          bool didFail = true;
          // Unless the cube is in the right position, trigger a failure since you moved it
          if (target.MarkersVisible) {
            float distance = Vector3.Distance(_CurrentRobot.WorldPosition, target.WorldPosition);
            if (distance < CubePounceGame.kCubePlaceDist) {
              didFail = false;
              ResetLastSeenTimeStamp();
            }
          }
          if (didFail) {
            if (_LastSeenTimeStamp == -1) {
              _LastSeenTimeStamp = Time.time;
            }
            if (Time.time - _LastSeenTimeStamp > CubePounceGame.kCubeLostDelay) {
              _CubeSlapGame.SharedMinigameView.InfoTitleText = Localization.Get(LocalizationKeys.kCubePounceHeaderCozmoWinEarly);
              _CubeSlapGame.SharedMinigameView.ShowInfoTextSlideWithKey(LocalizationKeys.kCubePounceInfoCozmoWinEarly);
              _CubeSlapGame.OnCozmoWin();
            }
          }
        }*/

        if (Time.time - _FirstTimestamp > _SlapDelay) {
          _CubeSlapGame.AttemptSlap();
          _SlapTriggered = true;
        }
      }
    }

    private void HandleCubeMoved(int id, float accX, float accY, float aaZ) {
      if (!_SlapTriggered && id == _CubeSlapGame.GetCurrentTarget().ID) {
        _CubeSlapGame.SharedMinigameView.InfoTitleText = Localization.Get(LocalizationKeys.kCubePounceHeaderCozmoWinEarly);
        _CubeSlapGame.SharedMinigameView.ShowInfoTextSlideWithKey(LocalizationKeys.kCubePounceInfoCozmoWinEarly);
        _CubeSlapGame.OnCozmoWin();
      }
    }

    private void ResetLastSeenTimeStamp() {
      //_LastSeenTimeStamp = -1;
    }

    public override void Exit() {
      base.Exit();
      LightCube.OnMovedAction -= HandleCubeMoved;
    }
  }
}


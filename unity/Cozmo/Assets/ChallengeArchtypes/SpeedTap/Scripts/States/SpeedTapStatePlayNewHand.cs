﻿using System;
using UnityEngine;

namespace SpeedTap {

  public class SpeedTapStatePlayNewHand : State {

    private SpeedTapGame _SpeedTapGame = null;
    private float _StartTimeMs = 0;
    private float _OnDelayTimeMs = 2000.0f;
    private float _OffDelayTimeMs = 2000.0f;
    private float _CozmoTapDelayTimeMs = 300.0f;
    private float _MatchProbability = 0.35f;

    private bool _LightsOn = false;
    private bool _GotMatch = false;
    private bool _CozmoTapping = false;

    enum WinState {
      Neutral,
      CozmoWins,
      PlayerWins
    }

    WinState curWinState_ = WinState.Neutral;

    Color[] colors = { Color.white, Color.red, Color.green, Color.blue, Color.magenta };

    public override void Enter() {
      base.Enter();
      _SpeedTapGame = _StateMachine.GetGame() as SpeedTapGame;
      _StartTimeMs = Time.time * 1000.0f;
      _SpeedTapGame.CozmoBlock.SetLEDs(0, 0, 0xFF);
      _SpeedTapGame.PlayerBlock.SetLEDs(0, 0, 0xFF);
      _LightsOn = false;

      _CurrentRobot.SetLiftHeight(1.0f);

      _SpeedTapGame.PlayerTappedBlockEvent += PlayerDidTap;
      RobotEngineManager.Instance.RobotCompletedAnimation += RobotCompletedTapAnimation;
    }

    public override void Update() {
      base.Update();

      float currTimeMs = Time.time * 1000.0f;

      if (_LightsOn) {
        if (_GotMatch) {
          if (!_CozmoTapping) {
            if ((currTimeMs - _StartTimeMs) >= _CozmoTapDelayTimeMs) {
              DAS.Info("SpeedTap.CozmoTapping", "" + _SpeedTapGame.CozmoScore);
              _CurrentRobot.SendAnimation(AnimationName.kTapCube);
              _CozmoTapping = true;
            }
          }
        }
        if ((currTimeMs - _StartTimeMs) >= _OnDelayTimeMs) {
          _SpeedTapGame.CozmoBlock.SetLEDs(0, 0, 0xFF);
          _SpeedTapGame.PlayerBlock.SetLEDs(0, 0, 0xFF);
          _LightsOn = false;
          _StartTimeMs = currTimeMs;
        }
      }
      else {
        if ((currTimeMs - _StartTimeMs) >= _OffDelayTimeMs) {
          curWinState_ = WinState.Neutral;
          RollForLights();
          _LightsOn = true;
          _StartTimeMs = currTimeMs;
        }
      }
    }

    public override void Exit() {
      base.Exit();

      RobotEngineManager.Instance.RobotCompletedAnimation -= RobotCompletedTapAnimation;
      _SpeedTapGame.PlayerTappedBlockEvent -= PlayerDidTap;
    }

    void RobotCompletedTapAnimation(bool success, string animName) {
      DAS.Info("SpeedTapStatePlayNewHand.RobotCompletedTapAnimation", animName + " success = " + success);
      switch (animName) {
      case AnimationName.kTapCube:
        DAS.Info("SpeedTapStatePlayNewHand.tap_complete", "");
        // check for player tapped first here.
        CozmoDidTap();
        break;
      case AnimationName.kFinishTabCubeLose:
        DAS.Info("SpeedTapStatePlayNewHand.tap_lose", "");
        _GotMatch = false;
        _CozmoTapping = false;
        break;
      case AnimationName.kFinishTapCubeWin:
        DAS.Info("SpeedTapStatePlayNewHand.tap_win", "");
        _GotMatch = false;
        _CozmoTapping = false;
        break;
      }

    }

    void CozmoDidTap() {
      DAS.Info("SpeedTapStatePlayNewHand.cozmo_tap", "");
      if (curWinState_ == WinState.Neutral) {
        curWinState_ = WinState.CozmoWins;
        _SpeedTapGame.CozmoScore++;
        _SpeedTapGame.UpdateUI();
        // play sound, do dance
        _CurrentRobot.SendAnimation(AnimationName.kFinishTapCubeWin);
      }
      // otherwise cozmo is too late!
      else {
        _CurrentRobot.SendAnimation(AnimationName.kFinishTabCubeLose);
      }
    }

    void BlockTapped(int blockID, int numTaps) {
      if (blockID == _SpeedTapGame.PlayerBlock.ID) {
        PlayerDidTap();
      }   
    }

    void PlayerDidTap() {
      DAS.Info("SpeedTapStatePlayNewHand.player_tap", "");
      if (_GotMatch) {
        if (curWinState_ == WinState.Neutral) {
          curWinState_ = WinState.PlayerWins;
          _SpeedTapGame.PlayerScore++;
          _SpeedTapGame.UpdateUI();
        }
      }
      else if (curWinState_ == WinState.Neutral && _LightsOn) {
        curWinState_ = WinState.CozmoWins;
        _SpeedTapGame.PlayerScore = Mathf.Max(0, _SpeedTapGame.PlayerScore - 1);
        _SpeedTapGame.UpdateUI();
      }
    }

    private void RollForLights() {
      _SpeedTapGame.RollingBlocks();

      float matchExperiment = UnityEngine.Random.value;
      if (matchExperiment <= _MatchProbability) {
        // Do match
        _GotMatch = true;
        int randColorIndex = ((int)(matchExperiment * 1000f)) % colors.Length;
        Color matchColor = colors[randColorIndex];
        _SpeedTapGame.CozmoBlock.SetLEDs(CozmoPalette.ColorToUInt(matchColor), 0, 0xFF);
        _SpeedTapGame.PlayerBlock.SetLEDs(CozmoPalette.ColorToUInt(matchColor), 0, 0xFF);
      }
      else {
        // Do non-match
        _GotMatch = false;
        int playerColorIdx = ((int)(matchExperiment * 1000f)) % colors.Length;
        int cozmoColorIdx = ((int)(matchExperiment * 3333f)) % colors.Length;
        if (cozmoColorIdx == playerColorIdx) {
          cozmoColorIdx = (cozmoColorIdx + 1) % colors.Length;
        }
        Color playerColor = colors[playerColorIdx];
        Color cozmoColor = colors[cozmoColorIdx];
        _SpeedTapGame.PlayerBlock.SetLEDs(CozmoPalette.ColorToUInt(playerColor), 0, 0xFF);
        _SpeedTapGame.CozmoBlock.SetLEDs(CozmoPalette.ColorToUInt(cozmoColor), 0, 0xFF);
      }
    }

  }

}

﻿using UnityEngine;
using System.Collections;
using System.Collections.Generic;
using System.Text;

namespace Vortex {

  public class VortexGame : GameBase {

    [SerializeField]
    private VortexPanel _GamePanelPrefab;
    private VortexPanel _GamePanel;

    class PlayerData {
      public int blockID;
      public Color color;
      public int score;
      public int robotID;
      public int currTaps;
      public string playerName;

      public PlayerData() {
        Reset();
      }

      public void Reset() {
        blockID = -1;
        color = Color.red;
        score = 0;
        robotID = -1;
        currTaps = 0;
        playerName = "Player";
      }
    }
    // TODO: these consts should be part of a difficulty config.
    private const int kMaxRounds = 5;
    public const int kSpinDegreesMin = 520;
    public const int kSpinDegreesMax = 1260;
    public const float kSpinTimeMin = 3.0f;
    public const float kSpinTimeMax = 5.0f;

    private const int kMaxPlayers = 4;
    private PlayerData[] _PlayerDataList;
    private int _RoundNumber;

    protected override void Initialize(MinigameConfigBase minigameConfig) {
      // TODO
      InitializeMinigameObjects(minigameConfig.NumCubesRequired());
    }

    protected void InitializeMinigameObjects(int numCubes) {
      DAS.Info(this, "VortexGame::Start");
      _GamePanel = UIManager.OpenView(_GamePanelPrefab);

      _GamePanel.HandleSpinEnded = HandleSpinEnded;
      _GamePanel.HandleSpinStarted = HandleSpinStarted;
      _GamePanel.HandleDebugTap = HandleDebugTap;
      _GamePanel.HandleReplayClicked = HandleReplaySelected;
      _GamePanel.SetLockSpinner(true);

      LightCube.TappedAction += HandleBlockTapped;

      // we need at least one for cozmo and one for at least one player but if we find more cool, stateintro will deal with it.
      InitialCubesState initCubeState = new InitialCubesState(new HowToPlayState(new StateIntro()), numCubes);
      _StateMachine.SetNextState(initCubeState);

      _RoundNumber = 0;
    }

    protected override void CleanUpOnDestroy() {
      if (_GamePanel != null) {
        UIManager.CloseViewImmediately(_GamePanel);
      }

      LightCube.TappedAction -= HandleBlockTapped;
    }

    private void HandleSpinStarted() {
      StateSpinWait next_state = new StateSpinWait();
      _StateMachine.SetNextState(next_state);
      _GamePanel.SetLockSpinner(true);
    }

    public void UpdateScoreText() {
      StringBuilder str = new StringBuilder();
      for (int i = 0; i < kMaxPlayers; ++i) {
        if (_PlayerDataList[i].blockID >= 0) {
          str.AppendLine(_PlayerDataList[i].playerName + " - " + _PlayerDataList[i].score.ToString());
        }
      }
      _GamePanel.SetScoreboardText(str.ToString());
    }

    private void HandleSpinEnded() {
      bool didCozmoWin = false;
      int wheelNumber = _GamePanel.GetWheelNumber();
      // points for guessing the number right, none for not guessing, 10 for getting right
      for (int i = 0; i < kMaxPlayers; ++i) {
        if (_PlayerDataList[i].currTaps == wheelNumber) {
          _PlayerDataList[i].score += 10;
          if (_PlayerDataList[i].robotID != -1) {
            didCozmoWin = true;
          }
        }
        else if (_PlayerDataList[i].currTaps != 0) {
          _PlayerDataList[i].score = 0;
        }
        // reset for next turn
        _PlayerDataList[i].currTaps = 0;
      }
      _StateMachine.SetNextState(new StateSpinComplete(didCozmoWin));
      UpdateScoreText();
    }

    public void HandleSpinResultsCompleted() {
      _RoundNumber++;
      // start next round
      if (_RoundNumber < kMaxRounds) {
        _StateMachine.SetNextState(new StateRequestSpin());
        _GamePanel.SetLockSpinner(false);
        // "Round: 1.
        _GamePanel.SetStatusText(Localization.GetWithArgs(LocalizationKeys.kVortexVortexStatus, _RoundNumber));
      }
      else {
        int high_score = -1;
        string str = "";
        for (int i = 0; i < kMaxPlayers; ++i) {
          if (_PlayerDataList[i].score > high_score) {
            high_score = _PlayerDataList[i].score;
            str = _PlayerDataList[i].playerName;
          }
        }
        _RoundNumber = 0;
        _GamePanel.SetStatusText(Localization.GetWithArgs(LocalizationKeys.kVortexVortexWon, str));
        _StateMachine.SetNextState(new StateOutro());
        _GamePanel.EnableReplayButton(true);
      }
      ResetLights();
    }

    public void HandleDebugTap() {
      HandleBlockTapped(1, 1);
    }

    public void HandleReplaySelected() {
      _GamePanel.SetStatusText("");
      _GamePanel.SetScoreboardText("Scoreboard");
      _StateMachine.SetNextState(new StateIntro());
      _GamePanel.EnableReplayButton(false);
    }

    public void ResetLights() {
      for (int i = 0; i < kMaxPlayers; ++i) {
        LightCube lightcube;
        if (_PlayerDataList[i].blockID != -1 &&
            CurrentRobot.LightCubes.TryGetValue(_PlayerDataList[i].blockID, out lightcube)) {
          lightcube.SetLEDs(0);
          lightcube.Lights[i].OnColor = _PlayerDataList[i].color.ToUInt();
        }
      }
    }

    public void HandleBlockTapped(int blockID, int tappedTimes) {
      DAS.Info(this, "Player Block Tapped. " + blockID);
      if (_PlayerDataList == null) {
        return;
      }

      PlayerData player = null;
      for (int i = 0; i < kMaxPlayers; ++i) {
        if (_PlayerDataList[i].blockID == blockID) {
          player = _PlayerDataList[i];
          player.currTaps += tappedTimes;
          break;
        }
      }

      LightCube lightcube;
      if (player != null && CurrentRobot.LightCubes.TryGetValue(blockID, out lightcube)) {
        // clear all and just set the sides we need.
        lightcube.SetLEDs(0);
        for (int j = 0; j < lightcube.Lights.Length; ++j) {
          if (j < player.currTaps) {
            lightcube.Lights[j].OnColor = player.color.ToUInt();
          }
        }
      }
    }

    public void OnIntroComplete(int cozmoBlockID) {
      // make players
      _PlayerDataList = new PlayerData[kMaxPlayers];
      for (int i = 0; i < kMaxPlayers; ++i) {
        _PlayerDataList[i] = new PlayerData();
      }

      int known_players = 0;
      // Cozmo is always blue, the rest is whatever
      Color[] kColors = { Color.red, Color.green, Color.magenta, Color.yellow };
      // Because tostring is all RGBA even when know colors
      string[] kPlayerNames = { "Red", "Green", "Magenta", "Yellow" };
      foreach (KeyValuePair<int, LightCube> lightCube in CurrentRobot.LightCubes) {
        Color set_color = kColors[known_players];
        _PlayerDataList[known_players].playerName = kPlayerNames[known_players];
        if (cozmoBlockID == lightCube.Value.ID) {
          set_color = Color.blue;
          _PlayerDataList[known_players].playerName = "Cozmo";
          _PlayerDataList[known_players].robotID = CurrentRobot.ID;
        }
        for (int j = 0; j < lightCube.Value.Lights.Length; ++j) {
          lightCube.Value.Lights[j].OnColor = set_color.ToUInt();
        }
        _PlayerDataList[known_players].blockID = lightCube.Key;
        _PlayerDataList[known_players].color = set_color;
        known_players++;
      }

      _StateMachine.SetNextState(new StateRequestSpin());
      _GamePanel.SetLockSpinner(false);
    }
  }

}

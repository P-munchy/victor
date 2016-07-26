﻿using UnityEngine;
using UnityEngine.UI;
using System.Collections;
using System.Collections.Generic;
using System;
using Cozmo.HomeHub;
using System.Linq;

public class MinigamePane : MonoBehaviour {
  // Force Win Game Buttons
  [SerializeField]
  private Button _CozmoGameButton;
  [SerializeField]
  private Button _PlayerGameButton;

  // Force Win Round Buttons
  [SerializeField]
  private Button _CozmoRoundButton;
  [SerializeField]
  private Button _PlayerRoundButton;

  // Force Rounds Won Buttons/Fields
  [SerializeField]
  private Button _SetCozmoRoundsButton;
  [SerializeField]
  private Button _SetPlayerRoundsButton;
  [SerializeField]
  private InputField _SetCozmoRoundsField;
  [SerializeField]
  private InputField _SetPlayerRoundsField;

  // Force Score Buttons/Fields
  [SerializeField]
  private Button _SetCozmoScoreButton;
  [SerializeField]
  private Button _SetPlayerScoreButton;
  [SerializeField]
  private InputField _SetCozmoScoreField;
  [SerializeField]
  private InputField _SetPlayerScoreField;

  // Use this for initialization
  void Start() {
    _CozmoGameButton.onClick.AddListener(CozmoWinGameClicked);
    _CozmoRoundButton.onClick.AddListener(CozmoRoundButtonClicked);
    _SetCozmoRoundsButton.onClick.AddListener(CozmoSetRoundsButtonClicked);
    _SetCozmoScoreButton.onClick.AddListener(CozmoSetScoreButtonClicked);

    _PlayerGameButton.onClick.AddListener(PlayerWinGameClicked);
    _PlayerRoundButton.onClick.AddListener(PlayerRoundButtonClicked);
    _SetPlayerRoundsButton.onClick.AddListener(PlayerSetRoundsButtonClicked);
    _SetPlayerScoreButton.onClick.AddListener(PlayerSetScoreButtonClicked);
	
  }

  private GameBase GetCurrMinigame() {
    if (HomeHub.Instance != null) {
      if (HomeHub.Instance.MiniGameInstance != null) {
        return HomeHub.Instance.MiniGameInstance;
      }
    }
    Debug.LogWarning("CurrentMinigame is NULL, Only use these commands during a Minigame");
    return null;
  }


  // Force Game Win For Cozmo
  private void CozmoWinGameClicked() {
    var minigame = GetCurrMinigame();
    if (minigame != null) {
      minigame.StartPointlessGameEnd(false);
    }
  }
  // Force Game Win For Player
  private void PlayerWinGameClicked() {
    var minigame = GetCurrMinigame();
    if (minigame != null) {
      minigame.StartPointlessGameEnd(true);
    }
  }

  /// <summary>
  /// Force Round Win for Cozmo, forces cozmo score to higher than player score and then ends the round
  /// </summary>
  private void CozmoRoundButtonClicked() {
    var minigame = GetCurrMinigame();
    if (minigame != null) {
      minigame.CozmoScore = Mathf.Max(minigame.PlayerScore + 1, minigame.CozmoScore);
      minigame.EndCurrentRound();
    }
  }

  /// <summary>
  /// Force Round Win for Player, forces player score to higher than cozmo score and then ends the round
  /// </summary>
  private void PlayerRoundButtonClicked() {
    var minigame = GetCurrMinigame();
    if (minigame != null) {
      minigame.PlayerScore = Mathf.Max(minigame.CozmoScore + 1, minigame.PlayerScore);
      minigame.EndCurrentRound();
    }
  }

  // Set Rounds Won for Cozmo
  private void CozmoSetRoundsButtonClicked() {
    var minigame = GetCurrMinigame();
    if (minigame != null) {
      minigame.CozmoRoundsWon = int.Parse(_SetCozmoRoundsField.text);
      minigame.UpdateUI();
    }
  }
  // Set Rounds Won for Player
  private void PlayerSetRoundsButtonClicked() {
    var minigame = GetCurrMinigame();
    if (minigame != null) {
      minigame.PlayerRoundsWon = int.Parse(_SetPlayerRoundsField.text);
      minigame.UpdateUI();
    }
  }

  // Set Score for Cozmo
  private void CozmoSetScoreButtonClicked() {
    var minigame = GetCurrMinigame();
    if (minigame != null) {
      minigame.CozmoScore = int.Parse(_SetCozmoScoreField.text);
      minigame.UpdateUI();
    }
  }
  // Set Score for Player
  private void PlayerSetScoreButtonClicked() {
    var minigame = GetCurrMinigame();
    if (minigame != null) {
      minigame.PlayerScore = int.Parse(_SetPlayerScoreField.text);
      minigame.UpdateUI();
    }
  }
}

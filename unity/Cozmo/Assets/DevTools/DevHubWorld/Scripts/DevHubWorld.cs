﻿using UnityEngine;
using System.Collections;

public class DevHubWorld : HubWorldBase {
 
  [SerializeField]
  private DevHubWorldDialog _DevHubWorldDialogPrefab;
  private DevHubWorldDialog _DevHubWorldDialogInstance;

  [SerializeField]
  private ChallengeDataList _ChallengeDataList;

  private GameBase _MiniGameInstance;

  public override bool LoadHubWorld() {
    ShowHubWorldDialog();
    // dev volume defaults to 0
    RobotEngineManager.Instance.CurrentRobot.SetRobotVolume(0.0f);
    return true;
  }

  public override bool DestroyHubWorld() {
    
    // Deregister events
    // Destroy dialog if it exists
    if (_DevHubWorldDialogInstance != null) {
      _DevHubWorldDialogInstance.OnDevButtonClicked -= HandleDevButtonClicked;
      _DevHubWorldDialogInstance.CloseViewImmediately();
    }
    
    CloseMiniGameImmediately();
    return true;
  }

  private void ShowHubWorldDialog() {
    // Create dialog with the game prefabs
    _DevHubWorldDialogInstance = UIManager.OpenView(_DevHubWorldDialogPrefab) as DevHubWorldDialog;
    _DevHubWorldDialogInstance.Initialize(_ChallengeDataList);
    
    // Listen for dialog button tap events
    _DevHubWorldDialogInstance.OnDevButtonClicked += HandleDevButtonClicked;
  }

  private void HandleDevButtonClicked(ChallengeData challenge) {
    _DevHubWorldDialogInstance.OnDevButtonClicked -= HandleDevButtonClicked;
    _DevHubWorldDialogInstance.CloseView();
    
    GameObject newMiniGameObject = GameObject.Instantiate(challenge.MinigamePrefab);
    _MiniGameInstance = newMiniGameObject.GetComponent<GameBase>();
    _MiniGameInstance.InitializeMinigame(challenge.MinigameConfig);
    _MiniGameInstance.OnMiniGameQuit += HandleMiniGameQuit;
    _MiniGameInstance.OnMiniGameLose += HandleMiniGameQuit;
    _MiniGameInstance.OnMiniGameWin += HandleMiniGameQuit;
  }

  private void HandleMiniGameQuit() {
    CloseMiniGame();
    ShowHubWorldDialog();
  }

  private void CloseMiniGame() {
    if (_MiniGameInstance != null) {
      _MiniGameInstance.CloseMinigame();
    }
  }

  private void CloseMiniGameImmediately() {
    if (_MiniGameInstance != null) {
      _MiniGameInstance.CloseMinigameImmediately();
    }
  }
}

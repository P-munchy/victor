﻿using UnityEngine;
using System.Collections;
using System.Collections.Generic;
using DataPersistence;
using Cozmo.Util;
using Anki.Assets;

namespace Cozmo.HomeHub {
  public class HomeHub : HubWorldBase {

    private static HomeHub _Instance = null;

    public static HomeHub Instance {
      get {
        if (_Instance == null) {
          DAS.Error("HomeHub.Instance", "NULL HomeHub Instance");
        }
        return _Instance;
      }
    }

    [SerializeField]
    private SerializableAssetBundleNames _MinigameDataPrefabAssetBundle;

    [SerializeField]
    private GameObjectDataLink _HomeViewPrefabData;

    private HomeView _HomeViewInstance;

    [SerializeField]
    private GameObjectDataLink _ChallengeDetailsPrefabData;

    private ChallengeDetailsDialog _ChallengeDetailsDialogInstance;

    [SerializeField]
    private ChallengeDataList _ChallengeDataList;

    private Dictionary<string, ChallengeStatePacket> _ChallengeStatesById;

    private GameBase _MiniGameInstance;

    public GameBase MiniGameInstance {
      get {
        return _MiniGameInstance;
      }
    }

    private CompletedChallengeData _CurrentChallengePlaying;

    public override bool LoadHubWorld() {
      RobotEngineManager.Instance.AddCallback<Anki.Cozmo.ExternalInterface.RequestSetUnlockResult>(RefreshChallengeUnlockInfo);
      _Instance = this;
      LoadChallengeData(_ChallengeDataList, out _ChallengeStatesById);
      StartLoadHomeView();
      return true;
    }

    public override bool DestroyHubWorld() {
      RobotEngineManager.Instance.RemoveCallback<Anki.Cozmo.ExternalInterface.RequestSetUnlockResult>(RefreshChallengeUnlockInfo);
      CloseMiniGameImmediately();

      // Deregister events
      // Destroy dialog if it exists
      if (_HomeViewInstance != null) {
        DeregisterDialogEvents();
        _HomeViewInstance.CloseViewImmediately();
      }

      return true;
    }

    private void RefreshChallengeUnlockInfo(object message) {
      LoadChallengeData(_ChallengeDataList, out _ChallengeStatesById);
      if (_HomeViewInstance != null) {
        _HomeViewInstance.SetChallengeStates(_ChallengeStatesById);
      }
    }

    private void LoadChallengeData(ChallengeDataList sourceChallenges,
                                   out Dictionary<string, ChallengeStatePacket> challengeStateByKey) {
      // Initial load of what's unlocked and completed from data
      challengeStateByKey = new Dictionary<string, ChallengeStatePacket>();

      // For each challenge
      ChallengeStatePacket statePacket;
      foreach (ChallengeData data in sourceChallenges.ChallengeData) {
        // Create a new data packet
        statePacket = new ChallengeStatePacket();
        statePacket.Data = data;

        // Determine the current state of the challenge
        statePacket.ChallengeUnlocked = UnlockablesManager.Instance.IsUnlocked(data.UnlockId.Value);
        // Add the challenge to the dictionary
        challengeStateByKey.Add(data.ChallengeID, statePacket);
      }
    }

    private void StartLoadHomeView() {
      LoadHomeViewAssetBundle(LoadHomeView);
    }

    private void LoadHomeViewAssetBundle(System.Action<bool> loadCallback) {
      AssetBundleManager.Instance.LoadAssetBundleAsync(_HomeViewPrefabData.AssetBundle, loadCallback);
    }

    private void LoadHomeView(bool assetBundleSuccess) {
      _HomeViewPrefabData.LoadAssetData((GameObject homeViewPrefab) => {
        StartCoroutine(ShowHomeViewAfterOtherViewClosed(homeViewPrefab));
      });
    }

    private IEnumerator ShowHomeViewAfterOtherViewClosed(GameObject homeViewPrefab) {
      while (_MiniGameInstance != null) {
        yield return 0;
      }

      _HomeViewInstance = UIManager.OpenView(homeViewPrefab.GetComponent<HomeView>());
      _HomeViewInstance.OnLockedChallengeClicked += HandleLockedChallengeClicked;
      _HomeViewInstance.OnUnlockedChallengeClicked += HandleUnlockedChallengeClicked;
      _HomeViewInstance.OnCompletedChallengeClicked += HandleCompletedChallengeClicked;

      // Show the current state of challenges being locked/unlocked
      _HomeViewInstance.Initialize(_ChallengeStatesById, this);

      Anki.Cozmo.Audio.GameAudioClient.SetMusicState(Anki.Cozmo.Audio.GameState.Music.Freeplay);

      var robot = RobotEngineManager.Instance.CurrentRobot;
      if (robot != null) {
        // Display Cozmo's default face
        robot.DisplayProceduralFace(
          0,
          Vector2.zero,
          Vector2.one,
          ProceduralEyeParameters.MakeDefaultLeftEye(),
          ProceduralEyeParameters.MakeDefaultRightEye());

        robot.SetEnableFreeplayBehaviorChooser(true);
        DailyGoalManager.Instance.MinigameConfirmed += HandleStartChallengeRequest;
      }
    }

    private void HandleLockedChallengeClicked(string challengeClicked, Transform buttonTransform) {
      // Do nothing for now
    }

    private void HandleUnlockedChallengeClicked(string challengeClicked, Transform buttonTransform) {
      OpenChallengeDetailsDialog(challengeClicked, buttonTransform);
    }

    private void HandleCompletedChallengeClicked(string challengeClicked, Transform buttonTransform) {
      OpenChallengeDetailsDialog(challengeClicked, buttonTransform);
    }

    private void OpenChallengeDetailsDialog(string challenge, Transform buttonTransform) {
      if (_ChallengeDetailsDialogInstance == null) {
        _ChallengeDetailsPrefabData.LoadAssetData((GameObject challengeDetailsPrefab) => {
          _ChallengeDetailsDialogInstance = UIManager.OpenView(challengeDetailsPrefab.GetComponent<ChallengeDetailsDialog>(),
            newView => {
              newView.Initialize(_ChallengeStatesById[challenge].Data, buttonTransform);
            });

          // React to when we should start the challenge.
          _ChallengeDetailsDialogInstance.ChallengeStarted += HandleStartChallengeClicked;
        });
      }
    }

    private void HandleStartChallengeClicked(string challengeClicked) {
      PlayMinigame(challengeClicked);
    }

    private void HandleStartChallengeRequest(string challengeRequested) {
      PlayMinigame(challengeRequested);
    }

    private void PlayMinigame(string challengeId) {
      // Keep track of the current challenge
      _CurrentChallengePlaying = new CompletedChallengeData() {
        ChallengeId = challengeId,
        StartTime = System.DateTime.UtcNow,
      };

      // Reset the robot behavior
      if (RobotEngineManager.Instance.CurrentRobot != null) {
        RobotEngineManager.Instance.CurrentRobot.SetEnableFreeplayBehaviorChooser(false);
      }

      // Close dialog
      CloseHomeView();
    }

    private void HandleHomeViewCloseAnimationFinished() {
      if (_HomeViewInstance != null) {
        _HomeViewInstance.ViewCloseAnimationFinished -= HandleHomeViewCloseAnimationFinished;
        _HomeViewInstance = null;
      }
      AssetBundleManager.Instance.UnloadAssetBundle(_HomeViewPrefabData.AssetBundle);

      LoadMinigameAssetBundle((bool prefabsSuccess) => {
        if (prefabsSuccess) {
          LoadMinigame(_ChallengeStatesById[_CurrentChallengePlaying.ChallengeId].Data);
        }
        else {
          // TODO show error dialog and boot to home
        }
      });
    }

    private void LoadMinigameAssetBundle(System.Action<bool> loadCallback) {
      AssetBundleManager.Instance.LoadAssetBundleAsync(
        _MinigameDataPrefabAssetBundle.Value.ToString(), loadCallback);
    }

    private void LoadMinigame(ChallengeData challengeData) {
      challengeData.LoadPrefabData((ChallengePrefabData prefabData) => {
        StartCoroutine(ShowMinigameAfterHomeViewCloses(challengeData, prefabData));
      });
    }

    private IEnumerator ShowMinigameAfterHomeViewCloses(ChallengeData challengeData, ChallengePrefabData prefabData) {
      while (_HomeViewInstance != null || _ChallengeDetailsDialogInstance != null) {
        yield return 0;
      }

      GameObject newMiniGameObject = Instantiate(prefabData.MinigamePrefab);
      _MiniGameInstance = newMiniGameObject.GetComponent<GameBase>();

      // OnSharedMinigameViewInitialized is called as part of the InitializeMinigame flow; 
      // On device this involves loading assets from data but in editor it may be instantaneous
      // so we need to listen to the event first and then initialize
      _MiniGameInstance.OnSharedMinigameViewInitialized += HandleSharedMinigameViewInitialized;
      _MiniGameInstance.InitializeMinigame(challengeData);

      _MiniGameInstance.OnShowEndGameDialog += HandleEndGameDialog;
      _MiniGameInstance.OnMiniGameWin += HandleMiniGameWin;
      _MiniGameInstance.OnMiniGameLose += HandleMiniGameLose;
      _MiniGameInstance.OnMiniGameQuit += HandleMiniGameQuit;

      RobotEngineManager.Instance.CurrentRobot.SetIdleAnimation(Anki.Cozmo.AnimationTrigger.Count);
    }

    private void HandleSharedMinigameViewInitialized(Cozmo.MinigameWidgets.SharedMinigameView newView) {
      _MiniGameInstance.OnSharedMinigameViewInitialized -= HandleSharedMinigameViewInitialized;
      newView.ViewCloseAnimationFinished += HandleMinigameFinishedClosing;
    }

    private void HandleEndGameDialog() {
      RobotEngineManager.Instance.CurrentRobot.SetEnableFreeplayBehaviorChooser(true);
      RobotEngineManager.Instance.CurrentRobot.SetVisionMode(Anki.Cozmo.VisionMode.DetectingFaces, true);
      RobotEngineManager.Instance.CurrentRobot.SetVisionMode(Anki.Cozmo.VisionMode.DetectingMarkers, true);
      RobotEngineManager.Instance.CurrentRobot.SetVisionMode(Anki.Cozmo.VisionMode.DetectingMotion, true);
      // TODO : Remove this once we have a more stable, permanent solution in Engine for false cliff detection
      RobotEngineManager.Instance.CurrentRobot.SetEnableCliffSensor(true);
    }

    private void HandleMiniGameWin() {
      HandleMiniGameCompleted(didWin: true);
    }

    private void HandleMiniGameLose() {
      HandleMiniGameCompleted(didWin: false);
    }

    private void HandleMiniGameCompleted(bool didWin) {
      // If we are in a challenge that needs to be completed, complete it
      if (_CurrentChallengePlaying != null) {
        CompleteChallenge(_CurrentChallengePlaying, didWin);
        _CurrentChallengePlaying = null;
      }
    }

    private void HandleMiniGameQuit() {
      // Reset the current challenge
      _CurrentChallengePlaying = null;
    }

    private void HandleMinigameFinishedClosing() {
      _MiniGameInstance.SharedMinigameView.ViewCloseAnimationFinished -= HandleMinigameFinishedClosing;
      _MiniGameInstance = null;
      UnloadMinigameAssetBundle();
      StartLoadHomeView();
    }

    private void UnloadMinigameAssetBundle() {
      AssetBundleManager.Instance.UnloadAssetBundle(_MinigameDataPrefabAssetBundle.Value.ToString());
    }

    private void CompleteChallenge(CompletedChallengeData completedChallenge, bool won) {
      // the last session is not necessarily valid as the 'CurrentSession', as its possible
      // the day rolled over while we were playing the challenge.
      var session = DataPersistenceManager.Instance.Data.DefaultProfile.Sessions.LastOrDefault();
      if (session == null) {
        DAS.Error(this, "Somehow managed to complete a challenge with no sessions saved!");
      }

      DataPersistenceManager.Instance.Save();
    }

    private void CloseMiniGameImmediately() {
      if (_MiniGameInstance != null) {
        DeregisterMinigameEvents();
        _MiniGameInstance.CloseMinigameImmediately();
      }
    }

    private void DeregisterMinigameEvents() {
      if (_MiniGameInstance != null) {
        _MiniGameInstance.OnMiniGameQuit -= HandleMiniGameQuit;
        _MiniGameInstance.OnMiniGameWin -= HandleMiniGameWin;
        _MiniGameInstance.OnMiniGameLose -= HandleMiniGameLose;
        _MiniGameInstance.OnShowEndGameDialog -= HandleEndGameDialog;
        _MiniGameInstance.OnSharedMinigameViewInitialized -= HandleSharedMinigameViewInitialized;
        if (MiniGameInstance.SharedMinigameView != null) {
          _MiniGameInstance.SharedMinigameView.ViewCloseAnimationFinished -= HandleMinigameFinishedClosing;
        }
      }
    }

    private void CloseHomeView() {
      if (_ChallengeDetailsDialogInstance != null) {
        _ChallengeDetailsDialogInstance.ChallengeStarted -= HandleStartChallengeClicked;
        _ChallengeDetailsDialogInstance.ViewCloseAnimationFinished += HandleCloseDetailsDialogAnimationFinished;
        _ChallengeDetailsDialogInstance.CloseView();
      }

      if (_HomeViewInstance != null) {
        DeregisterDialogEvents();
        _HomeViewInstance.ViewCloseAnimationFinished += HandleHomeViewCloseAnimationFinished;
        _HomeViewInstance.CloseView();
      }
    }

    private void HandleCloseDetailsDialogAnimationFinished() {
      _ChallengeDetailsDialogInstance.ViewCloseAnimationFinished -= HandleCloseDetailsDialogAnimationFinished;
      _ChallengeDetailsDialogInstance = null;
    }

    private void DeregisterDialogEvents() {
      if (_HomeViewInstance != null) {
        _HomeViewInstance.OnLockedChallengeClicked -= HandleLockedChallengeClicked;
        _HomeViewInstance.OnUnlockedChallengeClicked -= HandleUnlockedChallengeClicked;
        _HomeViewInstance.OnCompletedChallengeClicked -= HandleCompletedChallengeClicked;
      }
      DailyGoalManager.Instance.MinigameConfirmed -= HandleStartChallengeRequest;
    }

    #region Testing

    public void TestLoadTimeline() {
      DestroyHubWorld();
      LoadHubWorld();
    }

    public void TestCompleteChallenge(string completedChallengeId) {
      if (_HomeViewInstance != null) {

        CompleteChallenge(new CompletedChallengeData() {
          ChallengeId = completedChallengeId
        }, true);

        // Force refresh of the dialog
        DeregisterDialogEvents();
        _HomeViewInstance.CloseViewImmediately();
        StartLoadHomeView();
      }
    }

    #endregion
  }

  public class ChallengeStatePacket {
    public ChallengeData Data;
    public bool ChallengeUnlocked;
  }
}

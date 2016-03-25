﻿using UnityEngine;
using UnityEngine.UI;
using Cozmo.UI;
using System.Collections;
using System.Collections.Generic;
using Cozmo.MinigameWidgets;
using DG.Tweening;
using Anki.Cozmo;
using System.Linq;
using Cozmo.Util;

// Provides common interface for HubWorlds to react to games
// ending and to start/restart games. Also has interface for killing games
public abstract class GameBase : MonoBehaviour {

  private System.Guid? _GameUUID;

  public delegate void MiniGameQuitHandler();

  public event MiniGameQuitHandler OnMiniGameQuit;

  public delegate void MiniGameWinHandler(StatContainer rewardedXp,Transform[] rewardIcons);

  public event MiniGameWinHandler OnMiniGameWin;

  public delegate void MiniGameLoseHandler(StatContainer rewardedXp,Transform[] rewardIcons);

  public event MiniGameWinHandler OnMiniGameLose;

  public IRobot CurrentRobot { get { return RobotEngineManager.Instance != null ? RobotEngineManager.Instance.CurrentRobot : null; } }

  private SharedMinigameView _SharedMinigameViewInstance;

  public SharedMinigameView SharedMinigameView {
    get { return _SharedMinigameViewInstance; }
  }

  public Anki.Cozmo.Audio.GameState.Music GetDefaultMusicState() {
    return _ChallengeData.DefaultMusic;
  }

  protected Transform SharedMinigameViewInstanceParent { get { return _SharedMinigameViewInstance.transform; } }

  protected ChallengeData _ChallengeData;
  private ChallengeEndedDialog _ChallengeEndViewInstance;
  private bool _WonChallenge;

  protected StateMachine _StateMachine = new StateMachine();

  private StatContainer _RewardedXp;

  private float _GameStartTime;

  public List<LightCube> CubesForGame;

  private Dictionary<int, CycleData> _CubeCycleTimers;

  private class CycleData {
    public int cubeID;
    public float timeElaspedSeconds;
    public float cycleIntervalSeconds;
    public Color[] cycleColors;
    public int colorIndex;
  }

  #region Initialization

  public void InitializeMinigame(ChallengeData challengeData) {
    _GameStartTime = Time.time;
    _StateMachine.SetGameRef(this);

    _ChallengeData = challengeData;
    _WonChallenge = false;

    Anki.Cozmo.Audio.GameAudioClient.SetMusicState(GetDefaultMusicState());

    RobotEngineManager.Instance.CurrentRobot.TurnTowardsLastFacePose(Mathf.PI, FinishTurnToFace);

    _CubeCycleTimers = new Dictionary<int, CycleData>();
  }

  private void FinishTurnToFace(bool success) {
    _SharedMinigameViewInstance = UIManager.OpenView(
      UIPrefabHolder.Instance.SharedMinigameViewPrefab, 
      newView => {
        newView.Initialize(_ChallengeData.HowToPlayDialogContentPrefab,
          _ChallengeData.HowToPlayDialogContentLocKey);
        InitializeView(newView, _ChallengeData);
      });

    Initialize(_ChallengeData.MinigameConfig);
    _SharedMinigameViewInstance.QuitMiniGameConfirmed += HandleQuitConfirmed;

    DAS.Event(DASConstants.Game.kStart, GetGameUUID());
    DAS.Event(DASConstants.Game.kType, GetDasGameName());

  }

  protected abstract void Initialize(MinigameConfigBase minigameConfigData);

  protected virtual void InitializeView(SharedMinigameView newView, ChallengeData data) {
    // For all challenges, set the title text and add a quit button by default
    ChallengeTitleWidget titleWidget = newView.TitleWidget;
    titleWidget.Text = Localization.Get(data.ChallengeTitleLocKey);
    titleWidget.Icon = data.ChallengeIcon;
    newView.ShowBackButton();
  }

  #endregion

  // end Initialization

  #region Update

  protected virtual void Update() {
    UpdateCubeCycleLights();
    UpdateStateMachine();
  }

  protected virtual void UpdateStateMachine() {
    _StateMachine.UpdateStateMachine();
  }

  #endregion

  // end Update

  #region Clean Up

  /// <summary>
  /// Clean up listeners and extra game objects. Called before the game is 
  /// destroyed when the player quits or the robot loses connection.
  /// </summary>
  protected abstract void CleanUpOnDestroy();

  public void OnDestroy() {
    DAS.Event(DASConstants.Game.kEnd, GetGameTimeElapsedAsStr());
    if (_SharedMinigameViewInstance != null) {
      _SharedMinigameViewInstance.CloseViewImmediately();
      _SharedMinigameViewInstance = null;
    }
    DAS.Info(this, "Finished GameBase On Destroy");
  }

  public void CloseMinigameImmediately() {
    DAS.Info(this, "Close Minigame Immediately");
    if (CurrentRobot != null) {
      CurrentRobot.ResetRobotState(EndGameRobotReset);
    }
    CleanUpOnDestroy();
    Destroy(gameObject);
  }

  public void EndGameRobotReset() {
    RobotEngineManager.Instance.CurrentRobot.SetBehaviorSystem(true);
    RobotEngineManager.Instance.CurrentRobot.ActivateBehaviorChooser(Anki.Cozmo.BehaviorChooserType.Default);
    CurrentRobot.SetVisionMode(VisionMode.DetectingFaces, true);
    CurrentRobot.SetVisionMode(VisionMode.DetectingMarkers, true);
    CurrentRobot.SetVisionMode(VisionMode.DetectingMotion, true);
    // TODO : Remove this once we have a more stable, permanent solution in Engine for false cliff detection
    CurrentRobot.SetEnableCliffSensor(true);
  }

  #endregion

  // end Clean Up

  #region Calculate Stats

  protected virtual int CalculateTimeStatRewards() {
    return Mathf.CeilToInt((Time.time - _GameStartTime) / 30.0f);
  }

  protected virtual int CalculateNoveltyStatRewards() {
    const int maxPoints = 5;

    // sessions are in chronological order, completed challenges are as well.
    // using Reversed gets them in reverse chronological order
    var completedChallenges = 
      DataPersistence.DataPersistenceManager.Instance.Data.Sessions
          .Reversed().SelectMany(x => x.CompletedChallenges.Reversed());

    int noveltyPoints = 0;
    bool found = false;
    foreach (var challenge in completedChallenges) {
      if (challenge.ChallengeId == this._ChallengeData.ChallengeID || noveltyPoints == maxPoints) {
        found = true;
        break;
      }
      noveltyPoints++;
    }
    return found ? noveltyPoints : maxPoints;
  }

  // should be override for each mini game that wants to grant excitement rewards.
  protected virtual int CalculateExcitementStatRewards() {
    return 0;
  }

  private int ComputeXpForStat(Anki.Cozmo.ProgressionStatType statType) {
    switch (statType) {
    case Anki.Cozmo.ProgressionStatType.Time:
      return CalculateTimeStatRewards();
    case Anki.Cozmo.ProgressionStatType.Novelty:
      return CalculateNoveltyStatRewards();
    case Anki.Cozmo.ProgressionStatType.Excitement:
      return CalculateExcitementStatRewards();
    default: 
      return 0;
    }
  }

  #endregion

  // end Calculate Stats

  #region Minigame Exit

  protected void RaiseMiniGameQuit() {
    _StateMachine.Stop();

    DAS.Event(DASConstants.Game.kQuit, GetQuitGameState());
    if (OnMiniGameQuit != null) {
      OnMiniGameQuit();
    }

    CloseMinigameImmediately();
  }

  public void RaiseMiniGameWin(string subtitleText = null) {
    _StateMachine.Stop();
    _WonChallenge = true;

    Anki.Cozmo.Audio.GameAudioClient.PostSFXEvent(Anki.Cozmo.Audio.GameEvent.SFX.SharedWin);

    UpdateScoreboard(_WonChallenge);
    OpenChallengeEndedDialog(subtitleText);
  }

  public void RaiseMiniGameLose(string subtitleText = null) {
    _StateMachine.Stop();
    _WonChallenge = false;

    Anki.Cozmo.Audio.GameAudioClient.PostSFXEvent(Anki.Cozmo.Audio.GameEvent.SFX.SharedLose);

    UpdateScoreboard(_WonChallenge);
    OpenChallengeEndedDialog(subtitleText);
  }

  private void UpdateScoreboard(bool didPlayerWin) {
    ScoreWidget cozmoScoreboard = _SharedMinigameViewInstance.CozmoScoreboard;
    cozmoScoreboard.Dim = false;
    cozmoScoreboard.IsWinner = !didPlayerWin;
    ScoreWidget playerScoreboard = _SharedMinigameViewInstance.PlayerScoreboard;
    playerScoreboard.Dim = false;
    playerScoreboard.IsWinner = didPlayerWin;
  }

  private void OpenChallengeEndedDialog(string subtitleText = null) {
    // Open confirmation dialog instead
    GameObject challengeEndSlide = _SharedMinigameViewInstance.ShowNarrowGameStateSlide(
                                     UIPrefabHolder.Instance.ChallengeEndViewPrefab.gameObject, 
                                     "challenge_end_slide");
    _ChallengeEndViewInstance = challengeEndSlide.GetComponent<ChallengeEndedDialog>();
    _ChallengeEndViewInstance.SetupDialog(subtitleText);

    if (CurrentRobot != null) {
      CurrentRobot.ResetRobotState(EndGameRobotReset);
      // Disable all Request game behavior groups while in this view, Timeline View will handle renabling these
      // if appropriate.
      DailyGoalManager.Instance.DisableRequestGameBehaviorGroups();
    }

    // Listen for dialog close
    SharedMinigameView.ShowContinueButtonCentered(HandleChallengeResultViewClosed,
      Localization.Get(LocalizationKeys.kButtonContinue));
    

    _RewardedXp = new StatContainer();

    foreach (var statType in StatContainer.sKeys) {
      // Check that this is a goal xp
      if (DailyGoalManager.Instance.HasGoalForStat(statType)) {
        int grantedXp = ComputeXpForStat(statType);
        if (grantedXp != 0) {
          _RewardedXp[statType] = grantedXp;
          _ChallengeEndViewInstance.AddReward(statType, grantedXp);

          // TODO: Move granting to after animation?
          // Grant right away even if there are animations in the daily goal ui
          CurrentRobot.AddToProgressionStat(statType, grantedXp);
        }
      }
    }
  }

  private void HandleChallengeResultViewClosed() {
    // Get unparented reward icons
    Transform[] rewardIconObjects = _ChallengeEndViewInstance.GetRewardIconsByStat();

    // Pass icons and xp to HomeHub
    if (_WonChallenge) {
      DAS.Event(DASConstants.Game.kEndWithRank, DASConstants.Game.kRankPlayerWon);
      if (OnMiniGameWin != null) {
        OnMiniGameWin(_RewardedXp, rewardIconObjects);
      } 
    }
    else {
      DAS.Event(DASConstants.Game.kEndWithRank, DASConstants.Game.kRankPlayerLose);
      if (OnMiniGameLose != null) {
        OnMiniGameLose(_RewardedXp, rewardIconObjects);
      }
    }

    SendEventForRewards(_RewardedXp);
    Anki.Cozmo.Audio.GameAudioClient.PostSFXEvent(Anki.Cozmo.Audio.GameEvent.SFX.GameEnd);

    // Close minigame UI
    CloseMinigameImmediately();
    DAS.Info(this, "HandleChallengeResultViewClosed");
  }

  private void HandleQuitConfirmed() {
    RaiseMiniGameQuit();
  }

  #endregion

  // end Minigame Exit handling

  #region Difficulty Select

  private int _CurrentDifficulty;

  public int CurrentDifficulty {
    get { return _CurrentDifficulty; }
    set { 
      _CurrentDifficulty = value;
      OnDifficultySet(value);
    }
  }

  protected virtual void OnDifficultySet(int difficulty) {
  }

  #endregion

  // end Difficulty Select

  #region LightCubes

  public void StartCycleCube(int cubeID, Color[] lightColorsCounterclockwise, float cycleIntervalSeconds) {
    // Set colors
    LightCube cube = CurrentRobot.LightCubes[cubeID];
    int colorIndex = 0;
    for (int i = 0; i < cube.Lights.Length; i++) {
      colorIndex = i % lightColorsCounterclockwise.Length;
      cube.Lights[i].OnColor = lightColorsCounterclockwise[colorIndex].ToUInt();
    }

    // Set up timing data
    CycleData data = new CycleData();
    data.cubeID = cubeID;
    data.cycleIntervalSeconds = cycleIntervalSeconds;
    data.timeElaspedSeconds = 0f;
    data.colorIndex = 0;
    data.cycleColors = lightColorsCounterclockwise;
    if (_CubeCycleTimers.ContainsKey(cube.ID)) {
      _CubeCycleTimers[cube.ID] = data;
    }
    else {
      _CubeCycleTimers.Add(cube.ID, data);
    }
  }

  public void StopCycleCube(int cubeID) {
    LightCube cube = CurrentRobot.LightCubes[cubeID];
    if (_CubeCycleTimers.ContainsKey(cube.ID)) {
      _CubeCycleTimers.Remove(cube.ID);
    }
  }

  private void UpdateCubeCycleLights() {
    foreach (KeyValuePair<int,CycleData> kvp in _CubeCycleTimers) {
      kvp.Value.timeElaspedSeconds += Time.deltaTime;

      if (kvp.Value.timeElaspedSeconds > kvp.Value.cycleIntervalSeconds) {
        CycleLightsClockwise(kvp.Value);
        kvp.Value.timeElaspedSeconds %= kvp.Value.cycleIntervalSeconds;
      }
    }
  }

  private void CycleLightsClockwise(CycleData data) {
    LightCube cube = CurrentRobot.LightCubes[data.cubeID];
    data.colorIndex++;
    data.colorIndex %= data.cycleColors.Length;
    int colorIndex = 0;
    for (int i = 0; i < cube.Lights.Length; i++) {
      colorIndex = (data.colorIndex + i) % data.cycleColors.Length;
      cube.Lights[i].OnColor = data.cycleColors[colorIndex].ToUInt();
      cube.Lights[i].OnPeriodMs = LightCube.Light.FOREVER;
    }
  }

  #endregion

  // end LightCubes

  #region DAS Events

  private string GetGameUUID() {
    // TODO: Does this need to be more complicated?
    if (!_GameUUID.HasValue) {
      _GameUUID = System.Guid.NewGuid();
    }
    return _GameUUID.Value.ToString();
  }

  private string GetDasGameName() {
    return _ChallengeData.ChallengeID;
  }

  private string GetGameTimeElapsedAsStr() {
    return string.Format("{0}s", Time.time - _GameStartTime);
  }

  private string GetQuitGameState() {
    // TODO
    return null;
  }

  private void SendEventForRewards(StatContainer rewards) {
    int rewardAmount;
    foreach (var statType in StatContainer.sKeys) {
      if (rewards.TryGetValue(statType, out rewardAmount)) {
        DAS.Event(
          string.Format(DASConstants.Game.kEndPointsEarned, statType.ToString().ToLower()), 
          DASUtil.FormatStatAmount(statType, rewardAmount));
      }
    }
  }

  #endregion

  // end DAS
}
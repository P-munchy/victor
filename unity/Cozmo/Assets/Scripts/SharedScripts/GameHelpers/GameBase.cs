using Anki.Assets;
using Anki.Cozmo;
using Anki.Cozmo.ExternalInterface;
using Cozmo.Challenge;
using Cozmo.MinigameWidgets;
using Cozmo.UI;
using System.Collections;
using System.Collections.Generic;
using UnityEngine;

// Provides common interface for HubWorlds to react to games
// ending and to start/restart games. Also has interface for killing games
public abstract class GameBase : MonoBehaviour {

  private const float kWaitForPickupOrPlaceTimeout_sec = 30f;
  private const float kChallengeCompleteScoreboardDelay = 5f;
  private const float kGameIntervalCheck = 30.0f;

  private float _AutoAdvanceTimestamp = -1f;
  private float _GameIntervalLastTimestamp = -1;

  private System.Guid? _GameUUID;

  public delegate void ChallengeQuitHandler();

  public event ChallengeQuitHandler OnChallengeQuit;

  public delegate void ChallengeWinHandler();

  public event ChallengeWinHandler OnChallengeWin;

  public delegate void ChallengeLoseHandler();

  public event ChallengeWinHandler OnChallengeLose;

  public delegate void EndGameDialogHandler();

  public event EndGameDialogHandler OnShowEndGameDialog;

  public delegate void SharedMinigameViewHandler(SharedMinigameView newView);

  public event SharedMinigameViewHandler OnSharedMinigameViewInitialized;

  public IRobot CurrentRobot { get { return RobotEngineManager.Instance != null ? RobotEngineManager.Instance.CurrentRobot : null; } }

  private AlertModal _InterruptedAlertView = null;

  private SharedMinigameView _SharedMinigameViewInstance;

  public SharedMinigameView SharedMinigameView {
    get { return _SharedMinigameViewInstance; }
  }

  public Anki.AudioMetaData.GameState.Music GetDefaultMusicState() {
    return _ChallengeData.DefaultMusic;
  }

  protected Transform SharedMinigameViewInstanceParent { get { return _SharedMinigameViewInstance.transform; } }

  protected ChallengeData _ChallengeData;
  private ChallengeEndedDialog _ChallengeEndViewInstance;

  // Sets the winner index from _PlayerInfo, or one of the tie/quit enums
  protected const int ENDSTATE_NONE = -3;
  protected const int ENDSTATE_TIE = -2;
  protected const int ENDSTATE_QUIT = -1;
  protected int _EndStateIndex = ENDSTATE_NONE;

  protected bool _ShowScoreboardOnComplete = true;
  protected bool _ShowEndWinnerSlide = false;

  private List<DifficultySelectOptionData> _DifficultyOptions;

  public List<DifficultySelectOptionData> DifficultyOptions {
    get {
      return _DifficultyOptions;
    }
  }

  public string ChallengeID {
    get {
      return _ChallengeData.ChallengeID;
    }
  }

  protected StateMachine _StateMachine = new StateMachine();

  private float _GameStartTime;

  [HideInInspector]
  public List<int> CubeIdsForGame;

  private Dictionary<int, BlinkData> _BlinkCubeTimers;

  // List of all players
  protected List<PlayerInfo> _PlayerInfo = new List<PlayerInfo>();

  public virtual PlayerInfo AddPlayer(PlayerType playerType, string playerName) {
    PlayerInfo info = new PlayerInfo(playerType, playerName);
    _PlayerInfo.Add(info);
    return info;
  }
  // Called after all players have been added, in the event special logic
  // needs to happen.
  public virtual void InitializeAllPlayers() {
  }

  public virtual void AddDefaultPlayers() {
    AddPlayer(PlayerType.Cozmo, Localization.Get(LocalizationKeys.kNameCozmo));
    AddPlayer(PlayerType.Human, DataPersistence.DataPersistenceManager.Instance.Data.DefaultProfile.ProfileName);
  }
  public virtual void ClearPlayers() {
    _PlayerInfo.Clear();
  }

  public virtual List<PlayerInfo> GetPlayersByType(PlayerType playerType) {
    return _PlayerInfo.FindAll(x => x.playerType == playerType);
  }
  public virtual PlayerInfo GetFirstPlayerByType(PlayerType playerType) {
    return _PlayerInfo.Find(x => x.playerType == playerType);
  }
  public virtual PlayerInfo GetPlayerByIndex(int index) {
    return index < _PlayerInfo.Count && index >= 0 ? _PlayerInfo[index] : null;
  }
  public virtual int GetPlayerCount() {
    return _PlayerInfo.Count;
  }
  public virtual List<PlayerInfo> GetPlayers() {
    return _PlayerInfo;
  }
  protected virtual string GetLoadingTextKey() {
    return LocalizationKeys.kMinigameLabelCozmoPrep;
  }

  private class BlinkData {
    public int cubeID;
    public float timeElaspedSeconds;
    public float blinkDuration;
    public Color[] originalColor;
  }

  public bool Paused {
    get {
      return _StateMachine.IsPaused;
    }
  }

  private bool _ResultsViewReached = false;

  #region Initialization

  // called when the game starts to disable reactionary behaviors, then again when the game exs to re-enable them
  protected virtual void InitializeReactionaryBehaviorsForGameStart() {
    RobotEngineManager.Instance.CurrentRobot.DisableReactionsWithLock(ReactionaryBehaviorEnableGroups.kMinigameId, ReactionaryBehaviorEnableGroups.kDefaultMinigameTriggers);
  }

  private void ResetReactionaryBehaviorsForGameEnd() {
    if (RobotEngineManager.Instance.CurrentRobot != null) {
      RobotEngineManager.Instance.CurrentRobot.RemoveDisableReactionsLock(ReactionaryBehaviorEnableGroups.kMinigameId);
    }
  }

  public void InitializeChallenge(ChallengeData challengeData) {
    if (challengeData.IsActivity) {
      Cozmo.Needs.NeedsStateManager.Instance.SetFullPause(true);
    }
    Cozmo.PauseManager.Instance.AllowFreeplayOnResume = false;

    _GameStartTime = Time.time;
    _GameIntervalLastTimestamp = -1;
    _StateMachine.SetGameRef(this);

    _ChallengeData = challengeData;
    _DifficultyOptions = _ChallengeData.DifficultyOptions;
    _ResultsViewReached = false;
    if (CurrentRobot != null) {

      // this is done to prevent entering a game while robot is trying to finish get out seqeuences as part
      // of RobotResetState.
      CurrentRobot.CancelAllCallbacks();
      CurrentRobot.CancelAction(RobotActionType.UNKNOWN);

      // Selection Wait Chooser, cube lights, turns off game requests
      HubWorldBase.Instance.StopFreeplay();


      if (CurrentRobot.Status(RobotStatusFlag.IS_CARRYING_BLOCK)) {
        CurrentRobot.PlaceObjectOnGroundHere((success) => {
          PlayGetInAnimation();
        });
      }
      else {
        PlayGetInAnimation();
      }

    }

    Anki.Cozmo.Audio.GameAudioClient.SetMusicState(GetDefaultMusicState());

    _BlinkCubeTimers = new Dictionary<int, BlinkData>();

    SkillSystem.Instance.StartGame(_ChallengeData);
    // Clear Pending Rewards and Unlocks so ChallengeEndedDialog only displays things earned during this game
    RewardedActionManager.Instance.SendPendingRewardsToInventory();
    AddDefaultPlayers();
  }

  public void PopulateTitleWidget() {
    ChallengeTitleWidget titleWidget = _SharedMinigameViewInstance.TitleWidget;
    titleWidget.Text = Localization.Get(_ChallengeData.ChallengeTitleLocKey);
    titleWidget.SubtitleText = null;
  }

  private void PlayGetInAnimation() {
    CurrentRobot.SendAnimationTrigger(_ChallengeData.GetInAnimTrigger.Value, callback: (success) => {
      InitializeChallengeDone();
    });
  }

  private void InitializeChallengeDone() {
    InitializeReactionaryBehaviorsForGameStart();

    RegisterRobotReactionaryBehaviorEvents();

    LoadMinigameUIAssetBundle();
    DAS.Event("game.launch", GetDasGameName());
  }

  private void LoadMinigameUIAssetBundle() {
    string minigameAssetBundleName = AssetBundleNames.minigame_ui_prefabs.ToString();
    AssetBundleManager.Instance.LoadAssetBundleAsync(
      minigameAssetBundleName, (bool success) => {
        if (success) {
          LoadSharedMinigameView(minigameAssetBundleName);
        }
        else {
          DAS.Error("GameBase.LoadMinigameUIAssetBundle", "Failed to load asset bundle " + minigameAssetBundleName);
        }
      });
  }

  private void LoadSharedMinigameView(string minigameAssetBundleName) {
    MinigameUIPrefabHolder.LoadSharedMinigameViewPrefab(minigameAssetBundleName, (GameObject viewPrefab) => {
      if (viewPrefab != null) {
        SharedMinigameView prefabScript = viewPrefab.GetComponent<SharedMinigameView>();
        UIManager.OpenView(prefabScript, HandleSharedMinigameViewCreated);
      }
      else {
        DAS.Error("GameBase.LoadSharedMinigameView", "Failed to load shared minigame view");
      }
    });
  }

  private void HandleSharedMinigameViewCreated(BaseView newSharedMinigameView) {
    _SharedMinigameViewInstance = (SharedMinigameView)newSharedMinigameView;
    _SharedMinigameViewInstance.Initialize(_ChallengeData);
    InitializeMinigameView(_SharedMinigameViewInstance, _ChallengeData);

    if (OnSharedMinigameViewInitialized != null) {
      OnSharedMinigameViewInitialized(_SharedMinigameViewInstance);
    }

    bool videoPlayedAlready = DataPersistence.DataPersistenceManager.Instance.Data.DefaultProfile.GameInstructionalVideoPlayed.ContainsKey(_ChallengeData.ChallengeID);
    bool noInstructionVideo = string.IsNullOrEmpty(_ChallengeData.InstructionVideoPath);

    if (videoPlayedAlready || noInstructionVideo) {
      FinishedInstructionalVideo();
    }
    else {
      _SharedMinigameViewInstance.PlayVideo(_ChallengeData.InstructionVideoPath, FinishedInstructionalVideo);
    }
  }

  private void FinishedInstructionalVideo() {
    bool videoPlayedAlready = DataPersistence.DataPersistenceManager.Instance.Data.DefaultProfile.GameInstructionalVideoPlayed.ContainsKey(_ChallengeData.ChallengeID);
    if (!videoPlayedAlready) {
      DataPersistence.DataPersistenceManager.Instance.Data.DefaultProfile.GameInstructionalVideoPlayed.Add(_ChallengeData.ChallengeID, true);
      DataPersistence.DataPersistenceManager.Instance.Save();
    }
    PrepRobotForGame();
  }

  private void InitializeMinigameView(SharedMinigameView newView, ChallengeData data) {
    // For all challenges, set the title text and add a quit button by default
    PopulateTitleWidget();
    newView.ShowQuitButton();

    if (data.IsMinigame) {
      newView.InitializeColor(UIColorPalette.GameBackgroundColor);
    }
    else {
      newView.InitializeColor(UIColorPalette.ActivityBackgroundColor);
    }

    newView.ShowWideSlideWithText(GetLoadingTextKey(), null);

    newView.ShowShelf();
    newView.ShowSpinnerWidget();
    newView.QuitChallengeConfirmed += HandleQuitConfirmed;
    ContextManager.Instance.OnAppHoldStart += HandleAppHoldStart;
    ContextManager.Instance.OnAppHoldEnd += HandleAppHoldEnd;
  }

  private void PrepRobotForGame() {
    IRobot currentRobot = RobotEngineManager.Instance.CurrentRobot;
    if (currentRobot == null) {
      return;
    }
    if (currentRobot.Status(RobotStatusFlag.IS_PICKING_OR_PLACING)) {
      StartCoroutine(WaitForPickingUpOrPlacingFinish(currentRobot, Time.time));
    }
    else {
      CheckForCarryingBlock(currentRobot);
    }

    CurrentRobot.OnLightCubeRemoved += HandleLightCubeRemoved;
  }

  private IEnumerator WaitForPickingUpOrPlacingFinish(IRobot robot, float startTimestamp) {
    while (robot.Status(RobotStatusFlag.IS_PICKING_OR_PLACING)
           && (Time.time - startTimestamp) < kWaitForPickupOrPlaceTimeout_sec) {
      yield return new WaitForEndOfFrame();
    }

    CheckForCarryingBlock(robot);
  }

  private void CheckForCarryingBlock(IRobot robot) {
    if (robot.Status(RobotStatusFlag.IS_CARRYING_BLOCK)) {
      robot.PlaceObjectOnGroundHere(FinishPlaceObjectOnGround);
    }
    else {
      robot.TurnTowardsLastFacePose(Mathf.PI, callback: FinishTurnToFace);
    }
  }

  private void FinishPlaceObjectOnGround(bool success) {
    RobotEngineManager.Instance.CurrentRobot.TurnTowardsLastFacePose(Mathf.PI, callback: FinishTurnToFace);
  }

  private void FinishTurnToFace(bool success) {
    _SharedMinigameViewInstance.ShowMiddleBackground();
    _SharedMinigameViewInstance.HideSpinnerWidget();

    DAS.SetGlobal(DASConstants.Game.kGlobal, GetGameUUID());
    DAS.Event(DASConstants.Game.kStart, GetGameUUID());
    DAS.Event(DASConstants.Game.kType, GetDasGameName());
    DasTracker.Instance.TrackGameStarted();

    InitializeGame(_ChallengeData.ChallengeConfig);
    if (!string.IsNullOrEmpty(_ChallengeData.InstructionVideoPath)) {
      _SharedMinigameViewInstance.ShowInstructionsVideoButton();
    }

    SetupViewAfterCozmoReady(_SharedMinigameViewInstance, _ChallengeData);

  }

  /// <summary>
  /// Called after Cozmo is "Ready". At this point the SharedMinigameView has already been instantiated.
  /// Use this method to initialize the state machine.
  /// </summary>
  /// <param name="challengeConfigData">Minigame config data.</param>
  protected abstract void InitializeGame(ChallengeConfigBase challengeConfigData);

  protected virtual void SetupViewAfterCozmoReady(SharedMinigameView newView, ChallengeData data) {
  }

  #endregion

  // end Initialization

  #region Update

  protected virtual void Update() {
    UpdateBlinkLights();
    UpdateStateMachine();
    AutoAdvanceCheck();
    TimedIntervalCheck();

    for (int i = 0; i < _PlayerInfo.Count; ++i) {
      _PlayerInfo[i].UpdateGoal();
    }
  }

  private void TimedIntervalCheck() {
    if (_GameIntervalLastTimestamp < 0.0f) {
      _GameIntervalLastTimestamp = Time.time;
    }
    if (Time.time - _GameIntervalLastTimestamp > kGameIntervalCheck) {
      _GameIntervalLastTimestamp = Time.time;
      GameEventManager.Instance.FireGameEvent(GameEventWrapperFactory.Create(GameEvent.OnChallengeInterval, GetGameTimeElapsedInSeconds()));
    }
  }

  private void UpdateStateMachine() {
    _StateMachine.UpdateStateMachine();
  }

  // Use these if we need special cube behavior on app hold or whatever for specific challenges
  protected virtual void HandleAppHoldStart() {
    DAS.Info("GameBase.HandleAppHoldStart", "StartAppHold");

  }

  protected virtual void HandleAppHoldEnd() {
    DAS.Info("GameBase.HandleAppHoldEnd", "EndAppHold");

  }

  public void PauseStateMachine(State.PauseReason reason, ReactionTrigger reactionaryBehavior) {
    _StateMachine.Pause(reason, reactionaryBehavior);
  }

  public void ResumeStateMachine(State.PauseReason reason, ReactionTrigger reactionaryBehavior) {
    _StateMachine.Resume(reason, reactionaryBehavior);
  }

  #endregion

  // end Update

  #region Scoring

  public int HumanScore {
    get {
      PlayerInfo player = GetFirstPlayerByType(PlayerType.Human);
      if (player == null) {
        return 0;
      }
      return player.playerScoreRound;
    }
  }
  public int CozmoScore {
    get {
      PlayerInfo player = GetFirstPlayerByType(PlayerType.Cozmo);
      if (player == null) {
        return 0;
      }
      return player.playerScoreRound;
    }
  }

  public int HumanScoreTotal {
    get {
      PlayerInfo player = GetFirstPlayerByType(PlayerType.Human);
      if (player == null) {
        return 0;
      }
      return player.playerScoreTotal;
    }
  }
  public int CozmoScoreTotal {
    get {
      PlayerInfo player = GetFirstPlayerByType(PlayerType.Cozmo);
      if (player == null) {
        return 0;
      }
      return player.playerScoreTotal;
    }
  }

  // Points needed to win Round
  [HideInInspector]
  public int MaxScorePerRound;

  public void ResetScore() {
    for (int i = 0; i < _PlayerInfo.Count; ++i) {
      _PlayerInfo[i].playerScoreRound = 0;
    }
    UpdateUI();
  }

  public virtual void AddPoint(bool humanScored) {
    if (humanScored) {
      AddPoint(GetFirstPlayerByType(PlayerType.Human));
    }
    else {
      AddPoint(GetFirstPlayerByType(PlayerType.Cozmo));
    }
  }
  public virtual void AddPoint(PlayerInfo player, int points = 1, bool fireEvent = true) {
    if (player == null) {
      return;
    }
    player.playerScoreRound += points;
    player.playerScoreTotal += points;

    if (fireEvent) {
      GameEventManager.Instance.FireGameEvent(GameEventWrapperFactory.Create(GameEvent.OnChallengePointScored, _ChallengeData.ChallengeID,
                                              _CurrentDifficulty, player.playerType != PlayerType.Cozmo,
                                              HumanScore, CozmoScore, IsHighIntensityRound()));
    }
  }

  public virtual void AddPoint(List<PlayerInfo> playersScored, int points = 1) {
    // Only care about events in a two player game currently
    for (int i = 0; i < playersScored.Count; ++i) {
      AddPoint(playersScored[i], points, _PlayerInfo.Count < 3);
    }
  }

  public int HumanRoundsWon {
    get {
      PlayerInfo player = GetFirstPlayerByType(PlayerType.Human);
      if (player == null) {
        return 0;
      }
      return player.playerRoundsWon;
    }
  }
  public int CozmoRoundsWon {
    get {
      PlayerInfo player = GetFirstPlayerByType(PlayerType.Cozmo);
      if (player == null) {
        return 0;
      }
      return player.playerRoundsWon;
    }
  }
  // Total number of Rounds in this Game
  [HideInInspector]
  public int TotalRounds;


  // Number of Rounds Played this Game
  public int RoundsPlayed {
    get {
      int currPlayed = 0;
      for (int i = 0; i < _PlayerInfo.Count; ++i) {
        currPlayed += _PlayerInfo[i].playerRoundsWon;
      }
      return currPlayed;
    }
  }

  public int RoundsNeededToWin {
    get {
      return (TotalRounds / 2) + 1;
    }
  }

  public int CurrentRound {
    get {
      return RoundsPlayed + 1;
    }
  }

  public virtual void UpdateUI() {
    // TODO: Put any base logic for Updating UI Here.
  }

  public virtual void UpdateUIForGameEnd() {
    // TODO: Put any base logic for Updating UI for game end here.
    SharedMinigameView.HideQuitButton();
  }

  public virtual bool IsRoundComplete() {
    for (int i = 0; i < _PlayerInfo.Count; ++i) {
      if (_PlayerInfo[i].playerScoreRound >= MaxScorePerRound) {
        return true;
      }
    }
    return false;
  }

  // True if the Round is Close in terms of Points.
  // can be overriden if you have special challenge specific logic for determining this
  public virtual bool IsHighIntensityRound() {
    int oneThirdRoundsTotal = TotalRounds / 3;
    return RoundsPlayed > oneThirdRoundsTotal;
  }

  /// <summary>
  /// Ends the current round, whoever has the higher current score wins.
  /// </summary>
  public virtual void EndCurrentRound() {
    bool playerWon = false;
    PlayerInfo winningPlayer = null;
    int winningScore = -1;
    for (int i = 0; i < _PlayerInfo.Count; ++i) {
      if (_PlayerInfo[i].playerScoreRound > winningScore) {
        winningScore = _PlayerInfo[i].playerScoreRound;
        winningPlayer = _PlayerInfo[i];
        playerWon = winningPlayer.playerType != PlayerType.Cozmo;
      }
    }
    if (winningPlayer != null) {
      winningPlayer.playerRoundsWon++;
    }
    GameEventManager.Instance.FireGameEvent(GameEventWrapperFactory.Create(GameEvent.OnChallengeRoundEnd, _ChallengeData.ChallengeID,
                                    CurrentDifficulty, playerWon, HumanScore, CozmoScore, IsHighIntensityRound()));

    UpdateUI();
  }

  // True if someone has enough rounds won to complete the Game.
  public virtual bool IsGameComplete() {
    int losingScore = int.MaxValue;
    int winningScore = 0;
    for (int i = 0; i < _PlayerInfo.Count; ++i) {
      if (_PlayerInfo[i].playerRoundsWon >= winningScore) {
        winningScore = _PlayerInfo[i].playerRoundsWon;
      }
      if (_PlayerInfo[i].playerRoundsWon <= losingScore) {
        losingScore = _PlayerInfo[i].playerRoundsWon;
      }
    }
    int roundsLeft = TotalRounds - losingScore - winningScore;
    return (winningScore > losingScore + roundsLeft);
  }

  // True if the Game is Close in terms of Rounds.
  public bool IsHighIntensityGame() {
    int twoThirdsRoundsTotal = TotalRounds / 3 * 2;
    return RoundsPlayed > twoThirdsRoundsTotal;
  }

  protected virtual Dictionary<string, float> GetGameSpecificEventValues() {
    return null;
  }

  public PlayerInfo GetPlayerMostPointsWon() {
    PlayerInfo ret = null;
    int mostPoints = 0;
    for (int i = 0; i < _PlayerInfo.Count; ++i) {
      if (_PlayerInfo[i].playerScoreRound > mostPoints) {
        ret = _PlayerInfo[i];
        mostPoints = _PlayerInfo[i].playerScoreRound;
      }
    }
    return ret;
  }

  public PlayerInfo GetPlayerMostRoundsWon() {
    PlayerInfo ret = null;
    int mostRoundsWon = 0;
    for (int i = 0; i < _PlayerInfo.Count; ++i) {
      if (_PlayerInfo[i].playerRoundsWon > mostRoundsWon) {
        ret = _PlayerInfo[i];
        mostRoundsWon = _PlayerInfo[i].playerRoundsWon;
      }
    }
    return ret;
  }
  // Handles the end of the game based on Rounds won, will attempt to progress difficulty as well
  public virtual void StartRoundBasedGameEnd() {
    // Fire OnGameComplete, passing in ChallengeID, CurrentDifficulty, and if Playerwon
    int endStateIndex = ENDSTATE_NONE;
    int mostRoundsWon = 0;
    for (int i = 0; i < _PlayerInfo.Count; ++i) {
      if (_PlayerInfo[i].playerRoundsWon > mostRoundsWon) {
        endStateIndex = i;
        mostRoundsWon = _PlayerInfo[i].playerRoundsWon;
      }
    }
    StartBaseGameEnd(endStateIndex);
  }

  protected bool DidHumanWin() {
    if (0 <= _EndStateIndex && _EndStateIndex < _PlayerInfo.Count) {
      return _PlayerInfo[_EndStateIndex].playerType != PlayerType.Cozmo;
    }
    return false;
  }

  public virtual void StartBaseGameEnd(int endStateIndex) {
    _EndStateIndex = endStateIndex;
    GameEventManager.Instance.FireGameEvent(GameEventWrapperFactory.Create(GameEvent.OnChallengeComplete,
                                                    _ChallengeData.ChallengeID, _CurrentDifficulty, DidHumanWin(),
                                                    HumanScore, CozmoScore, IsHighIntensityRound(), GetGameSpecificEventValues()));

    if (endStateIndex == ENDSTATE_TIE) {
      RaiseChallengeTie();
    }
    // either our quit or none error state
    else if (endStateIndex < 0 || endStateIndex >= _PlayerInfo.Count) {
      RaiseChallengeQuit();
    }
    // A valid player index
    else {
      PlayerInfo winningPlayer = _PlayerInfo[endStateIndex];
      // even in MP mode we show the same screens.
      // the rewards themselves fitler by num_players and rewards screen will pull winner.
      if (winningPlayer.playerType == PlayerType.Cozmo) {
        RaiseChallengeLose();
      }
      else {
        HandleUnlockRewards();
        RaiseChallengeWin();
      }
    }
  }

  public virtual void StartBaseGameEnd(bool playerWon) {
    int endStateIndex = ENDSTATE_NONE;
    for (int i = 0; i < _PlayerInfo.Count; ++i) {
      if (playerWon && _PlayerInfo[i].playerType != PlayerType.Cozmo) {
        endStateIndex = i;
      }
      else if (!playerWon && _PlayerInfo[i].playerType == PlayerType.Cozmo) {
        endStateIndex = i;
      }
    }
    StartBaseGameEnd(endStateIndex);
  }

  private void HandleUnlockRewards() {
    DataPersistence.PlayerProfile playerProfile = DataPersistence.DataPersistenceManager.Instance.Data.DefaultProfile;
    int currentDifficultyUnlocked = 0;
    if (playerProfile.GameDifficulty.ContainsKey(_ChallengeData.ChallengeID)) {
      currentDifficultyUnlocked = playerProfile.GameDifficulty[_ChallengeData.ChallengeID];
    }
    // Set NewDifficultyUnlock to -1 so it will be ignored by ChallengeEndedDialog
    RewardedActionManager.Instance.NewDifficultyUnlock = -1;
    int newDifficultyUnlocked = CurrentDifficulty + 1;
    bool higherLevelUnlocked = currentDifficultyUnlocked < newDifficultyUnlocked;
    bool highestOptionLevel = !(_ChallengeData.DifficultyOptions != null && newDifficultyUnlocked < _ChallengeData.DifficultyOptions.Count);

    if (higherLevelUnlocked && !highestOptionLevel) {
      playerProfile.GameDifficulty[_ChallengeData.ChallengeID] = newDifficultyUnlocked;
      DataPersistence.DataPersistenceManager.Instance.Save();
      GameEventManager.Instance.FireGameEvent(GameEventWrapperFactory.Create(GameEvent.OnChallengeDifficultyUnlock, _ChallengeData.ChallengeID, newDifficultyUnlocked));
      RewardedActionManager.Instance.NewDifficultyUnlock = newDifficultyUnlocked;
      DasTracker.Instance.TrackDifficultyUnlocked(_ChallengeData.ChallengeID, newDifficultyUnlocked);
    }
  }

  /// <summary>
  /// Returns Highest Unlocked Difficulty.
  /// </summary>
  /// <returns>Int representing the highest difficulty unlocked for this game's ChallengeID.</returns>
  public int HighestLevelCompleted() {
    int difficulty = 0;
    if (DataPersistence.DataPersistenceManager.Instance.Data.DefaultProfile.GameDifficulty.TryGetValue(_ChallengeData.ChallengeID, out difficulty)) {
      // someone has reduced the number of difficulties saved, should only happen in development
      if (difficulty >= _ChallengeData.DifficultyOptions.Count) {
        return _ChallengeData.DifficultyOptions.Count - 1;
      }
      return difficulty;
    }
    return 0;
  }

  #endregion

  // End Scoring

  #region Clean Up

  /// <summary>
  /// Clean up listeners and extra game objects. Called before the game is 
  /// destroyed when the player quits or the robot loses connection.
  /// </summary>
  protected abstract void CleanUpOnDestroy();

  public void OnDestroy() {
    ContextManager.Instance.AppHoldEnd();
    DAS.Event(DASConstants.Game.kEnd, GetGameTimeElapsedAsStr());
    DAS.SetGlobal(DASConstants.Game.kGlobal, null);
    DasTracker.Instance.TrackGameEnded();
    if (_SharedMinigameViewInstance != null) {
      _SharedMinigameViewInstance.CloseDialogImmediately();
      _SharedMinigameViewInstance = null;
    }
    DAS.Info(this, "Finished GameBase On Destroy");
    SkillSystem.Instance.EndGame();

    if (CurrentRobot != null) {
      CurrentRobot.SetEnableFreeplayLightStates(true);
    }
    ResetReactionaryBehaviorsForGameEnd();

    // Destroy any active player goals
    for (int i = 0; i < _PlayerInfo.Count; ++i) {
      _PlayerInfo[i].SetGoal(null);
    }

    AssetBundleManager.Instance.UnloadAssetBundle(AssetBundleNames.minigame_ui_prefabs.ToString());
  }

  private void QuitChallenge() {
    try {
      _SharedMinigameViewInstance.DialogCloseAnimationFinished += QuitChallengeAnimationFinished;
      _SharedMinigameViewInstance.CloseDialog();

      Anki.Cozmo.Audio.GameAudioClient.SetMusicState(Anki.AudioMetaData.GameState.Music.Freeplay);

      // cancels any queued up actions or co-routines
      if (CurrentRobot != null) {
        CurrentRobot.CancelAction(RobotActionType.UNKNOWN);
      }
      StopAllCoroutines();
    }
    catch (System.Exception e) {
      // This is happening sometimes when disconnecting from robot when a dialog is up.
      DAS.Info("GameBase.QuitChallenge", "StopAllCoroutines null ref internally sometimes: " + e.Message);
      DAS.Debug("GameBase.QuitChallenge.StackTrace", DASUtil.FormatStackTrace(e.StackTrace));
      HockeyApp.ReportStackTrace("GameBase.QuitChallenge", e.StackTrace);
    }
  }

  private void QuitChallengeAnimationFinished() {
    _SharedMinigameViewInstance.DialogCloseAnimationFinished -= QuitChallengeAnimationFinished;

    if (OnChallengeQuit != null) {
      OnChallengeQuit();
    }

    CleanUp();
  }

  private void CloseChallenge() {
    _SharedMinigameViewInstance.DialogCloseAnimationFinished += CleanUp;
    _SharedMinigameViewInstance.CloseDialog();
  }

  public void CloseChallengeImmediately() {
    DAS.Info(this, "Close Challenge Immediately");
    // If quitting early from a minigame, clear the pending action rewards, they should not be rewarded
    if (!_ResultsViewReached && _ChallengeData.IsMinigame) {
      RewardedActionManager.Instance.ResetPendingRewards();
    }
    _StateMachine.Stop();
    if (_SharedMinigameViewInstance != null) {
      _SharedMinigameViewInstance.CloseDialogImmediately();
      _SharedMinigameViewInstance = null;
    }
    CleanUp();
  }

  public void CleanUp() {

    DAS.Debug("GameBase.CleanUp", "cleanup called");

    if (_SharedMinigameViewInstance != null) {
      _SharedMinigameViewInstance.DialogCloseAnimationFinished -= CleanUp;
    }

    Cozmo.PauseManager.Instance.AllowFreeplayOnResume = true;

    ContextManager.Instance.OnAppHoldStart -= HandleAppHoldStart;
    ContextManager.Instance.OnAppHoldEnd -= HandleAppHoldEnd;

    if (ContextManager.Instance.ManagerBusy) {
      ContextManager.Instance.OnAppHoldEnd();
    }

    if (CurrentRobot != null) {
      if (_StateMachine.GetReactionThatPausedGame() == Anki.Cozmo.ReactionTrigger.NoneTrigger) {
        // clears the action queue before quitting the game.
        CurrentRobot.CancelAction(RobotActionType.UNKNOWN);
      }

      CurrentRobot.OnLightCubeRemoved -= HandleLightCubeRemoved;
    }

    if (_InterruptedAlertView != null) {
      _InterruptedAlertView.CloseDialogImmediately();
    }

    // Some CleanUpOnDestroy overrides send a robot animation as well
    CleanUpOnDestroy();
    Destroy(gameObject);
  }

  public void SoftEndGameRobotReset() {
    DeregisterRobotReactionaryBehaviorEvents();
    ResetReactionaryBehaviorsForGameEnd();

    AddToTotalGamesPlayed();

    DAS.Debug("GameBase.SoftEndGameRobotReset", "soft end game reset called");

    Cozmo.RequestGame.RequestGameManager.Instance.DisableRequestGameBehaviorGroups();

    if (OnShowEndGameDialog != null) {
      OnShowEndGameDialog();
    }
  }

  private void AddToTotalGamesPlayed() {
    // Increment the amount that this challenge has been played by 1, only count fully completed playthroughs of challenges
    if (DataPersistence.DataPersistenceManager.Instance.Data.DefaultProfile.TotalGamesPlayed.ContainsKey(_ChallengeData.ChallengeID)) {
      DataPersistence.DataPersistenceManager.Instance.Data.DefaultProfile.TotalGamesPlayed[_ChallengeData.ChallengeID]++;
    }
    else {
      DataPersistence.DataPersistenceManager.Instance.Data.DefaultProfile.TotalGamesPlayed.Add(_ChallengeData.ChallengeID, 1);
    }
    DataPersistence.DataPersistenceManager.Instance.Save();
  }

  #endregion

  // end Clean Up


  #region Challenge Exit

  public virtual void RaiseChallengeQuit() {
    _EndStateIndex = ENDSTATE_QUIT;
    _StateMachine.Stop();

    DeregisterRobotReactionaryBehaviorEvents();
    ResetReactionaryBehaviorsForGameEnd();

    DAS.Event(DASConstants.Game.kQuit, null);
    SendCustomEndGameDasEvents();

    // Track # times played for activities that have no win or lose state
    // Also fire OnChallengeComplete here for those
    if (!_ChallengeData.IsMinigame) {
      AddToTotalGamesPlayed();
      GameEventManager.Instance.FireGameEvent(GameEventWrapperFactory.Create(GameEvent.OnChallengeComplete,
                                                                             _ChallengeData.ChallengeID,
                                                                             _CurrentDifficulty,
                                                                             true,
                                                                             HumanScore,
                                                                             CozmoScore,
                                                                             IsHighIntensityRound()));
    }
    // If quitting early from a minigame, clear the pending action rewards, they should not be rewarded
    if (!_ResultsViewReached && _ChallengeData.IsMinigame) {
      RewardedActionManager.Instance.ResetPendingRewards();
    }

    QuitChallenge();

    if (_ChallengeData.IsActivity) {
      Cozmo.Needs.NeedsStateManager.Instance.SetFullPause(false);
    }
  }

  protected virtual void SendCustomEndGameDasEvents() {
    // Implement in subclasses if desired
  }

  private void RaiseChallengeWin() {
    _StateMachine.Stop();
    if (DataPersistence.DataPersistenceManager.Instance.CurrentSession.TotalWins.ContainsKey(_ChallengeData.ChallengeID)) {
      DataPersistence.DataPersistenceManager.Instance.CurrentSession.TotalWins[_ChallengeData.ChallengeID]++;
    }
    else {
      DataPersistence.DataPersistenceManager.Instance.CurrentSession.TotalWins.Add(_ChallengeData.ChallengeID, 1);
    }

    UpdatePlayNeed();

    if (_ShowScoreboardOnComplete) {
      UpdateScoreboard(didPlayerWin: DidHumanWin());
    }
    ShowWinnerState(_EndStateIndex);
  }

  private void RaiseChallengeLose() {
    _StateMachine.Stop();

    UpdatePlayNeed();

    if (_ShowScoreboardOnComplete) {
      UpdateScoreboard(didPlayerWin: DidHumanWin());
    }

    ShowWinnerState(_EndStateIndex);
  }

  private void RaiseChallengeTie() {
    _StateMachine.Stop();

    UpdatePlayNeed();

    ShowWinnerState(_EndStateIndex);
  }

  private void UpdatePlayNeed() {
    if (_ChallengeData.IsActivity) {
      Cozmo.Needs.NeedsStateManager.Instance.SetFullPause(false);
    }

    if (Cozmo.Needs.NeedsStateManager.Instance != null) {
      if (_ChallengeData.IsMinigame) {
        if (DidHumanWin()) {
          Cozmo.Needs.NeedsStateManager.Instance.RegisterNeedActionCompleted(_ChallengeData.NeedsActionIdWin.Value);
        }
        else {
          Cozmo.Needs.NeedsStateManager.Instance.RegisterNeedActionCompleted(_ChallengeData.NeedsActionIdLose.Value);
        }
      }
    }
  }

  protected virtual void UpdateScoreboard(bool didPlayerWin) {
    ScoreWidget cozmoScoreboard = _SharedMinigameViewInstance.CozmoScoreboard;
    cozmoScoreboard.Dim = false;
    cozmoScoreboard.IsWinner = !didPlayerWin;
    ScoreWidget playerScoreboard = _SharedMinigameViewInstance.PlayerScoreboard;
    playerScoreboard.Dim = false;
    playerScoreboard.IsWinner = didPlayerWin;
  }

  protected virtual void ShowWinnerState(int currentEndIndex, string overrideWinnerText = null, string footerText = "", bool showWinnerTextInShelf = false) {
    SoftEndGameRobotReset();
    _ResultsViewReached = true;
    ContextManager.Instance.AppFlash(playChime: true);
    SaveHighScore();

    string winnerText = "";
    if (overrideWinnerText != null) {
      winnerText = overrideWinnerText;
    }
    else if (currentEndIndex == ENDSTATE_TIE) {
      winnerText = Localization.Get(LocalizationKeys.kMinigameTextTie);
    }
    else if (currentEndIndex >= 0) {

      bool useFlatText = _ShowEndWinnerSlide || showWinnerTextInShelf;

      PlayerInfo player = _PlayerInfo[currentEndIndex];
      if (player.playerType == PlayerType.Cozmo) {
        if (useFlatText) {
          winnerText = Localization.Get(LocalizationKeys.kMinigameTextCozmoWinsFlat);
        }
        else {
          winnerText = Localization.Get(LocalizationKeys.kMinigameTextCozmoWins);
        }
      }
      else {
        //If the player doesn't have a name, instead of "" WINS!, change the message to YOU WIN!
        if (!string.IsNullOrEmpty(player.name)) {
          if (useFlatText) {
            winnerText = Localization.GetWithArgs(LocalizationKeys.kMinigameTextPlayerWinsFlat, new object[] { player.name });
          }
          else {
            winnerText = Localization.GetWithArgs(LocalizationKeys.kMinigameTextPlayerWins, new object[] { player.name });
          }
        }
        else {
          if (useFlatText) {
            winnerText = Localization.GetWithArgs(LocalizationKeys.kMinigameTextYouWinFlat);
          }
          else {
            winnerText = Localization.GetWithArgs(LocalizationKeys.kMinigameTextYouWin);
          }
        }
      }
    }

    if (_ShowEndWinnerSlide) {
      SharedMinigameView.ShowWinnerStateSlide(DidHumanWin(), winnerText, footerText);
      SharedMinigameView.HideTitleWidget();
    }
    else {
      if (showWinnerTextInShelf) {
        SharedMinigameView.ShelfWidget.SetMinigameText(winnerText);
      }
      else {
        SharedMinigameView.InfoTitleText = winnerText;
      }
    }
    _AutoAdvanceTimestamp = Time.time;
  }

  protected virtual bool SaveHighScore() {
    DataPersistence.PlayerProfile playerProfile = DataPersistence.DataPersistenceManager.Instance.Data.DefaultProfile;
    // Scores are per mode
    string key = _ChallengeData.ChallengeID + CurrentDifficulty;
    if (!playerProfile.HighScores.ContainsKey(key)) {
      playerProfile.HighScores[key] = 0;
    }
    if (playerProfile.HighScores[key] < HumanScore) {
      GameEventManager.Instance.FireGameEvent(GameEventWrapperFactory.Create(GameEvent.OnNewHighScore,
        _ChallengeData.ChallengeID, _CurrentDifficulty, DidHumanWin(), HumanScore, CozmoScore, IsHighIntensityRound(), playerProfile.HighScores[key]));
      playerProfile.HighScores[key] = HumanScore;
      return true;
    }
    return false;
  }

  public void AutoAdvanceCheck() {
    if (_AutoAdvanceTimestamp != -1 && (Time.time - _AutoAdvanceTimestamp > kChallengeCompleteScoreboardDelay)) {
      HandleChallengeResultAdvance();
    }
  }

  // Update the Challenge Ended Dialog to show all pending rewards and unlocks,
  // if there are none, close the view.
  private void HandleChallengeResultAdvance() {
    _AutoAdvanceTimestamp = -1;
    if (RewardedActionManager.Instance.NewDifficultyPending
        || RewardedActionManager.Instance.NewSkillChangePending) {
      SharedMinigameView.HidePlayerScoreboard();
      SharedMinigameView.HidePlayer2Scoreboard();
      SharedMinigameView.HideCozmoScoreboard();
      SharedMinigameView.HideShelf();
      DASReportPendingActionRewards();
      SharedMinigameView.ShowContinueButtonOffset(HandleChallengeResultViewClosed,
                                                  Localization.Get(LocalizationKeys.kButtonContinue),
                                                  string.Empty,
                                                  Color.clear,
                                                  "game_results_continue_button");
      _ChallengeEndViewInstance = _SharedMinigameViewInstance.ShowChallengeEndedSlide(_ChallengeData);
      _ChallengeEndViewInstance.DisplayRewards();
    }
    else {
      HandleChallengeResultViewClosed();
    }
  }

  /// <summary>
  /// Fires a DAS event for each pending action reward.
  /// </summary>
  private void DASReportPendingActionRewards() {
    foreach (KeyValuePair<RewardedActionData, int> reward in RewardedActionManager.Instance.PendingActionRewards) {
      DAS.Event("game.end.energy_reward", reward.Key.Reward.ItemID, DASUtil.FormatExtraData(reward.Key.Reward.Amount.ToString()));
    }
  }

  private void HandleChallengeResultViewClosed() {
    // Turn off lights on robot
    if (CurrentRobot != null) {
      CurrentRobot.TurnOffAllLights();
    }

    // Close challenge UI
    CloseChallenge();

    if (DidHumanWin()) {
      DAS.Event(DASConstants.Game.kEndWithRank, DASConstants.Game.kRankPlayerWon);
      if (OnChallengeWin != null) {
        OnChallengeWin();
      }
    }
    else {
      DAS.Event(DASConstants.Game.kEndWithRank, DASConstants.Game.kRankPlayerLose);
      if (OnChallengeLose != null) {
        OnChallengeLose();
      }
    }

    // Analytics wants the human with the highest score in MP.
    int humanRoundsWon = 0;
    for (int i = 0; i < _PlayerInfo.Count; ++i) {
      if (_PlayerInfo[i].playerType == PlayerType.Human &&
          _PlayerInfo[i].playerRoundsWon > humanRoundsWon) {
        humanRoundsWon = _PlayerInfo[i].playerRoundsWon;
      }
    }

    int robotRoundsWon = 0;
    PlayerInfo robotPlayer = GetFirstPlayerByType(PlayerType.Cozmo);
    if (robotPlayer != null) {
      robotRoundsWon = robotPlayer.playerRoundsWon;
    }
    DAS.Event("game.end.score", humanRoundsWon.ToString(), DASUtil.FormatExtraData(robotRoundsWon.ToString()));

    if (GetPlayerCount() > 2) {
      PlayerInfo info = GetPlayerMostRoundsWon();
      int playerIndex = _PlayerInfo.IndexOf(info);
      DAS.Event("game.end.MP.winnertype", playerIndex.ToString());
      DAS.Event("game.end.MP.roundsTotal", TotalRounds.ToString());
    }

    Anki.Cozmo.Audio.GameAudioClient.PostSFXEvent(Anki.AudioMetaData.GameEvent.Sfx.Game_End);
    Anki.Cozmo.Audio.GameAudioClient.SetMusicState(Anki.AudioMetaData.GameState.Music.Freeplay);

    DAS.Info(this, "HandleChallengeResultViewClosed");
  }

  private void HandleQuitConfirmed() {
    RaiseChallengeQuit();
  }

  #endregion

  // end Challenge Exit handling

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

  public void SetLEDs(int id, Color[] color) {
    if (CurrentRobot != null) {
      LightCube cube = null;
      CurrentRobot.LightCubes.TryGetValue(id, out cube);
      if (cube != null) {
        cube.SetLEDs(color);
      }
    }
  }

  public void SetLEDs(int id, Color color) {
    if (CurrentRobot != null) {
      LightCube cube = null;
      CurrentRobot.LightCubes.TryGetValue(id, out cube);
      if (cube != null) {
        cube.SetLEDs(color);
      }
    }
  }


  public float GetCycleDurationSeconds(int numRotations, float cycleIntervalSeconds) {
    return (numRotations * cycleIntervalSeconds * 4);
  }

  public void StartCycleCube(int cubeID, Color[] lightColorsCounterclockwise, float cycleIntervalSeconds) {
    StartCycleCubeInternal(cubeID, lightColorsCounterclockwise, cycleIntervalSeconds);
  }

  private void StartCycleCubeInternal(int cubeID, Color[] lightColorsCounterclockwise, float cycleIntervalSeconds) {
    // Remove from blink lights if it exists there
    StopBlinkLight(cubeID);

    // Set colors
    LightCube cube;

    if (CurrentRobot == null || !CurrentRobot.LightCubes.TryGetValue(cubeID, out cube)) {
      DAS.Warn("GameBase.StartCycleCubeInternal", "No lightcube with ID " + cubeID + " cube probably disconnected");
      return;
    }

    cube.SetLEDs(lightColorsCounterclockwise);

    cube.rotationPeriodMs = cycleIntervalSeconds * 1000f;
  }

  public void BlinkLight(int cubeId, float duration, Color blinkColor, Color originalColor) {
    BlinkLight(cubeId, duration, new Color[] { blinkColor }, new Color[] { originalColor });
  }

  public void BlinkLight(int cubeId, float duration, Color[] blinkColorsCounterclockwise, Color[] originalColors) {
    // Set up timing data
    BlinkData data = new BlinkData();
    data.cubeID = cubeId;
    data.blinkDuration = duration;
    data.timeElaspedSeconds = 0f;
    data.originalColor = originalColors;

    // Set colors
    LightCube cube = null;
    CurrentRobot.LightCubes.TryGetValue(cubeId, out cube);
    if (cube == null) {
      return;
    }

    cube.SetLEDs(blinkColorsCounterclockwise);

    // Update data
    if (_BlinkCubeTimers.ContainsKey(cubeId)) {
      _BlinkCubeTimers[cubeId] = data;
    }
    else {
      _BlinkCubeTimers.Add(cubeId, data);
    }
  }

  private void StopBlinkLight(int cubeId) {
    if (_BlinkCubeTimers.ContainsKey(cubeId)) {
      LightCube cube = null;
      CurrentRobot.LightCubes.TryGetValue(cubeId, out cube);
      if (cube == null) {
        return;
      }
      cube.SetLEDs(_BlinkCubeTimers[cubeId].originalColor);
      _BlinkCubeTimers.Remove(cubeId);
    }
  }

  private void UpdateBlinkLights() {
    List<int> cubesToStopBlinking = new List<int>();
    foreach (KeyValuePair<int, BlinkData> cubeIdToTimer in _BlinkCubeTimers) {
      cubeIdToTimer.Value.timeElaspedSeconds += Time.deltaTime;
      if (cubeIdToTimer.Value.timeElaspedSeconds > cubeIdToTimer.Value.blinkDuration) {
        cubesToStopBlinking.Add(cubeIdToTimer.Key);
      }
    }

    foreach (int cubeId in cubesToStopBlinking) {
      StopBlinkLight(cubeId);
    }
  }

  public void HandleLightCubeRemoved(LightCube cube) {
    // If this game was using a cube that was removed, quit the game and
    // for all other cubes the game is using make sure to turn their lights off
    foreach (int id in CubeIdsForGame) {
      if (id == cube.ID) {
        ShowInterruptionQuitGameView("cube_lost_alert",
                                     LocalizationKeys.kMinigameLostTrackOfBlockTitle,
                                     LocalizationKeys.kMinigameLostTrackOfBlockDescription);

        // Remove this handler in the case multiple lightcubes are removed at the
        // same time we don't show multiple QuitGameViews
        if (CurrentRobot != null) {
          CurrentRobot.OnLightCubeRemoved -= HandleLightCubeRemoved;
        }
      }
      else {
        if (CurrentRobot != null) {
          LightCube lightCube;
          CurrentRobot.LightCubes.TryGetValue(id, out lightCube);
          if (lightCube != null) {
            lightCube.SetLEDsOff();
          }
        }
      }
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
    return _ChallengeData.ChallengeID.ToLower().Replace("game", "");
  }

  private string GetGameTimeElapsedAsStr() {
    return string.Format("{0}", Time.time - _GameStartTime);
  }

  public float GetGameTimeElapsedInSeconds() {
    return Time.time - _GameStartTime;
  }

  #endregion

  // end DAS

  #region Cliff or Pickup handling

  protected void RegisterRobotReactionaryBehaviorEvents() {
    DeregisterRobotReactionaryBehaviorEvents();
    RobotEngineManager.Instance.AddCallback<ReactionTriggerTransition>(HandleRobotReactionaryBehavior);
  }

  protected void DeregisterRobotReactionaryBehaviorEvents() {
    RobotEngineManager.Instance.RemoveCallback<ReactionTriggerTransition>(HandleRobotReactionaryBehavior);
  }

  protected void HandleRobotReactionaryBehavior(object messageObject) {
    ReactionTriggerTransition behaviorTransition = messageObject as ReactionTriggerTransition;

    // log das event
    string currentStateString = "";
    State currState = _StateMachine.GetCurrState();
    if (currState != null) {
      currentStateString = currState.GetType().ToString();
    }
    DAS.Event("robot.interrupt", currentStateString, DASUtil.FormatExtraData(behaviorTransition.newTrigger.ToString()));

    if ((behaviorTransition.newTrigger != ReactionTrigger.NoneTrigger) &&
        (behaviorTransition.oldTrigger == ReactionTrigger.NoneTrigger)) {
      PauseStateMachine(State.PauseReason.ENGINE_MESSAGE, behaviorTransition.newTrigger);
    }
    else if (behaviorTransition.newTrigger == ReactionTrigger.NoneTrigger) {
      ResumeStateMachine(State.PauseReason.ENGINE_MESSAGE, behaviorTransition.oldTrigger);
    }
  }

  public void ShowDontMoveCozmoAlertView() {
    ShowInterruptionQuitGameView("cozmo_moved_by_user_alert", LocalizationKeys.kMinigameDontMoveCozmoTitle, LocalizationKeys.kMinigameDontMoveCozmoDescription);
  }

  public void ShowInterruptionQuitGameView(string dasAlertName, string titleKey, string descriptionKey) {
    _StateMachine.Stop();

    SoftEndGameRobotReset();

    if (_SharedMinigameViewInstance != null) {
      _SharedMinigameViewInstance.HideQuitButton();
    }

    CreateInterruptionQuitGameView(dasAlertName, titleKey, descriptionKey);
  }

  private void CreateInterruptionQuitGameView(string dasAlertName, string titleKey, string descriptionKey) {
    if (_InterruptedAlertView == null) {
      var interruptedAlertData = new AlertModalData(dasAlertName, titleKey, descriptionKey,
                                                    new AlertModalButtonData("okay_button", LocalizationKeys.kButtonOkay,
                                                                             clickCallback: HandleInterruptionQuitGameViewClosed));

      var interruptedAlertPriorityData = new ModalPriorityData(ModalPriorityLayer.High, 0,
                                                               LowPriorityModalAction.Queue,
                                                               HighPriorityModalAction.ForceCloseOthersAndOpen);

      System.Action<AlertModal> interruptedAlertCreated = (alertModal) => {
        alertModal.ModalClosedWithCloseButtonOrOutsideAnimationFinished += HandleInterruptionQuitGameViewClosed;
        alertModal.ModalForceClosedAnimationFinished += () => {
          _InterruptedAlertView = null;
          CreateInterruptionQuitGameView(dasAlertName, titleKey, descriptionKey);
        };
        _InterruptedAlertView = alertModal;
        Anki.Cozmo.Audio.GameAudioClient.PostSFXEvent(Anki.AudioMetaData.GameEvent.Sfx.Gp_Shared_Game_End);
      };

      UIManager.OpenAlert(interruptedAlertData, interruptedAlertPriorityData, interruptedAlertCreated,
                          overrideCloseOnTouchOutside: false);
    }
  }

  private void HandleInterruptionQuitGameViewClosed() {
    RaiseChallengeQuit();
  }

  #endregion
}

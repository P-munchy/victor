using UnityEngine;
using System.Collections;
using System;
using Anki.Cozmo;
using Anki.Cozmo.Audio;
using Cozmo.Util;
using System.Collections.Generic;

namespace SpeedTap {

  public enum FirstToTap {
    Cozmo,
    Player,
    NoTaps
  }

  public class SpeedTapGame : GameBase {

    private const float _kTapAdjustRange = 5.0f;

    private const string _kWrongTapChance = "WrongTapChance";
    private const string _kTapDelayMin = "TapDelayMin";
    private const string _kTapDelayMax = "TapDelayMax";

    public FirstToTap FirstTapper {
      get {
        // If neither timestamp has been set, no taps
        if (_LastCozmoTimeStamp == -1 && _LastPlayerTimeStamp == -1) {
          return FirstToTap.NoTaps;
        }
        // If one of the two timestamps hasn't been set, other one counts as first
        if ((_LastCozmoTimeStamp == -1) || (_LastPlayerTimeStamp == -1)) {
          if (_LastCozmoTimeStamp != -1) {
            return FirstToTap.Cozmo;
          }
          else if (_LastPlayerTimeStamp != -1) {
            return FirstToTap.Player;
          }
        }
        // If both have been set, most recent timestamp counts as first
        if (_LastCozmoTimeStamp < _LastPlayerTimeStamp) {
          return FirstToTap.Cozmo;
        }
        else {
          return FirstToTap.Player;
        }
      }
    }

    public void ResetTapTimestamps() {
      _LastCozmoTimeStamp = -1;
      _LastPlayerTimeStamp = -1;
    }

    private float _LastPlayerTimeStamp = -1;
    private float _LastCozmoTimeStamp = -1;

    #region Config Values

    public float BaseMatchChance { get; private set; }

    public float MatchChanceIncrease { get; private set; }

    public float CurrentMatchChance { get; set; }

    public float MinIdleIntervalMs { get; private set; }

    public float MaxIdleIntervalMs { get; private set; }

    public float MinTapDelayMs { get; private set; }

    public float MaxTapDelayMs { get; private set; }

    public float CozmoMistakeChance { get; private set; }

    public float CozmoFakeoutChance { get; private set; }

    #endregion

    private Vector3 _CozmoPos;
    private Quaternion _CozmoRot;

    public LightCube CozmoBlock;
    public LightCube PlayerBlock;

    public readonly Color[] PlayerWinColors = new Color[4];
    public readonly Color[] CozmoWinColors = new Color[4];

    public Color PlayerWinColor { 
      get { 
        return PlayerWinColors[0]; 
      } 
      set {
        PlayerWinColors.Fill(value);
      }
    }

    public Color CozmoWinColor { 
      get { 
        return CozmoWinColors[0]; 
      } 
      set {
        CozmoWinColors.Fill(value);
      }
    }

    private List<SpeedTapDifficultyData> _AllDifficultySettings;
    private SpeedTapDifficultyData _CurrentDifficultySettings;

    public SpeedTapDifficultyData CurrentDifficultySettings {
      get {
        return _CurrentDifficultySettings;
      }
    }


    private MusicStateWrapper _BetweenRoundsMusic;

    public MusicStateWrapper BetweenRoundsMusic { get { return _BetweenRoundsMusic; } }

    public SpeedTapRulesBase Rules;

    public event Action PlayerTappedBlockEvent;
    public event Action CozmoTappedBlockEvent;

    [SerializeField]
    private GameObject _PlayerTapSlidePrefab;

    [SerializeField]
    private GameObject _PlayerTapRoundBeginSlidePrefab;

    [SerializeField]
    private GameObject _WaitForCozmoSlidePrefab;

    protected override void Initialize(MinigameConfigBase minigameConfig) {
      SpeedTapGameConfig speedTapConfig = minigameConfig as SpeedTapGameConfig;
      // Set all Config based values
      TotalRounds = speedTapConfig.Rounds;
      MaxScorePerRound = speedTapConfig.MaxScorePerRound;
      _AllDifficultySettings = speedTapConfig.DifficultySettings;
      _BetweenRoundsMusic = speedTapConfig.BetweenRoundMusic;
      BaseMatchChance = speedTapConfig.BaseMatchChance;
      CurrentMatchChance = BaseMatchChance;
      MatchChanceIncrease = speedTapConfig.MatchChanceIncrease;
      MinIdleIntervalMs = speedTapConfig.MinIdleIntervalMs;
      MaxIdleIntervalMs = speedTapConfig.MaxIdleIntervalMs;
      CozmoFakeoutChance = speedTapConfig.CozmoFakeoutChance;

      CozmoMistakeChance = SkillSystem.Instance.GetSkillVal(_kWrongTapChance);
      MinTapDelayMs = SkillSystem.Instance.GetSkillVal(_kTapDelayMin);
      MaxTapDelayMs = SkillSystem.Instance.GetSkillVal(_kTapDelayMax);

      GameEventManager.Instance.SendGameEventToEngine(Anki.Cozmo.GameEvent.OnSpeedtapStarted);
      // End config based values
      InitializeMinigameObjects(1);
    }

    // Use this for initialization
    protected void InitializeMinigameObjects(int cubesRequired) { 

      InitialCubesState initCubeState = new InitialCubesState(
                                          new SelectDifficultyState(
                                            new SpeedTapCozmoDriveToCube(true),
                                            DifficultyOptions,
                                            HighestLevelCompleted()
                                          ), 
                                          cubesRequired);
      _StateMachine.SetNextState(initCubeState);

      CurrentRobot.VisionWhileMoving(true);
      LightCube.TappedAction += BlockTapped;
      RobotEngineManager.Instance.OnRobotAnimationEvent += OnRobotAnimationEvent;
      CurrentRobot.SetVisionMode(Anki.Cozmo.VisionMode.DetectingFaces, false);
      CurrentRobot.SetVisionMode(Anki.Cozmo.VisionMode.DetectingMarkers, true);
      CurrentRobot.SetVisionMode(Anki.Cozmo.VisionMode.DetectingMotion, false);
      CurrentRobot.SetEnableFreeplayBehaviorChooser(false);
    }

    // Set up Difficulty Settings that are not round specific
    protected override void OnDifficultySet(int difficulty) {
      Rules = GetRules((SpeedTapRuleSet)difficulty);
      _CurrentDifficultySettings = null;
      for (int i = 0; i < _AllDifficultySettings.Count; i++) {
        if (_AllDifficultySettings[i].DifficultyID == difficulty) {
          _CurrentDifficultySettings = _AllDifficultySettings[i];
        }
      }
      if (_CurrentDifficultySettings == null) {
        DAS.Warn("SpeedTapGame.OnDifficultySet.NoValidSettingFound", string.Empty);
        _CurrentDifficultySettings = _AllDifficultySettings[0];
      }
      else {
        if (_CurrentDifficultySettings.Colors != null && _CurrentDifficultySettings.Colors.Length > 0) {
          Rules.SetUsableColors(_CurrentDifficultySettings.Colors);
        }
      }
    }

    protected override void CleanUpOnDestroy() {
      LightCube.TappedAction -= BlockTapped;
      RobotEngineManager.Instance.OnRobotAnimationEvent -= OnRobotAnimationEvent;

      GameEventManager.Instance.SendGameEventToEngine(Anki.Cozmo.GameEvent.OnSpeedtapGetOut);
    }

    public void InitialCubesDone() {
      CozmoBlock = CurrentRobot.LightCubes[CubeIdsForGame[0]];
    }

    public void SetPlayerCube(LightCube cube) {
      PlayerBlock = cube;
      CubeIdsForGame.Add(cube);
    }

    public override void UpdateUI() {
      base.UpdateUI();
      int halfTotalRounds = (TotalRounds + 1) / 2;
      Cozmo.MinigameWidgets.ScoreWidget cozmoScoreWidget = SharedMinigameView.CozmoScoreboard;
      cozmoScoreWidget.Score = CozmoScore;
      cozmoScoreWidget.MaxRounds = halfTotalRounds;
      cozmoScoreWidget.RoundsWon = CozmoRoundsWon;

      Cozmo.MinigameWidgets.ScoreWidget playerScoreWidget = SharedMinigameView.PlayerScoreboard;
      playerScoreWidget.Score = PlayerScore;
      playerScoreWidget.MaxRounds = halfTotalRounds;
      playerScoreWidget.RoundsWon = PlayerRoundsWon;

      // Display the current round
      SharedMinigameView.InfoTitleText = Localization.GetWithArgs(LocalizationKeys.kSpeedTapRoundsText, CozmoRoundsWon + PlayerRoundsWon + 1);
    }

    public override void UpdateUIForGameEnd() {
      base.UpdateUIForGameEnd();
      // Hide Current Round at end
      SharedMinigameView.InfoTitleText = string.Empty;
    }

    /// <summary>
    /// Sets player's last tapped timestamp based on light cube message.
    /// Cozmo uses AnimationEvents for maximum accuracy.
    /// </summary>
    /// <param name="blockID">Block ID.</param>
    /// <param name="tappedTimes">Tapped times.</param>
    /// <param name="timeStamp">Time stamp.</param>
    private void BlockTapped(int blockID, int tappedTimes, float timeStamp) {
      if (PlayerBlock != null && PlayerBlock.ID == blockID) {
        if (PlayerTappedBlockEvent != null) {
          _LastPlayerTimeStamp = timeStamp;
          PlayerTappedBlockEvent();
        }
      }
    }

    /// <summary>
    /// Sets cozmo's last tapped timestamp based on animation event message
    /// </summary>
    /// <param name="animEvent">Animation event.</param>
    private void OnRobotAnimationEvent(Anki.Cozmo.ExternalInterface.AnimationEvent animEvent) {
      _LastCozmoTimeStamp = animEvent.timestamp;
    }

    private static SpeedTapRulesBase GetRules(SpeedTapRuleSet ruleSet) {
      switch (ruleSet) {
      case SpeedTapRuleSet.NoRed:
        return new NoRedSpeedTapRules();
      case SpeedTapRuleSet.TwoColor:
        return new TwoColorSpeedTapRules();
      case SpeedTapRuleSet.LightCountMultiColor:
        return new LightCountMultiColorSpeedTapRules();
      case SpeedTapRuleSet.LightCountTwoColor:
        return new LightCountTwoColorSpeedTapRules();
      default:
        return new NoRedSpeedTapRules();
      }
    }

    public void ShowPlayerTapConfirmSlide() {
      SharedMinigameView.ShowWideGameStateSlide(_PlayerTapSlidePrefab, "PlayerTapConfirmSlide");
    }

    public void ShowPlayerTapNewRoundSlide() {
      SharedMinigameView.ShowWideGameStateSlide(_PlayerTapRoundBeginSlidePrefab, "PlayerTapNewRoundSlide");
    }

    public void ShowWaitForCozmoSlide() {
      SharedMinigameView.ShowWideGameStateSlide(_WaitForCozmoSlidePrefab, "WaitForCozmoSlide");
    }

    public void SetCozmoOrigPos() {
      _CozmoPos = CurrentRobot.WorldPosition;
      _CozmoRot = CurrentRobot.Rotation;
    }

    // TODO: Reset _CozmoPos and _CozmoRot whenever we receive a deloc message to prevent COZMO-829
    public void CheckForAdjust(RobotCallback adjustCallback = null) {
      float dist = 0.0f;
      dist = (CurrentRobot.WorldPosition - _CozmoPos).magnitude;
      if (dist > _kTapAdjustRange) {
        Debug.LogWarning(string.Format("ADJUST : From Pos {0} - Rot {1} : To Pos {2} - Rot {3}", CurrentRobot.WorldPosition, CurrentRobot.Rotation, _CozmoPos, _CozmoRot));
        CurrentRobot.GotoPose(_CozmoPos, _CozmoRot, false, false, adjustCallback);
      }
      else {
        if (adjustCallback != null) {
          adjustCallback.Invoke(false);
        }
      }
    }

    protected override void RaiseMiniGameQuit() {
      base.RaiseMiniGameQuit();
      Dictionary<string, string> quitGameScoreKeyValues = new Dictionary<string, string>();
      Dictionary<string, string> quitGameRoundsWonKeyValues = new Dictionary<string, string>();

      quitGameScoreKeyValues.Add("CozmoScore", CozmoScore.ToString());
      quitGameRoundsWonKeyValues.Add("CozmoRoundsWon", CozmoRoundsWon.ToString());

      DAS.Event(DASConstants.Game.kQuitGameScore, PlayerScore.ToString(), null, quitGameScoreKeyValues);
      DAS.Event(DASConstants.Game.kQuitGameRoundsWon, PlayerRoundsWon.ToString(), null, quitGameRoundsWonKeyValues);
    }

    public SpeedTapRoundData GetCurrentRoundData() {
      return CurrentDifficultySettings.SpeedTapRoundSettings[RoundsPlayed];
    }

    public float GetLightsOffDurationSec() {
      SpeedTapRoundData currRoundData = GetCurrentRoundData();
      return currRoundData.SecondsBetweenHands;
    }

    public float GetLightsOnDurationSec() {
      SpeedTapRoundData currRoundData = GetCurrentRoundData();
      return currRoundData.SecondsHandDisplayed;
    }

    public void StartRoundMusic() {
      SpeedTapRoundData currRoundData = GetCurrentRoundData();
      if (currRoundData.MidRoundMusic != Anki.Cozmo.Audio.GameState.Music.Invalid) {
        GameAudioClient.SetMusicState(currRoundData.MidRoundMusic);
      }
      else {
        GameAudioClient.SetMusicState(GetDefaultMusicState());
      }
    }

    private void ShowCubeMovedQuitGameView(string titleKey, string descriptionKey) {
      EndGameRobotReset();

      Cozmo.UI.AlertView alertView = UIManager.OpenView(Cozmo.UI.AlertViewLoader.Instance.AlertViewPrefab, overrideCloseOnTouchOutside: false);
      alertView.SetCloseButtonEnabled(false);
      alertView.SetPrimaryButton(LocalizationKeys.kButtonQuitGame, HandleCubeMovedQuitGameViewClosed);
      alertView.ViewClosed += HandleCubeMovedQuitGameViewClosed;
      alertView.TitleLocKey = titleKey;
      alertView.DescriptionLocKey = descriptionKey;
      Anki.Cozmo.Audio.GameAudioClient.PostSFXEvent(Anki.Cozmo.Audio.GameEvent.SFX.GameSharedEnd);
    }

    private void HandleCubeMovedQuitGameViewClosed() {
      RaiseMiniGameQuit();
    }
  }
}

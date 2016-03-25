﻿using UnityEngine;
using System.Collections;
using System.Collections.Generic;

namespace CubeSlap {
  
  public class CubeSlapGame : GameBase {

    public const string kSetUp = "SetUp";
    public const string kWaitForPounce = "WaitForPounce";
    public const string kCozmoWinEarly = "CozmoWinEarly";
    public const string kCozmoWinPounce = "CozmoWinPounce";
    public const string kPlayerWin = "PlayerWin";
    // Consts for determining the exact placement and forgiveness for cube location
    // Must be consistent for animations to work
    public const float kCubePlaceDist = 80.0f;
    public const float kCubeLostDelay = 0.25f;
    private int _CozmoScore;
    private int _PlayerScore;
    private int _PlayerRoundsWon;
    private int _CozmoRoundsWon;

    private float _MinSlapDelay;
    private float _MaxSlapDelay;
    private int _Rounds;

    private bool _CliffFlagTrown = false;
    // Flag to keep track if we've actually done the Pounce animation this round
    private bool _SlapFlagThrown = false;
    private float _CurrentSlapChance;
    private float _BaseSlapChance;
    private int _MaxFakeouts;

    private LightCube _CurrentTarget = null;

    [SerializeField]
    private List<string> _FakeoutAnimations;
    [SerializeField]
    private string _PounceAnimation = "pounceForward";
    [SerializeField]
    private string _RetractAnimation = "pounceRetract";

    protected override void Initialize(MinigameConfigBase minigameConfig) {
      CubeSlapConfig config = minigameConfig as CubeSlapConfig;
      _Rounds = config.Rounds;
      _MinSlapDelay = config.MinSlapDelay;
      _MaxSlapDelay = config.MaxSlapDelay;
      _BaseSlapChance = config.StartingSlapChance;
      _MaxFakeouts = config.MaxFakeouts;
      _CozmoScore = 0;
      _PlayerScore = 0;
      _CurrentTarget = null;
      InitializeMinigameObjects(config.NumCubesRequired());
    }

    protected void InitializeMinigameObjects(int numCubes) {

      CurrentRobot.SetBehaviorSystem(false);
      CurrentRobot.SetVisionMode(Anki.Cozmo.VisionMode.DetectingFaces, false);

      RobotEngineManager.Instance.OnCliffEvent += HandleCliffEvent;

      InitialCubesState initCubeState = new InitialCubesState(new HowToPlayState(new SeekState()), numCubes);
      _StateMachine.SetNextState(initCubeState);
    }

    protected override void CleanUpOnDestroy() {
      RobotEngineManager.Instance.OnCliffEvent -= HandleCliffEvent;
    }

    public LightCube GetCurrentTarget() {
      if (_CurrentTarget == null) {
        if (this.CubeIdsForGame.Count > 0) {
          _CurrentTarget = CurrentRobot.LightCubes[this.CubeIdsForGame[0]];
        }
      }
      return _CurrentTarget;
    }

    // Attempt the pounce
    public void AttemptSlap() {
      float SlapRoll;
      if (_MaxFakeouts <= 0) {
        SlapRoll = 0.0f;
      }
      else {
        SlapRoll = Random.Range(0.0f, 1.0f);
      }
      if (SlapRoll <= _CurrentSlapChance) {
        // Enter Animation State to attempt a pounce.
        // Set Callback for HandleEndSlapAttempt
        _CliffFlagTrown = false;
        _SlapFlagThrown = true;
        CurrentRobot.SendAnimation(_PounceAnimation, HandleEndSlapAttempt);
      }
      else {
        // If you do a fakeout instead, increase the likelyhood of a slap
        // attempt based on the max number of fakeouts.
        int rand = Random.Range(0, _FakeoutAnimations.Count);
        _CurrentSlapChance += ((1.0f - _BaseSlapChance) / _MaxFakeouts);
        _StateMachine.SetNextState(new AnimationState(_FakeoutAnimations[rand], HandleFakeoutEnd));
      }
    }

    private void HandleEndSlapAttempt(bool success) {
      // If the animation completes and the cube is beneath Cozmo,
      // Cozmo has won.
      if (_CliffFlagTrown) {
        SharedMinigameView.InfoTitleText = Localization.Get(LocalizationKeys.kCubePounceHeaderCozmoWinPoint);
        SharedMinigameView.ShowInfoTextSlideWithKey(LocalizationKeys.kCubePounceInfoCozmoWinPoint);
        OnFailure();
        return;
      }
      else {
        // If the animation completes Cozmo is not on top of the Cube,
        // The player has won this round 
        SharedMinigameView.InfoTitleText = Localization.Get(LocalizationKeys.kCubePounceHeaderPlayerWinPoint);
        SharedMinigameView.ShowInfoTextSlideWithKey(LocalizationKeys.kCubePounceInfoPlayerWinPoint);
        OnSuccess();
      }
    }

    public void HandleFakeoutEnd(bool success) {
      _StateMachine.SetNextState(new SlapGameState());
    }

    private void HandleCliffEvent(Anki.Cozmo.CliffEvent cliff) {
      // Ignore if it throws this without a cliff actually detected
      if (!cliff.detected) {
        return;
      }
      _CliffFlagTrown = true;
    }

    public void OnSuccess() {
      _PlayerScore++;
      UpdateScoreboard();
      _StateMachine.SetNextState(new AnimationState(AnimationName.kMajorFail, HandleAnimationDone));
    }

    public void OnFailure() {
      _CozmoScore++;
      UpdateScoreboard();
      _StateMachine.SetNextState(new AnimationGroupState(AnimationGroupName.kWin, HandleAnimationDone));
    }

    public void HandleAnimationDone(bool success) {
      // Determines winner and loser at the end of Cozmo's animation, or returns
      // to seek state for the next round
      // Display the current round
      UpdateRoundsUI();
      if (_CozmoScore + _PlayerScore >= _Rounds) {
        if (_CozmoScore > _PlayerScore) {
          _StateMachine.SetNextState(new AnimationGroupState(AnimationGroupName.kSpeedTap_WinSession, HandleLoseGameAnimationDone));
        }
        else {
          _StateMachine.SetNextState(new AnimationGroupState(AnimationGroupName.kSpeedTap_LoseSession, HandleWinGameAnimationDone));
        }
      }
      else if (_SlapFlagThrown) {
        _SlapFlagThrown = false;
        _StateMachine.SetNextState(new AnimationState(_RetractAnimation, HandleRetractDone));
      }
      else {
        _StateMachine.SetNextState(new SeekState());
      }
    }

    public void HandleWinGameAnimationDone(bool success) {
      RaiseMiniGameWin();
    }

    public void HandleLoseGameAnimationDone(bool success) {
      RaiseMiniGameLose();
    }

    public void HandleRetractDone(bool success) {
      _StateMachine.SetNextState(new SeekState());
    }

    public float GetSlapDelay() {
      return Random.Range(_MinSlapDelay, _MaxSlapDelay);
    }

    public void ResetSlapChance() {
      _CurrentSlapChance = _BaseSlapChance;
    }

    protected override int CalculateExcitementStatRewards() {
      return _CozmoScore;
    }

    public void UpdateRoundsUI() {
      // Display the current round
      SharedMinigameView.InfoTitleText = Localization.GetWithArgs(LocalizationKeys.kSpeedTapRoundsText, _CozmoScore + _PlayerScore + 1);
    }

    public void UpdateScoreboard() {
      SharedMinigameView.CozmoScoreboard.Score = _CozmoScore;
      SharedMinigameView.PlayerScoreboard.Score = _PlayerScore;
    }
  }
}

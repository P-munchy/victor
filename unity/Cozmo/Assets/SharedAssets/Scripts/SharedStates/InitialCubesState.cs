﻿using UnityEngine;
using System.Collections;
using System.Collections.Generic;

public class InitialCubesState : State {

  private State _NextState;
  private int _CubesRequired;
  private ShowCozmoCubeSlide _ShowCozmoCubesSlide;
  private int _NumValidCubes;
  private GameBase _Game;

  public InitialCubesState(State nextState, int cubesRequired) {
    _NextState = nextState;
    _CubesRequired = cubesRequired;
  }

  public override void Enter() {
    base.Enter();
    _CurrentRobot.SetLiftHeight(0f);
    _CurrentRobot.SetHeadAngle(CozmoUtil.kIdealBlockViewHeadValue);
    _CurrentRobot.SetVisionMode(Anki.Cozmo.VisionMode.DetectingMarkers, true);

    _Game = _StateMachine.GetGame();
    Debug.Log(_Game);
    Debug.Log(_Game.SharedMinigameView);
    _ShowCozmoCubesSlide = _Game.SharedMinigameView.ShowCozmoCubesSlide(_CubesRequired);
    _Game.SharedMinigameView.ShowContinueButtonOnShelf(HandleContinueButtonClicked,
      Localization.Get(LocalizationKeys.kButtonContinue), 
      Localization.GetWithArgs(LocalizationKeys.kMinigameLabelCubesFound, 0),
      Cozmo.UI.UIColorPalette.NeutralTextColor());
    _Game.SharedMinigameView.EnableContinueButton(false);
    _Game.CubesForGame = new List<LightCube>();
  }

  public override void Update() {
    base.Update();

    int numValidCubes = 0;
    foreach (KeyValuePair<int, LightCube> lightCube in _CurrentRobot.LightCubes) {

      bool isValidCube = lightCube.Value.MarkersVisible;

      if (isValidCube && numValidCubes < _CubesRequired) { 
        lightCube.Value.SetLEDs(Color.white);
        numValidCubes++;
        if (!_Game.CubesForGame.Contains(lightCube.Value)) {
          _Game.CubesForGame.Add(lightCube.Value);
        }
      }
      else {
        lightCube.Value.TurnLEDsOff();
        if (_Game.CubesForGame.Contains(lightCube.Value)) {
          _Game.CubesForGame.Remove(lightCube.Value);
        }
      }
    }

    if (numValidCubes != _NumValidCubes) {
      _NumValidCubes = numValidCubes;
      _ShowCozmoCubesSlide.LightUpCubes(_NumValidCubes);

      if (_NumValidCubes >= _CubesRequired) {
        _Game.SharedMinigameView.SetContinueButtonShelfText(Localization.GetWithArgs(LocalizationKeys.kMinigameLabelCubesReady,
          _CubesRequired), Cozmo.UI.UIColorPalette.CompleteTextColor());

        _Game.SharedMinigameView.EnableContinueButton(true);
      }
      else {
        _Game.SharedMinigameView.SetContinueButtonShelfText(Localization.GetWithArgs(LocalizationKeys.kMinigameLabelCubesFound,
          _NumValidCubes), Cozmo.UI.UIColorPalette.NeutralTextColor());

        _Game.SharedMinigameView.EnableContinueButton(false);
      }
    }
  }

  public override void Exit() {
    base.Exit();

    _Game.SharedMinigameView.HideGameStateSlide();
  }

  private void HandleContinueButtonClicked() {
    // TODO: Check if the game has been run before; if so skip the HowToPlayState
    _StateMachine.SetNextState(_NextState);
  }
}

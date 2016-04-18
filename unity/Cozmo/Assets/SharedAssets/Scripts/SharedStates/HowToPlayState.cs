﻿using UnityEngine;
using System.Collections;

public class HowToPlayState : State {

  private State _NextState;

  public HowToPlayState(State nextState) {
    _NextState = nextState;
  }

  private GameBase _Game;

  public override void Enter() {
    _Game = _StateMachine.GetGame();
    _Game.SharedMinigameView.ShowContinueButtonOnShelf(HandleContinueButtonClicked, 
      Localization.Get(LocalizationKeys.kButtonStartChallenge), Localization.Get(LocalizationKeys.kMinigameLabelReadyToPlay), 
      Cozmo.UI.UIColorPalette.NeutralTextColor(), "from_how_to_play_to_start_game_continue_button");
    _Game.SharedMinigameView.ShowHowToPlayButton();
    _Game.SharedMinigameView.OpenHowToPlayView();
  }

  private void HandleContinueButtonClicked() {
    // TODO: Check if the game has been run before; if so skip the HowToPlayState
    _Game.SharedMinigameView.HideBackButton();
    _Game.SharedMinigameView.ShowQuitButton();
    _Game.SharedMinigameView.CloseHowToPlayView();
    _Game.SharedMinigameView.HideContinueButtonShelf();

    if (_NextState == null) {
      _StateMachine.PopState();
    }
    else {
      _StateMachine.SetNextState(_NextState);
    }
  }
}

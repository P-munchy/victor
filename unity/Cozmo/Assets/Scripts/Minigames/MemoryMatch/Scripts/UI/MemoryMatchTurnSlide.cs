﻿using UnityEngine;
using UnityEngine.UI;
using System.Collections;
using Anki.UI;
using Cozmo.UI;

namespace MemoryMatch {
  public class MemoryMatchTurnSlide : MonoBehaviour {

    [SerializeField]
    private CozmoImage _CenterImage;

    [SerializeField]
    private CozmoText _StatusTextLabel;

    [SerializeField]
    private CozmoText _CenterTextLabel;

    [SerializeField]
    private string _CorrectSpriteId;
    [SerializeField]
    private string _WrongSpriteId;

    [SerializeField]
    private MemoryMatchLivesWidget _PlayerWidget;
    [SerializeField]
    private MemoryMatchLivesWidget _CozmoWidget;

    [SerializeField]
    private CozmoButton _ButtonPlayPattern;

    private UnityEngine.Events.UnityAction _ClickHandler = null;

    public void Awake() {
      _PlayerWidget.gameObject.SetActive(false);
      _CozmoWidget.gameObject.SetActive(false);
      _CenterImage.gameObject.SetActive(false);
      _StatusTextLabel.text = "";
    }

    public void ShowEndGame(Sprite portraitSprite) {
      _PlayerWidget.gameObject.SetActive(false);
      _CozmoWidget.gameObject.SetActive(false);
      if (portraitSprite) {
        _CenterImage.gameObject.SetActive(true);
        _CenterImage.sprite = portraitSprite;
      }
      else {
        _CenterImage.gameObject.SetActive(false);
      }
      ShowStatusText("");
    }

    public void ShowStatusText(string status) {
      if (status != null) {
        _StatusTextLabel.text = status;
      }
      else {
        _StatusTextLabel.text = "";
      }
      //Refresh text object
      _StatusTextLabel.gameObject.SetActive(false);
      if (!string.IsNullOrEmpty(status)) {
        _StatusTextLabel.gameObject.SetActive(true);
      }
    }
    public void ShowCenterText(string status) {
      if (status != null) {
        _CenterTextLabel.text = status;
      }
      else {
        _CenterTextLabel.text = "";
      }
    }

    public void ShowHumanLives(int lives, int maxLives) {
      _PlayerWidget.UpdateStatus(lives, maxLives);
      _PlayerWidget.gameObject.SetActive(true);
      _PlayerWidget.SetTurn(true);
      _CozmoWidget.SetTurn(false);
    }
    public void ShowCozmoLives(int lives, int maxLives) {
      _CozmoWidget.UpdateStatus(lives, maxLives);
      _CozmoWidget.gameObject.SetActive(true);
      _PlayerWidget.SetTurn(false);
      _CozmoWidget.SetTurn(true);
    }

    public void ShowCenterImage(bool enabled, bool showCorrect) {
      _CenterImage.gameObject.SetActive(enabled);
      _CenterImage.LinkedComponentId = showCorrect ? _CorrectSpriteId : _WrongSpriteId;
      _CenterImage.UpdateSkinnableElements();
    }

    public void ShowPlayPatternButton(UnityEngine.Events.UnityAction ClickHandler) {
      ContextManager.Instance.AppFlash(playChime: true);
      HidePlayPatternButton(); // clean up previous
      _ClickHandler = ClickHandler;
      _ButtonPlayPattern.gameObject.SetActive(true);
      _ButtonPlayPattern.Initialize(ClickHandler, "MemoryMatch.PlayPattern", "next_round_of_play_continue_button");
    }

    public void HidePlayPatternButton() {
      _ButtonPlayPattern.gameObject.SetActive(false);
      if (_ClickHandler != null) {
        _ButtonPlayPattern.onClick.RemoveListener(_ClickHandler);
        _ClickHandler = null;
      }
    }
  }
}
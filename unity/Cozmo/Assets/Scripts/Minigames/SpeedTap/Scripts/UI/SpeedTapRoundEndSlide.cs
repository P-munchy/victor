﻿using UnityEngine;
using UnityEngine.UI;
using System.Collections;
using Anki.UI;
using Cozmo.UI;

namespace SpeedTap {
  public class SpeedTapRoundEndSlide : MonoBehaviour {

    [SerializeField]
    private CozmoImage _PortraitImage;

    [SerializeField]
    private CozmoText _WinnerNameTextLabel;

    [SerializeField]
    private CozmoText _CurrentRoundTextLabel;

    [SerializeField]
    private SegmentedBar _RoundCountBar;

    public float BannerAnimationDurationSeconds = 3f;

    public void Initialize(Sprite portraitSprite, string winnerName, int roundsWon, int roundsNeeded, int currentRound, Color playerColor) {
      _PortraitImage.sprite = portraitSprite;
      _PortraitImage.color = playerColor;
      _WinnerNameTextLabel.text = winnerName;
      _CurrentRoundTextLabel.text = Localization.GetWithArgs(LocalizationKeys.kSpeedTapTextDisplayWinnerSubtitle, currentRound);
      _RoundCountBar.SetMaximumSegments(roundsNeeded);
      _RoundCountBar.SetCurrentNumSegments(roundsWon);
    }
  }
}
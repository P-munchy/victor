﻿using UnityEngine;
using UnityEngine.UI;
using System.Collections;
using DG.Tweening;
using Anki.UI;
using Cozmo.UI;

namespace Cozmo {
  namespace MinigameWidgets {
    public class ScoreWidget : MinigameWidget {

      private const float kAnimYOffset = 0.0f;
      private const float kAnimDur = 0.25f;

      private float _AnimationXOffset;

      public float AnimationXOffset {
        set {
          _AnimationXOffset = value;
          bool isOnLeft = (_AnimationXOffset < 0);

          // Widget on the left; use the left circuit
          _LeftCircuitry.gameObject.SetActive(isOnLeft);
          _RightCircuitry.gameObject.SetActive(!isOnLeft);

          // Flip background x scale if on left
          if (isOnLeft) {
            _Background.gameObject.transform.localScale = new Vector3(-1, 1, 1);
          }
        }
        private get { return _AnimationXOffset; }
      }

      [SerializeField]
      private Image _PortraitImage;

      [SerializeField]
      private LayoutElement _ScoreContainer;

      [SerializeField]
      private AnkiTextLabel _ScoreCountLabel;

      [SerializeField]
      private LayoutElement _RoundContainer;

      [SerializeField]
      private SegmentedBar _RoundCountBar;

      [SerializeField]
      private RectTransform _WinnerContainer;

      [SerializeField]
      private Canvas _WinnerCanvas;

      [SerializeField]
      private Image _LeftCircuitry;

      [SerializeField]
      private Image _RightCircuitry;

      [SerializeField]
      private Image _Background;

      public int MaxRounds {
        set {
          _RoundContainer.gameObject.SetActive(true);
          _RoundCountBar.SetMaximumSegments(value);
          _RoundCountBar.SetCurrentNumSegments(0);
        }
      }

      public Sprite Portrait {
        set {
          _PortraitImage.sprite = value;
        }
      }

      public int Score {
        set {
          _ScoreContainer.gameObject.SetActive(true);
          _ScoreCountLabel.text = Localization.GetNumber(value);
        }
      }

      public int RoundsWon {
        set {
          _RoundCountBar.SetCurrentNumSegments(value);
        }
      }

      public bool IsWinner {
        set {
          _WinnerContainer.gameObject.SetActive(value);
        }
      }

      public bool Dim {
        set {
          _PortraitImage.color = value ? Color.gray : Color.white;
        }
      }

      private void Awake() {
        _ScoreContainer.gameObject.SetActive(false);
        _RoundContainer.gameObject.SetActive(false);
        _WinnerContainer.gameObject.SetActive(false);
      }

      private void Update() {
        if (_WinnerContainer.gameObject.activeInHierarchy && _WinnerCanvas != null) {
          // Force the canvas to reposition itself
          _WinnerCanvas.gameObject.SetActive(false);
          _WinnerCanvas.gameObject.SetActive(true);
        }
      }

      #region IMinigameWidget

      public override void DestroyWidgetImmediately() {
        Destroy(gameObject);
      }

      public override Sequence CreateOpenAnimSequence() {
        return CreateOpenAnimSequence(AnimationXOffset, kAnimYOffset, kAnimDur);
      }

      public override Sequence CreateCloseAnimSequence() {
        return CreateCloseAnimSequence(AnimationXOffset, kAnimYOffset, kAnimDur);
      }

      #endregion
    }
  }
}
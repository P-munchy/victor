﻿using UnityEngine;
using UnityEngine.UI;
using DG.Tweening;
using Anki.UI;

namespace Cozmo {
  namespace MinigameWidgets {

    public class BannerWidget : MinigameWidget {

      private const float kAnimXOffset = 0.0f;
      private const float kAnimYOffset = -476.0f;
      private const float kAnimDur = 0.25f;

      [SerializeField]
      private RectTransform _BannerContainer;

      [SerializeField]
      private AnkiTextLabel _BannerTextLabel;

      [SerializeField]
      private float _BannerInOutAnimationDurationSeconds = 0.3f;

      [SerializeField]
      private float _BannerSlowAnimationDurationSeconds = .75f;

      [SerializeField]
      private float _BannerLeftOffscreenLocalXPos = 0;

      [SerializeField]
      private float _BannerRightOffscreenLocalXPos = 0f;

      [SerializeField]
      private float _BannerSlowDistance = 100f;

      private Sequence _BannerTween = null;

      private void Start() {
        transform.SetAsFirstSibling();
        _BannerContainer.gameObject.SetActive(false);
      }

      private void OnDestroy() {
        if (_BannerTween != null) {
          _BannerTween.Kill();
        }
      }


      public void PlayBannerAnimation(string textToDisplay, TweenCallback animationEndCallback = null, float customSlowDurationSeconds = 0f) {
        _BannerContainer.gameObject.SetActive(true);
        Vector3 localPos = _BannerContainer.gameObject.transform.localPosition;
        localPos.x = _BannerLeftOffscreenLocalXPos;
        _BannerContainer.gameObject.transform.localPosition = localPos;

        // set text
        _BannerTextLabel.text = textToDisplay;

        float slowDuration = (customSlowDurationSeconds != 0) ? customSlowDurationSeconds : _BannerSlowAnimationDurationSeconds;

        // build sequence
        if (_BannerTween != null) {
          _BannerTween.Kill();
        }
        _BannerTween = DOTween.Sequence();
        float midpoint = (_BannerRightOffscreenLocalXPos + _BannerLeftOffscreenLocalXPos) * 0.5f;
        _BannerTween.Append(_BannerContainer.DOLocalMoveX(midpoint - _BannerSlowDistance, _BannerInOutAnimationDurationSeconds).SetEase(Ease.OutQuad));
        _BannerTween.Append(_BannerContainer.DOLocalMoveX(midpoint, slowDuration));
        _BannerTween.Append(_BannerContainer.DOLocalMoveX(_BannerRightOffscreenLocalXPos, _BannerInOutAnimationDurationSeconds).SetEase(Ease.InQuad));
        _BannerTween.AppendCallback(HandleBannerAnimationEnd);
        if (animationEndCallback != null) {
          _BannerTween.AppendCallback(animationEndCallback);
        }
      }

      private void HandleBannerAnimationEnd() {
        _BannerContainer.gameObject.SetActive(false);
      }

      #region IMinigameWidget

      public override void DestroyWidgetImmediately() {
        Destroy(gameObject);
      }

      public override Sequence CreateOpenAnimSequence() {
        return CreateOpenAnimSequence(kAnimXOffset, kAnimYOffset, kAnimDur);
      }

      public override Sequence CreateCloseAnimSequence() {
        return CreateCloseAnimSequence(kAnimXOffset, kAnimYOffset, kAnimDur);
      }

      #endregion
    }
  }
}

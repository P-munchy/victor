﻿using UnityEngine;
using UnityEngine.UI;
using DG.Tweening;
using Anki.UI;

namespace Cozmo {
  namespace MinigameWidgets {
    public class ContinueGameShelfWidget : MinigameWidget {

      public delegate void ContinueButtonClickHandler();

      [SerializeField]
      private Cozmo.UI.CozmoButton _ContinueButton;

      [SerializeField]
      private AnkiTextLabel _ShelfTextLabel;

      private ContinueButtonClickHandler _OnClickCallback;

      public string DASEventViewController {
        get { return _ContinueButton.DASEventViewController; } 
        set { _ContinueButton.DASEventViewController = value; }
      }

      private void Awake() {
        _ContinueButton.DASEventButtonName = "continue_button";
        _ContinueButton.onClick.AddListener(HandleContinueButtonClicked);
      }

      private void OnDestroy() {
        _ContinueButton.onClick.RemoveListener(HandleContinueButtonClicked);
      }

      public void Initialize(ContinueGameShelfWidget.ContinueButtonClickHandler buttonClickHandler,
                             string buttonText, string shelfText, Color shelfColor) {
        _ContinueButton.Text = buttonText;
        _OnClickCallback = buttonClickHandler;
        SetShelfText(shelfText, shelfColor);
      }

      public void SetShelfText(string text, Color textColor) {
        if (_ShelfTextLabel != null) {
          _ShelfTextLabel.text = text;
          _ShelfTextLabel.color = textColor;
        }
      }

      private void HandleContinueButtonClicked() {
        if (_OnClickCallback != null) {
          _OnClickCallback();
        }
      }

      public void SetButtonInteractivity(bool enableButton) {
        _ContinueButton.Interactable = enableButton;
      }

      #region IMinigameWidget

      public override void DestroyWidgetImmediately() {
        Destroy(gameObject);
      }

      // TODO: Don't hardcode this
      public override Sequence OpenAnimationSequence() {
        Sequence open = DOTween.Sequence();
        open.Append(this.transform.DOLocalMove(new Vector3(this.transform.localPosition.x, 
          this.transform.localPosition.y - 300, this.transform.localPosition.z),
          0.25f).From().SetEase(Ease.OutQuad));
        return open;
      }

      // TODO: Don't hardcode this
      public override Sequence CloseAnimationSequence() {
        Sequence close = DOTween.Sequence();
        close.Append(this.transform.DOLocalMove(new Vector3(this.transform.localPosition.x, 
          this.transform.localPosition.y - 300, this.transform.localPosition.z),
          0.25f).SetEase(Ease.OutQuad));
        return close;
      }

      #endregion
    }
  }
}
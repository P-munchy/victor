﻿using UnityEngine;
using System.Collections.Generic;
using DG.Tweening;

namespace Cozmo {
  namespace UI {
    public abstract class BaseDialog : MonoBehaviour {
      // Static events
      public delegate void BaseDialogHandler(BaseDialog dialog);

      public static event BaseDialogHandler BaseDialogOpened;
      public static event BaseDialogHandler BaseDialogOpenAnimationFinished;
      public static event BaseDialogHandler BaseDialogClosed;
      public static event BaseDialogHandler BaseDialogCloseAnimationFinished;

      // Instance events
      public delegate void SimpleBaseDialogHandler();

      public event SimpleBaseDialogHandler DialogOpened;
      public event SimpleBaseDialogHandler DialogOpenAnimationFinished;
      public event SimpleBaseDialogHandler DialogClosed;
      public event SimpleBaseDialogHandler DialogCloseAnimationFinished;

      private const string _kOpenAnimationDisableKey = "BaseDialog.OpenAnimations";
      private const string _kCloseAnimationDisableKey = "BaseDialog.CloseAnimations";

      protected enum DialogState {
        Initialized,
        IsOpening,
        Open,
        IsClosing,
        Closed
      }

      private DialogState _CurrentDialogState = DialogState.Initialized;

      public bool IsClosed {
        get { return _CurrentDialogState == DialogState.IsClosing || _CurrentDialogState == DialogState.Closed; }
      }

      public bool IsAnimating {
        get { return _CurrentDialogState == DialogState.IsClosing || _CurrentDialogState == DialogState.IsOpening; }
      }

      [SerializeField]
      private string _DASEventDialogName = "";

      public string DASEventDialogName {
        get { return _DASEventDialogName; }
        set { _DASEventDialogName = value; }
      }

      protected string _DASEventExtraData = "";
      public string DASEventExtraData {
        get { return _DASEventExtraData; }
      }

      // The UI event that should play when this dialog opens
      [SerializeField]
      private Anki.Cozmo.Audio.AudioEventParameter _OpenAudioEvent = Anki.Cozmo.Audio.AudioEventParameter.InvalidEvent;

      public Anki.Cozmo.Audio.AudioEventParameter OpenAudioEvent {
        get { return _OpenAudioEvent; }
        set { _OpenAudioEvent = value; }
      }

      private Sequence _TransitionAnimation;

      [SerializeField]
      protected CanvasGroup _AlphaController;

      [SerializeField]
      protected MoveTweenSettings _MoveTweenSettings;

      [SerializeField]
      protected FadeTweenSettings _FadeTweenSettings;

      void OnDestroy() {
        if (_CurrentDialogState != DialogState.Closed) {
          if (_CurrentDialogState != DialogState.IsClosing) {
            RaiseDialogClosed();
          }

          BaseDialogCleanUpInternal();
        }
      }

      #region Overriden Methods

      public void Initialize() {
        CheckDASEventName();
        _CurrentDialogState = DialogState.Initialized;
      }

      public void StartOpen() {
        RaiseDialogOpened();
        PlayOpenSound();
        PlayOpenAnimations();
      }

      protected virtual void CleanUp() {
      }

      protected virtual void ConstructOpenAnimation(Sequence openAnimation) {
        if (!ConstructOpenAnimationBasedOnSettings(ref openAnimation)) {
          ConstructDefaultFadeOpenAnimation(openAnimation);
        }
      }

      protected bool ConstructOpenAnimationBasedOnSettings(ref Sequence openAnimation) {
        bool animationCreated = false;
        UIDefaultTransitionSettings settings = UIDefaultTransitionSettings.Instance;
        if (_MoveTweenSettings.targets.Length > 0) {
          settings.ConstructOpenMoveTween(ref openAnimation, _MoveTweenSettings);
          animationCreated = true;
        }
        if (_FadeTweenSettings.targets.Length > 0) {
          settings.ConstructOpenFadeTween(ref openAnimation, _FadeTweenSettings);
          animationCreated = true;
        }
        return animationCreated;
      }

      protected void ConstructDefaultFadeOpenAnimation(Sequence openAnimation) {
        UIDefaultTransitionSettings settings = UIDefaultTransitionSettings.Instance;
        CreateAlphaControllerIfNull();
        _AlphaController.alpha = 0;
        openAnimation.Join(settings.CreateFadeInTween(_AlphaController));
      }

      protected virtual void ConstructCloseAnimation(Sequence closeAnimation) {
        if (!ConstructCloseAnimationBasedOnSettings(ref closeAnimation)) {
          ConstructDefaultFadeCloseAnimation(closeAnimation);
        }
      }

      protected bool ConstructCloseAnimationBasedOnSettings(ref Sequence closeAnimation) {
        bool animationCreated = false;
        UIDefaultTransitionSettings settings = UIDefaultTransitionSettings.Instance;
        if (_MoveTweenSettings.targets.Length > 0) {
          settings.ConstructCloseMoveTween(ref closeAnimation, _MoveTweenSettings);
          animationCreated = true;
        }
        if (_FadeTweenSettings.targets.Length > 0) {
          settings.ConstructCloseFadeTween(ref closeAnimation, _FadeTweenSettings);
          animationCreated = true;
        }
        return animationCreated;
      }

      protected void ConstructDefaultFadeCloseAnimation(Sequence closeAnimation) {
        UIDefaultTransitionSettings settings = UIDefaultTransitionSettings.Instance;
        CreateAlphaControllerIfNull();
        _AlphaController.alpha = 1;
        closeAnimation.Join(settings.CreateFadeOutTween(_AlphaController));
      }

      protected void CreateAlphaControllerIfNull() {
        if (_AlphaController == null) {
          _AlphaController = gameObject.GetComponent<CanvasGroup>();
          if (_AlphaController == null) {
            _AlphaController = gameObject.AddComponent<CanvasGroup>();
          }
        }
      }

      #endregion

      private void CheckDASEventName() {
        if (string.IsNullOrEmpty(_DASEventDialogName)) {
          DAS.Error(this, string.Format("Dialog is missing a _DASEventDialogName! Please check the prefab. name={0}", gameObject.name));
          _DASEventDialogName = gameObject.name;
        }
      }

      public void CloseDialog() {
        // if we are already closing the dialog don't make multiple calls
        // to the callback and don't try to play the close animation again
        if (_CurrentDialogState != DialogState.IsClosing
            && _CurrentDialogState != DialogState.Closed) {
          PlayCloseAnimations();
          RaiseDialogClosed();
        }
      }

      public void CloseDialogImmediately() {
        if (_CurrentDialogState != DialogState.Closed) {
          if (_CurrentDialogState == DialogState.IsOpening) {
            UIManager.EnableTouchEvents(_kOpenAnimationDisableKey);
          }

          if (_CurrentDialogState != DialogState.IsClosing) {
            RaiseDialogClosed();
          }

          BaseDialogCleanUpInternal();
          Destroy(gameObject);
        }
      }

      // Play a sound when the dialog is shown (on calling Initialize)
      private void PlayOpenSound() {
        if (!OpenAudioEvent.IsInvalid()) {
          Anki.Cozmo.Audio.GameAudioClient.PostAudioEvent(OpenAudioEvent);
        }
      }

      private void PlayOpenAnimations() {
        if (_CurrentDialogState == DialogState.Initialized) {
          _CurrentDialogState = DialogState.IsOpening;
          UIManager.DisableTouchEvents(_kOpenAnimationDisableKey);

          // Play some animations
          if (_TransitionAnimation != null) {
            _TransitionAnimation.Kill();
          }
          _TransitionAnimation = DOTween.Sequence();
          ConstructOpenAnimation(_TransitionAnimation);
          _TransitionAnimation.AppendCallback(OnOpenAnimationsFinished);
        }
      }

      private void OnOpenAnimationsFinished() {
        _CurrentDialogState = DialogState.Open;
        UIManager.EnableTouchEvents(_kOpenAnimationDisableKey);

        // Raise event
        RaiseDialogOpenAnimationFinished();
      }

      private void PlayCloseAnimations() {
        if (_CurrentDialogState == DialogState.IsOpening) {
          UIManager.EnableTouchEvents(_kOpenAnimationDisableKey);
        }

        _CurrentDialogState = DialogState.IsClosing;
        UIManager.DisableTouchEvents(_kCloseAnimationDisableKey);

        // Play some animations
        if (_TransitionAnimation != null) {
          _TransitionAnimation.Kill();
        }
        _TransitionAnimation = DOTween.Sequence();
        ConstructCloseAnimation(_TransitionAnimation);
        _TransitionAnimation.AppendCallback(OnCloseAnimationsFinished);
      }

      private void OnCloseAnimationsFinished() {
        if (_CurrentDialogState != DialogState.Closed) {
          BaseDialogCleanUpInternal();
          Destroy(gameObject);
        }
      }

      private void BaseDialogCleanUpInternal() {
        _CurrentDialogState = DialogState.Closed;
        if (UIManager.Instance != null) {
          UIManager.EnableTouchEvents(_kCloseAnimationDisableKey);
        }
        if (_TransitionAnimation != null) {
          _TransitionAnimation.Kill();
        }
        CleanUp();
        RaiseDialogCloseAnimationFinished();
      }

      protected virtual void RaiseDialogOpened() {
        if (BaseDialogOpened != null) {
          BaseDialogOpened(this);
        }
        if (DialogOpened != null) {
          DialogOpened();
        }
      }

      protected virtual void RaiseDialogOpenAnimationFinished() {
        if (BaseDialogOpenAnimationFinished != null) {
          BaseDialogOpenAnimationFinished(this);
        }
        if (DialogOpenAnimationFinished != null) {
          DialogOpenAnimationFinished();
        }
      }

      protected virtual void RaiseDialogClosed() {
        if (BaseDialogClosed != null) {
          BaseDialogClosed(this);
        }
        if (DialogClosed != null) {
          DialogClosed();
        }
      }

      protected virtual void RaiseDialogCloseAnimationFinished() {
        if (BaseDialogCloseAnimationFinished != null) {
          BaseDialogCloseAnimationFinished(this);
        }
        if (DialogCloseAnimationFinished != null) {
          DialogCloseAnimationFinished();
        }
      }
    }
  }
}
﻿using UnityEngine;
using System.Collections;
using UnityEngine.UI;
using Anki.UI;
using System;

public class ArtistCozmoImageMenu : MonoBehaviour {

  [SerializeField]
  private RawImage _RawImage;

  [SerializeField]
  private Cozmo.UI.CozmoButton _RetryButton;

  [SerializeField]
  private Cozmo.UI.CozmoButton _SaveButton;

  public event Action OnSaveClick;

  public event Action OnRetryClick;

  private void Awake() {
    _SaveButton.onClick.AddListener(HandleSaveClick);
    _SaveButton.DASEventButtonName = "save_button";
    _RetryButton.onClick.AddListener(HandleRetryClick);
    _RetryButton.DASEventButtonName = "retry_button";
    _SaveButton.DASEventViewController = _RetryButton.DASEventViewController = "artist_cozmo_image_menu";
  }

  private void HandleSaveClick() {
    if (OnSaveClick != null) {
      OnSaveClick();
    }
  }

  private void HandleRetryClick() {
    if (OnRetryClick != null) {
      OnRetryClick();
    }
  }


  public void Initialize(Texture texture) {
    _RawImage.texture = texture;
  }


}

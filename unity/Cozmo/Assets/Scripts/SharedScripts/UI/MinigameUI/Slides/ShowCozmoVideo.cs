﻿using UnityEngine;
using UnityEngine.UI;
using Cozmo.UI;
using System.Collections;
using System.Collections.Generic;

public class ShowCozmoVideo : MonoBehaviour {

#if ENABLE_DEBUG_PANEL
  private const uint kDevSkipVideoTapCount = 3;
#endif

  public System.Action OnContinueButton;

  [SerializeField]
  private MediaPlayerCtrl _MediaPlayerCtrl;

  [SerializeField]
  private RawImage _RawImage;

  [SerializeField]
  private CozmoButton _ReplayButton;

  [SerializeField]
  private CozmoButton _ContinueButton;

  [SerializeField]
  private CozmoButton _SkipButton;

  [SerializeField, Range(0f, 1f), Tooltip("Volume level (linear scalar) to which music should fade when video plays")]
  private float _MusicDuckingLevel = 0.5f; // 0.5 volume scalar is about -6dBFS

  [SerializeField, Range(0f, 2f), Tooltip("Time in seconds it should take for music to fade down/up")]
  private float _MusicDuckingFadeTime = 1f;

  [SerializeField]
  private VideoSubtitleController _VideoSubtitleController;

  private List<VideoLocalizationPair> _VideoSubtitles;

  private string _Filename;
#if ENABLE_DEBUG_PANEL
  private uint _TapCount;
#endif

#if UNITY_EDITOR
  // The plugin only works in iOS and Android. In the editor we use Unity's video player functionality.
  private Coroutine _PlayCoroutine;
#endif

  private void Awake() {
    _ContinueButton.Initialize(HandleContinueButton, "continue_button", "show_cozmo_video");
    _ReplayButton.Initialize(HandleReplayButton, "replay_button", "show_cozmo_video");
    _SkipButton.Initialize(HandleContinueButton, "skip_button", "show_cozmo_video");
#if !UNITY_EDITOR
    // Disable the background image until the video has been loaded
    _RawImage.enabled = false;

    // Disable buttons until the video has finished
    _ReplayButton.gameObject.SetActive(false);
    _ContinueButton.gameObject.SetActive(false);

    // Register a function to play the video once it has been loaded
    _MediaPlayerCtrl.OnReady += () => {
      _RawImage.enabled = true;
      _MediaPlayerCtrl.SetVolume(Cozmo.Settings.DefaultSettingsValuesConfig.Instance.VideoVolume);
      _MediaPlayerCtrl.Play();
      DuckMusic();
    };

    // Register a function to enable the buttons once the video is finished
    _MediaPlayerCtrl.OnEnd += () => {
#if ENABLE_DEBUG_PANEL
      _TapCount = 0;
#endif
      HandleVideoFinished();
      UnDuckMusic();
    };
#endif
  }

  private void OnDestroy() {
#if UNITY_EDITOR
    if (_PlayCoroutine != null) {
      StopCoroutine(_PlayCoroutine);
      _PlayCoroutine = null;
    }
#else
    _MediaPlayerCtrl.UnLoad();
#endif
  }

  // filename is a path relative to the StreamingAssets folder
  public void PlayVideo(string filename, List<VideoLocalizationPair> subtitles) {
    _Filename = filename;
    _VideoSubtitles = subtitles;
#if UNITY_EDITOR
    _PlayCoroutine = StartCoroutine(LoadAndPlayCoroutine(filename));
#else
    _MediaPlayerCtrl.Load(filename);
#endif
    _VideoSubtitleController.PlaySubtitles(subtitles);
  }

  private void DuckMusic() {
    var volType = Anki.Cozmo.Audio.VolumeParameters.VolumeType.Music;
    float vol = Anki.Cozmo.Audio.GameAudioClient.GetVolume(volType);
    if (vol > _MusicDuckingLevel) {
      Anki.Cozmo.Audio.GameAudioClient.SetVolumeValue(volType,
                                                      _MusicDuckingLevel,
                                                      timeInMS: (int)(_MusicDuckingFadeTime * 1000),
                                                      storeValue: false);
    }
  }

  private void UnDuckMusic() {
    var volType = Anki.Cozmo.Audio.VolumeParameters.VolumeType.Music;
    float vol = Anki.Cozmo.Audio.GameAudioClient.GetVolume(volType);
    if (vol > _MusicDuckingLevel) {
      Anki.Cozmo.Audio.GameAudioClient.SetVolumeValue(volType,
                                                      vol,
                                                      timeInMS: (int)(_MusicDuckingFadeTime * 1000),
                                                      storeValue: false);
    }
  }

  private void HandleVideoFinished() {
    _ReplayButton.gameObject.SetActive(true);
    _ContinueButton.gameObject.SetActive(true);
    ShowSkipButton(false);
  }

#if ENABLE_DEBUG_PANEL
  protected void Update() {
    if (Input.GetMouseButtonDown(0)) {
      OnTapVideo();
    }
  }
#endif

  public void ShowSkipButton(bool show) {
    _SkipButton.gameObject.SetActive(show);
  }

  private void HandleContinueButton() {
    if (OnContinueButton != null) {
      OnContinueButton();
    }

    UnDuckMusic();
  }

  private void HandleReplayButton() {
    if (string.IsNullOrEmpty(_Filename)) {
      DAS.Error("ShowCozmoVideo.HandleReplayButton", "Attempting to replay empty video string");
    }
    else {
      _ReplayButton.gameObject.SetActive(false);
      _ContinueButton.gameObject.SetActive(false);
      ShowSkipButton(true);
      _MediaPlayerCtrl.SeekTo(0);
      PlayVideo(_Filename, _VideoSubtitles);
    }
  }

#if ENABLE_DEBUG_PANEL
  private void OnTapVideo() {
    if (_MediaPlayerCtrl.GetCurrentState() != MediaPlayerCtrl.MEDIAPLAYER_STATE.PLAYING) {
      return;
    }

    _TapCount++;
    if (_TapCount >= kDevSkipVideoTapCount) {
      DAS.Info("ShowCozmoVideo.OnTapVideo", "Stopping video...");
      _MediaPlayerCtrl.Stop();
      _MediaPlayerCtrl.OnEnd.Invoke();
    }
  }
#endif

#if UNITY_EDITOR
  private IEnumerator LoadAndPlayCoroutine(string filename) {
    WWW fileWWW = new WWW("file://" + Application.streamingAssetsPath + "/" + filename);

    // Wait until the video has been loaded
    while (!fileWWW.isDone) {
      yield return null;
    }

    if (fileWWW.error != null) {
      Debug.LogError(fileWWW.error);
      _PlayCoroutine = null;
      HandleVideoFinished();
      yield break;
    }

    // When there is a problem with the codec, Unity doesn't gives an error back but just
    // prints error messages in the console. This is the best way we have to know if the 
    // Unity could load the video file correctly
    if (fileWWW.GetMovieTexture().duration <= 0) {
      Debug.LogError("Couldn't load the video file " + filename);
      HandleVideoFinished();
      yield break;
    }

    while (!fileWWW.GetMovieTexture().isReadyToPlay) {
      yield return null;
    }

    // Set the texture to the RawImage's texture so the video is shown on screen and play it
    _RawImage.texture = fileWWW.GetMovieTexture();
    fileWWW.GetMovieTexture().Play();

    // duck the music while playing, then bring it back up when the movie is done
    DuckMusic();

    while (fileWWW.GetMovieTexture().isPlaying) {
      yield return null;
    }

    // HACK: manually set the background color to match the last frame 
    // to account for the video lib sometimes showing a black background or ending
    // on a frame that is not the last frame of the video.
    _RawImage.texture = null;
    _RawImage.color = new Color(0.11372549f, 0.53333333333f, 0.82352941176f);

    UnDuckMusic();

    _PlayCoroutine = null;
  }
#endif
}

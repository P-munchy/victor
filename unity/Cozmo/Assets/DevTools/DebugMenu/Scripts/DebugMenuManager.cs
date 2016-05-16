﻿using UnityEngine;
using Cozmo.HomeHub;
using System.Collections;

public class DebugMenuManager : MonoBehaviour {

  [SerializeField]
  private DebugMenuDialog _DebugMenuDialogPrefab;
  private DebugMenuDialog _DebugMenuDialogInstance;

  [SerializeField]
  private LatencyCalculator _LatencyCalculator;

  [SerializeField]
  private Canvas _DebugMenuCanvas;

  private int _LastOpenedDebugTab = 0;

  private static DebugMenuManager _Instance;

  public static DebugMenuManager Instance {
    get {
      return _Instance;
    }
    private set {
      if (_Instance == null) {
        _Instance = value;
      }    
    }
  }

  public Canvas DebugOverlayCanvas {
    get {
      return _DebugMenuCanvas;
    }
  }

  void Start() {
    // TODO: Destroy self if not production
    _Instance = this;
  }
  
  // TODO: Pragma out this code for production
  public void OnDebugMenuButtonTap() {
    CreateDebugDialog();
  }

  private GameBase GetCurrMinigame() {
    if (HomeHub.Instance != null) {
      if (HomeHub.Instance.MiniGameInstance != null) {
        return HomeHub.Instance.MiniGameInstance;
      }
    }
    return null;
  }

  private void CreateDebugDialog() {
    GameObject debugMenuDialogInstance = GameObject.Instantiate(_DebugMenuDialogPrefab.gameObject);
    Transform dialogTransform = debugMenuDialogInstance.transform;
    dialogTransform.SetParent(_DebugMenuCanvas.transform, false);
    dialogTransform.localPosition = Vector3.zero;
    _DebugMenuDialogInstance = debugMenuDialogInstance.GetComponent<DebugMenuDialog>();
    _DebugMenuDialogInstance.Initialize(_LastOpenedDebugTab);
    _DebugMenuDialogInstance.OnDebugMenuClosed += OnDebugMenuDialogClose;
  }

  private void OnDebugMenuDialogClose(int lastOpenTab) {
    _DebugMenuDialogInstance.OnDebugMenuClosed -= OnDebugMenuDialogClose;
    _LastOpenedDebugTab = lastOpenTab;
  }

  public bool IsDialogOpen() {
    return _DebugMenuDialogInstance != null;
  }

  public void EnableLatencyPopup(bool enable) {
    _LatencyCalculator.EnableLatencyPopup(enable);
  }
}

﻿using UnityEngine;
using UnityEngine.UI;
using System.Collections;

public class RobotSettingsPane : MonoBehaviour {

  [SerializeField]
  private Button _ToggleDebugStringButton;

  [SerializeField]
  private Toggle _ToggleDebugStringType;

  [SerializeField]
  private GameObject _RobotStateTextFieldPrefab;

  private void Start() {

    _ToggleDebugStringButton.onClick.AddListener(OnToggleDebugString);
    
    _ToggleDebugStringType.onValueChanged.AddListener(OnToggleDebugStringType);
  }

  private void OnToggleDebugStringType(bool check) {
    RobotStateTextField.UseAnimString(check);
  }

  private void OnToggleDebugString() {
    Canvas debug_canvas = DebugMenuManager.Instance.DebugOverlayCanvas;
    if (debug_canvas != null) {
      RobotStateTextField old_instance = debug_canvas.GetComponentInChildren<RobotStateTextField>();
      if (old_instance == null) {
        UIManager.CreateUIElement(_RobotStateTextFieldPrefab, debug_canvas.transform);
      }
      else {
        Destroy(old_instance.gameObject);
      }
    }
    
  }

}

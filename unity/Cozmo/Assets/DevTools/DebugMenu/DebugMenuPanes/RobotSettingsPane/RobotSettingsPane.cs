﻿using UnityEngine;
using UnityEngine.UI;
using System.Collections;

public class RobotSettingsPane : MonoBehaviour {

  [SerializeField]
  private Slider _VolumeSlider;

  private void Start() {
    _VolumeSlider.minValue = 0;
    _VolumeSlider.maxValue = 1;
    _VolumeSlider.value = 0.5f;
    IRobot robot = RobotEngineManager.Instance.CurrentRobot;
    if (robot != null) {
      _VolumeSlider.value = robot.GetRobotVolume();
    }
    _VolumeSlider.onValueChanged.AddListener(OnVolumeSliderChanged);
  }

  private void OnVolumeSliderChanged(float newValue) {
    IRobot robot = RobotEngineManager.Instance.CurrentRobot;
    if (robot != null) {
      robot.SetRobotVolume(_VolumeSlider.value);
    }
  }
}

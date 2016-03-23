﻿using UnityEngine;
using UnityEngine.UI;
using System.Collections;
using System.Collections.Generic;
using System.Text;
using Anki.UI;

public class ConsoleLogPane : MonoBehaviour {

  public delegate void ConsoleLogPaneOpenHandler(ConsoleLogPane consoleLogPane);

  public static event ConsoleLogPaneOpenHandler ConsoleLogPaneOpened;

  public delegate void ConsoleSOSLogButtonEnableHandler();

  public event ConsoleSOSLogButtonEnableHandler ConsoleSOSLogButtonEnable;

  private static void RaiseConsoleLogPaneOpened(ConsoleLogPane consoleLogPane) {
    if (ConsoleLogPaneOpened != null) {
      ConsoleLogPaneOpened(consoleLogPane);
    }
  }

  public delegate void ConsoleLogPaneClosedHandler();

  public event ConsoleLogPaneClosedHandler ConsoleLogPaneClosed;

  private void RaiseConsoleLogPaneClosed() {
    if (ConsoleLogPaneClosed != null) {
      ConsoleLogPaneClosed();
    }
  }

  public delegate void ConsoleLogToggleChangedHandler(LogPacket.ELogKind logKind, bool newIsOnValue);

  public event ConsoleLogToggleChangedHandler ConsoleLogToggleChanged;

  private void RaiseConsoleLogToggleChanged(LogPacket.ELogKind logKind, bool newIsOnValue) {
    if (ConsoleLogToggleChanged != null) {
      ConsoleLogToggleChanged(logKind, newIsOnValue);
    }
  }

  [SerializeField]
  private ScrollRect _TextScrollRect;

  [SerializeField]
  private RectTransform _ScrollRectLayoutTextContainer;
  
  [SerializeField]
  private ConsoleLogToggle[] _LogToggles;

  [SerializeField]
  private Button _EnableSOSButton;

  private SimpleObjectPool<AnkiTextLabel> _TextLabelPool;
  private List<AnkiTextLabel> _TextLabelsUsed;
  private AnkiTextLabel _NewestTextLabel;

  // Use this for initialization
  private void Start() {
    _TextLabelsUsed = new List<AnkiTextLabel>();

    RaiseConsoleLogPaneOpened(this);

    _EnableSOSButton.onClick.AddListener(HandleOnEnableSOSLogButton);
  }

  private void OnDestroy() {
    // Return all the labels to the pool
    ReturnLabelsToPool();

    RaiseConsoleLogPaneClosed();
  }

  private void HandleOnEnableSOSLogButton() {
    if (ConsoleSOSLogButtonEnable != null) {
      ConsoleSOSLogButtonEnable();
    }
  }

  public void Initialize(List<string> consoleText, SimpleObjectPool<AnkiTextLabel> textLabelPool) {
    _TextLabelPool = textLabelPool;
    CreateLabelsForText(consoleText);

    // Scroll to the bottom
    _TextScrollRect.verticalNormalizedPosition = 1;
  }

  public void SetText(List<string> consoleText) {
    bool wasAtBottom = (_TextScrollRect.verticalNormalizedPosition >= 1);

    // Return all the text labels to the pool
    ReturnLabelsToPool();

    // Create a text label for each string
    CreateLabelsForText(consoleText);

    if (wasAtBottom) {
      _TextScrollRect.verticalNormalizedPosition = 1;
    }
  }

  public void AppendLog(string newLog) {
    bool wasAtBottom = (_TextScrollRect.verticalNormalizedPosition >= 1);

    // If there is space, add the new log to the current newest text field
    if (_NewestTextLabel != null &&
        newLog.Length + 1 + _NewestTextLabel.text.Length < ConsoleLogManager.kUnityTextFieldCharLimit) {
      StringBuilder sb = new StringBuilder();
      sb.Append(_NewestTextLabel.text);
      sb.Append(newLog);
      sb.Append("\n");
      _NewestTextLabel.text = sb.ToString();
    }
    else {
      // If we are out of space, create a new text label and add the log there
      _NewestTextLabel = CreateNewTextLabel(newLog + "\n");
    }

    if (wasAtBottom) {
      _TextScrollRect.verticalNormalizedPosition = 1;
    }
  }

  private void ReturnLabelsToPool() {
    foreach (AnkiTextLabel label in _TextLabelsUsed) {
      _TextLabelPool.ReturnToPool(label);
    }
    _TextLabelsUsed.Clear();
    _NewestTextLabel = null;
  }

  private void CreateLabelsForText(List<string> consoleText) {
    // Create a text label for each string
    AnkiTextLabel newTextLabel = null;
    foreach (string logChunk in consoleText) {
      newTextLabel = CreateNewTextLabel(logChunk);
    }

    // Set the last label as the newest label
    _NewestTextLabel = newTextLabel;
  }

  private AnkiTextLabel CreateNewTextLabel(string labelText) {
    AnkiTextLabel newTextLabel = _TextLabelPool.GetObjectFromPool();
    newTextLabel.text = labelText;
    newTextLabel.gameObject.transform.SetParent(_ScrollRectLayoutTextContainer, false);
    newTextLabel.gameObject.SetActive(true);
    _TextLabelsUsed.Add(newTextLabel);
    return newTextLabel;
  }

  #region Toggles

  public void SetToggle(LogPacket.ELogKind logKind, bool isOn) {
    foreach (ConsoleLogToggle logToggle in _LogToggles) {
      if (logToggle.logKind == logKind) {
        logToggle.toggle.isOn = isOn;
      }
    }
  }

  public void OnInfoToggleChanged(bool newValue) {
    RaiseConsoleLogToggleChanged(LogPacket.ELogKind.Info, newValue);
  }

  public void OnWarningToggleChanged(bool newValue) {
    RaiseConsoleLogToggleChanged(LogPacket.ELogKind.Warning, newValue);
  }

  public void OnErrorToggleChanged(bool newValue) {
    RaiseConsoleLogToggleChanged(LogPacket.ELogKind.Error, newValue);
  }

  public void OnEventToggleChanged(bool newValue) {
    RaiseConsoleLogToggleChanged(LogPacket.ELogKind.Event, newValue);
  }

  public void OnDebugToggleChanged(bool newValue) {
    RaiseConsoleLogToggleChanged(LogPacket.ELogKind.Debug, newValue);
  }

  #endregion
}

[System.Serializable]
public class ConsoleLogToggle {
  public LogPacket.ELogKind logKind;
  public Toggle toggle;
}
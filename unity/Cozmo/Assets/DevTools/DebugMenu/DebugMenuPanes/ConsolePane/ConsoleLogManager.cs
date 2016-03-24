﻿using UnityEngine;
using System.Collections;
using System.Collections.Generic;
using System.Text;
using System.Linq;
using Anki.UI;

public class ConsoleLogManager : MonoBehaviour, IDASTarget {

  // Each string element should be < 16250 characters because
  // Unity uses a mesh to display text, 4 verts per letter, and has
  // a hard limit of 65000 verts per mesh
  public const int kUnityTextFieldCharLimit = 14000;

  [SerializeField]
  private AnkiTextLabel _ConsoleTextLabelPrefab;

  private SimpleObjectPool<AnkiTextLabel> _TextLabelPool;

  [SerializeField]
  private int numberCachedLogMaximum = 100;

  private Queue<LogPacket> _MostRecentLogs;

  private ConsoleLogPane _ConsoleLogPaneView;

  private Dictionary<LogPacket.ELogKind, bool> _LastToggleValues;

  // Use this for initialization
  private void Awake() {
    _TextLabelPool = new SimpleObjectPool<AnkiTextLabel>(CreateTextLabel, ResetTextLabel, 3);
    _MostRecentLogs = new Queue<LogPacket>();
    _ConsoleLogPaneView = null;

    _LastToggleValues = new Dictionary<LogPacket.ELogKind, bool>();
    _LastToggleValues.Add(LogPacket.ELogKind.Info, true);
    _LastToggleValues.Add(LogPacket.ELogKind.Warning, true);
    _LastToggleValues.Add(LogPacket.ELogKind.Error, true);
    _LastToggleValues.Add(LogPacket.ELogKind.Event, true);
    _LastToggleValues.Add(LogPacket.ELogKind.Debug, true);

    DAS.AddTarget(this);

    ConsoleLogPane.ConsoleLogPaneOpened += OnConsoleLogPaneOpened;

  }

  private void EnableSOSLogs() {
    SOSLogManager.Instance.CreateListener();
    RobotEngineManager.Instance.CurrentRobot.SetEnableSOSLogging(true);
    SOSLogManager.Instance.RegisterListener(HandleNewSOSLog);
  }

  private void HandleNewSOSLog(string log) {
    if (log.Contains("[Warn]")) {
      SaveLogPacket(LogPacket.ELogKind.Warning, "", log, null);
    }
    else if (log.Contains("[Error]")) {
      SaveLogPacket(LogPacket.ELogKind.Error, "", log, null);
    }
    else if (log.Contains("[Debug]")) {
      SaveLogPacket(LogPacket.ELogKind.Debug, "", log, null);
    }
    else if (log.Contains("[Info]")) {
      SaveLogPacket(LogPacket.ELogKind.Info, "", log, null);
    }
    else if (log.Contains("[Event]")) {
      SaveLogPacket(LogPacket.ELogKind.Event, "", log, null);
    }
    else {
      SaveLogPacket(LogPacket.ELogKind.Debug, "", log, null);
    }
  }

  private void OnDestroy() {
    DAS.RemoveTarget(this);
    ConsoleLogPane.ConsoleLogPaneOpened -= OnConsoleLogPaneOpened;

    if (_ConsoleLogPaneView != null) {
      _ConsoleLogPaneView.ConsoleLogToggleChanged -= OnConsoleToggleChanged;
    }
  }

  void IDASTarget.Info(string eventName, string eventValue, object context) {
    SaveLogPacket(LogPacket.ELogKind.Info, eventName, eventValue, context);
  }

  void IDASTarget.Error(string eventName, string eventValue, object context) {
    SaveLogPacket(LogPacket.ELogKind.Error, eventName, eventValue, context);
  }

  void IDASTarget.Warn(string eventName, string eventValue, object context) {
    SaveLogPacket(LogPacket.ELogKind.Warning, eventName, eventValue, context);
  }

  void IDASTarget.Event(string eventName, string eventValue, object context) {
    SaveLogPacket(LogPacket.ELogKind.Event, eventName, eventValue, context);
  }

  void IDASTarget.Debug(string eventName, string eventValue, object context) {
    SaveLogPacket(LogPacket.ELogKind.Debug, eventName, eventValue, context);
  }

  private void SaveLogPacket(LogPacket.ELogKind logKind, string eventName, string eventValue, object context) {
    // Add the new log to the queue
    LogPacket newPacket = new LogPacket(logKind, eventName, eventValue, context);
    _MostRecentLogs.Enqueue(newPacket);
    while (_MostRecentLogs.Count > numberCachedLogMaximum) {
      _MostRecentLogs.Dequeue();
    }
    
    // Update the UI, if it is open
    if (_ConsoleLogPaneView != null) {
      // TODO: Only add the new log to the UI if the filter is toggled on
      if (_LastToggleValues[newPacket.LogKind] == true) {
        _ConsoleLogPaneView.AppendLog(newPacket.ToString());
      }
    }
  }

  private void OnConsoleLogPaneOpened(ConsoleLogPane logPane) {
    _ConsoleLogPaneView = logPane;
    _ConsoleLogPaneView.ConsoleSOSLogButtonEnable += EnableSOSLogs;

    List<string> consoleText = CompileRecentLogs();
    _ConsoleLogPaneView.Initialize(consoleText, _TextLabelPool);
    foreach (KeyValuePair<LogPacket.ELogKind, bool> kvp in _LastToggleValues) {
      _ConsoleLogPaneView.SetToggle(kvp.Key, kvp.Value);
    }
    _ConsoleLogPaneView.ConsoleLogToggleChanged += OnConsoleToggleChanged;
  }

  private void OnConsoleToggleChanged(LogPacket.ELogKind logKind, bool newValue) {
    // IVY: For some reason the newValue is always false - bug on unity's end?
    // lastToggleValues_[logKind] = newValue;
    _LastToggleValues[logKind] = !_LastToggleValues[logKind];

    // Change the text for the pane
    List<string> consoleText = CompileRecentLogs();
    _ConsoleLogPaneView.SetText(consoleText);
  }

  private void OnConsoleLogPaneClosed() {
    _ConsoleLogPaneView.ConsoleLogToggleChanged -= OnConsoleToggleChanged;
    _ConsoleLogPaneView = null;
  }

  private List<string> CompileRecentLogs() {
    List<string> consoleLogs = new List<string>();
    StringBuilder sb = new StringBuilder();
    string logString;
    foreach (LogPacket packet in _MostRecentLogs) {
      if (_LastToggleValues[packet.LogKind] == true) {
        logString = packet.ToString();

        if (sb.Length + logString.Length + 1 > kUnityTextFieldCharLimit) {
          consoleLogs.Add(sb.ToString());

          // Empty the string builder
          sb.Length = 0;
        }

        sb.Append(packet.ToString());
        sb.Append("\n");
      }
    }

    if (sb.Length > 0) {
      consoleLogs.Add(sb.ToString());
    }
    return consoleLogs;
  }

  #region Text Label Pooling

  private AnkiTextLabel CreateTextLabel() {
    // Create the text label as a child under the parent container for the pool
    GameObject newLabelObject = UIManager.CreateUIElement(_ConsoleTextLabelPrefab.gameObject, this.transform);
    newLabelObject.SetActive(false);
    AnkiTextLabel textScript = newLabelObject.GetComponent<AnkiTextLabel>();

    return textScript;
  }

  private void ResetTextLabel(AnkiTextLabel toReset, bool spawned) {
    if (!spawned) {
      // Add the text label as a child under the parent container for the pool
      toReset.transform.SetParent(this.transform, true);
      toReset.text = null;
      toReset.gameObject.SetActive(false);
    }
  }

  #endregion
}

public class LogPacket {
  public enum ELogKind {
    Info,
    Warning,
    Error,
    Debug,
    Event
  }

  public ELogKind LogKind {
    get;
    private set;
  }

  public string EventName {
    get;
    private set;
  }

  public string EventValue {
    get;
    private set;
  }

  public object Context {
    get;
    private set;
  }

  public LogPacket(ELogKind logKind, string eventName, string eventValue, object context) {
    LogKind = logKind;
    EventName = eventName;
    EventValue = eventValue;
    Context = context;
  }

  public override string ToString() {
    string logKindStr = "";
    string colorStr = "";
    switch (LogKind) {
    case ELogKind.Info:
      logKindStr = "INFO";
      colorStr = "ffffff";
      break;
    case ELogKind.Warning:
      logKindStr = "WARN";
      colorStr = "ffcc00";
      break;
    case ELogKind.Error:
      logKindStr = "ERROR";
      colorStr = "ff0000";
      break;
    case ELogKind.Event:
      logKindStr = "EVENT";
      colorStr = "0099ff";
      break;
    case ELogKind.Debug:
      logKindStr = "DEBUG";
      colorStr = "00cc00";
      break;
    }
    
    string contextStr = "";
    if (Context != null) {
      Dictionary<string, string> contextDict = Context as Dictionary<string, string>;
      if (contextDict != null) {
        contextStr = string.Join(", ", contextDict.Select(kvp => kvp.Key + "=" + kvp.Value).ToArray());
      }
      else {
        contextStr = Context.ToString();
      }
    }
    // TODO: Colorize the text
    return string.Format("<color=#{0}>[{1}] {2}: {3} ({4})</color>", colorStr, logKindStr, EventName, EventValue, contextStr);
  }
}
﻿using UnityEngine;
using System.Collections;
using System.Collections.Generic;
using System.Text;
using System.Linq;
using Anki.UI;

public class ConsoleLogManager : MonoBehaviour, IDASTarget {


  private static ConsoleLogManager _Instance;

  public static ConsoleLogManager Instance {
    get {
      if (_Instance == null) {
        DAS.Error("ConsoleLogManager.NullInstance", "Don't access ConsoleLogManager until Start!");
      }
      return _Instance;
    }
    private set {
      if (_Instance != null) {
        DAS.Error("ConsoleLogManager.DuplicateInstance", "ConsoleLogManager already exists");
      }
      _Instance = value;
    }
  }


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
  private Queue<LogPacket> _ReceivedPackets;

  private ConsoleLogPane _ConsoleLogPaneView;

  private Dictionary<LogPacket.ELogKind, bool> _LastToggleValues;

  private bool _SOSLoggingEnabled = false;

  // Use this for initialization
  private void Awake() {
    Instance = this;

    _TextLabelPool = new SimpleObjectPool<AnkiTextLabel>(CreateTextLabel, ResetTextLabel, 3);
    _MostRecentLogs = new Queue<LogPacket>();
    _ReceivedPackets = new Queue<LogPacket>();
    _ConsoleLogPaneView = null;

    _LastToggleValues = new Dictionary<LogPacket.ELogKind, bool>();
    _LastToggleValues.Add(LogPacket.ELogKind.Info, true);
    _LastToggleValues.Add(LogPacket.ELogKind.Warning, true);
    _LastToggleValues.Add(LogPacket.ELogKind.Error, true);
    _LastToggleValues.Add(LogPacket.ELogKind.Event, true);
    _LastToggleValues.Add(LogPacket.ELogKind.Debug, true);
    _LastToggleValues.Add(LogPacket.ELogKind.Global, true);

    DAS.AddTarget(this);

    ConsoleLogPane.ConsoleLogPaneOpened += OnConsoleLogPaneOpened;

  }

  private void Update() {
    lock (_ReceivedPackets) {
      while (_ReceivedPackets.Count > 0) {
        LogPacket newPacket;

        // Packets could be trying to be saved from another thread while we are processing them here so we have to lock
        newPacket = _ReceivedPackets.Dequeue();

        _MostRecentLogs.Enqueue(newPacket);
        while (_MostRecentLogs.Count > numberCachedLogMaximum) {
          _MostRecentLogs.Dequeue();
        }

        // Update the UI, if it is open
        if ((_ConsoleLogPaneView != null) && (_LastToggleValues[newPacket.LogKind])) {
          _ConsoleLogPaneView.AppendLog(newPacket.ToString());
        }
      }
    }
  }

  public void EnableSOSLogs(bool enable) {

    DataPersistence.DataPersistenceManager.Instance.Data.DebugPrefs.SOSLoggerEnabled = enable;
    DataPersistence.DataPersistenceManager.Instance.Save();

    if (!_SOSLoggingEnabled && enable) {
      _SOSLoggingEnabled = enable;
      SOSLogManager.Instance.CreateListener();
      RobotEngineManager.Instance.CurrentRobot.SetEnableSOSLogging(true);
      SOSLogManager.Instance.RegisterListener(HandleNewSOSLog);
    }
    else if (_SOSLoggingEnabled && !enable) {
      _SOSLoggingEnabled = false;
      SOSLogManager.Instance.CleanUp();
    }
  }

  private void HandleNewSOSLog(string log) {
    if (log.Contains("[Warn]")) {
      SaveLogPacket(LogPacket.ELogKind.Warning, "", log, null, null);
    }
    else if (log.Contains("[Error]")) {
      SaveLogPacket(LogPacket.ELogKind.Error, "", log, null, null);
    }
    else if (log.Contains("[Debug]")) {
      SaveLogPacket(LogPacket.ELogKind.Debug, "", log, null, null);
    }
    else if (log.Contains("[Info]")) {
      SaveLogPacket(LogPacket.ELogKind.Info, "", log, null, null);
    }
    else if (log.Contains("[Event]")) {
      SaveLogPacket(LogPacket.ELogKind.Event, "", log, null, null);
    }
    else if (log.Contains("[Global]")) {
      SaveLogPacket(LogPacket.ELogKind.Global, "", log, null, null);
    }
    else {
      SaveLogPacket(LogPacket.ELogKind.Debug, "", log, null, null);
      Debug.LogWarning("Malformed Log Detected");
    }
  }

  private void OnDestroy() {
    DAS.RemoveTarget(this);
    ConsoleLogPane.ConsoleLogPaneOpened -= OnConsoleLogPaneOpened;

    if (_ConsoleLogPaneView != null) {
      _ConsoleLogPaneView.ConsoleLogToggleChanged -= OnConsoleToggleChanged;
    }
  }

  void IDASTarget.Info(string eventName, string eventValue, object context, Dictionary<string, string> keyValues) {
    SaveLogPacket(LogPacket.ELogKind.Info, eventName, eventValue, context, keyValues);
  }

  void IDASTarget.Error(string eventName, string eventValue, object context, Dictionary<string, string> keyValues) {
    SaveLogPacket(LogPacket.ELogKind.Error, eventName, eventValue, context, keyValues);
  }

  void IDASTarget.Warn(string eventName, string eventValue, object context, Dictionary<string, string> keyValues) {
    SaveLogPacket(LogPacket.ELogKind.Warning, eventName, eventValue, context, keyValues);
  }

  void IDASTarget.Event(string eventName, string eventValue, object context, Dictionary<string, string> keyValues) {
    SaveLogPacket(LogPacket.ELogKind.Event, eventName, eventValue, context, keyValues);
  }

  void IDASTarget.Debug(string eventName, string eventValue, object context, Dictionary<string, string> keyValues) {
    SaveLogPacket(LogPacket.ELogKind.Debug, eventName, eventValue, context, keyValues);
  }

  void IDASTarget.SetGlobal(string eventName, string eventValue) {
    SaveLogPacket(LogPacket.ELogKind.Global, eventName, eventValue, null, null);
  }

  private void SaveLogPacket(LogPacket.ELogKind logKind, string eventName, string eventValue, object context, Dictionary<string, string> keyValues) {
    LogPacket newPacket = new LogPacket(logKind, eventName, eventValue, context, keyValues);

    // This can be called from multiple threads while the main one is processing the received packets so we have to lock
    lock (_ReceivedPackets) {
      _ReceivedPackets.Enqueue(newPacket);
    }
  }

  private void OnConsoleLogPaneOpened(ConsoleLogPane logPane) {
    _ConsoleLogPaneView = logPane;
    _ConsoleLogPaneView.ConsoleSOSToggle += EnableSOSLogs;
    _ConsoleLogPaneView.ConsoleLogCopyToClipboard += CopyLogsToClipboard;

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

  private void CopyLogsToClipboard() {
    List<string> logDb = CompileRecentLogs();
    string logFull = "";
    for (int i = 0; i < logDb.Count; ++i) {
      logFull += logDb[i] + "\n";
    }
    CozmoBinding.SendToClipboard(logFull);
    GUIUtility.systemCopyBuffer = logFull;
  }

  private void OnConsoleLogPaneClosed() {
    _ConsoleLogPaneView.ConsoleSOSToggle -= EnableSOSLogs;
    _ConsoleLogPaneView.ConsoleLogCopyToClipboard -= CopyLogsToClipboard;
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
      toReset.transform.SetParent(this.transform, false);
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
    Event,
    Global
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

  public Dictionary<string, string> KeyValues {
    get;
    private set;
  }

  public LogPacket(ELogKind logKind, string eventName, string eventValue, object context, Dictionary<string, string> keyValue) {
    LogKind = logKind;
    EventName = eventName;
    EventValue = eventValue;
    Context = context;
    KeyValues = keyValue;
  }

  public override string ToString() {
    string logKindStr = "";
    string colorStr = "";
    switch (LogKind) {
    case ELogKind.Global:
      logKindStr = "GLOBAL";
      colorStr = "ff00cc";
      break;
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

    string keyValuesStr = "";
    if (KeyValues != null) {
      keyValuesStr = string.Join(", ", KeyValues.Select(kvp => kvp.Key + "=" + kvp.Value).ToArray());
    }

    StringBuilder formatStr = new StringBuilder("<color=#{0}>[{1}] {2}: {3}"); 
    if (!string.IsNullOrEmpty(contextStr)) {
      formatStr.Append(" ({4})");
    }

    if (!string.IsNullOrEmpty(keyValuesStr)) {
      formatStr.Append(" ({5})");
    }
    formatStr.Append("</color>");

    return string.Format(formatStr.ToString(), colorStr, logKindStr, EventName, EventValue, contextStr, keyValuesStr);
  }
}
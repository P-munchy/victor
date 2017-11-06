﻿using System;
using System.Collections.Generic;

public class DasTracker {

  private DateTime _LastBackgroundUtcTime;
  private DateTime _LastResumeUtcTime;
  private DateTime _LastTimerUpdateUtcTime;
  private DateTime _CurrentSessionStartUtcTime;
  private DateTime _CurrentRobotStartUtcTime;
  private DateTime _AppStartupUtcTime;
  private DateTime _ConnectFlowStartUtcTime;
  private DateTime _WifiFlowStartUtcTime;
  private bool _FirstTimeConnectActive;
  private bool _ConnectSessionIsFirstTime;
  private double _RunningApprunTime;
  private double _RunningSessionTime;
  private double? _RunningRobotTime = null;

  public DasTracker() {
    _LastBackgroundUtcTime = DateTime.UtcNow;
  }

  public void TrackAppBackgrounded() {
    UpdateRunningTimers();
    _LastBackgroundUtcTime = DateTime.UtcNow;

    // app.entered_background - no extra data
    DAS.Event("app.entered_background", "");

    HandleSessionEnd();
  }

  public void TrackAppResumed() {
    _LastResumeUtcTime = DateTime.UtcNow;
    _LastTimerUpdateUtcTime = DateTime.UtcNow;
    {
      // app.became_active - include timestamp of last backgrounding (ms since epoch)
      var epoch = new DateTime(1970, 1, 1, 0, 0, 0, DateTimeKind.Utc);
      DAS.Event("app.became_active", Convert.ToUInt64((_LastBackgroundUtcTime - epoch).TotalMilliseconds).ToString());
    }

    HandleSessionStart();

    if ((DateTime.UtcNow - _LastBackgroundUtcTime).TotalMinutes < 10.0) {
      // app.session.resume - if backgrounding lasted <10 minutes
      DAS.Event("app.session.resume", "");
    }
  }

  public void TrackAppStartup() {
    _AppStartupUtcTime = DateTime.UtcNow;
    _LastTimerUpdateUtcTime = DateTime.UtcNow;
    HandleSessionStart();
  }

  public void TrackAppQuit() {
    UpdateRunningTimers();
    if (_LastBackgroundUtcTime > _AppStartupUtcTime && _LastBackgroundUtcTime > _LastResumeUtcTime) {
      // if app has been backgrounded more recently than resumed, session has already ended
      HandleSessionEnd();
    }

    if (_RunningRobotTime != null) {
      // cozmo_engine.robot_connection_manager.disconnect_reason - reason, along with battery voltage/percent
      var eventName = "cozmo_engine.robot_connection_manager.disconnect_reason";
      var reason = Anki.Cozmo.RobotDisconnectReason.AppTerminated.ToString();
      var robot = RobotEngineManager.Instance.CurrentRobot;
      var batteryVoltage = robot != null ? robot.BatteryVoltage : 0.0f;
      var batteryPercent = robot != null ? robot.BatteryPercent(batteryVoltage) : 0.0f;
      var dataString = batteryVoltage.ToString("n2") + "," + batteryPercent.ToString("n2");
      DAS.Event(eventName, reason, new Dictionary<string, string> { { "$data", dataString } });
    }

    // app.terminated - no extra data
    DAS.Event("app.terminated", "");
    // app.apprun.length - time spent in app not including backgrounding
    DAS.Event("app.apprun.length", Convert.ToUInt32(_RunningApprunTime).ToString());
  }

  public void TrackRobotConnected() {
    UpdateRunningTimers(); // otherwise pre-connect time will get included in running robot timer
    _RunningRobotTime = 0.0;
    _CurrentRobotStartUtcTime = DateTime.UtcNow;

    // app.connected_session.start - notification order id of clicked notif, if any - message key of clicked notif, if any
    Cozmo.Notifications.Notification clickedNotif = Cozmo.Notifications.NotificationsManager.Instance.GetNotificationClickedForSession();
    if (clickedNotif != null) {
      var dataDict = GetDataDictionary("$data", clickedNotif.TextKey);
      DAS.Event("app.connected_session.start", clickedNotif.OrderId, dataDict);
    }
    else {
      DAS.Event("app.connected_session.start", "");
    }
    Cozmo.Notifications.NotificationsManager.Instance.CreditNotificationForConnect();
  }

  public void TrackRobotDisconnected(byte robotId) {
    if (_RunningRobotTime == null) {
      return;
    }
    UpdateRunningTimers();

    {
      uint totalConnectedTime = Convert.ToUInt32((DateTime.UtcNow - _CurrentRobotStartUtcTime).TotalSeconds);
      var dataDict = GetDataDictionary("$data", totalConnectedTime.ToString());

      // app.connected_session.end - include:
      //   - time spent in session (w/o backgrounding)
      //   - $data = time spent in session including backgrounding
      DAS.Event("app.connected_session.end", Convert.ToUInt32(_RunningRobotTime).ToString(), dataDict);
    }
    {
      var robot = RobotEngineManager.Instance.CurrentRobot;
      if (robot.ID == robotId) {
        var dataDict = GetDataDictionary("$phys", robot.SerialNumber.ToString("X8"));

        // app.connection_lost - included:
        //   - reason for connection loss
        //   - $phys
        DAS.Event("app.connection_lost", "disconnected", dataDict);
      }
    }

    _RunningRobotTime = null;
  }

  public void TrackFirstTimeConnectStarted() {
    _FirstTimeConnectActive = true;
  }

  public void TrackFirstTimeConnectEnded() {
    _FirstTimeConnectActive = false;
  }

  public void TrackConnectFlowStarted() {
    // app.connect.start - 0/1 if in out of box experience
    DAS.Event("app.connect.start", _FirstTimeConnectActive ? "1" : "0");

    _ConnectFlowStartUtcTime = DateTime.UtcNow;
    _ConnectSessionIsFirstTime = _FirstTimeConnectActive; // in case this is reset before OnFlowEnded()
  }

  public void TrackConnectFlowEnded() {
    // we're leaving connect flow - do we have a robot connection active?
    if (_RunningRobotTime != null) {
      HandleConnectSuccess();
    }

    {
      var dataDict = GetDataDictionary("$data", _ConnectSessionIsFirstTime ? "1" : "0");
      uint secondsInFlow = Convert.ToUInt32((DateTime.UtcNow - _ConnectFlowStartUtcTime).TotalSeconds);

      // app.connect.exit - include:
      //   - total time in connect flow (seconds)
      //   - 0/1 if in out of box experience
      DAS.Event("app.connect.exit", secondsInFlow.ToString(), dataDict);
    }
  }

  public void TrackSearchForCozmoFailed() {
    if (_ConnectFlowStartUtcTime.Equals(new DateTime())) {
      return;
    }
    var dataDict = GetDataDictionary("$data", _ConnectSessionIsFirstTime ? "1" : "0");
    uint secondsInFlow = Convert.ToUInt32((DateTime.UtcNow - _ConnectFlowStartUtcTime).TotalSeconds);

    // app.connect.fail - include:
    //   - total time in connect flow (seconds)
    //   - 0/1 if in out of box experience
    DAS.Event("app.connect.fail", secondsInFlow.ToString(), dataDict);
  }

  public void TrackWifiInstructionsStarted() {
    // app.connect.wifi.start - 0/1 if in out of box experience
    DAS.Event("app.connect.wifi.start", _ConnectSessionIsFirstTime ? "1" : "0");

    _WifiFlowStartUtcTime = DateTime.UtcNow;
  }

  public void TrackWifiInstructionsEnded() {
    var dataDict = GetDataDictionary("$data", _ConnectSessionIsFirstTime ? "1" : "0");
    uint secondsInFlow = Convert.ToUInt32((DateTime.UtcNow - _WifiFlowStartUtcTime).TotalSeconds);

    // app.connect.wifi.complete - include:
    //   - total time in wifi setup portion of flow
    //   - 0/1 if in out of box experience
    DAS.Event("app.connect.wifi.complete", secondsInFlow.ToString(), dataDict);
  }

  public void TrackWifiInstructionsGetHelp() {
    // app.connect.wifi.get_help - no extra data
    DAS.Event("app.connect.wifi.get_help", "");
  }

  public void TrackChargerPromptEntered() {
    // app.connect.charger_prompt - no extra data
    DAS.Event("app.connect.charger_prompt", "");
  }

  public void TrackChargerPromptConnect() {
    // app.connect.charger_prompt.connect - no extra data
    DAS.Event("app.connect.charger_prompt.connect", "");
  }

  public void TrackBirthDateEntered(DateTime date) {
    // DateTime doesn't give us years so we have to do this instead, so dumb
    // stackoverflow suggested something like this
    date = date.AddYears(1);
    var now = System.DateTime.Now;
    int age = 0;
    while (date < now && age < 200) {
      date = date.AddYears(1);
      age++;
    }

    // app.age_gate.age - entered age
    DAS.Event("app.age_gate.age", age.ToString());
  }

  public void TrackBirthDateSkipped() {
    // .age_gate.age - no extra data
    DAS.Event("app.age_gate.skip", "");
  }

  public void TrackCubePromptEntered() {
    // app.connect.cubes_prompt - no extra data
    DAS.Event("app.connect.cubes_prompt", "");
  }

  public void TrackIntroManagerRobotDisconnect(string viewName) {
    if (string.IsNullOrEmpty(viewName)) {
      return;
    }

    // app.connect.abort_to_title - view aborted from
    DAS.Event("app.connect.abort_to_title", viewName);
  }

  public void TrackDifficultyUnlocked(string gameId, int difficultyLevel) {
    // game.end.unlock - unlock name (game name + difficulty)
    DAS.Event("game.end.unlock", gameId + "_" + difficultyLevel);
  }

  private HashSet<int> _SeenObjects = new HashSet<int>();
  private bool _InGame = false;
  public void TrackGameStarted() {
    _InGame = true;
  }

  public void TrackGameEnded() {
    _InGame = false;
    _SeenObjects.Clear();
  }

  public void TrackObjectObserved(Anki.Cozmo.ExternalInterface.RobotObservedObject message) {
    if (!_InGame || _SeenObjects.Contains(message.objectID)) {
      return;
    }

    _SeenObjects.Add(message.objectID);
    DAS.Event("game.object_recognized", message.objectType.ToString());
  }

  private void HandleConnectSuccess() {
    // connect flow ends when connection is successfully made
    var dataDict = GetDataDictionary("$data", _ConnectSessionIsFirstTime ? "1" : "0");
    uint secondsInFlow = Convert.ToUInt32((DateTime.UtcNow - _ConnectFlowStartUtcTime).TotalSeconds);

    // app.connect.success - include:
    //   - total time in connect flow (seconds)
    //   - 0/1 if in out of box experience
    DAS.Event("app.connect.success", secondsInFlow.ToString(), dataDict);
  }

  private void HandleSessionStart() {
    // app.session.start - no extra data
    DAS.Event("app.session.start", "");

    _CurrentSessionStartUtcTime = DateTime.UtcNow;
    _RunningSessionTime = 0.0;
  }

  private void HandleSessionEnd() {
    UpdateRunningTimers();
    uint sessionSeconds = Convert.ToUInt32((DateTime.UtcNow - _CurrentSessionStartUtcTime).TotalSeconds);
    var dataDict = GetDataDictionary("$data", sessionSeconds.ToString());

    // app.session.end - include:
    //   - time spent in session (w/o backgrounding)
    //   - $data = time spent in session including backgrounding
    DAS.Event("app.session.end", Convert.ToUInt32(_RunningSessionTime).ToString(), dataDict);

    _RunningSessionTime = 0.0;
  }

  private void UpdateRunningTimers() {
    var now = DateTime.UtcNow;
    double secondsSinceLastUpdate = (now - _LastTimerUpdateUtcTime).TotalSeconds;
    _LastTimerUpdateUtcTime = now; // prevents potential double counting

    _RunningApprunTime += secondsSinceLastUpdate;
    _RunningSessionTime += secondsSinceLastUpdate;
    if (_RunningRobotTime != null) {
      _RunningRobotTime += secondsSinceLastUpdate;
    }
  }

  public static Dictionary<string, string> GetDataDictionary(params string[] list) {
    var ret = new Dictionary<string, string>();
    if (list.Length % 2 != 0) {
      throw new ArgumentOutOfRangeException("count", "must send pairs of values");
    }
    for (int i = 0; i + 1 < list.Length; i += 2) {
      ret.Add(list[i], list[i + 1]);
    }
    return ret;
  }

  private static DasTracker _Instance;
  public static DasTracker Instance {
    get {
      if (_Instance == null) {
        _Instance = new DasTracker();
      }
      return _Instance;
    }
  }
}

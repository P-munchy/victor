using UnityEngine;
using Cozmo.UI;
using System;
using DataPersistence;
using Cozmo.ConnectionFlow;
using Cozmo.Notifications;

namespace Cozmo {
  public class PauseManager : MonoBehaviour {

#if ENABLE_DEBUG_PANEL
    public void FakeBatteryAlert() {
      if (!ListeningForBatteryLevel) {
        DAS.Warn("robot.low_battery", "blocked bad fake battery alert");
        return;
      }
      if (!sLowBatteryEventLogged) {
        sLowBatteryEventLogged = true;
        DAS.Event("robot.low_battery", "");
      }
      OpenLowBatteryAlert();
    }
#endif

    private static PauseManager _Instance;
    public Action OnPauseDialogOpen;
    public Action<bool> OnPauseStateChanged;
    public Action OnCozmoSleepCancelled;
    private bool _IsPaused = false;
    private bool _ClosedMinigameOnPause = false;
    private BaseModal _GoToSleepDialog = null;
    private bool _EngineTriggeredSleep = false;
    private bool _StartedIdleTimeout = false;
    private bool _IdleTimeOutEnabled = true;
    private float _ShouldPlayWakeupTimestamp = -1;

    private const float _kMaxValidBatteryVoltage = 4.2f;
    private float _LowPassFilteredVoltage = _kMaxValidBatteryVoltage;
    private bool _LowBatteryAlertTriggered = false;
    private AlertModal _LowBatteryAlertInstance = null;
    public bool ListeningForBatteryLevel = false;
    public static bool sLowBatteryEventLogged = false;

    private AlertModal _SleepCozmoConfirmDialog;

    public bool IsConfirmSleepDialogOpen { get { return (null != _SleepCozmoConfirmDialog); } }
    public bool IsGoToSleepDialogOpen { get { return (null != _GoToSleepDialog); } }
    public bool IsLowBatteryDialogOpen { get { return (null != _LowBatteryAlertInstance); } }
    public bool IsIdleTimeOutEnabled {
      get { return _IdleTimeOutEnabled; }
      set { _IdleTimeOutEnabled = value; }
    }
    public bool ExitChallengeOnPause { get; set; }
    public bool AllowFreeplayOnResume { get; set; }

    [SerializeField]
    private AlertModal _LowBatteryAlertPrefab;

    // Does singleton instance exist?
    public static bool InstanceExists {
      get {
        return (_Instance != null);
      }
    }

    public static PauseManager Instance {
      get {
        if (_Instance == null) {
          string stackTrace = System.Environment.StackTrace;
          DAS.Error("PauseManager.NullInstance", "Do not access PauseManager until start");
          DAS.Debug("PauseManager.NullInstance.StackTrace", DASUtil.FormatStackTrace(stackTrace));
          HockeyApp.ReportStackTrace("PauseManager.NullInstance", stackTrace);
        }
        return _Instance;
      }
      private set {
        if (_Instance != null) {
          DAS.Error("PauseManager.DuplicateInstance", "PauseManager Instance already exists");
        }
        _Instance = value;
      }
    }

    private Cozmo.Settings.DefaultSettingsValuesConfig Settings { get { return Cozmo.Settings.DefaultSettingsValuesConfig.Instance; } }

    private void Awake() {
      Instance = this;
      Cozmo.Notifications.NotificationsManager.Instance.Init();
    }

    private void Start() {
      RobotEngineManager.Instance.AddCallback<Anki.Cozmo.ExternalInterface.GoingToSleep>(HandleGoingToSleep);
      RobotEngineManager.Instance.AddCallback<Anki.Cozmo.ExternalInterface.RobotDisconnected>(HandleDisconnectionMessage);
      RobotEngineManager.Instance.AddCallback<Anki.Cozmo.ExternalInterface.ReactionTriggerTransition>(HandleReactionaryBehavior);
      ExitChallengeOnPause = true;
      AllowFreeplayOnResume = true;
      DasTracker.Instance.TrackAppStartup();
    }

    private void Update() {
      if (!ListeningForBatteryLevel) {
        return;
      }
      IRobot robot = RobotEngineManager.Instance.CurrentRobot;
      // Battery voltage gets initialized to the float maxvalue, so ignore it until it's valid 
      if (null != robot && robot.BatteryVoltage <= _kMaxValidBatteryVoltage) {
        _LowPassFilteredVoltage = _LowPassFilteredVoltage * Settings.FilterSmoothingWeight + (1.0f - Settings.FilterSmoothingWeight) * robot.BatteryVoltage;

        if (!_IsPaused && !IsConfirmSleepDialogOpen && !IsGoToSleepDialogOpen && _LowPassFilteredVoltage < Settings.LowBatteryVoltageValue && !_LowBatteryAlertTriggered) {
          if (!sLowBatteryEventLogged) {
            sLowBatteryEventLogged = true;
            DAS.Event("robot.low_battery", "");
          }
          OpenLowBatteryAlert();
        }
      }
    }

    private void OnDestroy() {
      RobotEngineManager.Instance.RemoveCallback<Anki.Cozmo.ExternalInterface.GoingToSleep>(HandleGoingToSleep);
      RobotEngineManager.Instance.RemoveCallback<Anki.Cozmo.ExternalInterface.RobotDisconnected>(HandleDisconnectionMessage);
      RobotEngineManager.Instance.RemoveCallback<Anki.Cozmo.ExternalInterface.ReactionTriggerTransition>(HandleReactionaryBehavior);
    }

    private void OnApplicationFocus(bool focusStatus) {
      DAS.Debug("PauseManager.OnApplicationFocus", "Application focus: " + focusStatus);
    }

    private void OnApplicationPause(bool bPause) {
      DAS.Debug("PauseManager.OnApplicationPause", "Application pause: " + bPause);

      if (bPause && DataPersistence.DataPersistenceManager.Instance != null) {
        // always save on pause
        DataPersistence.DataPersistenceManager.Instance.Save();
      }

      HandleApplicationPause(bPause);
    }

    private void OnApplicationQuit() {
      DasTracker.Instance.TrackAppQuit();
    }

    private void HandleReactionaryBehavior(Anki.Cozmo.ExternalInterface.ReactionTriggerTransition message) {
      if (DataPersistenceManager.Instance.IsSDKEnabled) {
        return;
      }
      // Stop sleeping if the sleep dialog is open and a reactionary behavior other than PlacedOnCharger runs 
      if ((IsConfirmSleepDialogOpen) &&
          message.newTrigger != Anki.Cozmo.ReactionTrigger.PlacedOnCharger &&
          message.newTrigger != Anki.Cozmo.ReactionTrigger.NoneTrigger) {
        HandleCancelSleepToWakeUp();
        DAS.Debug("PauseManager.HandleReactionaryBehavior.StopSleep", "Transition to: " + message.newTrigger.ToString() + " Transitioning From: " + message.oldTrigger.ToString());
      }
    }

    private void HandleApplicationPause(bool shouldBePaused) {
      IRobot robot = RobotEngineManager.Instance.CurrentRobot;

      // When pausing, try to close any challenge that's open and reset robot state to Idle
      if (!_IsPaused && shouldBePaused) {
        DAS.Debug("PauseManager.HandleApplicationPause", "Application being paused");
        _IsPaused = true;

        HubWorldBase hub = HubWorldBase.Instance;
        if (null != hub && ExitChallengeOnPause) {
          _ClosedMinigameOnPause = hub.CloseChallengeImmediately();
        }
        else {
          _ClosedMinigameOnPause = false;
        }

        _ShouldPlayWakeupTimestamp = -1;
        if (!_EngineTriggeredSleep && _IdleTimeOutEnabled) {
          RobotEngineManager.Instance.SendRobotDisconnectReason(Anki.Cozmo.RobotDisconnectReason.SleepBackground);
          StartIdleTimeout(Settings.AppBackground_TimeTilSleep_sec, Settings.AppBackground_TimeTilDisconnect_sec);
          // Set up a timer so that if we unpause after starting the goToSleep, we'll do the wakeup (using a little buffer past beginning of sleep)
          _ShouldPlayWakeupTimestamp = Time.realtimeSinceStartup + Settings.AppBackground_TimeTilSleep_sec + Settings.AppBackground_SleepAnimGetInBuffer_sec;
        }

        if (null != robot) {
          robot.DisableReactionsWithLock(ReactionaryBehaviorEnableGroups.kPauseManagerId, ReactionaryBehaviorEnableGroups.kAppBackgroundedTriggers);
        }

        // Let the engine know that we're being paused
        RobotEngineManager.Instance.SendGameBeingPaused(true);

        DasTracker.Instance.TrackAppBackgrounded();

        // Keep as an event so can call from nonmonobehaviors
        if (OnPauseStateChanged != null) {
          OnPauseStateChanged(shouldBePaused);
        }

        RobotEngineManager.Instance.FlushChannelMessages();
      }
      // When unpausing, put the robot back into freeplay
      else if (_IsPaused && !shouldBePaused) {
        DAS.Debug("PauseManager.HandleApplicationPause", "Application unpaused");
        _IsPaused = false;

        DasTracker.Instance.TrackAppResumed();

        // Keep as an event so can call from nonmonobehaviors
        if (OnPauseStateChanged != null) {
          OnPauseStateChanged(shouldBePaused);
        }

        // Let the engine know that we're being unpaused
        RobotEngineManager.Instance.SendGameBeingPaused(false);

        // If the go to sleep dialog is open, the user has selected sleep and there's no turning back, so don't wake up Cozmo
        if (!IsGoToSleepDialogOpen) {
          bool shouldPlayWakeup = false;
          if (!_EngineTriggeredSleep && _IdleTimeOutEnabled) {
            StopIdleTimeout();

            if (_ShouldPlayWakeupTimestamp > 0 && Time.realtimeSinceStartup >= _ShouldPlayWakeupTimestamp) {
              shouldPlayWakeup = true;
            }
          }
          else {
            // Normally StopIdleTimeout() is responsible for removing the reaction lock, however, if idle timeout is not enabled
            // we still need to make sure to remove the lock
            if (robot != null) {
              robot.RemoveDisableReactionsLock(ReactionaryBehaviorEnableGroups.kPauseManagerId);
            }
          }

          if (shouldPlayWakeup) {
            if (null != robot) {
              robot.SendAnimationTrigger(Anki.Cozmo.AnimationTrigger.GoToSleepGetOut, HandleFinishedWakeup);
            }
          }
          else {
            HandleFinishedWakeup(true);
          }
        }
      }
    }

    private void HandleConfirmEngineTriggeredSleepCozmoButtonTapped() {
      StartPlayerInducedSleep(Anki.Cozmo.RobotDisconnectReason.SleepPlacedOnCharger);
    }

    private void HandleConfirmSleepCozmoButtonTapped() {
      StartPlayerInducedSleep(Anki.Cozmo.RobotDisconnectReason.SleepSettings);
    }

    public void StartPlayerInducedSleep(Anki.Cozmo.RobotDisconnectReason reason) {
      IRobot robot = RobotEngineManager.Instance.CurrentRobot;
      if (null != robot) {
        robot.DisableAllReactionsWithLock(ReactionaryBehaviorEnableGroups.kPauseManagerId);
      }
      RobotEngineManager.Instance.SendRobotDisconnectReason(reason);
      StartIdleTimeout(Settings.PlayerSleepCozmo_TimeTilSleep_sec, Settings.PlayerSleepCozmo_TimeTilDisconnect_sec);
      OpenGoToSleepDialogAndFreezeUI();
    }

    private void HandleFinishedWakeup(bool success) {
      HubWorldBase hub = HubWorldBase.Instance;
      IRobot robot = RobotEngineManager.Instance.CurrentRobot;

      if (null != robot && null != hub) {
        hub.StartFreeplay();
        robot.EnableCubeSleep(false);

        // If we were in the middle of a minigame, return to the main needs view
        if (_ClosedMinigameOnPause) {
          hub.StartLoadNeedsHubView();
          _ClosedMinigameOnPause = false;
        }
      }
      else {
        // If this is fired because we are returning from backgrounding the app
        // but Cozmo has disconnected, CloseAllDialogs and handle it like a disconnect
        PauseManager.Instance.PauseManagerReset();
        if (NeedsConnectionManager.Instance != null) {
          NeedsConnectionManager.Instance.ForceBoot(Anki.Cozmo.RobotDisconnectReason.Unknown);
        }
      }
    }

    // Handles message sent from engine when the player puts cozmo on the charger
    private void HandleGoingToSleep(Anki.Cozmo.ExternalInterface.GoingToSleep msg) {
      if (DataPersistenceManager.Instance.IsSDKEnabled) {
        return;
      }
      CloseLowBatteryDialog();
      CloseConfirmSleepDialog();
      _EngineTriggeredSleep = true;

      OpenConfirmSleepCozmoDialog(handleSleepCancel: true);
    }

    private void HandleDisconnectionMessage(Anki.Cozmo.ExternalInterface.RobotDisconnected msg) {
      PauseManagerReset();
    }

    private void PauseManagerReset() {
      CloseAllDialogs();
      ListeningForBatteryLevel = false;
      _StartedIdleTimeout = false;
      _EngineTriggeredSleep = false;
      _LowPassFilteredVoltage = _kMaxValidBatteryVoltage;
      _LowBatteryAlertTriggered = false;
    }

    private void StartIdleTimeout(float sleep_sec, float disconnect_sec) {
      if (!_StartedIdleTimeout) {
        IRobot robot = RobotEngineManager.Instance.CurrentRobot;
        if (null != robot) {
          robot.ResetRobotState(null);
          robot.EnableCubeSleep(true);
        }

        RobotEngineManager.Instance.StartIdleTimeout(faceOffTime_s: sleep_sec, disconnectTime_s: disconnect_sec);
        _StartedIdleTimeout = true;
      }
    }

    private void StopIdleTimeout() {
      IRobot robot = RobotEngineManager.Instance.CurrentRobot;
      bool robotValid = robot != null;

      // Only disable cube sleep if we definately enabled it from the call to StartIdleTimeout
      if (_StartedIdleTimeout) {
        if (robotValid) {
          robot.EnableCubeSleep(false);
        }
      }

      _StartedIdleTimeout = false;
      RobotEngineManager.Instance.CancelIdleTimeout();

      if (robotValid) {
        robot.RemoveDisableReactionsLock(ReactionaryBehaviorEnableGroups.kPauseManagerId);
        robot.RobotResumeFromIdle(AllowFreeplayOnResume);
      }
    }

    public void OpenConfirmSleepCozmoDialog(bool handleSleepCancel) {
      if (DataPersistenceManager.Instance.IsSDKEnabled) {
        return;
      }
      if (IsGoToSleepDialogOpen) {
        // already going to sleep, so do nothing.
        return;
      }

      CloseLowBatteryDialog();
      if (!IsConfirmSleepDialogOpen) {
        AlertModalData confirmSleepCozmoAlert = null;
        if (handleSleepCancel) {
          confirmSleepCozmoAlert = new AlertModalData("sleep_cozmo_on_charger_alert",
                                                      LocalizationKeys.kSettingsSleepCozmoPanelConfirmationModalTitle,
                                                      LocalizationKeys.kSettingsSleepCozmoPanelConfirmModalDescription,
                                                      new AlertModalButtonData("confirm_sleep_button",
                                                                               LocalizationKeys.kSettingsSleepCozmoPanelConfirmModalButtonConfirm,
                                                                               HandleConfirmEngineTriggeredSleepCozmoButtonTapped,
                                                                               Anki.Cozmo.Audio.AudioEventParameter.UIEvent(Anki.AudioMetaData.GameEvent.Ui.Cozmo_Disconnect)),
                                                      new AlertModalButtonData("cancel_sleep_button", LocalizationKeys.kButtonCancel,
                                                                               HandleEngineTriggeredSleepCancel));

        }
        else {
          confirmSleepCozmoAlert = new AlertModalData("sleep_cozmo_from_settings_alert",
                                                      LocalizationKeys.kSettingsSleepCozmoPanelConfirmationModalTitle,
                                                      LocalizationKeys.kSettingsSleepCozmoPanelConfirmModalDescription,
                                                      new AlertModalButtonData("confirm_sleep_button",
                                                                               LocalizationKeys.kSettingsSleepCozmoPanelConfirmModalButtonConfirm,
                                                                               HandleConfirmSleepCozmoButtonTapped,
                                                                               Anki.Cozmo.Audio.AudioEventParameter.UIEvent(Anki.AudioMetaData.GameEvent.Ui.Cozmo_Disconnect)),
                                                      new AlertModalButtonData("cancel_sleep_button",
                                                                               LocalizationKeys.kButtonCancel));
        }

        var confirmSleepCozmoPriority = new ModalPriorityData(ModalPriorityLayer.High, 1,
                                                              LowPriorityModalAction.Queue,
                                                              HighPriorityModalAction.Stack);

        Action<AlertModal> confirmSleepCreated = (alertModal) => {
          _SleepCozmoConfirmDialog = alertModal;
          alertModal.OpenAudioEvent = Anki.Cozmo.Audio.AudioEventParameter.UIEvent(Anki.AudioMetaData.GameEvent.Ui.Attention_Device);
        };

        UIManager.OpenAlert(confirmSleepCozmoAlert, confirmSleepCozmoPriority, confirmSleepCreated,
                            overrideBackgroundDim: null,
                            overrideCloseOnTouchOutside: false);
      }
    }

    private void HandleEngineTriggeredSleepCancel() {
      StopIdleTimeout();
      _EngineTriggeredSleep = false;
      // Send event for sleep trigger cancelled so that custom behaviors can resume.
      if (OnCozmoSleepCancelled != null) {
        OnCozmoSleepCancelled();
      }
    }

    private void OpenGoToSleepDialogAndFreezeUI() {
      CloseLowBatteryDialog();
      CloseConfirmSleepDialog();
      if (!IsGoToSleepDialogOpen) {

        var goToSleepPriority = new ModalPriorityData(ModalPriorityLayer.VeryHigh, 0,
                                                      LowPriorityModalAction.Queue,
                                                      HighPriorityModalAction.ForceCloseOthersAndOpen);

        var goToSleepAlertData = new AlertModalData("cozmo_going_to_sleep_alert",
                                                    LocalizationKeys.kConnectivityCozmoSleepTitle,
                                                    LocalizationKeys.kConnectivityCozmoSleepDesc);

        UIManager.OpenAlert(goToSleepAlertData,
                            goToSleepPriority,
                            HandleGoToSleepAlertCreated,
                            overrideCloseOnTouchOutside: false);
      }
    }

    private void OpenLowBatteryAlert() {
      if (DataPersistenceManager.Instance.IsSDKEnabled) {
        return;
      }
      if (!IsLowBatteryDialogOpen) {
        var lowBatteryAlertData = new AlertModalData("low_battery_alert",
                                                     LocalizationKeys.kConnectivityCozmoLowBatteryTitle,
                                                     LocalizationKeys.kConnectivityCozmoLowBatteryDesc,
                                                     showCloseButton: true);

        Action<BaseModal> lowBatteryAlertCreated = (newModal) => {
          AlertModal alertModal = (AlertModal)newModal;
          alertModal.InitializeAlertData(lowBatteryAlertData);
          _LowBatteryAlertInstance = alertModal;
        };

        var lowBatteryPriorityData = new ModalPriorityData(ModalPriorityLayer.VeryLow, 1,
                                                           LowPriorityModalAction.Queue,
                                                           HighPriorityModalAction.Stack);
        DAS.Debug("PauseManager.OpenLowBatteryAlert", "Opening Low Battery Alert");
        UIManager.OpenModal(_LowBatteryAlertPrefab, lowBatteryPriorityData, lowBatteryAlertCreated);
        // this needs to be set right away otherwise the low battery listener will call OpenLowBatteryAlert a ton of times.
        _LowBatteryAlertTriggered = true;
      }
    }

    private void CloseAllDialogs() {
      CloseConfirmSleepDialog();
      CloseGoToSleepDialog();
      CloseLowBatteryDialog();
    }

    private void CloseConfirmSleepDialog() {
      if (null != _SleepCozmoConfirmDialog) {
        _SleepCozmoConfirmDialog.CloseDialog();
        _SleepCozmoConfirmDialog = null;
      }
    }

    private void CloseGoToSleepDialog() {
      if (null != _GoToSleepDialog) {
        _GoToSleepDialog.CloseDialog();
        _GoToSleepDialog = null;

        // Set Music State back to freeplay
        Anki.Cozmo.Audio.GameAudioClient.SetMusicState(Anki.AudioMetaData.GameState.Music.Freeplay);
      }
    }

    private void CloseLowBatteryDialog() {
      if (null != _LowBatteryAlertInstance) {
        _LowBatteryAlertInstance.CloseDialog();
        _LowBatteryAlertInstance = null;
      }
    }

    private void HandleGoToSleepAlertCreated(BaseModal modal) {
      _GoToSleepDialog = modal;
      // Set Music State
      Anki.Cozmo.Audio.GameAudioClient.SetMusicState(Anki.AudioMetaData.GameState.Music.Sleep);
    }

    private void HandleCancelSleepToWakeUp() {
      StopIdleTimeout();
      _EngineTriggeredSleep = false;
      CloseGoToSleepDialog();
      CloseConfirmSleepDialog();
    }
  }
}

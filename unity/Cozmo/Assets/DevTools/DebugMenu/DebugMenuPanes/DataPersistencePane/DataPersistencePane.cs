﻿using UnityEngine;
using UnityEngine.UI;
using Cozmo.Challenge;
using Cozmo.ConnectionFlow;

namespace DataPersistence {
  public class DataPersistencePane : MonoBehaviour {

    [SerializeField]
    private Button _ResetSaveDataButton;
    [SerializeField]
    private Button _ResetEverythingButton;
    [SerializeField]
    private Button _ResetNeedsButton;

    [SerializeField]
    private Button _StartNewSessionButton;

    [SerializeField]
    private Text _SessionDays;

    [SerializeField]
    private ChallengeDataList _ChallengeDataList;

    [SerializeField]
    private InputField _SkillProfileCurrentLevel;
    [SerializeField]
    private InputField _SkillProfileHighLevel;
    [SerializeField]
    private InputField _SkillRobotHighLevel;
    [SerializeField]
    private Button _SubmitSkillsButton;

    [SerializeField]
    private Text _LblStatus;
    private bool _IsResettingEverything = false;
    private bool _IsResettingNeeds = false;

    private void Start() {
      _ResetSaveDataButton.onClick.AddListener(HandleResetSaveDataButtonClicked);
      _StartNewSessionButton.onClick.AddListener(StartNewSessionButtonClicked);

      _SubmitSkillsButton.onClick.AddListener(SubmitSkillsButtonClicked);
      _ResetEverythingButton.onClick.AddListener(SubmitResetEverythingData);
      _ResetNeedsButton.onClick.AddListener(SubmitResetNeedsData);
      _LblStatus.text = "";
      InitSkills();

      RobotEngineManager.Instance.AddCallback<Anki.Cozmo.ExternalInterface.RestoreRobotStatus>(HandleRestoreStatus);
    }

    private void OnDestroy() {
      RobotEngineManager.Instance.RemoveCallback<Anki.Cozmo.ExternalInterface.RestoreRobotStatus>(HandleRestoreStatus);
    }

    private void HandleResetSaveDataButtonClicked() {
      // use reflection to change readonly field
      typeof(DataPersistenceManager).GetField("Data").SetValue(DataPersistenceManager.Instance, new SaveData());
      DataPersistenceManager.Instance.Save();

      if (NeedsConnectionManager.Instance != null) {
        NeedsConnectionManager.Instance.ForceBoot();
      }

      // Clear the block pool
      Anki.Cozmo.ExternalInterface.BlockPoolResetMessage blockPoolResetMessage = new Anki.Cozmo.ExternalInterface.BlockPoolResetMessage();
      RobotEngineManager.Instance.Message.BlockPoolResetMessage = blockPoolResetMessage;
      RobotEngineManager.Instance.SendMessage();

      // Delete the needs state data from device
      if (RobotEngineManager.Instance.CurrentRobot != null) {
        RobotEngineManager.Instance.CurrentRobot.WipeDeviceNeedsData();
      }
    }

    private void StartNewSessionButtonClicked() {

      int days;
      if (!int.TryParse(_SessionDays.text, out days)) {
        days = 1;
      }
      if (days > 0) {
        // Make sure next set of goals is random
        OnboardingManager.Instance.CompletePhase(OnboardingManager.OnboardingPhases.DailyGoals);
        DataPersistence.DataPersistenceManager.Instance.Data.DefaultProfile.FirstTimeUserFlow = false;
        DataPersistenceManager.Instance.Data.DefaultProfile.Sessions.ForEach(x => x.Date = x.Date.OffsetDays(-days));
        DataPersistenceManager.Instance.Save();
      }
      if (NeedsConnectionManager.Instance != null) {
        NeedsConnectionManager.Instance.ForceBoot();
      }
    }

    private void InitSkills() {
      int profileCurrLevel;
      int profileHighLevel;
      int robotHighLevel;
      SkillSystem.Instance.GetDebugSkillsForGame(out profileCurrLevel, out profileHighLevel, out robotHighLevel);
      _SkillProfileCurrentLevel.text = profileCurrLevel.ToString();
      _SkillProfileHighLevel.text = profileHighLevel.ToString();
      _SkillRobotHighLevel.text = robotHighLevel.ToString();
    }

    private void SubmitSkillsButtonClicked() {
      SkillSystem.Instance.SetDebugSkillsForGame(int.Parse(_SkillProfileCurrentLevel.text),
        int.Parse(_SkillProfileHighLevel.text), int.Parse(_SkillRobotHighLevel.text));
    }

    private void SubmitResetEverythingData() {
      if (!_IsResettingEverything && !_IsResettingNeeds) {
        if (RobotEngineManager.Instance.CurrentRobot == null) {
          _LblStatus.text = "Error: Not connected to the robot!";
          _LblStatus.color = Color.red;
        }
        else {
          _IsResettingEverything = true;
          _LblStatus.text = "THINKING. Stop touching things.";
          _LblStatus.color = Color.blue;
          RobotEngineManager.Instance.CurrentRobot.WipeDeviceNeedsData();
          RobotEngineManager.Instance.CurrentRobot.WipeRobotGameData();
        }
      }
    }

    private void SubmitResetNeedsData() {
      if (!_IsResettingEverything && !_IsResettingNeeds) {
        if (RobotEngineManager.Instance.CurrentRobot == null) {
          _LblStatus.text = "Error: Not connected to the robot!";
          _LblStatus.color = Color.red;
        }
        else {
          _IsResettingNeeds = true;
          _LblStatus.text = "THINKING. Stop touching things.";
          _LblStatus.color = Color.blue;
          RobotEngineManager.Instance.CurrentRobot.WipeDeviceNeedsData();
          RobotEngineManager.Instance.CurrentRobot.WipeRobotNeedsData();
        }
      }
    }

    private void HandleRestoreStatus(Anki.Cozmo.ExternalInterface.RestoreRobotStatus status) {
      if (status.isWipe) {
        if (status.success) {
          if (_IsResettingEverything) {
            HandleResetSaveDataButtonClicked();
          }
          else {
            if (NeedsConnectionManager.Instance != null) {
              NeedsConnectionManager.Instance.ForceBoot();
            }
          }
          DebugMenuManager.Instance.CloseDebugMenuDialog();
        }
        else {
          _LblStatus.text = "Error: Robot Data clear failed!";
          _LblStatus.color = Color.red;
        }
        _IsResettingEverything = false;
        _IsResettingNeeds = false;
      }
    }

  }
}
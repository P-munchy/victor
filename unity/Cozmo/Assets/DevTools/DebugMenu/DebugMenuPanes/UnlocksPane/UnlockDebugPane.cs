﻿using Cozmo.Challenge;
using System.Collections.Generic;
using UnityEngine;

public class UnlockDebugPane : MonoBehaviour {
  [SerializeField]
  private UnityEngine.UI.Button _LockButton;

  [SerializeField]
  private UnityEngine.UI.Button _UnlockButton;

  [SerializeField]
  private UnityEngine.UI.Dropdown _UnlockSelection;

  [SerializeField]
  private UnityEngine.UI.Button _UnlockDifficultiesButton;

  [SerializeField]
  private UnityEngine.UI.Button _UnlockAllSongsButton;

  [SerializeField]
  private UnityEngine.UI.Button _LockAllSongsButton;

  private const string _kSongUnlockPrefix = "Singing_";

  private void Start() {
    _LockButton.onClick.AddListener(OnHandleLockButtonClicked);
    _UnlockButton.onClick.AddListener(OnHandleUnlockButtonClicked);
    PopulateOptions();

    _UnlockDifficultiesButton.onClick.AddListener(OnHandleUnlockDifficultiesButtonClicked);
    _UnlockAllSongsButton.onClick.AddListener(OnHandleUnlockAllSongsButtonClicked);
    _LockAllSongsButton.onClick.AddListener(OnHandleLockAllSongsButtonClicked);
  }

  private void PopulateOptions() {
    List<UnityEngine.UI.Dropdown.OptionData> options = new List<UnityEngine.UI.Dropdown.OptionData>();
    for (int i = 0; i < System.Enum.GetValues(typeof(Anki.Cozmo.UnlockId)).Length; ++i) {
      UnityEngine.UI.Dropdown.OptionData option = new UnityEngine.UI.Dropdown.OptionData();
      option.text = System.Enum.GetValues(typeof(Anki.Cozmo.UnlockId)).GetValue(i).ToString();
      options.Add(option);
    }
    _UnlockSelection.AddOptions(options);
  }

  private Anki.Cozmo.UnlockId GetSelectedUnlockId() {
    return (Anki.Cozmo.UnlockId)System.Enum.Parse(typeof(Anki.Cozmo.UnlockId), _UnlockSelection.options[_UnlockSelection.value].text);
  }

  private void OnHandleLockButtonClicked() {
    UnlockablesManager.Instance.TrySetUnlocked(GetSelectedUnlockId(), false);
  }

  private void OnHandleUnlockButtonClicked() {
    UnlockablesManager.Instance.TrySetUnlocked(GetSelectedUnlockId(), true);
  }

  private void OnHandleUnlockAllSongsButtonClicked() {
    for (int i = 0; i < (int)Anki.Cozmo.UnlockId.Count; i++) {
      Anki.Cozmo.UnlockId id = (Anki.Cozmo.UnlockId)i;
      if (id.ToString().Contains(_kSongUnlockPrefix)) {
        UnlockablesManager.Instance.TrySetUnlocked(id, true);
      }
    }
  }

  private void OnHandleLockAllSongsButtonClicked() {
    for (int i = 0; i < (int)Anki.Cozmo.UnlockId.Count; i++) {
      Anki.Cozmo.UnlockId id = (Anki.Cozmo.UnlockId)i;
      if (id.ToString().Contains(_kSongUnlockPrefix)) {
        UnlockablesManager.Instance.TrySetUnlocked(id, false);
      }
    }
  }

  // Difficulties are unlocked in the app not the robot, so not related to real unlock manager
  // but most people logically look for the unlock of any feature here.
  private void OnHandleUnlockDifficultiesButtonClicked() {
    DataPersistence.PlayerProfile playerProfile = DataPersistence.DataPersistenceManager.Instance.Data.DefaultProfile;
    // Set the high scores because in MPspeedtap it checks for "completions" not wins...
    ChallengeData[] challengeList = ChallengeDataList.Instance.ChallengeData;
    for (int i = 0; i < challengeList.Length; ++i) {
      int numDifficulties = challengeList[i].DifficultyOptions.Count;
      playerProfile.GameDifficulty[challengeList[i].ChallengeID] = numDifficulties;
      for (int j = 0; j < numDifficulties; ++j) {
        playerProfile.HighScores[challengeList[i].ChallengeID + j] = 1;
      }
    }
  }
}

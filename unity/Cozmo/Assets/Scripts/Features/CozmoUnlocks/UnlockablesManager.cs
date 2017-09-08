using Anki.Cozmo;
using Cozmo.Challenge;
using System;
using System.Collections.Generic;
using UnityEngine;
using System.Text.RegularExpressions;

using Anki.Cozmo.ExternalInterface;


public class UnlockablesManager : MonoBehaviour {

  public static UnlockablesManager Instance { get; private set; }

  private void OnEnable() {
    if (Instance != null && Instance != this) {
      Destroy(gameObject);
      return;
    }
    else {
      Instance = this;
      InitializeUnlockablesState();
    }
  }

  public Action<UnlockId> OnUnlockComplete;

  public bool UnlocksLoaded { get { return _UnlocksLoaded; } }

  private bool _UnlocksLoaded = false;

  private Dictionary<Anki.Cozmo.UnlockId, bool> _UnlockablesState = new Dictionary<Anki.Cozmo.UnlockId, bool>();

  [SerializeField]
  private UnlockableInfoList _UnlockableInfoList;

  // index is the face slot, value is the unlockID;
  [SerializeField]
  private int _NumFaceSlots;

  public bool IsNewUnlock(Anki.Cozmo.UnlockId uID) {
    // In the case that you're connected to a cozmo that unlocked something and then connect to another cozmo with same device.
    // only show the new arrow if it's a known unlock
    return IsUnlocked(uID) && DataPersistence.DataPersistenceManager.Instance.Data.DefaultProfile.NewUnlocks.Contains(uID);
  }

  private void OnDestroy() {
    RobotEngineManager.Instance.RemoveCallback<Anki.Cozmo.ExternalInterface.RequestSetUnlockResult>(HandleOnUnlockRequestSuccess);
    RobotEngineManager.Instance.RemoveCallback<Anki.Cozmo.ExternalInterface.UnlockStatus>(HandleUnlockStatus);
    RobotEngineManager.Instance.RemoveCallback<Anki.Cozmo.ExternalInterface.UnlockedDefaults>(HandleUnlockDefaultsSet);
    GameEventManager.Instance.OnGameEvent -= HandleGameEvent;
    DataPersistence.DataPersistenceManager.Instance.OnSaveDataReset -= HandleSaveDataReset;
  }

  // should be called when connected to the robot and loaded unlock info from the physical robot.
  public void OnConnectLoad(Dictionary<Anki.Cozmo.UnlockId, bool> loadedUnlockables, bool fromBackup = false) {
    DAS.Info("UnlockablesManager.OnConnectLoad", "Unlocks loaded from robot / engine");
    _UnlockablesState = loadedUnlockables;
    _UnlocksLoaded = true;


    //Trying to track down if unlocks is getting out of sync and UI is broken, so logging on connection
    // Only want to log once so ignore the engine backups and only take the real robot version
    if (!fromBackup) {
      string game_unlock_status = "";
      string spark_unlock_status = "";
      foreach (KeyValuePair<UnlockId, bool> kvp in _UnlockablesState) {
        if (kvp.Value) {
          UnlockableInfo info = GetUnlockableInfo(kvp.Key, false);
          if (info != null) {
            if (info.UnlockableType == UnlockableType.Action) {
              spark_unlock_status += kvp.Key + ",";
            }
            else if (info.UnlockableType == UnlockableType.Game) {
              game_unlock_status += kvp.Key + ",";
            }
          }
          // not logging face unlocks
        }
      }
      DAS.Event("robot.game_unlock_status", game_unlock_status);
      DAS.Event("robot.spark_unlock_status", spark_unlock_status);
    }
  }

  private bool NothingUnlocked() {
    foreach (KeyValuePair<Anki.Cozmo.UnlockId, bool> kvp in _UnlockablesState) {
      if (kvp.Value) {
        return false;
      }
    }
    return true;
  }

  public int FaceSlotsSize() {
    return _NumFaceSlots;
  }

  // Should only be called before connecting to robot, robot will overwrite these
  public void InitializeUnlockablesState() {
    DAS.Info("UnlockablesManager.InitializeUnlockablesState", "InitializeUnlockablesState");
    for (int i = 0; i < _UnlockableInfoList.UnlockableInfoData.Length; ++i) {
      if (_UnlockablesState.ContainsKey(_UnlockableInfoList.UnlockableInfoData[i].Id.Value) == false) {
        _UnlockablesState.Add(_UnlockableInfoList.UnlockableInfoData[i].Id.Value, false);
      }
    }
    RobotEngineManager.Instance.AddCallback<Anki.Cozmo.ExternalInterface.RequestSetUnlockResult>(HandleOnUnlockRequestSuccess);
    RobotEngineManager.Instance.AddCallback<Anki.Cozmo.ExternalInterface.UnlockStatus>(HandleUnlockStatus);
    RobotEngineManager.Instance.AddCallback<Anki.Cozmo.ExternalInterface.UnlockedDefaults>(HandleUnlockDefaultsSet);
    GameEventManager.Instance.OnGameEvent += HandleGameEvent;
    DataPersistence.DataPersistenceManager.Instance.OnSaveDataReset += HandleSaveDataReset;
    RobotEngineManager.Instance.SendRequestUnlockDataFromBackup();
  }

  public List<UnlockableInfo> GetUnlockablesByType(UnlockableType unlockableType) {
    List<UnlockableInfo> unlockables = new List<UnlockableInfo>();
    for (int i = 0; i < _UnlockableInfoList.UnlockableInfoData.Length; ++i) {
      if (_UnlockableInfoList.UnlockableInfoData[i].UnlockableType == unlockableType) {
        unlockables.Add(_UnlockableInfoList.UnlockableInfoData[i]);
      }
    }
    return unlockables;
  }

  public List<UnlockableInfo> GetUnlocked() {
    List<UnlockableInfo> unlocked = new List<UnlockableInfo>();
    for (int i = 0; i < _UnlockableInfoList.UnlockableInfoData.Length; ++i) {
      if (_UnlockableInfoList.UnlockableInfoData[i].FeatureIsEnabled) {
        if (_UnlockablesState[_UnlockableInfoList.UnlockableInfoData[i].Id.Value]) {
          unlocked.Add(_UnlockableInfoList.UnlockableInfoData[i]);
        }
      }
    }
    return unlocked;
  }

  // explicit unlocks only.
  public List<UnlockableInfo> GetAvailableAndLocked() {
    List<UnlockableInfo> available = new List<UnlockableInfo>();
    for (int i = 0; i < _UnlockableInfoList.UnlockableInfoData.Length; ++i) {
      bool locked = !_UnlockablesState[_UnlockableInfoList.UnlockableInfoData[i].Id.Value];
      bool featureIsEnabled = _UnlockableInfoList.UnlockableInfoData[i].FeatureIsEnabled;
      bool isAvailable = featureIsEnabled && IsUnlockableAvailable(_UnlockableInfoList.UnlockableInfoData[i].Id.Value);
      if (locked && isAvailable) {
        available.Add(_UnlockableInfoList.UnlockableInfoData[i]);
      }
    }
    return available;
  }

  public UnlockableInfo GetUnlockableInfo(Anki.Cozmo.UnlockId id, bool errorOnNotFound = true) {
    UnlockableInfo info = Array.Find(_UnlockableInfoList.UnlockableInfoData, x => x.Id.Value == id);
    if (info == null && errorOnNotFound) {
      DAS.Error("UnlockablesManager.GetUnlockableInfo", "Invalid unlockable id " + id);
    }
    return info;
  }

  public void TrySetUnlocked(Anki.Cozmo.UnlockId id, bool unlocked) {
    if (RobotEngineManager.Instance.CurrentRobot != null) {
      RobotEngineManager.Instance.CurrentRobot.RequestSetUnlock(id, unlocked);
    }
    else {
      DAS.Error("UnlockablesManager.TrySetUnlocked.NullRobotError",
                "Tried to request an unlock but robot was null! id=" + id + " unlocked=" + unlocked);
    }
  }

  public bool IsUnlocked(Anki.Cozmo.UnlockId id) {
    bool unlocked = false;
    if (_UnlockablesState.TryGetValue(id, out unlocked)) {
      return unlocked;
    }
    else {
      DAS.Error("UnlockablesManager.IsUnlocked", string.Format("_UnlockablesState does not contain {0}", id));
      return false;
    }
  }

  public bool IsUnlockableAvailable(Anki.Cozmo.UnlockId id) {
    if (_UnlockablesState[id]) {
      return true;
    }

    UnlockableInfo unlockableInfo = Array.Find(_UnlockableInfoList.UnlockableInfoData, x => x.Id.Value == id);

    if (unlockableInfo == null) {
      DAS.Error(this, "Invalid unlockable id " + id);
      return false;
    }

    if (!IsOSSupported(unlockableInfo)) {
      return false;
    }

    if (unlockableInfo.ComingSoon || !unlockableInfo.FeatureIsEnabled) {
      return false;
    }

    return true;
  }

  public bool IsOSSupported(UnlockableInfo unlockInfo) {
#if UNITY_ANDROID && !UNITY_EDITOR
    if (unlockInfo.AndroidSDKVersion > 0) {
      var activity = CozmoBinding.GetCurrentActivity();
      int deviceSDKVersion = activity.Call<int>("getSDKVersion");
      int requiredSDKVersion = unlockInfo.AndroidSDKVersion;
      return deviceSDKVersion >= requiredSDKVersion;
    }
#endif
    return true;
  }

  private void HandleUnlockStatus(Anki.Cozmo.ExternalInterface.UnlockStatus message) {
    // Only update from backup if we aren't connected to a real cozmo.
    if ((message.fromBackup) && (RobotEngineManager.Instance.CurrentRobot != null)) {
      DAS.Info("UnlockablesManager.HandleUnlockStatus", "Backup Data Ignored, Already have Real Robot Data");
      return;
    }
    if (message.fromBackup) {
      DAS.Info("UnlockablesManager.HandleUnlockStatus", "Backup Data Accepted, No Robot Connected");
    }

    Dictionary<Anki.Cozmo.UnlockId, bool> loadedUnlockables = new Dictionary<UnlockId, bool>();
    for (int i = 0; i < (int)Anki.Cozmo.UnlockId.Count; ++i) {
      loadedUnlockables.Add((UnlockId)i, false);
    }

    for (int i = 0; i < message.unlocks.Length; ++i) {
      DAS.Info("UnlockablesManager.HandleUnlockStatus ", message.unlocks[i].ToString());
      loadedUnlockables[message.unlocks[i]] = true;
    }
    OnConnectLoad(loadedUnlockables, message.fromBackup);
  }

  private void HandleUnlockDefaultsSet(Anki.Cozmo.ExternalInterface.UnlockedDefaults message) {
    // This is a new robot, init our "default" unlocks
    DataPersistence.DataPersistenceManager.Instance.Data.DefaultProfile.NewUnlocks.Clear();
    if (!DebugMenuManager.Instance.DemoMode) {
      for (int i = 0; i < message.defaultUnlocks.Length; ++i) {
        DataPersistence.DataPersistenceManager.Instance.Data.DefaultProfile.NewUnlocks.Add(message.defaultUnlocks[i]);
      }
    }
  }

  private void HandleOnUnlockRequestSuccess(Anki.Cozmo.ExternalInterface.RequestSetUnlockResult resultMessage) {
    UnlockableInfo unlockData = GetUnlockableInfo(resultMessage.unlockID);
    if (unlockData == null) {
      DAS.Warn("UnlockablesManager.HandleOnUnlockRequestSuccess.UnlockIdNotFound", "Could not find data for id=" + resultMessage.unlockID);
      return;
    }

    _UnlockablesState[resultMessage.unlockID] = resultMessage.unlocked;
    if (resultMessage.unlocked) {
      GameEventManager.Instance.FireGameEvent(GameEventWrapperFactory.Create(GameEvent.OnUnlockableEarned, resultMessage.unlockID));
      // During demo mode, because this was from a debug event and all come at once, do not soft spark
      if (!DebugMenuManager.Instance.DemoMode) {
        // Demo mode doesn't want the arrows since it wants the appearance of everything already being unlocked.
        DataPersistence.DataPersistenceManager.Instance.Data.DefaultProfile.NewUnlocks.Add(resultMessage.unlockID);
      }

      if (OnUnlockComplete != null) {
        OnUnlockComplete(resultMessage.unlockID);
      }
    }
  }

  private void HandleSaveDataReset() {
    _UnlocksLoaded = false;
    List<UnlockId> keys = new List<Anki.Cozmo.UnlockId>(_UnlockablesState.Keys);
    foreach (UnlockId key in keys) {
      _UnlockablesState[key] = false;
    }
  }

  // Do not clear unlock slots until they have been played once.
  private void HandleGameEvent(GameEventWrapper gameEvent) {
    ChallengeData currGameData = null;
    if (ChallengeDataList.Instance != null && gameEvent.GameEventEnum == GameEvent.OnChallengeComplete && gameEvent is ChallengeGameEvent) {
      string currGameID = (gameEvent as ChallengeGameEvent).GameID;
      currGameData = Array.Find(ChallengeDataList.Instance.ChallengeData, (obj) => obj.ChallengeID == currGameID);
    }
    if (currGameData != null) {
      // Extremely special "Onboarding" case where we don't want to clear if they backed out.
      // it is new until a face is enrolled rather than played, also since it's unlocked by default it isn't on the list
      bool wantsRemove = true;
      IRobot robot = RobotEngineManager.Instance.CurrentRobot;
      if (robot != null && currGameData.UnlockId.Value == UnlockId.MeetCozmoGame) {
        if (robot.EnrolledFaces.Count == 0) {
          wantsRemove = false;
        }
      }
      if (wantsRemove) {
        DataPersistence.DataPersistenceManager.Instance.Data.DefaultProfile.NewUnlocks.Remove(currGameData.UnlockId.Value);
      }
    }
  }

}

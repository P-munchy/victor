﻿using UnityEngine;
using System.Collections;
using System.Collections.Generic;
using DataPersistence;
using G2U = Anki.Cozmo.ExternalInterface;
using U2G = Anki.Cozmo.ExternalInterface;
using Anki.Cozmo.NVStorage;

/* 
Singleton for games to interface with to get current skill level values.
 This system calculates when thresholds should be calculated and changed.

It is a pure C# class not a monobehavior since it doesn't need to be on a scene.
However, that means it does need to be inited from elsewhere.

*/

public class SkillSystem {
  private static SkillSystem _sInstance;

  public event System.Action<int> OnLevelUp;

  private byte[] _CozmoHighestLevels;
  private ChallengeData _CurrChallengeData;
  private int _ChallengeIndex;

  #region GameAPI

  public static SkillSystem Instance {
    get { 
      if (_sInstance == null) {
        _sInstance = new SkillSystem();
      }
      return _sInstance; 
    }
  }

  public void InitInstance() {
    SkillSystem.Instance.Init();
  }

  public void DestroyInstance() {
    SkillSystem.Instance.Destroy();
    _sInstance = null;
  }



  public void StartGame(ChallengeData data) { 
    _CurrChallengeData = data;

    ChallengeData[] challengeList = ChallengeDataList.Instance.ChallengeData;
    for (int i = 0; i < challengeList.Length; ++i) {
      if (challengeList[i].ChallengeID == _CurrChallengeData.ChallengeID) {
        _ChallengeIndex = i;
        break;
      }
    }
  }

  public void EndGame() { 
    _CurrChallengeData = null;
    _ChallengeIndex = -1;
  }

  // If player last level was 10 but on a new cozmo thats only level 3. That cozmo should play at level 3
  public int GetCozmoSkillLevel(GameSkillData playerSkill) {
    return Mathf.Min(_CozmoHighestLevels[_ChallengeIndex], playerSkill.LastLevel);
  }


  public GameSkillLevelConfig GetSkillLevelConfig() {
    GameSkillData currSkillData = GetSkillDataForGame();
    if (currSkillData != null) {
      GameSkillConfig skillConfig = _CurrChallengeData.MinigameConfig.SkillConfig;
      GameSkillLevelConfig skillLevelConfig = skillConfig.GetCurrLevelConfig(GetCozmoSkillLevel(currSkillData));

      return skillLevelConfig;
    }
    return null;
  }

  public float GetSkillVal(string skillName) {
    GameSkillLevelConfig skillLevelConfig = GetSkillLevelConfig();
    float val = 0.0f;
    if (skillLevelConfig != null) {
      skillLevelConfig.SkillMap.TryGetValue(skillName, out val);
    }
    return val;
  }

  public bool HasSkillVal(string skillName) {
    GameSkillLevelConfig skillLevelConfig = GetSkillLevelConfig();
    if (skillLevelConfig != null) {
      return skillLevelConfig.SkillMap.ContainsKey(skillName);
    }
    return false;
  }

  #endregion

  #region DebugMenuAPI

  public bool GetDebugSkillsForGame(out int profileCurrSkill, out int profileHighSkill, out int robotHighSkill) {
    profileCurrSkill = -1;
    profileHighSkill = -1;
    robotHighSkill = -1;
    GameSkillData profileData = GetSkillDataForGame();
    if (profileData != null && _ChallengeIndex >= 0) {
      profileCurrSkill = profileData.LastLevel;
      profileHighSkill = profileData.HighestLevel;
      robotHighSkill = _CozmoHighestLevels[_ChallengeIndex];
    }
    return profileData != null;
  }

  public bool SetDebugSkillsForGame(int profileCurrSkill, int profileHighSkill, int robotHighSkill) {
    GameSkillData profileData = GetSkillDataForGame();
    if (profileData != null && _ChallengeIndex >= 0) {
      profileData.LastLevel = profileCurrSkill;
      profileData.HighestLevel = profileHighSkill;
      _CozmoHighestLevels[_ChallengeIndex] = (byte)robotHighSkill;

      UpdateHighestSkillsOnRobot();
      DataPersistenceManager.Instance.Save();
    }
    return profileData != null;
  }

  public void DebugEraseStorage() {
    RobotEngineManager.Instance.Message.NVStorageEraseEntry = new G2U.NVStorageEraseEntry();
    RobotEngineManager.Instance.Message.NVStorageEraseEntry.tag = NVEntryTag.NVEntry_GameSkillLevels;
    RobotEngineManager.Instance.SendMessage();
  }

  #endregion

  private GameSkillData GetSkillDataForGame() {
// In the event we've never played this game before and they have old save data.
    if (DataPersistence.DataPersistenceManager.Instance.Data.DefaultProfile.CozmoSkillLevels == null) {
      DataPersistence.DataPersistenceManager.Instance.Data.DefaultProfile.CozmoSkillLevels = new Dictionary<string, GameSkillData>();
    }
    if (_CurrChallengeData) {
      if (!DataPersistence.DataPersistenceManager.Instance.Data.DefaultProfile.CozmoSkillLevels.ContainsKey(_CurrChallengeData.ChallengeID)) {
        DataPersistence.DataPersistenceManager.Instance.Data.DefaultProfile.CozmoSkillLevels.Add(_CurrChallengeData.ChallengeID, new GameSkillData());
      }
      return DataPersistence.DataPersistenceManager.Instance.Data.DefaultProfile.CozmoSkillLevels[_CurrChallengeData.ChallengeID];
    }
    return null;
  }

  public void HandleGameEvent(Anki.Cozmo.GameEvent cozEvent) {
    GameSkillData currSkillData = GetSkillDataForGame();
    if (currSkillData != null) {
      GameSkillConfig skillConfig = _CurrChallengeData.MinigameConfig.SkillConfig;
      if (skillConfig.IsGainChallengePointEvent(cozEvent)) {
        currSkillData.WinPointsTotal++;
      }
      if (skillConfig.IsLoseChallengePointEvent(cozEvent)) {
        currSkillData.LossPointsTotal++;
      }
      // In the event we quit out early and didn't reach an evaluate event, force a clear
      if (skillConfig.IsResetPointEvent(cozEvent)) {
        currSkillData.ResetPoints();
      }

      if (skillConfig.IsLevelChangeEvent(cozEvent)) {
        // See if things are pass the level up/down thresholds...
        GameSkillLevelConfig skillLevelConfig = GetSkillLevelConfig();
        if (skillLevelConfig != null) {
          // Reset the for next calculation if our percent has changed.
          float pointTotal = (float)(currSkillData.WinPointsTotal + currSkillData.LossPointsTotal);
          // Only evaluate after a certain number of points scored if desired so players can't just quit.
          bool thresholdPassed = true;
          if (skillConfig.UsePointThreshold && skillConfig.ComparedPointThreshold < pointTotal) {
            thresholdPassed = false;
          }
          if (thresholdPassed) {
            float winPercent = (currSkillData.WinPointsTotal / pointTotal);
            // We're losing too much, level up

            if (winPercent < skillLevelConfig.LowerBoundThreshold) {
              int cozmoSkillLevel = GetCozmoSkillLevel(currSkillData);

              //  if new high, let the player know
              if (cozmoSkillLevel + 1 < skillConfig.GetMaxLevel()) {
                bool newHighestLevel = cozmoSkillLevel == currSkillData.HighestLevel;
                // this is explicity player profile based, ignore robot level.
                if (cozmoSkillLevel == currSkillData.LastLevel) {
                  currSkillData.ChangeLevel(currSkillData.LastLevel + 1);
                }
                if (_CozmoHighestLevels[_ChallengeIndex] < currSkillData.LastLevel) {
                  _CozmoHighestLevels[_ChallengeIndex]++;
                }
                UpdateHighestSkillsOnRobot();
              
                DAS.Event("game.cozmoskill.levelup", _CurrChallengeData.ChallengeID, null, 
                  DASUtil.FormatExtraData(cozmoSkillLevel.ToString() + "," + currSkillData.HighestLevel.ToString()));

                if (OnLevelUp != null && newHighestLevel) {
                  OnLevelUp(currSkillData.LastLevel);
                  DataPersistenceManager.Instance.Save();
                }
              }
            }
            // we're winning too much, level down
            else if (winPercent > skillLevelConfig.UpperBoundThreshold) {
              currSkillData.ChangeLevel(currSkillData.LastLevel - 1);
              // cozmosHighestRobotLevel never levels down
              DAS.Event("game.cozmoskill.leveldown", _CurrChallengeData.ChallengeID, null, 
                DASUtil.FormatExtraData(currSkillData.LastLevel.ToString() + "," + currSkillData.HighestLevel.ToString()));
            }
            else {
              currSkillData.ResetPoints();
            }
          }
        }
      }
    }
  }

  private void Destroy() {
    GameEventManager.Instance.OnGameEvent -= HandleGameEvent;

    RobotEngineManager.Instance.RobotConnected -= HandleRobotConnected;
    RobotEngineManager.Instance.OnGotNVStorageData -= HandleNVStorageRead;
    RobotEngineManager.Instance.OnGotNVStorageOpResult -= HandleNVStorageOpResult;
  }

  private void Init() {
    GameEventManager.Instance.OnGameEvent += HandleGameEvent;
    
    RobotEngineManager.Instance.RobotConnected += HandleRobotConnected;
    RobotEngineManager.Instance.OnGotNVStorageData += HandleNVStorageRead;
    RobotEngineManager.Instance.OnGotNVStorageOpResult += HandleNVStorageOpResult;

    _ChallengeIndex = -1;
    _CurrChallengeData = null;
    SetCozmoHighestLevelsReached(null, 0);
  }

  private void HandleRobotConnected(int rbt_id) {

    RobotEngineManager.Instance.Message.NVStorageReadEntry = new G2U.NVStorageReadEntry();
    RobotEngineManager.Instance.Message.NVStorageReadEntry.tag = NVEntryTag.NVEntry_GameSkillLevels;
    RobotEngineManager.Instance.SendMessage();
  }
  // if this was an failure we're never going to get the result we
  private void HandleNVStorageOpResult(G2U.NVStorageOpResult opResult) {
    if (opResult.op == NVOperation.NVOP_READ &&
        opResult.tag == NVEntryTag.NVEntry_GameSkillLevels) {
      if (opResult.result != NVResult.NV_OKAY &&
          opResult.result != NVResult.NV_SCHEDULED) {
        // write out defaults so we have some 0s for next time,
        // This was likely the first time and was just a "not found"
        UpdateHighestSkillsOnRobot();
      }
    }
  }

  private void HandleNVStorageRead(G2U.NVStorageData robotData) {
    if (robotData.tag == NVEntryTag.NVEntry_GameSkillLevels) {
      SetCozmoHighestLevelsReached(robotData.data, robotData.data_length);
    }
  }

  private void SetCozmoHighestLevelsReached(byte[] robotData, int robotDataLen) {
    // RobotData is just highest level in challengeList order
    ChallengeDataList challengeList = ChallengeDataList.Instance;
    // default if starting on the wrong scene or in factory 
    int numChallenges = 1;
    if (challengeList != null) {
      numChallenges = challengeList.ChallengeData.Length;
    }
    numChallenges = Mathf.Max(robotDataLen, numChallenges);
    _CozmoHighestLevels = new byte[numChallenges];
    // first time init
    if (robotData != null) {
      System.Array.Copy(robotData, _CozmoHighestLevels, robotDataLen);
    }
  }

  private void UpdateHighestSkillsOnRobot() {
    // Write to updated array...
    RobotEngineManager.Instance.Message.NVStorageWriteEntry = new G2U.NVStorageWriteEntry();
    RobotEngineManager.Instance.Message.NVStorageWriteEntry.tag = Anki.Cozmo.NVStorage.NVEntryTag.NVEntry_GameSkillLevels;
    System.Array.Copy(_CozmoHighestLevels, RobotEngineManager.Instance.Message.NVStorageWriteEntry.data, _CozmoHighestLevels.Length);

    RobotEngineManager.Instance.Message.NVStorageWriteEntry.data_length = (ushort)_CozmoHighestLevels.Length;
    RobotEngineManager.Instance.SendMessage();
  }


}
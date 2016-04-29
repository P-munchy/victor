﻿using System;
using UnityEngine;
using System.Collections;
using System.Collections.Generic;
using Anki.Cozmo;
using DataPersistence;
using Newtonsoft.Json;

namespace Cozmo {
  namespace UI {
    [System.Serializable]
    public class DailyGoal : IDisposable {
      
      public GameEvent GoalEvent;
      public LocalizedString Title;
      public LocalizedString Description;
      public string RewardType;
      public int Progress;
      public int Target;
      public int PointsRewarded;

      private bool _Completed = false;

      public bool GoalComplete {
        get {
          return (Progress >= Target);
        }
      }


      // Action that fires when this Daily Goal is updated, passes through the DailyGoal itself so listeners can handle it.
      [JsonIgnore]
      public Action<DailyGoal> OnDailyGoalUpdated;
      [JsonIgnore]
      public Action<DailyGoal> OnDailyGoalCompleted;

      // TODO: Refactor GameEvent to allow for more situation based events.
      // Example : Replace SpeedTapSessionWin with MinigameSessionEnded, but the related Goal would then
      // have a MinigameIDCondition (SpeedTap) and a DidWinCondition (True).
      // NOTE : How do we plan to manage gamestate like this? Especially things like DidWin.

      // Conditions that must be met in order for this to progress when its event is fired.
      public List<GoalCondition> ProgConditions = new List<GoalCondition>();

      public DailyGoal(GameEvent gEvent, string titleKey, string descKey, int reward, int target, string rewardType, List<GoalCondition> triggerCon, int currProg = 0) {
        GoalEvent = gEvent;
        Title = new LocalizedString();
        Description = new LocalizedString();
        Title.Key = titleKey;
        Description.Key = descKey;
        PointsRewarded = reward;
        Target = target;
        Progress = currProg;
        _Completed = GoalComplete;
        RewardType = rewardType;
        ProgConditions = triggerCon;
        GameEventManager.Instance.OnGameEvent += ProgressGoal;
      }

      public void Dispose() {
        GameEventManager.Instance.OnGameEvent -= ProgressGoal;
      }

      public void ProgressGoal(GameEvent gEvent) {
        if (gEvent != GoalEvent) {
          return;
        }
        // If ProgConditions aren't met, don't progress
        if (!CanProg()) {
          return;
        }
        // Progress Goal
        Progress++;
        DAS.Event(this, string.Format("{0} Progressed to {1}", Title, Progress));
        // Check if Completed
        CheckIfComplete();
        if (OnDailyGoalUpdated != null) {
          OnDailyGoalUpdated.Invoke(this);
        }
      }

      public void DebugSetGoalProgress(int prog) {
        Progress = prog;
        if (!GoalComplete && _Completed) {
          _Completed = false;
          GameEventManager.Instance.OnGameEvent += ProgressGoal;
        }
        else if (_Completed == false) {
          CheckIfComplete();
        }
        if (OnDailyGoalUpdated != null) {
          OnDailyGoalUpdated.Invoke(this);
        }
        
      }

      public void DebugUndoGoalProgress() {
        if (Progress > 0) {
          Progress--;
          if (!GoalComplete && _Completed) {
            _Completed = false;
            GameEventManager.Instance.OnGameEvent += ProgressGoal;
          }
          if (OnDailyGoalUpdated != null) {
            OnDailyGoalUpdated.Invoke(this);
          }
        }
        
      }

      public void DebugResetGoalProgress() {
        Progress = 0;
        if (_Completed) {
          _Completed = false;
          GameEventManager.Instance.OnGameEvent += ProgressGoal;
        }
        if (OnDailyGoalUpdated != null) {
          OnDailyGoalUpdated.Invoke(this);
        }

      }

      /// <summary>
      /// Checks if the goal has just been completed, handles any logic that is fired when
      /// a goal is completed.
      /// </summary>
      public void CheckIfComplete() {
        if (GoalComplete && _Completed == false) {
          // Grant Reward
          DAS.Event(this, string.Format("{0} Completed", Title));
          DataPersistenceManager.Instance.Data.DefaultProfile.Inventory.AddItemAmount(RewardType, PointsRewarded);
          if (OnDailyGoalCompleted != null) {
            OnDailyGoalCompleted.Invoke(this);
          }
          _Completed = true;
          GameEventManager.Instance.OnGameEvent -= ProgressGoal;
        }
      }

      public bool CanProg() {
        if (ProgConditions == null) {
          return true;
        }
        for (int i = 0; i < ProgConditions.Count; i++) {
          if (ProgConditions[i].ConditionMet() == false) {
            return false;
          }
        }
        return true;
      }
    }
  }
}
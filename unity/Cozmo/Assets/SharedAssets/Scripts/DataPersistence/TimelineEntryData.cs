﻿using System;
using System.Collections.Generic;

namespace DataPersistence {
  public class TimelineEntryData {
    public Date Date;

    public StatContainer Goals;

    public StatContainer Progress;

    public float PlayTime;

    public readonly List<CompletedChallengeData> CompletedChallenges;

    // The entry is considered complete after the friendship points have been awarded.
    public bool Complete;

    // Is true once all daily goals have been completed
    public bool GoalsFinished;

    public int StartingFriendshipPoints;

    public int StartingFriendshipLevel;

    public int EndingFriendshipPoints;

    public int EndingFriendshipLevel;

    // because each level resets friendship point count,
    // we won't be able to simply do ending points - starting points
    public int AwardedFriendshipPoints;

    public TimelineEntryData() {
      Goals = new StatContainer();
      Progress = new StatContainer();
      CompletedChallenges = new List<CompletedChallengeData>();
    }

    public TimelineEntryData(Date date) : this() {
      Date = date;
    }
  }

  public class CompletedChallengeData {
    public DateTime StartTime;

    public DateTime EndTime;

    public readonly StatContainer AvailableStats;

    public readonly StatContainer RecievedStats;

    public string ChallengeId;

    public bool Won;

    public CompletedChallengeData() {
      AvailableStats = new StatContainer();
      RecievedStats = new StatContainer();
    }
  }
}


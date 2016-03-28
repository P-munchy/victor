﻿using UnityEngine;
using System.Collections;
using AskCozmo;
using System;
using Anki.Cozmo;

public class ChallengeData : ScriptableObject {
  // the mini game prefab to load for this challenge
  [SerializeField]
  private GameObject _MinigamePrefab;

  public GameObject MinigamePrefab {
    get { return _MinigamePrefab; }
  }

  // the key used to find this specific challenge
  [SerializeField]
  private string _ChallengeID;

  public string ChallengeID {
    get { return _ChallengeID; }
  }

  // whether or not we should show this challenge as part of the game.
  [SerializeField]
  private bool _EnableHubWorldChallenge = true;

  public bool EnableHubWorldChallenge {
    get { return _EnableHubWorldChallenge; }
  }

  // show or not on dev world challenge list
  [SerializeField]
  private bool _EnableDevWorldChallenge = true;

  public bool EnableDevWorldChallenge {
    get { return _EnableDevWorldChallenge; }
  }

  // the key used to get the actual localized string for the
  // challenge title shown to the player.
  [SerializeField]
  private string _ChallengeTitleLocKey;

  public string ChallengeTitleLocKey {
    get { return _ChallengeTitleLocKey; }
  }

  // the key used to get the actual localized string for the
  // challenge description shown to the player.
  [SerializeField]
  private string _ChallengeDescriptionLocKey;

  public string ChallengeDescriptionLocKey {
    get { return _ChallengeDescriptionLocKey; }
  }

  // icon to show to represent this challenge
  [SerializeField]
  private Sprite _ChallengeIcon;

  public Sprite ChallengeIcon {
    get { return _ChallengeIcon; }
  }

  // The set of requirements needed to unlock this challenge
  [SerializeField]
  private ChallengeRequirements _ChallengeReqs;

  public ChallengeRequirements ChallengeReqs {
    get { return _ChallengeReqs; }
  }

  [SerializeField]
  protected string _HowToPlayDialogContentLocKey = null;

  public string HowToPlayDialogContentLocKey {
    get { return _HowToPlayDialogContentLocKey; }
  }

  [SerializeField]
  protected GameObject _HowToPlayDialogContentPrefab;

  public GameObject HowToPlayDialogContentPrefab {
    get { return _HowToPlayDialogContentPrefab; }
  }

  [SerializeField]
  protected MusicStateWrapper _Music;

  public Anki.Cozmo.Audio.GameState.Music DefaultMusic {
    get { return _Music.Music; }
  }

  // string path to MinigameConfig
  public MinigameConfigBase MinigameConfig;
}

[System.Serializable]
public struct MusicStateWrapper {

  public MusicStateWrapper(Anki.Cozmo.Audio.GameState.Music music) {
    _Music = (int)music;
  }

  [SerializeField]
  private int _Music;

  public Anki.Cozmo.Audio.GameState.Music Music { 
    get { return (Anki.Cozmo.Audio.GameState.Music)_Music; }
    set { _Music = (int)value; }
  }

  public static implicit operator Anki.Cozmo.Audio.GameState.Music(MusicStateWrapper other) {
    return other.Music;
  }

  public static implicit operator MusicStateWrapper(Anki.Cozmo.Audio.GameState.Music other) {
    return new MusicStateWrapper(other);
  }
}

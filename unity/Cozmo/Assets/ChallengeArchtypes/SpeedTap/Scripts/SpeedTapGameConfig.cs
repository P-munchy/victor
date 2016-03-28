﻿using UnityEngine;
using System.Collections.Generic;

public class SpeedTapGameConfig : MinigameConfigBase {
  public override int NumCubesRequired() {
    return 2;
  }

  public override int NumPlayersRequired() {
    return 1;
  }

  public int Rounds;
  public int MaxScorePerRound;

  [SerializeField]
  protected MusicStateWrapper _BetweenRoundMusic;

  public Anki.Cozmo.Audio.GameState.Music BetweenRoundMusic {
    get { return _BetweenRoundMusic.Music; }
  }


  public List<DifficultySelectOptionData> DifficultyOptions = new List<DifficultySelectOptionData>();
}

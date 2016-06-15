﻿using UnityEngine;
using System.Collections.Generic;

namespace Simon {
  public class ScanForInitialCubeState : InitialCubesState {

    private const float _kMinDistMM = 60.0f;
    private const float _kRotateSec = 2.0f;
    private enum ScannedSetupCubeState {
      Unknown,
      Seen,
      TooClose,
      Ready
    };

    private enum ScanPhase {
      NoCubesSeen,
      WaitForContinue,
      ScanLeft,
      ScanRight,
      ScanCenter,
      Stopped,
      Error,
    }

    private Dictionary<int, ScannedSetupCubeState> _SetupCubeState;
    private bool _CubesStateUpdated;
    private Color _CubeTooCloseColor;
    private ScanPhase _ScanPhase;

    private BlockToCozmoPositionComparerByID _BlockPosComparer;

    public ScanForInitialCubeState(State nextState, int cubesRequired, Color CubeTooCloseColor) : base(nextState, cubesRequired) {
      _SetupCubeState = new Dictionary<int, ScannedSetupCubeState>();
      _CubesStateUpdated = false;
      _CubeTooCloseColor = CubeTooCloseColor;
      _BlockPosComparer = new BlockToCozmoPositionComparerByID(_CurrentRobot);
    }

    public override void Enter() {
      base.Enter();

      foreach (KeyValuePair<int, LightCube> lightCube in _CurrentRobot.LightCubes) {
        lightCube.Value.SetLEDsOff();
      }

      SetScanPhase(ScanPhase.NoCubesSeen);
      InitShowCubesSlide();
    }

    // ignore base class events
    protected override void CheckForNewlySeenCubes() {
    }

    public override void Update() {
      // Intentionally avoid base class since that will only check currently visible cubes
      if (_ScanPhase == ScanPhase.NoCubesSeen) {
        int visibleLightCount = _CurrentRobot.VisibleLightCubes.Count;
        if (visibleLightCount > 0) {
          UpdateScannedCubes();
          // If Cozmo can see all at once, we know it's too close
          if (visibleLightCount == _CubesRequired) {
            SetScanPhase(ScanPhase.Error);
          }
          else {
            SetScanPhase(ScanPhase.WaitForContinue);
          }
        }
      }
      else if (_ScanPhase != ScanPhase.Error) {
        UpdateScannedCubes();
        if (_CubesStateUpdated) {
          if (_Game.CubeIdsForGame.Count == _CubesRequired) {
            int readyCubes = 0;
            int closeCubes = 0;
            foreach (KeyValuePair<int, ScannedSetupCubeState> cubeState in _SetupCubeState) {
              if (cubeState.Value == ScannedSetupCubeState.Ready) {
                readyCubes++;
              }
              else if (cubeState.Value == ScannedSetupCubeState.TooClose) {
                closeCubes++;
              }
            }
            if (readyCubes == _CubesRequired) {
              SetScanPhase(ScanPhase.Stopped);
            }
            else if (closeCubes != 0) {
              SetScanPhase(ScanPhase.Error);
            }
          }
          _CubesStateUpdated = false;
          UpdateUI(_Game.CubeIdsForGame.Count);
        }
      }
    }

    private void UpdateSetupLightState(int cubeID, ScannedSetupCubeState state) {
      if (state != _SetupCubeState[cubeID]) {
        _SetupCubeState[cubeID] = state;
        _CubesStateUpdated = true;
        LightCube cube = _CurrentRobot.LightCubes[cubeID];
        if (state == ScannedSetupCubeState.Seen) {
          cube.SetLEDs(Cozmo.CubePalette.InViewColor.lightColor);
          Anki.Cozmo.Audio.GameAudioClient.PostSFXEvent(Anki.Cozmo.Audio.GameEvent.SFX.GameSharedBlockConnect);
        }
        else if (state == ScannedSetupCubeState.TooClose) {
          cube.SetLEDs(_CubeTooCloseColor);
        }
        else if (state == ScannedSetupCubeState.Ready) {
          cube.SetLEDs(Cozmo.CubePalette.ReadyColor.lightColor);
        }
      }
    }

    private ScannedSetupCubeState GetCubeDistance(LightCube cubeA, LightCube cubeB) {
      float dist = Vector3.Distance(cubeA.WorldPosition, cubeB.WorldPosition);
      if (dist < _kMinDistMM) {
        return ScannedSetupCubeState.TooClose;
      }
      return ScannedSetupCubeState.Ready;
    }

    private void UpdateSetupCubeState(int checkIndex) {
      if (_Game.CubeIdsForGame.Count == 1) {
        UpdateSetupLightState(_Game.CubeIdsForGame[checkIndex], ScannedSetupCubeState.Seen);
      }
      else if (checkIndex == 0) {
        // One to your right.
        LightCube currCube = _CurrentRobot.LightCubes[_Game.CubeIdsForGame[checkIndex]];
        LightCube otherCube = _CurrentRobot.LightCubes[_Game.CubeIdsForGame[checkIndex + 1]];
        UpdateSetupLightState(currCube, GetCubeDistance(currCube, otherCube));
      }
      else if (checkIndex == _Game.CubeIdsForGame.Count - 1) {
        // One to your left
        LightCube currCube = _CurrentRobot.LightCubes[_Game.CubeIdsForGame[checkIndex]];
        LightCube otherCube = _CurrentRobot.LightCubes[_Game.CubeIdsForGame[checkIndex - 1]];
        UpdateSetupLightState(currCube.ID, GetCubeDistance(currCube, otherCube));
      }
      else {
        // Check both right and left for cubes in the middle.
        LightCube currCube = _CurrentRobot.LightCubes[_Game.CubeIdsForGame[checkIndex]];
        LightCube leftCube = _CurrentRobot.LightCubes[_Game.CubeIdsForGame[checkIndex - 1]];
        LightCube rightCube = _CurrentRobot.LightCubes[_Game.CubeIdsForGame[checkIndex + 1]];
        ScannedSetupCubeState cubeLeftState = GetCubeDistance(currCube, leftCube);
        ScannedSetupCubeState cubeRightState = GetCubeDistance(currCube, rightCube);
        if (cubeLeftState != ScannedSetupCubeState.Ready) {
          UpdateSetupLightState(currCube.ID, cubeLeftState);
        }
        else if (cubeRightState != ScannedSetupCubeState.Ready) {
          UpdateSetupLightState(currCube.ID, cubeRightState);
        }
        else {
          UpdateSetupLightState(currCube.ID, ScannedSetupCubeState.Ready);
        }
      }
    }

    protected void UpdateScannedCubes() {
      LightCube cube = null;
      foreach (KeyValuePair<int, LightCube> lightCube in _CurrentRobot.LightCubes) {
        cube = lightCube.Value;

        if (cube.MarkersVisible) {
          if (!_Game.CubeIdsForGame.Contains(cube.ID)) {
            if (_Game.CubeIdsForGame.Count < _CubesRequired) {
              _Game.CubeIdsForGame.Add(cube.ID);
              _SetupCubeState.Add(cube.ID, ScannedSetupCubeState.Unknown);
              cube.SetLEDs(Cozmo.CubePalette.InViewColor.lightColor);
            }
          }
        }

      }

      // Theres only 3 cubes, so shouldn't take that long.
      // And they tend to shift slowly enough where they won't get real move messages.
      _Game.CubeIdsForGame.Sort(_BlockPosComparer);

      for (int i = 0; i < _Game.CubeIdsForGame.Count; ++i) {
        UpdateSetupCubeState(i);
      }

    }

    protected override void UpdateUI(int numValidCubes) {
      if (_ShowCozmoCubesSlide != null) {
        switch (numValidCubes) {
        case 1:
          // Start with lighting up the center specifically.
          _ShowCozmoCubesSlide.LightUpCubes(new List<int> { 1 });
          break;
        case 2:
          // Start with lighting up the center specifically.
          _ShowCozmoCubesSlide.LightUpCubes(new List<int> { 1, 2 });
          break;
        default:
          _ShowCozmoCubesSlide.LightUpCubes(numValidCubes);
          break;
        }
      }
    }

    protected override void HandleContinueButtonClicked() {
      if (_ScanPhase == ScanPhase.WaitForContinue) {
        SetScanPhase(ScanPhase.ScanLeft);
      }
      else if (_ScanPhase == ScanPhase.Error) {
        SetScanPhase(ScanPhase.NoCubesSeen);
      }
      else {
        base.HandleContinueButtonClicked();
      }
    }

    private void InitShowCubesSlide() {
      if (_ShowCozmoCubesSlide == null) {
        _ShowCozmoCubesSlide = _Game.SharedMinigameView.ShowCozmoCubesSlide(_CubesRequired);
      }
      _ShowCozmoCubesSlide.SetLabelText(Localization.Get(LocalizationKeys.kSimonGameLabelPlaceCenter));
      _ShowCozmoCubesSlide.SetCubeSpacing(100);
    }

    private void SetScanPhase(ScanPhase nextState) {
      if (_ScanPhase != nextState) {
        // clean up previous
        if (_ScanPhase == ScanPhase.Error) {
          InitShowCubesSlide();
          // Reset for another scan since hopefully they moved them
          _Game.CubeIdsForGame.Clear();
          _SetupCubeState.Clear();
          // Reset to seen so we don't error out immediately again and scan for being too close...
          foreach (KeyValuePair<int, LightCube> lightCube in _CurrentRobot.LightCubes) {
            lightCube.Value.SetLEDsOff();
          }
        }

        // setup next state...
        if (nextState == ScanPhase.NoCubesSeen) {
          _Game.SharedMinigameView.EnableContinueButton(false);
        }
        else if (nextState == ScanPhase.WaitForContinue) {
          _Game.SharedMinigameView.EnableContinueButton(true);
        }
        else if (nextState == ScanPhase.ScanLeft || nextState == ScanPhase.ScanCenter) {
          _Game.SharedMinigameView.EnableContinueButton(false);
          const float kLeftScanDeg = 45.0f;
          _CurrentRobot.TurnInPlace(Mathf.Deg2Rad * kLeftScanDeg, SimonGame.kTurnSpeed_rps, SimonGame.kTurnAccel_rps2, HandleTurnFinished);
          _ShowCozmoCubesSlide.RotateCozmoImageTo(kLeftScanDeg, _kRotateSec);
        }
        else if (nextState == ScanPhase.ScanRight) {
          _Game.SharedMinigameView.EnableContinueButton(false);
          // Half speed since going further
          const float kRightScanDeg = -90.0f;
          _CurrentRobot.TurnInPlace(Mathf.Deg2Rad * kRightScanDeg, SimonGame.kTurnSpeed_rps / 2, SimonGame.kTurnAccel_rps2, HandleTurnFinished);
          // Half of the total Degrees cozmo rotates since these are absolute          
          _ShowCozmoCubesSlide.RotateCozmoImageTo(kRightScanDeg / 2.0f, _kRotateSec);
        }
        else if (nextState == ScanPhase.Stopped) {
          // Rotate towards center
          _ShowCozmoCubesSlide.RotateCozmoImageTo(0.0f, _kRotateSec);
          _CurrentRobot.TurnTowardsObject(_CurrentRobot.LightCubes[_Game.CubeIdsForGame[1]], false);
          _Game.SharedMinigameView.EnableContinueButton(true);
        }
        else if (nextState == ScanPhase.Error) {
          _ShowCozmoCubesSlide = null;
          _Game.SharedMinigameView.EnableContinueButton(true);
          if (_Game.CubeIdsForGame.Count > 1) {
            _CurrentRobot.TurnTowardsObject(_CurrentRobot.LightCubes[_Game.CubeIdsForGame[1]], false);
          }
          SimonGame simonGame = _Game as SimonGame;
          _Game.SharedMinigameView.ShowWideGameStateSlide(
                                                     simonGame.SimonSetupErrorPrefab.gameObject, "simon_error_slide");
        }

        _ScanPhase = nextState;
        _Game.SharedMinigameView.SetContinueButtonSupplementText(GetWaitingForCubesText(_CubesRequired), Cozmo.UI.UIColorPalette.NeutralTextColor);
      }
    }

    private void HandleTurnFinished(bool success) {
      // Stopped or Error could have taken over
      if (_ScanPhase == ScanPhase.ScanLeft) {
        SetScanPhase(ScanPhase.ScanRight);
      }
      else if (_ScanPhase == ScanPhase.ScanRight) {
        SetScanPhase(ScanPhase.ScanCenter);
      }
      else if (_ScanPhase == ScanPhase.ScanCenter) {
        SetScanPhase(ScanPhase.ScanLeft);
      }
    }

    protected override string GetWaitingForCubesText(int numCubes) {
      if (_ScanPhase == ScanPhase.NoCubesSeen || _ScanPhase == ScanPhase.WaitForContinue) {
        return Localization.Get(LocalizationKeys.kSimonGameLabelWaitingForCubesPlaceCenter);
      }
      else if (_ScanPhase == ScanPhase.Error) {
        return "";
      }
      return Localization.Get(LocalizationKeys.kSimonGameLabelWaitingForCubesScanning);
    }


  }
}
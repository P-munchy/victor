﻿using UnityEngine;
using System.Collections;
using Cozmo.MinigameWidgets;

namespace Cozmo {
  namespace Minigame {
    namespace DroneMode {
      public class DroneModeGame : GameBase {
        private const float _kCalculateSteeringInputInterval_ms = 100f;
        private const float _kChangedTurnDirectionThreshold = 0.0001f;

        public delegate void TurnDirectionChangedHandler(float newNormalizedPitch);
        public event System.Action<float> OnTurnDirectionChanged;

        [SerializeField]
        private DroneModeView _DroneModeViewPrefab;
        public DroneModeView DroneModeViewPrefab { get { return _DroneModeViewPrefab; } }

        private DroneModeConfig _DroneModeConfig;

        public float MaxReverseSpeed_mmps { get { return _DroneModeConfig.MaxReverseSpeed_mmps; } }

        public float MaxForwardSpeed_mmps { get { return _DroneModeConfig.MaxForwardSpeed_mmps; } }

        public float PointTurnSpeed_mmps { get { return _DroneModeConfig.PointTurnSpeed_mmps; } }

        public float TurboSpeed_mmps { get { return _DroneModeConfig.TurboSpeed_mmps; } }

        public float HeadMovementSpeed_radps { get { return _DroneModeConfig.HeadMovementSpeed_radps; } }

        public float NeutralTiltSize { get { return _DroneModeConfig.NeutralTiltSize; } }

        public float StartingLiftHeight { get { return _DroneModeConfig.StartingLiftHeight; } }

        private float _CurrentTurnDirection;
        private IEnumerator _SteeringInputCoroutine;

        protected override void InitializeGame(MinigameConfigBase minigameConfigData) {
          _DroneModeConfig = minigameConfigData as DroneModeConfig;
          InitializeRobot();
          InitializeStateMachine();
        }

        private void InitializeStateMachine() {
          DroneModeShowInstructionsState instructionState = new DroneModeShowInstructionsState();
          _StateMachine.SetNextState(instructionState);
        }

        private void InitializeRobot() {
          CurrentRobot.VisionWhileMoving(true);
          CurrentRobot.SetVisionMode(Anki.Cozmo.VisionMode.DetectingFaces, true);
          CurrentRobot.SetVisionMode(Anki.Cozmo.VisionMode.DetectingMarkers, true);
          CurrentRobot.SetVisionMode(Anki.Cozmo.VisionMode.DetectingMotion, false);
          CurrentRobot.SetEnableFreeplayBehaviorChooser(false);
        }

        protected override void SetupViewAfterCozmoReady(SharedMinigameView newView, ChallengeData data) {
          // TODO
        }
        protected override void CleanUpOnDestroy() {
          // TODO
        }

        public void EnableTiltInput() {
          if (_SteeringInputCoroutine == null) {
            _SteeringInputCoroutine = SendSteeringInput();
            StartCoroutine(_SteeringInputCoroutine);
          }
        }

        public void DisableTiltInput() {
          if (_SteeringInputCoroutine != null) {
            StopCoroutine(_SteeringInputCoroutine);
            _SteeringInputCoroutine = null;
          }
        }

        private IEnumerator SendSteeringInput() {
          while (true) {
            float newDevicePitch = (float)Mathf.Atan(Input.acceleration.x / Mathf.Sqrt(Mathf.Pow(Input.acceleration.y, 2) + Mathf.Pow(Input.acceleration.z, 2)));
            float normalizedTurnDirection = MapDevicePitchToTurnDirection(newDevicePitch);

            if (!float.IsNaN(normalizedTurnDirection) && !normalizedTurnDirection.IsNear(_CurrentTurnDirection, _kChangedTurnDirectionThreshold)) {
              _CurrentTurnDirection = normalizedTurnDirection;
              if (OnTurnDirectionChanged != null) {
                OnTurnDirectionChanged(_CurrentTurnDirection);
              }
            }
            yield return new WaitForSeconds(_kCalculateSteeringInputInterval_ms / 1000.0f);
          }
        }

        private float MapDevicePitchToTurnDirection(float newDevicePitch) {
          float normalizedPitch = 0f;
          newDevicePitch = Mathf.Clamp(newDevicePitch, -1f, 1f);
          float negativeThreshold = -NeutralTiltSize;
          if (newDevicePitch < negativeThreshold) {
            float difference = newDevicePitch - negativeThreshold;
            normalizedPitch = difference / Mathf.Abs(-1 - negativeThreshold);
          }
          else if (newDevicePitch > NeutralTiltSize) {
            float difference = newDevicePitch - NeutralTiltSize;
            normalizedPitch = difference / (1 - NeutralTiltSize);
          }
          return normalizedPitch;
        }

        public float CalculateDriveWheelSpeed(DroneModeView.SpeedSliderSegment sliderSegment, float sliderSegmentValue) {
          float driveWheelSpeed_mmps = 0f;
          switch (sliderSegment) {
          case DroneModeView.SpeedSliderSegment.Turbo:
            driveWheelSpeed_mmps = TurboSpeed_mmps;
            break;
          case DroneModeView.SpeedSliderSegment.Forward:
            driveWheelSpeed_mmps = MaxForwardSpeed_mmps * sliderSegmentValue;
            break;
          case DroneModeView.SpeedSliderSegment.Reverse:
            driveWheelSpeed_mmps = MaxReverseSpeed_mmps * sliderSegmentValue * -1;
            break;
          default:
            driveWheelSpeed_mmps = 0f;
            break;
          }
          return driveWheelSpeed_mmps;
        }

        public float CalculateDriveHeadSpeed(DroneModeView.HeadSliderSegment sliderSegment, float sliderSegmentValue) {
          float driveHeadSpeed_radps = 0f;
          switch (sliderSegment) {
          case DroneModeView.HeadSliderSegment.Forward:
            driveHeadSpeed_radps = HeadMovementSpeed_radps * sliderSegmentValue;
            break;
          case DroneModeView.HeadSliderSegment.Reverse:
            driveHeadSpeed_radps = HeadMovementSpeed_radps * sliderSegmentValue * -1;
            break;
          default:
            driveHeadSpeed_radps = 0f;
            break;
          }
          return driveHeadSpeed_radps;
        }
      }
    }
  }
}
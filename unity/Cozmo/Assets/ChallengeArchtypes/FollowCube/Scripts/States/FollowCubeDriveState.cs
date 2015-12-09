﻿using UnityEngine;
using System.Collections;

namespace FollowCube {

  public class FollowCubeDriveState : State {

    private LightCube _TargetCube;
    private FollowCubeGame _GameInstance;

    private float _LastSeenTargetTime = -1;

    private float _PreviousAnglePose;
    private float _TotalRadiansTraveled;

    public override void Enter() {
      base.Enter();

      _GameInstance = _StateMachine.GetGame() as FollowCubeGame;

      _CurrentRobot.SetHeadAngle(0);
      _CurrentRobot.SetLiftHeight(0);

      _PreviousAnglePose = _CurrentRobot.PoseAngle;
      _TotalRadiansTraveled = 0;
    }

    public override void Update() {
      base.Update();

      // Try to find a target
      if (_TargetCube == null) {
        _TargetCube = FindClosestLightCube();
      }

      // If there is a target, and it's currently in view, follow it.
      if (_TargetCube != null) {
        if (IsCubeVisible(_TargetCube)) {
          ResetUnseenTimestamp();

          _TargetCube.SetLEDs(Color.white);
          FollowCube(_TargetCube);

          // Keep track of any change in direction.
          float deltaRadians = _PreviousAnglePose - _CurrentRobot.PoseAngle;
          // Disregard a huge change in rotation, because that means he's passing the 
          // border from -pi to pi.
          if (Mathf.Abs(deltaRadians) < 1) {
            _TotalRadiansTraveled += deltaRadians;
          }
          _PreviousAnglePose = _CurrentRobot.PoseAngle;

          // If we have turned fully around in either direction, the player wins.
          _GameInstance.Progress = (Mathf.Abs(_TotalRadiansTraveled) / Mathf.PI * 2) * (1.0f / _GameInstance.NumSegments) + (4.0f / _GameInstance.NumSegments);
          if (Mathf.Abs(_TotalRadiansTraveled) > Mathf.PI * 2) {
            AnimationState animState = new AnimationState();
            animState.Initialize(AnimationName.kMajorWin, HandleWinAnimationDone);
            _StateMachine.SetNextState(animState);
          }
        }
        else {
          // Keep track of when Cozmo first loses track of the block
          if (IsUnseenTimestampUninitialized()) {
            _LastSeenTargetTime = Time.time;
          }

          if (Time.time - _LastSeenTargetTime > _GameInstance.NotSeenForgivenessThreshold) {
            ResetUnseenTimestamp();
            // register a failed attempt because it's been too long since
            // we've seen a block.
            _TargetCube.TurnLEDsOff();
            _TargetCube = null;
            _StateMachine.SetNextState(new FollowCubeFailedState());
          }
          else {
            // Continue trying to follow the cube
            FollowCube(_TargetCube);
          }
        }
      }
      else {
        // Don't move until we find a cube.
        _CurrentRobot.DriveWheels(0.0f, 0.0f);
      }
    }

    private void HandleWinAnimationDone(bool success) {
      _StateMachine.GetGame().RaiseMiniGameWin();
    }

    private LightCube FindClosestLightCube() {
      ObservedObject closest = null;
      float dist = float.MaxValue;
      foreach (ObservedObject obj in _CurrentRobot.VisibleObjects) {
        if (obj is LightCube) {
          float d = Vector3.Distance(_CurrentRobot.WorldPosition, obj.WorldPosition);
          if (d < dist) {
            dist = d;
            closest = obj;
          }
        }
      }
      LightCube cube = closest as LightCube;
      return cube;
    }

    private bool IsCubeVisible(LightCube cube) {
      return _CurrentRobot.VisibleObjects.Contains(cube);
    }

    void FollowCube(LightCube target) {
      float dist = Vector3.Distance(_CurrentRobot.WorldPosition, target.WorldPosition);
      float angle = Vector2.Angle(_CurrentRobot.Forward, target.WorldPosition - _CurrentRobot.WorldPosition);
      float speed = _GameInstance.ForwardSpeed;
      if (angle < 10.0f) {
        float distMax = _GameInstance.DistanceMax;
        float distMin = _GameInstance.DistanceMin;

        if (dist > distMax) {
          _CurrentRobot.DriveWheels(speed, speed);
        }
        else if (dist < distMin) {
          _CurrentRobot.DriveWheels(-speed, -speed);
        }
        else {
          _CurrentRobot.DriveWheels(0.0f, 0.0f);
        }
      }
      else {
        // we need to turn to face it
        bool shouldTurnRight = ShouldTurnRight(target);
        if (shouldTurnRight) {
          _CurrentRobot.DriveWheels(speed, -speed);
        }
        else {
          _CurrentRobot.DriveWheels(-speed, speed);
        }
      }

    }

    private bool ShouldTurnRight(ObservedObject followBlock) {
      float turnAngle = Vector3.Cross(_CurrentRobot.Forward, followBlock.WorldPosition - _CurrentRobot.WorldPosition).z;
      return (turnAngle < 0.0f);
    }

    private bool IsUnseenTimestampUninitialized() {
      return _LastSeenTargetTime == -1;
    }

    private void ResetUnseenTimestamp() {
      _LastSeenTargetTime = -1;
    }
  }

}

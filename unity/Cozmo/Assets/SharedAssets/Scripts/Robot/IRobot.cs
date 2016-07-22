﻿using UnityEngine;
using System;
using System.Collections;
using System.Collections.Generic;
using Anki.Cozmo;
using Anki.Cozmo.ExternalInterface;
using Cozmo.Util;
using G2U = Anki.Cozmo.ExternalInterface;

public delegate void RobotCallback(bool success);
public delegate void FriendshipLevelUp(int newLevel);

public interface ILight {
  uint OnColor { get; set; }

  uint OffColor { get; set; }

  uint OnPeriodMs { get; set; }

  uint OffPeriodMs { get; set; }

  uint TransitionOnPeriodMs { get; set; }

  uint TransitionOffPeriodMs { get; set; }

  void SetLastInfo();

  bool Changed { get; }

  void ClearData();
}

// Interface for Robot so we can mock it in unit tests
public interface IRobot : IDisposable {

  byte ID { get; }

  float HeadAngle { get; }

  float PoseAngle { get; }

  float PitchAngle { get; }

  float LeftWheelSpeed { get; }

  float RightWheelSpeed { get; }

  float LiftHeight { get; }

  float LiftHeightFactor { get; }

  Vector3 WorldPosition { get; }

  Quaternion Rotation { get; }

  Vector3 Forward { get; }

  Vector3 Right { get; }

  RobotStatusFlag RobotStatus { get; }

  GameStatusFlag GameStatus { get; }

  float BatteryVoltage { get; }

  Dictionary<int, LightCube> LightCubes { get; }

  List<LightCube> VisibleLightCubes { get; }

  List<Face> Faces { get; }

  Dictionary<int, string> EnrolledFaces { get; set; }

  float[] EmotionValues { get; }

  ILight[] BackpackLights { get; }

  bool IsSparked { get; }

  Anki.Cozmo.UnlockId SparkUnlockId { get; }

  int CarryingObjectID { get; }

  int HeadTrackingObjectID { get; }

  string CurrentBehaviorString { get; set; }

  string CurrentDebugAnimationString { get; set; }

  ObservedObject CarryingObject { get; }

  event Action<ObservedObject> OnCarryingObjectSet;

  ObservedObject HeadTrackingObject { get; }

  event Action<ObservedObject> OnHeadTrackingObjectSet;

  bool Status(RobotStatusFlag s);

  bool IsLocalized();

  Vector3 WorldToCozmo(Vector3 worldSpacePosition);

  bool IsLightCubeInPickupRange(LightCube lightCube);

  void ResetRobotState(Action onComplete);

  void TryResetHeadAndLift(Action onComplete);

  void ClearData(bool initializing = false);

  ObservedObject GetObservedObjectById(int id);

  LightCube GetLightCubeWithFactoryID(uint factoryID);

  ObservedObject GetObservedObjectWithFactoryID(uint factoryID);

  void VisualizeQuad(Vector3 lowerLeft, Vector3 upperRight);

  void AddToEmotion(Anki.Cozmo.EmotionType type, float deltaValue, string source);

  void SetEmotion(Anki.Cozmo.EmotionType type, float value);

  void SetCalibrationData(float focalLengthX, float focalLengthY, float centerX, float centerY);

  void SetEnableCliffSensor(bool enabled);

  void EnableSparkUnlock(Anki.Cozmo.UnlockId id);

  void StopSparkUnlock();

  // enable/disable games available for Cozmo to request
  void SetAvailableGames(BehaviorGameFlag games);

  void DisplayProceduralFace(float faceAngle, Vector2 faceCenter, Vector2 faceScale, float[] leftEyeParams, float[] rightEyeParams);

  void DriveHead(float speed_radps);

  void DriveWheels(float leftWheelSpeedMmps, float rightWheelSpeedMmps);

  void PlaceObjectOnGroundHere(RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW);

  void PlaceObjectRel(ObservedObject target, float offsetFromMarker, float approachAngle, RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW);

  void PlaceOnObject(ObservedObject target, float approachAngle, RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW);

  void CancelAction(RobotActionType actionType = RobotActionType.UNKNOWN);

  void CancelCallback(RobotCallback callback);

  void CancelAllCallbacks();

  void EnrollNamedFace(int faceID, string name, Anki.Cozmo.FaceEnrollmentSequence seq = Anki.Cozmo.FaceEnrollmentSequence.Default, bool saveToRobot = true, RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW);

  void SendAnimationTrigger(AnimationTrigger animTriggerEvent, RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW);

  void SetIdleAnimation(AnimationTrigger default_anim);
  void PushIdleAnimation(AnimationTrigger default_anim);
  void PopIdleAnimation();

  void PopDrivingAnimations();

  void PushDrivingAnimations(AnimationTrigger drivingStartAnim, AnimationTrigger drivingLoopAnim, AnimationTrigger drivingEndAnim);

  void SetLiveIdleAnimationParameters(Anki.Cozmo.LiveIdleAnimationParameter[] paramNames, float[] paramValues, bool setUnspecifiedToDefault = false);

  float GetHeadAngleFactor();

  void SetHeadAngle(float angleFactor = -0.8f,
                    RobotCallback callback = null,
                    QueueActionPosition queueActionPosition = QueueActionPosition.NOW,
                    bool useExactAngle = false,
                    float accelRadSec = 2f,
                    float maxSpeedFactor = 1f);

  void SetRobotVolume(float volume);

  float GetRobotVolume();

  void TrackToObject(ObservedObject observedObject, bool headOnly = true);

  void StopTrackToObject();

  void TurnTowardsObject(ObservedObject observedObject, bool headTrackWhenDone = true, float maxPanSpeed_radPerSec = 4.3f, float panAccel_radPerSec2 = 10f,
                         RobotCallback callback = null,
                         QueueActionPosition queueActionPosition = QueueActionPosition.NOW);

  void TurnTowardsFacePose(Face face, float maxPanSpeed_radPerSec = 4.3f, float panAccel_radPerSec2 = 10f,
                           RobotCallback callback = null,
                           QueueActionPosition queueActionPosition = QueueActionPosition.NOW);

  void TurnTowardsLastFacePose(float maxTurnAngle, bool sayName = false, RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW);

  void PickupObject(ObservedObject selectedObject, bool usePreDockPose = true, bool useManualSpeed = false, bool useApproachAngle = false, float approachAngleRad = 0.0f, RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW);

  void RollObject(ObservedObject selectedObject, bool usePreDockPose = true, bool useManualSpeed = false, RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW);

  void PlaceObjectOnGround(Vector3 position, Quaternion rotation, bool level = false, bool useManualSpeed = false, RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW);

  void GotoPose(Vector3 position, Quaternion rotation, bool level = false, bool useManualSpeed = false, RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW);

  void GotoPose(float x_mm, float y_mm, float rad, bool level = false, bool useManualSpeed = false, RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW);

  void GotoObject(ObservedObject obj, float distance_mm, bool goToPreDockPose = false, RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW);

  void AlignWithObject(ObservedObject obj, float distanceFromMarker_mm, RobotCallback callback = null, bool useApproachAngle = false, bool usePreDockPose = false, float approachAngleRad = 0.0f, AlignmentType alignmentType = AlignmentType.CUSTOM, QueueActionPosition queueActionPosition = QueueActionPosition.NOW);

  LightCube GetClosestLightCube();

  void SearchForCube(LightCube cube, RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW);

  void SearchForObject(ObjectFamily objectFamily, int objectId, bool matchAnyObject, RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW);

  void SetLiftHeight(float heightFactor, RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW);

  void SetRobotCarryingObject(int objectID = -1);

  void ClearAllBlocks();

  void ClearAllObjects();

  void VisionWhileMoving(bool enable);

  void TurnInPlace(float angle_rad, float speed_rad_per_sec, float accel_rad_per_sec2, RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW);

  void TraverseObject(int objectID, bool usePreDockPose = false, bool useManualSpeed = false);

  void SetVisionMode(VisionMode mode, bool enable);

  void RequestSetUnlock(Anki.Cozmo.UnlockId unlockID, bool unlocked);

  void SetEnableSOSLogging(bool enable);

  void ExecuteBehavior(BehaviorType type);

  void SetEnableFreeplayBehaviorChooser(bool enable);

  void ActivateBehaviorChooser(BehaviorChooserType behaviorChooserType);

  void TurnOffBackpackLED(LEDId ledToChange);

  void SetAllBackpackBarLED(Color color);

  void SetAllBackpackBarLED(uint colorUint);

  void TurnOffAllBackpackBarLED();

  void SetBackpackBarLED(LEDId ledToChange, Color color);

  void SetBackbackArrowLED(LEDId ledId, float intensity);

  void SetFlashingBackpackLED(LEDId ledToChange, Color color, uint onDurationMs = 200, uint offDurationMs = 200, uint transitionDurationMs = 0);

  void TurnOffAllLights(bool now = false);

  void UpdateLightMessages(bool now = false);

  ObservedObject GetCharger();

  void MountCharger(ObservedObject charger, RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW);

  #region PressDemoMessages

  void TransitionToNextDemoState();

  void WakeUp(bool withEdge);

  #endregion

  void SayTextWithEvent(string text, AnimationTrigger playEvent, SayTextStyle style = SayTextStyle.Normal, RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW);

  void SendDemoResetState();

  void UpdateEnrolledFaceByID(int faceID, string oldFaceName, string newFaceName);

  void EraseAllEnrolledFaces();

  void EraseEnrolledFaceByID(int faceID);

  void LoadFaceAlbumFromFile(string path, bool isPathRelative = true);

}
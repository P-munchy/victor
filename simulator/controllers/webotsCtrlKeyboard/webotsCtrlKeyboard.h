/*
 * File:          webotsCtrlKeyboard.cpp
 * Date:
 * Description:
 * Author:
 * Modifications:
 */

#ifndef __webotsCtrlKeyboard_H_
#define __webotsCtrlKeyboard_H_

#include "simulator/game/uiGameController.h"

namespace Anki {
namespace Cozmo {

class WebotsKeyboardController : public UiGameController {
public:
  WebotsKeyboardController(s32 step_time_ms);

  // Called before WaitOnKeyboardToConnect (and also before Init), sets up the basics needed for
  // WaitOnKeyboardToConnect, including enabling the keyboard
  void PreInit();
  
  // if the corresponding proto field is set to true, this function will wait until the user presses
  // Shift+enter to return.This can be used to allow unity to connect. If we notice another connection
  // attempt, we will exit the keyboard controller. This is called before Init
  void WaitOnKeyboardToConnect();

protected:
  bool RegisterKeyFcn(int key, int modifier, std::function<void()> fcn, const char* help_msg, std::string display_string = "");
  void ProcessKeystroke();
  void ProcessKeyPressFunction(int key, int modifier);
  
  // === Key press functions ===
  void PrintHelp();
  
  void LogRawProxData();
  void ToggleVizDisplay();
  void TogglePoseMarkerMode();
  void PlayNeedsGetOutAnimIfNeeded();
  void GotoPoseMarker();
  
  void ToggleEngineLightComponent();
  void SearchForNearbyObject();
  void ToggleCliffSensorEnable();
  void ToggleTestBackpackLights();
  
  void ToggleTrackToFace();
  void ToggleTrackToObject();
  void TrackPet();
  void ExecuteTestPlan();
  
  void ToggleCubeAccelStreaming();
  void ExecuteBehavior();
  void LogCliffSensorData();
  
  void FakeCloudIntent();
  
  void NVStorage_EraseTag();
  void NVStorage_ReadTag();
  void SetEmotion();
  
  void PickOrPlaceObject();
  void MountSelectedCharger();
  
  void PopAWheelie();
  void RollObject();
  
  void SetControllerGains();
  
  void ToggleVisionWhileMoving();
  void SetRobotVolume();
  
  void SetActiveObjectLights();
  
  void AlignWithObject();
  void TurnTowardsObject();
  void GotoObject();
  void RequestIMUData();
  
  void AssociateNameWithCurrentFace();
  void TurnTowardsFace();
  void EraseLastObservedFace();
  void ToggleFaceDetection();
  
  void DenyGameStart();
  void FillNeedsMeters();
  void SetUnlock();
  
  void ToggleImageStreaming();
  void ToggleEyeRendering();

  void FlipSelectedBlock();
  
  void RequestSingleImageToGame();
  void ToggleImageStreamingToGame();
  void SaveSingleImage();
  void ToggleImageSaving();
  void ToggleImageAndStateSaving();
  
  void TurnInPlaceCCW();
  void TurnInPlaceCW();
  
  void ExecutePlaypenTest();
  void ToggleSendAvailableObjects();
  
  void ReadCameraCalibration();
  void ReadGameSkills();
  void ReadMfgTestData();
  
  void SetFaceDisplayHue();
  void SendRandomProceduralFace();
  
  void PushIdleAnimation();
  void PlayAnimation();
  void PlayAnimationGroup();
  
  void RunDebugConsoleFunc();
  void SetDebugConsoleVar();

  void SetRollActionParams();
  void SetCameraSettings();
  void SayText();
  void PlayCubeAnimation();
  
  void TurnTowardsImagePoint();
  void QuitKeyboardController();
  void ToggleLiftPower();
  
  void MoveLiftToLowDock();
  void MoveLiftToHighDock();
  void MoveLiftToCarryHeight();
  
  void MoveHeadToLowLimit();
  void MoveHeadToHorizontal();
  void MoveHeadToHighLimit();
  
  void MoveHeadUp();
  void MoveHeadDown();
  void MoveLiftUp();
  void MoveLiftDown();
  
  void DriveForward();
  void DriveBackward();
  void DriveLeft();
  void DriveRight();
  
  void ExecuteRobotTestMode();
  void PressBackButton();
  
  // ==== End of key press functions ====
  
  
  f32 GetLiftSpeed_radps();
  f32 GetLiftAccel_radps2();
  f32 GetLiftDuration_sec();
  f32 GetHeadSpeed_radps();
  f32 GetHeadAccel_radps2();
  f32 GetHeadDuration_sec();
  
  void TestLightCube();
  
  Pose3d GetGoalMarkerPose();
  
  virtual void InitInternal() override;
  virtual s32 UpdateInternal() override;

  virtual void HandleImageChunk(const ImageChunk& msg) override;
  virtual void HandleRobotObservedObject(const ExternalInterface::RobotObservedObject& msg) override;
  virtual void HandleRobotObservedFace(const ExternalInterface::RobotObservedFace& msg) override;
  virtual void HandleRobotObservedPet(const ExternalInterface::RobotObservedPet& msg) override;
  virtual void HandleDebugString(const ExternalInterface::DebugString& msg) override;
  virtual void HandleNVStorageOpResult(const ExternalInterface::NVStorageOpResult& msg) override;
  virtual void HandleFaceEnrollmentCompleted(const ExternalInterface::FaceEnrollmentCompleted& msg) override;
  virtual void HandleLoadedKnownFace(const Vision::LoadedKnownFace& msg) override;
  virtual void HandleEngineErrorCode(const ExternalInterface::EngineErrorCodeMessage& msg) override;
  virtual void HandleRobotConnected(const ExternalInterface::RobotConnectionResponse& msg) override;
  
private:

  bool _shouldQuit = false;
  
}; // class WebotsKeyboardController
} // namespace Cozmo
} // namespace Anki

#endif  // __webotsCtrlKeyboard_H_

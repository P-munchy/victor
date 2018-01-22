/*
 * File:          uiGameController.h
 * Date:
 * Description:   Any UI/Game to be run as a Webots controller should be derived from this class.
 * Author:
 * Modifications:
 */

#ifndef __UI_GAME_CONTROLLER_H__
#define __UI_GAME_CONTROLLER_H__

#include "coretech/common/engine/math/pose.h"
#include "coretech/common/engine/math/poseOriginList.h"
#include "coretech/common/engine/objectIDs.h"
#include "engine/aiComponent/behaviorComponent/behaviors/iCozmoBehavior_fwd.h"
#include "engine/robot.h"
#include "engine/cozmoAPI/comms/gameComms.h"
#include "engine/cozmoAPI/comms/gameMessageHandler.h"
#include "coretech/common/shared/types.h"
#include "coretech/vision/engine/faceIdTypes.h"
#include "clad/externalInterface/messageEngineToGame.h"
#include "clad/externalInterface/messageGameToEngine.h"
#include "clad/robotInterface/messageEngineToRobot.h"
#include "clad/robotInterface/messageFromAnimProcess.h"
#include "clad/types/imageTypes.h"
#include "clad/types/nvStorageTypes.h"
#include "clad/types/objectFamilies.h"
#include "clad/types/objectTypes.h"
#include "clad/types/robotTestModes.h"
#include "clad/types/visionModes.h"
#include <unordered_set>
#include <webots/Supervisor.hpp>


namespace Anki {
  
  // Forward declaration:
  namespace Util {
    namespace Data {
      class DataPlatform;
    }
  }
  
namespace Cozmo {

class UiGameController {

public:
  typedef struct {
    ObjectFamily family;
    ObjectType   type;
    s32 id;
    f32 area;
    bool isActive;
    
    void Reset() {
      family = ObjectFamily::Unknown;
      type = ObjectType::UnknownObject;
      id = -1;
      area = 0;
      isActive = false;
    }
  } ObservedObject;
  
  
  
  UiGameController(s32 step_time_ms);
  ~UiGameController();
  
  void Init();
  s32 Update();
  
  void SetDataPlatform(const Util::Data::DataPlatform* dataPlatform);
  const Util::Data::DataPlatform* GetDataPlatform() const;
  
  void QuitWebots(s32 status);
  void QuitController(s32 status);
  
  ///
  // @brief      Cycles the viz origin between all observed cubes and the robot itself.
  //
  void CycleVizOrigin();

  ///
  // @brief Update the viz origin to be at the robot.
  //
  void UpdateVizOriginToRobot();
  void UpdateVizOrigin(const Pose3d& originPose);
  
protected:
  
  virtual void InitInternal() {}
  virtual s32 UpdateInternal() = 0;

  void EnableAutoBlockpool(bool enable) { _doAutoBlockPool = enable; }
  
  // TODO: These default handlers and senders should be CLAD-generated!
  
  // Message handlers
  virtual void HandlePing(const ExternalInterface::Ping& msg){};
  virtual void HandleRobotStateUpdate(const ExternalInterface::RobotState& msg){};
  virtual void HandleRobotObservedObject(const ExternalInterface::RobotObservedObject& msg){};
  virtual void HandleRobotObservedFace(const ExternalInterface::RobotObservedFace& msg){};
  virtual void HandleRobotObservedPet(const ExternalInterface::RobotObservedPet& msg) {};
  virtual void HandleRobotDeletedLocatedObject(const ExternalInterface::RobotDeletedLocatedObject& msg){};
  virtual void HandleUiDeviceAvailable(const ExternalInterface::UiDeviceAvailable& msgIn){};
  virtual void HandleUiDeviceConnected(const ExternalInterface::UiDeviceConnected& msg){};
  virtual void HandleRobotConnected(const ExternalInterface::RobotConnectionResponse& msg){};
  virtual void HandleRobotCompletedAction(const ExternalInterface::RobotCompletedAction& msg){};
  virtual void HandleImageChunk(const ImageChunk& msg){};
  virtual void HandleActiveObjectAccel(const ObjectAccel& msg){};
  virtual void HandleActiveObjectConnectionState(const ObjectConnectionState& msg){};
  virtual void HandleActiveObjectMoved(const ObjectMoved& msg){};
  virtual void HandleActiveObjectStoppedMoving(const ObjectStoppedMoving& msg){};
  virtual void HandleActiveObjectTapped(const ObjectTapped& msg){};
  virtual void HandleActiveObjectUpAxisChanged(const ObjectUpAxisChanged& msg){};
  virtual void HandleLocatedObjectStates(const ExternalInterface::LocatedObjectStates& msg){};
  virtual void HandleConnectedObjectStates(const ExternalInterface::ConnectedObjectStates& msg){};
  virtual void HandleAnimationAvailable(const ExternalInterface::AnimationAvailable& msg){};
  virtual void HandleAnimationAborted(const ExternalInterface::AnimationAborted& msg){};
  virtual void HandleDebugString(const ExternalInterface::DebugString& msg){};
  virtual void HandleNVStorageOpResult(const ExternalInterface::NVStorageOpResult& msg){};
  virtual void HandleFactoryTestResultEntry(const FactoryTestResultEntry& msg){};
  virtual void HandleRobotErasedAllEnrolledFaces(const ExternalInterface::RobotErasedAllEnrolledFaces& msg){};
  virtual void HandleLoadedKnownFace(const Vision::LoadedKnownFace& msg){};
  virtual void HandleFaceEnrollmentCompleted(const ExternalInterface::FaceEnrollmentCompleted &msg) {};
  virtual void HandleCliffEvent(const CliffEvent& msg){};
  virtual void HandleSetCliffDetectThresholds(const SetCliffDetectThresholds& msg){};
  
  virtual void HandleBehaviorTransition(const ExternalInterface::BehaviorTransition& msg){};
  virtual void HandleEndOfMessage(const ExternalInterface::EndOfMessage& msg){};
  virtual void HandleRobotOffTreadsStateChanged(const ExternalInterface::RobotOffTreadsStateChanged& msg){};
  virtual void HandleEngineErrorCode(const ExternalInterface::EngineErrorCodeMessage& msg) {};
  virtual void HandleDefinedCustomObject(const ExternalInterface::DefinedCustomObject& msg) {};
  virtual void HandleRobotDeletedAllCustomObjects(const ExternalInterface::RobotDeletedAllCustomObjects& msg) {};
  virtual void HandleRobotDeletedCustomMarkerObjects(const ExternalInterface::RobotDeletedCustomMarkerObjects& msg) {};
  virtual void HandleRobotDeletedFixedCustomObjects(const ExternalInterface::RobotDeletedFixedCustomObjects& msg) {};
  
  // Message senders
  void SendMessage(const ExternalInterface::MessageGameToEngine& msg);
  void SendPing(bool isResponse);
  void SendDriveWheels(const f32 lwheel_speed_mmps, const f32 rwheel_speed_mmps, const f32 lwheel_accel_mmps2, const f32 rwheel_accel_mmps2);
  void SendDriveArc(const f32 speed, const f32 accel, const s16 curvature_mm);
  void SendDriveStraight(f32 speed_mmps,  // Speed should be positive
                         f32 dist_mm,     // Use +ve for forward, -ve for backward
                         bool shouldPlayAnimation = false);

  // SendTurnInPlace returns the IdTag of the queued action:
  uint32_t SendTurnInPlace(const f32 angle_rad,
                           const f32 speed_radPerSec = 0.f,
                           const f32 accel_radPerSec2 = 0.f,
                           const f32 tol_rad = 0.f,  // 0: Use default tolerance of POINT_TURN_ANGLE_TOL
                           const bool isAbsolute = false,
                           const QueueActionPosition queueActionPosition = QueueActionPosition::NOW);

  // Queue a generic action, automatically set robot id, id tag, and position to run now
  void SendAction(const ExternalInterface::QueueSingleAction& msg_in);
  
  void SendTurnInPlaceAtSpeed(const f32 speed_rad_per_sec, const f32 accel_rad_per_sec2);  
  void SendMoveHead(const f32 speed_rad_per_sec);
  void SendMoveLift(const f32 speed_rad_per_sec);
  void SendMoveHeadToAngle(const f32 rad, const f32 speed, const f32 accel, const f32 duration_sec = 0.f);
  void SendMoveLiftToHeight(const f32 mm, const f32 speed, const f32 accel, const f32 duration_sec = 0.f);
  void SendEnableLiftPower(bool enable);
  void SendTapBlockOnGround(const u8 numTaps);
  void SendStopAllMotors();
  void SendImageRequest(ImageSendMode mode);
  void SendSetRobotImageSendMode(ImageSendMode mode);
  void SendSaveImages(ImageSendMode imageMode, const std::string& path = "", const int8_t qualityOnRobot = -1);
  void SendSaveState(bool enabled, const std::string& path = "");
  void SendEnableDisplay(bool on);
  void SendExecutePathToPose(const Pose3d& p,
                             PathMotionProfile motionProf,
                             const bool useManualSpeed);
                             
  void SendGotoObject(const s32 objectID,
                      const f32 distFromObjectOrigin_mm,
                      PathMotionProfile motionProf,
                      const bool useManualSpeed = false,
                      const bool usePreDockPose = false);
  
  void SendAlignWithObject(const s32 objectID,
                           const f32 distFromMarker_mm,
                           PathMotionProfile motionProf,
                           const bool usePreDockPose,
                           const bool useApproachAngle = false,
                           const f32 approachAngle_rad = false,
                           const bool useManualSpeed = false);
  
  void SendPlaceObjectOnGroundSequence(const Pose3d& p,
                                       PathMotionProfile motionProf,
                                       const bool useExactRotation = false,
                                       const bool useManualSpeed = false);
  
  void SendPickupObject(const s32 objectID,
                        PathMotionProfile motionProf,
                        const bool usePreDockPose,
                        const bool useApproachAngle = false,
                        const f32 approachAngle_rad = 0,
                        const bool useManualSpeed = false);
  
  void SendPickupSelectedObject(PathMotionProfile motionProf,
                                const bool usePreDockPose,
                                const bool useApproachAngle,
                                const f32 approachAngle_rad,
                                const bool useManualSpeed = false);
  
  void SendPlaceOnObject(const s32 objectID,
                         PathMotionProfile motionProf,
                         const bool usePreDockPose,
                         const bool useApproachAngle = false,
                         const f32 approachAngle_rad = 0,
                         const bool useManualSpeed = false);
  
  void SendPlaceOnSelectedObject(PathMotionProfile motionProf,
                                 const bool usePreDockPose,
                                 const bool useApproachAngle = false,
                                 const f32 approachAngle_rad = 0,
                                 const bool useManualSpeed = false);

  void SendPlaceRelObject(const s32 objectID,
                          PathMotionProfile motionProf,
                          const bool usePreDockPose,
                          const f32 placementOffsetX_mm,
                          const bool useApproachAngle = false,
                          const f32 approachAngle_rad = 0,
                          const bool useManualSpeed = false);
  
  void SendPlaceRelSelectedObject(PathMotionProfile motionProf,
                                  const bool usePreDockPose,
                                  const f32 placementOffsetX_mm,
                                  const bool useApproachAngle = false,
                                  const f32 approachAngle_rad = 0,
                                  const bool useManualSpeed = false);

  void SendRollObject(const s32 objectID,
                      PathMotionProfile motionProf,
                      const bool doDeepRoll,
                      const bool usePreDockPose,
                      const bool useApproachAngle = false,
                      const f32 approachAngle_rad = 0,
                      const bool useManualSpeed = false);
  
  void SendRollSelectedObject(PathMotionProfile motionProf,
                              const bool doDeepRoll,
                              const bool usePreDockPose,
                              const bool useApproachAngle = false,
                              const f32 approachAngle_rad = 0,
                              const bool useManualSpeed = false);

  void SendPopAWheelie(const s32 objectID,
                       PathMotionProfile motionProf,
                       const bool usePreDockPose,
                       const bool useApproachAngle = false,
                       const f32 approachAngle_rad = 0,
                       const bool useManualSpeed = false);

  void SendFacePlant(const s32 objectID,
                     PathMotionProfile motionProf,
                     const bool usePreDockPose,
                     const bool useApproachAngle = false,
                     const f32 approachAngle_rad = 0,
                     const bool useManualSpeed = false);
  
  void SendTraverseSelectedObject(PathMotionProfile motionProf,
                                  const bool usePreDockPose,
                                  const bool useManualSpeed);

  void SendMountCharger(const s32 objectID,
                        PathMotionProfile motionProf,
                        const bool useCliffSensorCorrection = true,
                        const bool useManualSpeed = false);
  
  void SendMountSelectedCharger(PathMotionProfile motionProf,
                                const bool useCliffSensorCorrection = true,
                                const bool useManualSpeed = false);
  
  void SendRequestEnabledBehaviorList();
  void SendTrackToObject(const u32 objectID, bool headOnly = false);
  void SendTrackToFace(const u32 faceID, bool headOnly = false);
  void SendExecuteTestPlan(PathMotionProfile motionProf);
  void SendClearAllBlocks();
  void SendClearAllObjects();
  void SendSelectNextObject();
  void SendAbortPath();
  void SendAbortAll();
  void SendDrawPoseMarker(const Pose3d& p);
  void SendErasePoseMarker();
  void SendControllerGains(ControllerChannel channel, f32 kp, f32 ki, f32 kd, f32 maxErrorSum);
  void SendRollActionParams(f32 liftHeight_mm, f32 driveSpeed_mmps, f32 driveAccel_mmps2, u32 driveDuration_ms, f32 backupDist_mm);
  void SendSetRobotVolume(const f32 volume);
  void SendStartTestMode(TestMode mode, s32 p1 = 0, s32 p2 = 0, s32 p3 = 0);
  void SendIMURequest(u32 length_ms);
  void SendLogCliffDataRequest(const u32 length_ms);
  void SendLogProxDataRequest(const u32 length_ms);
  void SendAnimation(const char* animName, u32 numLoops, bool throttleMessages = false);
  void SendAnimationGroup(const char* animName, bool throttleMessages = false);
  void SendDevAnimation(const char* animName, u32 numLoops); // FIXME: Remove after code refactor - JMR
  void SendReplayLastAnimation();
  void SendReadAnimationFile();
  void SendEnableVisionMode(VisionMode mode, bool enable);
  void SendSetIdleAnimation(const std::string &animName);
  uint32_t SendQueuePlayAnimAction(const std::string &animName, u32 numLoops, QueueActionPosition pos);
  void SendCancelAction();
  void SendSaveCalibrationImage();
  void SendClearCalibrationImages();
  void SendComputeCameraCalibration();
  void SendCameraCalibration(f32 focalLength_x, f32 focalLength_y, f32 center_x, f32 center_y);
  void SendNVStorageWriteEntry(NVStorage::NVEntryTag tag, u8* data, size_t size, u8 blobIndex, u8 numTotalBlobs);
  void SendNVStorageReadEntry(NVStorage::NVEntryTag tag);
  void SendNVStorageEraseEntry(NVStorage::NVEntryTag tag);
  void SendNVClearPartialPendingWriteData();
  void SendEnableBlockTapFilter(bool enable);
  void SendEnableBlockPool(double maxDiscoveryTime, bool enabled);
  void SendStreamObjectAccel(const u32 objectID, bool enable = true);

  ///
  // @brief      Send SetActiveObjectLEDs CLAD message
  //
  // See the .clad file for documentation on parameters. The parameter robotID does not need to be
  // passed in.
  //
  void SendSetActiveObjectLEDs(const u32 objectID, 
                               const u32 onColor,
                               const u32 offColor,
                               const u32 onPeriod_ms,
                               const u32 offPeriod_ms,
                               const u32 transitionOnPeriod_ms,
                               const u32 transitionOffPeriod_ms,
                               const s32 offset,
                               const u32 rotationPeriod_ms,
                               const f32 relativeToX,
                               const f32 relativeToY,
                               const WhichCubeLEDs whichLEDs,
                               const MakeRelativeMode makeRelative,
                               const bool turnOffUnspecifiedLEDs);

  void SendSetAllActiveObjectLEDs(const u32 objectID, 
                                  const std::array<u32, 4> onColor,
                                  const std::array<u32, 4> offColor,
                                  const std::array<u32, 4> onPeriod_ms,
                                  const std::array<u32, 4> offPeriod_ms,
                                  const std::array<u32, 4> transitionOnPeriod_ms,
                                  const std::array<u32, 4> transitionOffPeriod_ms,
                                  const std::array<s32, 4> offset,
                                  const u32 rotationPeriod_ms,
                                  const f32 relativeToX,
                                  const f32 relativeToY,
                                  const MakeRelativeMode makeRelative);

  // ====== Accessors =====
  s32 GetStepTimeMS() const;
  webots::Supervisor* GetSupervisor();

  PoseOriginList _poseOriginList;
  
  // Pose to use as "actual" poses' origin
  const Pose3d  _webotsOrigin;
  
  // Robot state message convenience functions
  const Pose3d& GetRobotPose() const;
  const Pose3d& GetRobotPoseActual() const;
  f32           GetRobotHeadAngle_rad() const;
  f32           GetLiftHeight_mm() const;
  void          GetWheelSpeeds_mmps(f32& left, f32& right) const;
  s32           GetCarryingObjectID() const;
  s32           GetCarryingObjectOnTopID() const;
  bool          IsRobotStatus(RobotStatusFlag mask) const;
  
  const ExternalInterface::RobotState& GetRobotState() const { return _robotStateMsg; }
  
  std::vector<s32> GetAllObjectIDs() const;
  std::vector<s32> GetAllObjectIDsByFamily(ObjectFamily family) const;
  std::vector<s32> GetAllObjectIDsByFamilyAndType(ObjectFamily family, ObjectType type) const;
  Result           GetObjectFamily(s32 objectID, ObjectFamily& family) const;
  Result           GetObjectType(s32 objectID, ObjectType& type) const;
  Result           GetObjectPose(s32 objectID, Pose3d& pose) const;
  
  u32              GetNumObjectsInFamily(ObjectFamily family) const;
  u32              GetNumObjectsInFamilyAndType(ObjectFamily family, ObjectType type) const;
  u32              GetNumObjects() const;
  void             ClearAllKnownObjects();
  
  // Helper to create a Pose3d from a poseStruct and add a new origin if needed
  Pose3d CreatePoseHelper(const PoseStruct3d& poseStruct);
  
  void AddOrUpdateObject(s32 objID, ObjectType objType, ObjectFamily objFamily,
                         const PoseStruct3d& poseStruct);
  
  const std::map<s32, Pose3d>& GetObjectPoseMap();
  
  const ObservedObject& GetLastObservedObject() const;

  const Vision::FaceID_t GetLastObservedFaceID() const;
  
  BehaviorClass GetBehaviorClass(const std::string& behaviorName) const;
  
  // NVStorage
  const std::vector<u8>* GetReceivedNVStorageData(NVStorage::NVEntryTag tag) const;
  void ClearReceivedNVStorageData(NVStorage::NVEntryTag tag);
  bool IsMultiBlobEntryTag(u32 tag) const;

  ///
  // @brief      Sets the actual robot pose.
  // @param[in]  newPose  The new pose with translation in millimeters.
  //
  void SetActualRobotPose(const Pose3d& newPose);

  void SetActualObjectPose(const std::string& name, const Pose3d& newPose);
  const Pose3d GetLightCubePoseActual(ObjectType lightCubeType);


  ///
  // @brief      Physically move the cube in simulation.
  // @param[in]  lightCubeType  The ObjectType for the light cube
  // @param[in]  pose           Pose with translation in millimeters.
  //
  void SetLightCubePose(ObjectType lightCubeType, const Pose3d& pose);
  bool HasActualLightCubePose(ObjectType lightCubeType) const;
  
  ///
  // @brief      Iterates through _lightCubes and removes the one of the given ObjectType
  //             (should be unique).
  // @param[in]  type  Cube ObjectType
  // @return     Whether or not it was successfully removed
  //
  bool RemoveLightCubeByType(ObjectType type);
  
  ///
  // @brief      Adds a cube of the given ObjectType if doesn't already exist
  //             (should be unique).
  // @param[in]  type       Cube ObjectType
  // @param[in]  p          Pose at which cube should be added
  // @param[in]  factoryID  FactoryID of cube to be added. (If 0, then factoryID is auto-generated based on ObjectType)
  // @return     Whether or not it was successfully added
  //
  bool AddLightCubeByType(ObjectType type, const Pose3d& p, const u32 factoryID = 0);

  
  
  static size_t MakeWordAligned(size_t size);
  const std::string GetAnimationTestName() const;
  const double GetSupervisorTime() const;

  ///
  // @brief      Gets the node by definition name defined in the webots world files (.wbt).
  // @param[in]  defName  The definition name
  // @return     The node with the definition name.
  //
  webots::Node* GetNodeByDefName(const std::string& defName) const;
  
  ///
  // @brief      Packages the pose of a webots node into a Pose3d object
  // @param[in]  node  Node to get the pose for
  // @return     The pose of the webots node; translation units are in millimeters.
  //
  const Pose3d GetPose3dOfNode(webots::Node* node) const;
  
  ///
  // @brief      Sets the pose of a webots node from a Pose3d object
  // @param[in]  node  Node whose pose to change
  // @param[in]  The new pose to use; translation units are in millimeters.
  //
  void SetNodePose(webots::Node* node, const Pose3d& newPose);
  
  ///
  // @brief      Determines if x seconds passed since the first time this function was called.
  //             Useful in the CST test controllers where the same code block in each state can be
  //             called many times repeatedly as UpdateInternal is called on every tick and waiting
  //             by blocking the thread is not an option as the time will not advance if the thread
  //             is blocked.
  // @param[in]  xSeconds   Number of seconds to wait for.
  // @return     True if has x seconds passed since the first call of the function, False otherwise.
  //
  const bool HasXSecondsPassedYet(double xSeconds);

  ///
  // @brief      Apply a force to a node at the node origin in webots.
  // @param[in]  defName  The defName of the node to apply force to. (https://www.cyberbotics.com/reference/def-and-use.php)
  // @param[in]  xForce   The x force
  // @param[in]  yForce   The y force
  // @param[in]  zForce   The z force
  //
  void SendApplyForce(const std::string& defName, int xForce, int yForce, int zForce);
  
  
private:
  void HandlePingBase(const ExternalInterface::Ping& msg);
  void HandleRobotStateUpdateBase(const ExternalInterface::RobotState& msg);
  void HandleRobotDelocalizedBase(const ExternalInterface::RobotDelocalized& msg);
  void HandleRobotOffTreadsStateChangedBase(const ExternalInterface::RobotOffTreadsStateChanged& msg);
  void HandleRobotObservedObjectBase(const ExternalInterface::RobotObservedObject& msg);
  void HandleRobotObservedFaceBase(const ExternalInterface::RobotObservedFace& msg);
  void HandleRobotObservedPetBase(const ExternalInterface::RobotObservedPet& msg);
  void HandleRobotDeletedLocatedObjectBase(const ExternalInterface::RobotDeletedLocatedObject& msg);
  void HandleUiDeviceAvailableBase(const ExternalInterface::UiDeviceAvailable& msg);
  void HandleUiDeviceConnectedBase(const ExternalInterface::UiDeviceConnected& msg);
  void HandleRobotConnectedBase(const ExternalInterface::RobotConnectionResponse& msg);
  void HandleRobotCompletedActionBase(const ExternalInterface::RobotCompletedAction& msg);
  void HandleImageChunkBase(const ImageChunk& msg);
  void HandleActiveObjectAccelBase(const ObjectAccel& msg);
  void HandleActiveObjectConnectionStateBase(const ObjectConnectionState& msg);
  void HandleActiveObjectMovedBase(const ObjectMoved& msg);
  void HandleActiveObjectStoppedMovingBase(const ObjectStoppedMoving& msg);
  void HandleActiveObjectTappedBase(const ObjectTapped& msg);
  void HandleActiveObjectUpAxisChangedBase(const ObjectUpAxisChanged& msg);
  void HandleLocatedObjectStatesBase(const ExternalInterface::LocatedObjectStates& msg);
  void HandleConnectedObjectStatesBase(const ExternalInterface::ConnectedObjectStates& msg);
  void HandleAnimationAvailableBase(const ExternalInterface::AnimationAvailable& msg);
  void HandleAnimationAbortedBase(const ExternalInterface::AnimationAborted& msg);
  void HandleDebugStringBase(const ExternalInterface::DebugString& msg);
  void HandleNVStorageOpResultBase(const ExternalInterface::NVStorageOpResult& msg);
  void HandleBehaviorTransitionBase(const ExternalInterface::BehaviorTransition& msg);
  void HandleEndOfMessageBase(const ExternalInterface::EndOfMessage& msg);
  void HandleFactoryTestResultEntryBase(const FactoryTestResultEntry& msg);
  void HandleLoadedKnownFaceBase(const Vision::LoadedKnownFace& msg);
  void HandleFaceEnrollmentCompletedBase(const ExternalInterface::FaceEnrollmentCompleted &msg);
  void HandleCliffEventBase(const CliffEvent& msg);
  void HandleSetCliffDetectThresholdsBase(const SetCliffDetectThresholds& msg);
  void HandleEngineErrorCodeBase(const ExternalInterface::EngineErrorCodeMessage& msg);
  void HandleEngineLoadingStatusBase(const ExternalInterface::EngineLoadingDataStatus& msg);
  void HandleDefinedCustomObjectBase(const ExternalInterface::DefinedCustomObject& msg);
  void HandleRobotDeletedAllCustomObjectsBase(const ExternalInterface::RobotDeletedAllCustomObjects& msg);
  void HandleRobotDeletedCustomMarkerObjectsBase(const ExternalInterface::RobotDeletedCustomMarkerObjects& msg);
  void HandleRobotDeletedFixedCustomObjectsBase(const ExternalInterface::RobotDeletedFixedCustomObjects& msg);
  
  void UpdateActualObjectPoses();
  
  ///
  // @brief      Iterates through _lightCubes and returns the first light cube with the given ID
  //             (should be unique).
  // @param[in]  type    The ObjectType for the cube in question.
  // @return     The webots node for the light cube.
  //
  webots::Node* GetLightCubeByType(ObjectType type) const;
  
  const f32 TIME_UNTIL_READY_SEC = 1.5;
  
  s32 _stepTimeMS;
  webots::Supervisor _supervisor;
  
  webots::Node* _robotNode       = nullptr;

  std::vector<webots::Node*> _lightCubes;
  std::vector<webots::Node*>::iterator _lightCubeOriginIter = _lightCubes.end();
  
  Pose3d _robotPose;
  Pose3d _robotPoseActual;
  bool _firstRobotPoseUpdate;
  
  ExternalInterface::RobotState _robotStateMsg;
  
  UiGameController::ObservedObject _lastObservedObject;
  std::map<s32, std::pair<ObjectFamily, ObjectType> > _objectIDToFamilyTypeMap;
  std::map<ObjectFamily, std::map<ObjectType, std::vector<s32> > > _objectFamilyToTypeToIDMap;
  std::map<s32, Pose3d> _objectIDToPoseMap;
  
  Vision::FaceID_t _lastObservedFaceID;
  
  webots::Node* _root = nullptr;
  
  typedef enum {
    UI_WAITING_FOR_GAME = 0,
    UI_WAITING_FOR_ENGINE_LOAD,
    UI_RUNNING
  } UI_State_t;
  
  UI_State_t _uiState;
  
  GameMessageHandler _msgHandler;
  GameComms *_gameComms = nullptr;
  
  const Util::Data::DataPlatform* _dataPlatform = nullptr;

  UdpClient _physicsControllerClient;

  bool _doAutoBlockPool;
  bool _isBlockPoolInitialized;
  
  float _engineLoadedRatio = 0.0f;
  
  double _waitTimer = -1.0;
  
  uint32_t _queueActionIdTag = 0;
  
  // Seed used to start engine
  uint32_t _randomSeed = 0;
  
  std::string _locale = "en-US";
  
}; // class UiGameController
  
  
  
} // namespace Cozmo
} // namespace Anki


#endif // __UI_GAME_CONTROLLER_H__

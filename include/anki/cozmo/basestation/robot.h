//
//  robot.h
//  Products_Cozmo
//
//  Created by Andrew Stein on 8/23/13.
//  Copyright (c) 2013 Anki, Inc. All rights reserved.
//

#ifndef __Products_Cozmo__robot__
#define __Products_Cozmo__robot__

#include <queue>

#include "anki/common/types.h"
#include "anki/common/basestation/math/pose.h"

#include "anki/vision/basestation/camera.h"
#include "anki/vision/basestation/visionMarker.h"

#include "anki/planning/shared/path.h"

#include "anki/cozmo/shared/cozmoTypes.h"
#include "anki/cozmo/basestation/block.h"
#include "anki/cozmo/basestation/messages.h"
#include "anki/cozmo/basestation/robotPoseHistory.h"

namespace Anki {
  namespace Cozmo {
    
    // Forward declarations:
    class BlockWorld;
    class IMessageHandler;
    class IPathPlanner;
    class PathDolerOuter;
    
    class BlockDockingSystem
    {
    public:
      
    protected:
      
      
    }; // class BlockDockingSystem
    
    
    class Robot
    {
    public:
      enum State {
        IDLE,
        FOLLOWING_PATH,
        BEGIN_DOCKING,
        DOCKING,
        PLACE_BLOCK_ON_GROUND
      };
      
      static const std::map<State, std::string> StateNames;
      
      Robot(const RobotID_t robotID, IMessageHandler* msgHandler, BlockWorld* world, IPathPlanner* pathPlanner);
      ~Robot();
      
      void Update();
      
      // Accessors
      const RobotID_t        GetID()           const;
      const Pose3d&          GetPose()         const;
      const Pose3d*          GetPoseOrigin()   const {return _poseOrigin;}
      bool                   IsLocalized()     const {return _isLocalized;}
      const Vision::Camera&  GetCamera()       const;
      
      // Returns true if head_angle is valid.
      // *valid_head_angle is made to equal the closest valid head angle to head_angle.
      bool IsValidHeadAngle(f32 head_angle, f32* valid_head_angle = nullptr) const;
      
      Vision::Camera&                   GetCamera();
      void                              SetCameraCalibration(const Vision::CameraCalibration& calib);
	    const Vision::CameraCalibration&  GetCameraCalibration() const;
      
      const f32              GetHeadAngle()    const;
      const f32              GetLiftAngle()    const;
      
      const Pose3d&          GetNeckPose()     const {return _neckPose;}
      const Pose3d&          GetHeadCamPose()  const {return _headCamPose;}
      const Pose3d&          GetLiftPose()     const {return _liftPose;}  // At current lift position!
      const State            GetState()        const;
      
      const ObjectID_t       GetDockBlock()    const {return _dockBlockID;}
      const ObjectID_t       GetCarryingBlock()const {return _carryingBlockID;}
      
      void SetState(const State newState);
      void SetPose(const Pose3d &newPose);
      void SetHeadAngle(const f32& angle);
      void SetLiftAngle(const f32& angle);
      
      void IncrementPoseFrameID() {++_frameId;}
      PoseFrameID_t GetPoseFrameID() const {return _frameId;}
      
      
      // Clears the path that the robot is executing which also stops the robot
      Result ClearPath();

      // Removes the specified number of segments from the front and back of the path
      Result TrimPath(const u8 numPopFrontSegments, const u8 numPopBackSegments);
      
      // Sends a path to the robot to be immediately executed
      // Puts Robot in FOLLOWING_PATH state. Will transition to IDLE when path is complete.
      Result ExecutePath(const Planning::Path& path);

      // Compute a path to a pose and execute it
      // Puts Robot in FOLLOWING_PATH state. Will transition to IDLE when path is complete.
      Result GetPathToPose(const Pose3d& pose, Planning::Path& path);
      Result ExecutePathToPose(const Pose3d& pose);
      Result ExecutePathToPose(const Pose3d& pose, const Radians headAngle);
      
      // Same as above, but select from a set poses and return the selected index.
      Result GetPathToPose(const std::vector<Pose3d>& poses, size_t& selectedIndex, Planning::Path& path);
      Result ExecutePathToPose(const std::vector<Pose3d>& poses, size_t& selectedIndex);
      Result ExecutePathToPose(const std::vector<Pose3d>& poses, const Radians headAngle, size_t& selectedIndex);

      // executes a test path defined in latticePlanner
      void ExecuteTestPath();

      IPathPlanner* GetPathPlanner() { return _longPathPlanner; }

      void AbortCurrentPath();
      
      // True if wheel speeds are non-zero in most recent RobotState message
      bool IsMoving() const {return _isMoving;}
      void SetIsMoving(bool t) {_isMoving= t;}
      
      void SetCurrPathSegment(const s8 s) {_currPathSegment = s;}
      s8   GetCurrPathSegment() {return _currPathSegment;}
      bool IsTraversingPath() {return (_currPathSegment >= 0) || (_lastSentPathID > _lastRecvdPathID);}

      void SetNumFreeSegmentSlots(const u8 n) {_numFreeSegmentSlots = n;}
      u8 GetNumFreeSegmentSlots() const {return _numFreeSegmentSlots;}
      
      void SetLastRecvdPathID(u16 path_id) {_lastRecvdPathID = path_id;}
      u16 GetLastRecvdPathID() {return _lastRecvdPathID;}
      u16 GetLastSentPathID() {return _lastSentPathID;}

      void SetCarryingBlock(ObjectID_t carryBlockID) {_carryingBlockID = carryBlockID;}
      bool IsCarryingBlock() {return _carryingBlockID != ANY_OBJECT;}

      void SetPickingOrPlacing(bool t) {_isPickingOrPlacing = t;}
      bool IsPickingOrPlacing() {return _isPickingOrPlacing;}
      
      ///////// Motor commands  ///////////
      
      // Sends message to move lift at specified speed
      Result MoveLift(const f32 speed_rad_per_sec);
      
      // Sends message to move head at specified speed
      Result MoveHead(const f32 speed_rad_per_sec);
      
      // Sends a message to the robot to move the lift to the specified height
      Result MoveLiftToHeight(const f32 height_mm,
                              const f32 max_speed_rad_per_sec,
                              const f32 accel_rad_per_sec2);
      
      // Sends a message to the robot to move the head to the specified angle
      Result MoveHeadToAngle(const f32 angle_rad,
                             const f32 max_speed_rad_per_sec,
                             const f32 accel_rad_per_sec2);
      
      Result DriveWheels(const f32 lwheel_speed_mmps,
                         const f32 rwheel_speed_mmps);
      
      Result StopAllMotors();
      
      // Plan a path to an available docking pose of the specified block, and
      // then dock with it.
      Result ExecuteDockingSequence(ObjectID_t blockToDockWith);
      
      // Plan a path to place the block currently being carried at the specified
      // pose.
      Result ExecutePlaceBlockOnGroundSequence(const Pose3d& atPose);
      
      // Put the carried block down right where the robot is now
      Result ExecutePlaceBlockOnGroundSequence();
      
      // Sends a message to the robot to dock with the specified marker of the
      // specified block that it should currently be seeing.
      Result DockWithBlock(const ObjectID_t blockID,
                           const Vision::KnownMarker* marker,
                           const DockAction_t dockAction);
      
      // Sends a message to the robot to dock with the specified marker of the
      // specified block, which it should currently be seeing. If pixel_radius == u8_MAX,
      // the marker can be seen anywhere in the image (same as above function), otherwise the
      // marker's center must be seen at the specified image coordinates
      // with pixel_radius pixels.
      Result DockWithBlock(const ObjectID_t blockID,
                           const Vision::KnownMarker* marker,
                           const DockAction_t dockAction,
                           const u16 image_pixel_x,
                           const u16 image_pixel_y,
                           const u8 pixel_radius);

      // Transitions the block that robot was docking with to the one that it
      // is carrying, and puts it in the robot's pose chain, attached to the
      // lift. Returns RESULT_FAIL if the robot wasn't already docking with
      // a block.
      Result PickUpDockBlock();
      
      Result VerifyBlockPickup();
      
      // Places the block that the robot was carrying in its current position
      // w.r.t. the world, and removes it from the lift pose chain so it is no
      // longer attached to the robot.  Note that IsCarryingBlock() will still
      // report true, until it is actually verified that the placement worked.
      Result PlaceCarriedBlock(); //const TimeStamp_t atTime);
      
      Result VerifyBlockPlacement();
      
      // Turn on/off headlight LEDs
      Result SetHeadlight(u8 intensity);
      
      ///////// Messaging ////////
      // TODO: Most of these send functions should be private and wrapped in
      // relevant state modifying functions. e.g. SendStopAllMotors() should be
      // called from StopAllMotors().
      
      // Sync time with physical robot and trigger it robot to send back camera
      // calibration
      Result SendInit() const;
      
      // Send's robot's current pose
      Result SendAbsLocalizationUpdate() const;

      // Update the head angle on the robot
      Result SendHeadAngleUpdate() const;

      // Request camera snapshot from robot
      Result SendImageRequest(const ImageSendMode_t mode) const;

      // Run a test mode
      Result SendStartTestMode(const TestMode mode) const;
      
      // Get the bounding quad of the robot at its current or a given pose
      Quad2f GetBoundingQuadXY(const f32 padding_mm = 0.f) const; // at current pose
      Quad2f GetBoundingQuadXY(const Pose3d& atPose, const f32 paddingScale = 0.f) const; // at specific pose
      
      // Set controller gains on robot
      Result SendHeadControllerGains(const f32 kp, const f32 ki, const f32 maxIntegralError);
      Result SendLiftControllerGains(const f32 kp, const f32 ki, const f32 maxIntegralError);
      
      // Set VisionSystem parameters
      Result SendSetVisionSystemParams(VisionSystemParams_t p);
      
      // Play animation
      // If numLoops == 0, animation repeats forever.
      Result SendPlayAnimation(const AnimationID_t id, const u32 numLoops = 0);
      
      // For processing image chunks arriving from robot.
      // Sends complete images to VizManager for visualization.
      // If _saveImages is true, then images are saved as pgm.
      Result ProcessImageChunk(const MessageImageChunk &msg);
      
      // Enable/Disable saving of images constructed from ImageChunk messages as pgm files.
      void SaveImages(bool on);
      bool IsSavingImages() const;
      
      // =========== Pose history =============
      // Returns ref to robot's pose history
      const RobotPoseHistory& GetPoseHistory() {return _poseHistory;}
      
      Result AddRawOdomPoseToHistory(const TimeStamp_t t,
                                     const PoseFrameID_t frameID,
                                     const f32 pose_x, const f32 pose_y, const f32 pose_z,
                                     const f32 pose_angle,
                                     const f32 head_angle,
                                     const f32 lift_angle,
                                     const Pose3d* pose_origin);
      
      Result AddVisionOnlyPoseToHistory(const TimeStamp_t t,
                                        const RobotPoseStamp& p);

      Result ComputeAndInsertPoseIntoHistory(const TimeStamp_t t_request,
                                             TimeStamp_t& t, RobotPoseStamp** p,
                                             HistPoseKey* key = nullptr,
                                             bool withInterpolation = false);

      Result GetVisionOnlyPoseAt(const TimeStamp_t t_request, RobotPoseStamp** p);
      Result GetComputedPoseAt(const TimeStamp_t t_request, RobotPoseStamp** p, HistPoseKey* key = nullptr);
      
      TimeStamp_t GetLastMsgTimestamp() const;
      
      bool IsValidPoseKey(const HistPoseKey key) const;
      
      // Updates the current pose to the best estimate based on
      // historical poses including vision-based poses.
      // Returns true if the pose is successfully updated, false otherwise.
      bool UpdateCurrPoseFromHistory();
      
      
      // ========= Lights ==========
      void SetDefaultLights(const u32 eye_left_color, const u32 eye_right_color);
      
      
    protected:
      // The robot's identifier
      RobotID_t        _ID;
      
      // A reference to the MessageHandler that the robot uses for outgoing comms
      IMessageHandler* _msgHandler;
      
      // A reference to the BlockWorld the robot lives in
      BlockWorld*      _world;
      
      // Path Following. There are two planners, only one of which can
      // be selected at a time
      IPathPlanner*    _selectedPathPlanner;
      IPathPlanner*    _longPathPlanner;
      IPathPlanner*    _shortPathPlanner;
      Planning::Path   _path;
      s8               _currPathSegment;
      u8               _numFreeSegmentSlots;
      Pose3d           _goalPose;
      f32              _goalDistanceThreshold;
      Radians          _goalAngleThreshold;
      u16              _lastSentPathID;
      u16              _lastRecvdPathID;

      // This functions sets _selectedPathPlanner to the appropriate
      // planner
      void SelectPlanner(const Pose3d& targetPose);

      PathDolerOuter* pdo_;
      
      // if true and we are traversing a path, then next time the
      // block world changes, re-plan from scratch
      bool _forceReplanOnNextWorldChange;

      // Whether or not images that are construted from incoming ImageChunk messages
      // should be saved as PGM
      bool _saveImages;

	    // Robot stores the calibration, camera just gets a reference to it
      // This is so we can share the same calibration data across multiple
      // cameras (e.g. those stored inside the pose history)
      Vision::CameraCalibration _cameraCalibration;
      Vision::Camera            _camera;
      
      // Geometry / Pose
      Pose3d*          _poseOrigin;
      Pose3d           _pose;
      PoseFrameID_t    _frameId;
      bool             _isLocalized;
      
      const Pose3d _neckPose; // joint around which head rotates
      const Pose3d _headCamPose; // in canonical (untilted) position w.r.t. neck joint
      const Pose3d _liftBasePose; // around which the base rotates/lifts
      Pose3d _liftPose;     // current, w.r.t. liftBasePose

      f32 _currentHeadAngle;
      f32 _currentLiftAngle;
      
      static const Quad2f CanonicalBoundingBoxXY;
      
      // Pose history
      RobotPoseHistory _poseHistory;

      // State
      //bool _isCarryingBlock;
      bool _isPickingOrPlacing;
      //Block* _carryingBlock;
      ObjectID_t _carryingBlockID;
      bool _isMoving;
      State _state, _nextState;
      
      // Leaves input liftPose's parent alone and computes its position w.r.t.
      // liftBasePose, given the angle
      static void ComputeLiftPose(const f32 atAngle, Pose3d& liftPose);
      
      // Docking
      // Note that we don't store a pointer to the block because it
      // could deleted, but it is ok to hang onto a pointer to the
      // marker on that block, so long as we always verify the block
      // exists and is still valid (since, therefore, the marker must
      // be as well)
      ObjectID_t                  _dockBlockID;
      const Vision::KnownMarker*  _dockMarker;
      DockAction_t                _dockAction;
      Pose3d                      _dockBlockOrigPose;
      
      f32 _waitUntilTime;
      
      ///////// Messaging ////////
      
      // Sends message to move lift at specified speed
      Result SendMoveLift(const f32 speed_rad_per_sec) const;
      
      // Sends message to move head at specified speed
      Result SendMoveHead(const f32 speed_rad_per_sec) const;
      
      // Sends a message to the robot to move the lift to the specified height
      Result SendSetLiftHeight(const f32 height_mm,
                               const f32 max_speed_rad_per_sec,
                               const f32 accel_rad_per_sec2) const;
      
      // Sends a message to the robot to move the head to the specified angle
      Result SendSetHeadAngle(const f32 angle_rad,
                              const f32 max_speed_rad_per_sec,
                              const f32 accel_rad_per_sec2) const;
      
      Result SendDriveWheels(const f32 lwheel_speed_mmps,
                             const f32 rwheel_speed_mmps) const;
      
      Result SendStopAllMotors() const;

      // Clears the path that the robot is executing which also stops the robot
      Result SendClearPath() const;

      // Removes the specified number of segments from the front and back of the path
      Result SendTrimPath(const u8 numPopFrontSegments, const u8 numPopBackSegments) const;
      
      // Sends a path to the robot to be immediately executed
      Result SendExecutePath(const Planning::Path& path) const;
      
      // Sends a message to the robot to dock with the specified block
      // that it should currently be seeing. If pixel_radius == u8_MAX,
      // the marker can be seen anywhere in the image (same as above function), otherwise the
      // marker's center must be seen at the specified image coordinates
      // with pixel_radius pixels.
      Result SendDockWithBlock(const Vision::Marker::Code& markerType,
                               const f32 markerWidth_mm,
                               const DockAction_t dockAction,
                               const u16 image_pixel_x,
                               const u16 image_pixel_y,
                               const u8 pixel_radius) const;

      Result SendPlaceBlockOnGround();
      
      // Turn on/off headlight LEDs
      Result SendHeadlight(u8 intensity);
      
      
    }; // class Robot

    //
    // Inline accessors:
    //
    inline const RobotID_t Robot::GetID(void) const
    { return _ID; }
    
    inline const Pose3d& Robot::GetPose(void) const
    { return _pose; }
    
    inline const Vision::Camera& Robot::GetCamera(void) const
    { return _camera; }
    
    inline Vision::Camera& Robot::GetCamera(void)
    { return _camera; }
    
    inline const Robot::State Robot::GetState() const
    { return _state; }
    
    inline void Robot::SetCameraCalibration(const Vision::CameraCalibration& calib)
    {
      _cameraCalibration = calib;
      _camera.SetSharedCalibration(&_cameraCalibration);
    }

	inline const Vision::CameraCalibration& Robot::GetCameraCalibration() const
    { return _cameraCalibration; }
    
    inline const f32 Robot::GetHeadAngle() const
    { return _currentHeadAngle; }
    
    inline const f32 Robot::GetLiftAngle() const
    { return _currentLiftAngle; }
    
    //
    // RobotManager class for keeping up with available robots, by their ID
    //
    // TODO: Singleton or not?
#define USE_SINGLETON_ROBOT_MANAGER 0
    
    class RobotManager
    {
    public:
    
#if USE_SINGLETON_ROBOT_MANAGER
      // Return singleton instance
      static RobotManager* getInstance();
#else
      RobotManager();
#endif

      // Sets pointers to other managers
      // TODO: Change these to interface pointers so they can't be NULL
      Result Init(IMessageHandler* msgHandler, BlockWorld* blockWorld, IPathPlanner* pathPlanner);
      
      // Get the list of known robot ID's
      std::vector<RobotID_t> const& GetRobotIDList() const;
      
      // Get a pointer to a robot by ID
      Robot* GetRobotByID(const RobotID_t robotID);
      
      // Check whether a robot exists
      bool DoesRobotExist(const RobotID_t withID) const;
      
      // Add / remove robots
      void AddRobot(const RobotID_t withID);
      void RemoveRobot(const RobotID_t withID);
      
      // Call each Robot's Update() function
      void UpdateAllRobots();
      
      // Return a
      // Return the number of availale robots
      size_t GetNumRobots() const;
      
    protected:
      
#if USE_SINGLETON_ROBOT_MANAGER
      RobotManager(); // protected constructor for singleton class
      
      static RobotManager* singletonInstance_;
#endif
      
      IMessageHandler* _msgHandler;
      BlockWorld*      _blockWorld;
      IPathPlanner*    _pathPlanner;
      
      std::map<RobotID_t,Robot*> _robots;
      std::vector<RobotID_t>     _IDs;
      
    }; // class RobotManager
    
#if USE_SINGLETON_ROBOT_MANAGER
    inline RobotManager* RobotManager::getInstance()
    {
      if(0 == singletonInstance_) {
        singletonInstance_ = new RobotManager();
      }
      return singletonInstance_;
    }
#endif
    
  } // namespace Cozmo
} // namespace Anki

#endif // __Products_Cozmo__robot__

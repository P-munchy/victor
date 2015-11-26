/**
 * File: cozmoActions.h
 *
 * Author: Andrew Stein
 * Date:   8/29/2014
 *
 * Description: Implements cozmo-specific actions, derived from the IAction interface.
 *
 *
 * Copyright: Anki, Inc. 2014
 **/

#ifndef ANKI_COZMO_ACTIONS_H
#define ANKI_COZMO_ACTIONS_H

#include "anki/cozmo/basestation/actionableObject.h"
#include "anki/cozmo/basestation/actionInterface.h"
#include "anki/cozmo/basestation/compoundActions.h"
#include "anki/cozmo/shared/cozmoEngineConfig.h"
#include "anki/common/types.h"
#include "anki/common/basestation/objectIDs.h"
#include "anki/common/basestation/math/pose.h"
#include "util/signals/simpleSignal_fwd.h"
#include "clad/types/actionTypes.h"
#include "clad/types/animationKeyFrames.h"
#include "clad/types/pathMotionProfile.h"

namespace Anki {
  
  namespace Vision {
    // Forward Declarations:
    class KnownMarker;
  }
  
  namespace Cozmo {

    // Forward Declarations:
    class Robot;
    
    class DriveToPoseAction : public IAction
    {
    public:
      DriveToPoseAction(const Pose3d& pose,
                        const PathMotionProfile motionProf = DEFAULT_PATH_MOTION_PROFILE,
                        const bool forceHeadDown  = true,
                        const bool useManualSpeed = false,
                        const Point3f& distThreshold = DEFAULT_POSE_EQUAL_DIST_THRESOLD_MM,
                        const Radians& angleThreshold = DEFAULT_POSE_EQUAL_ANGLE_THRESHOLD_RAD,
                        const float maxPlanningTime = DEFAULT_MAX_PLANNER_COMPUTATION_TIME_S,
                        const float maxReplanPlanningTime = DEFAULT_MAX_PLANNER_REPLAN_COMPUTATION_TIME_S);
      
      DriveToPoseAction(const PathMotionProfile motionProf = DEFAULT_PATH_MOTION_PROFILE,
                        const bool forceHeadDown  = true,
                        const bool useManualSpeed = false); // Note that SetGoal() must be called befure Update()!
      DriveToPoseAction(const std::vector<Pose3d>& poses,
                        const PathMotionProfile motionProf = DEFAULT_PATH_MOTION_PROFILE,
                        const bool forceHeadDown  = true,
                        const bool useManualSpeed = false,
                        const Point3f& distThreshold = DEFAULT_POSE_EQUAL_DIST_THRESOLD_MM,
                        const Radians& angleThreshold = DEFAULT_POSE_EQUAL_ANGLE_THRESHOLD_RAD,
                        const float maxPlanningTime = DEFAULT_MAX_PLANNER_COMPUTATION_TIME_S,
                        const float maxReplanPlanningTime = DEFAULT_MAX_PLANNER_REPLAN_COMPUTATION_TIME_S);
      
      // TODO: Add methods to adjust the goal thresholds from defaults
      
      virtual const std::string& GetName() const override;
      virtual RobotActionType GetType() const override { return RobotActionType::DRIVE_TO_POSE; }
      
      virtual u8 GetAnimTracksToDisable() const override { return (uint8_t)AnimTrackFlag::BODY_TRACK; }
      
    protected:

      virtual ActionResult Init(Robot& robot) override;
      virtual ActionResult CheckIfDone(Robot& robot) override;
      virtual void Cleanup(Robot &robot) override;
      virtual void Reset() override;
      
      Result SetGoal(const Pose3d& pose);
      Result SetGoal(const Pose3d& pose, const Point3f& distThreshold, const Radians& angleThreshold);
      
      // Set possible goal options
      Result SetGoals(const std::vector<Pose3d>& poses);
      Result SetGoals(const std::vector<Pose3d>& poses, const Point3f& distThreshold, const Radians& angleThreshold);
      
      bool IsUsingManualSpeed() {return _useManualSpeed;}
      
      // Don't lock wheels if we're using manual speed control (i.e. "assisted RC")
      virtual bool ShouldLockWheels() const override { return !_useManualSpeed; }
      
      bool     _startedTraversingPath = false;
      
    private:
      bool     _isGoalSet;
      bool     _driveWithHeadDown;
      
      std::vector<Pose3d> _goalPoses;
      size_t              _selectedGoalIndex;
      
      PathMotionProfile _pathMotionProfile;
      
      Point3f  _goalDistanceThreshold;
      Radians  _goalAngleThreshold;
      bool     _useManualSpeed;

      float _maxPlanningTime;
      float _maxReplanPlanningTime;

      float _timeToAbortPlanning;
      
      Signal::SmartHandle _signalHandle;
      
    }; // class DriveToPoseAction
    
    
    // Uses the robot's planner to select the best pre-action pose for the
    // specified action type. Drives there using a DriveToPoseAction. Then
    // moves the robot's head to the angle indicated by the pre-action pose
    // (which may be different from the angle used for path following).
    class DriveToObjectAction : public IAction 
    {
    public:
      DriveToObjectAction(const ObjectID& objectID,
                          const PreActionPose::ActionType& actionType,
                          const PathMotionProfile motionProf = DEFAULT_PATH_MOTION_PROFILE,
                          const f32 predockOffsetDistX_mm = 0,
                          const bool useApproachAngle = false,
                          const f32 approachAngle_rad = 0,
                          const bool useManualSpeed = false);
      
      DriveToObjectAction(const ObjectID& objectID,
                          const f32 distance_mm,
                          const PathMotionProfile motionProf = DEFAULT_PATH_MOTION_PROFILE,
                          const bool useManualSpeed = false);
      
      // TODO: Add version where marker code is specified instead of action?
      //DriveToObjectAction(Robot& robot, const ObjectID& objectID, Vision::Marker::Code code);
      
      virtual const std::string& GetName() const override;
      virtual RobotActionType GetType() const override { return RobotActionType::DRIVE_TO_OBJECT; }
      
      virtual u8 GetAnimTracksToDisable() const override { return (uint8_t)AnimTrackFlag::BODY_TRACK; }
      
      // If set, instead of driving to the nearest preActionPose, only the preActionPose
      // that is most closely aligned with the approach angle is considered.
      void SetApproachAngle(const f32 angle_rad);
      
    protected:
      
      virtual ActionResult Init(Robot& robot) override;
      virtual ActionResult CheckIfDone(Robot& robot) override;
      
      ActionResult InitHelper(Robot& robot, ActionableObject* object);
      ActionResult GetPossiblePoses(const Robot& robot, ActionableObject* object,
                                    std::vector<Pose3d>& possiblePoses,
                                    bool& alreadyInPosition);
      
      virtual void Cleanup(Robot &robot) override;
      virtual void Reset() override;
      
      // Not private b/c DriveToPlaceCarriedObject uses
      ObjectID                   _objectID;
      PreActionPose::ActionType  _actionType;
      f32                        _distance_mm;
      f32                        _predockOffsetDistX_mm;
      bool                       _useManualSpeed;
      CompoundActionSequential   _compoundAction;
      
      bool                       _useApproachAngle;
      Radians                    _approachAngle_rad;
      
      PathMotionProfile          _pathMotionProfile;
    }; // DriveToObjectAction
    
    
    class DriveToPlaceCarriedObjectAction : public DriveToObjectAction
    {
    public:
      DriveToPlaceCarriedObjectAction(const Robot& robot,
                                      const Pose3d& placementPose,
                                      const bool placeOnGround,
                                      const PathMotionProfile motionProfile = DEFAULT_PATH_MOTION_PROFILE,
                                      const bool useExactRotation = false,
                                      const bool useManualSpeed = false);
      
      virtual const std::string& GetName() const override;
      virtual RobotActionType GetType() const override { return RobotActionType::DRIVE_TO_PLACE_CARRIED_OBJECT; }
      
    protected:
      
      virtual ActionResult Init(Robot& robot) override;
      virtual ActionResult CheckIfDone(Robot& robot) override; // Simplified version from DriveToObjectAction
      Pose3d _placementPose;
      
      bool   _useExactRotation;
      
    }; // DriveToPlaceCarriedObjectAction()
    
    
    // Turn in place by a given angle, wherever the robot is when the action
    // is executed.
    class TurnInPlaceAction : public IAction
    {
    public:
      TurnInPlaceAction(const Radians& angle, const bool isAbsolute);
      
      virtual const std::string& GetName() const override;
      virtual RobotActionType GetType() const override { return RobotActionType::TURN_IN_PLACE; }
      
      virtual u8 GetAnimTracksToDisable() const override { return (uint8_t)AnimTrackFlag::BODY_TRACK; }
      
      // Modify default parameters (must be called before Init() to have an effect)
      void SetMaxSpeed(f32 maxSpeed_radPerSec)           { _maxSpeed_radPerSec = maxSpeed_radPerSec; }
      void SetAccel(f32 accel_radPerSec2)                { _accel_radPerSec2 = accel_radPerSec2; }
      void SetTolerance(const Radians& angleTol_rad)     { _angleTolerance = angleTol_rad; }
      void SetVariability(const Radians& angleVar_rad)   { _variability = angleVar_rad; }
      
    protected:
      
      virtual ActionResult Init(Robot& robot) override;
      virtual ActionResult CheckIfDone(Robot& robot) override;
      f32 GetTimeoutInSeconds() const override;
      
    private:
      
      bool IsBodyInPosition(const Robot& robot, Radians& currentAngle) const;
      
      bool    _inPosition = false;
      Radians _targetAngle;
      Radians _angleTolerance = DEG_TO_RAD(5);
      Radians _variability = 0;
      bool    _isAbsoluteAngle;
      f32     _maxSpeed_radPerSec = 50.f;
      f32     _accel_radPerSec2 = 10.f;
      f32     _timeout_sec = -1.f;
      
    }; // class TurnInPlaceAction
    
    
    class MoveHeadToAngleAction : public IAction
    {
    public:
      MoveHeadToAngleAction(const Radians& headAngle, const Radians& tolerance = DEG_TO_RAD(2.f),
                            const Radians& variability = 0);
      
      virtual const std::string& GetName() const override { return _name; }
      virtual RobotActionType GetType() const override { return RobotActionType::MOVE_HEAD_TO_ANGLE; }
      
      virtual u8 GetAnimTracksToDisable() const override { return (uint8_t)AnimTrackFlag::HEAD_TRACK; }
      
      // Modify default parameters (must be called before Init() to have an effect)
      // TODO: Use setters for variability and tolerance too
      void SetMaxSpeed(f32 maxSpeed_radPerSec)   { _maxSpeed_radPerSec = maxSpeed_radPerSec; }
      void SetAccel(f32 accel_radPerSec2)        { _accel_radPerSec2 = accel_radPerSec2; }
      
    protected:
      
      virtual ActionResult Init(Robot& robot) override;
      virtual ActionResult CheckIfDone(Robot& robot) override;
      virtual f32 GetTimeoutInSeconds() const override;
      
    private:
      
      bool IsHeadInPosition(const Robot& robot) const;
      
      Radians     _headAngle;
      Radians     _angleTolerance;
      Radians     _variability;
      
      std::string _name;
      bool        _inPosition;
      
      f32         _maxSpeed_radPerSec = 15.f;
      f32         _accel_radPerSec2   = 20.f;

      f32         _timeout_sec = -1.f;
      
    };  // class MoveHeadToAngleAction
    
    // Set the lift to specified height with a given tolerance. Note that settign
    // the tolerance too small will likely lead to an action timeout.
    class MoveLiftToHeightAction : public IAction
    {
    public:
      
      // Named presets:
      enum class Preset : u8 {
        LOW_DOCK,
        HIGH_DOCK,
        CARRY,
        OUT_OF_FOV // Moves to low or carry, depending on which is closer to current height
      };
      
      MoveLiftToHeightAction(const f32 height_mm, const f32 tolerance_mm = 5.f, const f32 variability = 0);
      MoveLiftToHeightAction(const Preset preset, const f32 tolerance_mm = 5.f);
      
      virtual const std::string& GetName() const override { return _name; };
      virtual RobotActionType GetType() const override { return RobotActionType::MOVE_LIFT_TO_HEIGHT; }
      
      virtual u8 GetAnimTracksToDisable() const override { return (uint8_t)AnimTrackFlag::LIFT_TRACK; }
      
    protected:
      
      static f32 GetPresetHeight(Preset preset);
      static const std::string& GetPresetName(Preset preset);
      
      virtual ActionResult Init(Robot& robot) override;
      virtual ActionResult CheckIfDone(Robot& robot) override;
      
    private:
      
      bool IsLiftInPosition(const Robot& robot) const;
      
      f32          _height_mm;
      f32          _heightTolerance;
      f32          _variability;
      f32          _heightWithVariation;
      
      std::string  _name;
      bool         _inPosition;
      
    }; // class MoveLiftToHeightAction
    
    class PanAndTiltAction : public IAction
    {
    public:
      // Rotate the body according to bodyPan angle and tilt the head according
      // to headTilt angle. Angles are considered relative to current robot pose
      // if isAbsolute==false.
      // If an angle is less than AngleTol, then no movement occurs but the
      // eyes will dart to look at the angle.
      PanAndTiltAction(Radians bodyPan, Radians headTilt,
                       bool isPanAbsolute, bool isTiltAbsolute);
      
      virtual const std::string& GetName() const override { return _name; }
      
      virtual RobotActionType GetType() const override { return RobotActionType::PAN_AND_TILT; }
      
      virtual u8 GetAnimTracksToDisable() const override {
        return (u8)AnimTrackFlag::BODY_TRACK | (u8)AnimTrackFlag::HEAD_TRACK;
      }
      
      // Modify default parameters (must be called before Init() to have an effect)
      void SetMaxPanSpeed(f32 maxSpeed_radPerSec)        { _maxPanSpeed_radPerSec = maxSpeed_radPerSec; }
      void SetPanAccel(f32 accel_radPerSec2)             { _panAccel_radPerSec2 = accel_radPerSec2; }
      void SetPanTolerance(const Radians& angleTol_rad)  { _panAngleTol = angleTol_rad.getAbsoluteVal(); }
      void SetMaxTiltSpeed(f32 maxSpeed_radPerSec)       { _maxTiltSpeed_radPerSec = maxSpeed_radPerSec; }
      void SetTiltAccel(f32 accel_radPerSec2)            { _tiltAccel_radPerSec2 = accel_radPerSec2; }
      void SetTiltTolerance(const Radians& angleTol_rad) { _tiltAngleTol = angleTol_rad.getAbsoluteVal(); }

    protected:
      virtual ActionResult Init(Robot& robot) override;
      virtual ActionResult CheckIfDone(Robot& robot) override;
      virtual void Reset() override;
      
      void SetBodyPanAngle(Radians angle) { _bodyPanAngle = angle; }
      void SetHeadTiltAngle(Radians angle) { _headTiltAngle = angle; }
      
    private:
      CompoundActionParallel _compoundAction;
      
      Radians _bodyPanAngle;
      Radians _headTiltAngle;
      bool    _isPanAbsolute;
      bool    _isTiltAbsolute;
      
      Radians _panAngleTol = DEG_TO_RAD(5);
      f32     _maxPanSpeed_radPerSec = 50.f;
      f32     _panAccel_radPerSec2 = 10.f;
      Radians _tiltAngleTol = DEG_TO_RAD(5);
      f32     _maxTiltSpeed_radPerSec = 15.f;
      f32     _tiltAccel_radPerSec2 = 20.f;
      
      std::string _name = "PanAndTiltAction";
      
    }; // class PanAndTiltAction
    
    
    // Tilt head and rotate body to face the given pose.
    // Use angles specified at construction to control the body rotation.
    class FacePoseAction : public PanAndTiltAction
    {
    public:
      // Note that the rotation in formation in pose will be ignored
      FacePoseAction(const Pose3d& pose, Radians turnAngleTol, Radians maxTurnAngle);
      
      virtual const std::string& GetName() const override;
      virtual RobotActionType GetType() const override { return RobotActionType::FACE_POSE; }
      
    protected:
      virtual ActionResult Init(Robot& robot) override;
      
      FacePoseAction(Radians turnAngleTol, Radians maxTurnAngle);
      void SetPose(const Pose3d& pose);
      virtual Radians GetHeadAngle(f32 heightDiff);
      
    private:
      Pose3d    _poseWrtRobot;
      bool      _isPoseSet;
      Radians   _maxTurnAngle;
      
    }; // class FacePoseAction
    
    
    // Verify that an object exists by facing tilting the head to face its
    // last-known pose and verify that we can still see it. Optionally, you can
    // also require that a specific marker be seen as well.
    class VisuallyVerifyObjectAction : public IAction
    {
    public:
      VisuallyVerifyObjectAction(ObjectID objectID,
                                 Vision::Marker::Code whichCode = Vision::Marker::ANY_CODE);
      
      virtual const std::string& GetName() const override;
      virtual RobotActionType GetType() const override { return RobotActionType::VISUALLY_VERIFY_OBJECT; }
      
    protected:
      virtual ActionResult Init(Robot& robot) override;
      virtual ActionResult CheckIfDone(Robot& robot) override;
      virtual bool ShouldLockWheels() const override { return true; }
      
      // Max amount of time to wait before verifying after moving head that we are
      // indeed seeing the object/marker we expect.
      // TODO: Can this default be reduced?
      virtual f32 GetWaitToVerifyTime() const { return 0.25f; }
      
      ObjectID             _objectID;
      Vision::Marker::Code _whichCode;
      f32                  _waitToVerifyTime;
      
      
      MoveLiftToHeightAction  _moveLiftToHeightAction;
      bool                 _moveLiftToHeightActionDone;
      
    }; // class VisuallyVerifyObjectAction
    
    
    // Tilt head and rotate body to face the specified (marker on an) object.
    // Use angles specified at construction to control the body rotation.
    class FaceObjectAction : public FacePoseAction
    {
    public:
      // If facing the object requires less than turnAngleTol turn, then no
      // turn is performed. If a turn greater than maxTurnAngle is required,
      // the action fails. For angles in between, the robot will first turn
      // to face the object, then tilt its head. To disallow turning, set
      // maxTurnAngle to zero.
      
      FaceObjectAction(ObjectID objectID, Radians turnAngleTol, Radians maxTurnAngle,
                       bool visuallyVerifyWhenDone = false,
                       bool headTrackWhenDone = false);
      
      FaceObjectAction(ObjectID objectID, Vision::Marker::Code whichCode,
                       Radians turnAngleTol, Radians maxTurnAngle,
                       bool visuallyVerifyWhenDone = false,
                       bool headTrackWhenDone = false);
      
      virtual const std::string& GetName() const override;
      virtual RobotActionType GetType() const override { return RobotActionType::FACE_OBJECT; }
      
      virtual void GetCompletionStruct(Robot& robot, ActionCompletedStruct& completionInfo) const override;
      
    protected:
      
      virtual ActionResult Init(Robot& robot) override;
      virtual ActionResult CheckIfDone(Robot& robot) override;
      virtual void Reset() override;
      
      virtual Radians GetHeadAngle(f32 heightDiff) override;
      
      // Override to allow wheel control while facing the object
      virtual bool ShouldLockWheels() const override { return false; }
      
      bool                 _facePoseCompoundActionDone;
      
      VisuallyVerifyObjectAction    _visuallyVerifyAction;
      
      ObjectID             _objectID;
      Vision::Marker::Code _whichCode;
      bool                 _visuallyVerifyWhenDone;
      bool                 _headTrackWhenDone;
      
    }; // FaceObjectAction
    
    
    // Interface for actions that involve "docking" with an object
    class IDockAction : public IAction
    {
    public:
      IDockAction(ObjectID objectID,
                  const bool useManualSpeed = false,
                  const f32 placementOffsetX_mm = 0,
                  const f32 placementOffsetY_mm = 0,
                  const f32 placementOffsetAngle_rad = 0,
                  const bool placeObjectOnGround = false);
      
      virtual ~IDockAction();
      
      // Use a value <= 0 to ignore how far away the robot is from the closest
      // PreActionPose and proceed regardless.
      void SetPreActionPoseAngleTolerance(Radians angleTolerance);
      
      virtual u8 GetAnimTracksToDisable() const override {
        return (uint8_t)AnimTrackFlag::HEAD_TRACK | (uint8_t)AnimTrackFlag::LIFT_TRACK | (uint8_t)AnimTrackFlag::BODY_TRACK;
      }
      
    protected:
      
      // IDockAction implements these two required methods from IAction for its
      // derived classes
      virtual ActionResult Init(Robot& robot) override final;
      virtual ActionResult CheckIfDone(Robot& robot) override final;
      virtual void Cleanup(Robot& robot) override final;
      
      // Most docking actions don't use a second dock marker, but in case they
      // do, they can override this method to choose one from the available
      // preaction poses, given which one was closest.
      virtual const Vision::KnownMarker* GetDockMarker2(const std::vector<PreActionPose>& preActionPoses,
                                                        const size_t closestIndex) { return nullptr; }
      
      // Pure virtual methods that must be implemented by derived classes in
      // order to define the parameters of docking and how to verify success.
      virtual Result SelectDockAction(Robot& robot, ActionableObject* object) = 0;
      virtual PreActionPose::ActionType GetPreActionType() = 0;
      virtual ActionResult Verify(Robot& robot) = 0;
      
      virtual void Reset() override;
      
      // Optional additional delay before verification
      virtual f32 GetVerifyDelayInSeconds() const { return 0.f; }
      
      // Should only lock wheels if we are not using manual speed (i.e. "assisted RC")
      virtual bool ShouldLockWheels() const override { return !_useManualSpeed; }
      
      ObjectID                    _dockObjectID;
      DockAction                _dockAction;
      const Vision::KnownMarker*  _dockMarker;
      const Vision::KnownMarker*  _dockMarker2;
      Radians                     _preActionPoseAngleTolerance;
      f32                         _waitToVerifyTime;
      bool                        _wasPickingOrPlacing;
      bool                        _useManualSpeed;
      FaceObjectAction*           _faceAndVerifyAction;
      f32                         _placementOffsetX_mm;
      f32                         _placementOffsetY_mm;
      f32                         _placementOffsetAngle_rad;
      bool                        _placeObjectOnGroundIfCarrying;
    }; // class IDockAction

    
    // "Docks" to the specified object at the distance specified
    class AlignWithObjectAction : public IDockAction
    {
    public:
      AlignWithObjectAction(ObjectID objectID,
                            f32 distanceFromMarker_mm,
                            const bool useManualSpeed = false);
      virtual ~AlignWithObjectAction();
      
      virtual const std::string& GetName() const override;
      
      virtual RobotActionType GetType() const override {return RobotActionType::ALIGN_WITH_OBJECT;};
      
    protected:
      
      virtual void GetCompletionStruct(Robot& robot, ActionCompletedStruct& completionInfo) const override;
      
      virtual PreActionPose::ActionType GetPreActionType() override { return PreActionPose::ActionType::DOCKING; }

      virtual Result SelectDockAction(Robot& robot, ActionableObject* object) override;
      
      virtual ActionResult Verify(Robot& robot) override;
      
      virtual void Reset() override;
      
    }; // class AlignWithObjectAction
    
    
    // Picks up the specified object.
    class PickupObjectAction : public IDockAction
    {
    public:
      PickupObjectAction(ObjectID objectID,
                         const bool useManualSpeed = false);
      virtual ~PickupObjectAction();
      
      virtual const std::string& GetName() const override;
      
      // Override to determine type (pick/place, low/high) dynamically depending
      // on what we were doing.
      virtual RobotActionType GetType() const override;
      
    protected:
      
      virtual void GetCompletionStruct(Robot& robot, ActionCompletedStruct& completionInfo) const override;
      
      virtual PreActionPose::ActionType GetPreActionType() override { return PreActionPose::ActionType::DOCKING; }
      
      virtual Result SelectDockAction(Robot& robot, ActionableObject* object) override;
      
      virtual ActionResult Verify(Robot& robot) override;
      
      virtual void Reset() override;
      
      // For verifying if we successfully picked up the object
      Pose3d _dockObjectOrigPose;
      
    }; // class PickupObjectAction
    

    // If carrying an object, places it on or relative to the specified object.
    class PlaceRelObjectAction : public IDockAction
    {
    public:
      PlaceRelObjectAction(ObjectID objectID,
                           const bool placeOnGround = false,                           
                           const f32 placementOffsetX_mm = 0,
                           const bool useManualSpeed = false);
      virtual ~PlaceRelObjectAction();
      
      virtual const std::string& GetName() const override;
      
      // Override to determine type (pick/place, low/high) dynamically depending
      // on what we were doing.
      virtual RobotActionType GetType() const override;
      
    protected:
      
      virtual void GetCompletionStruct(Robot& robot, ActionCompletedStruct& completionInfo) const override;
      
      virtual PreActionPose::ActionType GetPreActionType() override { return PreActionPose::ActionType::PLACE_RELATIVE; }
      
      virtual Result SelectDockAction(Robot& robot, ActionableObject* object) override;
      
      virtual ActionResult Verify(Robot& robot) override;
      
      virtual void Reset() override;
      
      // For verifying if we successfully picked up the object
      //Pose3d _dockObjectOrigPose;
      
      // If placing an object, we need a place to store what robot was
      // carrying, for verification.
      ObjectID                   _carryObjectID;
      const Vision::KnownMarker* _carryObjectMarker;
      
      IActionRunner*             _placementVerifyAction;
      bool                       _verifyComplete; // used in PLACE modes
      
    }; // class PlaceRelObjectAction
    
    
    // If not carrying anything, rolls the specified object.
    // If carrying an object, fails.
    class RollObjectAction : public IDockAction
    {
    public:
      RollObjectAction(ObjectID objectID,
                       const bool useManualSpeed = false);
      virtual ~RollObjectAction();
      
      virtual const std::string& GetName() const override;
      
      // Override to determine type (low roll, or potentially other rolls) dynamically depending
      // on what we were doing.
      virtual RobotActionType GetType() const override;

      // Override completion signal to fill in information about rolled objects
      virtual void GetCompletionStruct(Robot& robot, ActionCompletedStruct& completionInfo) const override;

    protected:
      
      virtual PreActionPose::ActionType GetPreActionType() override { return PreActionPose::ROLLING; }
      
      virtual Result SelectDockAction(Robot& robot, ActionableObject* object) override;
      
      virtual ActionResult Verify(Robot& robot) override;
      
      virtual void Reset() override;

      // For verifying if we successfully rolled the object
      Pose3d _dockObjectOrigPose;
      
      const Vision::KnownMarker* _expectedMarkerPostRoll;
      
      IActionRunner*             _rollVerifyAction;
      
    }; // class RollObjectAction
    
    
    // If not carrying anything, pops a wheelie off of the specified object
    class PopAWheelieAction : public IDockAction
    {
    public:
      PopAWheelieAction(ObjectID objectID,
                       const bool useManualSpeed = false);
      virtual ~PopAWheelieAction();
      
      virtual const std::string& GetName() const override;
      
      // Override to determine type (low roll, or potentially other rolls) dynamically depending
      // on what we were doing.
      virtual RobotActionType GetType() const override;
      
      // Override completion signal to fill in information about rolled objects
      virtual void GetCompletionStruct(Robot& robot, ActionCompletedStruct& completionInfo) const override;
      
    protected:
      
      virtual PreActionPose::ActionType GetPreActionType() override { return PreActionPose::ROLLING; }
      
      virtual Result SelectDockAction(Robot& robot, ActionableObject* object) override;
      
      virtual ActionResult Verify(Robot& robot) override;
      
     // virtual void Reset() override;
      
    }; // class PopAWheelieAction

    

    // Compound action for driving to an object, visually verifying it can still be seen,
    // and then driving to it until it is at the specified distance (i.e. distanceFromMarker_mm)
    // from the marker.
    // @param distanceFromMarker_mm - The distance from the marker along it's normal axis that the robot should stop at.
    // @param useApproachAngle  - If true, then only the preAction pose that results in a robot
    //                            approach angle closest to approachAngle_rad is considered.
    // @param approachAngle_rad - The desired docking approach angle of the robot in world coordinates.
    class DriveToAlignWithObjectAction : public CompoundActionSequential
    {
    public:
      DriveToAlignWithObjectAction(const ObjectID& objectID,
                                   const f32 distanceFromMarker_mm,
                                   const PathMotionProfile motionProfile = DEFAULT_PATH_MOTION_PROFILE,
                                   const bool useApproachAngle = false,
                                   const f32 approachAngle_rad = 0,
                                   const bool useManualSpeed = false);
      
      // GetType returns the type from the AlignWithObjectAction
      virtual RobotActionType GetType() const override { return RobotActionType::ALIGN_WITH_OBJECT; }
      
      // Use AlignWithObjectAction's completion info
      virtual void GetCompletionStruct(Robot& robot, ActionCompletedStruct& completionInfo) const override {
        _actions.back().second->GetCompletionStruct(robot, completionInfo);
      }
      
    };
    

    
    // Common compound action for driving to an object, visually verifying we
    // can still see it, and then picking it up.
    // @param useApproachAngle  - If true, then only the preAction pose that results in a robot
    //                            approach angle closest to approachAngle_rad is considered.
    // @param approachAngle_rad - The desired docking approach angle of the robot in world coordinates.
    class DriveToPickupObjectAction : public CompoundActionSequential
    {
    public:
      DriveToPickupObjectAction(const ObjectID& objectID,
                                const PathMotionProfile motionProfile = DEFAULT_PATH_MOTION_PROFILE,
                                const bool useApproachAngle = false,
                                const f32 approachAngle_rad = 0,
                                const bool useManualSpeed = false);
      
      // GetType returns the type from the PickupObjectAction, which is
      // determined dynamically
      virtual RobotActionType GetType() const override { return _actions.back().second->GetType(); }
      
      // Use PickupObjectAction's completion info
      virtual void GetCompletionStruct(Robot& robot, ActionCompletedStruct& completionInfo) const override {
        _actions.back().second->GetCompletionStruct(robot, completionInfo);
      }
      
    };
    

    // Common compound action for driving to an object, visually verifying we
    // can still see it, and then placing an object on it.
    // @param objectID         - object to place carried object on
    class DriveToPlaceOnObjectAction : public CompoundActionSequential
    {
    public:
     
      // Places carried object on top of objectID
      DriveToPlaceOnObjectAction(const Robot& robot,
                                  const ObjectID& objectID,
                                  const PathMotionProfile motionProf = DEFAULT_PATH_MOTION_PROFILE,
                                  const bool useApproachAngle = false,
                                  const f32 approachAngle_rad = 0,
                                 const bool useManualSpeed = false);

      // GetType returns the type from the PlaceRelObjectAction, which is
      // determined dynamically
      virtual RobotActionType GetType() const override { return _actions.back().second->GetType(); }
      
      // Use PlaceRelObjectAction's completion info
      virtual void GetCompletionStruct(Robot& robot, ActionCompletedStruct& completionInfo) const override {
        _actions.back().second->GetCompletionStruct(robot, completionInfo);
      }
      
    };

    
    
    // Common compound action for driving to an object, visually verifying we
    // can still see it, and then placing an object relative to it.
    // @param placementOffsetX_mm - The desired distance between the center of the docking marker
    //                              and the center of the object that is being placed, along the
    //                              direction of the docking marker's normal.
    // @param useApproachAngle  - If true, then only the preAction pose that results in a robot
    //                            approach angle closest to approachAngle_rad is considered.
    // @param approachAngle_rad - The desired docking approach angle of the robot in world coordinates.
    class DriveToPlaceRelObjectAction : public CompoundActionSequential
    {
    public:
      // Place carried object on ground at specified placementOffset from objectID,
      // chooses preAction pose closest to approachAngle_rad if useApproachAngle == true.
      DriveToPlaceRelObjectAction(const ObjectID& objectID,
                                  const PathMotionProfile motionProfile = DEFAULT_PATH_MOTION_PROFILE,
                                  const f32 placementOffsetX_mm = 0,
                                  const bool useApproachAngle = false,
                                  const f32 approachAngle_rad = 0,
                                  const bool useManualSpeed = false);
      
      
      // GetType returns the type from the PlaceRelObjectAction, which is
      // determined dynamically
      virtual RobotActionType GetType() const override { return _actions.back().second->GetType(); }
      
      // Use PlaceRelObjectAction's completion info
      virtual void GetCompletionStruct(Robot& robot, ActionCompletedStruct& completionInfo) const override {
        _actions.back().second->GetCompletionStruct(robot, completionInfo);
      }
      
    };
    
    
    // Common compound action for driving to an object, visually verifying we
    // can still see it, and then rolling it.
    // @param useApproachAngle  - If true, then only the preAction pose that results in a robot
    //                            approach angle closest to approachAngle_rad is considered.
    // @param approachAngle_rad - The desired docking approach angle of the robot in world coordinates.
    class DriveToRollObjectAction : public CompoundActionSequential
    {
    public:
      DriveToRollObjectAction(const ObjectID& objectID,
                              const PathMotionProfile motionProfile = DEFAULT_PATH_MOTION_PROFILE,
                              const bool useApproachAngle = false,
                              const f32 approachAngle_rad = 0,
                              const bool useManualSpeed = false);
      
      // GetType returns the type from the PlaceRelObjectAction, which is
      // determined dynamically
      virtual RobotActionType GetType() const override { return _actions.back().second->GetType(); }
      
      // Use RollObjectAction's completion signal
      virtual void GetCompletionStruct(Robot& robot, ActionCompletedStruct& completionInfo) const override {
        _actions.back().second->GetCompletionStruct(robot, completionInfo);
      }
      
    };

    // Common compound action for driving to an object and popping a wheelie off of it
    // @param useApproachAngle  - If true, then only the preAction pose that results in a robot
    //                            approach angle closest to approachAngle_rad is considered.
    // @param approachAngle_rad - The desired docking approach angle of the robot in world coordinates.
    class DriveToPopAWheelieAction : public CompoundActionSequential
    {
    public:
      DriveToPopAWheelieAction(const ObjectID& objectID,
                               const PathMotionProfile motionProfile = DEFAULT_PATH_MOTION_PROFILE,
                               const bool useApproachAngle = false,
                               const f32 approachAngle_rad = 0,
                               const bool useManualSpeed = false);
      
      // GetType returns the type from the PlaceRelObjectAction, which is
      // determined dynamically
      virtual RobotActionType GetType() const override { return _actions.back().second->GetType(); }
      
      // Use RollObjectAction's completion signal
      virtual void GetCompletionStruct(Robot& robot, ActionCompletedStruct& completionInfo) const override {
        _actions.back().second->GetCompletionStruct(robot, completionInfo);
      }
      
    };
    
    class PlaceObjectOnGroundAction : public IAction
    {
    public:
      
      PlaceObjectOnGroundAction();
      virtual ~PlaceObjectOnGroundAction();
      
      virtual const std::string& GetName() const override;
      virtual RobotActionType GetType() const override { return RobotActionType::PLACE_OBJECT_LOW; }
      
      virtual u8 GetAnimTracksToDisable() const override { return (uint8_t)AnimTrackFlag::LIFT_TRACK; }
      
    protected:
      
      virtual ActionResult Init(Robot& robot) override;
      virtual ActionResult CheckIfDone(Robot& robot) override;
      virtual void Reset() override;
      
      // Need longer than default for check if done:
      virtual f32 GetCheckIfDoneDelayInSeconds() const override { return 1.5f; }
      
      ObjectID                    _carryingObjectID;
      const Vision::KnownMarker*  _carryObjectMarker;
      FaceObjectAction*           _faceAndVerifyAction;
      
    }; // class PlaceObjectOnGroundAction
    
    
    // Common compound action
    // @param placementPose    - The pose in which the carried object should be placed.
    // @param useExactRotation - If true, then the carried object is placed in the exact
    //                           6D pose represented by placement pose. Otherwise,
    //                           x,y and general axis alignment with placementPose rotation
    //                           are the only constraints.
    class PlaceObjectOnGroundAtPoseAction : public CompoundActionSequential
    {
    public:
      PlaceObjectOnGroundAtPoseAction(const Robot& robot,
                                      const Pose3d& placementPose,
                                      const PathMotionProfile motionProfile = DEFAULT_PATH_MOTION_PROFILE,
                                      const bool useExactRotation = false,
                                      const bool useManualSpeed = false);

      virtual RobotActionType GetType() const override { return RobotActionType::PLACE_OBJECT_LOW; }
    };
    
    class CrossBridgeAction : public IDockAction
    {
    public:
      CrossBridgeAction(ObjectID bridgeID, const bool useManualSpeed);
      
      virtual const std::string& GetName() const override;
      virtual RobotActionType GetType() const override { return RobotActionType::CROSS_BRIDGE; }
      
    protected:
      
      virtual PreActionPose::ActionType GetPreActionType() override { return PreActionPose::ENTRY; }
      
      virtual Result SelectDockAction(Robot& robot, ActionableObject* object) override;
      
      virtual ActionResult Verify(Robot& robot) override;
      
      // Crossing a bridge _does_ require the second dockMarker,
      // so override the virtual method for setting it
      virtual const Vision::KnownMarker* GetDockMarker2(const std::vector<PreActionPose>& preActionPoses,
                                                        const size_t closestIndex) override;
      
    }; // class CrossBridgeAction
    
    
    class AscendOrDescendRampAction : public IDockAction
    {
    public:
      AscendOrDescendRampAction(ObjectID rampID, const bool useManualSpeed);
      
      virtual const std::string& GetName() const override;
      virtual RobotActionType GetType() const override { return RobotActionType::ASCEND_OR_DESCEND_RAMP; }
      
    protected:
      
      virtual Result SelectDockAction(Robot& robot, ActionableObject* object) override;
      
      virtual ActionResult Verify(Robot& robot) override;
      
      virtual PreActionPose::ActionType GetPreActionType() override { return PreActionPose::ENTRY; }
      
      // Give the robot a little longer to start ascending/descending before
      // checking if it is done
      virtual f32 GetCheckIfDoneDelayInSeconds() const override { return 1.f; }
      
    }; // class AscendOrDesceneRampAction
    
    
    class MountChargerAction : public IDockAction
    {
    public:
      MountChargerAction(ObjectID chargerID, const bool useManualSpeed);
      
      virtual const std::string& GetName() const override;
      virtual RobotActionType GetType() const override { return RobotActionType::MOUNT_CHARGER; }
      
    protected:
      
      virtual Result SelectDockAction(Robot& robot, ActionableObject* object) override;
      
      virtual ActionResult Verify(Robot& robot) override;
      
      virtual PreActionPose::ActionType GetPreActionType() override { return PreActionPose::ENTRY; }
      
      // Give the robot a little longer to start ascending/descending before
      // checking if it is done
      virtual f32 GetCheckIfDoneDelayInSeconds() const override { return 1.f; }
      
    }; // class MountChargerAction
    
    
    // This is just a selector for AscendOrDescendRampAction or
    // CrossBridgeAction, depending on the object's type.
    class TraverseObjectAction : public IActionRunner
    {
    public:
      TraverseObjectAction(ObjectID objectID, const bool useManualSpeed);
      virtual ~TraverseObjectAction();
      
      virtual const std::string& GetName() const override;
      virtual RobotActionType GetType() const override { return RobotActionType::TRAVERSE_OBJECT; }
      
      virtual void Cleanup(Robot& robot) override {
        if(_chosenAction != nullptr) {
          _chosenAction->Cleanup(robot);
        }
      }
      
    protected:
      
      // Update will just call the chosenAction's implementation
      virtual ActionResult UpdateInternal(Robot& robot) override;
      virtual void Reset() override;
      
      ObjectID       _objectID;
      IActionRunner* _chosenAction;
      bool           _useManualSpeed;
      
    }; // class TraverseObjectAction
    
    
    // Common compound action
    class DriveToAndTraverseObjectAction : public CompoundActionSequential
    {
    public:
      DriveToAndTraverseObjectAction(const ObjectID& objectID,
                                     const PathMotionProfile motionProfile = DEFAULT_PATH_MOTION_PROFILE,
                                     const bool useManualSpeed = false)
      : CompoundActionSequential({
        new DriveToObjectAction(objectID,
                                PreActionPose::ENTRY,
                                motionProfile,
                                0,
                                false,
                                0,
                                useManualSpeed),
        new TraverseObjectAction(objectID, useManualSpeed)})
      {
        
      }
      
      virtual RobotActionType GetType() const override { return RobotActionType::DRIVE_TO_AND_TRAVERSE_OBJECT; }
      
    };
    
    
    class PlayAnimationAction : public IAction
    {
    public:
      PlayAnimationAction(const std::string& animName,
                          const u32 numLoops = 1);
      
      virtual const std::string& GetName() const override { return _name; }
      virtual RobotActionType GetType() const override { return RobotActionType::PLAY_ANIMATION; }
      
      virtual void GetCompletionStruct(Robot& robot, ActionCompletedStruct& completionInfo) const override;
      
    protected:
      
      virtual ActionResult Init(Robot& robot) override;
      virtual ActionResult CheckIfDone(Robot& robot) override;
      virtual void Cleanup(Robot& robot) override;
      
      //AnimationID_t _animID;
      std::string   _animName;
      std::string   _name;
      u32           _numLoops;
      bool          _startedPlaying;
      u8            _animTag;
      
    }; // class PlayAnimationAction
    
    
    class PlaySoundAction : public IAction
    {
    public:
      PlaySoundAction(const std::string& soundName);
      
      virtual const std::string& GetName() const override { return _name; }
      virtual RobotActionType GetType() const override { return RobotActionType::PLAY_SOUND; }
      
    protected:
      
      virtual ActionResult CheckIfDone(Robot& robot) override;
      
      std::string _soundName;
      std::string _name;
      
    }; // class PlaySoundAction
    
    // Waits for a specified amount of time in seconds, from the time the action
    // is begun. Returns RUNNING while waiting and SUCCESS when the time has
    // elapsed.
    class WaitAction : public IAction
    {
    public:
      WaitAction(f32 waitTimeInSeconds);
      
      virtual const std::string& GetName() const override { return _name; }
      virtual RobotActionType GetType() const override { return RobotActionType::WAIT; }
      
    protected:
      
      virtual ActionResult Init(Robot& robot) override;
      virtual ActionResult CheckIfDone(Robot& robot) override;
      
      f32         _waitTimeInSeconds;
      f32         _doneTimeInSeconds;
      std::string _name;
      
    };
  } // namespace Cozmo
} // namespace Anki

#endif // ANKI_COZMO_ACTIONS_H

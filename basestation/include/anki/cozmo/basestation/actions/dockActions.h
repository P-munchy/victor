/**
 * File: dockActions.h
 *
 * Author: Andrew Stein
 * Date:   8/29/2014
 *
 * Description: Implements docking related cozmo-specific actions, derived from the IAction interface.
 *
 *
 * Copyright: Anki, Inc. 2014
 **/

#ifndef ANKI_COZMO_DOCKACTIONS_H
#define ANKI_COZMO_DOCKACTIONS_H

#include "clad/types/actionTypes.h"
#include "anki/cozmo/basestation/actions/actionInterface.h"
#include "anki/cozmo/basestation/actions/basicActions.h"
#include "anki/cozmo/basestation/actions/compoundActions.h"
#include "clad/types/animationKeyFrames.h"
#include "clad/types/dockingSignals.h"
#include "anki/cozmo/basestation/animation/animationStreamer.h"
#include "util/helpers/templateHelpers.h"
#include "clad/types/animationTrigger.h"

#include <set>

namespace Anki {
  
  namespace Vision {
    // Forward Declarations:
    class KnownMarker;
  }
  
  namespace Cozmo {
    
    // Forward Declarations:
    class Robot;
    class Animation;
    class DriveToPlaceCarriedObjectAction;
    
    Point2f ComputePreActionPoseDistThreshold(const Pose3d& preActionPose,
                                              const Pose3d& actionObject,
                                              const Radians& preActionPoseAngleTolerance);

    // Interface for actions that involve "docking" with an object
    class IDockAction : public IAction
    {
    public:
      IDockAction(Robot& robot,
                  ObjectID objectID,
                  const std::string name,
                  const RobotActionType type,
                  const bool useManualSpeed = false);
      
      virtual ~IDockAction();
      
      // If true robot will check that it is close enough to the closest preaction pose before docking
      // If false robot will dock with closest visible marker on dockObject from current position
      void SetDoNearPredockPoseCheck(bool doCheck) { _doNearPredockPoseCheck = doCheck; }
      
      // Use a value <= 0 to ignore how far away the robot is from the closest
      // PreActionPose and proceed to dock with marker corresponding to closest preaction pose.
      void SetPreActionPoseAngleTolerance(Radians angleTolerance);
      
      // Set docking speed and acceleration
      void SetSpeedAndAccel(f32 speed_mmps, f32 accel_mmps2, f32 decel_mmps2);
      void SetSpeed(f32 speed_mmps);
      void SetAccel(f32 accel_mmps2, f32 decel_mmps2);
      
      // Set placement offset relative to marker
      void SetPlacementOffset(f32 offsetX_mm, f32 offsetY_mm, f32 offsetAngle_rad);
      
      // Set whether or not to place carried object on ground
      void SetPlaceOnGround(bool placeOnGround);
      
      // Sets the animation to play when lift moves after docking.
      // The animation should only contain a sound track!
      void SetPostDockLiftMovingAnimation(AnimationTrigger animTrigger);
      
      void SetDockingMethod(DockingMethod dockingMethod) { _dockingMethod = dockingMethod; }
      
      void SetDoLiftLoadCheck(bool enable) { _doLiftLoadCheck = enable; }
      
      void SetNumDockingRetries(u8 numRetries) { _numDockingRetries = numRetries; }
      
      // The offset for the preDock pose as opposed to the offset for the actual docking manuever
      // Is used when checking if we are close enough to the preDock pose
      void SetPreDockPoseDistOffset(f32 offset) { _preDockPoseDistOffsetX_mm = offset; }
      
      // Whether or not the action will check that we are currently seeing a specific marker
      // (the one corresponding to the closest preDock pose) on the object before docking or
      // that we are seeing any marker on the object
      // By default this is false (the action is looking for a specific marker)
      void SetShouldVisuallyVerifyObjectOnly(const bool b) { _visuallyVerifyObjectOnly = b; }
      
      // Whether or not we should look up to check if there is an object above the dockObject
      void SetShouldCheckForObjectOnTopOf(const bool b) { _checkForObjectOnTopOf = b; }
      
      // Whether or not to suppress the given behavior during this action
      void SetShouldSuppressReactionaryBehavior(ReactionTrigger behavior) { _reactionTriggersToSuppress.insert(behavior); }
      
      struct PreActionPoseInput
      {
        // Inputs
        ActionableObject* object;
        PreActionPose::ActionType preActionPoseType;
        bool doNearPreDockPoseCheck;
        f32 preActionPoseAngleTolerance;
        f32 preDockPoseDistOffsetX_mm;
        bool useApproachAngle;
        f32 approachAngle_rad;
        
        PreActionPoseInput(ActionableObject* object,
                          PreActionPose::ActionType preActionPoseType,
                          bool doNearPreDockPoseCheck,
                          f32 preDockPoseDistOffsetX_mm,
                          f32 preActionPoseAngleTolerance,
                          bool useApproachAngle,
                          f32 approachAngle_rad)
        : object(object)
        , preActionPoseType(preActionPoseType)
        , doNearPreDockPoseCheck(doNearPreDockPoseCheck)
        , preActionPoseAngleTolerance(preActionPoseAngleTolerance)
        , preDockPoseDistOffsetX_mm(preDockPoseDistOffsetX_mm)
        , useApproachAngle(useApproachAngle)
        , approachAngle_rad(approachAngle_rad)
        {
          
        }
      };
      
      struct PreActionPoseOutput
      {
        ActionResult actionResult;
        std::vector<PreActionPose> preActionPoses;
        size_t closestIndex;
        Point2f closestPoint;
        bool robotAtClosestPreActionPose;
        Point2f distThresholdUsed;
        
        PreActionPoseOutput()
        : actionResult(ActionResult::NOT_STARTED)
        , closestIndex(-1)
        , robotAtClosestPreActionPose(false)
        , distThresholdUsed(-1,-1)
        {
          
        }
      };
      
      // Computes that angle (wrt world) at which the robot would have to approach the given pose
      // such that it places the carried object at the given pose
      static ActionResult ComputePlacementApproachAngle(const Robot& robot,
                                                        const Pose3d& placementPose,
                                                        f32& approachAngle_rad);
      
      static void GetPreActionPoses(Robot& robot, const PreActionPoseInput& input, PreActionPoseOutput& output);
      
      // Whether or not the lift is believed to be carrying something based on liftLoadCheck
      // at the end of a pickup action.
      enum class LiftLoadState : uint8_t {
        UNKNOWN,     // LiftLoad message was never received from robot
        HAS_LOAD,
        HAS_NO_LOAD
      };
      
    protected:
      
      // IDockAction implements these two required methods from IAction for its
      // derived classes
      virtual ActionResult Init() override final;
      virtual ActionResult CheckIfDone() override final;
      
      // Derived classes should override if they want to perform checks that may
      // be dependent on the world state which may not be true when the action
      // is created.
      virtual ActionResult InitInternal() { return ActionResult::SUCCESS;}

      
      // Most docking actions don't use a second dock marker, but in case they
      // do, they can override this method to choose one from the available
      // preaction poses, given which one was closest.
      virtual const Vision::KnownMarker* GetDockMarker2(const std::vector<PreActionPose>& preActionPoses,
                                                        const size_t closestIndex) { return nullptr; }
      
      // Pure virtual methods that must be implemented by derived classes in
      // order to define the parameters of docking and how to verify success.
      virtual ActionResult SelectDockAction(ActionableObject* object) = 0;
      virtual PreActionPose::ActionType GetPreActionType() = 0;
      virtual ActionResult Verify() = 0;
      
      // Optional additional delay before verification
      virtual f32 GetVerifyDelayInSeconds() const { return 0.f; }
      
      // Subclasses should call this because it sets the interaction result
      virtual void GetCompletionUnion(ActionCompletedUnion& completionUnion) const override {
        // TODO: Annoying we have to copy this out, bet the Get_() method is const...
        ObjectInteractionCompleted interactionCompleted;
        interactionCompleted.numObjects = 1;
        interactionCompleted.objectIDs[0] = _dockObjectID;
        completionUnion.Set_objectInteractionCompleted(interactionCompleted);
      }
      
      // Purely to shorten the name for nice whitespace alignment
      using UniqueCompoundPtr = std::unique_ptr<ICompoundAction>;
      
      ObjectID                   _dockObjectID;
      DockAction                 _dockAction;
      const Vision::KnownMarker* _dockMarker                     = nullptr;
      const Vision::KnownMarker* _dockMarker2                    = nullptr;
      Radians                    _preActionPoseAngleTolerance    = DEFAULT_PREDOCK_POSE_ANGLE_TOLERANCE;
      f32                        _waitToVerifyTime               = -1;
      bool                       _wasPickingOrPlacing            = false;
      bool                       _useManualSpeed                 = false;
      UniqueCompoundPtr          _faceAndVerifyAction            = nullptr;
      f32                        _placementOffsetX_mm            = 0;
      f32                        _placementOffsetY_mm            = 0;
      f32                        _placementOffsetAngle_rad       = 0;
      bool                       _placeObjectOnGroundIfCarrying  = false;
      f32                        _dockSpeed_mmps                 = DEFAULT_PATH_MOTION_PROFILE.dockSpeed_mmps;
      f32                        _dockAccel_mmps2                = DEFAULT_PATH_MOTION_PROFILE.dockAccel_mmps2;
      f32                        _dockDecel_mmps2                = DEFAULT_PATH_MOTION_PROFILE.dockDecel_mmps2;
      bool                       _doNearPredockPoseCheck         = true;
      u8                         _numDockingRetries              = 0;
      DockingMethod              _dockingMethod                  = DockingMethod::BLIND_DOCKING;
      f32                        _preDockPoseDistOffsetX_mm      = 0;
      bool                       _checkForObjectOnTopOf          = true;
      bool                       _doLiftLoadCheck                = false;
      LiftLoadState              _liftLoadState                  = LiftLoadState::UNKNOWN;
      std::set<ReactionTrigger>  _reactionTriggersToSuppress;
      
    private:
    
      // Sets up the turnTowardsObject action and the "glance up to see if there is a block on top of the
      // block we are docking with" action
      void SetupTurnAndVerifyAction(const ObservableObject* dockObject);
      
      // Handler for when lift begins to move so that we can play an accompanying sound
      Signal::SmartHandle        _liftMovingSignalHandle;
      
      // Handler for when lift load message is received
      Signal::SmartHandle        _liftLoadSignalHandle;
      
      // Name of animation to play when moving lift post-dock
      AnimationTrigger           _liftMovingAnimation = AnimationTrigger::Count;
      
      AnimationStreamer::Tag     _squintLayerTag = AnimationStreamer::NotAnimatingTag;
      
      bool _lightsSet                = false;
      bool _visuallyVerifyObjectOnly = false;
      
    }; // class IDockAction
    
    
    // If not carrying anything, pops a wheelie off of the specified object
    class PopAWheelieAction : public IDockAction
    {
    public:
      PopAWheelieAction(Robot& robot, ObjectID objectID, const bool useManualSpeed = false);
      
      // Override completion signal to fill in information about rolled objects
      virtual void GetCompletionUnion(ActionCompletedUnion& completionUnion) const override;
      
    protected:
      
      virtual PreActionPose::ActionType GetPreActionType() override { return PreActionPose::ROLLING; }
      
      virtual ActionResult SelectDockAction(ActionableObject* object) override;
      
      virtual ActionResult Verify() override;
      
    }; // class PopAWheelieAction
    
    // If not carrying anything, does a face plant by knocking over a stack of blocks
    class FacePlantAction : public IDockAction
    {
    public:
      FacePlantAction(Robot& robot, ObjectID objectID, const bool useManualSpeed = false);
      
      virtual void GetCompletionUnion(ActionCompletedUnion& completionUnion) const override;
      
    protected:
      
      static constexpr f32 kMaxSuccessfulPitchAngle_rad = DEG_TO_RAD(-70.f);
      
      virtual PreActionPose::ActionType GetPreActionType() override { return PreActionPose::DOCKING; }
      
      virtual ActionResult SelectDockAction(ActionableObject* object) override;
      
      virtual ActionResult Verify() override;
      
    }; // class FacePlantAction
    
    
    // "Docks" to the specified object at the distance specified
    class AlignWithObjectAction : public IDockAction
    {
    public:
      AlignWithObjectAction(Robot& robot,
                            ObjectID objectID,
                            const f32 distanceFromMarker_mm,
                            const AlignmentType alignmentType = AlignmentType::CUSTOM,
                            const bool useManualSpeed = false);
      
      virtual ~AlignWithObjectAction();
      
    protected:
      
      virtual void GetCompletionUnion(ActionCompletedUnion& completionUnion) const override;
      
      virtual PreActionPose::ActionType GetPreActionType() override { return PreActionPose::ActionType::PLACE_RELATIVE; }
      
      virtual ActionResult SelectDockAction(ActionableObject* object) override;
      
      virtual ActionResult Verify() override;
      
    }; // class AlignWithObjectAction
    
    
    // Picks up the specified object.
    class PickupObjectAction : public IDockAction
    {
    public:
      PickupObjectAction(Robot& robot,
                         ObjectID objectID,
                         const bool useManualSpeed = false);
      
      virtual ~PickupObjectAction();

    protected:
      
      virtual void GetCompletionUnion(ActionCompletedUnion& completionUnion) const override;
      
      virtual PreActionPose::ActionType GetPreActionType() override { return PreActionPose::ActionType::DOCKING; }
      
      virtual ActionResult SelectDockAction(ActionableObject* object) override;
      
      virtual ActionResult Verify() override;
      
      // For verifying if we successfully picked up the object
      Pose3d _dockObjectOrigPose;
      
    private:
    
      std::unique_ptr<IActionRunner> _verifyAction = nullptr;
      bool                           _verifyActionDone = false;
      TimeStamp_t                    _firstVerifyCallTime = 0;
      
      const u32 kLiftLoadTimeout_ms = 500;
      u32 _liftLoadWaitTime_ms = 0;
      
      // The max amount of time that cube motion is allowed to be moving after robot completes backup.
      // This is to check that the cube is not in the user's hands.
      const u32 kMaxObjectStillMovingAfterRobotStopTime_ms = 500;
      
      // The max amount of time that a cube is allowed to have not been moving before the point that the robot completes backup.
      // This is to check to make sure the cube was moving at all during the pickup action.
      const u32 kMaxObjectHasntMovedBeforeRobotStopTime_ms = 500;
      
      // Same as kMaxObjectHasntMovedBeforeRobotStopTime_ms but for high pickup.
      // which often results in only brief motion when cube is first lifted compared to low pickup.
      // This means the high pickup action is easier to fool if you move the block out of the way at
      // the last second and set it down somewhere, but... you know... just stop being a dick.
      const u32 kMaxObjectHasntMovedBeforeRobotStopTimeForHighPickup_ms = 2000;
      
    }; // class PickupObjectAction
    
    
    class PlaceObjectOnGroundAction : public IAction
    {
    public:
      
      PlaceObjectOnGroundAction(Robot& robot);
      virtual ~PlaceObjectOnGroundAction();
      
    protected:
      
      virtual ActionResult Init() override;
      virtual ActionResult CheckIfDone() override;
      
      virtual void GetCompletionUnion(ActionCompletedUnion& completionUnion) const override;
      
      ObjectID                       _carryingObjectID;
      const Vision::KnownMarker*     _carryObjectMarker = nullptr;
      std::unique_ptr<IActionRunner> _faceAndVerifyAction = nullptr;
      bool                           _startedPlacing = false;
      
      
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
      PlaceObjectOnGroundAtPoseAction(Robot& robot,
                                      const Pose3d& placementPose,
                                      const bool useExactRotation = false,
                                      const bool useManualSpeed = false,
                                      const bool checkFreeDestination = false,
                                      const float destinationObjectPadding_mm = 0.0f);
      
      void SetMotionProfile(const PathMotionProfile& motionProfile);
      
    private:
      std::weak_ptr<IActionRunner> _driveAction;
    };
    
    
    // If carrying an object, places it on or relative to the specified object.
    class PlaceRelObjectAction : public IDockAction
    {
    public:
      PlaceRelObjectAction(Robot& robot,
                           ObjectID objectID,
                           const bool placeOnGround = false,
                           const f32 placementOffsetX_mm = 0,
                           const f32 placementOffsetY_mm = 0,
                           const bool useManualSpeed = false,
                           const bool relativeCurrentMarker = true);
      virtual ~PlaceRelObjectAction()
      {
        if(_placementVerifyAction != nullptr)
        {
          _placementVerifyAction->PrepForCompletion();
        }
      }
      
      virtual ActionResult InitInternal() override;
      
    protected:
      
      virtual void GetCompletionUnion(ActionCompletedUnion& completionUnion) const override;
      
      virtual PreActionPose::ActionType GetPreActionType() override { return PreActionPose::ActionType::PLACE_RELATIVE; }
      
      virtual ActionResult SelectDockAction(ActionableObject* object) override;
      
      virtual ActionResult Verify() override;
      
      // For verifying if we successfully picked up the object
      //Pose3d _dockObjectOrigPose;
      
      // If placing an object, we need a place to store what robot was
      // carrying, for verification.
      ObjectID                   _carryObjectID;
      const Vision::KnownMarker* _carryObjectMarker;
      
      std::unique_ptr<IActionRunner> _placementVerifyAction = nullptr;
      bool                           _verifyComplete; // used in PLACE modes
      
    private:
      f32 _relOffsetX_mm;
      f32 _relOffsetY_mm;
      bool _relativeCurrentMarker;

      // Uses the robot's angle in its pre-dock pose and the docking object's rotation
      // to calculate how to reflect/negate the placement offsets so they are relative
      // to the docking object's world coordinates instead of the currently visible marker
      ActionResult TransformPlacementOffsetsRelativeObject();
      
    }; // class PlaceRelObjectAction
    
    
    // If not carrying anything, rolls the specified object.
    // If carrying an object, fails.
    class RollObjectAction : public IDockAction
    {
    public:
      RollObjectAction(Robot& robot, ObjectID objectID, const bool useManualSpeed = false);
      virtual ~RollObjectAction()
      {
        if(_rollVerifyAction != nullptr)
        {
          _rollVerifyAction->PrepForCompletion();
        }
      }
      
      // Whether or not to do the deep roll action instead of the default roll
      void EnableDeepRoll(bool enable);
      
    protected:
      
      // Override completion signal to fill in information about rolled objects
      virtual void GetCompletionUnion(ActionCompletedUnion& completionUnion) const override;
      
      virtual PreActionPose::ActionType GetPreActionType() override { return PreActionPose::ROLLING; }
      
      virtual ActionResult SelectDockAction(ActionableObject* object) override;
      
      virtual ActionResult Verify() override;
      
      // For verifying if we successfully rolled the object
      Pose3d _dockObjectOrigPose;
      
      const Vision::KnownMarker* _expectedMarkerPostRoll = nullptr;
      
      std::unique_ptr<IActionRunner> _rollVerifyAction = nullptr;
      
    private:
      // How much we should look down to be able to see the marker of the object we just rolled
      const f32 kAngleToLookDown = DEG_TO_RAD(-15.f);
      
    }; // class RollObjectAction

    
    class CrossBridgeAction : public IDockAction
    {
    public:
      CrossBridgeAction(Robot& robot, ObjectID bridgeID, const bool useManualSpeed);
      
    protected:
      
      virtual PreActionPose::ActionType GetPreActionType() override { return PreActionPose::ENTRY; }
      
      virtual ActionResult SelectDockAction(ActionableObject* object) override;
      
      virtual ActionResult Verify() override;
      
      // Crossing a bridge _does_ require the second dockMarker,
      // so override the virtual method for setting it
      virtual const Vision::KnownMarker* GetDockMarker2(const std::vector<PreActionPose>& preActionPoses,
                                                        const size_t closestIndex) override;
      
    }; // class CrossBridgeAction
    
    
    class AscendOrDescendRampAction : public IDockAction
    {
    public:
      AscendOrDescendRampAction(Robot& robot, ObjectID rampID, const bool useManualSpeed);
      
    protected:
      
      virtual ActionResult SelectDockAction(ActionableObject* object) override;
      
      virtual ActionResult Verify() override;
      
      virtual PreActionPose::ActionType GetPreActionType() override { return PreActionPose::ENTRY; }
      
      // Give the robot a little longer to start ascending/descending before
      // checking if it is done
      virtual f32 GetCheckIfDoneDelayInSeconds() const override { return 1.f; }
      
    }; // class AscendOrDesceneRampAction
    
    
    class MountChargerAction : public IDockAction
    {
    public:
      MountChargerAction(Robot& robot, ObjectID chargerID, const bool useManualSpeed);
      
    protected:
      
      virtual ActionResult SelectDockAction(ActionableObject* object) override;
      
      virtual ActionResult Verify() override;
      
      virtual PreActionPose::ActionType GetPreActionType() override { return PreActionPose::ENTRY; }
      
      // Give the robot a little longer to start ascending/descending before
      // checking if it is done
      virtual f32 GetCheckIfDoneDelayInSeconds() const override { return 1.f; }
      
    }; // class MountChargerAction
  }
}

#endif /* ANKI_COZMO_DOCKACTIONS_H */

/**
 * File: driveToActions.h
 *
 * Author: Andrew Stein
 * Date:   8/29/2014
 *
 * Description: Implements drive-to cozmo-specific actions, derived from the IAction interface.
 *
 *
 * Copyright: Anki, Inc. 2014
 **/

#ifndef ANKI_COZMO_DRIVE_TO_ACTIONS_H
#define ANKI_COZMO_DRIVE_TO_ACTIONS_H

#include "anki/common/types.h"
#include "anki/cozmo/basestation/actionableObject.h"
#include "anki/cozmo/basestation/actions/actionInterface.h"
#include "anki/cozmo/basestation/actions/compoundActions.h"
#include "anki/planning/shared/goalDefs.h"
#include "clad/externalInterface/messageEngineToGame.h"
#include "clad/types/actionTypes.h"
#include "clad/types/animationKeyFrames.h"
#include "clad/types/animationTrigger.h"
#include "clad/types/dockingSignals.h"
#include "util/helpers/templateHelpers.h"
#include <memory>

namespace Anki {
  
  namespace Cozmo {
  
    class IDockAction;

    class DriveToPoseAction : public IAction
    {
    public:
      DriveToPoseAction(Robot& robot,
                        const Pose3d& pose,
                        const bool forceHeadDown,
                        const bool useManualSpeed = false,
                        const Point3f& distThreshold = DEFAULT_POSE_EQUAL_DIST_THRESOLD_MM,
                        const Radians& angleThreshold = DEFAULT_POSE_EQUAL_ANGLE_THRESHOLD_RAD,
                        const float maxPlanningTime = DEFAULT_MAX_PLANNER_COMPUTATION_TIME_S,
                        const float maxReplanPlanningTime = DEFAULT_MAX_PLANNER_REPLAN_COMPUTATION_TIME_S);
      
      DriveToPoseAction(Robot& robot,
                        const bool forceHeadDown,
                        const bool useManualSpeed = false); // Note that SetGoal(s) must be called before Update()!
      
      DriveToPoseAction(Robot& robot,
                        const std::vector<Pose3d>& poses,
                        const bool forceHeadDown,
                        const bool useManualSpeed = false,
                        const Point3f& distThreshold = DEFAULT_POSE_EQUAL_DIST_THRESOLD_MM,
                        const Radians& angleThreshold = DEFAULT_POSE_EQUAL_ANGLE_THRESHOLD_RAD,
                        const float maxPlanningTime = DEFAULT_MAX_PLANNER_COMPUTATION_TIME_S,
                        const float maxReplanPlanningTime = DEFAULT_MAX_PLANNER_REPLAN_COMPUTATION_TIME_S);
      virtual ~DriveToPoseAction();
      
      // TODO: Add methods to adjust the goal thresholds from defaults
      
      // Set single goal
      Result SetGoal(const Pose3d& pose,
                     const Point3f& distThreshold  = DEFAULT_POSE_EQUAL_DIST_THRESOLD_MM,
                     const Radians& angleThreshold = DEFAULT_POSE_EQUAL_ANGLE_THRESHOLD_RAD);
      
      // Set possible goal options
      Result SetGoals(const std::vector<Pose3d>& poses,
                      const Point3f& distThreshold  = DEFAULT_POSE_EQUAL_DIST_THRESOLD_MM,
                      const Radians& angleThreshold = DEFAULT_POSE_EQUAL_ANGLE_THRESHOLD_RAD);
      
      // Set possible goal options that were generated from an object's pose (predock poses)
      Result SetGoals(const std::vector<Pose3d>& poses,
                      const Pose3d& objectPoseGoalsGeneratedFrom,
                      const Point3f& distThreshold  = DEFAULT_POSE_EQUAL_DIST_THRESOLD_MM,
                      const Radians& angleThreshold = DEFAULT_POSE_EQUAL_ANGLE_THRESHOLD_RAD);
      
      void SetMotionProfile(const PathMotionProfile& motionProfile);

    protected:
      
      virtual ActionResult Init() override;
      virtual ActionResult CheckIfDone() override;
      
      bool IsUsingManualSpeed() {return _useManualSpeed;}
      
    private:
      bool     _isGoalSet;
      bool     _driveWithHeadDown;
      
      std::vector<Pose3d> _goalPoses;
      std::shared_ptr<Planning::GoalID> _selectedGoalIndex;
      
      PathMotionProfile _pathMotionProfile;
      bool _hasMotionProfile = false;
      
      Point3f  _goalDistanceThreshold;
      Radians  _goalAngleThreshold;
      bool     _useManualSpeed;
      
      float _maxPlanningTime;
      float _maxReplanPlanningTime;
      
      float _timeToAbortPlanning;
            
      // The pose of the object that the _goalPoses were generated from
      Pose3d _objectPoseGoalsGeneratedFrom;
      bool _useObjectPose = false;
      
      int _debugPrintCtr = 0;
      
    }; // class DriveToPoseAction

    
    // Uses the robot's planner to select the best pre-action pose for the
    // specified action type. Drives there using a DriveToPoseAction. Then
    // moves the robot's head to the angle indicated by the pre-action pose
    // (which may be different from the angle used for path following).
    class DriveToObjectAction : public IAction
    {
    public:
      DriveToObjectAction(Robot& robot,
                          const ObjectID& objectID,
                          const PreActionPose::ActionType& actionType,
                          const f32 predockOffsetDistX_mm = 0,
                          const bool useApproachAngle = false,
                          const f32 approachAngle_rad = 0,
                          const bool useManualSpeed = false);
      
      DriveToObjectAction(Robot& robot,
                          const ObjectID& objectID,
                          const f32 distance_mm,
                          const bool useManualSpeed = false);
      virtual ~DriveToObjectAction();
      
      // TODO: Add version where marker code is specified instead of action?
      //DriveToObjectAction(Robot& robot, const ObjectID& objectID, Vision::Marker::Code code);
      
      // If set, instead of driving to the nearest preActionPose, only the preActionPose
      // that is most closely aligned with the approach angle is considered.
      void SetApproachAngle(const f32 angle_rad);
      const bool GetUseApproachAngle() const;
      // returns a bool indicating the success or failure of setting the pose
      const bool GetClosestPreDockPose(ActionableObject* object, Pose3d& closestPose) const;
      
      // Whether or not to verify the final pose, once the path is complete,
      // according to the latest know preAction pose for the specified object.
      void DoPositionCheckOnPathCompletion(bool doCheck) { _doPositionCheckOnPathCompletion = doCheck; }
      
      void SetMotionProfile(const PathMotionProfile& motionProfile);
      
      using GetPossiblePosesFunc = std::function<ActionResult(ActionableObject* object,
                                                              std::vector<Pose3d>& possiblePoses,
                                                              bool& alreadyInPosition)>;
      
      void SetGetPossiblePosesFunc(GetPossiblePosesFunc func)
      {
        if(IsRunning()){
          PRINT_NAMED_ERROR("DriveToActions.SetGetPossiblePosesFunc.TriedToSetWhileRunning",
                            "PossiblePosesFunc is not allowed to change while the driveToAction is running. \
                             ActionName: %s ActionTag:%i", GetName().c_str(), GetTag());
          return;
        }
        
        _getPossiblePosesFunc = func;
      }
      
      // Default GetPossiblePoses function is public in case others want to just
      // use it as the baseline and modify it's results slightly
      ActionResult GetPossiblePoses(ActionableObject* object,
                                    std::vector<Pose3d>& possiblePoses,
                                    bool& alreadyInPosition);
      
      virtual void GetCompletionUnion(ActionCompletedUnion& completionUnion) const override;
      
    protected:
      
      virtual ActionResult Init() override;
      virtual ActionResult CheckIfDone() override;
      
      ActionResult InitHelper(ActionableObject* object);
      
      
      // Not private b/c DriveToPlaceCarriedObject uses
      ObjectID                   _objectID;
      PreActionPose::ActionType  _actionType;
      f32                        _distance_mm;
      f32                        _predockOffsetDistX_mm;
      bool                       _useManualSpeed;
      CompoundActionSequential   _compoundAction;
      
      bool                       _useApproachAngle;
      Radians                    _approachAngle_rad;

      bool                       _doPositionCheckOnPathCompletion = true;
      
      PathMotionProfile          _pathMotionProfile;
      bool                       _hasMotionProfile = false;
            
    private:
      GetPossiblePosesFunc _getPossiblePosesFunc;
      bool _lightsSet = false;
      
    }; // DriveToObjectAction

  
    class DriveToPlaceCarriedObjectAction : public DriveToObjectAction
    {
    public:
      // destinationObjectPadding_mm: padding around the object size at destination used if checkDestinationFree is true
      DriveToPlaceCarriedObjectAction(Robot& robot,
                                      const Pose3d& placementPose,
                                      const bool placeOnGround,
                                      const bool useExactRotation = false,
                                      const bool useManualSpeed = false,
                                      const bool checkDestinationFree = false,
                                      const float destinationObjectPadding_mm = 0.0f);
      
    protected:
    
      virtual ActionResult Init() override;
      virtual ActionResult CheckIfDone() override; // Simplified version from DriveToObjectAction
      
      // checks if the placement destination is free (alternatively we could provide an std::function callback)
      bool IsPlacementGoalFree() const;
      
      Pose3d _placementPose;
      
      bool   _useExactRotation;
      bool   _checkDestinationFree; // if true the action will often check that the destination is still free to place the object
      float  _destinationObjectPadding_mm; // padding around the object size at destination if _checkDestinationFree is true
      
    }; // DriveToPlaceCarriedObjectAction()

    
    // Interface for all classes below which first drive to an object and then
    // do something with it.
    // If maxTurnTowardsFaceAngle > 0, robot will turn a maximum of that angle towards
    // last face after driving to the object (and say name if that is specified).
    class IDriveToInteractWithObject : public CompoundActionSequential
    {
    protected:
      // Not directly instantiable
      IDriveToInteractWithObject(Robot& robot,
                                 const ObjectID& objectID,
                                 const PreActionPose::ActionType& actionType,
                                 const f32 predockOffsetDistX_mm,
                                 const bool useApproachAngle,
                                 const f32 approachAngle_rad,
                                 const bool useManualSpeed,
                                 Radians maxTurnTowardsFaceAngle_rad,
                                 const bool sayName);
      
      IDriveToInteractWithObject(Robot& robot,
                                 const ObjectID& objectID,
                                 const f32 distance,
                                 const bool useManualSpeed);

    public:
      virtual ~IDriveToInteractWithObject();
      
      void SetMotionProfile(const PathMotionProfile& motionProfile);
      
      // Forces both of the turnTowards subActions to force complete (basically not run)
      void DontTurnTowardsFace();
      
      void SetMaxTurnTowardsFaceAngle(const Radians angle);
      void SetTiltTolerance(const Radians tol);
      
      DriveToObjectAction* GetDriveToObjectAction() {
        // For debug builds do a dynamic cast for the validity checks
        DEV_ASSERT(dynamic_cast<DriveToObjectAction*>(_driveToObjectAction.lock().get()) != nullptr,
                   "DriveToObjectAction.GetDriveToObjectAction.DynamicCastFailed");
        
        return static_cast<DriveToObjectAction*>(_driveToObjectAction.lock().get());
      }
      
      // Subclasses that are a drive-to action followed by a dock action should be calling
      // this function instead of the base classes AddAction() in order to set the approriate
      // preDock pose offset for the dock action
      std::weak_ptr<IActionRunner> AddDockAction(IDockAction* dockAction, bool ignoreFailure = false);

      // Sets the animation trigger to use to say the name. Only valid if sayName was true
      void SetSayNameAnimationTrigger(AnimationTrigger trigger);
      
      // Sets the backup animation to play if the name is not known, but there is a confirmed face. Only valid
      // if sayName is true (this is because we are trying to use an animation to say the name, but if we
      // don't have a name, we want to use this animation instead)
      void SetNoNameAnimationTrigger(AnimationTrigger trigger);

      // Pass in a callback which will get called when the robot switches from driving to it's predock pose to
      // the actual docking action
      using PreDockCallback = std::function<void(Robot&)>;
      void SetPreDockCallback(PreDockCallback callback) { _preDockCallback = callback; }
      
      const bool GetUseApproachAngle() const;
      
      // Whether or not we should look up to check if there is an object above the dockObject
      void SetShouldCheckForObjectOnTopOf(const bool b);
      
    protected:

      virtual Result UpdateDerived() override;
      
      // If set, instead of driving to the nearest preActionPose, only the preActionPose
      // that is most closely aligned with the approach angle is considered.
      void SetApproachAngle(const f32 angle_rad);
      
    private:
      // Keep weak_ptrs to each of the actions inside this compound action so they can be easily
      // modified
      // Unfortunately the weak_ptrs need to be cast to the appropriate types to use them. Static casts
      // are used for this. These are safe in this case as they are only ever casting to the
      // original type of the action
      std::weak_ptr<IActionRunner> _driveToObjectAction;
      std::weak_ptr<IActionRunner> _turnTowardsLastFacePoseAction;
      std::weak_ptr<IActionRunner> _turnTowardsObjectAction;
      std::weak_ptr<IActionRunner> _dockAction;
      ObjectID _objectID;
      bool     _lightsSet = false;
      f32      _preDockPoseDistOffsetX_mm = 0;
      PreDockCallback _preDockCallback;
      
    }; // class IDriveToInteractWithObject
        
    
    // Compound action for driving to an object, visually verifying it can still be seen,
    // and then driving to it until it is at the specified distance (i.e. distanceFromMarker_mm)
    // from the marker.
    // @param distanceFromMarker_mm - The distance from the marker along it's normal axis that the robot should stop at.
    // @param useApproachAngle  - If true, then only the preAction pose that results in a robot
    //                            approach angle closest to approachAngle_rad is considered.
    // @param approachAngle_rad - The desired docking approach angle of the robot in world coordinates.
    class DriveToAlignWithObjectAction : public IDriveToInteractWithObject
    {
    public:
      DriveToAlignWithObjectAction(Robot& robot,
                                   const ObjectID& objectID,
                                   const f32 distanceFromMarker_mm,
                                   const bool useApproachAngle = false,
                                   const f32 approachAngle_rad = 0,
                                   const AlignmentType alignmentType = AlignmentType::CUSTOM,
                                   const bool useManualSpeed = false,
                                   Radians maxTurnTowardsFaceAngle_rad = 0.f,
                                   const bool sayName = false);
      
      virtual ~DriveToAlignWithObjectAction() { }
    };
    
    
    // Common compound action for driving to an object, visually verifying we
    // can still see it, and then picking it up.
    // @param useApproachAngle  - If true, then only the preAction pose that results in a robot
    //                            approach angle closest to approachAngle_rad is considered.
    // @param approachAngle_rad - The desired docking approach angle of the robot in world coordinates.
    class DriveToPickupObjectAction : public IDriveToInteractWithObject
    {
    public:
      DriveToPickupObjectAction(Robot& robot,
                                const ObjectID& objectID,
                                const bool useApproachAngle = false,
                                const f32 approachAngle_rad = 0,
                                const bool useManualSpeed = false,
                                Radians maxTurnTowardsFaceAngle_rad = 0.f,
                                const bool sayName = false,
                                AnimationTrigger animBeforeDock = AnimationTrigger::Count);
      
      virtual ~DriveToPickupObjectAction() { }
      
      void SetDockingMethod(DockingMethod dockingMethod);
      
      void SetPostDockLiftMovingAnimation(Anki::Cozmo::AnimationTrigger trigger);
      
    private:
      std::weak_ptr<IActionRunner> _pickupAction;
    };
    
    
    // Common compound action for driving to an object, visually verifying we
    // can still see it, and then placing an object on it.
    // @param objectID         - object to place carried object on
    class DriveToPlaceOnObjectAction : public IDriveToInteractWithObject
    {
    public:
      
      // Places carried object on top of objectID
      DriveToPlaceOnObjectAction(Robot& robot,
                                 const ObjectID& objectID,
                                 const bool useApproachAngle = false,
                                 const f32 approachAngle_rad = 0,
                                 const bool useManualSpeed = false,
                                 Radians maxTurnTowardsFaceAngle_rad = 0.f,
                                 const bool sayName = false);
      
      virtual ~DriveToPlaceOnObjectAction() { }
    };
    
    
    // Common compound action for driving to an object, visually verifying we
    // can still see it, and then placing an object relative to it.
    // @param placementOffsetX_mm - The desired distance between the center of the docking marker
    //                              and the center of the object that is being placed, along the
    //                              direction of the docking marker's normal.
    // @param useApproachAngle  - If true, then only the preAction pose that results in a robot
    //                            approach angle closest to approachAngle_rad is considered.
    // @param approachAngle_rad - The desired docking approach angle of the robot in world coordinates.
    class DriveToPlaceRelObjectAction : public IDriveToInteractWithObject
    {
    public:
      // Place carried object on ground at specified placementOffset from objectID,
      // chooses preAction pose closest to approachAngle_rad if useApproachAngle == true.
      DriveToPlaceRelObjectAction(Robot& robot,
                                  const ObjectID& objectID,
                                  const bool placingOnGround = true,
                                  const f32 placementOffsetX_mm = 0,
                                  const f32 placementOffsetY_mm = 0,
                                  const bool useApproachAngle = false,
                                  const f32 approachAngle_rad = 0,
                                  const bool useManualSpeed = false,
                                  Radians maxTurnTowardsFaceAngle_rad = 0.f,
                                  const bool sayName = false,
                                  const bool relativeCurrentMarker = true);
      
      virtual ~DriveToPlaceRelObjectAction() { }
      
    };
    
    
    // Common compound action for driving to an object, visually verifying we
    // can still see it, and then rolling it.
    // @param useApproachAngle  - If true, then only the preAction pose that results in a robot
    //                            approach angle closest to approachAngle_rad is considered.
    // @param approachAngle_rad - The desired docking approach angle of the robot in world coordinates.
    class RollObjectAction;
    class DriveToRollObjectAction : public IDriveToInteractWithObject
    {
    public:
      DriveToRollObjectAction(Robot& robot,
                              const ObjectID& objectID,
                              const bool useApproachAngle = false,
                              const f32 approachAngle_rad = 0,
                              const bool useManualSpeed = false,
                              Radians maxTurnTowardsFaceAngle_rad = 0.f,
                              const bool sayName = true);

      virtual ~DriveToRollObjectAction() { }
      
      // Sets the approach angle so that, if possible, the roll action will roll the block to land upright. If
      // the block is upside down or already upright, and roll action will be allowed
      void RollToUpright();

      // Calculate the approach angle the robot should use to drive to the pre-dock
      // pose that will result in the block being rolled upright.  Returns true
      // if the angle parameter has been set, false if the angle couldn't be
      // calculated or an approach angle to roll upright doesn't exist
      static bool GetRollToUprightApproachAngle(Robot& robot,
                                                const ObjectID& objID,
                                                f32& approachAngle_rad);

      Result EnableDeepRoll(bool enable);
      
    private:
      ObjectID _objectID;
      std::weak_ptr<IActionRunner> _rollAction;
    };
    
    
    // Common compound action for driving to an object and popping a wheelie off of it
    // @param useApproachAngle  - If true, then only the preAction pose that results in a robot
    //                            approach angle closest to approachAngle_rad is considered.
    // @param approachAngle_rad - The desired docking approach angle of the robot in world coordinates.
    class DriveToPopAWheelieAction : public IDriveToInteractWithObject
    {
    public:
      DriveToPopAWheelieAction(Robot& robot,
                               const ObjectID& objectID,
                               const bool useApproachAngle = false,
                               const f32 approachAngle_rad = 0,
                               const bool useManualSpeed = false,
                               Radians maxTurnTowardsFaceAngle_rad = 0.f,
                               const bool sayName = true);
      
      virtual ~DriveToPopAWheelieAction() { }
    };
    
    // Common compound action for driving to an object (stack) and face planting off of it by knocking the stack over
    // @param useApproachAngle  - If true, then only the preAction pose that results in a robot
    //                            approach angle closest to approachAngle_rad is considered.
    // @param approachAngle_rad - The desired docking approach angle of the robot in world coordinates.
    class DriveToFacePlantAction : public IDriveToInteractWithObject
    {
    public:
      DriveToFacePlantAction(Robot& robot,
                             const ObjectID& objectID,
                             const bool useApproachAngle = false,
                             const f32 approachAngle_rad = 0,
                             const bool useManualSpeed = false,
                             Radians maxTurnTowardsFaceAngle_rad = 0.f,
                             const bool sayName = false);
      
      virtual ~DriveToFacePlantAction() { }
    };

    // Common compound action
    class DriveToAndTraverseObjectAction : public IDriveToInteractWithObject
    {
    public:
      DriveToAndTraverseObjectAction(Robot& robot,
                                     const ObjectID& objectID,
                                     const bool useManualSpeed = false,
                                     Radians maxTurnTowardsFaceAngle_rad = 0.f,
                                     const bool sayName = false);
      
      virtual ~DriveToAndTraverseObjectAction() { }
      
    };
    
    
    class DriveToAndMountChargerAction : public IDriveToInteractWithObject
    {
    public:
      DriveToAndMountChargerAction(Robot& robot,
                                   const ObjectID& objectID,
                                   const bool useManualSpeed = false,
                                   Radians maxTurnTowardsFaceAngle_rad = 0.f,
                                   const bool sayName = false);
      
      virtual ~DriveToAndMountChargerAction() { }
      
    };
    
    class DriveToRealignWithObjectAction : public CompoundActionSequential
    {
    public:
      DriveToRealignWithObjectAction(Robot& robot, ObjectID objectID, float dist_mm);
    };
  }
}

#endif /* ANKI_COZMO_DRIVE_TO_ACTIONS_H */

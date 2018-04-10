/**
 * File: basicActions.cpp
 *
 * Author: Andrew Stein
 * Date:   8/29/2014
 *
 * Description: Implements basic cozmo-specific actions, derived from the IAction interface.
 *
 *
 * Copyright: Anki, Inc. 2014
 **/

#include "engine/actions/basicActions.h"
#include "clad/robotInterface/messageRobotToEngine.h"
#include "coretech/common/engine/math/poseOriginList.h"
#include "coretech/common/engine/utils/timer.h"
#include "engine/actions/dockActions.h"
#include "engine/actions/driveToActions.h"
#include "engine/actions/sayTextAction.h"
#include "engine/actions/trackObjectAction.h"
#include "engine/actions/visuallyVerifyActions.h"
#include "engine/ankiEventUtil.h"
#include "engine/blockWorld/blockWorld.h"
#include "engine/components/movementComponent.h"
#include "engine/components/pathComponent.h"
#include "engine/components/visionComponent.h"
#include "engine/cozmoContext.h"
#include "engine/drivingAnimationHandler.h"
#include "engine/externalInterface/externalInterface.h"
#include "engine/faceWorld.h"
#include "engine/moodSystem/moodManager.h"
#include "engine/robot.h"
#include "engine/robotInterface/messageHandler.h"
#include "engine/vision/visionModesHelpers.h"
#include "util/console/consoleInterface.h"

namespace Anki {
  
  namespace Cozmo {
    
    // Whether or not to insert WaitActions before and after TurnTowardsObject's VisuallyVerifyAction
    CONSOLE_VAR(bool, kInsertWaitsInTurnTowardsObjectVerify,"TurnTowardsObject", false);
    
    TurnInPlaceAction::TurnInPlaceAction(const float angle_rad, const bool isAbsolute)
    : IAction("TurnInPlace",
              RobotActionType::TURN_IN_PLACE,
              (u8)AnimTrackFlag::BODY_TRACK)
    , _requestedAngle_rad(angle_rad)
    , _isAbsoluteAngle(isAbsolute)
    {

    }
    
    TurnInPlaceAction::~TurnInPlaceAction()
    {
      if(HasRobot()){
        GetRobot().GetAnimationComponent().RemoveEyeShift(_kEyeShiftLayerName);
      }
    }
    
    void TurnInPlaceAction::SetRequestedTurnAngle(const f32 turnAngle_rad)
    {
      DEV_ASSERT(!_isInitialized, "TurnInPlaceAction.SetRequestedTurnAngle.ActionAlreadyInitialized");
      _requestedAngle_rad = turnAngle_rad;
    }
    
    void TurnInPlaceAction::SetMaxSpeed(f32 maxSpeed_radPerSec)
    {
      DEV_ASSERT(!_isInitialized, "TurnInPlaceAction.SetMaxSpeed.ActionAlreadyInitialized");
      if (std::fabsf(maxSpeed_radPerSec) > MAX_BODY_ROTATION_SPEED_RAD_PER_SEC) {
        PRINT_NAMED_WARNING("TurnInPlaceAction.SetMaxSpeed.SpeedExceedsLimit",
                            "Speed of %f deg/s exceeds limit of %f deg/s. Clamping.",
                            RAD_TO_DEG(maxSpeed_radPerSec), MAX_BODY_ROTATION_SPEED_DEG_PER_SEC);
        _maxSpeed_radPerSec = std::copysign(MAX_BODY_ROTATION_SPEED_RAD_PER_SEC, maxSpeed_radPerSec);
        _motionProfileManuallySet = true;
      } else if (maxSpeed_radPerSec == 0) {
        _maxSpeed_radPerSec = _kDefaultSpeed;
      } else {
        _maxSpeed_radPerSec = maxSpeed_radPerSec;
        _motionProfileManuallySet = true;
      }
    }
    
    void TurnInPlaceAction::SetAccel(f32 accel_radPerSec2)
    {
      DEV_ASSERT(!_isInitialized, "TurnInPlaceAction.SetAccel.ActionAlreadyInitialized");
      if (accel_radPerSec2 == 0) {
        _accel_radPerSec2 = _kDefaultAccel;
      } else {
        _accel_radPerSec2 = accel_radPerSec2;
        _motionProfileManuallySet = true;
      }
    }
    

    bool TurnInPlaceAction::SetMotionProfile(const PathMotionProfile& motionProfile)
    {
      DEV_ASSERT(!_isInitialized, "TurnInPlaceAction.SetMotionProfile.ActionAlreadyInitialized");
      if( _motionProfileManuallySet ) {
        // don't want to use the custom profile since someone manually specified speeds
        return false;
      }
      else {
        _maxSpeed_radPerSec = motionProfile.pointTurnSpeed_rad_per_sec;
        _accel_radPerSec2 = motionProfile.pointTurnAccel_rad_per_sec2;
        return true;
      }
    }
  
    void TurnInPlaceAction::SetTolerance(const Radians& angleTol_rad)
    {
      DEV_ASSERT(!_isInitialized, "TurnInPlaceAction.SetTolerance.ActionAlreadyInitialized");
      _angleTolerance = angleTol_rad.getAbsoluteVal();
      
      // NOTE: can't be lower than what is used internally on the robot
      if( _angleTolerance.ToFloat() < POINT_TURN_ANGLE_TOL ) {
        if (Util::IsNear(_angleTolerance.ToFloat(), 0.f)) {
          PRINT_CH_INFO("Actions", "TurnInPlaceAction.SetTolerance.UseDefault",
                        "Tolerance of zero is treated as use default tolerance %f deg",
                        RAD_TO_DEG(POINT_TURN_ANGLE_TOL));
        } else {
          PRINT_NAMED_WARNING("TurnInPlaceAction.InvalidTolerance",
                              "Tried to set tolerance of %fdeg, min is %f",
                              _angleTolerance.getDegrees(),
                              RAD_TO_DEG(POINT_TURN_ANGLE_TOL));
        }
        _angleTolerance = POINT_TURN_ANGLE_TOL;
      }
    }
    
    inline Result TurnInPlaceAction::SendSetBodyAngle()
    {
      return GetRobot().GetMoveComponent().TurnInPlace(_currentTargetAngle.ToFloat(),
                                                       _maxSpeed_radPerSec,
                                                       _accel_radPerSec2,
                                                       _angleTolerance.ToFloat(),
                                                       
                                                       // For relative turns, the total angle to turn can be greater than 180 degrees.
                                                       //  So we need to tell the robot how 'far' it should turn. For absolute angles,
                                                       //  the robot should always just take the shortest path to the desired angle.
                                                       _isAbsoluteAngle ? 0 : (uint16_t) std::floor( std::abs(_angularDistExpected_rad / M_PI_F) ),
                                                   
                                                       // For absolute turns, the robot should take the shortest path to
                                                       //  the desired angle:
                                                       _isAbsoluteAngle,
                                                       
                                                       &_actionID);
    }
    
    bool TurnInPlaceAction::IsOffTreadsStateValid() const
    {
      // If the robot is not on its treads, it may exhibit erratic turning behavior
      const auto otState = GetRobot().GetOffTreadsState();
      const bool valid = (otState == OffTreadsState::OnTreads);
      if (!valid) {
        PRINT_NAMED_WARNING("TurnInPlaceAction.OffTreadsStateInvalid",
                            "[%d] Off tread state %s is invalid for TurnInPlace",
                            GetTag(),
                            EnumToString(otState));
      }
      return valid;
    }
    
    ActionResult TurnInPlaceAction::Init()
    {
      _turnStarted = false;
      
      // Ensure that the OffTreadsState is valid
      if (!IsOffTreadsStateValid()) {
        return ActionResult::INVALID_OFF_TREADS_STATE;
      }
      
      // Grab the robot's current heading and PoseFrameId (which
      //  is used later to detect if relocalization occurred mid-turn)
      _prevPoseFrameId = GetRobot().GetPoseFrameID();
      _relocalizedCnt = 0;
      
      DEV_ASSERT(GetRobot().GetPose().IsChildOf(GetRobot().GetWorldOrigin()), "TurnInPlaceAction.Init.RobotOriginMismatch");
      
      _currentAngle = GetRobot().GetPose().GetRotation().GetAngleAroundZaxis();
      
      // Compute variability to add to target angle (if any):
      float variabilityToAdd_rad = 0.f;
      if (_variability != 0.f) {
          variabilityToAdd_rad = (float) GetRNG().RandDblInRange(-_variability.ToDouble(),
                                                                  _variability.ToDouble());
      }
      
      // Compute the target absolute angle for this turn (depending on if this
      //   is a relative or absolute turn request):
      if (_isAbsoluteAngle) {
        _currentTargetAngle = _requestedAngle_rad + variabilityToAdd_rad;
        
        _angularDistExpected_rad = (_currentTargetAngle - _currentAngle).ToFloat();
      } else {
        // This is a relative turn.
        // First, check the turn angle to make sure it's not too large:
        if (std::abs(_requestedAngle_rad) > 2.f*M_PI_F*_kMaxRelativeTurnRevs) {
          PRINT_NAMED_WARNING("TurnInPlaceAction.Init.AngleTooLarge",
                              "Requested relative turn angle (%.1f deg) is too large!",
                              RAD_TO_DEG(_requestedAngle_rad));
          return ActionResult::ABORT;
        }
        
        // In case this is a retry, subtract how much of the turn has been
        //  completed so far (0 for first time):
        _requestedAngle_rad -= _angularDistTraversed_rad;
        
        // Add the requested relative angle to the current heading to get the absolute target angle.
        _currentTargetAngle = _currentAngle + _requestedAngle_rad + variabilityToAdd_rad;
      
        // The angular distance is simply the requested relative angle and any variability
        //  (note: abs() of this can be greater than 2*PI rads).
        _angularDistExpected_rad = _requestedAngle_rad + variabilityToAdd_rad;
        
        // Also, for relative turns, the sign of the requested angle should dictate the direction of
        //   the turn. (the robot uses the sign of _maxSpeed_radPerSec to decide which direction to turn)
        _maxSpeed_radPerSec = std::copysign(_maxSpeed_radPerSec, _requestedAngle_rad);
      }

      // reset angular distance traversed and previousAngle (used in CheckIfDone):
      _angularDistTraversed_rad = 0;
      _previousAngle = _currentAngle;
      
      _inPosition = IsBodyInPosition(_currentAngle);
      _motionCommanded = false;
      _motionCommandAcked = false;
      _turnStarted = false;
      
      if(!_inPosition) {

        if(RESULT_OK != SendSetBodyAngle()) {
          return ActionResult::SEND_MESSAGE_TO_ROBOT_FAILED;
        } else {
          _motionCommanded = true;
        }
        
        if(_moveEyes)
        {
          // Store the angular distance at which to remove eye shift (halfway through the turn)
          _absAngularDistToRemoveEyeDart_rad = 0.5f * std::abs(_angularDistExpected_rad);
          
          // Move the eyes (only if not in position)
          // Note: assuming screen is about the same x distance from the neck joint as the head cam
          float angleDiff_rad = _angularDistExpected_rad;
          
          // Clip angleDiff to 89 degrees to prevent unintended behavior due to tangent
          angleDiff_rad = Anki::Util::Clamp(angleDiff_rad, DEG_TO_RAD(-89.f), DEG_TO_RAD(89.f));

          const f32 x_mm = std::tan(angleDiff_rad) * HEAD_CAM_POSITION[0];
          const f32 xPixShift = x_mm * (static_cast<f32>(GetRobot().GetDisplayWidthInPixels()) / (4*SCREEN_SIZE[0]));
          GetRobot().GetAnimationComponent().AddOrUpdateEyeShift(_kEyeShiftLayerName, xPixShift, 0, 4*ANIM_TIME_STEP_MS);
        }
      }

      // Subscribe to motor command ack      
      auto actionStartedLambda = [this](const AnkiEvent<RobotInterface::RobotToEngine>& event)
      {
        if(_motionCommanded && _actionID == event.GetData().Get_motorActionAck().actionID) {
          PRINT_CH_INFO("Actions", "TurnInPlaceAction.MotorActionAcked",
                        "[%d] ActionID: %d",
                        GetTag(),
                        _actionID);
          _motionCommandAcked = true;
        }
      };
      
      _signalHandle = GetRobot().GetRobotMessageHandler()->Subscribe(RobotInterface::RobotToEngineTag::motorActionAck,
                                                                     actionStartedLambda);


      _isInitialized = true;
      
      return ActionResult::SUCCESS;
    }
    
    bool TurnInPlaceAction::IsBodyInPosition(Radians& currentAngle) const
    {
      currentAngle = GetRobot().GetPose().GetRotation().GetAngleAroundZaxis();
      bool inPosition = false;
      
      const float absAngularDistToTarget_rad = std::abs(_angularDistExpected_rad - _angularDistTraversed_rad);

      // Only check if body is in position if we're within Pi radians of completing
      //  the turn (to allow for multiple-rotation turns, e.g. 360 degrees).
      if (absAngularDistToTarget_rad < M_PI_F) {
        inPosition = currentAngle.IsNear(_currentTargetAngle, _angleTolerance.ToFloat() + Util::FLOATING_POINT_COMPARISON_TOLERANCE_FLT);
        
        // If we've relocalized during the turn, also consider the turn complete
        //  if we've turned through the entire expected angular distance (since the
        //  pose jump may cause the target vs. actual angle comparison to fail)
        if (_relocalizedCnt != 0 &&
            absAngularDistToTarget_rad < std::abs(_angleTolerance.ToFloat())) {
          inPosition = true;
        }
      }
      return inPosition && !GetRobot().GetMoveComponent().AreWheelsMoving();
    }
    
    ActionResult TurnInPlaceAction::CheckIfDone()
    {
      ActionResult result = ActionResult::RUNNING;
      
      if (_motionCommanded && !_motionCommandAcked) {
        PRINT_PERIODIC_CH_DEBUG(10, "Actions", "TurnInPlaceAction.CheckIfDone.WaitingForAck",
                                "[%d] ActionID: %d",
                                GetTag(),
                                _actionID);
        return result;
      }
      
      // Check to see if the pose frame ID has changed
      //  (due to robot re-localizing)
      if(_prevPoseFrameId != GetRobot().GetPoseFrameID())
      {
        ++_relocalizedCnt;
        PRINT_CH_INFO("Actions", "TurnInPlaceAction.CheckIfDone.PfidChanged",
                      "[%d] pose frame ID changed (old=%d, new=%d). "
                      "No longer comparing angles to check if done - using angular distance traversed instead. "
                      "(relocalizedCnt=%d) (inPositionNow=%d)",
                      GetTag(), _prevPoseFrameId, GetRobot().GetPoseFrameID(),
                      _relocalizedCnt, IsBodyInPosition(_currentAngle));
        _prevPoseFrameId = GetRobot().GetPoseFrameID();
        // Need to update previous angle since pose has changed (to
        //  keep _angularDistTraversed semi-accurate)
        _previousAngle = GetRobot().GetPose().GetRotation().GetAngleAroundZaxis();
      }
      
      if(!_inPosition) {
        _inPosition = IsBodyInPosition(_currentAngle);
      }
      
      // Keep track of how far we've traversed:
      _angularDistTraversed_rad += (_currentAngle - _previousAngle).ToFloat();
      _previousAngle = _currentAngle;

      // When we've turned at least halfway, remove eye dart
      if(GetRobot().GetAnimationComponent().IsEyeShifting(_kEyeShiftLayerName)) {
        if(_inPosition || (std::abs(_angularDistTraversed_rad) > _absAngularDistToRemoveEyeDart_rad))
        {
          PRINT_CH_DEBUG("Actions", "TurnInPlaceAction.CheckIfDone.RemovingEyeShift",
                         "Currently at %.1fdeg, on the way to %.1fdeg (traversed %.1fdeg)",
                         _currentAngle.getDegrees(),
                         _currentTargetAngle.getDegrees(),
                         RAD_TO_DEG(_angularDistTraversed_rad));
          GetRobot().GetAnimationComponent().RemoveEyeShift(_kEyeShiftLayerName, 3*ANIM_TIME_STEP_MS);
        }
      }

      const bool areWheelsMoving = GetRobot().GetMoveComponent().AreWheelsMoving();
      if ( areWheelsMoving ) {
        _turnStarted = true;
      }
      
      // Wait to get a state message back from the physical robot saying its body
      // is in the commanded position
      // TODO: Is this really necessary in practice?
      if(_inPosition) {
        result = ActionResult::SUCCESS;
        PRINT_CH_INFO("Actions", "TurnInPlaceAction.CheckIfDone.InPosition",
                      "[%d] In Position: %.1fdeg vs. %.1fdeg(+/-%.1f), angDistTravd=%+.1fdeg, angDistExpc=%+.1fdeg (tol: %f) (pfid: %d)",
                      GetTag(),
                      _currentAngle.getDegrees(),
                      _currentTargetAngle.getDegrees(),
                      _variability.getDegrees(),
                      RAD_TO_DEG(_angularDistTraversed_rad),
                      RAD_TO_DEG(_angularDistExpected_rad),
                      _angleTolerance.getDegrees(),
                      GetRobot().GetPoseFrameID());
      } else {
        // Don't spam "AngleNotReached" messages
        PRINT_PERIODIC_CH_DEBUG(10, "Actions", "TurnInPlaceAction.CheckIfDone.AngleNotReached",
                                "[%d] Waiting for body to reach angle: %.1fdeg vs. %.1fdeg(+/-%.1f), angDistTravd=%+.1fdeg, angDistExpc=%+.1fdeg (tol: %f) (pfid: %d)",
                                GetTag(),
                                _currentAngle.getDegrees(),
                                _currentTargetAngle.getDegrees(),
                                _variability.getDegrees(),
                                RAD_TO_DEG(_angularDistTraversed_rad),
                                RAD_TO_DEG(_angularDistExpected_rad),
                                _angleTolerance.getDegrees(),
                                GetRobot().GetPoseFrameID());
        
        if ( _turnStarted && !areWheelsMoving ) {
          PRINT_NAMED_WARNING("TurnInPlaceAction.StoppedMakingProgress",
                              "[%d] giving up since we stopped moving. currentAngle=%.1fdeg, target=%.1fdeg, angDistExp=%.1fdeg, angDistTrav=%.1fdeg (pfid: %d)",
                              GetTag(),
                              _currentAngle.getDegrees(),
                              _currentTargetAngle.getDegrees(),
                              RAD_TO_DEG(_angularDistExpected_rad),
                              RAD_TO_DEG(_angularDistTraversed_rad),
                              GetRobot().GetPoseFrameID());
          result = ActionResult::MOTOR_STOPPED_MAKING_PROGRESS;
        }
      }
      
      // Ensure that the OffTreadsState is valid
      if (!IsOffTreadsStateValid()) {
        result = ActionResult::INVALID_OFF_TREADS_STATE;
      }
      
      return result;
    }
    
    void TurnInPlaceAction::GetCompletionUnion(ActionCompletedUnion& completionUnion) const
    {
      TurnInPlaceCompleted info;
      info.relocalizedCnt = _relocalizedCnt;
      completionUnion.Set_turnInPlaceCompleted(std::move( info ));
    }

#pragma mark ---- SearchForNearbyObjectAction ----

    SearchForNearbyObjectAction::SearchForNearbyObjectAction(const ObjectID& desiredObjectID,
                                                             f32 backupDistance_mm,
                                                             f32 backupSpeed_mms,
                                                             f32 headAngle_rad)
    : IAction("SearchForNearbyObjectAction",
              RobotActionType::SEARCH_FOR_NEARBY_OBJECT,
              (u8)AnimTrackFlag::NO_TRACKS)
    , _compoundAction()
    , _desiredObjectID(desiredObjectID)
    , _objectObservedDuringSearch(false)
    , _backupDistance_mm(backupDistance_mm)
    , _backupSpeed_mms(backupSpeed_mms)
    , _headAngle_rad(headAngle_rad)
    {

    }
  
    SearchForNearbyObjectAction::~SearchForNearbyObjectAction()
    {
      _compoundAction.PrepForCompletion();
    }

    void SearchForNearbyObjectAction::OnRobotSet()
    {
      if(GetRobot().HasExternalInterface()){
        using namespace ExternalInterface;

        auto observedObjectCallback =
        [this](const AnkiEvent<ExternalInterface::MessageEngineToGame>& event){
          if(event.GetData().Get_RobotObservedObject().objectID == _desiredObjectID){
            _objectObservedDuringSearch = true;
          }
        };

        _eventHandlers.push_back(GetRobot().GetExternalInterface()->Subscribe(
                    ExternalInterface::MessageEngineToGameTag::RobotObservedObject,
                    observedObjectCallback));
      }

      _compoundAction.SetRobot(&GetRobot());
    }
    
  
    void SearchForNearbyObjectAction::SetSearchAngle(f32 minSearchAngle_rads, f32 maxSearchAngle_rads)
    {
      _minSearchAngle_rads = minSearchAngle_rads;
      _maxSearchAngle_rads = maxSearchAngle_rads;
    }
  
    void SearchForNearbyObjectAction::SetSearchWaitTime(f32 minWaitTime_s, f32 maxWaitTime_s)
    {
      _minWaitTime_s = minWaitTime_s;
      _maxWaitTime_s = maxWaitTime_s;
    }

    void SearchForNearbyObjectAction::GetRequiredVisionModes(std::set<VisionModeRequest>& requests) const
    {
      requests.insert({ VisionMode::DetectingMarkers, EVisionUpdateFrequency::High });
    }

    ActionResult SearchForNearbyObjectAction::Init()
    {
      // Incase we are re-running this action
      _compoundAction.ClearActions();
      _compoundAction.EnableMessageDisplay(IsMessageDisplayEnabled());

      float initialWait_s = GetRNG().RandDblInRange(_minWaitTime_s, _maxWaitTime_s);
      
      float firstTurnDir = GetRNG().RandDbl() > 0.5f ? 1.0f : -1.0f;      
      float firstAngle_rads = firstTurnDir * GetRNG().RandDblInRange(_minSearchAngle_rads, _maxSearchAngle_rads);
      float afterFirstTurnWait_s = GetRNG().RandDblInRange(_minWaitTime_s, _maxWaitTime_s);

      float secondAngle_rads = -firstAngle_rads
        - firstTurnDir * GetRNG().RandDblInRange(_minSearchAngle_rads, _maxSearchAngle_rads);
      float afterSecondTurnWait_s = GetRNG().RandDblInRange(_minWaitTime_s, _maxWaitTime_s);

      PRINT_NAMED_DEBUG("SearchForNearbyObjectAction.Init",
                        "Action will wait %f, turn %fdeg, wait %f, turn %fdeg, wait %f",
                        initialWait_s,
                        RAD_TO_DEG(firstAngle_rads),
                        afterFirstTurnWait_s,
                        RAD_TO_DEG(secondAngle_rads),
                        afterSecondTurnWait_s);

      AddToCompoundAction(new WaitAction(initialWait_s));

      DriveStraightAction* driveBackAction = nullptr;

      const float defaultBackupSpeed = Util::numeric_cast<f32>(Util::EnumToUnderlying(SFNOD::BackupSpeed_mms));
      if( Util::IsFltNear(defaultBackupSpeed, _backupSpeed_mms ) ) {
        // if using the default backup speed, don't specify it to the action (So the motion profile can take
        // over if it's set)
        driveBackAction = new DriveStraightAction(_backupDistance_mm);
        driveBackAction->SetShouldPlayAnimation(false);
      }
      else {
        // otherwise, manually specify the backup speed
        driveBackAction = new DriveStraightAction(_backupDistance_mm, _backupSpeed_mms, false);
      }
      
      IActionRunner* driveAndLook = new CompoundActionParallel({
        driveBackAction,
        new MoveHeadToAngleAction(_headAngle_rad)
      });
      
      
      AddToCompoundAction(driveAndLook);
      
      AddToCompoundAction(new WaitAction(initialWait_s));

      TurnInPlaceAction* turn0 = new TurnInPlaceAction(firstAngle_rads, false);
      turn0->SetTolerance(DEG_TO_RAD(4.0f));
      AddToCompoundAction(turn0);
      
      AddToCompoundAction(new WaitAction(afterFirstTurnWait_s));

      TurnInPlaceAction* turn1 = new TurnInPlaceAction(secondAngle_rads, false);
      turn1->SetTolerance(DEG_TO_RAD(4.0f));
      AddToCompoundAction(turn1);

      AddToCompoundAction(new WaitAction(afterSecondTurnWait_s));

      // Go ahead and do the first Update for the compound action so we don't
      // "waste" the first CheckIfDone call doing so. Proceed so long as this
      // first update doesn't _fail_
      ActionResult compoundResult = _compoundAction.Update();
      if((ActionResult::SUCCESS == compoundResult) ||
         (ActionResult::RUNNING == compoundResult))
      {
        return ActionResult::SUCCESS;
      } else {
        return compoundResult;
      }
    }

    ActionResult SearchForNearbyObjectAction::CheckIfDone()
    {
      ActionResult internalResult = _compoundAction.Update();
      // check if the object has been located and actually observed
      if(_objectObservedDuringSearch)
      {
        _objectObservedDuringSearch = false;
        return ActionResult::SUCCESS;
      }
      
      // unsuccessful in finding the object
      else if((internalResult == ActionResult::SUCCESS) &&
              _desiredObjectID.IsSet()){
        return ActionResult::VISUAL_OBSERVATION_FAILED;
      }
      
      return internalResult;
    }

    void SearchForNearbyObjectAction::AddToCompoundAction(IActionRunner* action)
    {
      _compoundAction.AddAction(action);
    }

#pragma mark ---- DriveStraightAction ----
    
    DriveStraightAction::DriveStraightAction(f32 dist_mm)
    : IAction("DriveStraight",
              RobotActionType::DRIVE_STRAIGHT,
              (u8)AnimTrackFlag::BODY_TRACK)
    , _dist_mm(dist_mm)
    {

      // set default speed based on the driving direction
      if( dist_mm >= 0.0f ) {
        _speed_mmps = DEFAULT_PATH_MOTION_PROFILE.speed_mmps;
      }
      else {
        _speed_mmps = -DEFAULT_PATH_MOTION_PROFILE.reverseSpeed_mmps;
      }
      
      SetName("DriveStraight" + std::to_string(_dist_mm) + "mm");
    }
  
    DriveStraightAction::DriveStraightAction(f32 dist_mm, f32 speed_mmps, bool shouldPlayAnimation)
      : DriveStraightAction(dist_mm)
    {
      _speed_mmps = speed_mmps;
      _motionProfileManuallySet = true; // speed has been specified manually
      _shouldPlayDrivingAnimation = shouldPlayAnimation;
      
      if(Util::IsFltLTZero(_speed_mmps))
      {
        PRINT_NAMED_WARNING("DriveStraightAction.Constructor.NegativeSpeed",
                            "Speed should always be positive (not %f). Making positive.",
                            _speed_mmps);
        _speed_mmps = -_speed_mmps;
      }
      
      if(Util::IsFltLTZero(dist_mm))
      {
        // If distance is negative, we are driving backward and will negate speed
        // internally. Yes, we could have just double-negated if the caller passed in
        // a negative speed already, but this avoids confusion on caller's side about
        // which signs to use and the documentation says speed should always be positive.
        DEV_ASSERT(_speed_mmps >= 0.f, "DriveStraightAction.Constructor.NegativeSpeed");
        _speed_mmps = -_speed_mmps;
      }
      
      SetName("DriveStraight" + std::to_string(_dist_mm) + "mm@" +
              std::to_string(_speed_mmps) + "mmps");
    }

    DriveStraightAction::~DriveStraightAction()
    {
      if(HasRobot()){
        if( GetRobot().GetPathComponent().IsActive() ) {
          GetRobot().GetPathComponent().Abort();
        }

        GetRobot().GetDrivingAnimationHandler().ActionIsBeingDestroyed();
      }
    }

    void DriveStraightAction::GetRequiredVisionModes(std::set<VisionModeRequest>& requests) const
    {
      requests.insert({ VisionMode::DetectingMarkers, EVisionUpdateFrequency::Low });
    }

    void DriveStraightAction::SetAccel(f32 accel_mmps2)
    {
      _accel_mmps2 = accel_mmps2;
      _motionProfileManuallySet = true;
    }

    void DriveStraightAction::SetDecel(f32 decel_mmps2)
    {
      _decel_mmps2 = decel_mmps2;
      _motionProfileManuallySet = true;
    }

    bool DriveStraightAction::SetMotionProfile(const PathMotionProfile& profile)
    {
      if( _motionProfileManuallySet ) {
        // don't want to use the custom profile since someone manually specified speeds
        return false;
      }
      else {
        _speed_mmps = ( _dist_mm < 0.0f ) ? -profile.reverseSpeed_mmps : profile.speed_mmps;
        _accel_mmps2 = profile.accel_mmps2;
        _decel_mmps2 = profile.decel_mmps2;
        return true;
      }
    }
  
    ActionResult DriveStraightAction::Init()
    {
      GetRobot().GetDrivingAnimationHandler().Init(GetTracksToLock(), GetTag(), IsSuppressingTrackLocking());
      
      if(Util::IsNearZero(_dist_mm)) {
        // special case
        _hasStarted = true;
        return ActionResult::SUCCESS;
      }
      
      const Radians heading = GetRobot().GetPose().GetRotation().GetAngleAroundZaxis();
      
      const Vec3f& T = GetRobot().GetDriveCenterPose().GetTranslation();
      const f32 x_start = T.x();
      const f32 y_start = T.y();
      
      const f32 x_end = x_start + _dist_mm * std::cos(heading.ToFloat());
      const f32 y_end = y_start + _dist_mm * std::sin(heading.ToFloat());
      
      Planning::Path path;
      if(false  == path.AppendLine(x_start, y_start, x_end, y_end,
                                   _speed_mmps, _accel_mmps2, _decel_mmps2))
      {
        PRINT_NAMED_ERROR("DriveStraightAction.Init.AppendLineFailed", "");
        return ActionResult::PATH_PLANNING_FAILED_ABORT;
      }
      
      _hasStarted = false;
      
      // Tell robot to execute this simple path
      if(RESULT_OK != GetRobot().GetPathComponent().ExecuteCustomPath(path)) {
        return ActionResult::SEND_MESSAGE_TO_ROBOT_FAILED;
      }
      
      return ActionResult::SUCCESS;
    }
    
    ActionResult DriveStraightAction::CheckIfDone()
    {
      if(GetRobot().GetDrivingAnimationHandler().IsPlayingEndAnim())
      {
        return ActionResult::RUNNING;
      }

      if( GetRobot().GetPathComponent().LastPathFailed() ) {
        return ActionResult::FAILED_TRAVERSING_PATH;
      }
      
      if(!_hasStarted) {
        PRINT_CH_INFO("Actions", "DriveStraightAction.CheckIfDone.WaitingForPathStart", "");
        _hasStarted = GetRobot().GetPathComponent().HasPathToFollow();
        if( _hasStarted )
        {
          PRINT_CH_DEBUG("Actions", "DriveStraightAction.CheckIfDone.PathJustStarted", "");
          if(_shouldPlayDrivingAnimation) {
            GetRobot().GetDrivingAnimationHandler().PlayStartAnim();
          }
        }
      }

      if ( _hasStarted && !GetRobot().GetPathComponent().IsActive() ) {
        PRINT_CH_DEBUG("Actions", "DriveStraightAction.CheckIfDone.PathJustCompleted", "");
        if( _shouldPlayDrivingAnimation ) {
          if( GetRobot().GetDrivingAnimationHandler().PlayEndAnim()) {
            return ActionResult::RUNNING;
          }
        }

        // no end animation to play, end action now
        return ActionResult::SUCCESS;
      }

      return ActionResult::RUNNING;
    }
    
    
#pragma mark ---- CalibrateMotorAction ----
    
    CalibrateMotorAction::CalibrateMotorAction(bool calibrateHead,
                                               bool calibrateLift)
    : IAction("CalibrateMotor-" + std::string(calibrateHead ? "Head" : "") + std::string(calibrateLift ? "Lift" : ""),
              RobotActionType::CALIBRATE_MOTORS,
              (calibrateHead ? (u8)AnimTrackFlag::HEAD_TRACK : 0) | (calibrateLift ? (u8)AnimTrackFlag::LIFT_TRACK : 0) )
    , _calibHead(calibrateHead)
    , _calibLift(calibrateLift)
    , _headCalibStarted(false)
    , _liftCalibStarted(false)
    {
      
    }
    
    ActionResult CalibrateMotorAction::Init()
    {
      ActionResult result = ActionResult::SUCCESS;
      _headCalibStarted = false;
      _liftCalibStarted = false;
      if (RESULT_OK != GetRobot().GetMoveComponent().CalibrateMotors(_calibHead, _calibLift)) {
        return ActionResult::SEND_MESSAGE_TO_ROBOT_FAILED;
      }
      
      if(GetRobot().HasExternalInterface())
      {
        using namespace ExternalInterface;
        auto helper = MakeAnkiEventUtil(*(GetRobot().GetExternalInterface()), *this, _signalHandles);
        helper.SubscribeEngineToGame<MessageEngineToGameTag::MotorCalibration>();
      }
      
      return result;
    }
    
    ActionResult CalibrateMotorAction::CheckIfDone()
    {
      ActionResult result = ActionResult::RUNNING;
      bool headCalibrating = !GetRobot().IsHeadCalibrated();
      bool liftCalibrating = !GetRobot().IsLiftCalibrated();
      
      bool headComplete = !_calibHead || (_headCalibStarted && !headCalibrating);
      bool liftComplete = !_calibLift || (_liftCalibStarted && !liftCalibrating);
      if (headComplete && liftComplete) {
        PRINT_NAMED_INFO("CalibrateMotorAction.CheckIfDone.Done", "");
        result = ActionResult::SUCCESS;
      }

      return result;
    }
    
    template<>
    void CalibrateMotorAction::HandleMessage(const MotorCalibration& msg)
    {
      if (msg.calibStarted) {
        if (msg.motorID == MotorID::MOTOR_HEAD) {
          _headCalibStarted = true;
        }
        if (msg.motorID == MotorID::MOTOR_LIFT) {
          _liftCalibStarted = true;
        }
      }
    }
    
#pragma mark ---- MoveHeadToAngleAction ----
    
    MoveHeadToAngleAction::MoveHeadToAngleAction(const Radians& headAngle, const Radians& tolerance, const Radians& variability)
    : IAction("MoveHeadTo" + std::to_string(headAngle.getDegrees()) + "Deg",
              RobotActionType::MOVE_HEAD_TO_ANGLE,
              (u8)AnimTrackFlag::HEAD_TRACK)
    , _headAngle(headAngle)
    , _angleTolerance(tolerance)
    , _variability(variability)
    , _inPosition(false)
    {
      if(_headAngle < MIN_HEAD_ANGLE) {
        PRINT_NAMED_WARNING("MoveHeadToAngleAction.Constructor.AngleTooLow",
                            "Requested head angle (%.1fdeg) less than min head angle (%.1fdeg). Clipping.",
                            _headAngle.getDegrees(), RAD_TO_DEG(MIN_HEAD_ANGLE));
        _headAngle = MIN_HEAD_ANGLE;
      } else if(_headAngle > MAX_HEAD_ANGLE) {
        PRINT_NAMED_WARNING("MoveHeadToAngleAction.Constructor.AngleTooHigh",
                            "Requested head angle (%.1fdeg) more than max head angle (%.1fdeg). Clipping.",
                            _headAngle.getDegrees(), RAD_TO_DEG(MAX_HEAD_ANGLE));
        _headAngle = MAX_HEAD_ANGLE;
      }
      
      if( _angleTolerance.ToFloat() < HEAD_ANGLE_TOL ) {
        PRINT_NAMED_WARNING("MoveHeadToAngleAction.InvalidTolerance",
                            "Tried to set tolerance of %fdeg, min is %f",
                            _angleTolerance.getDegrees(),
                            RAD_TO_DEG(HEAD_ANGLE_TOL));
        _angleTolerance = HEAD_ANGLE_TOL;
      }
      
      if(_variability > 0) {
        _headAngle += GetRNG().RandDblInRange(-_variability.ToDouble(), _variability.ToDouble());
        _headAngle = CLIP(_headAngle, MIN_HEAD_ANGLE, MAX_HEAD_ANGLE);
      }
    }
    
    MoveHeadToAngleAction::MoveHeadToAngleAction(const Preset preset, const Radians& tolerance, const Radians& variability)
      : MoveHeadToAngleAction(GetPresetHeadAngle(preset), tolerance, variability)
    {
      SetName(std::string("MoveHeadTo_") + GetPresetName(preset));
    }
    
    f32 MoveHeadToAngleAction::GetPresetHeadAngle(Preset preset)
    {
      switch(preset) {
        case Preset::GROUND_PLANE_VISIBLE: { return DEG_TO_RAD(-15.0f); }
        case Preset::IDEAL_BLOCK_VIEW: { return kIdealViewBlockHeadAngle; }
      }
      DEV_ASSERT(false, "MoveHeadToAngleAction.NotAPreset");
      return -1.0f;
    }
    
    const char* MoveHeadToAngleAction::GetPresetName(Preset preset)
    {
      switch(preset) {
        case Preset::GROUND_PLANE_VISIBLE: { return "GroundPlaneVisible"; }
        case Preset::IDEAL_BLOCK_VIEW: { return "IdealBlockView"; }
      }
      DEV_ASSERT(false, "MoveHeadToAngleAction.NotAPreset");
      return "ERROR";
    }
    
    MoveHeadToAngleAction::~MoveHeadToAngleAction()
    {
      if(HasRobot()){
        // Make sure eye shift gets removed, by this action, or by the MoveComponent if "hold" is enabled
        if(_holdEyes) {
          GetRobot().GetMoveComponent().RemoveEyeShiftWhenHeadMoves(_kEyeShiftLayerName, 3*ANIM_TIME_STEP_MS);
        } else {
          GetRobot().GetAnimationComponent().RemoveEyeShift(_kEyeShiftLayerName);
        }
      }
    }
    
    bool MoveHeadToAngleAction::IsHeadInPosition() const
    {
      const bool inPosition = _headAngle.IsNear(GetRobot().GetComponent<FullRobotPose>().GetHeadAngle(), _angleTolerance.ToFloat()+Util::FLOATING_POINT_COMPARISON_TOLERANCE_FLT);
      return inPosition;
    }
    
    ActionResult MoveHeadToAngleAction::Init()
    {
      ActionResult result = ActionResult::SUCCESS;
      _motionCommanded = false;
      _motionCommandAcked = false;
      _motionStarted = false;
      _inPosition = IsHeadInPosition();
      
      if (!_inPosition) {
        if(RESULT_OK != GetRobot().GetMoveComponent().MoveHeadToAngle(_headAngle.ToFloat(),
                                                                      _maxSpeed_radPerSec,
                                                                      _accel_radPerSec2,
                                                                      _duration_sec,
                                                                      &_actionID))
        {
          result = ActionResult::SEND_MESSAGE_TO_ROBOT_FAILED;
        } else {
          _motionCommanded = true;
        }
        
        if(_moveEyes)
        { 
          // Lead with the eyes, if not in position
          // Note: assuming screen is about the same x distance from the neck joint as the head cam
           Radians angleDiff =  GetRobot().GetComponent<FullRobotPose>().GetHeadAngle() - _headAngle;
           const f32 y_mm = std::tan(angleDiff.ToFloat()) * HEAD_CAM_POSITION[0];
           const f32 yPixShift = y_mm * (static_cast<f32>(GetRobot().GetDisplayHeightInPixels()/4) / SCREEN_SIZE[1]);
          GetRobot().GetAnimationComponent().AddOrUpdateEyeShift(_kEyeShiftLayerName, 0, yPixShift, 4*ANIM_TIME_STEP_MS);
          
          if(!_holdEyes) {
            // Store the half the angle differene so we know when to remove eye shift
            _halfAngle = 0.5f*(_headAngle - GetRobot().GetComponent<FullRobotPose>().GetHeadAngle()).getAbsoluteVal();
          }
        }
      }
      
      // Subscribe to motor command ack
      auto actionStartedLambda = [this](const AnkiEvent<RobotInterface::RobotToEngine>& event)
      {
        if(_motionCommanded && _actionID == event.GetData().Get_motorActionAck().actionID) {
          PRINT_CH_INFO("Actions", "MoveHeadToAngleAction.MotorActionAcked",
                        "[%d] ActionID: %d",
                        GetTag(),
                        _actionID);
          _motionCommandAcked = true;
        }
      };
      
      _signalHandle = GetRobot().GetRobotMessageHandler()->Subscribe(RobotInterface::RobotToEngineTag::motorActionAck,
                                                                     actionStartedLambda);


      return result;
    }
    
    ActionResult MoveHeadToAngleAction::CheckIfDone()
    {
      ActionResult result = ActionResult::RUNNING;
      
      if (_motionCommanded && !_motionCommandAcked) {
        PRINT_PERIODIC_CH_DEBUG(10, "Actions", "MoveHeadToAngleAction.CheckIfDone.WaitingForAck",
                                "[%d] ActionID: %d",
                                GetTag(),
                                _actionID);
        return result;
      }

      if(!_inPosition) {
        _inPosition = IsHeadInPosition();
      }
      
      if(GetRobot().GetAnimationComponent().IsEyeShifting(_kEyeShiftLayerName) && !_holdEyes )
      {
        // If we're not there yet but at least halfway, and we're not supposed
        // to "hold" the eyes, then remove eye shift
        if(_inPosition || _headAngle.IsNear(GetRobot().GetComponent<FullRobotPose>().GetHeadAngle(), _halfAngle))
        {
          PRINT_CH_DEBUG("Actions", "MoveHeadToAngleAction.CheckIfDone.RemovingEyeShift",
                         "[%d] Currently at %.1fdeg, on the way to %.1fdeg, within "
                         "half angle of %.1fdeg",
                         GetTag(),
                         RAD_TO_DEG(GetRobot().GetComponent<FullRobotPose>().GetHeadAngle()),
                         _headAngle.getDegrees(),
                         _halfAngle.getDegrees());
          
          GetRobot().GetAnimationComponent().RemoveEyeShift(_kEyeShiftLayerName, 3*ANIM_TIME_STEP_MS);
        }
      }
      
      const bool isHeadMoving = GetRobot().GetMoveComponent().IsHeadMoving();
      if( isHeadMoving ) {
        _motionStarted = true;
      }
      
      // Wait to get a state message back from the physical robot saying its head
      // is in the commanded position
      // TODO: Is this really necessary in practice?
      if(_inPosition) {
      
        if(isHeadMoving)
        {
          PRINT_CH_INFO("Actions",
                        "MoveHeadToAngleAction.CheckIfDone.HeadMovingInPosition",
                        "[%d] Head considered in position at %.1fdeg but still moving at %.1fdeg",
                        GetTag(),
                        _headAngle.getDegrees(),
                        RAD_TO_DEG(GetRobot().GetComponent<FullRobotPose>().GetHeadAngle()));
        }
      
        result = isHeadMoving ? ActionResult::RUNNING : ActionResult::SUCCESS;
      } else {
        // Don't spam "not in position messages"
        PRINT_PERIODIC_CH_DEBUG(10, "Actions", "MoveHeadToAngleAction.CheckIfDone.NotInPosition",
                                "[%d] Waiting for head to get in position: %.1fdeg vs. %.1fdeg(+/-%.1f) tol:%.1fdeg",
                                GetTag(),
                                RAD_TO_DEG(GetRobot().GetComponent<FullRobotPose>().GetHeadAngle()),
                                _headAngle.getDegrees(),
                                _variability.getDegrees(),
                                _angleTolerance.getDegrees());
        
        if( _motionStarted && !isHeadMoving ) {
          PRINT_NAMED_WARNING("MoveHeadToAngleAction.CheckIfDone.StoppedMakingProgress",
                              "[%d] giving up since we stopped moving",
                              GetTag());
          result = ActionResult::MOTOR_STOPPED_MAKING_PROGRESS;
        }
      }
      
      return result;
    }
    
#pragma mark ---- MoveLiftToHeightAction ----
    
    MoveLiftToHeightAction::MoveLiftToHeightAction(const f32 height_mm, const f32 tolerance_mm, const f32 variability)
    : IAction("MoveLiftTo" + std::to_string(height_mm) + "mm",
              RobotActionType::MOVE_LIFT_TO_HEIGHT,
              (u8)AnimTrackFlag::LIFT_TRACK)
    , _height_mm(height_mm)
    , _heightTolerance(tolerance_mm)
    , _variability(variability)
    , _inPosition(false)
    {
      
    }
    
    MoveLiftToHeightAction::MoveLiftToHeightAction(const Preset preset, const f32 tolerance_mm)
    : MoveLiftToHeightAction(GetPresetHeight(preset), tolerance_mm, 0.f)
    {
      SetName("MoveLiftTo" + GetPresetName(preset));
    }
    
    
    f32 MoveLiftToHeightAction::GetPresetHeight(Preset preset)
    {
      static const std::map<Preset, f32> LUT = {
        {Preset::LOW_DOCK,   LIFT_HEIGHT_LOWDOCK},
        {Preset::HIGH_DOCK,  LIFT_HEIGHT_HIGHDOCK},
        {Preset::CARRY,      LIFT_HEIGHT_CARRY},
        {Preset::OUT_OF_FOV, -1.f},
      };
      
      return LUT.at(preset);
    }
    
    const std::string& MoveLiftToHeightAction::GetPresetName(Preset preset)
    {
      static const std::map<Preset, std::string> LUT = {
        {Preset::LOW_DOCK,   "LowDock"},
        {Preset::HIGH_DOCK,  "HighDock"},
        {Preset::CARRY,      "HeightCarry"},
        {Preset::OUT_OF_FOV, "OutOfFOV"},
      };
      
      static const std::string unknown("UnknownPreset");
      
      auto iter = LUT.find(preset);
      if(iter == LUT.end()) {
        return unknown;
      } else {
        return iter->second;
      }
    }
    
    bool MoveLiftToHeightAction::IsLiftInPosition() const
    {
      const bool inPosition = (NEAR(_heightWithVariation, GetRobot().GetLiftHeight(), _heightTolerance) &&
                               !GetRobot().GetMoveComponent().IsLiftMoving());
      
      return inPosition;
    }
    
    ActionResult MoveLiftToHeightAction::Init()
    {
      ActionResult result = ActionResult::SUCCESS;
      _motionCommanded = false;
      _motionCommandAcked = false;
      _motionStarted = false;
      
      if (_height_mm >= 0 && (_height_mm < LIFT_HEIGHT_LOWDOCK || _height_mm > LIFT_HEIGHT_CARRY)) {
        PRINT_NAMED_WARNING("MoveLiftToHeightAction.Init.InvalidHeight",
                            "%f mm. Clipping to be in range.", _height_mm);
        _height_mm = CLIP(_height_mm, LIFT_HEIGHT_LOWDOCK, LIFT_HEIGHT_CARRY);
      }
      
      if(_height_mm < 0.f) {
        // Choose whatever is closer to current height, LOW or CARRY:
        const f32 currentHeight = GetRobot().GetLiftHeight();
        const f32 low   = GetPresetHeight(Preset::LOW_DOCK);
        const f32 carry = GetPresetHeight(Preset::CARRY);
        // Absolute values here shouldn't be necessary, since these are supposed
        // to be the lowest and highest possible lift settings, but just in case...
        if( std::abs(currentHeight-low) < std::abs(carry-currentHeight)) {
          _heightWithVariation = low;
        } else {
          _heightWithVariation = carry;
        }
      } else {
        _heightWithVariation = _height_mm;
        if(_variability > 0.f) {
          _heightWithVariation += GetRNG().RandDblInRange(-_variability, _variability);
        }
        _heightWithVariation = CLIP(_heightWithVariation, LIFT_HEIGHT_LOWDOCK, LIFT_HEIGHT_CARRY);
      }
      
      
      // Convert height tolerance to angle tolerance and make sure that it's larger
      // than the tolerance that the liftController uses.
      
      // Convert target height, height - tol, and height + tol to angles.
      f32 heightLower = _heightWithVariation - _heightTolerance;
      f32 heightUpper = _heightWithVariation + _heightTolerance;
      f32 targetAngle = ConvertLiftHeightToLiftAngleRad(_heightWithVariation);
      f32 targetAngleLower = ConvertLiftHeightToLiftAngleRad(heightLower);
      f32 targetAngleUpper = ConvertLiftHeightToLiftAngleRad(heightUpper);
      
      // Neither of the angular differences between targetAngle and its associated
      // lower and upper tolerance limits should be smaller than LIFT_ANGLE_TOL.
      // That is, unless the limits exceed the physical limits of the lift.
      f32 minAngleDiff = std::numeric_limits<f32>::max();
      if (heightLower > LIFT_HEIGHT_LOWDOCK) {
        minAngleDiff = targetAngle - targetAngleLower;
      }
      if (heightUpper < LIFT_HEIGHT_CARRY) {
        minAngleDiff = std::min(minAngleDiff, targetAngleUpper - targetAngle);
      }
      
      if (minAngleDiff < LIFT_ANGLE_TOL) {
        // Tolerance is too small. Clip to be within range.
        f32 desiredHeightLower = ConvertLiftAngleToLiftHeightMM(targetAngle - LIFT_ANGLE_TOL);
        f32 desiredHeightUpper = ConvertLiftAngleToLiftHeightMM(targetAngle + LIFT_ANGLE_TOL);
        f32 newHeightTolerance = std::max(_height_mm - desiredHeightLower, desiredHeightUpper - _height_mm);
        
        PRINT_NAMED_WARNING("MoveLiftToHeightAction.Init.TolTooSmall",
                            "HeightTol %f mm == AngleTol %f rad near height of %f mm. Clipping tol to %f mm",
                            _heightTolerance, minAngleDiff, _heightWithVariation, newHeightTolerance);
        _heightTolerance = newHeightTolerance;
      }
      
      _inPosition = IsLiftInPosition();
      
      if (!_inPosition) {
        if(GetRobot().GetMoveComponent().MoveLiftToHeight(_heightWithVariation,
                                                       _maxLiftSpeedRadPerSec,
                                                       _liftAccelRacPerSec2,
                                                       _duration,
                                                       &_actionID) != RESULT_OK) {
          result = ActionResult::SEND_MESSAGE_TO_ROBOT_FAILED;
        } else {
          _motionCommanded = true;
        }
        
      }
      
      // Subscribe to motor command ack
      auto actionStartedLambda = [this](const AnkiEvent<RobotInterface::RobotToEngine>& event)
      {
        if(_motionCommanded && _actionID == event.GetData().Get_motorActionAck().actionID) {
          PRINT_CH_INFO("Actions", "MoveLiftToHeightAction.MotorActionAcked",
                        "[%d] ActionID: %d",
                        GetTag(),
                        _actionID);
          _motionCommandAcked = true;
        }
      };
      
      _signalHandle = GetRobot().GetRobotMessageHandler()->Subscribe(RobotInterface::RobotToEngineTag::motorActionAck,
                                                                     actionStartedLambda);


      return result;
    }
    
    ActionResult MoveLiftToHeightAction::CheckIfDone()
    {
      ActionResult result = ActionResult::RUNNING;
      
      if (_motionCommanded && !_motionCommandAcked) {
        PRINT_PERIODIC_CH_DEBUG(10, "Actions", "MoveLiftToHeightAction.CheckIfDone.WaitingForAck",
                                "[%d] ActionID: %d",
                                GetTag(),
                                _actionID);
        return result;
      }

      if(!_inPosition) {
        _inPosition = IsLiftInPosition();
      }
      
      const bool isLiftMoving = GetRobot().GetMoveComponent().IsLiftMoving();
      if( isLiftMoving ) {
        _motionStarted = true;
      }
      
      if(_inPosition) {
        result = isLiftMoving ? ActionResult::RUNNING : ActionResult::SUCCESS;
      } else {
        PRINT_PERIODIC_CH_DEBUG(10, "Actions", "MoveLiftToHeightAction.CheckIfDone.NotInPosition",
                                "[%d] Waiting for lift to get in position: %.1fmm vs. %.1fmm (tol: %f)",
                                GetTag(),
                                GetRobot().GetLiftHeight(), _heightWithVariation, _heightTolerance);
        
        if( _motionStarted && !isLiftMoving ) {
          PRINT_NAMED_WARNING("MoveLiftToHeightAction.CheckIfDone.StoppedMakingProgress",
                              "[%d] giving up since we stopped moving",
                              GetTag());
          result = ActionResult::MOTOR_STOPPED_MAKING_PROGRESS;
        }
      }
      
      return result;
    }
    
#pragma mark ---- PanAndTiltAction ----
    
    PanAndTiltAction::PanAndTiltAction(Radians bodyPan, Radians headTilt,
                                       bool isPanAbsolute, bool isTiltAbsolute)
    : IAction("PanAndTilt",
              RobotActionType::PAN_AND_TILT,
              (u8)AnimTrackFlag::BODY_TRACK | (u8)AnimTrackFlag::HEAD_TRACK)
    , _compoundAction()
    , _bodyPanAngle(bodyPan)
    , _headTiltAngle(headTilt)
    , _isPanAbsolute(isPanAbsolute)
    , _isTiltAbsolute(isTiltAbsolute)
    {
      // Put the angles in the name for debugging
      SetName("Pan" + std::to_string(std::round(_bodyPanAngle.getDegrees())) +
              "AndTilt" + std::to_string(std::round(_headTiltAngle.getDegrees())));
    }
    
    PanAndTiltAction::~PanAndTiltAction()
    {
      _compoundAction.PrepForCompletion();
    }

    void PanAndTiltAction::OnRobotSet()
    {
      _compoundAction.SetRobot(&GetRobot());
      OnRobotSetInternalPan();
    }
    

    void PanAndTiltAction::SetMaxPanSpeed(f32 maxSpeed_radPerSec)
    {
      if (maxSpeed_radPerSec == 0.f) {
        _maxPanSpeed_radPerSec = _kDefaultMaxPanSpeed;
      } else if (std::fabsf(maxSpeed_radPerSec) > MAX_BODY_ROTATION_SPEED_RAD_PER_SEC) {
        PRINT_NAMED_WARNING("PanAndTiltAction.SetMaxSpeed.PanSpeedExceedsLimit",
                            "Speed of %f deg/s exceeds limit of %f deg/s. Clamping.",
                            RAD_TO_DEG(maxSpeed_radPerSec), MAX_BODY_ROTATION_SPEED_DEG_PER_SEC);
        _maxPanSpeed_radPerSec = std::copysign(MAX_BODY_ROTATION_SPEED_RAD_PER_SEC, maxSpeed_radPerSec);
        _panSpeedsManuallySet = true;
      } else {
        _maxPanSpeed_radPerSec = maxSpeed_radPerSec;
        _panSpeedsManuallySet = true;
      }
    }
    
    void PanAndTiltAction::SetPanAccel(f32 accel_radPerSec2)
    {
      // If 0, use default value
      if (accel_radPerSec2 == 0.f) {
        _panAccel_radPerSec2 = _kDefaultPanAccel;
      } else {
        _panAccel_radPerSec2 = accel_radPerSec2;
        _panSpeedsManuallySet = true;
      }
    }
    
    void PanAndTiltAction::SetPanTolerance(const Radians& angleTol_rad)
    {
      if (angleTol_rad == 0.f) {
        _panAngleTol = _kDefaultPanAngleTol;
        return;
      }
      
      _panAngleTol = angleTol_rad.getAbsoluteVal();
      
      // NOTE: can't be lower than what is used internally on the robot
      if( _panAngleTol.ToFloat() < POINT_TURN_ANGLE_TOL ) {
        PRINT_NAMED_WARNING("PanAndTiltAction.SetPanTolerance.InvalidTolerance",
                            "Tried to set tolerance of %fdeg, min is %f",
                            _panAngleTol.getDegrees(),
                            RAD_TO_DEG(POINT_TURN_ANGLE_TOL));
        _panAngleTol = POINT_TURN_ANGLE_TOL;
      }
    }
    
    void PanAndTiltAction::SetMaxTiltSpeed(f32 maxSpeed_radPerSec)
    {
      if (maxSpeed_radPerSec == 0.f) {
        _maxTiltSpeed_radPerSec = _kDefaultMaxTiltSpeed;
      } else {
        _maxTiltSpeed_radPerSec = maxSpeed_radPerSec;
        _tiltSpeedsManuallySet = true;
      }
    }
    
    void PanAndTiltAction::SetTiltAccel(f32 accel_radPerSec2)
    {
      if (accel_radPerSec2 == 0.f) {
        _tiltAccel_radPerSec2 = _kDefaultTiltAccel;
      } else {
        _tiltAccel_radPerSec2 = accel_radPerSec2;
        _tiltSpeedsManuallySet = true;
      }
    }
    
    void PanAndTiltAction::SetTiltTolerance(const Radians& angleTol_rad)
    {
      // If 0, use default value
      if (angleTol_rad == 0.f) {
        _tiltAngleTol = _kDefaultTiltAngleTol;
        return;
      }
      
      _tiltAngleTol = angleTol_rad.getAbsoluteVal();
      
      // NOTE: can't be lower than what is used internally on the robot
      if( _tiltAngleTol.ToFloat() < HEAD_ANGLE_TOL ) {
        PRINT_NAMED_WARNING("PanAndTiltAction.SetTiltTolerance.InvalidTolerance",
                            "Tried to set tolerance of %fdeg, min is %f",
                            _tiltAngleTol.getDegrees(),
                            RAD_TO_DEG(HEAD_ANGLE_TOL));
        _tiltAngleTol = HEAD_ANGLE_TOL;
      }
    }

    ActionResult PanAndTiltAction::Init()
    {
      // Incase we are re-running this action
      _compoundAction.ClearActions();
      _compoundAction.EnableMessageDisplay(IsMessageDisplayEnabled());
      
      TurnInPlaceAction* action = new TurnInPlaceAction(_bodyPanAngle.ToFloat(), _isPanAbsolute);      
      action->SetTolerance(_panAngleTol);
      action->SetMoveEyes(_moveEyes);
      if( _panSpeedsManuallySet ) {
        action->SetMaxSpeed(_maxPanSpeed_radPerSec);
        action->SetAccel(_panAccel_radPerSec2);
      }
      _compoundAction.AddAction(action);
      
      const Radians newHeadAngle = _isTiltAbsolute ? _headTiltAngle : GetRobot().GetComponent<FullRobotPose>().GetHeadAngle() + _headTiltAngle;
      MoveHeadToAngleAction* headAction = new MoveHeadToAngleAction(newHeadAngle, _tiltAngleTol);
      headAction->SetMoveEyes(_moveEyes);
      if( _tiltSpeedsManuallySet ) {
        headAction->SetMaxSpeed(_maxTiltSpeed_radPerSec);
        headAction->SetAccel(_tiltAccel_radPerSec2);
      }
      _compoundAction.AddAction(headAction);

      // Prevent the compound action from locking tracks (the PanAndTiltAction handles it itself)
      _compoundAction.ShouldSuppressTrackLocking(true);
      
      // Go ahead and do the first Update for the compound action so we don't
      // "waste" the first CheckIfDone call doing so. Proceed so long as this
      // first update doesn't _fail_
      ActionResult compoundResult = _compoundAction.Update();
      if(ActionResult::SUCCESS == compoundResult ||
         ActionResult::RUNNING == compoundResult)
      {
        return ActionResult::SUCCESS;
      } else {
        return compoundResult;
      }
      
    } // PanAndTiltAction::Init()
    
    
    ActionResult PanAndTiltAction::CheckIfDone()
    {
      return _compoundAction.Update();
    }
    #pragma mark ---- TurnTowardsObjectAction ----
    
    TurnTowardsObjectAction::TurnTowardsObjectAction(ObjectID objectID,
                                                     Radians maxTurnAngle,
                                                     bool visuallyVerifyWhenDone,
                                                     bool headTrackWhenDone)
    : TurnTowardsObjectAction(objectID,
                              Vision::Marker::ANY_CODE,
                              maxTurnAngle,
                              visuallyVerifyWhenDone,
                              headTrackWhenDone)
    {

    }
    
    TurnTowardsObjectAction::TurnTowardsObjectAction(ObjectID objectID,
                                                     Vision::Marker::Code whichCode,
                                                     Radians maxTurnAngle,
                                                     bool visuallyVerifyWhenDone,
                                                     bool headTrackWhenDone)
    : TurnTowardsPoseAction(maxTurnAngle)
    , _facePoseCompoundActionDone(false)
    , _visuallyVerifyWhenDone(visuallyVerifyWhenDone)
    , _objectID(objectID)
    , _whichCode(whichCode)
    , _headTrackWhenDone(headTrackWhenDone)
    {
      SetName("TurnTowardsObject" + std::to_string(_objectID.GetValue()));
      SetType(RobotActionType::TURN_TOWARDS_OBJECT);
    }
    
    TurnTowardsObjectAction::~TurnTowardsObjectAction()
    {
      if(nullptr != _visuallyVerifyAction) {
        _visuallyVerifyAction->PrepForCompletion();
      }
    }

    void TurnTowardsObjectAction::GetRequiredVisionModes(std::set<VisionModeRequest>& requests) const
    {
      requests.insert({ VisionMode::DetectingMarkers, EVisionUpdateFrequency::Low });
    }

    void TurnTowardsObjectAction::UseCustomObject(ObservableObject* objectPtr)
    {
      if( _objectID.IsSet() ) {
        PRINT_NAMED_WARNING("TurnTowardsObjectAction.UseCustomObject.CustomObjectOverwriteId",
                            "object id was already set to %d, but now setting it to use a custom object ptr",
                            _objectID.GetValue());
        _objectID.UnSet();
      }
      
      _objectPtr = objectPtr;
      
      SetName("TurnTowardsCustomObject" + std::to_string(_objectPtr->GetID().GetValue()));
      
      if(!_objectPtr->GetID().IsSet())
      {
        PRINT_CH_INFO("Actions", "TurnTowardsObjectAction.UseCustomObject.NoCustomID", "");
      }
    }

    ActionResult TurnTowardsObjectAction::Init()
    {
      const bool usingCustomObject = !_objectID.IsSet();
      
      if(usingCustomObject)
      {
        if(nullptr == _objectPtr)
        {
          PRINT_NAMED_ERROR("TurnTowardsObjectAction.Init.NullCustomObject", "");
          return ActionResult::BAD_OBJECT;
        }
        
        // A custom object's pose must be in the robot's origin to turn towards it
        if(!GetRobot().IsPoseInWorldOrigin(_objectPtr->GetPose())) {
          PRINT_NAMED_WARNING("TurnTowardsObjectAction.Init.CustomObjectNotInRobotFrame",
                              "Custom %s object %d in origin:%s vs. robot in origin:%s",
                              EnumToString(_objectPtr->GetType()),
                              _objectPtr->GetID().GetValue(),
                              _objectPtr->GetPose().FindRoot().GetName().c_str(),
                              GetRobot().GetWorldOrigin().GetName().c_str());
          return ActionResult::BAD_POSE;
        }
        
        if(_visuallyVerifyWhenDone)
        {
          PRINT_NAMED_WARNING("TurnTowardsObjectAction.Init.CannotVisuallyVerifyCustomObject",
                              "Disabling visual verification");
          _visuallyVerifyWhenDone = false;
        }
      }
      else
      {
        _objectPtr = GetRobot().GetBlockWorld().GetLocatedObjectByID(_objectID);
        if(nullptr == _objectPtr) {
          PRINT_NAMED_WARNING("TurnTowardsObjectAction.Init.ObjectNotFound",
                              "Object with ID=%d no longer exists in the world.",
                              _objectID.GetValue());
          return ActionResult::BAD_OBJECT;
        }
      }
      
      Pose3d objectPoseWrtRobot;
      if(_whichCode == Vision::Marker::ANY_CODE) {

        // if ANY_CODE is specified, find the closest marker face to the robot and use that pose. We don't
        // want to consider the "top" or "bottom" faces (based on current rotation)

        // Solution: project all points into 2D and pick the closest. The top and bottom faces will never be
        // closer than the closest side face (unless we are inside the cube)
        
        const Result poseResult = _objectPtr->GetClosestMarkerPose(GetRobot().GetPose(), true, objectPoseWrtRobot);
        
        if( RESULT_OK != poseResult ) {
          // This should not occur because we checked above that the object is in the
          // same coordinate frame as the robot
          PRINT_NAMED_ERROR("TurnTowardsObjectAction.Init.NoValidPose",
                            "Could not get a valid closest marker pose of %sobject %d",
                            usingCustomObject ? "custom " : "",
                            _objectPtr->GetID().GetValue());
          return ActionResult::BAD_MARKER;
        }
      } else {
        // Use the closest marker with the specified code:
        std::vector<Vision::KnownMarker*> const& markers = _objectPtr->GetMarkersWithCode(_whichCode);
        
        if(markers.empty()) {
          PRINT_NAMED_ERROR("TurnTowardsObjectAction.Init.NoMarkersWithCode",
                            "%sbject %d does not have any markers with code %d.",
                            usingCustomObject ? "Custom o" : "O",
                            _objectPtr->GetID().GetValue(), _whichCode);
          return ActionResult::BAD_MARKER;
        }
        
        Vision::KnownMarker* closestMarker = nullptr;
        
        f32 closestDist = std::numeric_limits<f32>::max();
        Pose3d markerPoseWrtRobot;
        for(auto marker : markers) {
          if(false == marker->GetPose().GetWithRespectTo(GetRobot().GetPose(), markerPoseWrtRobot)) {
            PRINT_NAMED_ERROR("TurnTowardsObjectAction.Init.MarkerOriginProblem",
                              "Could not get pose of marker with code %d of %sobject %d "
                              "w.r.t. robot pose.", _whichCode,
                              usingCustomObject ? "custom " : "",
                              _objectPtr->GetID().GetValue());
            return ActionResult::BAD_POSE;
          }
          
          const f32 currentDist = markerPoseWrtRobot.GetTranslation().Length();
          if(currentDist < closestDist) {
            closestDist = currentDist;
            closestMarker = marker;
            objectPoseWrtRobot = markerPoseWrtRobot;
          }
        }
        
        if(closestMarker == nullptr) {
          PRINT_NAMED_ERROR("TurnTowardsObjectAction.Init.NoClosestMarker",
                            "No closest marker found for %sobject %d.",
                            usingCustomObject ? "custom " : "",
                            _objectPtr->GetID().GetValue());
          return ActionResult::BAD_MARKER;
        }
      }
      
      // Have to set the parent class's pose before calling its Init()
      SetPose(objectPoseWrtRobot);
      
      ActionResult facePoseInitResult = TurnTowardsPoseAction::Init();
      if(ActionResult::SUCCESS != facePoseInitResult) {
        return facePoseInitResult;
      }
      
      _facePoseCompoundActionDone = false;
      
      return ActionResult::SUCCESS;
    } // TurnTowardsObjectAction::Init()
    
    
    ActionResult TurnTowardsObjectAction::CheckIfDone()
    {
      // Tick the compound action until it completes
      if(!_facePoseCompoundActionDone) {
        ActionResult compoundResult = TurnTowardsPoseAction::CheckIfDone();
        
        if(compoundResult != ActionResult::SUCCESS) {
          return compoundResult;
        } else {
          _facePoseCompoundActionDone = true;
          
          if(_doRefinedTurn)
          {
            // If we need to refine the turn just reset this action, set appropriate variables
            Reset(false);
            ShouldDoRefinedTurn(false);
            SetPanTolerance(_refinedTurnAngleTol_rad);
            
            PRINT_CH_INFO("Actions", "TurnTowardsObjectAction.CheckIfDone.RefiningTurn",
                          "Refining turn towards %sobject %d",
                          _objectID.IsSet() ? "" : "custom ",
                          _objectPtr->GetID().GetValue());
            
            return ActionResult::RUNNING;
          }
          else if(_visuallyVerifyWhenDone)
          {
            IActionRunner* action = nullptr;
            if(kInsertWaitsInTurnTowardsObjectVerify)
            {
              action = new CompoundActionSequential({new WaitAction(2),
                  new VisuallyVerifyObjectAction(_objectPtr->GetID(), _whichCode),
                  new WaitAction(2)});
            }
            else
            {
              action = new VisuallyVerifyObjectAction(_objectPtr->GetID(), _whichCode);
            }
            _visuallyVerifyAction.reset(action);
            if(_visuallyVerifyAction != nullptr){
              _visuallyVerifyAction->SetRobot(&GetRobot());
            }
            
            // Disable completion signals since this is inside another action
            _visuallyVerifyAction->ShouldSuppressTrackLocking(true);
            
            // Go ahead and do a first tick of visual verification's Update, to
            // get it initialized
            ActionResult verificationResult = _visuallyVerifyAction->Update();
            if(ActionResult::SUCCESS != verificationResult) {
              return verificationResult;
            }
          }
        }
      }

      // If we get here, _compoundAction completed returned SUCCESS. So we can
      // can continue with our additional checks:
      if (nullptr != _visuallyVerifyAction) {
        ActionResult verificationResult = _visuallyVerifyAction->Update();
        if (verificationResult != ActionResult::SUCCESS) {
          return verificationResult;
        }
      }
      
      if(_headTrackWhenDone) {
        if( !_objectID.IsSet() ) {
          PRINT_NAMED_WARNING("TurnTowardsObjectAction.CustomObject.TrackingNotsupported",
                              "No valid object id (you probably specified a custom action), so can't track");
          // TODO:(bn) hang action here for consistency?
        }
        else {
          GetRobot().GetActionList().QueueAction(QueueActionPosition::NEXT,
                                             new TrackObjectAction(_objectID));
        }
      }
      return ActionResult::SUCCESS;
    } // TurnTowardsObjectAction::CheckIfDone()
    
    void TurnTowardsObjectAction::GetCompletionUnion(ActionCompletedUnion& completionUnion) const
    {
      ObjectInteractionCompleted info;
      info.numObjects = 1;
      info.objectIDs[0] = _objectID;
      completionUnion.Set_objectInteractionCompleted(std::move( info ));
    }
    
#pragma mark ---- TraverseObjectAction ----
    
    TraverseObjectAction::TraverseObjectAction(ObjectID objectID)
    : IActionRunner("TraverseObject",
                    RobotActionType::TRAVERSE_OBJECT,
                    (u8)AnimTrackFlag::BODY_TRACK)
    , _objectID(objectID)
    {
  
    }
    
    void TraverseObjectAction::SetSpeedAndAccel(f32 speed_mmps, f32 accel_mmps2) {
      _speed_mmps = speed_mmps;
      _accel_mmps2 = accel_mmps2;
    }
    
    ActionResult TraverseObjectAction::UpdateInternal()
    {
      // Select the chosen action based on the object's type, if we haven't
      // already
      if(_chosenAction == nullptr) {
        ActionableObject* object = dynamic_cast<ActionableObject*>(GetRobot().GetBlockWorld().GetLocatedObjectByID(_objectID));
        if(object == nullptr) {
          PRINT_NAMED_ERROR("TraverseObjectAction.UpdateInternal.ObjectNotFound",
                            "Could not get actionable object with ID = %d from world.", _objectID.GetValue());
          return ActionResult::BAD_OBJECT;
        }
        
        if(object->GetType() == ObjectType::Bridge_LONG ||
           object->GetType() == ObjectType::Bridge_SHORT)
        {
          CrossBridgeAction* bridgeAction = new CrossBridgeAction(_objectID);
          bridgeAction->SetSpeedAndAccel(_speed_mmps, _accel_mmps2, _decel_mmps2);
          bridgeAction->ShouldSuppressTrackLocking(true);
          _chosenAction.reset(bridgeAction);
          _chosenAction->SetRobot(&GetRobot());
        }
        else if(object->GetType() == ObjectType::Ramp_Basic) {
          AscendOrDescendRampAction* rampAction = new AscendOrDescendRampAction(_objectID);
          rampAction->SetSpeedAndAccel(_speed_mmps, _accel_mmps2, _decel_mmps2);
          rampAction->ShouldSuppressTrackLocking(true);
          _chosenAction.reset(rampAction);
          _chosenAction->SetRobot(&GetRobot());
        }
        else {
          PRINT_NAMED_ERROR("TraverseObjectAction.UpdateInternal.CannotTraverseObjectType",
                            "Robot %d was asked to traverse object ID=%d of type %s, but "
                            "that traversal is not defined.", GetRobot().GetID(),
                            object->GetID().GetValue(), ObjectTypeToString(object->GetType()));
          
          return ActionResult::BAD_OBJECT;
        }
      }
      
      // Now just use chosenAction's Update()
      assert(_chosenAction != nullptr);
      return _chosenAction->Update();
      
    } // Update()
    
#pragma mark ---- TurnTowardsPoseAction ----
    
    TurnTowardsPoseAction::TurnTowardsPoseAction(const Pose3d& pose, Radians maxTurnAngle)
    : PanAndTiltAction(0, 0, false, true)
    , _poseWrtRobot(pose)
    , _maxTurnAngle(maxTurnAngle.getAbsoluteVal())
    , _isPoseSet(true)
    {
      SetName("TurnTowardsPose");
      SetType(RobotActionType::TURN_TOWARDS_POSE);
    }
    
    TurnTowardsPoseAction::TurnTowardsPoseAction(Radians maxTurnAngle)
    : PanAndTiltAction(0, 0, false, true)
    , _maxTurnAngle(maxTurnAngle.getAbsoluteVal())
    , _isPoseSet(false)
    {
      
    }
    
    
    // Compute the required head angle to face the object
    // NOTE: It would be more accurate to take head tilt into account, but I'm
    //  just using neck joint height as an approximation for the camera's
    //  current height, since its actual height changes slightly as the head
    //  rotates around the neck.
    //  Also, the equation for computing the actual angle in closed form gets
    //  surprisingly nasty very quickly.
    Radians TurnTowardsPoseAction::GetAbsoluteHeadAngleToLookAtPose(const Point3f& translationWrtRobot)
    {
      const f32 heightDiff = translationWrtRobot.z() - NECK_JOINT_POSITION[2];
      const f32 distanceXY = Point2f(translationWrtRobot).Length() - NECK_JOINT_POSITION[0];
      
      // Adding bias to account for the fact that the camera tends to look lower than
      // desired on account of it being lower wrt neck joint.
      // Ramp bias down to 0 for distanceXY values from 150mm to 300mm.
      const f32 kFullBiasDist_mm = 150;
      const f32 kNoBiasDist_mm = 300;
      const f32 biasScaleFactorDist = CLIP((kNoBiasDist_mm - distanceXY) / (kNoBiasDist_mm - kFullBiasDist_mm), 0, 1);
      
      // Adding bias to account for the fact that we don't look high enough when turning towards objects off the ground
      // Apply full bias for object 10mm above neck joint and 0 for objects below neck joint
      const f32 kFullBiasHeight_mm = 10;
      const f32 kNoBiasHeight_mm = 0;
      const f32 biasScaleFactorHeight = CLIP((kNoBiasHeight_mm - heightDiff) / (kNoBiasHeight_mm - kFullBiasHeight_mm), 0, 1);
      
      // Adds 4 degrees to account for 4 degree lookdown on EP3
      const Radians headAngle = std::atan2(heightDiff, distanceXY) +
        (kHeadAngleDistBias_rad * biasScaleFactorDist) +
        (kHeadAngleHeightBias_rad * biasScaleFactorHeight) +
        DEG_TO_RAD(4);

      return headAngle;
    }

    Radians TurnTowardsPoseAction::GetRelativeBodyAngleToLookAtPose(const Point3f& translationWrtRobot)
    {
      return std::atan2(translationWrtRobot.y(),
                        translationWrtRobot.x());
    }
    
    void TurnTowardsPoseAction::SetPose(const Pose3d& pose)
    {
      _poseWrtRobot = pose;
      _isPoseSet = true;
    }
    
    ActionResult TurnTowardsPoseAction::Init()
    {
      // in case of re-run
      _nothingToDo = false;
      SetBodyPanAngle(0);

      if(!_isPoseSet) {
        PRINT_NAMED_ERROR("TurnTowardsPoseAction.Init.PoseNotSet", "");
        return ActionResult::BAD_POSE;
      }
      
      if(!_poseWrtRobot.HasParent()) {
        PRINT_CH_INFO("Actions", "TurnTowardsPoseAction.Init.AssumingRobotOriginAsParent", "");
        _poseWrtRobot.SetParent(GetRobot().GetWorldOrigin());
      }
      else if(false == _poseWrtRobot.GetWithRespectTo(GetRobot().GetPose(), _poseWrtRobot))
      {
        // TODO: It's possible this is just "normal" when dealing with delocalization, so possibly downgradable to Info later
        PRINT_NAMED_WARNING("TurnTowardsPoseAction.Init.PoseOriginFailure",
                            "Could not get pose (in frame %d) w.r.t. robot pose (in frame %d).",
                            _poseWrtRobot.FindRoot().GetID(),
                            GetRobot().GetPoseOriginList().GetCurrentOriginID());
        
        if(ANKI_DEVELOPER_CODE)
        {
          _poseWrtRobot.Print();
          _poseWrtRobot.PrintNamedPathToRoot(false);
          GetRobot().GetPose().PrintNamedPathToRoot(false);
        }
        return ActionResult::BAD_POSE;
      }
      
      if(_maxTurnAngle > 0)
      {
        // Compute the required angle to face the object
        const Radians turnAngle = GetRelativeBodyAngleToLookAtPose(_poseWrtRobot.GetTranslation());
        
        PRINT_CH_INFO("Actions", "TurnTowardsPoseAction.Init.TurnAngle",
                         "Computed turn angle = %.1fdeg", turnAngle.getDegrees());
        
        if(turnAngle.getAbsoluteVal() <= _maxTurnAngle) {
          SetBodyPanAngle(turnAngle);
        } else {
          PRINT_CH_INFO("Actions", "TurnTowardsPoseAction.Init.RequiredTurnTooLarge",
                           "Required turn angle of %.1fdeg is larger than max angle of %.1fdeg.",
                            turnAngle.getDegrees(), _maxTurnAngle.getDegrees());
          
          _nothingToDo = true;
          return ActionResult::SUCCESS;
        }
      }
      
      // Compute the required head angle to face the object
      Radians headAngle;
      const f32 kYTolFrac = 0.01f; // Fraction of image height
      Result result = GetRobot().ComputeHeadAngleToSeePose(_poseWrtRobot, headAngle, kYTolFrac);
      if(RESULT_OK != result)
      {
        PRINT_NAMED_WARNING("TurnTowardsPoseAction.Init.FailedToComputedHeadAngle",
                            "PoseWrtRobot translation=(%f,%f,%f)",
                            _poseWrtRobot.GetTranslation().x(),
                            _poseWrtRobot.GetTranslation().y(),
                            _poseWrtRobot.GetTranslation().z());
        
        // Fall back on old approximate method to compute head angle
        headAngle = GetAbsoluteHeadAngleToLookAtPose(_poseWrtRobot.GetTranslation());
      }
      
      headAngle = CLIP(headAngle, MIN_HEAD_ANGLE, MAX_HEAD_ANGLE);
      
      SetHeadTiltAngle(headAngle);
      
      // Proceed with base class's Init()
      return PanAndTiltAction::Init();
      
    } // TurnTowardsPoseAction::Init()
    
    ActionResult TurnTowardsPoseAction::CheckIfDone()
    {
      if(_nothingToDo) {
        return ActionResult::SUCCESS;
      } else {
        return PanAndTiltAction::CheckIfDone();
      }
    }
  
#pragma mark ---- TurnTowardsImagePointAction ----
    
    TurnTowardsImagePointAction::TurnTowardsImagePointAction(const Point2f& imgPoint, const TimeStamp_t t)
    : PanAndTiltAction(0, 0, true, true)
    , _imgPoint(imgPoint)
    , _timestamp(t)
    {
      SetName("TurnTowardsImagePointAction");
      SetType(RobotActionType::TURN_TOWARDS_IMAGE_POINT);
    }
    
    ActionResult TurnTowardsImagePointAction::Init()
    {
      Radians panAngle, tiltAngle;
      Result result = GetRobot().ComputeTurnTowardsImagePointAngles(_imgPoint, _timestamp, panAngle, tiltAngle);
      if(RESULT_OK != result)
      {
        PRINT_NAMED_WARNING("TurnTowardsImagePointAction.Init.ComputeTurnTowardsImagePointAnglesFailed",
                            "(%f,%f) at t=%u", _imgPoint.x(), _imgPoint.y(), _timestamp);
        return ActionResult::ABORT;
      }
      
      SetBodyPanAngle(panAngle);
      SetHeadTiltAngle(tiltAngle);
      
      return PanAndTiltAction::Init();
    }
    
#pragma mark ---- TurnTowardsFaceAction ----

    TurnTowardsFaceAction::TurnTowardsFaceAction(const SmartFaceID& faceID,
                                                 Radians maxTurnAngle,
                                                 bool sayName)
    : TurnTowardsPoseAction(maxTurnAngle)
    , _faceID(faceID)
    , _sayName(sayName)
    {
      SetName("TurnTowardsFace" + _faceID.GetDebugStr());
      SetType(RobotActionType::TURN_TOWARDS_FACE);
      SetTracksToLock((u8)AnimTrackFlag::NO_TRACKS);
    }

    
    void TurnTowardsFaceAction::SetAction(IActionRunner *action)
    {
      if(nullptr != _action) {
        _action->PrepForCompletion();
      }
      _action.reset(action);
      if(nullptr != _action) {
        _action->ShouldSuppressTrackLocking(true);
      }
      
      if(_action != nullptr && HasRobot()){
        _action->SetRobot(&GetRobot());
      }
    }
    
    TurnTowardsFaceAction::~TurnTowardsFaceAction()
    {
      SetAction(nullptr);
      
      // In case we got interrupted and didn't get a chance to do this
      if(HasRobot() && _tracksLocked) {
        GetRobot().GetMoveComponent().UnlockTracks((u8)AnimTrackFlag::HEAD_TRACK |
                                               (u8)AnimTrackFlag::BODY_TRACK,
                                               GetTag());
      }
    }
    
    void TurnTowardsFaceAction::OnRobotSetInternalPan()
    {
      if(_action != nullptr){
        _action->SetRobot(&GetRobot());
      }
    }

    void TurnTowardsFaceAction::SetSayNameAnimationTrigger(AnimationTrigger trigger)
    {
      if( ! _sayName ) {
        PRINT_NAMED_DEBUG("TurnTowardsFaceAction.SetSayNameTriggerWithoutSayingName",
                          "setting say name trigger, but we aren't going to say the name. This is useless");
      }
      _sayNameTriggerCallback = [trigger](const Robot& robot, const SmartFaceID& faceID) {
        return trigger;
      };
    }

  void TurnTowardsFaceAction::SetNoNameAnimationTrigger(AnimationTrigger trigger)
    {
      if( ! _sayName ) {
        PRINT_NAMED_DEBUG("TurnTowardsFaceAction.SetNoNameTriggerWithoutSayingName",
                          "setting anim trigger for unnamed faces, but we aren't going to say the name.");
      }
      _noNameTriggerCallback = [trigger](const Robot& robot, const SmartFaceID& faceID) {
        return trigger;
      };
    }

    void TurnTowardsFaceAction::SetSayNameTriggerCallback(AnimTriggerForFaceCallback callback)
    {
      if( ! _sayName ) {
        PRINT_NAMED_DEBUG("TurnTowardsFaceAction.SetSayNameTriggerCallbackWithoutSayingName",
                          "setting say name trigger callback, but we aren't going to say the name. This is useless");
      }
      _sayNameTriggerCallback = callback;
    }      

    void TurnTowardsFaceAction::SetNoNameTriggerCallback(AnimTriggerForFaceCallback callback)
    {
      if( ! _sayName ) {
        PRINT_NAMED_DEBUG("TurnTowardsFaceAction.SetNoNameTriggerCallbackWithoutSayingName",
                          "setting say name trigger callback, but we aren't going to say the name. This is useless");
      }
      _noNameTriggerCallback = callback;
    }      

    void TurnTowardsFaceAction::GetRequiredVisionModes(std::set<VisionModeRequest>& requests) const
    {
      requests.insert({ VisionMode::DetectingFaces, EVisionUpdateFrequency::Low });
    }

    ActionResult TurnTowardsFaceAction::Init()
    {
      // If we have a last observed face set, use its pose. Otherwise pose will not be set
      // so fail if we require a face, skip ahead if we don't
      Pose3d pose;
      bool gotPose = false;
      const bool kLastObservedFaceMustBeInRobotOrigin = false;
      
      if(_faceID.IsValid())
      {
        auto face = GetRobot().GetFaceWorld().GetFace(_faceID);
        if(nullptr != face && face->GetHeadPose().GetWithRespectTo(GetRobot().GetPose(), pose)) {
          gotPose = true;
        }
      }
      else if(GetRobot().GetFaceWorld().GetLastObservedFace(pose, kLastObservedFaceMustBeInRobotOrigin) != 0)
      {
        // Make w.r.t. robot pose, not robot _origin_
        const bool success = pose.GetWithRespectTo(GetRobot().GetPose(), pose);
        if(success) {
          gotPose = true;
        } else {
          PRINT_NAMED_WARNING("TurnTowardsFaceAction.Init.BadLastObservedFacePose",
                              "Could not get last observed face pose w.r.t. robot pose");
        }
      }
      
      if(gotPose)
      {
        TurnTowardsPoseAction::SetPose(pose);
        
        _action = nullptr;
        _obsFaceID.Reset();
        _closestDistSq = std::numeric_limits<f32>::max();
        
        if(GetRobot().HasExternalInterface())
        {
          using namespace ExternalInterface;
          auto helper = MakeAnkiEventUtil(*(GetRobot().GetExternalInterface()), *this, _signalHandles);
          helper.SubscribeEngineToGame<MessageEngineToGameTag::RobotObservedFace>();
        }
        
        _state = State::Turning;
        GetRobot().GetMoveComponent().LockTracks((u8)AnimTrackFlag::HEAD_TRACK |
                                             (u8)AnimTrackFlag::BODY_TRACK,
                                             GetTag(),
                                             GetName());
        _tracksLocked = true;
        
        return TurnTowardsPoseAction::Init();
      }
      else
      {
        if( _requireFaceConfirmation ) {
          PRINT_CH_INFO("Actions", "TurnTowardsFaceAction.Init.NoFacePose",
                        "Required face pose, don't have one, failing");
          return ActionResult::NO_FACE;
        }
        else {
          _state = State::SayingName; // jump to end and play animation (if present)
          return ActionResult::SUCCESS;
        }
      }
        
    } // Init()

    template<>
    void TurnTowardsFaceAction::HandleMessage(const ExternalInterface::RobotObservedFace& msg)
    {
      if(_state == State::Turning || _state == State::WaitingForFace)
      {
        Vision::FaceID_t faceID = msg.faceID;
        if(_faceID.IsValid())
        {
          // We know what face we're looking for. If this is it, set the observed face ID to it.
          if(_faceID.MatchesFaceID(faceID)) {
            _obsFaceID = _faceID;
          }
        }
        else
        {
          // We are looking for any face.
          // Record this face if it is closer than any we've seen so far
          const Vision::TrackedFace* face = GetRobot().GetFaceWorld().GetFace(faceID);
          if(nullptr != face) {
            Pose3d faceWrtRobot;
            if(true == face->GetHeadPose().GetWithRespectTo(GetRobot().GetPose(), faceWrtRobot)) {
              const f32 distSq = faceWrtRobot.GetTranslation().LengthSq();
              if(distSq < _closestDistSq) {
                GetRobot().GetFaceWorld().UpdateSmartFaceToID(faceID, _obsFaceID);
                _closestDistSq = distSq;
                PRINT_NAMED_DEBUG("TurnTowardsFaceAction.ObservedFaceCallback",
                                  "Observed ID=%s at distSq=%.1f",
                                  _obsFaceID.GetDebugStr().c_str(), _closestDistSq);
              }
            }
          }
        }
      }
    } // HandleMessage(RobotObservedFace)
        
    void TurnTowardsFaceAction::CreateFineTuneAction()
    {
      PRINT_NAMED_DEBUG("TurnTowardsFaceAction.CreateFinalAction.SawFace",
                        "Observed ID=%s. Will fine tune.", _obsFaceID.GetDebugStr().c_str());

      if(_obsFaceID.IsValid() ) {
        const Vision::TrackedFace* face = GetRobot().GetFaceWorld().GetFace(_obsFaceID);
        if( ANKI_VERIFY(nullptr != face,
                        "TurnTowardsFaceAction.FindTune.NullFace",
                        "id %s returned null",
                        _obsFaceID.GetDebugStr().c_str()) ) {
          // Valid face...        
          Pose3d pose;
          if(true == face->GetHeadPose().GetWithRespectTo(GetRobot().GetPose(), pose)) {
            GetRobot().GetMoodManager().TriggerEmotionEvent("LookAtFaceVerified", MoodManager::GetCurrentTimeInSeconds());
          
            // ... with valid pose w.r.t. robot. Turn towards that face -- iff it doesn't
            // require too large of an adjustment.
            const Radians maxFineTuneAngle( std::min( GetMaxTurnAngle().ToFloat(), DEG_TO_RAD(45.f)) );
            TurnTowardsPoseAction* ptr = new TurnTowardsPoseAction(pose, maxFineTuneAngle);
            // note: apply the pan/tilt angle tolerance to the fine-tune action
            ptr->SetTiltTolerance(GetHeadTiltAngleTolerance());
            ptr->SetPanTolerance(GetBodyPanAngleTolerance());
            SetAction(ptr);
          }
        } else {
          SetAction(nullptr);
        }
      }
      else {
        SetAction(nullptr);
      }
      
      _state = State::FineTuning;
    } // CreateFineTuneAction()
    
    
    ActionResult TurnTowardsFaceAction::CheckIfDone()
    {
      ActionResult result = ActionResult::RUNNING;
      
      switch(_state)
      {
        case State::Turning:
        {
          result = TurnTowardsPoseAction::CheckIfDone();
          if(ActionResult::RUNNING != result) {
            GetRobot().GetMoveComponent().UnlockTracks((u8)AnimTrackFlag::HEAD_TRACK |
                                                   (u8)AnimTrackFlag::BODY_TRACK,
                                                   GetTag());
            _tracksLocked = false;
          }
          
          if(ActionResult::SUCCESS == result)
          {
            // Initial (blind) turning to pose finished...
            if(!_obsFaceID.IsValid()) {
              // ...didn't see a face yet, wait a couple of images to see if we do
              PRINT_NAMED_DEBUG("TurnTowardsFaceAction.CheckIfDone.NoFaceObservedYet",
                                "Will wait no more than %d frames",
                                _maxFramesToWait);
              DEV_ASSERT(nullptr == _action, "TurnTowardsFaceAction.CheckIfDone.ActionPointerShouldStillBeNull");
              SetAction(new WaitForImagesAction(_maxFramesToWait, VisionMode::DetectingFaces));
              // TODO:(bn) parallel action with an animation here? This will let us span the gap a bit better
              // and buy us more time. Skipping for now
              _state = State::WaitingForFace;
            } else {
              // ...if we've already seen a face, jump straight to turning
              // towards that face and (optionally) saying name.
              CreateFineTuneAction(); // Moves to State:FineTuning
            }
            result = ActionResult::RUNNING;
          }
          
          break;
        }
          
        case State::WaitingForFace:
        {
          result = _action->Update();
          if(_obsFaceID.IsValid()) {
            // We saw a/the face. Turn towards it and (optionally) say name.
            CreateFineTuneAction(); // Moves to State:FineTuning
            result = ActionResult::RUNNING;
          }
          else if( result != ActionResult::RUNNING && _requireFaceConfirmation ) {
            // the wait action isn't running anymore, we didn't get a face, and we require a face. This is a
            // failure
            result = ActionResult::NO_FACE;
          }
          break;
        }
          
        case State::FineTuning:
        {
          if(nullptr == _action) {
            // No final action, just done.
            result = ActionResult::SUCCESS;
          } else {
            // Wait for final action of fine-tune turning to complete.
             // Create action to say name if enabled and we have a name by now.
            result = _action->Update();
            if(ActionResult::SUCCESS == result && _sayName)
            {
              const Vision::TrackedFace* face = GetRobot().GetFaceWorld().GetFace(_obsFaceID);
              if(nullptr != face) {
                if( face->GetName().empty() ) {
                  if( _noNameTriggerCallback ) {
                    AnimationTrigger noNameAnim = _noNameTriggerCallback(GetRobot(), _obsFaceID);
                    if( noNameAnim != AnimationTrigger::Count ) {
                      SetAction(new TriggerLiftSafeAnimationAction(noNameAnim));
                      _state = State::SayingName;
                      result = ActionResult::RUNNING;
                    }
                  }
                }
                else {
                  // we have a name
                  SayTextAction* sayText = new SayTextAction(face->GetName(), SayTextIntent::Name_Normal);
                  if( _sayNameTriggerCallback ) {
                    AnimationTrigger sayNameAnim = _sayNameTriggerCallback(GetRobot(), _obsFaceID);
                    if( sayNameAnim != AnimationTrigger::Count ) {
                      sayText->SetAnimationTrigger( sayNameAnim );
                    }
                  }
                  SetAction(sayText);
                  _state = State::SayingName;
                  result = ActionResult::RUNNING;
                }
              }
            }
          }
          break;
        }
          
        case State::SayingName:
        {
          if(nullptr == _action) {
            // No say name action, just done
            result = ActionResult::SUCCESS;
          } else {
            // Wait for say name action to finish
            result = _action->Update();
          }
            
          break;
        }
          
      } // switch(_state)

      if( ActionResult::SUCCESS == result && _obsFaceID.IsValid() ) {
        // tell face world that we have successfully turned towards this face
        GetRobot().GetFaceWorld().SetTurnedTowardsFace(_obsFaceID);
      }
      
      return result;
      
    } // TurnTowardsFaceAction::CheckIfDone()

#pragma mark ---- TurnTowardsFaceWrapperAction ----

    TurnTowardsFaceWrapperAction::TurnTowardsFaceWrapperAction(IActionRunner* action,
                                                               bool turnBeforeAction,
                                                               bool turnAfterAction,
                                                               Radians maxTurnAngle,
                                                               bool sayName)
    : CompoundActionSequential()
    {
      if( turnBeforeAction ) {
        AddAction( new TurnTowardsLastFacePoseAction(maxTurnAngle, sayName) );
      }
      AddAction(action);
      if( turnAfterAction ) {
        AddAction( new TurnTowardsLastFacePoseAction(maxTurnAngle, sayName) ) ;
      }
      
      // Use the action we're wrapping for the completion info and type
      SetProxyTag(action->GetTag());
    }  
    
#pragma mark ---- WaitAction ----
    
    WaitAction::WaitAction(f32 waitTimeInSeconds)
    : IAction("WaitSeconds",
              RobotActionType::WAIT,
              (u8)AnimTrackFlag::NO_TRACKS)
    , _waitTimeInSeconds(waitTimeInSeconds)
    , _doneTimeInSeconds(-1.f)
    {
      // Put the wait time with two decimals of precision in the action's name
      char tempBuffer[32];
      snprintf(tempBuffer, 32, "Wait%.2fSeconds", _waitTimeInSeconds);
      SetName(tempBuffer);
    }
    
    ActionResult WaitAction::Init()
    {
      _doneTimeInSeconds = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + _waitTimeInSeconds;
      return ActionResult::SUCCESS;
    }
    
    ActionResult WaitAction::CheckIfDone()
    {
      assert(_doneTimeInSeconds > 0.f);
      if(BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() > _doneTimeInSeconds) {
        return ActionResult::SUCCESS;
      } else {
        return ActionResult::RUNNING;
      }
    }
    
#pragma mark ---- WaitForImagesAction ----
    
    WaitForImagesAction::WaitForImagesAction(u32 numFrames, VisionMode visionMode, TimeStamp_t afterTimeStamp)
    : IAction("WaitFor" + std::to_string(numFrames) + "Images",
              RobotActionType::WAIT_FOR_IMAGES,
              (u8)AnimTrackFlag::NO_TRACKS)
    , _numFramesToWaitFor(numFrames)
    , _afterTimeStamp(afterTimeStamp)
    , _visionMode(visionMode)
    {
    
    }
    
    void WaitForImagesAction::GetRequiredVisionModes(std::set<VisionModeRequest>& requests) const 
    {
      // If the user has subscribed to VisionMode::Count, they are asking to be notified after N
      // vision processing frames, regardless of mode. This does not require any subscription to 
      // be made to the VSM since the RobotProcessImage message will be sent even if no modes are
      // currently enabled.
      if(_visionMode != VisionMode::Count){
        requests.insert({ _visionMode, EVisionUpdateFrequency::High });
      }
    }

    ActionResult WaitForImagesAction::Init()
    {
      _numModeFramesSeen = 0;
      
      auto imageProcLambda = [this](const AnkiEvent<ExternalInterface::MessageEngineToGame>& msg)
      {
        DEV_ASSERT(ExternalInterface::MessageEngineToGameTag::RobotProcessedImage == msg.GetData().GetTag(),
                   "WaitForImagesAction.MessageTypeNotHandled");
        const ExternalInterface::RobotProcessedImage& imageMsg = msg.GetData().Get_RobotProcessedImage();
        if (imageMsg.timestamp > _afterTimeStamp)
        {
          if (VisionMode::Count == _visionMode)
          {
            ++_numModeFramesSeen;
            PRINT_NAMED_DEBUG("WaitForImagesAction.Callback", "Frame %d of %d for any mode",
                              _numModeFramesSeen, _numFramesToWaitFor);
          }
          else
          {
            for (const auto& mode : imageMsg.visionModes)
            {
              if (mode == _visionMode)
              {
                ++_numModeFramesSeen;
                PRINT_NAMED_DEBUG("WaitForImagesAction.Callback", "Frame %d of %d for mode %s",
                                  _numModeFramesSeen, _numFramesToWaitFor, EnumToString(mode));
                break;
              }
            }
          }
        }
      };
      
      _imageProcSignalHandle = GetRobot().GetExternalInterface()->Subscribe(ExternalInterface::MessageEngineToGameTag::RobotProcessedImage, imageProcLambda);
      
      return ActionResult::SUCCESS;
    }
    
    ActionResult WaitForImagesAction::CheckIfDone()
    {
      if (_numModeFramesSeen < _numFramesToWaitFor)
      {
        return ActionResult::RUNNING;
      }
      
      // Reset the signalHandler to unsubscribe from the ProcessedImage message in case this action is not
      // immediatly destroyed after completion
      _imageProcSignalHandle.reset();
      return ActionResult::SUCCESS;
    }
    
#pragma mark ---- ReadToolCodeAction ----
    
    ReadToolCodeAction::ReadToolCodeAction(bool doCalibration)
    : IAction("ReadToolCode",
              RobotActionType::READ_TOOL_CODE,
              (u8)AnimTrackFlag::NO_TRACKS)
    , _doCalibration(doCalibration)
    , _headAndLiftDownAction()
    {
      _toolCodeInfo.code = ToolCode::UnknownTool;
    }
    
    ActionResult ReadToolCodeAction::Init()
    {
      PRINT_NAMED_ERROR("ReadToolCodeAction.Deprecated", 
                        "ReadToolCode functionality deprecated 1/31/18. See JIRA VIC-1189");
      return ActionResult::ABORT;

      // Put the head and lift down for read
      _headAndLiftDownAction.AddAction(new MoveHeadToAngleAction(MIN_HEAD_ANGLE));
      _headAndLiftDownAction.AddAction(new MoveLiftToHeightAction(LIFT_HEIGHT_LOWDOCK, READ_TOOL_CODE_LIFT_HEIGHT_TOL_MM));
      _headAndLiftDownAction.SetRobot(&GetRobot());

      _state = State::WaitingToGetInPosition;
      
      _toolReadSignalHandle = GetRobot().GetExternalInterface()->Subscribe(ExternalInterface::MessageEngineToGameTag::RobotReadToolCode,
         [this] (const AnkiEvent<ExternalInterface::MessageEngineToGame> &msg) {
           _toolCodeInfo = msg.GetData().Get_RobotReadToolCode().info;
           PRINT_CH_INFO("Actions", "ReadToolCodeAction.SignalHandler",
                            "Read tool code: %s", EnumToString(_toolCodeInfo.code));
           this->_state = State::ReadCompleted;
      });
      
      return ActionResult::SUCCESS;
    }
    
    ReadToolCodeAction::~ReadToolCodeAction()
    {
      _headAndLiftDownAction.PrepForCompletion();
      if(HasRobot()){
        // NOTE::STR 1/31/18 Removing call to (now) private VisionComponent scheduling fuctions
        // See jira VIC-1189 for details on current plans for ToolCode functionality
        // GetRobot().GetVisionComponent().EnableMode(VisionMode::ReadingToolCode, false);        
      }
    }
    
    ActionResult ReadToolCodeAction::CheckIfDone()
    {
      PRINT_NAMED_ERROR("ReadToolCodeAction.Deprecated", 
                        "ReadToolCode functionality deprecated 1/31/18. See JIRA VIC-1189");
      return ActionResult::ABORT;

      ActionResult result = ActionResult::RUNNING;
      
      switch(_state)
      {
        case State::WaitingToGetInPosition:
        {
          // Wait for head and lift to get into position (i.e. the action to complete)
          result = _headAndLiftDownAction.Update();
          if(ActionResult::SUCCESS == result)
          {
            result = ActionResult::RUNNING; // return value should still be running
            
            Result setCalibResult = GetRobot().GetVisionComponent().EnableToolCodeCalibration(_doCalibration);
            if(RESULT_OK != setCalibResult) {
              PRINT_CH_INFO("Actions", "ReadToolCodeAction.CheckIfDone.FailedToSetCalibration", "");
              result = ActionResult::FAILED_SETTING_CALIBRATION;
            } else {
              // Tell the VisionSystem thread to check the tool code in the next image it gets.
              // It will disable this mode when it completes.

              // NOTE::STR 1/31/18 Removing call to (now) private VisionComponent scheduling fuctions
              // GetRobot().GetVisionComponent().EnableMode(VisionMode::ReadingToolCode, true);
              _state = State::WaitingForRead;
            }
          }
          break;
        }
          
        case State::WaitingForRead:
          // Nothing to do
          break;
          
        case State::ReadCompleted:
          if(_toolCodeInfo.code == ToolCode::UnknownTool) {
            result = ActionResult::UNKNOWN_TOOL_CODE;
          } else {
            result = ActionResult::SUCCESS;
          }
          break;
      }
      
      return result;
    } // CheckIfDone()
    
    void ReadToolCodeAction::GetCompletionUnion(ActionCompletedUnion& completionUnion) const
    {
      ReadToolCodeCompleted toolCodeComplete;
      toolCodeComplete.info = _toolCodeInfo;
      completionUnion.Set_readToolCodeCompleted(std::move( toolCodeComplete ));
    }

  }
}

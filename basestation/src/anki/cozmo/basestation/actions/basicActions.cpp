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

#include "basicActions.h"

#include "anki/common/basestation/utils/timer.h"
#include "anki/cozmo/basestation/actions/dockActions.h"
#include "anki/cozmo/basestation/actions/driveToActions.h"
#include "anki/cozmo/basestation/actions/sayTextAction.h"
#include "anki/cozmo/basestation/actions/trackingActions.h"
#include "anki/cozmo/basestation/components/visionComponent.h"
#include "anki/cozmo/basestation/drivingAnimationHandler.h"
#include "anki/cozmo/basestation/externalInterface/externalInterface.h"
#include "anki/cozmo/basestation/ankiEventUtil.h"
#include "anki/cozmo/basestation/robot.h"

namespace Anki {
  
  namespace Cozmo {
    
    TurnInPlaceAction::TurnInPlaceAction(Robot& robot, const Radians& angle, const bool isAbsolute)
    : IAction(robot)
    , _targetAngle(angle)
    , _isAbsoluteAngle(isAbsolute)
    {
      
    }
    
    TurnInPlaceAction::~TurnInPlaceAction()
    {
      if(_moveEyes)
      {
        // Make sure eye shift gets removed no matter what
        if(AnimationStreamer::NotAnimatingTag != _eyeShiftTag) {
          _robot.GetAnimationStreamer().RemovePersistentFaceLayer(_eyeShiftTag);
          _eyeShiftTag = AnimationStreamer::NotAnimatingTag;
        }
        // Restore previous keep face alive setting
        if(_wasKeepFaceAliveEnabled) {
          _robot.GetAnimationStreamer().SetParam(LiveIdleAnimationParameter::EnableKeepFaceAlive, true);
        }
      }

      if( IsRunning() ) {
        // stop the robot turning if the action is destroyed while running
        _robot.GetMoveComponent().StopAllMotors();
      }
    }
    
    const std::string& TurnInPlaceAction::GetName() const
    {
      static const std::string name("TurnInPlaceAction");
      return name;
    }
    
    void TurnInPlaceAction::SetMaxSpeed(f32 maxSpeed_radPerSec)
    {
      if (std::fabsf(maxSpeed_radPerSec) > MAX_BODY_ROTATION_SPEED_RAD_PER_SEC) {
        PRINT_NAMED_WARNING("TurnInPlaceAction.SetMaxSpeed.SpeedExceedsLimit",
                            "Speed of %f deg/s exceeds limit of %f deg/s. Clamping.",
                            RAD_TO_DEG_F32(maxSpeed_radPerSec), MAX_BODY_ROTATION_SPEED_DEG_PER_SEC);
        _maxSpeed_radPerSec = std::copysign(MAX_BODY_ROTATION_SPEED_RAD_PER_SEC, maxSpeed_radPerSec);
      } else if (maxSpeed_radPerSec == 0) {
        _maxSpeed_radPerSec = _kDefaultSpeed;
      } else {
        _maxSpeed_radPerSec = maxSpeed_radPerSec;
      }
    }
    
    void TurnInPlaceAction::SetAccel(f32 accel_radPerSec2) {
      if (accel_radPerSec2 == 0) {
        _accel_radPerSec2 = _kDefaultAccel;
      } else {
        _accel_radPerSec2 = accel_radPerSec2;
      }
    }
    
    
    void TurnInPlaceAction::SetTolerance(const Radians& angleTol_rad)
    {
      _angleTolerance = angleTol_rad.getAbsoluteVal();
      
      // NOTE: can't be lower than what is used internally on the robot
      if( _angleTolerance.ToFloat() < POINT_TURN_ANGLE_TOL ) {
        PRINT_NAMED_WARNING("TurnInPlaceAction.InvalidTolerance",
                            "Tried to set tolerance of %fdeg, min is %f",
                            RAD_TO_DEG(_angleTolerance.ToFloat()),
                            RAD_TO_DEG(POINT_TURN_ANGLE_TOL));
        _angleTolerance = POINT_TURN_ANGLE_TOL;
      }
    }
    
    ActionResult TurnInPlaceAction::Init()
    {
      // Compute a goal pose rotated by specified angle around robot's
      // _current_ pose, taking into account the current driveCenter offset
      Radians heading = 0;
      if (!_isAbsoluteAngle) {
        heading = _robot.GetPose().GetRotationAngle<'Z'>();
      }
      
      Radians newAngle(heading);
      newAngle += _targetAngle;
      if(_variability != 0) {
        newAngle += GetRNG().RandDblInRange(-_variability.ToDouble(),
                                            _variability.ToDouble());
      }
      
      Pose3d rotatedPose;
      Pose3d dcPose = _robot.GetDriveCenterPose();
      dcPose.SetRotation(newAngle, Z_AXIS_3D());
      _robot.ComputeOriginPose(dcPose, rotatedPose);
      
      _targetAngle = rotatedPose.GetRotation().GetAngleAroundZaxis();
      
      Radians currentAngle;
      _inPosition = IsBodyInPosition(currentAngle);
      
      if(!_inPosition) {
        RobotInterface::SetBodyAngle setBodyAngle;
        setBodyAngle.angle_rad             = _targetAngle.ToFloat();
        setBodyAngle.max_speed_rad_per_sec = _maxSpeed_radPerSec;
        setBodyAngle.accel_rad_per_sec2    = _accel_radPerSec2;
        setBodyAngle.angle_tolerance       = _angleTolerance.ToFloat();
        if(RESULT_OK != _robot.SendRobotMessage<RobotInterface::SetBodyAngle>(std::move(setBodyAngle))) {
          return ActionResult::FAILURE_RETRY;
        }
        
        if(_moveEyes)
        {
          // Disable keep face alive if it is enabled and save so we can restore later
          _wasKeepFaceAliveEnabled = _robot.GetAnimationStreamer().GetParam<bool>(LiveIdleAnimationParameter::EnableKeepFaceAlive);
          if(_wasKeepFaceAliveEnabled) {
            _robot.GetAnimationStreamer().SetParam(LiveIdleAnimationParameter::EnableKeepFaceAlive, false);
          }
          
          // Store half the total difference so we know when to remove eye shift
          _halfAngle = 0.5f*(_targetAngle - currentAngle).getAbsoluteVal();
          
          // Move the eyes (only if not in position)
          // Note: assuming screen is about the same x distance from the neck joint as the head cam
          Radians angleDiff = _targetAngle - currentAngle;
          
          // Clip angleDiff to 89 degrees to prevent unintended behavior due to tangent
          bool angleClipped = false;
          if(angleDiff.getDegrees() > 89)
          {
            angleDiff = DEG_TO_RAD(89);
            angleClipped = true;
          }
          else if(angleDiff.getDegrees() < -89)
          {
            angleDiff = DEG_TO_RAD(-89);
            angleClipped = true;
          }
          
          f32 x_mm = std::tan(angleDiff.ToFloat()) * HEAD_CAM_POSITION[0];
          const f32 xPixShift = x_mm * (static_cast<f32>(ProceduralFace::WIDTH) / (4*SCREEN_SIZE[0]));
          _robot.ShiftEyes(_eyeShiftTag, xPixShift, 0, 4*IKeyFrame::SAMPLE_LENGTH_MS, "TurnInPlaceEyeDart");
        }
      }
      
      return ActionResult::SUCCESS;
    }
    
    bool TurnInPlaceAction::IsBodyInPosition(Radians& currentAngle) const
    {
      currentAngle = _robot.GetPose().GetRotation().GetAngleAroundZaxis();
      const bool inPosition = NEAR(currentAngle-_targetAngle, 0.f, _angleTolerance);
      return inPosition;
    }
    
    ActionResult TurnInPlaceAction::CheckIfDone()
    {
      ActionResult result = ActionResult::RUNNING;
      
      Radians currentAngle;
      
      if(!_inPosition) {
        _inPosition = IsBodyInPosition(currentAngle);
      }
      
      // When we've turned at least halfway, remove eye dart
      if(AnimationStreamer::NotAnimatingTag != _eyeShiftTag) {
        if(_inPosition || NEAR(currentAngle-_targetAngle, 0.f, _halfAngle))
        {
          PRINT_NAMED_INFO("TurnInPlaceAction.CheckIfDone.RemovingEyeShift",
                           "Currently at %.1fdeg, on the way to %.1fdeg, within "
                           "half angle of %.1fdeg", currentAngle.getDegrees(),
                           _targetAngle.getDegrees(), _halfAngle.getDegrees());
          _robot.GetAnimationStreamer().RemovePersistentFaceLayer(_eyeShiftTag, 3*IKeyFrame::SAMPLE_LENGTH_MS);
          _eyeShiftTag = AnimationStreamer::NotAnimatingTag;
        }
      }

      if( _robot.GetMoveComponent().AreWheelsMoving()) {
        _turnStarted = true;
      }
      
      // Wait to get a state message back from the physical robot saying its body
      // is in the commanded position
      // TODO: Is this really necessary in practice?
      if(_inPosition) {
        result = _robot.GetMoveComponent().AreWheelsMoving() ? ActionResult::RUNNING : ActionResult::SUCCESS;
      } else {
        PRINT_NAMED_INFO("TurnInPlaceAction.CheckIfDone",
                         "[%d] Waiting for body to reach angle: %.1fdeg vs. %.1fdeg(+/-%.1f) (tol: %f) (pfid: %d)",
                         GetTag(),
                         currentAngle.getDegrees(),
                         _targetAngle.getDegrees(),
                         _variability.getDegrees(),
                         _angleTolerance.ToFloat(),
                         _robot.GetPoseFrameID());
        

        if( _turnStarted && !_robot.GetMoveComponent().AreWheelsMoving()) {
          PRINT_NAMED_WARNING("TurnInPlaceAction.StoppedMakingProgress",
                              "[%d] giving up since we stopped moving",
                              GetTag());
          result = ActionResult::FAILURE_RETRY;
        }
      }
      
      return result;
    }

#pragma mark ---- SearchSideToSideAction ----

    SearchSideToSideAction::SearchSideToSideAction(Robot& robot)
      : IAction(robot)
      , _compoundAction(robot)
    {
    }

    SearchSideToSideAction::~SearchSideToSideAction()
    {
      if( _shouldPopIdle ) {
        _robot.GetAnimationStreamer().PopIdleAnimation();
        _shouldPopIdle = false;
      }
      _compoundAction.PrepForCompletion();
    }
  
    void SearchSideToSideAction::SetSearchAngle(f32 minSearchAngle_rads, f32 maxSearchAngle_rads)
    {
      _minSearchAngle_rads = minSearchAngle_rads;
      _maxSearchAngle_rads = maxSearchAngle_rads;
    }
  
    void SearchSideToSideAction::SetSearchWaitTime(f32 minWaitTime_s, f32 maxWaitTime_s)
    {
      _minWaitTime_s = minWaitTime_s;
      _maxWaitTime_s = maxWaitTime_s;
    }

    ActionResult SearchSideToSideAction::Init()
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

      PRINT_NAMED_DEBUG("SearchSideToSideAction.Init",
                        "Action will wait %f, turn %fdeg, wait %f, turn %fdeg, wait %f",
                        initialWait_s,
                        RAD_TO_DEG(firstAngle_rads),
                        afterFirstTurnWait_s,
                        RAD_TO_DEG(secondAngle_rads),
                        afterSecondTurnWait_s);

      _compoundAction.AddAction(new WaitAction(_robot, initialWait_s));

      TurnInPlaceAction* turn0 = new TurnInPlaceAction(_robot, firstAngle_rads, false);
      turn0->SetTolerance(DEG_TO_RAD(4.0f));
      _compoundAction.AddAction(turn0);
      
      _compoundAction.AddAction(new WaitAction(_robot, afterFirstTurnWait_s));

      TurnInPlaceAction* turn1 = new TurnInPlaceAction(_robot, secondAngle_rads, false);
      turn1->SetTolerance(DEG_TO_RAD(4.0f));
      _compoundAction.AddAction(turn1);

      _compoundAction.AddAction(new WaitAction(_robot, afterSecondTurnWait_s));

      // Prevent the compound action from signaling completion
      _compoundAction.ShouldEmitCompletionSignal(false);
      
      // Prevent the compound action from locking tracks (the PanAndTiltAction handles it itself)
      _compoundAction.ShouldSuppressTrackLocking(true);

      // disable the live idle animation, so we aren't moving during the "wait" sections
      if( ! _shouldPopIdle ) {
        _shouldPopIdle = true;
        _robot.GetAnimationStreamer().PushIdleAnimation("NONE");
      }

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
    }

    ActionResult SearchSideToSideAction::CheckIfDone()
    {
      return _compoundAction.Update();
    }
  
#pragma mark ---- DriveStraightAction ----
    
    DriveStraightAction::DriveStraightAction(Robot& robot, f32 dist_mm, f32 speed_mmps)
    : IAction(robot)
    , _dist_mm(dist_mm)
    , _speed_mmps(speed_mmps)
    {
      if(_speed_mmps < 0.f) {
        PRINT_NAMED_WARNING("DriveStraightAction.Constructor.NegativeSpeed",
                            "Speed should always be positive (not %f). Making positive.",
                            _speed_mmps);
        _speed_mmps = -_speed_mmps;
      }
      
      if(dist_mm < 0.f) {
        // If distance is negative, we are driving backward and will negate speed
        // internally. Yes, we could have just double-negated if the caller passed in
        // a negative speed already, but this avoids confusion on caller's side about
        // which signs to use and the documentation says speed should always be positive.
        ASSERT_NAMED(_speed_mmps >= 0.f, "DriveStraightAction.Constructor.NegativeSpeed");
        _speed_mmps = -_speed_mmps;
      }
    }

    DriveStraightAction::~DriveStraightAction()
    {
      _robot.AbortDrivingToPose();
      _robot.GetContext()->GetVizManager()->ErasePath(_robot.GetID());

      _robot.GetDrivingAnimationHandler().ActionIsBeingDestroyed();
    }
  
    ActionResult DriveStraightAction::Init()
    {
      if(_dist_mm == 0.f) {
        // special case
        _hasStarted = true;
        return ActionResult::SUCCESS;
      }
      
      const Radians heading = _robot.GetPose().GetRotation().GetAngleAroundZaxis();
      
      const Vec3f& T = _robot.GetDriveCenterPose().GetTranslation();
      const f32 x_start = T.x();
      const f32 y_start = T.y();
      
      const f32 x_end = x_start + _dist_mm * std::cos(heading.ToFloat());
      const f32 y_end = y_start + _dist_mm * std::sin(heading.ToFloat());
      
      Planning::Path path;
      // TODO: does matID matter? I'm just using 0 below
      if(false  == path.AppendLine(0, x_start, y_start, x_end, y_end,
                                   _speed_mmps, _accel_mmps2, _decel_mmps2))
      {
        PRINT_NAMED_ERROR("DriveStraightAction.Init.AppendLineFailed", "");
        return ActionResult::FAILURE_ABORT;
      }
      
      _name = ("DriveStraight" + std::to_string(_dist_mm) + "mm@" +
               std::to_string(_speed_mmps) + "mmpsAction");
      
      _hasStarted = false;
      
      // Tell robot to execute this simple path
      if(RESULT_OK != _robot.ExecutePath(path, false)) {
        return ActionResult::FAILURE_ABORT;
      }
      
      return ActionResult::SUCCESS;
    }
    
    ActionResult DriveStraightAction::CheckIfDone()
    {
      ActionResult result = ActionResult::RUNNING;

      if(_robot.GetDrivingAnimationHandler().IsPlayingEndAnim())
      {
        return ActionResult::RUNNING;
      }
      else if ( _hasStarted && !_robot.IsTraversingPath() ) {
        result = ActionResult::SUCCESS;;
      }
      
      if(!_hasStarted) {
        PRINT_NAMED_INFO("DriveStraightAction.CheckIfDone.WaitingForPathStart", "");
        _hasStarted = _robot.IsTraversingPath();
        if( _hasStarted && _shouldPlayDrivingAnimation) {
          _robot.GetDrivingAnimationHandler().PlayStartAnim(GetTracksToLock());
        }
      } else if(/*hasStarted AND*/ !_robot.IsTraversingPath() && _shouldPlayDrivingAnimation) {
        if( _robot.GetDrivingAnimationHandler().PlayEndAnim()) {
          return ActionResult::RUNNING;
        }
        else {
          result = ActionResult::SUCCESS;
        }
      }
      
      return result;
    }
    
#pragma mark ---- MoveHeadToAngleAction ----
    
    MoveHeadToAngleAction::MoveHeadToAngleAction(Robot& robot, const Radians& headAngle, const Radians& tolerance, const Radians& variability)
    : IAction(robot)
    , _headAngle(headAngle)
    , _angleTolerance(tolerance)
    , _variability(variability)
    , _name("MoveHeadTo" + std::to_string(RAD_TO_DEG(_headAngle.ToFloat())) + "DegAction")
    , _inPosition(false)
    {
      if(_headAngle < MIN_HEAD_ANGLE) {
        PRINT_NAMED_WARNING("MoveHeadToAngleAction.Constructor",
                            "Requested head angle (%.1fdeg) less than min head angle (%.1fdeg). Clipping.",
                            _headAngle.getDegrees(), RAD_TO_DEG(MIN_HEAD_ANGLE));
        _headAngle = MIN_HEAD_ANGLE;
      } else if(_headAngle > MAX_HEAD_ANGLE) {
        PRINT_NAMED_WARNING("MoveHeadToAngleAction.Constructor",
                            "Requested head angle (%.1fdeg) more than max head angle (%.1fdeg). Clipping.",
                            _headAngle.getDegrees(), RAD_TO_DEG(MAX_HEAD_ANGLE));
        _headAngle = MAX_HEAD_ANGLE;
      }
      
      if( _angleTolerance.ToFloat() < HEAD_ANGLE_TOL ) {
        PRINT_NAMED_WARNING("MoveHeadToAngleAction.InvalidTolerance",
                            "Tried to set tolerance of %fdeg, min is %f",
                            RAD_TO_DEG(_angleTolerance.ToFloat()),
                            RAD_TO_DEG(HEAD_ANGLE_TOL));
        _angleTolerance = HEAD_ANGLE_TOL;
      }
      
      if(_variability > 0) {
        _headAngle += GetRNG().RandDblInRange(-_variability.ToDouble(), _variability.ToDouble());
        _headAngle = CLIP(_headAngle, MIN_HEAD_ANGLE, MAX_HEAD_ANGLE);
      }
    }
    
    MoveHeadToAngleAction::~MoveHeadToAngleAction()
    {
      if(AnimationStreamer::NotAnimatingTag != _eyeShiftTag)
      {
        // Make sure eye shift gets removed, by this action, or by the MoveComponent if "hold" is enabled
        if(_holdEyes) {
          _robot.GetMoveComponent().RemoveFaceLayerWhenHeadMoves(_eyeShiftTag, 3*IKeyFrame::SAMPLE_LENGTH_MS);
        } else {
          _robot.GetAnimationStreamer().RemovePersistentFaceLayer(_eyeShiftTag);
        }
        _eyeShiftTag = AnimationStreamer::NotAnimatingTag;
      }
      if(_moveEyes) {
        // Restore previous keep face alive setting
        if(_wasKeepFaceAliveEnabled) {
          _robot.GetAnimationStreamer().SetParam(LiveIdleAnimationParameter::EnableKeepFaceAlive, true);
        }
      }
    }
    
    bool MoveHeadToAngleAction::IsHeadInPosition() const
    {
      const bool inPosition = NEAR(Radians(_robot.GetHeadAngle()) - _headAngle, 0.f, _angleTolerance);
      
      return inPosition;
    }
    
    ActionResult MoveHeadToAngleAction::Init()
    {
      ActionResult result = ActionResult::SUCCESS;
      
      _inPosition = IsHeadInPosition();
      
      if(!_inPosition) {
        if(RESULT_OK != _robot.GetMoveComponent().MoveHeadToAngle(_headAngle.ToFloat(),
                                                                   _maxSpeed_radPerSec,
                                                                   _accel_radPerSec2,
                                                                   _duration_sec))
        {
          result = ActionResult::FAILURE_ABORT;
        }
        
        if(_moveEyes)
        {
          // Store initial state of keep face alive so we can restore it
          _wasKeepFaceAliveEnabled = _robot.GetAnimationStreamer().GetParam<bool>(LiveIdleAnimationParameter::EnableKeepFaceAlive);
          if(_wasKeepFaceAliveEnabled) {
            _robot.GetAnimationStreamer().SetParam(LiveIdleAnimationParameter::EnableKeepFaceAlive, false);
          }
          
          // Lead with the eyes, if not in position
          // Note: assuming screen is about the same x distance from the neck joint as the head cam
          Radians angleDiff =  _robot.GetHeadAngle() - _headAngle;
          const f32 y_mm = std::tan(angleDiff.ToFloat()) * HEAD_CAM_POSITION[0];
          const f32 yPixShift = y_mm * (static_cast<f32>(ProceduralFace::HEIGHT/4) / SCREEN_SIZE[1]);
          
          _robot.ShiftEyes(_eyeShiftTag, 0, yPixShift, 4*IKeyFrame::SAMPLE_LENGTH_MS, "MoveHeadToAngleEyeShift");
          
          if(!_holdEyes) {
            // Store the half the angle differene so we know when to remove eye shift
            _halfAngle = 0.5f*(_headAngle - _robot.GetHeadAngle()).getAbsoluteVal();
          }
        }
      }
      
      return result;
    }
    
    ActionResult MoveHeadToAngleAction::CheckIfDone()
    {
      ActionResult result = ActionResult::RUNNING;
      
      if(!_inPosition) {
        _inPosition = IsHeadInPosition();
      }
      
      if(!_holdEyes && AnimationStreamer::NotAnimatingTag != _eyeShiftTag)
      {
        // If we're not there yet but at least halfway, and we're not supposed
        // to "hold" the eyes, then remove eye shift
        if(_inPosition || NEAR(Radians(_robot.GetHeadAngle()) - _headAngle, 0.f, _halfAngle))
        {
          PRINT_NAMED_INFO("MoveHeadToAngleAction.CheckIfDone.RemovingEyeShift",
                           "[%d] Currently at %.1fdeg, on the way to %.1fdeg, within "
                           "half angle of %.1fdeg",
                           GetTag(),
                           RAD_TO_DEG(_robot.GetHeadAngle()),
                           _headAngle.getDegrees(),
                           _halfAngle.getDegrees());
          
          _robot.GetAnimationStreamer().RemovePersistentFaceLayer(_eyeShiftTag, 3*IKeyFrame::SAMPLE_LENGTH_MS);
          _eyeShiftTag = AnimationStreamer::NotAnimatingTag;
        }
      }
      
      if( _robot.GetMoveComponent().IsHeadMoving() ) {
        _motionStarted = true;
      }
      
      // Wait to get a state message back from the physical robot saying its head
      // is in the commanded position
      // TODO: Is this really necessary in practice?
      if(_inPosition) {
        result = _robot.GetMoveComponent().IsHeadMoving() ? ActionResult::RUNNING : ActionResult::SUCCESS;
      } else {
        PRINT_NAMED_INFO("MoveHeadToAngleAction.CheckIfDone",
                         "[%d] Waiting for head to get in position: %.1fdeg vs. %.1fdeg(+/-%.1f)",
                         GetTag(),
                         RAD_TO_DEG(_robot.GetHeadAngle()), _headAngle.getDegrees(), _variability.getDegrees());
        
        if( _motionStarted && ! _robot.GetMoveComponent().IsHeadMoving() ) {
          PRINT_NAMED_WARNING("MoveHeadToAngleAction.StoppedMakingProgress",
                              "[%d] giving up since we stopped moving",
                              GetTag());
          result = ActionResult::FAILURE_RETRY;
        }
      }
      
      return result;
    }
    
#pragma mark ---- MoveLiftToHeightAction ----
    
    MoveLiftToHeightAction::MoveLiftToHeightAction(Robot& robot, const f32 height_mm, const f32 tolerance_mm, const f32 variability)
    : IAction(robot)
    , _height_mm(height_mm)
    , _heightTolerance(tolerance_mm)
    , _variability(variability)
    , _name("MoveLiftTo" + std::to_string(_height_mm) + "mmAction")
    , _inPosition(false)
    {
      
    }
    
    MoveLiftToHeightAction::MoveLiftToHeightAction(Robot& robot, const Preset preset, const f32 tolerance_mm)
    : MoveLiftToHeightAction(robot, GetPresetHeight(preset), tolerance_mm, 0.f)
    {
      _name = "MoveLiftTo";
      _name += GetPresetName(preset);
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
      const bool inPosition = (NEAR(_heightWithVariation, _robot.GetLiftHeight(), _heightTolerance) &&
                               !_robot.GetMoveComponent().IsLiftMoving());
      
      return inPosition;
    }
    
    ActionResult MoveLiftToHeightAction::Init()
    {
      ActionResult result = ActionResult::SUCCESS;
      
      if (_height_mm >= 0 && (_height_mm < LIFT_HEIGHT_LOWDOCK || _height_mm > LIFT_HEIGHT_CARRY)) {
        PRINT_NAMED_WARNING("MoveLiftToHeightAction.Init.InvalidHeight",
                            "%f mm. Clipping to be in range.", _height_mm);
        _height_mm = CLIP(_height_mm, LIFT_HEIGHT_LOWDOCK, LIFT_HEIGHT_CARRY);
      }
      
      if(_height_mm < 0.f) {
        // Choose whatever is closer to current height, LOW or CARRY:
        const f32 currentHeight = _robot.GetLiftHeight();
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
      f32 targetAngle = Robot::ConvertLiftHeightToLiftAngleRad(_heightWithVariation);
      f32 targetAngleLower = Robot::ConvertLiftHeightToLiftAngleRad(heightLower);
      f32 targetAngleUpper = Robot::ConvertLiftHeightToLiftAngleRad(heightUpper);
      
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
        f32 desiredHeightLower = Robot::ConvertLiftAngleToLiftHeightMM(targetAngle - LIFT_ANGLE_TOL);
        f32 desiredHeightUpper = Robot::ConvertLiftAngleToLiftHeightMM(targetAngle + LIFT_ANGLE_TOL);
        f32 newHeightTolerance = std::max(_height_mm - desiredHeightLower, desiredHeightUpper - _height_mm);
        
        PRINT_NAMED_WARNING("MoveLiftToHeightAction.Init.TolTooSmall",
                            "HeightTol %f mm == AngleTol %f rad near height of %f mm. Clipping tol to %f mm",
                            _heightTolerance, minAngleDiff, _heightWithVariation, newHeightTolerance);
        _heightTolerance = newHeightTolerance;
      }
      
      _inPosition = IsLiftInPosition();
      
      if(!_inPosition) {
        if(_robot.GetMoveComponent().MoveLiftToHeight(_heightWithVariation,
                                                       _maxLiftSpeedRadPerSec,
                                                       _liftAccelRacPerSec2,
                                                       _duration) != RESULT_OK) {
          result = ActionResult::FAILURE_ABORT;
        }
      }
      
      return result;
    }
    
    ActionResult MoveLiftToHeightAction::CheckIfDone()
    {
      ActionResult result = ActionResult::RUNNING;
      
      if(!_inPosition) {
        _inPosition = IsLiftInPosition();
      }
      
      // TODO: Somehow verify robot got command to move lift before declaring success
      /*
       // Wait for the lift to start moving (meaning robot received command) and
       // then stop moving
       static bool liftStartedMoving = false;
       if(!liftStartedMoving) {
       liftStartedMoving = _robot.IsLiftMoving();
       }
       else
       */
      
      if( _robot.GetMoveComponent().IsLiftMoving() ) {
        _motionStarted = true;
      }
      
      if(_inPosition) {
        result = _robot.GetMoveComponent().IsLiftMoving() ? ActionResult::RUNNING : ActionResult::SUCCESS;
      } else {
        PRINT_NAMED_INFO("MoveLiftToHeightAction.CheckIfDone",
                         "[%d] Waiting for lift to get in position: %.1fmm vs. %.1fmm (tol: %f)",
                         GetTag(),
                         _robot.GetLiftHeight(), _heightWithVariation, _heightTolerance);
        
        if( _motionStarted && ! _robot.GetMoveComponent().IsLiftMoving() ) {
          PRINT_NAMED_WARNING("MoveLiftToHeightAction.StoppedMakingProgress",
                              "[%d] giving up since we stopped moving",
                              GetTag());
          result = ActionResult::FAILURE_RETRY;
        }
      }
      
      return result;
    }
    
#pragma mark ---- PanAndTiltAction ----
    
    PanAndTiltAction::PanAndTiltAction(Robot& robot, Radians bodyPan, Radians headTilt,
                                       bool isPanAbsolute, bool isTiltAbsolute)
    : IAction(robot)
    , _compoundAction(robot)
    , _bodyPanAngle(bodyPan)
    , _headTiltAngle(headTilt)
    , _isPanAbsolute(isPanAbsolute)
    , _isTiltAbsolute(isTiltAbsolute)
    {

    }
    
    PanAndTiltAction::~PanAndTiltAction()
    {
      _compoundAction.PrepForCompletion();
    }
    
    void PanAndTiltAction::SetMaxPanSpeed(f32 maxSpeed_radPerSec)
    {
      if (maxSpeed_radPerSec == 0.f) {
        _maxPanSpeed_radPerSec = _kDefaultMaxPanSpeed;
      } else if (std::fabsf(maxSpeed_radPerSec) > MAX_BODY_ROTATION_SPEED_RAD_PER_SEC) {
        PRINT_NAMED_WARNING("PanAndTiltAction.SetMaxSpeed.PanSpeedExceedsLimit",
                            "Speed of %f deg/s exceeds limit of %f deg/s. Clamping.",
                            RAD_TO_DEG_F32(maxSpeed_radPerSec), MAX_BODY_ROTATION_SPEED_DEG_PER_SEC);
        _maxPanSpeed_radPerSec = std::copysign(MAX_BODY_ROTATION_SPEED_RAD_PER_SEC, maxSpeed_radPerSec);
      } else {
        _maxPanSpeed_radPerSec = maxSpeed_radPerSec;
      }
    }
    
    void PanAndTiltAction::SetPanAccel(f32 accel_radPerSec2)
    {
      // If 0, use default value
      if (accel_radPerSec2 == 0.f) {
        _panAccel_radPerSec2 = _kDefaultPanAccel;
      } else {
        _panAccel_radPerSec2 = accel_radPerSec2;
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
        PRINT_NAMED_WARNING("PanAndTiltAction.InvalidTolerance",
                            "Tried to set tolerance of %fdeg, min is %f",
                            RAD_TO_DEG(_panAngleTol.ToFloat()),
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
      }
    }
    
    void PanAndTiltAction::SetTiltAccel(f32 accel_radPerSec2)
    {
      if (accel_radPerSec2 == 0.f) {
        _tiltAccel_radPerSec2 = _kDefaultTiltAccel;
      } else {
        _tiltAccel_radPerSec2 = accel_radPerSec2;
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
        PRINT_NAMED_WARNING("PanAndTiltAction.InvalidTolerance",
                            "Tried to set tolerance of %fdeg, min is %f",
                            RAD_TO_DEG(_tiltAngleTol.ToFloat()),
                            RAD_TO_DEG(HEAD_ANGLE_TOL));
        _tiltAngleTol = HEAD_ANGLE_TOL;
      }
    }
    
    ActionResult PanAndTiltAction::Init()
    {
      // Incase we are re-running this action
      _compoundAction.ClearActions();
      _compoundAction.EnableMessageDisplay(IsMessageDisplayEnabled());
      
      TurnInPlaceAction* action = new TurnInPlaceAction(_robot, _bodyPanAngle, _isPanAbsolute);
      action->SetTolerance(_panAngleTol);
      action->SetMaxSpeed(_maxPanSpeed_radPerSec);
      action->SetAccel(_panAccel_radPerSec2);
      action->SetMoveEyes(_moveEyes);
      _compoundAction.AddAction(action);
      
      const Radians newHeadAngle = _isTiltAbsolute ? _headTiltAngle : _robot.GetHeadAngle() + _headTiltAngle;
      MoveHeadToAngleAction* headAction = new MoveHeadToAngleAction(_robot, newHeadAngle, _tiltAngleTol);
      headAction->SetMaxSpeed(_maxTiltSpeed_radPerSec);
      headAction->SetAccel(_tiltAccel_radPerSec2);
      headAction->SetMoveEyes(_moveEyes);
      _compoundAction.AddAction(headAction);
      
      // Put the angles in the name for debugging
      _name = ("Pan" + std::to_string(std::round(_bodyPanAngle.getDegrees())) +
               "AndTilt" + std::to_string(std::round(_headTiltAngle.getDegrees())) +
               "Action");
      
      // Prevent the compound action from signaling completion
      _compoundAction.ShouldEmitCompletionSignal(false);
      
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
    
    TurnTowardsObjectAction::TurnTowardsObjectAction(Robot& robot,
                                       ObjectID objectID,
                                       Radians maxTurnAngle,
                                       bool visuallyVerifyWhenDone,
                                       bool headTrackWhenDone)
    : TurnTowardsObjectAction(robot, objectID, Vision::Marker::ANY_CODE,
                       maxTurnAngle, visuallyVerifyWhenDone, headTrackWhenDone)
    {
      
    }
    
    TurnTowardsObjectAction::TurnTowardsObjectAction(Robot& robot,
                                       ObjectID objectID,
                                       Vision::Marker::Code whichCode,
                                       Radians maxTurnAngle,
                                       bool visuallyVerifyWhenDone,
                                       bool headTrackWhenDone)
    : TurnTowardsPoseAction(robot, maxTurnAngle)
    , _facePoseCompoundActionDone(false)
    , _visuallyVerifyAction(robot, objectID, whichCode)
    , _objectID(objectID)
    , _whichCode(whichCode)
    , _visuallyVerifyWhenDone(visuallyVerifyWhenDone)
    , _headTrackWhenDone(headTrackWhenDone)
    {
      
    }
    
    TurnTowardsObjectAction::~TurnTowardsObjectAction()
    {
      _visuallyVerifyAction.PrepForCompletion();
    }
    
    ActionResult TurnTowardsObjectAction::Init()
    {
      ObservableObject* object = _robot.GetBlockWorld().GetObjectByID(_objectID);
      if(object == nullptr) {
        PRINT_NAMED_ERROR("TurnTowardsObjectAction.Init.ObjectNotFound",
                          "Object with ID=%d no longer exists in the world.",
                          _objectID.GetValue());
        return ActionResult::FAILURE_ABORT;
      }
      
      Pose3d objectPoseWrtRobot;
      if(_whichCode == Vision::Marker::ANY_CODE) {
        if(false == object->GetPose().GetWithRespectTo(_robot.GetPose(), objectPoseWrtRobot)) {
          PRINT_NAMED_ERROR("TurnTowardsObjectAction.Init.ObjectPoseOriginProblem",
                            "Could not get pose of object %d w.r.t. robot pose.",
                            _objectID.GetValue());
          return ActionResult::FAILURE_ABORT;
        }
      } else {
        // Use the closest marker with the specified code:
        std::vector<Vision::KnownMarker*> const& markers = object->GetMarkersWithCode(_whichCode);
        
        if(markers.empty()) {
          PRINT_NAMED_ERROR("TurnTowardsObjectAction.Init.NoMarkersWithCode",
                            "Object %d does not have any markers with code %d.",
                            _objectID.GetValue(), _whichCode);
          return ActionResult::FAILURE_ABORT;
        }
        
        Vision::KnownMarker* closestMarker = nullptr;
        if(markers.size() == 1) {
          closestMarker = markers.front();
          if(false == closestMarker->GetPose().GetWithRespectTo(_robot.GetPose(), objectPoseWrtRobot)) {
            PRINT_NAMED_ERROR("TurnTowardsObjectAction.Init.MarkerOriginProblem",
                              "Could not get pose of marker with code %d of object %d "
                              "w.r.t. robot pose.", _whichCode, _objectID.GetValue() );
            return ActionResult::FAILURE_ABORT;
          }
        } else {
          f32 closestDist = std::numeric_limits<f32>::max();
          Pose3d markerPoseWrtRobot;
          for(auto marker : markers) {
            if(false == marker->GetPose().GetWithRespectTo(_robot.GetPose(), markerPoseWrtRobot)) {
              PRINT_NAMED_ERROR("TurnTowardsObjectAction.Init.MarkerOriginProblem",
                                "Could not get pose of marker with code %d of object %d "
                                "w.r.t. robot pose.", _whichCode, _objectID.GetValue() );
              return ActionResult::FAILURE_ABORT;
            }
            
            const f32 currentDist = markerPoseWrtRobot.GetTranslation().Length();
            if(currentDist < closestDist) {
              closestDist = currentDist;
              closestMarker = marker;
              objectPoseWrtRobot = markerPoseWrtRobot;
            }
          }
        }
        
        if(closestMarker == nullptr) {
          PRINT_NAMED_ERROR("TurnTowardsObjectAction.Init.NoClosestMarker",
                            "No closest marker found for object %d.", _objectID.GetValue());
          return ActionResult::FAILURE_ABORT;
        }
      }
      
      // Have to set the parent class's pose before calling its Init()
      SetPose(objectPoseWrtRobot);
      
      ActionResult facePoseInitResult = TurnTowardsPoseAction::Init();
      if(ActionResult::SUCCESS != facePoseInitResult) {
        return facePoseInitResult;
      }
      
      _facePoseCompoundActionDone = false;
      
      // Disable completion signals since this is inside another action
      _visuallyVerifyAction.ShouldEmitCompletionSignal(false);
      _visuallyVerifyAction.ShouldSuppressTrackLocking(true);
      
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
          
          // Go ahead and do a first tick of visual verification's Update, to
          // get it initialized
          ActionResult verificationResult = _visuallyVerifyAction.Update();
          if(ActionResult::SUCCESS != verificationResult) {
            return verificationResult;
          }
        }
      }

      // If we get here, _compoundAction completed returned SUCCESS. So we can
      // can continue with our additional checks:
      if (_visuallyVerifyWhenDone) {
        ActionResult verificationResult = _visuallyVerifyAction.Update();
        if (verificationResult != ActionResult::SUCCESS) {
          return verificationResult;
        } else {
          _visuallyVerifyWhenDone = false;
        }
      }
      
      if(_headTrackWhenDone) {
        _robot.GetActionList().QueueActionNext(new TrackObjectAction(_robot, _objectID));
      }

      return ActionResult::SUCCESS;
    } // TurnTowardsObjectAction::CheckIfDone()
    
    
    const std::string& TurnTowardsObjectAction::GetName() const
    {
      static const std::string name("TurnTowardsObjectAction");
      return name;
    }
    
    void TurnTowardsObjectAction::GetCompletionUnion(ActionCompletedUnion& completionUnion) const
    {
      ObjectInteractionCompleted info;
      info.numObjects = 1;
      info.objectIDs[0] = _objectID;
      completionUnion.Set_objectInteractionCompleted(std::move( info ));
    }
    
#pragma mark ---- TraverseObjectAction ----
    
    TraverseObjectAction::TraverseObjectAction(Robot& robot, ObjectID objectID, const bool useManualSpeed)
    : IActionRunner(robot)
    , _objectID(objectID)
    , _useManualSpeed(useManualSpeed)
    {
  
    }
    
    const std::string& TraverseObjectAction::GetName() const
    {
      static const std::string name("TraverseObjectAction");
      return name;
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
        ActionableObject* object = dynamic_cast<ActionableObject*>(_robot.GetBlockWorld().GetObjectByID(_objectID));
        if(object == nullptr) {
          PRINT_NAMED_ERROR("TraverseObjectAction.Init.ObjectNotFound",
                            "Could not get actionable object with ID = %d from world.", _objectID.GetValue());
          return ActionResult::FAILURE_ABORT;
        }
        
        if(object->GetType() == ObjectType::Bridge_LONG ||
           object->GetType() == ObjectType::Bridge_SHORT)
        {
          CrossBridgeAction* bridgeAction = new CrossBridgeAction(_robot, _objectID, _useManualSpeed);
          bridgeAction->SetSpeedAndAccel(_speed_mmps, _accel_mmps2, _decel_mmps2);
          bridgeAction->ShouldSuppressTrackLocking(true);
          _chosenAction = bridgeAction;
        }
        else if(object->GetType() == ObjectType::Ramp_Basic) {
          AscendOrDescendRampAction* rampAction = new AscendOrDescendRampAction(_robot, _objectID, _useManualSpeed);
          rampAction->SetSpeedAndAccel(_speed_mmps, _accel_mmps2, _decel_mmps2);
          rampAction->ShouldSuppressTrackLocking(true);
          _chosenAction = rampAction;
        }
        else {
          PRINT_NAMED_ERROR("TraverseObjectAction.Init.CannotTraverseObjectType",
                            "Robot %d was asked to traverse object ID=%d of type %s, but "
                            "that traversal is not defined.", _robot.GetID(),
                            object->GetID().GetValue(), ObjectTypeToString(object->GetType()));
          
          return ActionResult::FAILURE_ABORT;
        }
      }
      
      // Now just use chosenAction's Update()
      assert(_chosenAction != nullptr);
      return _chosenAction->Update();
      
    } // Update()
    
#pragma mark ---- TurnTowardsPoseAction ----
    
    TurnTowardsPoseAction::TurnTowardsPoseAction(Robot& robot, const Pose3d& pose, Radians maxTurnAngle)
    : PanAndTiltAction(robot, 0, 0, false, true)
    , _poseWrtRobot(pose)
    , _maxTurnAngle(maxTurnAngle.getAbsoluteVal())
    , _isPoseSet(true)
    {
      
    }
    
    TurnTowardsPoseAction::TurnTowardsPoseAction(Robot& robot, Radians maxTurnAngle)
    : PanAndTiltAction(robot, 0, 0, false, true)
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
    Radians TurnTowardsPoseAction::GetHeadAngle()
    {
      const f32 heightDiff = _poseWrtRobot.GetTranslation().z() - NECK_JOINT_POSITION[2];
      const f32 distanceXY = Point2f(_poseWrtRobot.GetTranslation()).Length() - NECK_JOINT_POSITION[0];
      
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
      const Radians headAngle = std::atan2(heightDiff, distanceXY) + (kHeadAngleDistBias_rad * biasScaleFactorDist) + (kHeadAngleHeightBias_rad * biasScaleFactorHeight) + DEG_TO_RAD(4);

      return headAngle;
    }
    
    void TurnTowardsPoseAction::SetPose(const Pose3d& pose)
    {
      _poseWrtRobot = pose;
      _isPoseSet = true;
    }
    
    ActionResult TurnTowardsPoseAction::Init()
    {
      _nothingToDo = false; // in case of re-run
      
      if(!_isPoseSet) {
        PRINT_NAMED_ERROR("TurnTowardsPoseAction.Init.PoseNotSet", "");
        return ActionResult::FAILURE_ABORT;
      }
      
      if(_poseWrtRobot.GetParent() == nullptr) {
        PRINT_NAMED_INFO("TurnTowardsPoseAction.SetPose.AssumingRobotOriginAsParent", "");
        _poseWrtRobot.SetParent(_robot.GetWorldOrigin());
      }
      else if(false == _poseWrtRobot.GetWithRespectTo(_robot.GetPose(), _poseWrtRobot))
      {
        PRINT_NAMED_ERROR("TurnTowardsPoseAction.Init.PoseOriginFailure",
                          "Could not get pose w.r.t. robot pose.");
        _poseWrtRobot.Print();
        _poseWrtRobot.PrintNamedPathToOrigin(false);
        _robot.GetPose().PrintNamedPathToOrigin(false);
        return ActionResult::FAILURE_ABORT;
      }
      
      if(_maxTurnAngle > 0)
      {
        // Compute the required angle to face the object
        const Radians turnAngle = std::atan2(_poseWrtRobot.GetTranslation().y(),
                                             _poseWrtRobot.GetTranslation().x());
        
        PRINT_NAMED_INFO("TurnTowardsPoseAction.Init.TurnAngle",
                         "Computed turn angle = %.1fdeg", turnAngle.getDegrees());
        
        if(turnAngle.getAbsoluteVal() <= _maxTurnAngle) {
          SetBodyPanAngle(turnAngle);
        } else {
          PRINT_NAMED_INFO("TurnTowardsPoseAction.Init.RequiredTurnTooLarge",
                           "Required turn angle of %.1fdeg is larger than max angle of %.1fdeg.",
                            turnAngle.getDegrees(), _maxTurnAngle.getDegrees());
          
          _nothingToDo = true;
          return ActionResult::SUCCESS;
        }
      }
      
      // Compute the required head angle to face the object
      Radians headAngle = GetHeadAngle();
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
    
    
    const std::string& TurnTowardsPoseAction::GetName() const
    {
      static const std::string name("TurnTowardsPoseAction");
      return name;
    }
    
#pragma mark ---- VisuallyVerifyObjectAction ----
    
    VisuallyVerifyObjectAction::VisuallyVerifyObjectAction(Robot& robot,
                                                           ObjectID objectID,
                                                           Vision::Marker::Code whichCode)
    : IAction(robot)
    , _objectID(objectID)
    , _whichCode(whichCode)
    , _waitToVerifyTime(-1)
    , _moveLiftToHeightAction(robot, MoveLiftToHeightAction::Preset::OUT_OF_FOV)
    , _moveLiftToHeightActionDone(false)
    {
      
    }
    
    VisuallyVerifyObjectAction::~VisuallyVerifyObjectAction()
    {
      if(_waitForImagesAction != nullptr)
      {
        _waitForImagesAction->PrepForCompletion();
      }
      Util::SafeDelete(_waitForImagesAction);
      _moveLiftToHeightAction.PrepForCompletion();
    }
    
    const std::string& VisuallyVerifyObjectAction::GetName() const
    {
      static const std::string name("VisuallyVerifyObject" + std::to_string(_objectID.GetValue())
                                    + "Action");
      return name;
    }
    
    ActionResult VisuallyVerifyObjectAction::Init()
    {
      _waitForImagesAction = new WaitForImagesAction(_robot, GetNumImagesToWaitFor());
      _waitForImagesAction->ShouldEmitCompletionSignal(false);
    
      using namespace ExternalInterface;
      
      _objectSeen = false;
      
      auto obsObjLambda = [this](const AnkiEvent<MessageEngineToGame>& event)
      {
        const auto& objectObservation = event.GetData().Get_RobotObservedObject();
        // ID has to match and we have to actually have seen a marker (not just
        // saying part of the object is in FOV due to assumed projection)
        if(!_objectSeen && objectObservation.objectID == _objectID && objectObservation.markersVisible)
        {
          _objectSeen = true;
        }
      };
      
      _observedObjectHandle = _robot.GetExternalInterface()->Subscribe(MessageEngineToGameTag::RobotObservedObject, obsObjLambda);
      
      if(_whichCode == Vision::Marker::ANY_CODE) {
        _markerSeen = true;
      } else {
        _markerSeen = false;
      }
      
      // Get lift out of the way

      _moveLiftToHeightAction.ShouldEmitCompletionSignal(false);
      _moveLiftToHeightAction.ShouldSuppressTrackLocking(true);
      _moveLiftToHeightActionDone = false;
      _waitToVerifyTime = -1.f;
      
      // Go ahead and do the first update on moving the lift, so we don't "waste"
      // the first tick of CheckIfDone initializing the sub-action.
      ActionResult moveLiftInitResult = _moveLiftToHeightAction.Update();
      if(ActionResult::SUCCESS == moveLiftInitResult ||
         ActionResult::RUNNING == moveLiftInitResult)
      {
        // Continue to CheckIfDone as long as the first Update didn't _fail_
        return ActionResult::SUCCESS;
      } else {
        return moveLiftInitResult;
      }
    }
    
    ActionResult VisuallyVerifyObjectAction::CheckIfDone()
    {
      ActionResult actionRes = ActionResult::RUNNING;
      
      if(_objectSeen)
      {
        if(!_markerSeen)
        {
          // We've seen the object, check if we've seen the correct marker if one was
          // specified and we haven't seen it yet
          ObservableObject* object = _robot.GetBlockWorld().GetObjectByID(_objectID);
          if(object == nullptr) {
            PRINT_NAMED_ERROR("VisuallyVerifyObjectAction.CheckIfDone.ObjectNotFound",
                              "[%d] Object with ID=%d no longer exists in the world.",
                              GetTag(),
                              _objectID.GetValue());
            return ActionResult::FAILURE_ABORT;
          }
          
          // Look for which markers were seen since (and including) last observation time
          std::vector<const Vision::KnownMarker*> observedMarkers;
          object->GetObservedMarkers(observedMarkers, object->GetLastObservedTime());
          
          for(auto marker : observedMarkers) {
            if(marker->GetCode() == _whichCode) {
              _markerSeen = true;
              break;
            }
          }
          
          if(!_markerSeen) {
            // Seeing wrong marker(s). Log this for help in debugging
            std::string observedMarkerNames;
            for(auto marker : observedMarkers) {
              observedMarkerNames += Vision::MarkerTypeStrings[marker->GetCode()];
              observedMarkerNames += " ";
            }
            
            PRINT_NAMED_INFO("VisuallyVerifyObjectAction.CheckIfDone.WrongMarker",
                             "[%d] Have seen object %d, but not marker code %d. Have seen: %s",
                             GetTag(), _objectID.GetValue(), _whichCode, observedMarkerNames.c_str());
          }
        } // if(!_markerSeen)
        
        if(_markerSeen) {
          // We've seen the object and the correct marker: we're good to go!
          return ActionResult::SUCCESS;
        }
        
      } else {
        // Still waiting to see the object: keep moving head/lift
        if (!_moveLiftToHeightActionDone) {
          ActionResult liftActionRes = _moveLiftToHeightAction.Update();
          if (liftActionRes != ActionResult::SUCCESS) {
            if (liftActionRes != ActionResult::RUNNING) {
              PRINT_NAMED_WARNING("VisuallyVerifyObjectAction.CheckIfDone.CompoundActionFailed",
                                  "Failed to move lift out of FOV. Action result = %s\n",
                                  EnumToString(actionRes));
            }
            return liftActionRes;
          }
          _moveLiftToHeightActionDone = true;
        }
      } // if/else(objectSeen)
      
      if(!_robot.GetMoveComponent().IsMoving() && _waitForImagesAction->Update() != ActionResult::RUNNING)
      {
        PRINT_NAMED_WARNING("VisuallyVerifyObjectAction.CheckIfDone.TimedOut",
                            "Did not see object %d before processing %d images",
                            _objectID.GetValue(), GetNumImagesToWaitFor());
        return ActionResult::FAILURE_ABORT;
      }
      
      return actionRes;
      
    } // VisuallyVerifyObjectAction::CheckIfDone()
    
#pragma mark ---- TurnTowardsLastFacePoseAction ----

    TurnTowardsLastFacePoseAction::TurnTowardsLastFacePoseAction(Robot& robot, Radians maxTurnAngle, bool sayName)
    : TurnTowardsPoseAction(robot, maxTurnAngle)
    , _sayName(sayName)
    {
     
    }

    void TurnTowardsLastFacePoseAction::SetAction(IActionRunner *action)
    {
      if(nullptr != _action) {
        _action->PrepForCompletion();
        Util::SafeDelete(_action);
      }
      _action = action;
      if(nullptr != _action) {
        _action->ShouldEmitCompletionSignal(false);
        _action->ShouldSuppressTrackLocking(true);
      }
    }
    
    TurnTowardsLastFacePoseAction::~TurnTowardsLastFacePoseAction()
    {
      SetAction(nullptr);
      
      // In case we got interrupted and didn't get a chance to do this
      if(_tracksLocked) {
        _robot.GetMoveComponent().UnlockTracks((u8)AnimTrackFlag::HEAD_TRACK |
                                               (u8)AnimTrackFlag::BODY_TRACK);
      }
    }
    
    ActionResult TurnTowardsLastFacePoseAction::Init()
    {
      // If we have a last observed face set, use its pose. Otherwise pose wil not be set
      // and TurnTowardsPoseAction will return failure.
      Pose3d pose;
      if(_robot.GetFaceWorld().GetLastObservedFaceWithRespectToRobot(pose) != 0)
      {
        TurnTowardsPoseAction::SetPose(pose);
        
        _action = nullptr;
        _obsFaceID = Vision::UnknownFaceID;
        _closestDistSq = std::numeric_limits<f32>::max();
        
        if(_robot.HasExternalInterface())
        {
          using namespace ExternalInterface;
          auto helper = MakeAnkiEventUtil(*_robot.GetExternalInterface(), *this, _signalHandles);
          helper.SubscribeEngineToGame<MessageEngineToGameTag::RobotObservedFace>();
          helper.SubscribeEngineToGame<MessageEngineToGameTag::RobotChangedObservedFaceID>();
        }
        
        _state = State::Turning;
        _robot.GetMoveComponent().LockTracks((u8)AnimTrackFlag::HEAD_TRACK |
                                             (u8)AnimTrackFlag::BODY_TRACK);
        _tracksLocked = true;
        
        return TurnTowardsPoseAction::Init();
        
      } else {
        _state = State::SayingName; // jump to end
        return ActionResult::SUCCESS;
      }
      
    } // Init()

    template<>
    void TurnTowardsLastFacePoseAction::HandleMessage(const ExternalInterface::RobotObservedFace& msg)
    {
      Vision::FaceID_t faceID = msg.faceID;
      if(_state == State::Turning || _state == State::WaitingForFace)
      {
        // Record this face if it is closer than any we've seen so far
        const Vision::TrackedFace* face = _robot.GetFaceWorld().GetFace(faceID);
        if(nullptr != face) {
          Pose3d faceWrtRobot;
          if(true == face->GetHeadPose().GetWithRespectTo(_robot.GetPose(), faceWrtRobot)) {
            const f32 distSq = faceWrtRobot.GetTranslation().LengthSq();
            if(distSq < _closestDistSq) {
              _obsFaceID = faceID;
              _closestDistSq = distSq;
              PRINT_NAMED_DEBUG("TurnTowardsLastFacePoseAction.ObservedFaceCallback",
                                "Observed ID=%d at distSq=%.1f",
                                _obsFaceID, _closestDistSq);
            }
          }
        }
      }
    } // HandleMessage(RobotObservedFace)
    
    
    template<>
    void TurnTowardsLastFacePoseAction::HandleMessage(const ExternalInterface::RobotChangedObservedFaceID& msg)
    {
      if(_obsFaceID == msg.oldID) {
        PRINT_NAMED_DEBUG("TurnTowardsLastFacePoseAction.HandleChangedFaceIDMessage",
                          "Updating fine-tune ID from %d to %d",
                          _obsFaceID, msg.newID);
        _obsFaceID = msg.newID;
      }
    } // HandleMessage(RobotChangedObservedFaceID)
    
    
    void TurnTowardsLastFacePoseAction::CreateFineTuneAction()
    {
      PRINT_NAMED_DEBUG("TurnTowardsLastFacePoseAction.CreateFinalAction.SawFace",
                        "Observed ID=%d. Will fine tune.", _obsFaceID);
                        
      const Vision::TrackedFace* face = _robot.GetFaceWorld().GetFace(_obsFaceID);
      if(nullptr != face) {
        // Valid face...
        Pose3d pose;
        if(true == face->GetHeadPose().GetWithRespectTo(_robot.GetPose(), pose)) {
          // ... with valid pose w.r.t. robot. Turn towards that face -- iff it doesn't
          // require too large of an adjustment.
          SetAction(new TurnTowardsPoseAction(_robot, pose, DEG_TO_RAD(45)));
        }
      } else {
        SetAction(nullptr);
      }
      
      _state = State::FineTuning;
    } // CreateFineTuneAction()
    
    
    ActionResult TurnTowardsLastFacePoseAction::CheckIfDone()
    {
      ActionResult result = ActionResult::RUNNING;
      
      switch(_state)
      {
        case State::Turning:
        {
          result = TurnTowardsPoseAction::CheckIfDone();
          if(ActionResult::RUNNING != result) {
            _robot.GetMoveComponent().UnlockTracks((u8)AnimTrackFlag::HEAD_TRACK |
                                                   (u8)AnimTrackFlag::BODY_TRACK);
            _tracksLocked = false;
          }
          
          if(ActionResult::SUCCESS == result)
          {
            // Initial (blind) turning to pose finished...
            if(_obsFaceID == Vision::UnknownFaceID) {
              // ...didn't see a face yet, wait a couple of images to see if we do
              PRINT_NAMED_DEBUG("TurnTowardsLastFacePoseAction.CheckIfDone.NoFaceObservedYet",
                                "Will wait no more than %d frames",
                                _maxFramesToWait);
              ASSERT_NAMED(nullptr == _action, "TurnTowardsLastFacePoseAction.CheckIfDone.ActionPointerShouldStillBeNull");
              SetAction(new WaitForImagesAction(_robot, _maxFramesToWait));
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
          if(_obsFaceID != Vision::UnknownFaceID) {
            // We saw a face. Turn towards it and (optionally) say name.
            CreateFineTuneAction(); // Moves to State:FineTuning
            result = ActionResult::RUNNING;
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
              const Vision::TrackedFace* face = _robot.GetFaceWorld().GetFace(_obsFaceID);
              if(nullptr != face && !face->GetName().empty())
              {
                SetAction(new SayTextAction(_robot, face->GetName(), SayTextStyle::Name_Normal, false));
                _state = State::SayingName;
                result = ActionResult::RUNNING;
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
      
      return result;
      
    } // TurnTowardsLastFacePose::CheckIfDone()

  
    const std::string& TurnTowardsLastFacePoseAction::GetName() const
    {
      static const std::string name("TurnTowardsLastFacePoseAction");
      return name;
    }

#pragma mark ---- TurnTowardsFaceWrapperAction ----

    TurnTowardsFaceWrapperAction::TurnTowardsFaceWrapperAction(Robot& robot,
                                                               IActionRunner* action,
                                                               bool turnBeforeAction,
                                                               bool turnAfterAction,
                                                               Radians maxTurnAngle,
                                                               bool sayName)
    : CompoundActionSequential(robot)
    {
      if( turnBeforeAction ) {
        AddAction( new TurnTowardsLastFacePoseAction(robot, maxTurnAngle, sayName) );
      }
      AddAction(action);
      if( turnAfterAction ) {
        AddAction( new TurnTowardsLastFacePoseAction(robot, maxTurnAngle, sayName) ) ;
      }
      
      // Use the action we're wrapping for the completion info and type
      SetProxyTag(action->GetTag());
    }  
    
#pragma mark ---- WaitAction ----
    
    WaitAction::WaitAction(Robot& robot, f32 waitTimeInSeconds)
    : IAction(robot)
    , _waitTimeInSeconds(waitTimeInSeconds)
    , _doneTimeInSeconds(-1.f)
    {
      // Put the wait time with two decimals of precision in the action's name
      char tempBuffer[32];
      snprintf(tempBuffer, 32, "Wait%.2fSecondsAction", _waitTimeInSeconds);
      _name = tempBuffer;
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
    
    WaitForImagesAction::WaitForImagesAction(Robot& robot, u32 numFrames, TimeStamp_t afterTimeStamp)
    : IAction(robot)
    , _numFramesToWaitFor(numFrames)
    , _afterTimeStamp(afterTimeStamp)
    {
      _name = "WaitFor" + std::to_string(_numFramesToWaitFor) + "Images";
    }
    
    ActionResult WaitForImagesAction::Init()
    {
      _numFramesSeen = 0;
      
      auto imageProcLambda = [this](const AnkiEvent<ExternalInterface::MessageEngineToGame>& msg)
      {
        if(msg.GetData().Get_RobotProcessedImage().timestamp > _afterTimeStamp) {
          ++_numFramesSeen;
          PRINT_NAMED_DEBUG("WaitForImagesAction.Callback", "Frame %d of %d",
                            _numFramesSeen, _numFramesToWaitFor);
        }
      };
      
      _imageProcSignalHandle = _robot.GetExternalInterface()->Subscribe(ExternalInterface::MessageEngineToGameTag::RobotProcessedImage, imageProcLambda);
      
      return ActionResult::SUCCESS;
    }
    
    ActionResult WaitForImagesAction::CheckIfDone()
    {
      if(_numFramesSeen >= _numFramesToWaitFor) {
        return ActionResult::SUCCESS;
      }
      else {
        return ActionResult::RUNNING;
      }
    }
    
#pragma mark ---- ReadToolCodeAction ----
    
    ReadToolCodeAction::ReadToolCodeAction(Robot& robot, bool doCalibration)
    : IAction(robot)
    , _doCalibration(doCalibration)
    , _headAndLiftDownAction(robot)
    {
      _toolCodeInfo.code = ToolCode::UnknownTool;
    }
    
    ActionResult ReadToolCodeAction::Init()
    {
      // Put the head and lift down for read
      _headAndLiftDownAction.AddAction(new MoveHeadToAngleAction(_robot, MIN_HEAD_ANGLE));
      _headAndLiftDownAction.AddAction(new MoveLiftToHeightAction(_robot, LIFT_HEIGHT_LOWDOCK, READ_TOOL_CODE_LIFT_HEIGHT_TOL_MM));
      
      _state = State::WaitingToGetInPosition;
      
      _toolReadSignalHandle = _robot.GetExternalInterface()->Subscribe(ExternalInterface::MessageEngineToGameTag::RobotReadToolCode,
         [this] (const AnkiEvent<ExternalInterface::MessageEngineToGame> &msg) {
           _toolCodeInfo = msg.GetData().Get_RobotReadToolCode().info;
           PRINT_NAMED_INFO("ReadToolCodeAction.SignalHandler",
                            "Read tool code: %s", EnumToString(_toolCodeInfo.code));
           this->_state = State::ReadCompleted;
      });
      
      return ActionResult::SUCCESS;
    }
    
    ReadToolCodeAction::~ReadToolCodeAction()
    {
      _robot.GetVisionComponent().EnableMode(VisionMode::ReadingToolCode, false);
      _headAndLiftDownAction.PrepForCompletion();
    }
    
    ActionResult ReadToolCodeAction::CheckIfDone()
    {
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
            
            Result setCalibResult = _robot.GetVisionComponent().EnableToolCodeCalibration(_doCalibration);
            if(RESULT_OK != setCalibResult) {
              PRINT_NAMED_INFO("ReadToolCodeAction.CheckIfDone.FailedToSetCalibration", "");
              result = ActionResult::FAILURE_ABORT;
            } else {
              // Tell the VisionSystem thread to check the tool code in the next image it gets.
              // It will disable this mode when it completes.
              _robot.GetVisionComponent().EnableMode(VisionMode::ReadingToolCode, true);
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
            result = ActionResult::FAILURE_ABORT;
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

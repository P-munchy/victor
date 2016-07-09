//
//  robot.cpp
//  Products_Cozmo
//
//  Created by Andrew Stein on 8/23/13.
//  Copyright (c) 2013 Anki, Inc. All rights reserved.
//

#include "anki/cozmo/basestation/audio/robotAudioClient.h"
#include "anki/cozmo/basestation/pathPlanner.h"
#include "anki/cozmo/basestation/latticePlanner.h"
#include "anki/cozmo/basestation/minimalAnglePlanner.h"
#include "anki/cozmo/basestation/faceAndApproachPlanner.h"
#include "anki/cozmo/basestation/pathDolerOuter.h"
#include "anki/cozmo/basestation/blockWorld.h"
#include "anki/cozmo/basestation/block.h"
#include "anki/cozmo/basestation/activeCube.h"
#include "anki/cozmo/basestation/ledEncoding.h"
#include "anki/cozmo/basestation/robot.h"
#include "anki/cozmo/basestation/robotDataLoader.h"
#include "anki/cozmo/basestation/robotManager.h"
#include "anki/cozmo/basestation/utils/parsingConstants/parsingConstants.h"
#include "anki/cozmo/shared/cozmoEngineConfig.h"
#include "anki/common/basestation/math/point.h"
#include "anki/common/basestation/math/quad_impl.h"
#include "anki/common/basestation/math/point_impl.h"
#include "anki/common/basestation/math/rect_impl.h"
#include "anki/common/basestation/math/poseBase_impl.h"
#include "anki/common/basestation/utils/timer.h"
#include "anki/vision/CameraSettings.h"
// TODO: This is shared between basestation and robot and should be moved up
#include "anki/cozmo/shared/cozmoConfig.h"
#include "anki/cozmo/basestation/robotInterface/messageHandler.h"
#include "anki/cozmo/basestation/robotPoseHistory.h"
#include "anki/cozmo/basestation/ramp.h"
#include "anki/cozmo/basestation/charger.h"
#include "anki/cozmo/basestation/viz/vizManager.h"
#include "anki/cozmo/basestation/actions/basicActions.h"
#include "anki/cozmo/basestation/faceAnimationManager.h"
#include "anki/cozmo/basestation/externalInterface/externalInterface.h"
#include "anki/cozmo/basestation/behaviorSystem/behaviorChoosers/iBehaviorChooser.h"
#include "anki/cozmo/basestation/behaviorSystem/AIWhiteboard.h"
#include "anki/cozmo/basestation/cannedAnimationContainer.h"
#include "anki/cozmo/basestation/behaviors/behaviorInterface.h"
#include "anki/cozmo/basestation/moodSystem/moodManager.h"
#include "anki/cozmo/basestation/components/lightsComponent.h"
#include "anki/cozmo/basestation/components/progressionUnlockComponent.h"
#include "anki/cozmo/basestation/components/visionComponent.h"
#include "anki/cozmo/basestation/blocks/blockFilter.h"
#include "anki/cozmo/basestation/components/blockTapFilterComponent.h"
#include "anki/cozmo/basestation/speedChooser.h"
#include "anki/cozmo/basestation/drivingAnimationHandler.h"
#include "anki/common/basestation/utils/data/dataPlatform.h"
#include "anki/vision/basestation/visionMarker.h"
#include "anki/vision/basestation/observableObjectLibrary_impl.h"
#include "anki/vision/basestation/image.h"
#include "clad/robotInterface/messageEngineToRobot.h"
#include "clad/externalInterface/messageEngineToGame.h"
#include "clad/types/robotStatusAndActions.h"
#include "clad/types/activeObjectTypes.h"
#include "clad/types/gameStatusFlag.h"
#include "util/console/consoleInterface.h"
#include "util/helpers/templateHelpers.h"
#include "util/fileUtils/fileUtils.h"
#include "util/transport/reliableConnection.h"

#include "opencv2/calib3d/calib3d.hpp"
#include "opencv2/highgui/highgui.hpp" // For imwrite() in ProcessImage

#include <fstream>
#include <regex>
#include <dirent.h>
#include <sys/stat.h>

#define MAX_DISTANCE_FOR_SHORT_PLANNER 40.0f
#define MAX_DISTANCE_TO_PREDOCK_POSE 20.0f
#define MIN_DISTANCE_FOR_MINANGLE_PLANNER 1.0f

#define DEBUG_BLOCK_LIGHTS 0

namespace Anki {
namespace Cozmo {
    
/*
// static initializers
const RotationMatrix3d Robot::_kDefaultHeadCamRotation = RotationMatrix3d({
0, 0, 1,
-1, 0, 0,
0,-1, 0
});
*/

static const float kPitchAngleOnBack_rads = DEG_TO_RAD(74.5f);
static const float kPitchAngleOnBack_sim_rads = DEG_TO_RAD(96.4f);

CONSOLE_VAR(f32, kPitchAngleOnBackTolerance_deg, "Robot", 5.0f);
CONSOLE_VAR(u32, kRobotTimeToConsiderOnBack_ms, "Robot", 300);
CONSOLE_VAR(bool, kDebugPossibleBlockInteraction, "Robot", false);
  
// For tool code reading
// 4-degree look down: (Make sure to update cozmoBot.proto to match!)
const RotationMatrix3d Robot::_kDefaultHeadCamRotation = RotationMatrix3d({
  0,             -0.0698,    0.9976,
 -1.0000,         0,         0,
  0,             -0.9976,   -0.0698,
});


Robot::Robot(const RobotID_t robotID, const CozmoContext* context)
  : _context(context)
  , _ID(robotID)
  , _timeSynced(false)
  , _blockWorld(this)
  , _faceWorld(*this)
  , _behaviorMgr(*this)
  , _audioClient(new Audio::RobotAudioClient(this))
  , _animationStreamer(_context, *_audioClient)
  , _drivingAnimationHandler(new DrivingAnimationHandler(*this))
  , _movementComponent(*this)
  , _visionComponentPtr( new VisionComponent(*this, VisionComponent::RunMode::Asynchronous, _context))
  , _nvStorageComponent(*this, _context)
  , _textToSpeechComponent(_context)
  , _lightsComponent( new LightsComponent( *this ) )
  , _neckPose(0.f,Y_AXIS_3D(),
              {NECK_JOINT_POSITION[0], NECK_JOINT_POSITION[1], NECK_JOINT_POSITION[2]}, &_pose, "RobotNeck")
  , _headCamPose(_kDefaultHeadCamRotation,
                 {HEAD_CAM_POSITION[0], HEAD_CAM_POSITION[1], HEAD_CAM_POSITION[2]}, &_neckPose, "RobotHeadCam")
  , _liftBasePose(0.f, Y_AXIS_3D(),
                  {LIFT_BASE_POSITION[0], LIFT_BASE_POSITION[1], LIFT_BASE_POSITION[2]}, &_pose, "RobotLiftBase")
  , _liftPose(0.f, Y_AXIS_3D(), {LIFT_ARM_LENGTH, 0.f, 0.f}, &_liftBasePose, "RobotLift")
  , _currentHeadAngle(MIN_HEAD_ANGLE)
  , _poseHistory(nullptr)
  , _moodManager(new MoodManager(this))
  , _progressionUnlockComponent(new ProgressionUnlockComponent(*this))
  , _speedChooser(new SpeedChooser(*this))
  , _blockFilter(new BlockFilter(this))
  , _tapFilterComponent(new BlockTapFilterComponent(*this))
  , _traceHandler(_context->GetDataPlatform())
  , _hasMismatchedEngineToRobotCLAD(false)
  , _hasMismatchedRobotToEngineCLAD(false)
{
  _poseHistory = new RobotPoseHistory();
  PRINT_NAMED_INFO("Robot.Robot", "Created");
      
  _pose.SetName("Robot_" + std::to_string(_ID));
  _driveCenterPose.SetName("RobotDriveCenter_" + std::to_string(_ID));
      
  // Initializes _pose, _poseOrigins, and _worldOrigin:
  Delocalize();
      
  // Delocalize will mark isLocalized as false, but we are going to consider
  // the robot localized (by odometry alone) to start, until he gets picked up.
  _isLocalized = true;
  SetLocalizedTo(nullptr);
      
  InitRobotMessageComponent(_context->GetRobotManager()->GetMsgHandler(),robotID);
      
  if (HasExternalInterface())
  {
    SetupGainsHandlers(*_context->GetExternalInterface());
    SetupMiscHandlers(*_context->GetExternalInterface());
  }
      
  // The call to Delocalize() will increment frameID, but we want it to be
  // initialzied to 0, to match the physical robot's initialization
  _frameId = 0;
      
  _lastDebugStringHash = 0;
      
  // Read in Mood Manager Json
  if (nullptr != _context->GetDataPlatform())
  {
    _moodManager->Init(_context->GetDataLoader()->GetRobotMoodConfig());
    LoadEmotionEvents();
  }

  // Initialize progression
  if (nullptr != _context->GetDataPlatform())
  {
    Json::Value progressionUnlockConfig;
    std::string jsonFilename = "config/basestation/config/unlock_config.json";
    bool success = _context->GetDataPlatform()->readAsJson(Util::Data::Scope::Resources,
                                                           jsonFilename,
                                                           progressionUnlockConfig);
    if (!success)
    {
      PRINT_NAMED_ERROR("Robot.UnlockConfigJsonNotFound",
                        "Unlock Json config file %s not found.",
                        jsonFilename.c_str());
    }
        
    _progressionUnlockComponent->Init(progressionUnlockConfig);
    _progressionUnlockComponent->SendUnlockStatus();
  }
  else {
    Json::Value empty;
    _progressionUnlockComponent->Init(empty);
  }
      
  // load available behaviors into the behavior factory
  LoadBehaviors();
  _behaviorMgr.InitConfiguration(_context->GetDataLoader()->GetRobotBehaviorConfig());

      
  SetHeadAngle(_currentHeadAngle);
  _pdo = new PathDolerOuter(_context->GetRobotManager()->GetMsgHandler(), robotID);

  if (nullptr != _context->GetDataPlatform()) {
    _longPathPlanner  = new LatticePlanner(this, _context->GetDataPlatform());
  }
  else {
    // For unit tests, or cases where we don't have data, use the short planner in it's place
    PRINT_NAMED_WARNING("Robot.NoDataPlatform.WrongPlanner",
                        "Using short planner as the long planner, since we dont have a data platform");
    _longPathPlanner = new FaceAndApproachPlanner;
  }

  _shortPathPlanner = new FaceAndApproachPlanner;
  _shortMinAnglePathPlanner = new MinimalAnglePlanner;
  _selectedPathPlanner = _longPathPlanner;
      
  if (nullptr != _context->GetDataPlatform())
  {
    _visionComponentPtr->Init(_context->GetDataLoader()->GetRobotVisionConfig());
  }
      
} // Constructor: Robot
    
Robot::~Robot()
{
  AbortAll();
      
  // destroy vision component first because its thread might be using things from Robot. This fixes a crash
  // caused by the vision thread using _poseHistory when it was destroyed here
  Util::SafeDelete(_visionComponentPtr);
      
  Util::SafeDelete(_poseHistory);
  Util::SafeDelete(_pdo);
  Util::SafeDelete(_longPathPlanner);
  Util::SafeDelete(_shortPathPlanner);
  Util::SafeDelete(_shortMinAnglePathPlanner);
  Util::SafeDelete(_moodManager);
  Util::SafeDelete(_progressionUnlockComponent);
  Util::SafeDelete(_tapFilterComponent);
  Util::SafeDelete(_blockFilter);
  Util::SafeDelete(_drivingAnimationHandler);
  Util::SafeDelete(_speedChooser);

  _selectedPathPlanner = nullptr;
      
}
    
void Robot::SetOnCharger(bool onCharger)
{
  Charger* charger = dynamic_cast<Charger*>(GetBlockWorld().GetObjectByIDandFamily(_chargerID, ObjectFamily::Charger));
  if (onCharger && !_isOnCharger) {
        
    // If we don't actually have a charger, add an unconnected one now
    if (nullptr == charger)
    {
      ObjectID newObj = AddUnconnectedCharger();
      charger = dynamic_cast<Charger*>(GetBlockWorld().GetObjectByID(newObj));
      ASSERT_NAMED(nullptr != charger, "Robot.SetOnCharger.FailedToAddUnconnectedCharger");
    }
        
    PRINT_NAMED_INFO("Robot.SetOnCharger.OnCharger", "");
    Broadcast(ExternalInterface::MessageEngineToGame(ExternalInterface::ChargerEvent(true)));
        
  } else if (!onCharger && _isOnCharger) {
    PRINT_NAMED_INFO("Robot.SetOnCharger.OffCharger", "");
    Broadcast(ExternalInterface::MessageEngineToGame(ExternalInterface::ChargerEvent(false)));
  }
      
  if( onCharger && nullptr != charger )
  {
    charger->SetPoseToRobot(GetPose());
  }
      
  _isOnCharger = onCharger;
}
    
ObjectID Robot::AddUnconnectedCharger()
{
  ASSERT_NAMED(_chargerID.IsUnknown(), "AddUnconnectedCharger.ChargerAlreadyExists");
  ObjectID objID = GetBlockWorld().AddActiveObject(-1, 0, ActiveObjectType::OBJECT_CHARGER);
  SetCharger(objID);
  return _chargerID;
}

    
void Robot::SetPickedUp(bool t)
{
  // We use the cliff sensor to help determine if we're picked up; if it's disabled then ignore when it is
  // reported as true. If it's false we want to be able to go through the put down logic below.
  if (!IsCliffSensorEnabled() && t) {
    return;
  }
      
  if(_isPickedUp == false && t == true) {
    // Robot is being picked up: de-localize it
    Delocalize();
        
    _visionComponentPtr->Pause(true);
        
    Broadcast(ExternalInterface::MessageEngineToGame(ExternalInterface::RobotPickedUp(GetID())));
        
    if( _isOnChargerPlatform )
    {
      _isOnChargerPlatform = false;
      Broadcast(
        ExternalInterface::MessageEngineToGame(
          ExternalInterface::RobotOnChargerPlatformEvent(_isOnChargerPlatform)));
    }
  }
  else if (true == _isPickedUp && false == t) {
    // Robot just got put back down
    _visionComponentPtr->Pause(false);
        
    ASSERT_NAMED(!IsLocalized(), "Robot should be delocalized when first put back down!");
        
    // If we are not localized and there is nothing else left in the world that
    // we could localize to, then go ahead and mark us as localized (via
    // odometry alone)
    if(false == _blockWorld.AnyRemainingLocalizableObjects()) {
      PRINT_NAMED_INFO("Robot.SetPickedUp.NoMoreRemainingLocalizableObjects",
                       "Marking previously-unlocalized robot %d as localized to odometry because "
                       "there are no more objects to localize to in the world.", GetID());
      SetLocalizedTo(nullptr); // marks us as localized to odometry only
    }
        
    // Check the lift to see if tool changed while we were picked up
    //_actionList.QueueActionNext(new ReadToolCodeAction(*this));
        
    Broadcast(ExternalInterface::MessageEngineToGame(ExternalInterface::RobotPutDown(GetID())));
  }
  _isPickedUp = t;
}
    
void Robot::Delocalize()
{
  PRINT_NAMED_INFO("Robot.Delocalize", "Delocalizing robot %d.\n", GetID());
      
  _isLocalized = false;
  _localizedToID.UnSet();
  _localizedToFixedObject = false;
  _localizedMarkerDistToCameraSq = -1.f;

  // NOTE: no longer doing this here because Delocalize() can be called by
  //  BlockWorld::ClearAllExistingObjects, resulting in a weird loop...
  //_blockWorld.ClearAllExistingObjects();
      
  // TODO rsam:
  // origins are no longer destroyed to prevent children from having to rejigger as cubes do. This however
  // has the problem of leaving zombie origins, and having systems never deleting dead poses that can never
  // be transformed withRespectTo a current origin. The origins growing themselves is not a big problem since
  // they are merely a Pose3d instance. However systems that keep Poses around because they have a valid
  // origin could potentially be a problem. This would have to be profiled to identify those systems, so not
  // really worth adding here a warning for "number of zombies is too big" without actually keeping track
  // of how many children they hold, or for how long. Eg: zombies with no children could auto-delete themselves,
  // but is the cost of bookkeeping bigger than what we are currently losing to zombies? That's the question
  // to profile
      
  // Add a new pose origin to use until the robot gets localized again
  _poseOrigins.emplace_back();
  _poseOrigins.back().SetName("Robot" + std::to_string(_ID) + "_PoseOrigin" + std::to_string(_poseOrigins.size() - 1));
  _worldOrigin = &_poseOrigins.back();
      
  _pose.SetRotation(0, Z_AXIS_3D());
  _pose.SetTranslation({0.f, 0.f, 0.f});
  _pose.SetParent(_worldOrigin);
      
  _driveCenterPose.SetRotation(0, Z_AXIS_3D());
  _driveCenterPose.SetTranslation({0.f, 0.f, 0.f});
  _driveCenterPose.SetParent(_worldOrigin);
      
  _poseHistory->Clear();
  //++_frameId;
     
  // Update VizText
  GetContext()->GetVizManager()->SetText(VizManager::LOCALIZED_TO, NamedColors::YELLOW,
                                         "LocalizedTo: <nothing>");
  GetContext()->GetVizManager()->SetText(VizManager::WORLD_ORIGIN, NamedColors::YELLOW,
                                         "WorldOrigin[%lu]: %s",
                                         (unsigned long)_poseOrigins.size(),
                                         _worldOrigin->GetName().c_str());
      
  // create a new memory map for this origin
  _blockWorld.CreateLocalizedMemoryMap(_worldOrigin);
      
  // notify behavior whiteboard
  _behaviorMgr.GetWhiteboard().OnRobotDelocalized();
  
  // send message to game. At the moment I implement this so that Webots can update the render, but potentially
  // any system can listen to this
  Broadcast(ExternalInterface::MessageEngineToGame(ExternalInterface::RobotDelocalized(GetID())));
      
} // Delocalize()
    
Result Robot::SetLocalizedTo(const ObservableObject* object)
{
  if(object == nullptr) {
    GetContext()->GetVizManager()->SetText(VizManager::LOCALIZED_TO, NamedColors::YELLOW,
                                           "LocalizedTo: Odometry");
    _localizedToID.UnSet();
    _isLocalized = true;
    return RESULT_OK;
  }
      
  if(object->GetID().IsUnknown()) {
    PRINT_NAMED_ERROR("Robot.SetLocalizedTo.IdNotSet",
                      "Cannot localize to an object with no ID set.\n");
    return RESULT_FAIL;
  }
      
  // Find the closest, most recently observed marker on the object
  TimeStamp_t mostRecentObsTime = 0;
  for(const auto& marker : object->GetMarkers()) {
    if(marker.GetLastObservedTime() >= mostRecentObsTime) {
      Pose3d markerPoseWrtCamera;
      if(false == marker.GetPose().GetWithRespectTo(_visionComponentPtr->GetCamera().GetPose(), markerPoseWrtCamera)) {
        PRINT_NAMED_ERROR("Robot.SetLocalizedTo.MarkerOriginProblem",
                          "Could not get pose of marker w.r.t. robot camera.\n");
        return RESULT_FAIL;
      }
      const f32 distToMarkerSq = markerPoseWrtCamera.GetTranslation().LengthSq();
      if(_localizedMarkerDistToCameraSq < 0.f || distToMarkerSq < _localizedMarkerDistToCameraSq) {
        _localizedMarkerDistToCameraSq = distToMarkerSq;
        mostRecentObsTime = marker.GetLastObservedTime();
      }
    }
  }
  assert(_localizedMarkerDistToCameraSq >= 0.f);
      
  _localizedToID = object->GetID();
  _hasMovedSinceLocalization = false;
  _isLocalized = true;
      
  // Update VizText
  GetContext()->GetVizManager()->SetText(VizManager::LOCALIZED_TO, NamedColors::YELLOW,
                                         "LocalizedTo: %s_%d",
                                         ObjectTypeToString(object->GetType()), _localizedToID.GetValue());
  GetContext()->GetVizManager()->SetText(VizManager::WORLD_ORIGIN, NamedColors::YELLOW,
                                         "WorldOrigin[%lu]: %s",
                                         (unsigned long)_poseOrigins.size(),
                                         _worldOrigin->GetName().c_str());
      
  return RESULT_OK;
      
} // SetLocalizedTo()
    
    
Result Robot::UpdateFullRobotState(const RobotState& msg)
{
  Result lastResult = RESULT_OK;

  // Ignore state messages received before time sync
  if (!_timeSynced) {
    return lastResult;
  }
    
  // Set flag indicating that robot state messages have been received
  _newStateMsgAvailable = true;
      
  // Update head angle
  SetHeadAngle(msg.headAngle);
      
  // Update lift angle
  SetLiftAngle(msg.liftAngle);
      
  // Update robot pitch angle
  _pitchAngle = msg.pose.pitch_angle;
      
  // Get ID of last/current path that the robot executed
  SetLastRecvdPathID(msg.lastPathID);
      
  // Update other state vars
  SetCurrPathSegment( msg.currPathSegment );
  SetNumFreeSegmentSlots(msg.numFreeSegmentSlots);
      
  // Dole out more path segments to the physical robot if needed:
  if (IsTraversingPath() && GetLastRecvdPathID() == GetLastSentPathID()) {
    _pdo->Update(_currPathSegment, _numFreeSegmentSlots);
  }
      
  //robot->SetCarryingBlock( msg.status & IS_CARRYING_BLOCK ); // Still needed?
  SetPickingOrPlacing((bool)( msg.status & (uint16_t)RobotStatusFlag::IS_PICKING_OR_PLACING ));
  SetPickedUp((bool)( msg.status & (uint16_t)RobotStatusFlag::IS_PICKED_UP ));
  SetOnCharger(static_cast<bool>(msg.status & (uint16_t)RobotStatusFlag::IS_ON_CHARGER));
  _isCliffSensorOn = static_cast<bool>(msg.status & (uint16_t)RobotStatusFlag::CLIFF_DETECTED);

  GetMoveComponent().Update(msg);
      
  _battVoltage = (f32)msg.battVolt10x * 0.1f;
      
  _leftWheelSpeed_mmps = msg.lwheel_speed_mmps;
  _rightWheelSpeed_mmps = msg.rwheel_speed_mmps;
      
  _hasMovedSinceLocalization |= GetMoveComponent().IsMoving() || _isPickedUp;
      
  Pose3d newPose;
      
  if(IsOnRamp()) {
        
    // Sanity check:
    CORETECH_ASSERT(_rampID.IsSet());
        
    // Don't update pose history while on a ramp.
    // Instead, just compute how far the robot thinks it has gone (in the plane)
    // and compare that to where it was when it started traversing the ramp.
    // Adjust according to the angle of the ramp we know it's on.
        
    const f32 distanceTraveled = (Point2f(msg.pose.x, msg.pose.y) - _rampStartPosition).Length();
        
    Ramp* ramp = dynamic_cast<Ramp*>(_blockWorld.GetObjectByIDandFamily(_rampID, ObjectFamily::Ramp));
    if(ramp == nullptr) {
      PRINT_NAMED_ERROR("Robot.UpdateFullRobotState.NoRampWithID",
                        "Updating robot %d's state while on a ramp, but Ramp object with ID=%d not found in the world.",
                        _ID, _rampID.GetValue());
      return RESULT_FAIL;
    }
        
    // Progress must be along ramp's direction (init assuming ascent)
    Radians headingAngle = ramp->GetPose().GetRotationAngle<'Z'>();
        
    // Initialize tilt angle assuming we are ascending
    Radians tiltAngle = ramp->GetAngle();
        
    switch(_rampDirection)
    {
      case Ramp::DESCENDING:
        tiltAngle    *= -1.f;
        headingAngle += M_PI;
        break;
      case Ramp::ASCENDING:
        break;
            
      default:
        PRINT_NAMED_ERROR("Robot.UpdateFullRobotState.UnexpectedRampDirection",
                          "Robot is on a ramp, expecting the ramp direction to be either "
                          "ASCEND or DESCENDING, not %d.\n", _rampDirection);
        return RESULT_FAIL;
    }

    const f32 heightAdjust = distanceTraveled*sin(tiltAngle.ToFloat());
    const Point3f newTranslation(_rampStartPosition.x() + distanceTraveled*cos(headingAngle.ToFloat()),
                                 _rampStartPosition.y() + distanceTraveled*sin(headingAngle.ToFloat()),
                                 _rampStartHeight + heightAdjust);
        
    const RotationMatrix3d R_heading(headingAngle, Z_AXIS_3D());
    const RotationMatrix3d R_tilt(tiltAngle, Y_AXIS_3D());
        
    newPose = Pose3d(R_tilt*R_heading, newTranslation, _pose.GetParent());
    //SetPose(newPose); // Done by UpdateCurrPoseFromHistory() below
        
  } else {
    // This is "normal" mode, where we udpate pose history based on the
    // reported odometry from the physical robot
        
    // Ignore physical robot's notion of z from the message? (msg.pose_z)
    f32 pose_z = 0.f;

    if(msg.pose_frame_id == GetPoseFrameID()) {
      // Frame IDs match. Use the robot's current Z (but w.r.t. world origin)
      pose_z = GetPose().GetWithRespectToOrigin().GetTranslation().z();
    } else {
      // This is an old odometry update from a previous pose frame ID. We
      // need to look up the correct Z value to use for putting this
      // message's (x,y) odometry info into history. Since it comes from
      // pose history, it will already be w.r.t. world origin, since that's
      // how we store everything in pose history.
      RobotPoseStamp p;
      lastResult = _poseHistory->GetLastPoseWithFrameID(msg.pose_frame_id, p);
      if(lastResult != RESULT_OK) {
        PRINT_NAMED_ERROR("Robot.UpdateFullRobotState.GetLastPoseWithFrameIdError",
                          "Failed to get last pose from history with frame ID=%d.\n",
                          msg.pose_frame_id);
        return lastResult;
      }
      pose_z = p.GetPose().GetTranslation().z();
    }
        
    // Need to put the odometry update in terms of the current robot origin
    newPose = Pose3d(msg.pose.angle, Z_AXIS_3D(), {msg.pose.x, msg.pose.y, pose_z}, _worldOrigin);
        
  } // if/else on ramp
      
  // Add to history
  lastResult = AddRawOdomPoseToHistory(msg.timestamp,
                                       msg.pose_frame_id,
                                       newPose.GetTranslation().x(),
                                       newPose.GetTranslation().y(),
                                       newPose.GetTranslation().z(),
                                       newPose.GetRotationAngle<'Z'>().ToFloat(),
                                       msg.headAngle,
                                       msg.liftAngle);
      
  if(lastResult != RESULT_OK) {
    PRINT_NAMED_WARNING("Robot.UpdateFullRobotState.AddPoseError",
                        "AddRawOdomPoseToHistory failed for timestamp=%d\n", msg.timestamp);
    return lastResult;
  }
      
  if(UpdateCurrPoseFromHistory(*_worldOrigin) == false) {
    lastResult = RESULT_FAIL;
  }
      
  /*
    PRINT_NAMED_INFO("Robot.UpdateFullRobotState.OdometryUpdate",
    "Robot %d's pose updated to (%.3f, %.3f, %.3f) @ %.1fdeg based on "
    "msg at time=%d, frame=%d saying (%.3f, %.3f) @ %.1fdeg\n",
    _ID, _pose.GetTranslation().x(), _pose.GetTranslation().y(), _pose.GetTranslation().z(),
    _pose.GetRotationAngle<'Z'>().getDegrees(),
    msg.timestamp, msg.pose_frame_id,
    msg.pose_x, msg.pose_y, msg.pose_angle*180.f/M_PI);
  */
      
  // check if the robot is stuck on it's back. We track internally if it's on it's back, but don't send
  // the message out until some time.  // TODO:(bn) probably want to check that the robot isn't moving
  // around based on accelerometer here
  const float backAngle = IsPhysical() ? kPitchAngleOnBack_rads : kPitchAngleOnBack_sim_rads;
  const bool currOnBack = std::abs( GetPitchAngle() - backAngle ) <= DEG_TO_RAD( kPitchAngleOnBackTolerance_deg );
  bool sendOnBackValue = _lastSendOnBackValue;
      
  if( currOnBack && _isOnBack ) {
    // check if it has been long enough
    if( msg.timestamp > _robotFirstOnBack_ms + kRobotTimeToConsiderOnBack_ms ) {
      sendOnBackValue = true;
    }
  }
  else if( currOnBack && !_isOnBack ) {
    _robotFirstOnBack_ms = msg.timestamp;
  }
  else if ( ! currOnBack ) {
    sendOnBackValue = false;
  }

  _isOnBack = currOnBack;

      
  if( sendOnBackValue != _lastSendOnBackValue ) {
    Broadcast(ExternalInterface::MessageEngineToGame(ExternalInterface::RobotOnBack( sendOnBackValue )));
    _lastSendOnBackValue = sendOnBackValue;
  }

  // Engine modifications to state message.
  // TODO: Should this just be a different message? Or one that includes the state message from the robot?
  RobotState stateMsg(msg);

  const float imageFrameRate = 1000.0f / _visionComponentPtr->GetFramePeriod_ms();
  const float imageProcRate = 1000.0f / _visionComponentPtr->GetProcessingPeriod_ms();
            
  // Send state to visualizer for displaying
  GetContext()->GetVizManager()->SendRobotState(
    stateMsg,
    static_cast<size_t>(AnimConstants::KEYFRAME_BUFFER_SIZE) - (_numAnimationBytesStreamed - _numAnimationBytesPlayed),
    AnimationStreamer::NUM_AUDIO_FRAMES_LEAD-(_numAnimationAudioFramesStreamed - _numAnimationAudioFramesPlayed),
    (u8)MIN(((u8)imageFrameRate), u8_MAX),
    (u8)MIN(((u8)imageProcRate), u8_MAX),
    _enabledAnimTracks,
    _animationTag);
      
  return lastResult;
      
} // UpdateFullRobotState()
    
bool Robot::HasReceivedRobotState() const
{
  return _newStateMsgAvailable;
}
    
void Robot::SetCameraRotation(f32 roll, f32 pitch, f32 yaw)
{
  RotationMatrix3d rot(roll, -pitch, yaw);
  _headCamPose.SetRotation(rot * _kDefaultHeadCamRotation);
  PRINT_NAMED_INFO("Robot.SetCameraRotation", "yaw_corr=%f, pitch_corr=%f, roll_corr=%f", yaw, pitch, roll);
}
    
void Robot::SetPhysicalRobot(bool isPhysical)
{
  // TODO: Move somewhere else? This might not the best place for this, but it's where we
  // know whether or not we're talking to a physical robot or not so do things that depend on that here.
  // Assumes this function is only called once following connection.
      
  // Connect to active objects in saved blockpool, but only for physical robots.
  // Sim robots automatically connect to all robots in their world as long as CozmoBot's
  // autoConnectToBlocks field is TRUE.
  if (isPhysical) {
    if (_context->GetDataPlatform() != nullptr) {
      _blockFilter->Init(_context->GetDataPlatform()->pathToResource(Util::Data::Scope::External, "blockPool.txt"),
                         _context->GetExternalInterface());
    }
  }
      
      
  _isPhysical = isPhysical;
      
  // Modify net timeout depending on robot type - simulated robots shouldn't timeout so we can pause and debug
  // them We do this regardless of previous state to ensure it works when adding 1st simulated robot (as
  // _isPhysical already == false in that case) Note: We don't do this on phone by default, they also have a
  // remote connection to the simulator so removing timeout would force user to restart both sides each time.
  #if !(ANKI_IOS_BUILD || ANDROID)
  {
    static const double kPhysicalRobotNetConnectionTimeoutInMS =
      Anki::Util::ReliableConnection::GetConnectionTimeoutInMS(); // grab default on 1st call
    const double kSimulatedRobotNetConnectionTimeoutInMS = FLT_MAX;
    const double netConnectionTimeoutInMS =
      isPhysical ? kPhysicalRobotNetConnectionTimeoutInMS : kSimulatedRobotNetConnectionTimeoutInMS;
    PRINT_NAMED_INFO("Robot.SetPhysicalRobot", "ReliableConnection::SetConnectionTimeoutInMS(%f) for %s Robot",
                     netConnectionTimeoutInMS, isPhysical ? "Physical" : "Simulated");
    Anki::Util::ReliableConnection::SetConnectionTimeoutInMS(netConnectionTimeoutInMS);
  }
  #endif // !(ANKI_IOS_BUILD || ANDROID)
}

Vision::Camera Robot::GetHistoricalCamera(TimeStamp_t t_request) const
{
  RobotPoseStamp p;
  TimeStamp_t t;
  _poseHistory->GetRawPoseAt(t_request, t, p);
  return GetHistoricalCamera(p, t);
}
    
Pose3d Robot::GetHistoricalCameraPose(const RobotPoseStamp& histPoseStamp, TimeStamp_t t) const
{
  // Compute pose from robot body to camera
  // Start with canonical (untilted) headPose
  Pose3d camPose(_headCamPose);
      
  // Rotate that by the given angle
  RotationVector3d Rvec(-histPoseStamp.GetHeadAngle(), Y_AXIS_3D());
  camPose.RotateBy(Rvec);
      
  // Precompose with robot body to neck pose
  camPose.PreComposeWith(_neckPose);
      
  // Set parent pose to be the historical robot pose
  camPose.SetParent(&(histPoseStamp.GetPose()));
      
  camPose.SetName("PoseHistoryCamera_" + std::to_string(t));
      
  return camPose;
}
    
Vision::Camera Robot::GetHistoricalCamera(const RobotPoseStamp& p, TimeStamp_t t) const
{
  Vision::Camera camera(_visionComponentPtr->GetCamera());
      
  // Update the head camera's pose
  camera.SetPose(GetHistoricalCameraPose(p, t));
      
  return camera;
}
   
    
// Flashes a pattern on an active block
void Robot::ActiveObjectLightTest(const ObjectID& objectID) {
  /*
    static int p=0;
    static int currFrame = 0;
    const u32 onColor = 0x00ff00;
    const u32 offColor = 0x0;
    const u8 NUM_FRAMES = 4;
    const u32 LIGHT_PATTERN[NUM_FRAMES][NUM_BLOCK_LEDS] =
    {
    {onColor, offColor, offColor, offColor, onColor, offColor, offColor, offColor}
    ,{offColor, onColor, offColor, offColor, offColor, onColor, offColor, offColor}
    ,{offColor, offColor, offColor, onColor, offColor, offColor, offColor, onColor}
    ,{offColor, offColor, onColor, offColor, offColor, offColor, onColor, offColor}
    };
      
    if (p++ == 10) {
        
    SendSetBlockLights(blockID, LIGHT_PATTERN[currFrame]);
    //SendFlashBlockIDs();
        
    if (++currFrame == NUM_FRAMES) {
    currFrame = 0;
    }
        
    p = 0;
    }
  */
}
    
    
Result Robot::Update(bool ignoreVisionModes)
{
#if(0)
  ActiveBlockLightTest(1);
  return RESULT_OK;
#endif
  GetContext()->GetVizManager()->SendStartRobotUpdate();
      
  /* DEBUG
     const double currentTime_sec = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
     static double lastUpdateTime = currentTime_sec;
       
     const double updateTimeDiff = currentTime_sec - lastUpdateTime;
     if(updateTimeDiff > 1.0) {
     PRINT_NAMED_WARNING("Robot.Update", "Gap between robot update calls = %f\n", updateTimeDiff);
     }
     lastUpdateTime = currentTime_sec;
  */
      
      
  if(_visionComponentPtr->GetCamera().IsCalibrated())
  {
    VisionProcessingResult procResult;
    Result visionResult = _visionComponentPtr->UpdateAllResults(procResult);
    if(RESULT_OK != visionResult) {
      PRINT_NAMED_WARNING("Robot.Update.VisionComponentUpdateFail", "");
      return visionResult;
    }
        
    // Update Block and Face Worlds
    if((ignoreVisionModes || procResult.modesProcessed.IsBitFlagSet(VisionMode::DetectingMarkers)) &&
       RESULT_OK != _blockWorld.Update()) {
      PRINT_NAMED_WARNING("Robot.Update.BlockWorldUpdateFailed", "");
    }
        
    if((ignoreVisionModes || procResult.modesProcessed.IsBitFlagSet(VisionMode::DetectingFaces)) &&
       RESULT_OK != _faceWorld.Update()) {
      PRINT_NAMED_WARNING("Robot.Update.FaceWorldUpdateFailed", "");
    }
        
  } // if (GetCamera().IsCalibrated())
      
  ///////// MemoryMap ///////////
      
  // update navigation memory map
  _blockWorld.UpdateNavMemoryMap();        
      
  ///////// Update the behavior manager ///////////
      
  // TODO: This object encompasses, for the time-being, what some higher level
  // module(s) would do.  e.g. Some combination of game state, build planner,
  // personality planner, etc.
      
  const double currentTime = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
      
  _moodManager->Update(currentTime);
      
  _progressionUnlockComponent->Update();
  
  _tapFilterComponent->Update();
      
  const char* behaviorChooserName = "";
  std::string behaviorDebugStr("<disabled>");

  // https://ankiinc.atlassian.net/browse/COZMO-1242 : moving too early causes pose offset
  static int ticksToPreventBehaviorManagerFromRotatingTooEarly_Jira_1242 = 60;
  if(ticksToPreventBehaviorManagerFromRotatingTooEarly_Jira_1242 <=0)
  {
    _behaviorMgr.Update();
        
    const IBehavior* behavior = _behaviorMgr.GetCurrentBehavior();
    if(behavior != nullptr) {
      if( behavior->IsActing() ) {
        behaviorDebugStr = "A ";
      }
      else {
        behaviorDebugStr = "  ";
      }
      behaviorDebugStr += behavior->GetName();
      const std::string& stateName = behavior->GetStateName();
      if (!stateName.empty())
      {
        behaviorDebugStr += "-" + stateName;
      }
    }
        
    const IBehaviorChooser* behaviorChooser = _behaviorMgr.GetBehaviorChooser();
    if (behaviorChooser)
    {
      behaviorChooserName = behaviorChooser->GetName();
    }
  } else {
    --ticksToPreventBehaviorManagerFromRotatingTooEarly_Jira_1242;
  }
      
  GetContext()->GetVizManager()->SetText(VizManager::BEHAVIOR_STATE, NamedColors::MAGENTA,
                                         "%s", behaviorDebugStr.c_str());

      
  //////// Update Robot's State Machine /////////////
  Result actionResult = _actionList.Update();
  if(actionResult != RESULT_OK) {
    PRINT_NAMED_INFO("Robot.Update", "Robot %d had an action fail.", GetID());
  }        
  //////// Stream Animations /////////
  if(_timeSynced) { // Don't stream anything before we've connected
    Result animStreamResult = _animationStreamer.Update(*this);
    if(animStreamResult != RESULT_OK) {
      PRINT_NAMED_WARNING("Robot.Update",
                          "Robot %d had an animation streaming failure.", GetID());
    }
  }

  /////////// Update NVStorage //////////
  _nvStorageComponent.Update();

  /////////// Update path planning / following ////////////


  if( _driveToPoseStatus != ERobotDriveToPoseStatus::Waiting ) {

    bool forceReplan = _driveToPoseStatus == ERobotDriveToPoseStatus::Error;

    if( _numPlansFinished == _numPlansStarted ) {
      // nothing to do with the planners, so just update the status based on the path following
      if( IsTraversingPath() ) {
        _driveToPoseStatus = ERobotDriveToPoseStatus::FollowingPath;

        if( GetBlockWorld().DidObjectsChange() || forceReplan ) {
          // see if we need to replan, but only bother checking if the world objects changed
          switch( _selectedPathPlanner->ComputeNewPathIfNeeded( GetDriveCenterPose(), forceReplan ) ) {
            case EComputePathStatus::Error:
              _driveToPoseStatus = ERobotDriveToPoseStatus::Error;
              AbortDrivingToPose();
              PRINT_NAMED_INFO("Robot.Update.Replan.Fail", "ComputeNewPathIfNeeded returned failure!");
              break;

            case EComputePathStatus::Running:
              _numPlansStarted++;
              PRINT_NAMED_INFO("Robot.Update.Replan.Running", "ComputeNewPathIfNeeded running");
              _driveToPoseStatus = ERobotDriveToPoseStatus::Replanning;
              break;

            case EComputePathStatus::NoPlanNeeded:
              // leave status as following, don't update plan attempts since no new planning is needed
              break;
          }
        }
      }
      else {
        _driveToPoseStatus = ERobotDriveToPoseStatus::Waiting;
      }
    }
    else {
      // we are waiting on a plan to currently compute
      // TODO:(bn) timeout logic might fit well here?
      switch( _selectedPathPlanner->CheckPlanningStatus() ) {
        case EPlannerStatus::Error:
          _driveToPoseStatus =  ERobotDriveToPoseStatus::Error;
          PRINT_NAMED_INFO("Robot.Update.Planner.Error", "Running planner returned error status");
          AbortDrivingToPose();
          _numPlansFinished = _numPlansStarted;
          break;

        case EPlannerStatus::Running:
          // status should stay the same, but double check it
          if( _driveToPoseStatus != ERobotDriveToPoseStatus::ComputingPath &&
              _driveToPoseStatus != ERobotDriveToPoseStatus::Replanning) {
            PRINT_NAMED_WARNING("Robot.Planning.StatusError.Running",
                                "Status was invalid, setting to ComputePath");
            _driveToPoseStatus =  ERobotDriveToPoseStatus::ComputingPath;
          }
          break;

        case EPlannerStatus::CompleteWithPlan: {
          PRINT_NAMED_INFO("Robot.Update.Planner.CompleteWithPlan", "Running planner complete with a plan");

          _driveToPoseStatus = ERobotDriveToPoseStatus::FollowingPath;
          _numPlansFinished = _numPlansStarted;

          size_t selectedPoseIdx;
          Planning::Path newPath;

          _selectedPathPlanner->GetCompletePath(GetDriveCenterPose(), newPath, selectedPoseIdx, &_pathMotionProfile);
          ExecutePath(newPath, _usingManualPathSpeed);

          if( _plannerSelectedPoseIndexPtr != nullptr ) {
            // When someone called StartDrivingToPose with multiple possible poses, they had an option to pass
            // in a pointer to be set when we know which pose was selected by the planner. If that pointer is
            // non-null, set it now, then clear the pointer so we won't set it again

            // TODO:(bn) think about re-planning, here, what if replanning wanted to switch targets? For now,
            // replanning will always chose the same target pose, which should be OK for now
            *_plannerSelectedPoseIndexPtr = selectedPoseIdx;
            _plannerSelectedPoseIndexPtr = nullptr;
          }
          break;
        }


        case EPlannerStatus::CompleteNoPlan:
          PRINT_NAMED_INFO("Robot.Update.Planner.CompleteNoPlan", "Running planner complete with no plan");
          _driveToPoseStatus = ERobotDriveToPoseStatus::Waiting;
          _numPlansFinished = _numPlansStarted;
          break;
      }
    }
  }
      
  /////////// Update discovered active objects //////
  for (auto iter = _discoveredObjects.begin(); iter != _discoveredObjects.end();) {
    // Note not incrementing the iterator here
    const auto& obj = *iter;
    const int32_t maxTimestamp =
      10 * Util::numeric_cast<int32_t>(ActiveObjectConstants::ACTIVE_OBJECT_DISCOVERY_PERIOD_MS);
    const int32_t timeStampDiff =
      Util::numeric_cast<int32_t>(GetLastMsgTimestamp()) -
      Util::numeric_cast<int32_t>(obj.second.lastDiscoveredTimeStamp);
    if (timeStampDiff > maxTimestamp) {
      if (_enableDiscoveredObjectsBroadcasting) {
        PRINT_NAMED_INFO("Robot.Update.ObjectUndiscovered",
                         "FactoryID 0x%x (type: %s, lastObservedTime %d, currTime %d)",
                         obj.first, EnumToString(obj.second.objectType),
                         obj.second.lastDiscoveredTimeStamp, GetLastMsgTimestamp());

        // Send unavailable message for this object
        ExternalInterface::ObjectUnavailable m(obj.first);
        Broadcast(ExternalInterface::MessageEngineToGame(std::move(m)));
      }
      iter = _discoveredObjects.erase(iter);
    }
    else {
      ++iter;
    }
  }

      
  // Connect to objects requested via ConnectToObjects
  ConnectToRequestedObjects();
      
  /////////// Update visualization ////////////
      
  // Draw observed markers, but only if images are being streamed
  _blockWorld.DrawObsMarkers();
      
  // Draw All Objects by calling their Visualize() methods.
  _blockWorld.DrawAllObjects();
      
  // Nav memory map
  _blockWorld.DrawNavMemoryMap();
      
  // Always draw robot w.r.t. the origin, not in its current frame
  Pose3d robotPoseWrtOrigin = GetPose().GetWithRespectToOrigin();
      
  // Triangle pose marker
  GetContext()->GetVizManager()->DrawRobot(GetID(), robotPoseWrtOrigin);
      
  // Full Webots CozmoBot model
  GetContext()->GetVizManager()->DrawRobot(GetID(), robotPoseWrtOrigin, GetHeadAngle(), GetLiftAngle());
      
  // Robot bounding box
  static const ColorRGBA ROBOT_BOUNDING_QUAD_COLOR(0.0f, 0.8f, 0.0f, 0.75f);
      
  using namespace Quad;
  Quad2f quadOnGround2d = GetBoundingQuadXY(robotPoseWrtOrigin);
  const f32 zHeight = robotPoseWrtOrigin.GetTranslation().z() + WHEEL_RAD_TO_MM;
  Quad3f quadOnGround3d(Point3f(quadOnGround2d[TopLeft].x(),     quadOnGround2d[TopLeft].y(),     zHeight),
                        Point3f(quadOnGround2d[BottomLeft].x(),  quadOnGround2d[BottomLeft].y(),  zHeight),
                        Point3f(quadOnGround2d[TopRight].x(),    quadOnGround2d[TopRight].y(),    zHeight),
                        Point3f(quadOnGround2d[BottomRight].x(), quadOnGround2d[BottomRight].y(), zHeight));
    
  GetContext()->GetVizManager()->DrawRobotBoundingBox(GetID(), quadOnGround3d, ROBOT_BOUNDING_QUAD_COLOR);
      
  /*
  // Draw 3d bounding box
  Vec3f vizTranslation = GetPose().GetTranslation();
  vizTranslation.z() += 0.5f*ROBOT_BOUNDING_Z;
  Pose3d vizPose(GetPose().GetRotation(), vizTranslation);
      
  GetContext()->GetVizManager()->DrawCuboid(999, {ROBOT_BOUNDING_X, ROBOT_BOUNDING_Y, ROBOT_BOUNDING_Z},
  vizPose, ROBOT_BOUNDING_QUAD_COLOR);
  */
      
  GetContext()->GetVizManager()->SendEndRobotUpdate();

  // update time since last image received
  _timeSinceLastImage_s = std::max(0.0, currentTime - _lastImageRecvTime);
      
  // Sending debug string to game and viz
  char buffer [128];

  const float imageProcRate = 1000.0f / _visionComponentPtr->GetProcessingPeriod_ms();
      
  // So we can have an arbitrary number of data here that is likely to change want just hash it all
  // together if anything changes without spamming
  snprintf(buffer, sizeof(buffer),
           "%c%c%c%c %2dHz %s%s ",
           GetMoveComponent().IsLiftMoving() ? 'L' : ' ',
           GetMoveComponent().IsHeadMoving() ? 'H' : ' ',
           GetMoveComponent().IsMoving() ? 'B' : ' ',
           IsCarryingObject() ? 'C' : ' ',
           // SimpleMoodTypeToString(GetMoodManager().GetSimpleMood()),
           // _movementComponent.AreAnyTracksLocked((u8)AnimTrackFlag::LIFT_TRACK) ? 'L' : ' ',
           // _movementComponent.AreAnyTracksLocked((u8)AnimTrackFlag::HEAD_TRACK) ? 'H' : ' ',
           // _movementComponent.AreAnyTracksLocked((u8)AnimTrackFlag::BODY_TRACK) ? 'B' : ' ',
           (u8)MIN(((u8)imageProcRate), u8_MAX),
           behaviorChooserName,
           behaviorDebugStr.c_str());
      
  std::hash<std::string> hasher;
  size_t curr_hash = hasher(std::string(buffer));
  if( _lastDebugStringHash != curr_hash )
  {
    SendDebugString(buffer);
    _lastDebugStringHash = curr_hash;
  }
      
  // Update ChargerPlatform
  ObservableObject* charger =
    dynamic_cast<ObservableObject*>(GetBlockWorld().GetObjectByIDandFamily(_chargerID, ObjectFamily::Charger));
  if( charger && charger->IsPoseStateKnown() && !IsPickedUp() )
  {
    // This state is useful for knowing not to play a cliff react when just driving off the charger.
    bool isOnChargerPlatform = charger->GetBoundingQuadXY().Intersects(GetBoundingQuadXY());
    if( isOnChargerPlatform != _isOnChargerPlatform)
    {
      _isOnChargerPlatform = isOnChargerPlatform;
      Broadcast(
        ExternalInterface::MessageEngineToGame(
          ExternalInterface::RobotOnChargerPlatformEvent(_isOnChargerPlatform)));
    }
  }

  _lightsComponent->Update();


  if( kDebugPossibleBlockInteraction ) {
    // print a bunch of info helpful for debugging block states
    for( const auto& objByTypePair : GetBlockWorld().GetExistingObjectsByFamily(ObjectFamily::LightCube) ) {
      for( const auto& objByIdPair : objByTypePair.second ) {
        ObjectID objID = objByIdPair.first;
        const ObservableObject* obj = objByIdPair.second;

        const ObservableObject* topObj = GetBlockWorld().FindObjectOnTopOf(*obj, STACKED_HEIGHT_TOL_MM);
        Pose3d relPose;
        bool gotRelPose = obj->GetPose().GetWithRespectTo(GetPose(), relPose);

        const char* axisStr = "";
        switch( obj->GetPose().GetRotationMatrix().GetRotatedParentAxis<'Z'>() ) {
          case AxisName::X_POS: axisStr="+X"; break;
          case AxisName::X_NEG: axisStr="-X"; break;
          case AxisName::Y_POS: axisStr="+Y"; break;
          case AxisName::Y_NEG: axisStr="-Y"; break;
          case AxisName::Z_POS: axisStr="+Z"; break;
          case AxisName::Z_NEG: axisStr="-Z"; break;
        }
              
        PRINT_NAMED_DEBUG("Robot.ObjectInteractionState",
                          "block:%d poseState:%8s moving?%d RestingFlat?%d carried?%d poseWRT?%d objOnTop:%d"
                          " z=%6.2f UpAxis:%s CanStack?%d CanPickUp?%d FromGround?%d",
                          objID.GetValue(),
                          obj->PoseStateToString( obj->GetPoseState() ),
                          obj->IsMoving(),
                          obj->IsRestingFlat(),
                          (IsCarryingObject() && GetCarryingObject() == obj->GetID()),
                          gotRelPose,
                          topObj ? topObj->GetID().GetValue() : -1,
                          relPose.GetTranslation().z(),
                          axisStr,
                          CanStackOnTopOfObject(*obj),
                          CanPickUpObject(*obj),
                          CanPickUpObjectFromGround(*obj));                              
      }
    }
  }
      
  return RESULT_OK;
      
} // Update()
      
static bool IsValidHeadAngle(f32 head_angle, f32* clipped_valid_head_angle)
{
  if(head_angle < MIN_HEAD_ANGLE - HEAD_ANGLE_LIMIT_MARGIN) {
    //PRINT_NAMED_WARNING("Robot.HeadAngleOOB", "Head angle (%f rad) too small.\n", head_angle);
    if (clipped_valid_head_angle) {
      *clipped_valid_head_angle = MIN_HEAD_ANGLE;
    }
    return false;
  }
  else if(head_angle > MAX_HEAD_ANGLE + HEAD_ANGLE_LIMIT_MARGIN) {
    //PRINT_NAMED_WARNING("Robot.HeadAngleOOB", "Head angle (%f rad) too large.\n", head_angle);
    if (clipped_valid_head_angle) {
      *clipped_valid_head_angle = MAX_HEAD_ANGLE;
    }
    return false;
  }
      
  if (clipped_valid_head_angle) {
    *clipped_valid_head_angle = head_angle;
  }
  return true;
      
} // IsValidHeadAngle()

    
void Robot::SetNewPose(const Pose3d& newPose)
{
  SetPose(newPose.GetWithRespectToOrigin());
  ++_frameId;
      
  const TimeStamp_t timeStamp = _poseHistory->GetNewestTimeStamp();
      
  SendAbsLocalizationUpdate(_pose, timeStamp, _frameId);
}
    
void Robot::SetPose(const Pose3d &newPose)
{
  // Update our current pose and keep the name consistent
  const std::string name = _pose.GetName();
  _pose = newPose;
  _pose.SetName(name);
      
  ComputeDriveCenterPose(_pose, _driveCenterPose);
      
} // SetPose()
    
Pose3d Robot::GetCameraPose(f32 atAngle) const
{
  // Start with canonical (untilted) headPose
  Pose3d newHeadPose(_headCamPose);
      
  // Rotate that by the given angle
  RotationVector3d Rvec(-atAngle, Y_AXIS_3D());
  newHeadPose.RotateBy(Rvec);
  newHeadPose.SetName("Camera");

  return newHeadPose;
} // GetCameraHeadPose()
    
void Robot::SetHeadAngle(const f32& angle)
{
  if (!IsValidHeadAngle(angle, &_currentHeadAngle)) {
    PRINT_NAMED_WARNING("Robot.GetCameraHeadPose.HeadAngleOOB",
                        "Angle %.3frad / %.1f (TODO: Send correction or just recalibrate?)\n",
                        angle, RAD_TO_DEG(angle));
  }
      
  _visionComponentPtr->GetCamera().SetPose(GetCameraPose(_currentHeadAngle));
      
} // SetHeadAngle()
    
    

void Robot::ComputeLiftPose(const f32 atAngle, Pose3d& liftPose)
{
  // Reset to canonical position
  liftPose.SetRotation(atAngle, Y_AXIS_3D());
  liftPose.SetTranslation({LIFT_ARM_LENGTH, 0.f, 0.f});
      
  // Rotate to the given angle
  RotationVector3d Rvec(-atAngle, Y_AXIS_3D());
  liftPose.RotateBy(Rvec);
}
    
void Robot::SetLiftAngle(const f32& angle)
{
  // TODO: Add lift angle limits?
  _currentLiftAngle = angle;
      
  Robot::ComputeLiftPose(_currentLiftAngle, _liftPose);

  CORETECH_ASSERT(_liftPose.GetParent() == &_liftBasePose);
}
    
f32 Robot::GetPitchAngle() const
{
  return _pitchAngle;
}
        
void Robot::SelectPlanner(const Pose3d& targetPose)
{
  Pose2d target2d(targetPose);
  Pose2d start2d(GetPose());

  float distSquared = pow(target2d.GetX() - start2d.GetX(), 2) + pow(target2d.GetY() - start2d.GetY(), 2);

  if(distSquared < MAX_DISTANCE_FOR_SHORT_PLANNER * MAX_DISTANCE_FOR_SHORT_PLANNER) {

    Radians finalAngleDelta = targetPose.GetRotationAngle<'Z'>() - GetDriveCenterPose().GetRotationAngle<'Z'>();
    const bool withinFinalAngleTolerance = finalAngleDelta.getAbsoluteVal().ToFloat() <=
      2 * PLANNER_MAINTAIN_ANGLE_THRESHOLD;

    Radians initialTurnAngle = atan2( target2d.GetY() - GetDriveCenterPose().GetTranslation().y(),
                                      target2d.GetX() - GetDriveCenterPose().GetTranslation().x()) -
      GetDriveCenterPose().GetRotationAngle<'Z'>();

    const bool initialTurnAngleLarge = initialTurnAngle.getAbsoluteVal().ToFloat() >
      0.5 * PLANNER_MAINTAIN_ANGLE_THRESHOLD;

    const bool farEnoughAwayForMinAngle = distSquared > std::pow( MIN_DISTANCE_FOR_MINANGLE_PLANNER, 2);

    // if we would need to turn fairly far, but our current angle is fairly close to the goal, use the
    // planner which backs up first to minimize the turn
    if( withinFinalAngleTolerance && initialTurnAngleLarge && farEnoughAwayForMinAngle ) {
      PRINT_NAMED_INFO("Robot.SelectPlanner.ShortMinAngle",
                       "distance^2 is %f, angleDelta is %f, intiialTurnAngle is %f, selecting short min_angle planner",
                       distSquared,
                       finalAngleDelta.getAbsoluteVal().ToFloat(),
                       initialTurnAngle.getAbsoluteVal().ToFloat());
      _selectedPathPlanner = _shortMinAnglePathPlanner;
    }
    else {
      PRINT_NAMED_INFO("Robot.SelectPlanner.Short",
                       "distance^2 is %f, angleDelta is %f, intiialTurnAngle is %f, selecting short planner",
                       distSquared,
                       finalAngleDelta.getAbsoluteVal().ToFloat(),
                       initialTurnAngle.getAbsoluteVal().ToFloat());
      _selectedPathPlanner = _shortPathPlanner;
    }
  }
  else {
    PRINT_NAMED_INFO("Robot.SelectPlanner.Long", "distance^2 is %f, selecting long planner", distSquared);
    _selectedPathPlanner = _longPathPlanner;
  }
}

void Robot::SelectPlanner(const std::vector<Pose3d>& targetPoses)
{
  if( ! targetPoses.empty() ) {
    size_t closest = IPathPlanner::ComputeClosestGoalPose(GetDriveCenterPose(), targetPoses);
    SelectPlanner(targetPoses[closest]);
  }
}

Result Robot::StartDrivingToPose(const Pose3d& targetPose,
                                 const PathMotionProfile motionProfile,
                                 bool useManualSpeed)
{
  _usingManualPathSpeed = useManualSpeed;

  Pose3d targetPoseWrtOrigin;
  if(targetPose.GetWithRespectTo(*GetWorldOrigin(), targetPoseWrtOrigin) == false) {
    PRINT_NAMED_ERROR("Robot.StartDrivingToPose.OriginMisMatch",
                      "Could not get target pose w.r.t. robot %d's origin.", GetID());
    _driveToPoseStatus = ERobotDriveToPoseStatus::Error;
    return RESULT_FAIL;
  }

  SelectPlanner(targetPoseWrtOrigin);

  // Compute drive center pose of given target robot pose
  Pose3d targetDriveCenterPose;
  ComputeDriveCenterPose(targetPoseWrtOrigin, targetDriveCenterPose);

  // Compute drive center pose for start pose
  EComputePathStatus status = _selectedPathPlanner->ComputePath(GetDriveCenterPose(), targetDriveCenterPose);
  if( status == EComputePathStatus::Error ) {
    _driveToPoseStatus = ERobotDriveToPoseStatus::Error;
    return RESULT_FAIL;
  }

  if( IsTraversingPath() ) {
    _driveToPoseStatus = ERobotDriveToPoseStatus::FollowingPath;
  }
  else {
    _driveToPoseStatus = ERobotDriveToPoseStatus::ComputingPath;
  }

  _numPlansStarted++;
      
  _pathMotionProfile = motionProfile;

  return RESULT_OK;
}

Result Robot::StartDrivingToPose(const std::vector<Pose3d>& poses,
                                 const PathMotionProfile motionProfile,
                                 size_t* selectedPoseIndexPtr,
                                 bool useManualSpeed)
{
  _usingManualPathSpeed = useManualSpeed;
  _plannerSelectedPoseIndexPtr = selectedPoseIndexPtr;

  SelectPlanner(poses);

  // Compute drive center pose for start pose and goal poses
  std::vector<Pose3d> targetDriveCenterPoses(poses.size());
  for (int i=0; i< poses.size(); ++i) {
    ComputeDriveCenterPose(poses[i], targetDriveCenterPoses[i]);
  }

  EComputePathStatus status = _selectedPathPlanner->ComputePath(GetDriveCenterPose(), targetDriveCenterPoses);
  if( status == EComputePathStatus::Error ) {
    _driveToPoseStatus = ERobotDriveToPoseStatus::Error;

    return RESULT_FAIL;
  }

  if( IsTraversingPath() ) {
    _driveToPoseStatus = ERobotDriveToPoseStatus::FollowingPath;
  }
  else {
    _driveToPoseStatus = ERobotDriveToPoseStatus::ComputingPath;
  }

  _numPlansStarted++;

  _pathMotionProfile = motionProfile;
      
  return RESULT_OK;
}

ERobotDriveToPoseStatus Robot::CheckDriveToPoseStatus() const
{
  return _driveToPoseStatus;
}
    
Result Robot::PlaceObjectOnGround(const bool useManualSpeed)
{
  if(!IsCarryingObject()) {
    PRINT_NAMED_ERROR("Robot.PlaceObjectOnGround.NotCarryingObject",
                      "Robot told to place object on ground, but is not carrying an object.");
    return RESULT_FAIL;
  }
      
  _usingManualPathSpeed = useManualSpeed;
  _lastPickOrPlaceSucceeded = false;
      
  return SendRobotMessage<Anki::Cozmo::PlaceObjectOnGround>(0, 0, 0,
                                                            DEFAULT_PATH_MOTION_PROFILE.speed_mmps,
                                                            DEFAULT_PATH_MOTION_PROFILE.accel_mmps2,
                                                            DEFAULT_PATH_MOTION_PROFILE.decel_mmps2,
                                                            useManualSpeed);
}
    
void Robot::ShiftEyes(AnimationStreamer::Tag& tag, f32 xPix, f32 yPix,
                      TimeStamp_t duration_ms, const std::string& name)
{
  ProceduralFace procFace;
  ProceduralFace::Value xMin=0, xMax=0, yMin=0, yMax=0;
  procFace.GetEyeBoundingBox(xMin, xMax, yMin, yMax);
  procFace.LookAt(xPix, yPix,
                  std::max(xMin, ProceduralFace::WIDTH-xMax),
                  std::max(yMin, ProceduralFace::HEIGHT-yMax),
                  1.1f, 0.85f, 0.1f);
      
  ProceduralFaceKeyFrame keyframe(procFace, duration_ms);
      
  if(AnimationStreamer::NotAnimatingTag == tag) {
    AnimationStreamer::FaceTrack faceTrack;
    if(duration_ms > 0) {
      // Add an initial no-adjustment frame so we have something to interpolate
      // from on our way to the specified shift
      faceTrack.AddKeyFrameToBack(ProceduralFaceKeyFrame());
    }
    faceTrack.AddKeyFrameToBack(std::move(keyframe));
    tag = GetAnimationStreamer().AddPersistentFaceLayer(name, std::move(faceTrack));
  } else {
    GetAnimationStreamer().AddToPersistentFaceLayer(tag, std::move(keyframe));
  }
}
    
Result Robot::PlaySound(const std::string& soundName, u8 numLoops, u8 volume)
{
  Broadcast(ExternalInterface::MessageEngineToGame(ExternalInterface::PlaySound(soundName, numLoops, volume)));
      
  //CozmoEngineSignals::PlaySoundForRobotSignal().emit(GetID(), soundName, numLoops, volume);
  return RESULT_OK;
} // PlaySound()
      
      
void Robot::StopSound()
{
  Broadcast(ExternalInterface::MessageEngineToGame(ExternalInterface::StopSound()));
} // StopSound()


void Robot::LoadEmotionEvents()
{
  const auto& emotionEventData = _context->GetDataLoader()->GetEmotionEventJsons();
  for (const auto& fileJsonPair : emotionEventData)
  {
    const auto& filename = fileJsonPair.first;
    const auto& eventJson = fileJsonPair.second;
    if (!eventJson.empty() && _moodManager->LoadEmotionEvents(eventJson))
    {
      PRINT_NAMED_DEBUG("Robot.LoadEmotionEvents", "Loaded '%s'", filename.c_str());
    }
    else
    {
      PRINT_NAMED_WARNING("Robot.LoadEmotionEvents", "Failed to read '%s'", filename.c_str());
    }
  }
}

void Robot::LoadBehaviors()
{
  const auto& behaviorData = _context->GetDataLoader()->GetBehaviorJsons();
  for( const auto& fileJsonPair : behaviorData )
  {
    const auto& filename = fileJsonPair.first;
    const auto& behaviorJson = fileJsonPair.second;
    if (!behaviorJson.empty())
    {
      // PRINT_NAMED_DEBUG("Robot.LoadBehavior", "Loading '%s'", fullFileName.c_str());
      const Result ret = _behaviorMgr.CreateBehaviorFromConfiguration(behaviorJson);
      if ( ret != RESULT_OK ) {
        PRINT_NAMED_ERROR("Robot.LoadBehavior.CreateFailed", "Failed to create behavior from '%s'", filename.c_str());
      }
    }
    else
    {
      PRINT_NAMED_WARNING("Robot.LoadBehavior", "Failed to read '%s'", filename.c_str());
    }
    // don't print anything if we read an empty json
  }
}

Result Robot::SyncTime()
{
  _timeSynced = false;
  _poseHistory->Clear();
      
  return SendSyncTime();
}
    
Result Robot::LocalizeToObject(const ObservableObject* seenObject,
                               ObservableObject* existingObject)
{
  Result lastResult = RESULT_OK;
      
  if(existingObject == nullptr) {
    PRINT_NAMED_ERROR("Robot.LocalizeToObject.ExistingObjectPieceNullPointer", "");
    return RESULT_FAIL;
  }
      
  if(!existingObject->CanBeUsedForLocalization()) {
    PRINT_NAMED_ERROR("Robot.LocalizeToObject.UnlocalizedObject",
                      "Refusing to localize to object %d, which claims not to be localizable.",
                      existingObject->GetID().GetValue());
    return RESULT_FAIL;
  }
      
  /* Useful for Debug:
     PRINT_NAMED_INFO("Robot.LocalizeToMat.MatSeenChain",
     "%s\n", matSeen->GetPose().GetNamedPathToOrigin(true).c_str());
       
     PRINT_NAMED_INFO("Robot.LocalizeToMat.ExistingMatChain",
     "%s\n", existingMatPiece->GetPose().GetNamedPathToOrigin(true).c_str());
  */
      
  RobotPoseStamp* posePtr = nullptr;
  Pose3d robotPoseWrtObject;
  float  headAngle;
  float  liftAngle;
  if(nullptr == seenObject)
  {
    if(false == GetPose().GetWithRespectTo(existingObject->GetPose(), robotPoseWrtObject)) {
      PRINT_NAMED_ERROR("Robot.LocalizeToObject.ExistingObjectOriginMismatch",
                        "Could not get robot pose w.r.t. to existing object %d.",
                        existingObject->GetID().GetValue());
      return RESULT_FAIL;
    }
    liftAngle = GetLiftAngle();
    headAngle = GetHeadAngle();
  } else {
    // Get computed RobotPoseStamp at the time the object was observed.
    if ((lastResult = GetComputedPoseAt(seenObject->GetLastObservedTime(), &posePtr)) != RESULT_OK) {
      PRINT_NAMED_ERROR("Robot.LocalizeToObject.CouldNotFindHistoricalPose",
                        "Time %d", seenObject->GetLastObservedTime());
      return lastResult;
    }
        
    // The computed historical pose is always stored w.r.t. the robot's world
    // origin and parent chains are lost. Re-connect here so that GetWithRespectTo
    // will work correctly
    Pose3d robotPoseAtObsTime = posePtr->GetPose();
    robotPoseAtObsTime.SetParent(_worldOrigin);
        
    // Get the pose of the robot with respect to the observed object
    if(robotPoseAtObsTime.GetWithRespectTo(seenObject->GetPose(), robotPoseWrtObject) == false) {
      PRINT_NAMED_ERROR("Robot.LocalizeToObject.ObjectPoseOriginMisMatch",
                        "Could not get RobotPoseStamp w.r.t. seen object pose.");
      return RESULT_FAIL;
    }
        
    liftAngle = posePtr->GetLiftAngle();
    headAngle = posePtr->GetHeadAngle();
  }
      
  // Make the computed robot pose use the existing mat piece as its parent
  robotPoseWrtObject.SetParent(&existingObject->GetPose());
  //robotPoseWrtMat.SetName(std::string("Robot_") + std::to_string(robot->GetID()));
      
# if 0
  // Don't snap to horizontal or discrete Z levels when we see a mat marker
  // while on a ramp
  if(IsOnRamp() == false)
  {
    // If there is any significant rotation, make sure that it is roughly
    // around the Z axis
    Radians rotAngle;
    Vec3f rotAxis;
    robotPoseWrtObject.GetRotationVector().GetAngleAndAxis(rotAngle, rotAxis);
        
    if(std::abs(rotAngle.ToFloat()) > DEG_TO_RAD(5) && !AreUnitVectorsAligned(rotAxis, Z_AXIS_3D(), DEG_TO_RAD(15))) {
      PRINT_NAMED_WARNING("Robot.LocalizeToObject.OutOfPlaneRotation",
                          "Refusing to localize to %s because "
                          "Robot %d's Z axis would not be well aligned with the world Z axis. "
                          "(angle=%.1fdeg, axis=(%.3f,%.3f,%.3f)",
                          existingObject->GetType().GetName().c_str(), GetID(),
                          rotAngle.getDegrees(), rotAxis.x(), rotAxis.y(), rotAxis.z());
      return RESULT_FAIL;
    }
        
    // Snap to purely horizontal rotation
    // TODO: Snap to surface of mat?
    /*
      if(existingMatPiece->IsPoseOn(robotPoseWrtObject, 0, 10.f)) {
      Vec3f robotPoseWrtObject_trans = robotPoseWrtObject.GetTranslation();
      robotPoseWrtObject_trans.z() = existingObject->GetDrivingSurfaceHeight();
      robotPoseWrtObject.SetTranslation(robotPoseWrtObject_trans);
      }
    */
    robotPoseWrtObject.SetRotation( robotPoseWrtObject.GetRotationAngle<'Z'>(), Z_AXIS_3D() );
        
  } // if robot is on ramp
# endif
      
  // Add the new vision-based pose to the robot's history. Note that we use
  // the pose w.r.t. the origin for storing poses in history.
  Pose3d robotPoseWrtOrigin = robotPoseWrtObject.GetWithRespectToOrigin();
      
  if(IsLocalized()) {
    // Filter Z so it doesn't change too fast (unless we are switching from
    // delocalized to localized)
        
    // Make z a convex combination of new and previous value
    static const f32 zUpdateWeight = 0.1f; // weight of new value (previous gets weight of 1 - this)
    Vec3f T = robotPoseWrtOrigin.GetTranslation();
    T.z() = (zUpdateWeight*robotPoseWrtOrigin.GetTranslation().z() +
             (1.f - zUpdateWeight) * GetPose().GetTranslation().z());
    robotPoseWrtOrigin.SetTranslation(T);
  }
      
  if(nullptr != seenObject)
  {
    //
    if((lastResult = AddVisionOnlyPoseToHistory(existingObject->GetLastObservedTime(),
                                                robotPoseWrtOrigin.GetTranslation().x(),
                                                robotPoseWrtOrigin.GetTranslation().y(),
                                                robotPoseWrtOrigin.GetTranslation().z(),
                                                robotPoseWrtOrigin.GetRotationAngle<'Z'>().ToFloat(),
                                                headAngle, liftAngle)) != RESULT_OK)
    {
      PRINT_NAMED_ERROR("Robot.LocalizeToObject.FailedAddingVisionOnlyPoseToHistory", "");
      return lastResult;
    }
  }
      
  // If the robot's world origin is about to change by virtue of being localized
  // to existingObject, rejigger things so anything seen while the robot was
  // rooted to this world origin will get updated to be w.r.t. the new origin.
  if(_worldOrigin != &existingObject->GetPose().FindOrigin())
  {
    PRINT_NAMED_INFO("Robot.LocalizeToObject.RejiggeringOrigins",
                     "Robot %d's current world origin is %s, about to "
                     "localize to world origin %s.",
                     GetID(),
                     _worldOrigin->GetName().c_str(),
                     existingObject->GetPose().FindOrigin().GetName().c_str());
        
    // Store the current origin we are about to change so that we can
    // find objects that are using it below
    const Pose3d* oldOrigin = _worldOrigin;
        
    // Update the origin to which _worldOrigin currently points to contain
    // the transformation from its current pose to what is about to be the
    // robot's new origin.
    _worldOrigin->SetRotation(GetPose().GetRotation());
    _worldOrigin->SetTranslation(GetPose().GetTranslation());
    _worldOrigin->Invert();
    _worldOrigin->PreComposeWith(robotPoseWrtOrigin);
    _worldOrigin->SetParent(&robotPoseWrtObject.FindOrigin());
    _worldOrigin->SetName( _worldOrigin->GetName() + "_REJ");
        
    assert(_worldOrigin->IsOrigin() == false);
        
    // Now that the previous origin is hooked up to the new one (which is
    // now the old one's parent), point the worldOrigin at the new one.
    _worldOrigin = const_cast<Pose3d*>(_worldOrigin->GetParent()); // TODO: Avoid const cast?
        
    // Now we need to go through all objects whose poses have been adjusted
    // by this origin switch and notify the outside world of the change.
    _blockWorld.UpdateObjectOrigins(oldOrigin, _worldOrigin);

    // after updating all block world objects, no one should be pointing to the oldOrigin
    FlattenOutOrigins();
        
  } // if(_worldOrigin != &existingObject->GetPose().FindOrigin())
      
      
  if(nullptr != posePtr)
  {
    // Update the computed historical pose as well so that subsequent block
    // pose updates use obsMarkers whose camera's parent pose is correct.
    // Note again that we store the pose w.r.t. the origin in history.
    // TODO: Should SetPose() do the flattening w.r.t. origin?
    posePtr->SetPose(GetPoseFrameID(), robotPoseWrtOrigin, liftAngle, liftAngle);
  }

      
  // Compute the new "current" pose from history which uses the
  // past vision-based "ground truth" pose we just computed.
  assert(&existingObject->GetPose().FindOrigin() == _worldOrigin);
  assert(_worldOrigin != nullptr);
  if(UpdateCurrPoseFromHistory(*_worldOrigin) == false) {
    PRINT_NAMED_ERROR("Robot.LocalizeToObject.FailedUpdateCurrPoseFromHistory", "");
    return RESULT_FAIL;
  }
      
  // Mark the robot as now being localized to this object
  // NOTE: this should be _after_ calling AddVisionOnlyPoseToHistory, since
  //    that function checks whether the robot is already localized
  lastResult = SetLocalizedTo(existingObject);
  if(RESULT_OK != lastResult) {
    PRINT_NAMED_ERROR("Robot.LocalizeToObject.SetLocalizedToFail", "");
    return lastResult;
  }
      
  // Overly-verbose. Use for debugging localization issues
  /*
    PRINT_NAMED_INFO("Robot.LocalizeToObject",
    "Using %s object %d to localize robot %d at (%.3f,%.3f,%.3f), %.1fdeg@(%.2f,%.2f,%.2f), frameID=%d\n",
    ObjectTypeToString(existingObject->GetType()),
    existingObject->GetID().GetValue(), GetID(),
    GetPose().GetTranslation().x(),
    GetPose().GetTranslation().y(),
    GetPose().GetTranslation().z(),
    GetPose().GetRotationAngle<'Z'>().getDegrees(),
    GetPose().GetRotationAxis().x(),
    GetPose().GetRotationAxis().y(),
    GetPose().GetRotationAxis().z(),
    GetPoseFrameID());
  */
      
  // Send the ground truth pose that was computed instead of the new current
  // pose and let the robot deal with updating its current pose based on the
  // history that it keeps.
  SendAbsLocalizationUpdate();
      
  return RESULT_OK;
} // LocalizeToObject()
    
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Robot::FlattenOutOrigins()
{
  for(auto& originIter : _poseOrigins)
  {
    // if this origin has a parent and it's not the current worldOrigin, we want to update
    // this origin to be a direct child of the current origin
    if ( (originIter.GetParent() != nullptr) && (originIter.GetParent() != _worldOrigin) )
    {
      // get WRT current origin, and if we can (because our parent's origin is the current worldOrigin), then assign
      Pose3d iterWRTCurrentOrigin;
      if ( originIter.GetWithRespectTo(*_worldOrigin, iterWRTCurrentOrigin) )
      {
        const std::string& newName = originIter.GetName() + "_FLT";
        originIter = iterWRTCurrentOrigin;
        originIter.SetName( newName );
      }
    }
  }
}
    
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Result Robot::LocalizeToMat(const MatPiece* matSeen, MatPiece* existingMatPiece)
{
  Result lastResult;
      
  if(matSeen == nullptr) {
    PRINT_NAMED_ERROR("Robot.LocalizeToMat.MatSeenNullPointer", "");
    return RESULT_FAIL;
  } else if(existingMatPiece == nullptr) {
    PRINT_NAMED_ERROR("Robot.LocalizeToMat.ExistingMatPieceNullPointer", "");
    return RESULT_FAIL;
  }
      
  /* Useful for Debug:
     PRINT_NAMED_INFO("Robot.LocalizeToMat.MatSeenChain",
     "%s\n", matSeen->GetPose().GetNamedPathToOrigin(true).c_str());
      
     PRINT_NAMED_INFO("Robot.LocalizeToMat.ExistingMatChain",
     "%s\n", existingMatPiece->GetPose().GetNamedPathToOrigin(true).c_str());
  */
      
  // Get computed RobotPoseStamp at the time the mat was observed.
  RobotPoseStamp* posePtr = nullptr;
  if ((lastResult = GetComputedPoseAt(matSeen->GetLastObservedTime(), &posePtr)) != RESULT_OK) {
    PRINT_NAMED_ERROR("Robot.LocalizeToMat.CouldNotFindHistoricalPose", "Time %d", matSeen->GetLastObservedTime());
    return lastResult;
  }
      
  // The computed historical pose is always stored w.r.t. the robot's world
  // origin and parent chains are lost. Re-connect here so that GetWithRespectTo
  // will work correctly
  Pose3d robotPoseAtObsTime = posePtr->GetPose();
  robotPoseAtObsTime.SetParent(_worldOrigin);
      
  /*
  // Get computed Robot pose at the time the mat was observed (note that this
  // also makes the pose have the robot's current world origin as its parent
  Pose3d robotPoseAtObsTime;
  if(robot->GetComputedPoseAt(matSeen->GetLastObservedTime(), robotPoseAtObsTime) != RESULT_OK) {
  PRINT_NAMED_ERROR("BlockWorld.UpdateRobotPose.CouldNotComputeHistoricalPose",
                    "Time %d\n",
                    matSeen->GetLastObservedTime());
  return false;
  }
  */
      
  // Get the pose of the robot with respect to the observed mat piece
  Pose3d robotPoseWrtMat;
  if(robotPoseAtObsTime.GetWithRespectTo(matSeen->GetPose(), robotPoseWrtMat) == false) {
    PRINT_NAMED_ERROR("Robot.LocalizeToMat.MatPoseOriginMisMatch",
                      "Could not get RobotPoseStamp w.r.t. matPose.");
    return RESULT_FAIL;
  }
      
  // Make the computed robot pose use the existing mat piece as its parent
  robotPoseWrtMat.SetParent(&existingMatPiece->GetPose());
  //robotPoseWrtMat.SetName(std::string("Robot_") + std::to_string(robot->GetID()));
      
  // Don't snap to horizontal or discrete Z levels when we see a mat marker
  // while on a ramp
  if(IsOnRamp() == false)
  {
    // If there is any significant rotation, make sure that it is roughly
    // around the Z axis
    Radians rotAngle;
    Vec3f rotAxis;
    robotPoseWrtMat.GetRotationVector().GetAngleAndAxis(rotAngle, rotAxis);

    if(std::abs(rotAngle.ToFloat()) > DEG_TO_RAD(5) && !AreUnitVectorsAligned(rotAxis, Z_AXIS_3D(), DEG_TO_RAD(15))) {
      PRINT_NAMED_WARNING("Robot.LocalizeToMat.OutOfPlaneRotation",
                          "Refusing to localize to %s because "
                          "Robot %d's Z axis would not be well aligned with the world Z axis. "
                          "(angle=%.1fdeg, axis=(%.3f,%.3f,%.3f)",
                          ObjectTypeToString(existingMatPiece->GetType()), GetID(),
                          rotAngle.getDegrees(), rotAxis.x(), rotAxis.y(), rotAxis.z());
      return RESULT_FAIL;
    }
        
    // Snap to purely horizontal rotation and surface of the mat
    if(existingMatPiece->IsPoseOn(robotPoseWrtMat, 0, 10.f)) {
      Vec3f robotPoseWrtMat_trans = robotPoseWrtMat.GetTranslation();
      robotPoseWrtMat_trans.z() = existingMatPiece->GetDrivingSurfaceHeight();
      robotPoseWrtMat.SetTranslation(robotPoseWrtMat_trans);
    }
    robotPoseWrtMat.SetRotation( robotPoseWrtMat.GetRotationAngle<'Z'>(), Z_AXIS_3D() );
        
  } // if robot is on ramp
      
  if(!_localizedToFixedObject && !existingMatPiece->IsMoveable()) {
    // If we have not yet seen a fixed mat, and this is a fixed mat, rejigger
    // the origins so that we use it as the world origin
    PRINT_NAMED_INFO("Robot.LocalizeToMat.LocalizingToFirstFixedMat",
                     "Localizing robot %d to fixed %s mat for the first time.",
                     GetID(), ObjectTypeToString(existingMatPiece->GetType()));
        
    if((lastResult = UpdateWorldOrigin(robotPoseWrtMat)) != RESULT_OK) {
      PRINT_NAMED_ERROR("Robot.LocalizeToMat.SetPoseOriginFailure",
                        "Failed to update robot %d's pose origin when (re-)localizing it.", GetID());
      return lastResult;
    }
        
    _localizedToFixedObject = true;
  }
  else if(IsLocalized() == false) {
    // If the robot is not yet localized, it is about to be, so we need to
    // update pose origins so that anything it has seen so far becomes rooted
    // to this mat's origin (whether mat is fixed or not)
    PRINT_NAMED_INFO("Robot.LocalizeToMat.LocalizingRobotFirstTime",
                     "Localizing robot %d for the first time (to %s mat).",
                     GetID(), ObjectTypeToString(existingMatPiece->GetType()));
        
    if((lastResult = UpdateWorldOrigin(robotPoseWrtMat)) != RESULT_OK) {
      PRINT_NAMED_ERROR("Robot.LocalizeToMat.SetPoseOriginFailure",
                        "Failed to update robot %d's pose origin when (re-)localizing it.", GetID());
      return lastResult;
    }
        
    if(!existingMatPiece->IsMoveable()) {
      // If this also happens to be a fixed mat, then we have now localized
      // to a fixed mat
      _localizedToFixedObject = true;
    }
  }
      
  /*
  // Don't snap to horizontal or discrete Z levels when we see a mat marker
  // while on a ramp
  if(IsOnRamp() == false)
  {
  // If there is any significant rotation, make sure that it is roughly
  // around the Z axis
  Radians rotAngle;
  Vec3f rotAxis;
  robotPoseWrtMat.GetRotationVector().GetAngleAndAxis(rotAngle, rotAxis);
  const float dotProduct = DotProduct(rotAxis, Z_AXIS_3D());
  const float dotProductThreshold = 0.0152f; // 1.f - std::cos(DEG_TO_RAD(10)); // within 10 degrees
  if(!NEAR(rotAngle.ToFloat(), 0, DEG_TO_RAD(10)) && !NEAR(std::abs(dotProduct), 1.f, dotProductThreshold)) {
  PRINT_NAMED_WARNING("BlockWorld.UpdateRobotPose.RobotNotOnHorizontalPlane",
  "Robot's Z axis is not well aligned with the world Z axis. "
  "(angle=%.1fdeg, axis=(%.3f,%.3f,%.3f)\n",
  rotAngle.getDegrees(), rotAxis.x(), rotAxis.y(), rotAxis.z());
  }
        
  // Snap to purely horizontal rotation and surface of the mat
  if(existingMatPiece->IsPoseOn(robotPoseWrtMat, 0, 10.f)) {
  Vec3f robotPoseWrtMat_trans = robotPoseWrtMat.GetTranslation();
  robotPoseWrtMat_trans.z() = existingMatPiece->GetDrivingSurfaceHeight();
  robotPoseWrtMat.SetTranslation(robotPoseWrtMat_trans);
  }
  robotPoseWrtMat.SetRotation( robotPoseWrtMat.GetRotationAngle<'Z'>(), Z_AXIS_3D() );
        
  } // if robot is on ramp
  */
      
  // Add the new vision-based pose to the robot's history. Note that we use
  // the pose w.r.t. the origin for storing poses in history.
  // RobotPoseStamp p(robot->GetPoseFrameID(),
  //                  robotPoseWrtMat.GetWithRespectToOrigin(),
  //                  posePtr->GetHeadAngle(),
  //                  posePtr->GetLiftAngle());
  Pose3d robotPoseWrtOrigin = robotPoseWrtMat.GetWithRespectToOrigin();
      
  if((lastResult = AddVisionOnlyPoseToHistory(existingMatPiece->GetLastObservedTime(),
                                              robotPoseWrtOrigin.GetTranslation().x(),
                                              robotPoseWrtOrigin.GetTranslation().y(),
                                              robotPoseWrtOrigin.GetTranslation().z(),
                                              robotPoseWrtOrigin.GetRotationAngle<'Z'>().ToFloat(),
                                              posePtr->GetHeadAngle(),
                                              posePtr->GetLiftAngle())) != RESULT_OK)
  {
    PRINT_NAMED_ERROR("Robot.LocalizeToMat.FailedAddingVisionOnlyPoseToHistory", "");
    return lastResult;
  }
      
      
  // Update the computed historical pose as well so that subsequent block
  // pose updates use obsMarkers whose camera's parent pose is correct.
  // Note again that we store the pose w.r.t. the origin in history.
  // TODO: Should SetPose() do the flattening w.r.t. origin?
  posePtr->SetPose(GetPoseFrameID(), robotPoseWrtOrigin, posePtr->GetHeadAngle(), posePtr->GetLiftAngle());
      
  // Compute the new "current" pose from history which uses the
  // past vision-based "ground truth" pose we just computed.
  if(UpdateCurrPoseFromHistory(existingMatPiece->GetPose()) == false) {
    PRINT_NAMED_ERROR("Robot.LocalizeToMat.FailedUpdateCurrPoseFromHistory", "");
    return RESULT_FAIL;
  }
      
  // Mark the robot as now being localized to this mat
  // NOTE: this should be _after_ calling AddVisionOnlyPoseToHistory, since
  //    that function checks whether the robot is already localized
  lastResult = SetLocalizedTo(existingMatPiece);
  if(RESULT_OK != lastResult) {
    PRINT_NAMED_ERROR("Robot.LocalizeToMat.SetLocalizedToFail", "");
    return lastResult;
  }
      
  // Overly-verbose. Use for debugging localization issues
  /*
    PRINT_INFO("Using %s mat %d to localize robot %d at (%.3f,%.3f,%.3f), %.1fdeg@(%.2f,%.2f,%.2f)\n",
    existingMatPiece->GetType().GetName().c_str(),
    existingMatPiece->GetID().GetValue(), GetID(),
    GetPose().GetTranslation().x(),
    GetPose().GetTranslation().y(),
    GetPose().GetTranslation().z(),
    GetPose().GetRotationAngle<'Z'>().getDegrees(),
    GetPose().GetRotationAxis().x(),
    GetPose().GetRotationAxis().y(),
    GetPose().GetRotationAxis().z());
  */
      
  // Send the ground truth pose that was computed instead of the new current
  // pose and let the robot deal with updating its current pose based on the
  // history that it keeps.
  SendAbsLocalizationUpdate();
      
  return RESULT_OK;
      
} // LocalizeToMat()
    
    
// Clears the path that the robot is executing which also stops the robot
Result Robot::ClearPath()
{
  GetContext()->GetVizManager()->ErasePath(_ID);
  _pdo->ClearPath();
  return SendMessage(RobotInterface::EngineToRobot(RobotInterface::ClearPath(0)));
}
    
// Sends a path to the robot to be immediately executed
Result Robot::ExecutePath(const Planning::Path& path, const bool useManualSpeed)
{
  Result lastResult = RESULT_FAIL;
      
  if (path.GetNumSegments() == 0) {
    PRINT_NAMED_WARNING("Robot.ExecutePath.EmptyPath", "");
    lastResult = RESULT_OK;
  } else {
        
    // TODO: Clear currently executing path or write to buffered path?
    lastResult = ClearPath();
    if(lastResult == RESULT_OK) {
      ++_lastSentPathID;
      _pdo->SetPath(path);
      _usingManualPathSpeed = useManualSpeed;
      lastResult = SendExecutePath(path, useManualSpeed);
    }
        
    // Visualize path if robot has just started traversing it.
    GetContext()->GetVizManager()->DrawPath(_ID, path, NamedColors::EXECUTED_PATH);
        
  }
      
  return lastResult;
}
  
    
Result Robot::SetOnRamp(bool t)
{
  if(t == _onRamp) {
    // Nothing to do
    return RESULT_OK;
  }
      
  // We are either transition onto or off of a ramp
      
  Ramp* ramp = dynamic_cast<Ramp*>(_blockWorld.GetObjectByIDandFamily(_rampID, ObjectFamily::Ramp));
  if(ramp == nullptr) {
    PRINT_NAMED_WARNING("Robot.SetOnRamp.NoRampWithID",
                        "Robot %d is transitioning on/off of a ramp, but Ramp object with ID=%d not found in the world",
                        _ID, _rampID.GetValue());
    return RESULT_FAIL;
  }
      
  assert(_rampDirection == Ramp::ASCENDING || _rampDirection == Ramp::DESCENDING);
      
  const bool transitioningOnto = (t == true);
      
  if(transitioningOnto) {
    // Record start (x,y) position coming from robot so basestation can
    // compute actual (x,y,z) position from upcoming odometry updates
    // coming from robot (which do not take slope of ramp into account)
    _rampStartPosition = {_pose.GetTranslation().x(), _pose.GetTranslation().y()};
    _rampStartHeight   = _pose.GetTranslation().z();
        
    PRINT_NAMED_INFO("Robot.SetOnRamp.TransitionOntoRamp",
                     "Robot %d transitioning onto ramp %d, using start (%.1f,%.1f,%.1f)",
                     _ID, ramp->GetID().GetValue(), _rampStartPosition.x(), _rampStartPosition.y(), _rampStartHeight);
        
  } else {
    // Just do an absolute pose update, setting the robot's position to
    // where we "know" he should be when he finishes ascending or
    // descending the ramp
    switch(_rampDirection)
    {
      case Ramp::ASCENDING:
        SetPose(ramp->GetPostAscentPose(WHEEL_BASE_MM).GetWithRespectToOrigin());
        break;
            
      case Ramp::DESCENDING:
        SetPose(ramp->GetPostDescentPose(WHEEL_BASE_MM).GetWithRespectToOrigin());
        break;
            
      default:
        PRINT_NAMED_WARNING("Robot.SetOnRamp.UnexpectedRampDirection",
                            "When transitioning on/off ramp, expecting the ramp direction to be either "
                            "ASCENDING or DESCENDING, not %d.", _rampDirection);
        return RESULT_FAIL;
    }
        
    _rampDirection = Ramp::UNKNOWN;
        
    const TimeStamp_t timeStamp = _poseHistory->GetNewestTimeStamp();
        
    PRINT_NAMED_INFO("Robot.SetOnRamp.TransitionOffRamp",
                     "Robot %d transitioning off of ramp %d, at (%.1f,%.1f,%.1f) @ %.1fdeg, timeStamp = %d",
                     _ID, ramp->GetID().GetValue(),
                     _pose.GetTranslation().x(), _pose.GetTranslation().y(), _pose.GetTranslation().z(),
                     _pose.GetRotationAngle<'Z'>().getDegrees(),
                     timeStamp);
        
    // We are creating a new pose frame at the top of the ramp
    //IncrementPoseFrameID();
    ++_frameId;
    Result lastResult = SendAbsLocalizationUpdate(_pose,
                                                  timeStamp,
                                                  _frameId);
    if(lastResult != RESULT_OK) {
      PRINT_NAMED_WARNING("Robot.SetOnRamp.SendAbsLocUpdateFailed",
                          "Robot %d failed to send absolute localization update.", _ID);
      return lastResult;
    }

  } // if/else transitioning onto ramp
      
  _onRamp = t;
      
  return RESULT_OK;
      
} // SetOnPose()
    
    
Result Robot::SetPoseOnCharger()
{
  Charger* charger = dynamic_cast<Charger*>(_blockWorld.GetObjectByIDandFamily(_chargerID, ObjectFamily::Charger));
  if(charger == nullptr) {
    PRINT_NAMED_WARNING("Robot.SetPoseOnCharger.NoChargerWithID",
                        "Robot %d has docked to charger, but Charger object with ID=%d not found in the world.",
                        _ID, _chargerID.GetValue());
    return RESULT_FAIL;
  }
      
  // Just do an absolute pose update, setting the robot's position to
  // where we "know" he should be when he finishes ascending the charger.
  SetPose(charger->GetDockedPose().GetWithRespectToOrigin());

  const TimeStamp_t timeStamp = _poseHistory->GetNewestTimeStamp();
    
  PRINT_NAMED_INFO("Robot.SetPoseOnCharger.SetPose",
                   "Robot %d now on charger %d, at (%.1f,%.1f,%.1f) @ %.1fdeg, timeStamp = %d",
                   _ID, charger->GetID().GetValue(),
                   _pose.GetTranslation().x(), _pose.GetTranslation().y(), _pose.GetTranslation().z(),
                   _pose.GetRotationAngle<'Z'>().getDegrees(),
                   timeStamp);
      
  // We are creating a new pose frame at the top of the ramp
  //IncrementPoseFrameID();
  ++_frameId;
  Result lastResult = SendAbsLocalizationUpdate(_pose,
                                                timeStamp,
                                                _frameId);
  if(lastResult != RESULT_OK) {
    PRINT_NAMED_WARNING("Robot.SetPoseOnCharger.SendAbsLocUpdateFailed",
                        "Robot %d failed to send absolute localization update.", _ID);
    return lastResult;
  }
      
  return RESULT_OK;
      
} // SetOnPose()
    
    
Result Robot::DockWithObject(const ObjectID objectID,
                             const f32 speed_mmps,
                             const f32 accel_mmps2,
                             const f32 decel_mmps2,
                             const Vision::KnownMarker* marker,
                             const Vision::KnownMarker* marker2,
                             const DockAction dockAction,
                             const f32 placementOffsetX_mm,
                             const f32 placementOffsetY_mm,
                             const f32 placementOffsetAngle_rad,
                             const bool useManualSpeed,
                             const u8 numRetries,
                             const DockingMethod dockingMethod)
{
  return DockWithObject(objectID,
                        speed_mmps,
                        accel_mmps2,
                        decel_mmps2,
                        marker,
                        marker2,
                        dockAction,
                        0, 0, u8_MAX,
                        placementOffsetX_mm, placementOffsetY_mm, placementOffsetAngle_rad,
                        useManualSpeed,
                        numRetries,
                        dockingMethod);
}
    
Result Robot::DockWithObject(const ObjectID objectID,
                             const f32 speed_mmps,
                             const f32 accel_mmps2,
                             const f32 decel_mmps2,
                             const Vision::KnownMarker* marker,
                             const Vision::KnownMarker* marker2,
                             const DockAction dockAction,
                             const u16 image_pixel_x,
                             const u16 image_pixel_y,
                             const u8 pixel_radius,
                             const f32 placementOffsetX_mm,
                             const f32 placementOffsetY_mm,
                             const f32 placementOffsetAngle_rad,
                             const bool useManualSpeed,
                             const u8 numRetries,
                             const DockingMethod dockingMethod)
{
  ActionableObject* object = dynamic_cast<ActionableObject*>(_blockWorld.GetObjectByID(objectID));
  if(object == nullptr) {
    PRINT_NAMED_ERROR("Robot.DockWithObject.ObjectDoesNotExist",
                      "Object with ID=%d no longer exists for docking.", objectID.GetValue());
    return RESULT_FAIL;
  }
      
  CORETECH_ASSERT(marker != nullptr);
      
  // Need to store these so that when we receive notice from the physical
  // robot that it has picked up an object we can transition the docking
  // object to being carried, using PickUpDockObject()
  _dockObjectID = objectID;
  _dockMarker   = marker;
      
  // Dock marker has to be a child of the dock block
  if(marker->GetPose().GetParent() != &object->GetPose()) {
    PRINT_NAMED_ERROR("Robot.DockWithObject.MarkerNotOnObject",
                      "Specified dock marker must be a child of the specified dock object.");
    return RESULT_FAIL;
  }

  // Mark as dirty so that the robot no longer localizes to this object
  object->SetPoseState(Anki::Vision::ObservableObject::PoseState::Dirty);
      
  _usingManualPathSpeed = useManualSpeed;
  _lastPickOrPlaceSucceeded = false;
      
  // Sends a message to the robot to dock with the specified marker
  // that it should currently be seeing. If pixel_radius == u8_MAX,
  // the marker can be seen anywhere in the image (same as above function), otherwise the
  // marker's center must be seen at the specified image coordinates
  // with pixel_radius pixels.
  Result sendResult = SendRobotMessage<::Anki::Cozmo::DockWithObject>(0.0f,
                                                                      speed_mmps,
                                                                      accel_mmps2,
                                                                      decel_mmps2,
                                                                      dockAction,
                                                                      useManualSpeed,
                                                                      numRetries,
                                                                      dockingMethod);
  if(sendResult == RESULT_OK) {
        
    // When we are "docking" with a ramp or crossing a bridge, we
    // don't want to worry about the X angle being large (since we
    // _expect_ it to be large, since the markers are facing upward).
    const bool checkAngleX = !(dockAction == DockAction::DA_RAMP_ASCEND  ||
                               dockAction == DockAction::DA_RAMP_DESCEND ||
                               dockAction == DockAction::DA_CROSS_BRIDGE);
        
    // Tell the VisionSystem to start tracking this marker:
    _visionComponentPtr->SetMarkerToTrack(marker->GetCode(), marker->GetSize(),
                                          image_pixel_x, image_pixel_y, checkAngleX,
                                          placementOffsetX_mm, placementOffsetY_mm,
                                          placementOffsetAngle_rad);
  }
      
  return sendResult;
}
    
    
const std::set<ObjectID> Robot::GetCarryingObjects() const
{
  std::set<ObjectID> objects;
  if (_carryingObjectID.IsSet()) {
    objects.insert(_carryingObjectID);
  }
  if (_carryingObjectOnTopID.IsSet()) {
    objects.insert(_carryingObjectOnTopID);
  }
  return objects;
}
    
void Robot::SetCarryingObject(ObjectID carryObjectID)
{
  ObservableObject* object = _blockWorld.GetObjectByID(carryObjectID);
  if(object == nullptr) {
    PRINT_NAMED_ERROR("Robot.SetCarryingObject",
                      "Object %d no longer exists in the world. Can't set it as robot's carried object.",
                      carryObjectID.GetValue());
  } else {
    ActionableObject* carriedObject = dynamic_cast<ActionableObject*>(object);
    if(carriedObject == nullptr) {
      // This really should not happen
      PRINT_NAMED_ERROR("Robot.SetCarryingObject",
                        "Object %d could not be cast as an ActionableObject, so cannot mark it as carried.",
                        carryObjectID.GetValue());
    } else {
      if(carriedObject->IsBeingCarried()) {
        PRINT_NAMED_WARNING("Robot.SetCarryingObject",
                            "Robot %d is about to mark object %d as carried but that object already thinks it is "
                            "being carried.",
                            GetID(), carryObjectID.GetValue());
      }
      carriedObject->SetBeingCarried(true);
      _carryingObjectID = carryObjectID;
          
      // Don't remain localized to an object if we are now carrying it
      if(_carryingObjectID == GetLocalizedTo())
      {
        // Note that the robot may still remaing localized (based on its
        // odometry), but just not *to an object*
        SetLocalizedTo(nullptr);
      } // if(_carryingObjectID == GetLocalizedTo())
          
      // Tell the robot it's carrying something
      // TODO: This is probably not the right way/place to do this (should we pass in carryObjectOnTopID?)
      if(_carryingObjectOnTopID.IsSet()) {
        SendSetCarryState(CarryState::CARRY_2_BLOCK);
      } else {
        SendSetCarryState(CarryState::CARRY_1_BLOCK);
      }
    } // if/else (carriedObject == nullptr)
  }
}
    
void Robot::UnSetCarryingObjects(bool topOnly)
{
  std::set<ObjectID> carriedObjectIDs = GetCarryingObjects();
  for (auto& objID : carriedObjectIDs) {
    if (topOnly && objID != _carryingObjectOnTopID) {
      continue;
    }
        
    ObservableObject* object = _blockWorld.GetObjectByID(objID);
    if(object == nullptr) {
      PRINT_NAMED_ERROR("Robot.UnSetCarryingObjects",
                        "Object %d robot %d thought it was carrying no longer exists in the world.",
                        objID.GetValue(), GetID());
    } else {
      ActionableObject* carriedObject = dynamic_cast<ActionableObject*>(object);
      if(carriedObject == nullptr) {
        // This really should not happen
        PRINT_NAMED_ERROR("Robot.UnSetCarryingObjects",
                          "Carried object %d could not be cast as an ActionableObject.",
                          objID.GetValue());
      } else if(carriedObject->IsBeingCarried() == false) {
        PRINT_NAMED_WARNING("Robot.UnSetCarryingObjects",
                            "Robot %d thinks it is carrying object %d but that object "
                            "does not think it is being carried.", GetID(), objID.GetValue());
            
      } else {
        carriedObject->SetBeingCarried(false);
      }
    }
  }

  if (!topOnly) {      
    // Tell the robot it's not carrying anything
    if (_carryingObjectID.IsSet()) {
      SendSetCarryState(CarryState::CARRY_NONE);
    }

    // Even if the above failed, still mark the robot's carry ID as unset
    _carryingObjectID.UnSet();
  }
  _carryingObjectOnTopID.UnSet();
}
    
void Robot::UnSetCarryObject(ObjectID objID)
{
  // If it's the bottom object in the stack, unset all carried objects.
  if (_carryingObjectID == objID) {
    UnSetCarryingObjects(false);
  } else if (_carryingObjectOnTopID == objID) {
    UnSetCarryingObjects(true);
  }
}
    
    
Result Robot::SetObjectAsAttachedToLift(const ObjectID& objectID, const Vision::KnownMarker* objectMarker)
{
  if(!objectID.IsSet()) {
    PRINT_NAMED_ERROR("Robot.PickUpDockObject.ObjectIDNotSet",
                      "No docking object ID set, but told to pick one up.");
    return RESULT_FAIL;
  }
      
  if(objectMarker == nullptr) {
    PRINT_NAMED_ERROR("Robot.PickUpDockObject.NoDockMarkerSet",
                      "No docking marker set, but told to pick up object.");
    return RESULT_FAIL;
  }
      
  if(IsCarryingObject()) {
    PRINT_NAMED_ERROR("Robot.PickUpDockObject.AlreadyCarryingObject",
                      "Already carrying an object, but told to pick one up.");
    return RESULT_FAIL;
  }
      
  ActionableObject* object = dynamic_cast<ActionableObject*>(_blockWorld.GetObjectByID(objectID));
  if(object == nullptr) {
    PRINT_NAMED_ERROR("Robot.PickUpDockObject.ObjectDoesNotExist",
                      "Dock object with ID=%d no longer exists for picking up.", objectID.GetValue());
    return RESULT_FAIL;
  }
      
  // Base the object's pose relative to the lift on how far away the dock
  // marker is from the center of the block
  // TODO: compute the height adjustment per object or at least use values from cozmoConfig.h
  Pose3d objectPoseWrtLiftPose;
  if(object->GetPose().GetWithRespectTo(_liftPose, objectPoseWrtLiftPose) == false) {
    PRINT_NAMED_ERROR("Robot.PickUpDockObject.ObjectAndLiftPoseHaveDifferentOrigins",
                      "Object robot is picking up and robot's lift must share a common origin.");
    return RESULT_FAIL;
  }
      
  objectPoseWrtLiftPose.SetTranslation({objectMarker->GetPose().GetTranslation().Length() +
        LIFT_FRONT_WRT_WRIST_JOINT, 0.f, -12.5f});
      
  // make part of the lift's pose chain so the object will now be relative to
  // the lift and move with the robot
  objectPoseWrtLiftPose.SetParent(&_liftPose);
      

  // If we know there's an object on top of the object we are picking up,
  // mark it as being carried too
  // TODO: Do we need to be able to handle non-actionable objects on top of actionable ones?

  ObservableObject* objectOnTop = _blockWorld.FindObjectOnTopOf(*object, STACKED_HEIGHT_TOL_MM);
  if(objectOnTop != nullptr) {
    ActionableObject* actionObjectOnTop = dynamic_cast<ActionableObject*>(objectOnTop);
    if(actionObjectOnTop != nullptr) {
      Pose3d onTopPoseWrtCarriedPose;
      if(actionObjectOnTop->GetPose().GetWithRespectTo(object->GetPose(), onTopPoseWrtCarriedPose) == false)
      {
        PRINT_NAMED_WARNING("Robot.SetObjectAsAttachedToLift",
                            "Found object on top of carried object, but could not get its "
                            "pose w.r.t. the carried object.");
      } else {
        PRINT_NAMED_INFO("Robot.SetObjectAsAttachedToLift",
                         "Setting object %d on top of carried object as also being carried.",
                         actionObjectOnTop->GetID().GetValue());
        onTopPoseWrtCarriedPose.SetParent(&object->GetPose());
        actionObjectOnTop->SetPose(onTopPoseWrtCarriedPose);
        _carryingObjectOnTopID = actionObjectOnTop->GetID();
        actionObjectOnTop->SetBeingCarried(true);
      }
    }
  } else {
    _carryingObjectOnTopID.UnSet();
  }
      
  SetCarryingObject(objectID); // also marks the object as carried
  _carryingMarker   = objectMarker;

  // Don't actually change the object's pose until we've checked for objects on top
  object->SetPose(objectPoseWrtLiftPose);

  return RESULT_OK;
      
} // AttachObjectToLift()
    
    
Result Robot::SetCarriedObjectAsUnattached()
{
  if(IsCarryingObject() == false) {
    PRINT_NAMED_WARNING("Robot.SetCarriedObjectAsUnattached.CarryingObjectNotSpecified",
                        "Robot not carrying object, but told to place one. "
                        "(Possibly actually rolling or balancing or popping a wheelie.");
    return RESULT_FAIL;
  }
      
  ActionableObject* object = dynamic_cast<ActionableObject*>(_blockWorld.GetObjectByID(_carryingObjectID));
      
  if(object == nullptr)
  {
    // This really should not happen.  How can a object being carried get deleted?
    PRINT_NAMED_ERROR("Robot.SetCarriedObjectAsUnattached.CarryingObjectDoesNotExist",
                      "Carrying object with ID=%d no longer exists.", _carryingObjectID.GetValue());
    return RESULT_FAIL;
  }
     
  Pose3d placedPose;
  if(object->GetPose().GetWithRespectTo(_pose.FindOrigin(), placedPose) == false) {
    PRINT_NAMED_ERROR("Robot.SetCarriedObjectAsUnattached.OriginMisMatch",
                      "Could not get carrying object's pose relative to robot's origin.");
    return RESULT_FAIL;
  }
  object->SetPose(placedPose);
      
  PRINT_NAMED_INFO("Robot.SetCarriedObjectAsUnattached.ObjectPlaced",
                   "Robot %d successfully placed object %d at (%.2f, %.2f, %.2f).",
                   _ID, object->GetID().GetValue(),
                   object->GetPose().GetTranslation().x(),
                   object->GetPose().GetTranslation().y(),
                   object->GetPose().GetTranslation().z());

  UnSetCarryingObjects(); // also sets carried objects as not being carried anymore
  _carryingMarker = nullptr;
      
  if(_carryingObjectOnTopID.IsSet()) {
    ActionableObject* objectOnTop = dynamic_cast<ActionableObject*>(_blockWorld.GetObjectByID(_carryingObjectOnTopID));
    if(objectOnTop == nullptr)
    {
      // This really should not happen.  How can a object being carried get deleted?
      PRINT_NAMED_ERROR("Robot.SetCarriedObjectAsUnattached",
                        "Object on top of carrying object with ID=%d no longer exists.",
                        _carryingObjectOnTopID.GetValue());
      return RESULT_FAIL;
    }
        
    Pose3d placedPoseOnTop;
    if(objectOnTop->GetPose().GetWithRespectTo(_pose.FindOrigin(), placedPoseOnTop) == false) {
      PRINT_NAMED_ERROR("Robot.SetCarriedObjectAsUnattached.OriginMisMatch",
                        "Could not get carrying object's pose relative to robot's origin.");
      return RESULT_FAIL;
          
    }
    objectOnTop->SetPose(placedPoseOnTop);
    objectOnTop->SetBeingCarried(false);
    _carryingObjectOnTopID.UnSet();
    PRINT_NAMED_INFO("Robot.SetCarriedObjectAsUnattached", "Updated object %d on top of carried object.",
                     objectOnTop->GetID().GetValue());
  }
      
  return RESULT_OK;
      
} // UnattachCarriedObject()
    
    
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

bool Robot::CanInteractWithObjectHelper(const ObservableObject& object, Pose3d& relPose) const
{
  // TODO:(bn) maybe there should be some central logic for which object families are valid here
  if( object.GetFamily() != ObjectFamily::Block &&
      object.GetFamily() != ObjectFamily::LightCube ) {
    return false;
  }

  // check that the object is ready to place on top of
  if( object.IsPoseStateUnknown() ||
      !object.IsRestingFlat() ||
      (IsCarryingObject() && GetCarryingObject() == object.GetID()) ) {
    return false;
  }

  // check if we can transform to robot space
  if ( !object.GetPose().GetWithRespectTo(GetPose(), relPose) ) {
    return false;
  }

  // check if it has something on top
  const ObservableObject* objectOnTop = GetBlockWorld().FindObjectOnTopOf(object, STACKED_HEIGHT_TOL_MM);
  if ( nullptr != objectOnTop ) {
    return false;
  }

  return true;
}
  
// Helper for the following functions to reason about the object's height (mid or top)
// relative to specified threshold
inline static bool IsTooHigh(const ObservableObject& object, const Pose3d& poseWrtRobot,
                             float heightMultiplier, float heightTol, bool useTop)
{
  const Point3f rotatedSize( object.GetPose().GetRotation() * object.GetSize() );
  const float rotatedHeight = std::abs( rotatedSize.z() );
  float z = poseWrtRobot.GetTranslation().z();
  if(useTop) {
    z += rotatedHeight*0.5f;
  }
  const bool isTooHigh = z > (heightMultiplier * rotatedHeight + heightTol);
  return isTooHigh;
}
    
bool Robot::CanStackOnTopOfObject(const ObservableObject& objectToStackOn) const
{
  // Note rsam/kevin: this only works currently for original cubes. Doing height checks would require more
  // comparison of sizes, checks for I can stack but not pick up due to slack required to pick up, etc. In order
  // to simplify just cover the most basic case here (for the moment)

  Pose3d relPos;
  if( ! CanInteractWithObjectHelper(objectToStackOn, relPos) ) {
    return false;
  }
            
  // check if it's too high to stack on
  if ( IsTooHigh(objectToStackOn, relPos, 1.f, STACKED_HEIGHT_TOL_MM, true) ) {
    return false;
  }
    
  // all checks clear
  return true;
}

bool Robot::CanPickUpObject(const ObservableObject& objectToPickUp) const
{
  Pose3d relPos;
  if( ! CanInteractWithObjectHelper(objectToPickUp, relPos) ) {
    return false;
  }
      
  // check if it's too high to pick up
  if ( IsTooHigh(objectToPickUp, relPos, 2.f, STACKED_HEIGHT_TOL_MM, true) ) {
    return false;
  }
    
  // all checks clear
  return true;
}

bool Robot::CanPickUpObjectFromGround(const ObservableObject& objectToPickUp) const
{
  Pose3d relPos;
  if( ! CanInteractWithObjectHelper(objectToPickUp, relPos) ) {
    return false;
  }
      
  // check if it's too high to pick up
  if ( IsTooHigh(objectToPickUp, relPos, 0.5f, ON_GROUND_HEIGHT_TOL_MM, false) ) {
    return false;
  }
    
  // all checks clear
  return true;
}
    
// ============ Messaging ================
    
Result Robot::SendMessage(const RobotInterface::EngineToRobot& msg, bool reliable, bool hot) const
{
  Result sendResult = _context->GetRobotManager()->GetMsgHandler()->SendMessage(_ID, msg, reliable, hot);
  if(sendResult != RESULT_OK) {
    PRINT_NAMED_ERROR("Robot.SendMessage", "Robot %d failed to send a message.", _ID);
  }
  return sendResult;
}
      
// Sync time with physical robot and trigger it robot to send back camera calibration
Result Robot::SendSyncTime() const
{

  Result result = SendMessage(RobotInterface::EngineToRobot(
                                RobotInterface::SyncTime(_ID,
                                                         BaseStationTimer::getInstance()->GetCurrentTimeStamp(),
                                                         DRIVE_CENTER_OFFSET)));
      
  if(result == RESULT_OK) {
    result = SendMessage(RobotInterface::EngineToRobot(
                           RobotInterface::ImageRequest(ImageSendMode::Stream, ImageResolution::QVGA)));
        
    // Reset pose on connect
    PRINT_NAMED_INFO("Robot.SendSyncTime", "Setting pose to (0,0,0)");
    Pose3d zeroPose(0, Z_AXIS_3D(), {0,0,0});
    return SendAbsLocalizationUpdate(zeroPose, 0, GetPoseFrameID());
  } else {
    PRINT_NAMED_WARNING("Robot.SendSyncTime.FailedToSend","");
  }
      
  return result;
}
    
// Sends a path to the robot to be immediately executed
Result Robot::SendExecutePath(const Planning::Path& path, const bool useManualSpeed) const
{
  // Send start path execution message
  PRINT_NAMED_INFO("Robot::SendExecutePath",
                   "sending start execution message (pathID = %d, manualSpeed == %d)",
                   _lastSentPathID, useManualSpeed);
  return SendMessage(RobotInterface::EngineToRobot(RobotInterface::ExecutePath(_lastSentPathID, useManualSpeed)));
}
    
Result Robot::SendAbsLocalizationUpdate(const Pose3d&        pose,
                                        const TimeStamp_t&   t,
                                        const PoseFrameID_t& frameId) const
{
  return SendMessage(RobotInterface::EngineToRobot(
                       RobotInterface::AbsoluteLocalizationUpdate(
                         t,
                         frameId,
                         pose.GetTranslation().x(),
                         pose.GetTranslation().y(),
                         pose.GetRotation().GetAngleAroundZaxis().ToFloat()
                         )));
}
    
Result Robot::SendAbsLocalizationUpdate() const
{
  // Look in history for the last vis pose and send it.
  TimeStamp_t t;
  RobotPoseStamp p;
  if (_poseHistory->GetLatestVisionOnlyPose(t, p) == RESULT_FAIL) {
    PRINT_NAMED_WARNING("Robot.SendAbsLocUpdate.NoVizPoseFound", "");
    return RESULT_FAIL;
  }

  return SendAbsLocalizationUpdate(p.GetPose().GetWithRespectToOrigin(), t, p.GetFrameId());
}
    
Result Robot::SendHeadAngleUpdate() const
{
  return SendMessage(RobotInterface::EngineToRobot(
                       RobotInterface::HeadAngleUpdate(_currentHeadAngle)));
}

Result Robot::SendIMURequest(const u32 length_ms) const
{
  return SendRobotMessage<RobotInterface::ImuRequest>(length_ms);
}

Result Robot::SendEnablePickupParalysis(const bool enable) const
{
  return SendRobotMessage<RobotInterface::EnablePickupParalysis>(enable);
}
    
TimeStamp_t Robot::GetLastImageTimeStamp() const {
  return GetVisionComponent().GetLastProcessedImageTimeStamp();
}
    
/*
  const Pose3d Robot::ProxDetectTransform[] = { Pose3d(0, Z_AXIS_3D(), Vec3f(50, 25, 0)),
  Pose3d(0, Z_AXIS_3D(), Vec3f(50, 0, 0)),
  Pose3d(0, Z_AXIS_3D(), Vec3f(50, -25, 0)) };
*/


Quad2f Robot::GetBoundingQuadXY(const f32 padding_mm) const
{
  return GetBoundingQuadXY(_pose, padding_mm);
}
    
Quad2f Robot::GetBoundingQuadXY(const Pose3d& atPose, const f32 padding_mm) const
{
  const RotationMatrix2d R(atPose.GetRotation().GetAngleAroundZaxis());

  static const Quad2f CanonicalBoundingBoxXY(
    {ROBOT_BOUNDING_X_FRONT, -0.5f*ROBOT_BOUNDING_Y},
    {ROBOT_BOUNDING_X_FRONT,  0.5f*ROBOT_BOUNDING_Y},
    {ROBOT_BOUNDING_X_FRONT - ROBOT_BOUNDING_X, -0.5f*ROBOT_BOUNDING_Y},
    {ROBOT_BOUNDING_X_FRONT - ROBOT_BOUNDING_X,  0.5f*ROBOT_BOUNDING_Y});

  Quad2f boundingQuad(CanonicalBoundingBoxXY);
  if(padding_mm != 0.f) {
    Quad2f paddingQuad({ padding_mm, -padding_mm},
                       { padding_mm,  padding_mm},
                       {-padding_mm, -padding_mm},
                       {-padding_mm,  padding_mm});
    boundingQuad += paddingQuad;
  }
      
  using namespace Quad;
  for(CornerName iCorner = FirstCorner; iCorner < NumCorners; ++iCorner) {
    // Rotate to given pose
    boundingQuad[iCorner] = R * boundingQuad[iCorner];
  }
      
  // Re-center
  Point2f center(atPose.GetTranslation().x(), atPose.GetTranslation().y());
  boundingQuad += center;
      
  return boundingQuad;
      
} // GetBoundingBoxXY()
    
  
    
    
f32 Robot::GetHeight() const
{
  return std::max(ROBOT_BOUNDING_Z, GetLiftHeight() + LIFT_HEIGHT_ABOVE_GRIPPER);
}
    
f32 Robot::GetLiftHeight() const
{
  return ConvertLiftAngleToLiftHeightMM(GetLiftAngle());
}
    
Pose3d Robot::GetLiftPoseWrtCamera(f32 atLiftAngle, f32 atHeadAngle) const
{
  Pose3d liftPose(_liftPose);
  ComputeLiftPose(atLiftAngle, liftPose);
      
  Pose3d camPose = GetCameraPose(atHeadAngle);
      
  Pose3d liftPoseWrtCam;
  bool result = liftPose.GetWithRespectTo(camPose, liftPoseWrtCam);
  ASSERT_NAMED(result == true, "Lift and camera poses should be in same pose tree");
      
  return liftPoseWrtCam;
}
    
f32 Robot::ConvertLiftHeightToLiftAngleRad(f32 height_mm)
{
  height_mm = CLIP(height_mm, LIFT_HEIGHT_LOWDOCK, LIFT_HEIGHT_CARRY);
  return asinf((height_mm - LIFT_BASE_POSITION[2] - LIFT_FORK_HEIGHT_REL_TO_ARM_END)/LIFT_ARM_LENGTH);
}

f32 Robot::ConvertLiftAngleToLiftHeightMM(f32 angle_rad)
{
  return (sinf(angle_rad) * LIFT_ARM_LENGTH) + LIFT_BASE_POSITION[2] + LIFT_FORK_HEIGHT_REL_TO_ARM_END;
}
    
Result Robot::RequestIMU(const u32 length_ms) const
{
  return SendIMURequest(length_ms);
}
    
    
// ============ Pose history ===============
    
Result Robot::AddRawOdomPoseToHistory(const TimeStamp_t t,
                                      const PoseFrameID_t frameID,
                                      const f32 pose_x, const f32 pose_y, const f32 pose_z,
                                      const f32 pose_angle,
                                      const f32 head_angle,
                                      const f32 lift_angle)
{
  return _poseHistory->AddRawOdomPose(t, frameID, pose_x, pose_y, pose_z, pose_angle, head_angle, lift_angle);
}
    
    
Result Robot::UpdateWorldOrigin(Pose3d& newPoseWrtNewOrigin)
{
  // Reverse the connection between origin and robot, and connect the new
  // reversed connection
  //CORETECH_ASSERT(p.GetPose().GetParent() == _poseOrigin);
  //Pose3d originWrtRobot = _pose.GetInverse();
  //originWrtRobot.SetParent(&newPoseOrigin);
      
  // TODO: get rid of nasty const_cast somehow
  Pose3d* newOrigin = const_cast<Pose3d*>(newPoseWrtNewOrigin.GetParent());
  newOrigin->SetParent(nullptr);
      
  // TODO: We should only be doing this (modifying what _worldOrigin points to) when it is one of the
  // placeHolder poseOrigins, not if it is a mat!
  std::string origName(_worldOrigin->GetName());
  *_worldOrigin = _pose.GetInverse();
  _worldOrigin->SetParent(&newPoseWrtNewOrigin);
      
      
  // Connect the old origin's pose to the same root the robot now has.
  // It is no longer the robot's origin, but for any of its children,
  // it is now in the right coordinates.
  if(_worldOrigin->GetWithRespectTo(*newOrigin, *_worldOrigin) == false) {
    PRINT_NAMED_ERROR("Robot.UpdateWorldOrigin.NewLocalizationOriginProblem",
                      "Could not get pose origin w.r.t. new origin pose.");
    return RESULT_FAIL;
  }
      
  //_worldOrigin->PreComposeWith(*newOrigin);
      
  // Preserve the old world origin's name, despite updates above
  _worldOrigin->SetName(origName);
      
  // Now make the robot's world origin point to the new origin
  _worldOrigin = newOrigin;
      
  newOrigin->SetRotation(0, Z_AXIS_3D());
  newOrigin->SetTranslation({0,0,0});
      
  // Now make the robot's origin point to the new origin
  // TODO: avoid the icky const_cast here...
  _worldOrigin = const_cast<Pose3d*>(newPoseWrtNewOrigin.GetParent());

  _robotWorldOriginChangedSignal.emit(GetID());
      
  return RESULT_OK;
      
} // UpdateWorldOrigin()
    
    
Result Robot::AddVisionOnlyPoseToHistory(const TimeStamp_t t,
                                         const f32 pose_x, const f32 pose_y, const f32 pose_z,
                                         const f32 pose_angle,
                                         const f32 head_angle,
                                         const f32 lift_angle)
{      
  // We have a new ("ground truth") key frame. Increment the pose frame!
  //IncrementPoseFrameID();
  ++_frameId;
      
  return _poseHistory->AddVisionOnlyPose(t, _frameId,
                                         pose_x, pose_y, pose_z,
                                         pose_angle,
                                         head_angle,
                                         lift_angle);
}

Result Robot::ComputeAndInsertPoseIntoHistory(const TimeStamp_t t_request,
                                              TimeStamp_t& t, RobotPoseStamp** p,
                                              HistPoseKey* key,
                                              bool withInterpolation)
{
  return _poseHistory->ComputeAndInsertPoseAt(t_request, t, p, key, withInterpolation);
}

Result Robot::GetVisionOnlyPoseAt(const TimeStamp_t t_request, RobotPoseStamp** p)
{
  return _poseHistory->GetVisionOnlyPoseAt(t_request, p);
}

Result Robot::GetComputedPoseAt(const TimeStamp_t t_request, Pose3d& pose) const
{
  const RobotPoseStamp* poseStamp;
  Result lastResult = GetComputedPoseAt(t_request, &poseStamp);
  if(lastResult == RESULT_OK) {
    // Grab the pose stored in the pose stamp we just found, and hook up
    // its parent to the robot's current world origin (since pose history
    // doesn't keep track of pose parent chains)
    pose = poseStamp->GetPose();
    pose.SetParent(_worldOrigin);
  }
  return lastResult;
}
    
Result Robot::GetComputedPoseAt(const TimeStamp_t t_request, const RobotPoseStamp** p, HistPoseKey* key) const
{
  return _poseHistory->GetComputedPoseAt(t_request, p, key);
}

Result Robot::GetComputedPoseAt(const TimeStamp_t t_request, RobotPoseStamp** p, HistPoseKey* key)
{
  return _poseHistory->GetComputedPoseAt(t_request, p, key);
}

TimeStamp_t Robot::GetLastMsgTimestamp() const
{
  return _poseHistory->GetNewestTimeStamp();
}
    
bool Robot::IsValidPoseKey(const HistPoseKey key) const
{
  return _poseHistory->IsValidPoseKey(key);
}
    
bool Robot::UpdateCurrPoseFromHistory(const Pose3d& wrtParent)
{
  bool poseUpdated = false;
      
  TimeStamp_t t;
  RobotPoseStamp p;
  if (_poseHistory->ComputePoseAt(_poseHistory->GetNewestTimeStamp(), t, p) == RESULT_OK) {
    if (p.GetFrameId() == GetPoseFrameID()) {
          
      // Grab a copy of the pose from history, which has been flattened (i.e.,
      // made with respect to whatever its origin was when it was stored).
      // We just assume for now that is the same as the _current_ world origin
      // (bad assumption? or will differing frame IDs help us?), and make that
      // chaining connection so that we can get the pose w.r.t. the requested
      // parent.
      Pose3d histPoseWrtCurrentWorld(p.GetPose());
      histPoseWrtCurrentWorld.SetParent(&wrtParent.FindOrigin());
          
      Pose3d newPose;
      if((histPoseWrtCurrentWorld.GetWithRespectTo(wrtParent, newPose))==false) {
        PRINT_NAMED_ERROR("Robot.UpdateCurrPoseFromHistory.GetWrtParentFailed",
                          "Could not update robot %d's current pose from history w.r.t. specified pose %s.",
                          _ID, wrtParent.GetName().c_str());
      } else {
        SetPose(newPose);
        poseUpdated = true;
      }
           
    }
  }
      
  return poseUpdated;
}
    
void Robot::SetBackpackLights(const std::array<u32,(size_t)LEDId::NUM_BACKPACK_LEDS>& onColor,
                              const std::array<u32,(size_t)LEDId::NUM_BACKPACK_LEDS>& offColor,
                              const std::array<u32,(size_t)LEDId::NUM_BACKPACK_LEDS>& onPeriod_ms,
                              const std::array<u32,(size_t)LEDId::NUM_BACKPACK_LEDS>& offPeriod_ms,
                              const std::array<u32,(size_t)LEDId::NUM_BACKPACK_LEDS>& transitionOnPeriod_ms,
                              const std::array<u32,(size_t)LEDId::NUM_BACKPACK_LEDS>& transitionOffPeriod_ms)
{
  std::array<Anki::Cozmo::LightState, (size_t)LEDId::NUM_BACKPACK_LEDS> lights;
  for (int i = 0; i < (int)LEDId::NUM_BACKPACK_LEDS; ++i) {
    lights[i].onColor  = ENCODED_COLOR(onColor[i]);
    lights[i].offColor = ENCODED_COLOR(offColor[i]);
    lights[i].onFrames  = MS_TO_LED_FRAMES(onPeriod_ms[i]);
    lights[i].offFrames = MS_TO_LED_FRAMES(offPeriod_ms[i]);
    lights[i].transitionOnFrames  = MS_TO_LED_FRAMES(transitionOnPeriod_ms[i]);
    lights[i].transitionOffFrames = MS_TO_LED_FRAMES(transitionOffPeriod_ms[i]);
    // PRINT_NAMED_DEBUG("BackpackLights",
    //                   "LED %u, onColor 0x%x (0x%x), offColor 0x%x (0x%x)",
    //                   i, lights[i].onColor, onColor[i], lights[i].offColor, offColor[i]);
  }

  SendMessage(RobotInterface::EngineToRobot(RobotInterface::BackpackLights(lights)));
}
    
void Robot::SetHeadlight(bool on)
{
  SendMessage(RobotInterface::EngineToRobot(RobotInterface::SetHeadlight(on)));
}
    
Result Robot::SetObjectLights(const ObjectID& objectID,
                              const WhichCubeLEDs whichLEDs,
                              const u32 onColor, const u32 offColor,
                              const u32 onPeriod_ms, const u32 offPeriod_ms,
                              const u32 transitionOnPeriod_ms, const u32 transitionOffPeriod_ms,
                              const bool turnOffUnspecifiedLEDs,
                              const MakeRelativeMode makeRelative,
                              const Point2f& relativeToPoint)
{
  ActiveObject* activeObject = GetBlockWorld().GetActiveObjectByID(objectID);
  if(activeObject == nullptr) {
    PRINT_NAMED_ERROR("Robot.SetObjectLights", "Null active object pointer.");
    return RESULT_FAIL_INVALID_OBJECT;
  } else {
        
    WhichCubeLEDs rotatedWhichLEDs = whichLEDs;
        
    ActiveCube* activeCube = dynamic_cast<ActiveCube*>(activeObject);
    if(activeCube != nullptr) {
      // NOTE: if make relative mode is "off", this call doesn't do anything:
      rotatedWhichLEDs = activeCube->MakeWhichLEDsRelativeToXY(whichLEDs, relativeToPoint, makeRelative);
    } else if (makeRelative != MakeRelativeMode::RELATIVE_LED_MODE_OFF) {
      PRINT_NAMED_WARNING("Robot.SetObjectLights.MakeRelativeOnNonCube", "");
      return RESULT_FAIL;
    }

        
    activeObject->SetLEDs(rotatedWhichLEDs, onColor, offColor, onPeriod_ms, offPeriod_ms,
                          transitionOnPeriod_ms, transitionOffPeriod_ms,
                          turnOffUnspecifiedLEDs);
        
    std::array<Anki::Cozmo::LightState, 4> lights;
    ASSERT_NAMED((int)ActiveObjectConstants::NUM_CUBE_LEDS == 4, "Robot.wrong.number.of.cube.ligths");
    for (int i = 0; i < (int)ActiveObjectConstants::NUM_CUBE_LEDS; ++i){
      const ActiveObject::LEDstate& ledState = activeObject->GetLEDState(i);
      lights[i].onColor  = ENCODED_COLOR(ledState.onColor);
      lights[i].offColor = ENCODED_COLOR(ledState.offColor);
      lights[i].onFrames  = MS_TO_LED_FRAMES(ledState.onPeriod_ms);
      lights[i].offFrames = MS_TO_LED_FRAMES(ledState.offPeriod_ms);
      lights[i].transitionOnFrames  = MS_TO_LED_FRAMES(ledState.transitionOnPeriod_ms);
      lights[i].transitionOffFrames = MS_TO_LED_FRAMES(ledState.transitionOffPeriod_ms);
      // PRINT_NAMED_DEBUG("SetObjectLights(1)",
      //                   "LED %u, onColor 0x%x (0x%x), offColor 0x%x (0x%x)",
      //                   i,
      //                   lights[i].onColor,
      //                   ledState.onColor.AsRGBA(),
      //                   lights[i].offColor,
      //                   ledState.offColor.AsRGBA());
    }

    if( DEBUG_BLOCK_LIGHTS ) {
      PRINT_NAMED_DEBUG("Robot.SetObjectLights.Set1",
                        "Setting lights for object %d (activeID %d)",
                        objectID.GetValue(), activeObject->GetActiveID());
    }

    SendMessage(RobotInterface::EngineToRobot(SetCubeGamma(activeObject->GetLEDGamma())));
    return SendMessage(RobotInterface::EngineToRobot(CubeLights(lights, (uint32_t)activeObject->GetActiveID())));
  }
}
      
Result Robot::SetObjectLights(
  const ObjectID& objectID,
  const std::array<u32,(size_t)ActiveObjectConstants::NUM_CUBE_LEDS>& onColor,
  const std::array<u32,(size_t)ActiveObjectConstants::NUM_CUBE_LEDS>& offColor,
  const std::array<u32,(size_t)ActiveObjectConstants::NUM_CUBE_LEDS>& onPeriod_ms,
  const std::array<u32,(size_t)ActiveObjectConstants::NUM_CUBE_LEDS>& offPeriod_ms,
  const std::array<u32,(size_t)ActiveObjectConstants::NUM_CUBE_LEDS>& transitionOnPeriod_ms,
  const std::array<u32,(size_t)ActiveObjectConstants::NUM_CUBE_LEDS>& transitionOffPeriod_ms,
  const MakeRelativeMode makeRelative,
  const Point2f& relativeToPoint)
{
  ActiveObject* activeObject = GetBlockWorld().GetActiveObjectByID(objectID);
  if(activeObject == nullptr) {
    PRINT_NAMED_ERROR("Robot.SetObjectLights", "Null active object pointer.");
    return RESULT_FAIL_INVALID_OBJECT;
  } else {
        
    activeObject->SetLEDs(onColor, offColor, onPeriod_ms, offPeriod_ms, transitionOnPeriod_ms, transitionOffPeriod_ms);


    ActiveCube* activeCube = dynamic_cast<ActiveCube*>(activeObject);
    if(activeCube != nullptr) {
      // NOTE: if make relative mode is "off", this call doesn't do anything:
      activeCube->MakeStateRelativeToXY(relativeToPoint, makeRelative);
    } else if (makeRelative != MakeRelativeMode::RELATIVE_LED_MODE_OFF) {
      PRINT_NAMED_WARNING("Robot.SetObjectLights.MakeRelativeOnNonCube", "");
      return RESULT_FAIL;
    }
        
    std::array<Anki::Cozmo::LightState, 4> lights;
    ASSERT_NAMED((int)ActiveObjectConstants::NUM_CUBE_LEDS == 4, "Robot.wrong.number.of.cube.ligths");
    for (int i = 0; i < (int)ActiveObjectConstants::NUM_CUBE_LEDS; ++i){
      const ActiveObject::LEDstate& ledState = activeObject->GetLEDState(i);
      lights[i].onColor  = ENCODED_COLOR(ledState.onColor);
      lights[i].offColor = ENCODED_COLOR(ledState.offColor);
      lights[i].onFrames  = MS_TO_LED_FRAMES(ledState.onPeriod_ms);
      lights[i].offFrames = MS_TO_LED_FRAMES(ledState.offPeriod_ms);
      lights[i].transitionOnFrames  = MS_TO_LED_FRAMES(ledState.transitionOnPeriod_ms);
      lights[i].transitionOffFrames = MS_TO_LED_FRAMES(ledState.transitionOffPeriod_ms);
      
      // PRINT_NAMED_DEBUG("SetObjectLights(2)",
      //                   "LED %u, onColor 0x%x (0x%x), offColor 0x%x (0x%x), onFrames 0x%x (%ums), "
      //                   "offFrames 0x%x (%ums), transOnFrames 0x%x (%ums), transOffFrames 0x%x (%ums)",
      //                   i, lights[i].onColor, ledState.onColor.AsRGBA(),
      //                   lights[i].offColor, ledState.offColor.AsRGBA(),
      //                   lights[i].onFrames, ledState.onPeriod_ms,
      //                   lights[i].offFrames, ledState.offPeriod_ms,
      //                   lights[i].transitionOnFrames, ledState.transitionOnPeriod_ms,
      //                   lights[i].transitionOffFrames, ledState.transitionOffPeriod_ms);
      
    }

    if( DEBUG_BLOCK_LIGHTS ) {
      PRINT_NAMED_DEBUG("Robot.SetObjectLights.Set2",
                        "Setting lights for object %d (activeID %d)",
                        objectID.GetValue(), activeObject->GetActiveID());
    }
        
    SendMessage(RobotInterface::EngineToRobot(SetCubeGamma(activeObject->GetLEDGamma())));
    return SendMessage(RobotInterface::EngineToRobot(CubeLights(lights, (uint32_t)activeObject->GetActiveID())));
  }

}
    
Result Robot::ConnectToObjects(const FactoryIDArray& factory_ids)
{
  ASSERT_NAMED_EVENT(factory_ids.size() == _objectsToConnectTo.size(),
                     "Robot.ConnectToObjects.InvalidArrayLength",
                     "%zu slots requested. Max %zu",
                     factory_ids.size(), _objectsToConnectTo.size());
      
  std::stringstream strs;
  for (auto it = factory_ids.begin(); it != factory_ids.end(); ++it) {
    strs << "0x" << std::hex << *it << ", ";
  }
  std::stringstream strs2;
  for (auto it = _objectsToConnectTo.begin(); it != _objectsToConnectTo.end(); ++it) {
    strs2 << "0x" << std::hex << it->factoryID << ", pending = " << it->pending << ", ";
  }
  PRINT_NAMED_INFO("Robot.ConnectToObjects",
                   "Before processing factory_ids = %s. _objectsToConnectTo = %s",
                   strs.str().c_str(), strs2.str().c_str());
      
  // Save the new list so we process it during the update loop. Note that we compare
  // against the list of current connected objects but we store it in the list of
  // objects to connect to.
  for (int i = 0; i < _connectedObjects.size(); ++i) {
    if (factory_ids[i] != _connectedObjects[i].factoryID) {
      _objectsToConnectTo[i].factoryID = factory_ids[i];
      _objectsToConnectTo[i].pending = true;
    }
  }
      
  return RESULT_OK;
}
    
void Robot::ConnectToRequestedObjects()
{
  // Check if there is any new petition to connect to a new block
  auto it = std::find_if(_objectsToConnectTo.begin(), _objectsToConnectTo.end(), [](ObjectToConnectToInfo obj) {
      return obj.pending;
    });
      
  if (it == _objectsToConnectTo.end()) {
    return;
  }
      
  // std::stringstream strs;
  // for (auto it = _objectsToConnectTo.begin(); it != _objectsToConnectTo.end(); ++it) {
  //   strs << "0x" << std::hex << it->factoryID << ", pending = " << it->pending << ", ";
  // }
  // std::stringstream strs2;
  // for (auto it = _connectedObjects.begin(); it != _connectedObjects.end(); ++it) {
  //   strs2 << "0x" << std::hex << it->factoryID << ", ";
  // }
     
  // PRINT_NAMED_INFO("Robot.ConnectToRequestedObjects.BeforeProcessing",
  //                  "_objectsToConnectTo = %s. _connectedObjects = %s",
  //                  strs.str().c_str(), strs2.str().c_str());
      
  // Iterate over the connected objects and the new factory IDs to see what we need to send in the
  // message for every slot.
  ASSERT_NAMED(_objectsToConnectTo.size() == _connectedObjects.size(),
               "Robot.ConnectToRequestedObjects.InvalidArraySize");
  for (int i = 0; i < _objectsToConnectTo.size(); ++i) {
        
    ObjectToConnectToInfo& newObjectToConnectTo = _objectsToConnectTo[i];
    ActiveObjectInfo& activeObjectInfo = _connectedObjects[i];
        
    // If there is nothing to do with this slot, continue
    if (!newObjectToConnectTo.pending) {
      continue;
    }
        
    // If we have already connected to the object in the given slot, we don't have to do anything
    if (newObjectToConnectTo.factoryID == activeObjectInfo.factoryID) {
      newObjectToConnectTo.Reset();
      continue;
    }
        
    // If the new factory ID is 0 then we want to disconnect from the object
    if (newObjectToConnectTo.factoryID == 0) {
      PRINT_NAMED_INFO("Robot.ConnectToRequestedObjects.Sending",
                       "Sending message for slot %d with factory ID = %d",
                       i, 0);
      activeObjectInfo.Reset();
      newObjectToConnectTo.Reset();
      SendMessage(RobotInterface::EngineToRobot(SetPropSlot((FactoryID)0, i)));
      continue;
    }
        
    // We are connecting to a new object. Check if the object is discovered yet
    // If it is not, don't clear it from the list of requested objects in case
    // we find it in the next execution of this loop
    auto discoveredObjIt = _discoveredObjects.find(newObjectToConnectTo.factoryID);
    if (discoveredObjIt == _discoveredObjects.end()) {
      continue;
    }
        
    for (const auto & connectedObj : _connectedObjects) {
      if (connectedObj.objectType == discoveredObjIt->second.objectType) {
        PRINT_NAMED_WARNING("Robot.ConnectToRequestedObjects.SameTypeAlreadyConnected",
                            "Object with factory ID 0x%x matches type (%s) of another connected object. "
                            "Only one of each type may be connected.",
                            newObjectToConnectTo.factoryID, EnumToString(connectedObj.objectType));

        // If we can't connect to the new object we keep the one we have now
        newObjectToConnectTo.Reset();
        continue;
      }
    }
        
    // This is valid object to connect to.
    PRINT_NAMED_INFO("Robot.ConnectToRequestedObjects.Sending",
                     "Sending message for slot %d with factory ID = 0x%x",
                     i, newObjectToConnectTo.factoryID);
    SendMessage(RobotInterface::EngineToRobot(SetPropSlot(newObjectToConnectTo.factoryID, i)));
        
    // We are done with this slot
    _connectedObjects[i] = discoveredObjIt->second;
    newObjectToConnectTo.Reset();
  }
      
  // std::stringstream strs3;
  // for (auto it = _objectsToConnectTo.begin(); it != _objectsToConnectTo.end(); ++it) {
  //   strs3 << "0x" << std::hex << it->factoryID << ", pending = " << it->pending << ", ";
  // }
  // std::stringstream strs4;
  // for (auto it = _connectedObjects.begin(); it != _connectedObjects.end(); ++it) {
  //   strs4 << "0x" << std::hex << it->factoryID << ", ";
  // }
     
  // PRINT_NAMED_INFO("Robot.ConnectToRequestedObjects.AfterProcessing",
  //                  "_objectsToConnectTo = %s. _connectedObjects = %s", strs3.str().c_str(), strs4.str().c_str());

  return;
}
    
void Robot::BroadcastAvailableObjects(bool enable)
{
  _enableDiscoveredObjectsBroadcasting = enable;
}
    
      
Robot::ReactionCallbackIter Robot::AddReactionCallback(const Vision::Marker::Code code, ReactionCallback callback)
{
  //CoreTechPrint("_reactionCallbacks size = %lu\n", (unsigned long)_reactionCallbacks.size());
      
  _reactionCallbacks[code].emplace_front(callback);
      
  return _reactionCallbacks[code].cbegin();
      
} // AddReactionCallback()
    
    
// Remove a preivously-added callback using the iterator returned by
// AddReactionCallback above.
void Robot::RemoveReactionCallback(const Vision::Marker::Code code, ReactionCallbackIter callbackToRemove)
{
  _reactionCallbacks[code].erase(callbackToRemove);
  if(_reactionCallbacks[code].empty()) {
    _reactionCallbacks.erase(code);
  }
} // RemoveReactionCallback()
    
    
Result Robot::AbortAll()
{
  bool anyFailures = false;
      
  _actionList.Cancel();
      
  if(AbortDrivingToPose() != RESULT_OK) {
    anyFailures = true;
  }
      
  if(AbortDocking() != RESULT_OK) {
    anyFailures = true;
  }
      
  if(AbortAnimation() != RESULT_OK) {
    anyFailures = true;
  }
      
  if(anyFailures) {
    return RESULT_FAIL;
  } else {
    return RESULT_OK;
  }
      
}
      
Result Robot::AbortDocking()
{
  return SendAbortDocking();
}
      
Result Robot::AbortAnimation()
{
  return SendAbortAnimation();
}
    
Result Robot::AbortDrivingToPose()
{
  _selectedPathPlanner->StopPlanning();
  Result ret = ClearPath();
  _numPlansFinished = _numPlansStarted;

  return ret;
}

Result Robot::SendAbortAnimation()
{
  return SendMessage(RobotInterface::EngineToRobot(RobotInterface::AbortAnimation()));
}
    
Result Robot::SendAbortDocking()
{
  return SendMessage(RobotInterface::EngineToRobot(Anki::Cozmo::AbortDocking()));
}
 
Result Robot::SendSetCarryState(CarryState state)
{
  return SendMessage(RobotInterface::EngineToRobot(Anki::Cozmo::CarryStateUpdate(state)));
}
      
Result Robot::SendFlashObjectIDs()
{
  return SendMessage(RobotInterface::EngineToRobot(FlashObjectIDs()));
}
     
Result Robot::SendDebugString(const char* format, ...)
{
  int len = 0;
  const int kMaxDebugStringLen = u8_MAX;
  char text[kMaxDebugStringLen];
  strcpy(text, format);
      
  // Create formatted text
  va_list argptr;
  va_start(argptr, format);
  len = vsnprintf(text, kMaxDebugStringLen, format, argptr);
  va_end(argptr);
        
  std::string str(text);
      
  // Send message to game
  Broadcast(ExternalInterface::MessageEngineToGame(ExternalInterface::DebugString(str)));
      
  // Send message to viz
  GetContext()->GetVizManager()->SetText(VizManager::DEBUG_STRING,
                                         NamedColors::ORANGE,
                                         "%s", text);
      
  return RESULT_OK;
}
      
      
void Robot::ComputeDriveCenterPose(const Pose3d &robotPose, Pose3d &driveCenterPose) const
{
  MoveRobotPoseForward(robotPose, GetDriveCenterOffset(), driveCenterPose);
}
      
void Robot::ComputeOriginPose(const Pose3d &driveCenterPose, Pose3d &robotPose) const
{
  MoveRobotPoseForward(driveCenterPose, -GetDriveCenterOffset(), robotPose);
}

void Robot::MoveRobotPoseForward(const Pose3d &startPose, f32 distance, Pose3d &movedPose) {
  movedPose = startPose;
  f32 angle = startPose.GetRotationAngle<'Z'>().ToFloat();
  Vec3f trans;
  trans.x() = startPose.GetTranslation().x() + distance * cosf(angle);
  trans.y() = startPose.GetTranslation().y() + distance * sinf(angle);
  movedPose.SetTranslation(trans);
}
      
f32 Robot::GetDriveCenterOffset() const {
  f32 driveCenterOffset = DRIVE_CENTER_OFFSET;
  if (IsCarryingObject()) {
    driveCenterOffset = 0;
  }
  return driveCenterOffset;
}
    
bool Robot::Broadcast(ExternalInterface::MessageEngineToGame&& event)
{
  if(HasExternalInterface()) {
    GetExternalInterface()->Broadcast(event);
    return true;
  } else {
    return false;
  }
}
    
ExternalInterface::RobotState Robot::GetRobotState()
{
  ExternalInterface::RobotState msg;
      
  msg.robotID = GetID();
      
  msg.pose = PoseStruct3d(GetPose());
      
  msg.poseAngle_rad = GetPose().GetRotationAngle<'Z'>().ToFloat();
  msg.posePitch_rad = GetPitchAngle();
      
  msg.leftWheelSpeed_mmps  = GetLeftWheelSpeed();
  msg.rightWheelSpeed_mmps = GetRightWheelSpeed();
      
  msg.headAngle_rad = GetHeadAngle();
  msg.liftHeight_mm = GetLiftHeight();
      
  msg.status = 0;
  if(GetMoveComponent().IsMoving()) { msg.status |= (uint32_t)RobotStatusFlag::IS_MOVING; }
  if(IsPickingOrPlacing()) { msg.status |= (uint32_t)RobotStatusFlag::IS_PICKING_OR_PLACING; }
  if(IsPickedUp())         { msg.status |= (uint32_t)RobotStatusFlag::IS_PICKED_UP; }
  if(IsAnimating())        { msg.status |= (uint32_t)RobotStatusFlag::IS_ANIMATING; }
  if(IsIdleAnimating())    { msg.status |= (uint32_t)RobotStatusFlag::IS_ANIMATING_IDLE; }
  if(IsCarryingObject())   {
    msg.status |= (uint32_t)RobotStatusFlag::IS_CARRYING_BLOCK;
    msg.carryingObjectID = GetCarryingObject();
    msg.carryingObjectOnTopID = GetCarryingObjectOnTop();
  } else {
    msg.carryingObjectID = -1;
  }
  if(!GetActionList().IsEmpty()) {
    msg.status |= (uint32_t)RobotStatusFlag::IS_PATHING;
  }
      
  msg.gameStatus = 0;
  if (IsLocalized() && !IsPickedUp()) { msg.gameStatus |= (uint8_t)GameStatusFlag::IsLocalized; }
      
  msg.headTrackingObjectID = GetMoveComponent().GetTrackToObject();
      
  msg.localizedToObjectID = GetLocalizedTo();
      
  // TODO: Add proximity sensor data to state message
      
  msg.batteryVoltage = GetBatteryVoltage();
      
  msg.lastImageTimeStamp = GetVisionComponent().GetLastProcessedImageTimeStamp();
      
  return msg;
}
    
RobotInterface::MessageHandler* Robot::GetRobotMessageHandler()
{
  if (!_context->GetRobotManager())
  {
    ASSERT_NAMED(false, "Robot.GetRobotMessageHandler.nullptr");
    return nullptr;
  }
        
  return _context->GetRobotManager()->GetMsgHandler();
}
    
ObjectType Robot::GetDiscoveredObjectType(FactoryID id)
{
  auto it = _discoveredObjects.find(id);
  if (it != _discoveredObjects.end()) {
    return it->second.objectType;
  }
  return ObjectType::Unknown;
}
    
} // namespace Cozmo
} // namespace Anki

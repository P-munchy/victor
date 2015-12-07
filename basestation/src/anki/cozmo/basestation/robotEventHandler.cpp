/**
 * File: robotEventHandler.cpp
 *
 * Author: Lee
 * Created: 08/11/15
 *
 * Description: Class for subscribing to and handling events going to robots.
 *
 * Copyright: Anki, Inc. 2015
 *
 **/


#include "anki/cozmo/basestation/robotEventHandler.h"
#include "anki/cozmo/basestation/robotManager.h"
#include "anki/cozmo/basestation/robot.h"
#include "anki/cozmo/basestation/actionInterface.h"
#include "anki/cozmo/basestation/cozmoActions.h"
#include "anki/cozmo/basestation/externalInterface/externalInterface.h"
#include "anki/cozmo/basestation/moodSystem/moodManager.h"
#include "anki/cozmo/basestation/progressionSystem/progressionManager.h"
#include "anki/cozmo/shared/cozmoConfig.h"
#include "anki/common/basestation/math/point_impl.h"
#include "clad/externalInterface/messageGameToEngine.h"
#include "util/logging/logging.h"

namespace Anki {
namespace Cozmo {

RobotEventHandler::RobotEventHandler(RobotManager& manager, IExternalInterface* interface)
  : _robotManager(manager)
  , _externalInterface(interface)
{
  if (_externalInterface != nullptr)
  {
    // We'll use this callback for simple events we care about
    auto actionEventCallback = std::bind(&RobotEventHandler::HandleActionEvents, this, std::placeholders::_1);
    
    std::vector<ExternalInterface::MessageGameToEngineTag> tagList =
    {
      ExternalInterface::MessageGameToEngineTag::PlaceObjectOnGround,
      ExternalInterface::MessageGameToEngineTag::PlaceObjectOnGroundHere,
      ExternalInterface::MessageGameToEngineTag::GotoPose,
      ExternalInterface::MessageGameToEngineTag::GotoObject,
      ExternalInterface::MessageGameToEngineTag::AlignWithObject,
      ExternalInterface::MessageGameToEngineTag::PickupObject,
      ExternalInterface::MessageGameToEngineTag::PlaceOnObject,
      ExternalInterface::MessageGameToEngineTag::PlaceRelObject,
      ExternalInterface::MessageGameToEngineTag::RollObject,
      ExternalInterface::MessageGameToEngineTag::PopAWheelie,
      ExternalInterface::MessageGameToEngineTag::TraverseObject,
      ExternalInterface::MessageGameToEngineTag::MountCharger,
      ExternalInterface::MessageGameToEngineTag::PlayAnimation,
      ExternalInterface::MessageGameToEngineTag::FaceObject,
      ExternalInterface::MessageGameToEngineTag::FacePose,
      ExternalInterface::MessageGameToEngineTag::TurnInPlace,
    };
    
    // Subscribe to desired events
    for (auto tag : tagList)
    {
      _signalHandles.push_back(_externalInterface->Subscribe(tag, actionEventCallback));
    }
    
    // Custom handler for QueueSingleAction
    auto queueSingleActionCallback = std::bind(&RobotEventHandler::HandleQueueSingleAction, this, std::placeholders::_1);
    _signalHandles.push_back(_externalInterface->Subscribe(ExternalInterface::MessageGameToEngineTag::QueueSingleAction, queueSingleActionCallback));
    
    // Custom handler for QueueCompoundAction
    auto queueCompoundActionCallback = std::bind(&RobotEventHandler::HandleQueueCompoundAction, this, std::placeholders::_1);
    _signalHandles.push_back(_externalInterface->Subscribe(ExternalInterface::MessageGameToEngineTag::QueueCompoundAction, queueCompoundActionCallback));
    
    // Custom handler for SetLiftHeight
    auto setLiftHeightCallback = std::bind(&RobotEventHandler::HandleSetLiftHeight, this, std::placeholders::_1);
    _signalHandles.push_back(_externalInterface->Subscribe(ExternalInterface::MessageGameToEngineTag::SetLiftHeight, setLiftHeightCallback));
    
    // Custom handler for DisplayProceduralFace
    auto dispProcFaceCallback = std::bind(&RobotEventHandler::HandleDisplayProceduralFace, this, std::placeholders::_1);
    _signalHandles.push_back(_externalInterface->Subscribe(ExternalInterface::MessageGameToEngineTag::DisplayProceduralFace, dispProcFaceCallback));
    
    // Custom handler for ForceDelocalizeRobot
    auto delocalizeCallabck = std::bind(&RobotEventHandler::HandleForceDelocalizeRobot, this, std::placeholders::_1);
    _signalHandles.push_back(_externalInterface->Subscribe(ExternalInterface::MessageGameToEngineTag::ForceDelocalizeRobot, delocalizeCallabck));
    
    // Custom handlers for Mood events
    {
      auto moodEventCallback = std::bind(&RobotEventHandler::HandleMoodEvent, this, std::placeholders::_1);
      _signalHandles.push_back(_externalInterface->Subscribe(ExternalInterface::MessageGameToEngineTag::MoodMessage, moodEventCallback));
    }

    // Custom handlers for Progression events
    {
      auto progressionEventCallback = std::bind(&RobotEventHandler::HandleProgressionEvent, this, std::placeholders::_1);
      _signalHandles.push_back(_externalInterface->Subscribe(ExternalInterface::MessageGameToEngineTag::ProgressionMessage, progressionEventCallback));
    }
  }
}
  
IActionRunner* GetPlaceObjectOnGroundActionHelper(Robot& robot, const ExternalInterface::PlaceObjectOnGround& msg)
{
  // Create an action to drive to specied pose and then put down the carried
  // object.
  // TODO: Better way to set the object's z height and parent? (This assumes object's origin is 22mm off the ground!)
  Rotation3d rot(UnitQuaternion<f32>(msg.qw, msg.qx, msg.qy, msg.qz));
  Pose3d targetPose(rot, Vec3f(msg.x_mm, msg.y_mm, 22.f), robot.GetWorldOrigin());
  return new PlaceObjectOnGroundAtPoseAction(robot,
                                             targetPose,
                                             msg.motionProf,
                                             msg.useExactRotation,
                                             msg.useManualSpeed);
}

IActionRunner* GetDriveToPoseActionHelper(Robot& robot, const ExternalInterface::GotoPose& msg)
{
  // TODO: Add ability to indicate z too!
  // TODO: Better way to specify the target pose's parent
  Pose3d targetPose(msg.rad, Z_AXIS_3D(), Vec3f(msg.x_mm, msg.y_mm, 0), robot.GetWorldOrigin());
  targetPose.SetName("GotoPoseTarget");

  // TODO: expose whether or not to drive with head down in message?
  // (For now it is hard-coded to true)
  const bool driveWithHeadDown = true;
  
  return new DriveToPoseAction(targetPose,
                               msg.motionProf,
                               driveWithHeadDown,
                               msg.useManualSpeed);
}
  
  
IActionRunner* GetPickupActionHelper(Robot& robot, const ExternalInterface::PickupObject& msg)
{
  ObjectID selectedObjectID;
  if(msg.objectID < 0) {
    selectedObjectID = robot.GetBlockWorld().GetSelectedObject();
  } else {
    selectedObjectID = msg.objectID;
  }
  
  if(static_cast<bool>(msg.usePreDockPose)) {
    return new DriveToPickupObjectAction(selectedObjectID,
                                         msg.motionProf,
                                         msg.useApproachAngle,
                                         msg.approachAngle_rad,
                                         msg.useManualSpeed);
  } else {
    PickupObjectAction* action = new PickupObjectAction(selectedObjectID, msg.useManualSpeed);
    action->SetPreActionPoseAngleTolerance(-1.f); // disable pre-action pose distance check
    return action;
  }
}


IActionRunner* GetPlaceRelActionHelper(Robot& robot, const ExternalInterface::PlaceRelObject& msg)
{
  ObjectID selectedObjectID;
  if(msg.objectID < 0) {
    selectedObjectID = robot.GetBlockWorld().GetSelectedObject();
  } else {
    selectedObjectID = msg.objectID;
  }
  
  if(static_cast<bool>(msg.usePreDockPose)) {
    return new DriveToPlaceRelObjectAction(selectedObjectID,
                                           msg.motionProf,
                                           msg.placementOffsetX_mm,
                                           msg.useApproachAngle,
                                           msg.approachAngle_rad,
                                           msg.useManualSpeed);
  } else {
    PlaceRelObjectAction* action = new PlaceRelObjectAction(selectedObjectID,
                                                            true,
                                                            msg.placementOffsetX_mm,
                                                            msg.useManualSpeed);
    action->SetPreActionPoseAngleTolerance(-1.f); // disable pre-action pose distance check
    return action;
  }
}


IActionRunner* GetPlaceOnActionHelper(Robot& robot, const ExternalInterface::PlaceOnObject& msg)
{
  ObjectID selectedObjectID;
  if(msg.objectID < 0) {
    selectedObjectID = robot.GetBlockWorld().GetSelectedObject();
  } else {
    selectedObjectID = msg.objectID;
  }
  
  if(static_cast<bool>(msg.usePreDockPose)) {

    return new DriveToPlaceOnObjectAction(robot,
                                          selectedObjectID,
                                          msg.motionProf,
                                          msg.useApproachAngle,
                                          msg.approachAngle_rad,
                                          msg.useManualSpeed);
  } else {
    PlaceRelObjectAction* action = new PlaceRelObjectAction(selectedObjectID,
                                                            false,
                                                            0,
                                                            msg.useManualSpeed);
    action->SetPreActionPoseAngleTolerance(-1.f); // disable pre-action pose distance check
    return action;
  }
}
  
  
  
IActionRunner* GetDriveToObjectActionHelper(Robot& robot, const ExternalInterface::GotoObject& msg)
{
  ObjectID selectedObjectID;
  if(msg.objectID < 0) {
    selectedObjectID = robot.GetBlockWorld().GetSelectedObject();
  } else {
    selectedObjectID = msg.objectID;
  }
  
  return new DriveToObjectAction(selectedObjectID,
                                 msg.distanceFromObjectOrigin_mm,
                                 msg.motionProf,
                                 msg.useManualSpeed);
}

IActionRunner* GetDriveToAlignWithObjectActionHelper(Robot& robot, const ExternalInterface::AlignWithObject& msg)
{
  ObjectID selectedObjectID;
  if(msg.objectID < 0) {
    selectedObjectID = robot.GetBlockWorld().GetSelectedObject();
  } else {
    selectedObjectID = msg.objectID;
  }
  
  return new DriveToAlignWithObjectAction(selectedObjectID,
                                          msg.distanceFromMarker_mm,
                                          msg.motionProf,
                                          msg.useApproachAngle,
                                          msg.approachAngle_rad,
                                          msg.useManualSpeed);
}
  
IActionRunner* GetRollObjectActionHelper(Robot& robot, const ExternalInterface::RollObject& msg)
{
  ObjectID selectedObjectID;
  if(msg.objectID < 0) {
    selectedObjectID = robot.GetBlockWorld().GetSelectedObject();
  } else {
    selectedObjectID = msg.objectID;
  }
  
  if(static_cast<bool>(msg.usePreDockPose)) {
    return new DriveToRollObjectAction(selectedObjectID,
                                       msg.motionProf,
                                       msg.useApproachAngle,
                                       msg.approachAngle_rad,
                                       msg.useManualSpeed);
  } else {
    RollObjectAction* action = new RollObjectAction(selectedObjectID, msg.useManualSpeed);
    action->SetPreActionPoseAngleTolerance(-1.f); // disable pre-action pose distance check
    return action;
  }
}
  

IActionRunner* GetPopAWheelieActionHelper(Robot& robot, const ExternalInterface::PopAWheelie& msg)
{
  ObjectID selectedObjectID;
  if(msg.objectID < 0) {
    selectedObjectID = robot.GetBlockWorld().GetSelectedObject();
  } else {
    selectedObjectID = msg.objectID;
  }
  
  if(static_cast<bool>(msg.usePreDockPose)) {
    return new DriveToPopAWheelieAction(selectedObjectID,
                                        msg.motionProf,
                                        msg.useApproachAngle,
                                        msg.approachAngle_rad,
                                        msg.useManualSpeed);
  } else {
    PopAWheelieAction* action = new PopAWheelieAction(selectedObjectID, msg.useManualSpeed);
    action->SetPreActionPoseAngleTolerance(-1.f); // disable pre-action pose distance check
    return action;
  }
}

  
IActionRunner* GetTraverseObjectActionHelper(Robot& robot, const ExternalInterface::TraverseObject& msg)
{
  ObjectID selectedObjectID = robot.GetBlockWorld().GetSelectedObject();
  
  if(static_cast<bool>(msg.usePreDockPose)) {
    return new DriveToAndTraverseObjectAction(selectedObjectID,
                                              msg.motionProf,
                                              msg.useManualSpeed);
  } else {
    return new TraverseObjectAction(selectedObjectID, msg.useManualSpeed);
  }
}
  
IActionRunner* GetMountChargerActionHelper(Robot& robot, const ExternalInterface::MountCharger& msg)
{
  ObjectID selectedObjectID = robot.GetBlockWorld().GetSelectedObject();
  
  if(static_cast<bool>(msg.usePreDockPose)) {
    return new DriveToAndMountChargerAction(selectedObjectID,
                                            msg.motionProf,
                                            msg.useManualSpeed);
  } else {
    return new MountChargerAction(selectedObjectID, msg.useManualSpeed);
  }
}

  
IActionRunner* GetFaceObjectActionHelper(Robot& robot, const ExternalInterface::FaceObject& msg)
{
  ObjectID objectID;
  if(msg.objectID == u32_MAX) {
    objectID = robot.GetBlockWorld().GetSelectedObject();
  } else {
    objectID = msg.objectID;
  }
  return new FaceObjectAction(objectID,
                              Radians(msg.turnAngleTol),
                              Radians(msg.maxTurnAngle),
                              msg.visuallyVerifyWhenDone,
                              msg.headTrackWhenDone);
}
  
IActionRunner* GetFacePoseActionHelper(Robot& robot, const ExternalInterface::FacePose& facePose)
{
  Pose3d pose(0, Z_AXIS_3D(), {facePose.world_x, facePose.world_y, facePose.world_z},
              robot.GetWorldOrigin());
  return new FacePoseAction(pose,
                            Radians(facePose.turnAngleTol),
                            Radians(facePose.maxTurnAngle));
}
  
IActionRunner* CreateNewActionByType(Robot& robot,
                                     const ExternalInterface::RobotActionUnion& actionUnion)
{
  using namespace ExternalInterface;
  
  switch(actionUnion.GetTag())
  {
    case RobotActionUnionTag::turnInPlace:
    {
      auto & turnInPlace = actionUnion.Get_turnInPlace();
      return new TurnInPlaceAction(turnInPlace.angle_rad, turnInPlace.isAbsolute);
    }
    case RobotActionUnionTag::playAnimation:
    {
      auto & playAnimation = actionUnion.Get_playAnimation();
      return new PlayAnimationAction(playAnimation.animationName, playAnimation.numLoops);
    }
    case RobotActionUnionTag::pickupObject:
      return GetPickupActionHelper(robot, actionUnion.Get_pickupObject());

    case RobotActionUnionTag::placeOnObject:
      return GetPlaceOnActionHelper(robot, actionUnion.Get_placeOnObject());
      
    case RobotActionUnionTag::placeRelObject:
      return GetPlaceRelActionHelper(robot, actionUnion.Get_placeRelObject());
      
    case RobotActionUnionTag::setHeadAngle:
      // TODO: Provide a means to pass in the speed/acceleration values to the action
      return new MoveHeadToAngleAction(actionUnion.Get_setHeadAngle().angle_rad);
      
    case RobotActionUnionTag::setLiftHeight:
      // TODO: Provide a means to pass in the speed/acceleration values to the action
      return new MoveLiftToHeightAction(actionUnion.Get_setLiftHeight().height_mm);
      
    case RobotActionUnionTag::faceObject:
      return GetFaceObjectActionHelper(robot, actionUnion.Get_faceObject());
      
    case RobotActionUnionTag::facePose:
      return GetFacePoseActionHelper(robot, actionUnion.Get_facePose());
      
    case RobotActionUnionTag::rollObject:
      return GetRollObjectActionHelper(robot, actionUnion.Get_rollObject());
      
    case RobotActionUnionTag::popAWheelie:
      return GetPopAWheelieActionHelper(robot, actionUnion.Get_popAWheelie());
      
    case RobotActionUnionTag::goToObject:
      return GetDriveToObjectActionHelper(robot, actionUnion.Get_goToObject());
      
    case RobotActionUnionTag::goToPose:
      return GetDriveToPoseActionHelper(robot, actionUnion.Get_goToPose());

    case RobotActionUnionTag::alignWithObject:
      return GetDriveToAlignWithObjectActionHelper(robot, actionUnion.Get_alignWithObject());

      
      // TODO: Add cases for other actions
      
    default:
      PRINT_NAMED_ERROR("RobotEventHandler.CreateNewActionByType.InvalidActionTag",
                        "Failed to create an action for the given actionTag.");
      return nullptr;
  }
}
  
void RobotEventHandler::QueueActionHelper(const QueueActionPosition position, const u32 idTag, const u32 inSlot,
                                          ActionList& actionList, IActionRunner* action, const u8 numRetries/* = 0*/)
{
  action->SetTag(idTag);
  
  QueueActionHelper(position, inSlot, actionList, action, numRetries);
}

void RobotEventHandler::QueueActionHelper(const QueueActionPosition position, const u32 inSlot,
                                          ActionList& actionList, IActionRunner* action, const u8 numRetries/* = 0*/)
{
  switch(position)
  {
    case QueueActionPosition::NOW:
    {
      actionList.QueueActionNow(inSlot, action, numRetries);
      break;
    }
    case QueueActionPosition::NOW_AND_CLEAR_REMAINING:
    {
      // Cancel all queued actions and make this action the next thing in it
      actionList.Cancel();
      actionList.QueueActionNext(inSlot, action, numRetries);
      break;
    }
    case QueueActionPosition::NEXT:
    {
      actionList.QueueActionNext(inSlot, action, numRetries);
      break;
    }
    case QueueActionPosition::AT_END:
    {
      actionList.QueueActionAtEnd(inSlot, action, numRetries);
      break;
    }
    default:
    {
      PRINT_NAMED_ERROR("CozmoGameImpl.QueueActionHelper.InvalidPosition",
                        "Unrecognized 'position' for queuing action.\n");
      return;
    }
  }
}
  
void RobotEventHandler::HandleActionEvents(const AnkiEvent<ExternalInterface::MessageGameToEngine>& event)
{
  RobotID_t robotID = 1; // We init the robotID to 1
  Robot* robotPointer = _robotManager.GetRobotByID(robotID);
  
  // If we don't have a valid robot there's nothing to do
  if (nullptr == robotPointer)
  {
    return;
  }
  
  // We'll pass around a reference to the Robot for convenience sake
  Robot& robot = *robotPointer;
  
  // Now we fill out our Action and possibly update number of retries:
  IActionRunner* newAction = nullptr;
  u8 numRetries = 0;
  switch (event.GetData().GetTag())
  {
    case ExternalInterface::MessageGameToEngineTag::PlaceObjectOnGround:
    {
      numRetries = 1;
      newAction = GetPlaceObjectOnGroundActionHelper(robot, event.GetData().Get_PlaceObjectOnGround());
      break;
    }
    case ExternalInterface::MessageGameToEngineTag::PlaceObjectOnGroundHere:
    {
      newAction = new PlaceObjectOnGroundAction();
      break;
    }
    case ExternalInterface::MessageGameToEngineTag::GotoPose:
    {
      numRetries = 2;
      newAction = GetDriveToPoseActionHelper(robot, event.GetData().Get_GotoPose());
      break;
    }
    case ExternalInterface::MessageGameToEngineTag::GotoObject:
    {
      newAction = GetDriveToObjectActionHelper(robot, event.GetData().Get_GotoObject());
      break;
    }
    case ExternalInterface::MessageGameToEngineTag::AlignWithObject:
    {
      newAction = GetDriveToAlignWithObjectActionHelper(robot, event.GetData().Get_AlignWithObject());
      break;
    }
    case ExternalInterface::MessageGameToEngineTag::PickupObject:
    {
      numRetries = 1;
      newAction = GetPickupActionHelper(robot, event.GetData().Get_PickupObject());
      break;
    }
    case ExternalInterface::MessageGameToEngineTag::PlaceOnObject:
    {
      numRetries = 1;
      newAction = GetPlaceOnActionHelper(robot, event.GetData().Get_PlaceOnObject());
      break;
    }
    case ExternalInterface::MessageGameToEngineTag::PlaceRelObject:
    {
      numRetries = 1;
      newAction = GetPlaceRelActionHelper(robot, event.GetData().Get_PlaceRelObject());
      break;
    }
    case ExternalInterface::MessageGameToEngineTag::RollObject:
    {
      numRetries = 1;
      newAction = GetRollObjectActionHelper(robot, event.GetData().Get_RollObject());
      break;
    }
    case ExternalInterface::MessageGameToEngineTag::PopAWheelie:
    {
      numRetries = 1;
      newAction = GetPopAWheelieActionHelper(robot, event.GetData().Get_PopAWheelie());
      break;
    }
    case ExternalInterface::MessageGameToEngineTag::MountCharger:
    {
      numRetries = 1;
      newAction = GetMountChargerActionHelper(robot, event.GetData().Get_MountCharger());
      break;
    }
    case ExternalInterface::MessageGameToEngineTag::TraverseObject:
    {
      numRetries = 1;
      newAction = GetTraverseObjectActionHelper(robot, event.GetData().Get_TraverseObject());
      break;
    }
    case ExternalInterface::MessageGameToEngineTag::PlayAnimation:
    {
      const ExternalInterface::PlayAnimation& msg = event.GetData().Get_PlayAnimation();
      newAction = new PlayAnimationAction(msg.animationName, msg.numLoops);
      break;
    }
    case ExternalInterface::MessageGameToEngineTag::FaceObject:
    {
      newAction = GetFaceObjectActionHelper(robot, event.GetData().Get_FaceObject());
      break;
    }
    case ExternalInterface::MessageGameToEngineTag::FacePose:
    {
      const ExternalInterface::FacePose& facePose = event.GetData().Get_FacePose();
      newAction = GetFacePoseActionHelper(robot, facePose);
      break;
    }
    case ExternalInterface::MessageGameToEngineTag::TurnInPlace:
    {
      newAction = new TurnInPlaceAction(event.GetData().Get_TurnInPlace().angle_rad,
                                        event.GetData().Get_TurnInPlace().isAbsolute);
      break;
    }
    default:
    {
      PRINT_STREAM_ERROR("RobotEventHandler.HandleEvents",
                         "Subscribed to unhandled event of type "
                         << ExternalInterface::MessageGameToEngineTagToString(event.GetData().GetTag()) << "!");
      
      // We don't know what to do; bail;
      return;
    }
  }
  
  // Everything's ok and we have an action, so queue it
  QueueActionHelper(QueueActionPosition::AT_END, Robot::DriveAndManipulateSlot, robot.GetActionList(), newAction, numRetries);
}
  
void RobotEventHandler::HandleQueueSingleAction(const AnkiEvent<ExternalInterface::MessageGameToEngine>& event)
{
  const ExternalInterface::QueueSingleAction& msg = event.GetData().Get_QueueSingleAction();
  
  // Can't queue actions for nonexistent robots...
  Robot* robot = _robotManager.GetRobotByID(msg.robotID);
  if (nullptr == robot)
  {
    return;
  }
  
  IActionRunner* action = CreateNewActionByType(*robot, msg.action);
  
  // Put the action in the given position of the specified queue:
  QueueActionHelper(msg.position, msg.idTag, msg.inSlot, robot->GetActionList(), action, msg.numRetries);
}
  
void RobotEventHandler::HandleQueueCompoundAction(const AnkiEvent<ExternalInterface::MessageGameToEngine>& event)
{
  const ExternalInterface::QueueCompoundAction& msg = event.GetData().Get_QueueCompoundAction();
  
  // Can't queue actions for nonexistent robots...
  Robot* robot = _robotManager.GetRobotByID(msg.robotID);
  if (nullptr == robot)
  {
    return;
  }
  
  // Create an empty parallel or sequential compound action:
  ICompoundAction* compoundAction = nullptr;
  if(msg.parallel) {
    compoundAction = new CompoundActionParallel();
  } else {
    compoundAction = new CompoundActionSequential();
  }
  
  // Add all the actions in the message to the compound action, according
  // to their type
  for(size_t iAction=0; iAction < msg.actions.size(); ++iAction) {
    
    IActionRunner* action = CreateNewActionByType(*robot, msg.actions[iAction]);
    
    compoundAction->AddAction(action);
    
  } // for each action/actionType
  
  // Put the action in the given position of the specified queue:
  QueueActionHelper(msg.position, msg.idTag, msg.inSlot, robot->GetActionList(),
                    compoundAction, msg.numRetries);
}
  
void RobotEventHandler::HandleSetLiftHeight(const AnkiEvent<ExternalInterface::MessageGameToEngine>& event)
{
  // TODO: get RobotID in a non-hack way
  RobotID_t robotID = 1;
  Robot* robot = _robotManager.GetRobotByID(robotID);
  
  // We need a robot
  if (nullptr == robot)
  {
    return;
  }
  
  if(robot->GetMoveComponent().IsLiftLocked()) {
    PRINT_NAMED_INFO("RobotEventHandler.HandleSetLiftHeight.LiftLocked",
                     "Ignoring ExternalInterface::SetLiftHeight while lift is locked.");
  } else {
    const ExternalInterface::SetLiftHeight& msg = event.GetData().Get_SetLiftHeight();
    
    // Special case if commanding low dock height
    if (msg.height_mm == LIFT_HEIGHT_LOWDOCK && robot->IsCarryingObject()) {
      
      // Put the block down right here
      IActionRunner* newAction = new PlaceObjectOnGroundAction();
      QueueActionHelper(QueueActionPosition::AT_END, Robot::DriveAndManipulateSlot, robot->GetActionList(), newAction);
    }
    else {
      // In the normal case directly set the lift height
      robot->GetMoveComponent().MoveLiftToHeight(msg.height_mm, msg.max_speed_rad_per_sec, msg.accel_rad_per_sec2, msg.duration_sec);
    }
  }
}
  
void RobotEventHandler::HandleDisplayProceduralFace(const AnkiEvent<ExternalInterface::MessageGameToEngine>& event)
{
  const ExternalInterface::DisplayProceduralFace& msg = event.GetData().Get_DisplayProceduralFace();

  Robot* robot = _robotManager.GetRobotByID(msg.robotID);
  
  // We need a robot
  if (nullptr == robot)
  {
    return;
  }
  
  ProceduralFace procFace;
  procFace.GetParams().SetFromMessage(msg);
  procFace.SetTimeStamp(robot->GetLastMsgTimestamp());
  
  robot->SetProceduralFace(procFace);
}
  
  void RobotEventHandler::HandleForceDelocalizeRobot(const AnkiEvent<ExternalInterface::MessageGameToEngine>& event)
  {
    RobotID_t robotID = event.GetData().Get_ForceDelocalizeRobot().robotID;

    Robot* robot = _robotManager.GetRobotByID(robotID);
    
    // We need a robot
    if (nullptr == robot) {
      PRINT_NAMED_ERROR("RobotEventHandler.HandleForceDelocalizeRobot.InvalidRobotID",
                        "Failed to find robot %d to delocalize.", robotID);
      
      
    } else {
      PRINT_NAMED_INFO("RobotMessageHandler.ProcessMessage.ForceDelocalize",
                       "Forcibly delocalizing robot %d", robotID);
      
      robot->Delocalize();
    }
  }
  
void RobotEventHandler::HandleMoodEvent(const AnkiEvent<ExternalInterface::MessageGameToEngine>& event)
{
  const auto& eventData = event.GetData();
  const RobotID_t robotID = eventData.Get_MoodMessage().robotID;
  
  Robot* robot = _robotManager.GetRobotByID(robotID);
  
  // We need a robot
  if (nullptr == robot)
  {
    PRINT_NAMED_ERROR("RobotEventHandler.HandleMoodEvent.InvalidRobotID", "Failed to find robot %u.", robotID);
  }
  else
  {
    robot->GetMoodManager().HandleEvent(event);
  }
}
  
void RobotEventHandler::HandleProgressionEvent(const AnkiEvent<ExternalInterface::MessageGameToEngine>& event)
{
  const auto& eventData = event.GetData();
  const RobotID_t robotID = eventData.Get_ProgressionMessage().robotID;
  
  Robot* robot = _robotManager.GetRobotByID(robotID);
  
  // We need a robot
  if (nullptr == robot)
  {
    PRINT_NAMED_ERROR("RobotEventHandler.HandleProgressionEvent.InvalidRobotID", "Failed to find robot %u.", robotID);
  }
  else
  {
    robot->GetProgressionManager().HandleEvent(event);
  }
}

} // namespace Cozmo
} // namespace Anki

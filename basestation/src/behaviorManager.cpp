
/**
 * File: behaviorManager.cpp
 *
 * Author: Kevin Yoon
 * Date:   2/27/2014
 *
 * Description:
 *
 * Copyright: Anki, Inc. 2014
 **/

#include "behaviorManager.h"
#include "cozmoActions.h"
#include "pathPlanner.h"
#include "vizManager.h"
#include "soundManager.h"

#include "anki/common/basestation/utils/timer.h"
#include "anki/common/shared/utilities_shared.h"
#include "anki/common/basestation/math/point_impl.h"
#include "anki/common/basestation/math/poseBase_impl.h"

#include "anki/cozmo/basestation/robot.h"
#include "anki/cozmo/basestation/blockWorld.h"
#include "anki/cozmo/robot/cozmoConfig.h"

// The angle wrt the mat at which the user is expected to be.
// For happy head-nodding demo purposes.
#define USER_LOC_ANGLE_WRT_MAT -1.57

#define JUNE_DEMO_START_X 150.0
#define JUNE_DEMO_START_Y -120.0
#define JUNE_DEMO_START_THETA 0.0

namespace Anki {
  namespace Cozmo {
    
    static Result ScaredReaction(Robot* robot, Vision::ObservedMarker* marker)
    {
      PRINT_INFO("Saw Scary Block!\n");
      robot->SetBehaviorState(BehaviorManager::SCARED_FLEE);
      return RESULT_OK;
    }
    
    static Result ExcitedReaction(Robot* robot, Vision::ObservedMarker* marker)
    {
      PRINT_INFO("Saw Exciting Block!\n");
      robot->SetBehaviorState(BehaviorManager::EXCITABLE_CHASE);
      return RESULT_OK;
    }
    
    static bool IsMarkerCloseEnoughAndCentered(const Vision::ObservedMarker* marker, const u16 ncols)
    {
      bool result = false;
      
      // Parameters:
      const f32 minDiagSize       = 50.f;
      const f32 maxDistFromCenter = 35.f;
      
      const f32 diag1 = (marker->GetImageCorners()[Quad::TopLeft]  - marker->GetImageCorners()[Quad::BottomRight]).Length();
      const f32 diag2 = (marker->GetImageCorners()[Quad::TopRight] - marker->GetImageCorners()[Quad::BottomLeft]).Length();
      
      // If the marker is large enough in our field of view (this is a proxy for
      // "close enough" without needing to compute actual pose)
      if(diag1 >= minDiagSize && diag2 >= minDiagSize) {
        // If the marker is centered in the field of view (this is a proxy for
        // "robot is facing the marker")
        const Point2f centroid = marker->GetImageCorners().ComputeCentroid();
        if(std::abs(centroid.x() - static_cast<f32>(ncols/2)) < maxDistFromCenter) {
          result = true;
        }
      }
      
      return result;
    }
    
    static Result ArrowCallback(Robot* robot, Vision::ObservedMarker* marker)
    {
      Result lastResult = RESULT_OK;
     
      // Parameters (pass in?)
      const f32 driveSpeed = 30.f;
      
      if(robot->IsIdle() && IsMarkerCloseEnoughAndCentered(marker, robot->GetCamera().GetCalibration().GetNcols())) {
        
        Vec2f upVector = marker->GetImageCorners()[Quad::TopLeft] - marker->GetImageCorners()[Quad::BottomLeft];
        
        // Decide what to do based on the orientation of the arrow
        // NOTE: Remember that Y axis points down in image coordinates.
        
        const f32 angle = atan2(upVector.y(), upVector.x());
        
        if(angle >= -3.f*M_PI_4 && angle < -M_PI_4) { // UP
          PRINT_INFO("UP Arrow!\n");
          lastResult = robot->DriveWheels(driveSpeed, driveSpeed);
        }
        else if(angle >= -M_PI_4 && angle < M_PI_4) { // RIGHT
          PRINT_INFO("RIGHT Arrow!\n");
          //lastResult = robot->QueueAction(new TurnInPlaceAction(-M_PI_2));
          robot->GetActionList().AddAction(new TurnInPlaceAction(-M_PI_2));
        }
        else if(angle >= M_PI_4 && angle < 3*M_PI_4) { // DOWN
          PRINT_INFO("DOWN Arrow!\n");
          lastResult = robot->DriveWheels(-driveSpeed, -driveSpeed);
        }
        else if(angle >= 3*M_PI_4 || angle < -3*M_PI_4) { // LEFT
          PRINT_INFO("LEFT Arrow!\n");
          //lastResult = robot->QueueAction(new TurnInPlaceAction(M_PI_2));
          robot->GetActionList().AddAction(new TurnInPlaceAction(M_PI_2));
        }
        else {
          PRINT_NAMED_ERROR("TurnCallback.UnexpectedAngle",
                            "Unexpected angle for arrow marker: %.3f radians (%.1f degrees)\n",
                            angle, angle*180.f/M_PI);
          lastResult = RESULT_FAIL;
        }
      } // IfMarkerIsCloseEnoughAndCentered()
      
      return lastResult;
      
    } // ArrowCallback()
    
    static Result TurnAroundCallback(Robot* robot, Vision::ObservedMarker* marker)
    {
      Result lastResult = RESULT_OK;
      
      if(robot->IsIdle() && IsMarkerCloseEnoughAndCentered(marker, robot->GetCamera().GetCalibration().GetNcols())) {
        PRINT_INFO("TURNAROUND Arrow!\n");
        //lastResult = robot->QueueAction(new TurnInPlaceAction(M_PI));
        robot->GetActionList().AddAction(new TurnInPlaceAction(M_PI));
      } // IfMarkerIsCloseEnoughAndCentered()
      
      return lastResult;
    } // TurnAroundCallback()
    
    static Result StopCallback(Robot* robot, Vision::ObservedMarker* marker)
    {
      Result lastResult = RESULT_OK;

      if(IsMarkerCloseEnoughAndCentered(marker, robot->GetCamera().GetCalibration().GetNcols())) {
        lastResult = robot->StopAllMotors();
      }
      
      return lastResult;
    }
    
    
    BehaviorManager::BehaviorManager(Robot* robot)
    : _mode(None)
    , _robot(robot)
    {
      Reset();
      
      // NOTE: Do not _use_ the _robot pointer in this constructor because
      //  this constructor is being called from Robot's constructor.
      
      
      CORETECH_ASSERT(_robot != nullptr);
    }

    void BehaviorManager::StartMode(Mode mode)
    {
      Reset();
      _mode = mode;
      switch(mode) {
        case None:
          CoreTechPrint("Starting NONE behavior\n");
          break;
          
        case June2014DiceDemo:
          CoreTechPrint("Starting June demo behavior\n");
          _state     = WAITING_FOR_ROBOT;
          _nextState = DRIVE_TO_START;
          _updateFcn = &BehaviorManager::Update_June2014DiceDemo;
          _idleState = IDLE_NONE;
          _timesIdle = 0;
          SoundManager::getInstance()->Play(SOUND_DEMO_START);
          break;
          
        case ReactToMarkers:
          CoreTechPrint("Starting ReactToMarkers behavior\n");
          
          // Testing Reactions:
          _robot->AddReactionCallback(Vision::MARKER_ARROW,         &ArrowCallback);
          _robot->AddReactionCallback(Vision::MARKER_STOPWITHHAND,  &StopCallback);
          _robot->AddReactionCallback(Vision::MARKER_CIRCULARARROW, &TurnAroundCallback);
          
          // Once the callbacks are added
          StartMode(None);
          break;
          
        case CREEP:
        {
          CoreTechPrint("Starting Cozmo Robotic Emotional Engagement Playtest (CREEP)\n");
          
          _updateFcn = &BehaviorManager::Update_CREEP;
          
          _state     = SLEEPING;
          _nextState = SLEEPING;
          _stateAnimStarted = false;
          
          VizManager::getInstance()->SetText(VizManager::BEHAVIOR_STATE, NamedColors::YELLOW, GetBehaviorStateName(_state).c_str());
          
          _transitionManager.Clear();
          
          SoundManager::getInstance()->SetScheme(SOUND_SCHEME_CREEP);
          
          std::function<void()> wakeUpEvent = [this]() {
            _robot->PlayAnimation("ANIM_WAKE_UP");
          };
          
          std::function<void()> screamEvent = [this]() {
            SoundManager::getInstance()->Play(SOUND_SCREAM);
            //_robot->PlayAnimation("ANIM_OK_GOT_IT");
          };
          
          std::function<void()> reliefEvent = [this]() {
            SoundManager::getInstance()->Play(SOUND_PHEW);
            //_robot->PlayAnimation("ANIM_OK_DONE");
          };
          
          std::function<void()> excitedEvent = [this]() {
            SoundManager::getInstance()->Play(SOUND_OOH);
          };
          
          _transitionManager.AddTransition(SLEEPING,    NUM_STATES,      wakeUpEvent,  3.0);
          _transitionManager.AddTransition(NUM_STATES,  SCARED_FLEE,     screamEvent,  0.5);
          _transitionManager.AddTransition(SCARED_FLEE, NUM_STATES,      reliefEvent,  0.5);
          _transitionManager.AddTransition(NUM_STATES,  EXCITABLE_CHASE, excitedEvent, 0.5);
          
          // TODO: Fill in state animation lookup
          _stateAnimations[SLEEPING]        = "ANIM_SLEEPING";
          _stateAnimations[EXCITABLE_CHASE] = "ANIM_EXCITABLE_CHASE";
          _stateAnimations[SCAN]            = "ANIM_SCAN";
          _stateAnimations[SCARED_FLEE]     = "ANIM_SCARED_FLEE";
          _stateAnimations[DANCE_WITH_BLOCK]= "ANIM_SINGING";
          _stateAnimations[HELP_ME_STATE]   = "ANIM_HELPME";
          
          // Automatically switch states as reactions to certain markers:
          _robot->AddReactionCallback(Vision::MARKER_ANGRYFACE, &ScaredReaction);
          _robot->AddReactionCallback(Vision::MARKER_STAR5,     &ExcitedReaction);
          
          break;
        } // case CREEP
          
          
        default:
          PRINT_NAMED_ERROR("BehaviorManager.InvalidMode", "Invalid behavior mode");
          return;
      }
      
      //assert(_updateFcn != nullptr);
      
    } // StartMode()
    
    
    const std::string& BehaviorManager::GetBehaviorStateName(BehaviorState state) const
    {
      static const std::map<BehaviorState, std::string> nameLUT = {
        {EXCITABLE_CHASE, "EXCITABLE_CHASE"},
        {SCARED_FLEE,     "SCARED_FLEE"},
        {DANCE_WITH_BLOCK,"DANCE_WITH_BLOCK"},
        {SCAN,            "SCAN"},
        {HELP_ME_STATE,   "HELP_ME_STATE"},
        {SLEEPING,        "SLEEPING"},
      };
      
      static const std::string UNKNOWN("UNKNOWN");
      
      auto nameIter = nameLUT.find(state);
      if(nameIter == nameLUT.end()) {
        PRINT_NAMED_WARNING("BehaviorManager.GetBehaviorStateName.UnknownName",
                            "No string name stored for behavior state %d.\n", state);
        return UNKNOWN;
      } else {
        return nameIter->second;
      }
    }
    
    
    void BehaviorManager::SetNextState(BehaviorState nextState)
    {
      bool validState = false;
      switch(nextState)
      {
        case EXCITABLE_CHASE:
        case SCARED_FLEE:
        case DANCE_WITH_BLOCK:
        case SCAN:
        case HELP_ME_STATE:
        {
          if(_mode == CREEP) {
            validState = true;
          }
          break;
        }
        
        case ACKNOWLEDGEMENT_NOD:
        case DRIVE_TO_START:
        case WAITING_TO_SEE_DICE:
        case WAITING_FOR_DICE_TO_DISAPPEAR:
        case GOTO_EXPLORATION_POSE:
        case START_EXPLORING_TURN:
        case BACKING_UP:
        case BEGIN_EXPLORING:
        case EXPLORING:
        case CHECK_IT_OUT_UP:
        case CHECK_IT_OUT_DOWN:
        case FACE_USER:
        case HAPPY_NODDING:
        case BACK_AND_FORTH_EXCITED:
        {
          if(_mode == June2014DiceDemo) {
            validState = true;
          }
          break;
        }
          
        default:
          validState = false;
          
      } // switch(nextState)
      
      
      if(validState) {
        _nextState = nextState;
      } else {
        
        PRINT_NAMED_ERROR("BehaviorManager.SetNextState.InvalidStateForMode",
                          "Invalid state for current mode.\n");
      }
    } // SetNextState()
    
    BehaviorManager::Mode BehaviorManager::GetMode() const
    {
      return _mode;
    }
    
    void BehaviorManager::Reset()
    {
      _state = WAITING_FOR_ROBOT;
      _nextState = _state;
      _updateFcn = nullptr;
      
      // June2014DiceDemo
      _explorationStartAngle = 0;
      _objectToPickUp.UnSet();
      _objectToPlaceOn.UnSet();
      
    } // Reset()
    
    const ObjectID BehaviorManager::GetObjectOfInterest() const
    {
      return _robot->GetBlockWorld().GetSelectedObject();
    }
    
    
    void BehaviorManager::Update()
    {
      // Shared states
      switch(_state) {
        case WAITING_FOR_ROBOT:
        {
          // Nothing to do here anymore: we should not be "waiting" on a robot
          // because BehaviorManager is now part of a robot!
          _state = _nextState;
          break;
        }
        default:
          if (_updateFcn) {
            _updateFcn(this);
          } else {
            _state = _nextState = WAITING_FOR_ROBOT;
          }
          break;
      }

      
    } // Update()
    
    

    /********************************************************
     * June2014DiceDemo
     *
     * Look for two dice rolls. Look for the block with the 
     * number corresponding to the first roll and pick it up.
     * Place it on the block with the number corresponding to
     * the second roll.
     *
     ********************************************************/
    void BehaviorManager::Update_June2014DiceDemo()
    {

      static const ActionList::SlotHandle TraversalSlot = 0;
      
      constexpr float checkItOutAngleUp = DEG_TO_RAD(15);
      constexpr float checkItOutAngleDown = DEG_TO_RAD(-10);
      constexpr float checkItOutSpeed = 0.4;

      switch(_state) {

        case DRIVE_TO_START:
        {
          // Wait for robot to be IDLE
          if(_robot->IsIdle()) {
            Pose3d startPose(JUNE_DEMO_START_THETA,
                             Z_AXIS_3D,
                             {{JUNE_DEMO_START_X, JUNE_DEMO_START_Y, 0.f}});
            CoreTechPrint("Driving to demo start location\n");
            _robot->GetActionList().QueueActionAtEnd(TraversalSlot, new DriveToPoseAction(startPose));

            _state = WAITING_TO_SEE_DICE;

            _robot->SetDefaultLights(0x008080, 0x008080);
          }

          break;
        }
          
        case WAITING_FOR_DICE_TO_DISAPPEAR:
        {
          const BlockWorld::ObjectsMapByID_t& diceBlocks = _robot->GetBlockWorld().GetExistingObjectsByType(Block::Type::DICE);
          
          if(diceBlocks.empty()) {
            
            // Check to see if the dice block has been gone for long enough
            const TimeStamp_t timeSinceSeenDice_ms = BaseStationTimer::getInstance()->GetCurrentTimeStamp() - _diceDeletionTime;
            if(timeSinceSeenDice_ms > TimeBetweenDice_ms) {
              CoreTechPrint("First dice is gone: ready for next dice!\n");
              _state = WAITING_TO_SEE_DICE;
            }
          } else {
            _robot->GetBlockWorld().ClearObjectsByType(Block::Type::DICE);
            _diceDeletionTime = BaseStationTimer::getInstance()->GetCurrentTimeStamp();
            if (_waitUntilTime < BaseStationTimer::getInstance()->GetCurrentTimeInSeconds()) {
              // Keep clearing blocks until we don't see them anymore
              CoreTechPrint("Please move first dice away.\n");
              _robot->PlayAnimation("ANIM_HEAD_NOD", 2);
              _waitUntilTime = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + 5;
              SoundManager::getInstance()->Play(SOUND_WAITING4DICE2DISAPPEAR);
            }
          }
          break;
        }
          
        case WAITING_TO_SEE_DICE:
        {
          /*
          // DEBUG!!!
          _objectToPickUp = Block::NUMBER5_BLOCK_TYPE;
          _objectToPlaceOn = Block::NUMBER6_BLOCK_TYPE;
          _state = BEGIN_EXPLORING;
          break;
          */
          
          const f32 diceViewingHeadAngle = DEG_TO_RAD(-15);

          // Wait for robot to be IDLE
          if(_robot->IsIdle())
          {
            const BlockWorld::ObjectsMapByID_t& diceBlocks = _robot->GetBlockWorld().GetExistingObjectsByType(Block::Type::DICE);
            if(!diceBlocks.empty()) {
              
              if(diceBlocks.size() > 1) {
                // Multiple dice blocks in the world, keep deleting them all
                // until we only see one
                CoreTechPrint("More than one dice block found!\n");
                _robot->GetBlockWorld().ClearObjectsByType(Block::Type::DICE);
                
              } else {
                
                Block* diceBlock = dynamic_cast<Block*>(diceBlocks.begin()->second);
                CORETECH_ASSERT(diceBlock != nullptr);
                
                // Get all the observed markers on the dice and look for the one
                // facing up (i.e. the one that is nearly aligned with the z axis)
                // TODO: expose the threshold here?
                const TimeStamp_t timeWindow = _robot->GetLastMsgTimestamp() - 500;
                const f32 dotprodThresh = 1.f - cos(DEG_TO_RAD(20));
                std::vector<const Vision::KnownMarker*> diceMarkers;
                diceBlock->GetObservedMarkers(diceMarkers, timeWindow);
                
                const Vision::KnownMarker* topMarker = nullptr;
                for(auto marker : diceMarkers) {
                  //const f32 dotprod = DotProduct(marker->ComputeNormal(), Z_AXIS_3D);
                  Pose3d markerWrtRobotOrigin;
                  if(marker->GetPose().GetWithRespectTo(_robot->GetPose().FindOrigin(), markerWrtRobotOrigin) == false) {
                    PRINT_NAMED_ERROR("BehaviorManager.Update_June2014DiceDemo.MarkerOriginNotRobotOrigin",
                                      "Marker should share the same origin as the robot that observed it.\n");
                    Reset();
                  }
                  const f32 dotprod = marker->ComputeNormal(markerWrtRobotOrigin).z();
                  if(NEAR(dotprod, 1.f, dotprodThresh)) {
                    topMarker = marker;
                  }
                }
                
                // If dice exists in world but we haven't seen it for a while, delete it.
                if (diceMarkers.empty()) {
                  diceBlock->GetObservedMarkers(diceMarkers, _robot->GetLastMsgTimestamp() - 2000);
                  if (diceMarkers.empty()) {
                    CoreTechPrint("Haven't see dice marker for a while. Deleting dice.");
                    _robot->GetBlockWorld().ClearObjectsByType(Block::Type::DICE);
                    break;
                  }
                }
                
                if(topMarker != nullptr) {
                  // We found and observed the top marker on the dice. Use it to
                  // set which block we are looking for.
                  
                  // Don't forget to remove the dice as an ignore type for
                  // planning, since we _do_ want to avoid it as an obstacle
                  // when driving to pick and place blocks
                  _robot->GetPathPlanner()->RemoveIgnoreType(Block::Type::DICE);
                  
                  ObjectType blockToLookFor;
                  switch(static_cast<Vision::MarkerType>(topMarker->GetCode()))
                  {
                    case Vision::MARKER_DICE1:
                    {
                      blockToLookFor = Block::Type::NUMBER1;
                      break;
                    }
                    case Vision::MARKER_DICE2:
                    {
                      blockToLookFor = Block::Type::NUMBER2;
                      break;
                    }
                    case Vision::MARKER_DICE3:
                    {
                      blockToLookFor = Block::Type::NUMBER3;
                      break;
                    }
                    case Vision::MARKER_DICE4:
                    {
                      blockToLookFor = Block::Type::NUMBER4;
                      break;
                    }
                    case Vision::MARKER_DICE5:
                    {
                      blockToLookFor = Block::Type::NUMBER5;
                      break;
                    }
                    case Vision::MARKER_DICE6:
                    {
                      blockToLookFor = Block::Type::NUMBER6;
                      break;
                    }
                      
                    default:
                      PRINT_NAMED_ERROR("BehaviorManager.UnknownDiceMarker",
                                        "Found unexpected marker on dice: %s!",
                                        Vision::MarkerTypeStrings[topMarker->GetCode()]);
                      StartMode(None);
                      return;
                  } // switch(topMarker->GetCode())
                  
                  CoreTechPrint("Found top marker on dice: %s!\n",
                                Vision::MarkerTypeStrings[topMarker->GetCode()]);
                  
                  if(_objectToPickUp.IsUnknown()) {
                    
                    _objectToPickUp = blockToLookFor;
                    _objectToPlaceOn.SetToUnknown();
                    
                    CoreTechPrint("Set blockToPickUp = %s\n", _objectToPickUp.GetName().c_str());
                    
                    // Wait for first dice to disappear
                    _state = WAITING_FOR_DICE_TO_DISAPPEAR;

                    SoundManager::getInstance()->Play(SOUND_OK_GOT_IT);
                    
                    _waitUntilTime = 0;
                  } else {

                    if(blockToLookFor == _objectToPickUp) {
                      CoreTechPrint("Can't put a object on itself!\n");
                      // TODO:(bn) left and right + sad noise?
                    }
                    else {

                      _objectToPlaceOn = blockToLookFor;
                    
                      CoreTechPrint("Set objectToPlaceOn = %s\n", _objectToPlaceOn.GetName().c_str());

                      _robot->PlayAnimation("ANIM_HEAD_NOD", 2);
                      _waitUntilTime = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + 2.5;

                      _state = BEGIN_EXPLORING;

                      SoundManager::getInstance()->Play(SOUND_OK_GOT_IT);
                    }
                  }
                } else {
                  
                  CoreTechPrint("Found dice, but not its top marker.\n");
                  
                  //dockBlock_ = dynamic_cast<Block*>(diceBlock);
                  //CORETECH_THROW_IF(dockBlock_ == nullptr);
                  
                  // Try driving closer to dice
                  // Since we are purposefully trying to get really close to the
                  // dice, ignore it as an obstacle.  We'll consider an obstacle
                  // again later, when we start driving around to pick and place.
                  _robot->GetPathPlanner()->AddIgnoreType(Block::Type::DICE);
                  
                  Vec3f position( _robot->GetPose().GetTranslation() );
                  position -= diceBlock->GetPose().GetTranslation();
                  f32 actualDistToDice = position.Length();
                  f32 desiredDistToDice = ROBOT_BOUNDING_X_FRONT + 0.5f*diceBlock->GetSize().Length() + 5.f;

                  if (actualDistToDice > desiredDistToDice + 5) {
                    position.MakeUnitLength();
                    position *= desiredDistToDice;
                  
                    Radians angle = atan2(position.y(), position.x()) + PI_F;
                    position += diceBlock->GetPose().GetTranslation();
                    
                    _goalPose = Pose3d(angle, Z_AXIS_3D, {{position.x(), position.y(), 0.f}});
                    
                    _robot->GetActionList().QueueActionAtEnd(TraversalSlot, new DriveToPoseAction(_goalPose));

                  } else {
                    CoreTechPrint("Move dice closer!\n");
                  }
                  
                } // IF / ELSE top marker seen
                
              } // IF only one dice
              
              _timesIdle = 0;

            } // IF any diceBlocks available
            
            else {

              constexpr int numIdleForFrustrated = 3;
              constexpr float headUpWaitingAngle = DEG_TO_RAD(20);
              constexpr float headUpWaitingAngleFrustrated = DEG_TO_RAD(25);
              // Can't see dice
              switch(_idleState) {
                case IDLE_NONE:
                {
                  // if its been long enough, look up
                  if (_waitUntilTime < BaseStationTimer::getInstance()->GetCurrentTimeInSeconds()) {
                    if(++_timesIdle >= numIdleForFrustrated) {
                      SoundManager::getInstance()->Play(SOUND_WAITING4DICE);

                      _originalPose = _robot->GetPose();

                      Pose3d userFacingPose = _robot->GetPose();
                      userFacingPose.SetRotation(USER_LOC_ANGLE_WRT_MAT, Z_AXIS_3D);
                      _robot->GetActionList().QueueActionAtEnd(TraversalSlot, new DriveToPoseAction(userFacingPose));
                      CoreTechPrint("idle: facing user\n");

                      _idleState = IDLE_FACING_USER;
                    }
                    else {
                      CoreTechPrint("idle: looking up\n");
                      _robot->MoveHeadToAngle(headUpWaitingAngle, 3.0, 10);
                      _idleState = IDLE_LOOKING_UP;
                      _waitUntilTime = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + 0.7;
                    }
                  }
                  break;
                }

                case IDLE_LOOKING_UP:
                {
                  // once we get to the top, play the sound

                  if (_waitUntilTime < BaseStationTimer::getInstance()->GetCurrentTimeInSeconds()) {
                    CoreTechPrint("idle: playing sound\n");
                    SoundManager::getInstance()->Play(SOUND_WAITING4DICE);
                    _idleState = IDLE_PLAYING_SOUND;
                    if(_timesIdle >= numIdleForFrustrated) {
                      _waitUntilTime = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + 2.0;
                      SoundManager::getInstance()->Play(SOUND_WAITING4DICE);
                      SoundManager::getInstance()->Play(SOUND_WAITING4DICE);
                    }
                    else {
                      _waitUntilTime = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + 0.5;
                    }
                  }
                  break;
                }

                case IDLE_PLAYING_SOUND:
                {
                  // once the sound is done, look back down
                  if (_waitUntilTime < BaseStationTimer::getInstance()->GetCurrentTimeInSeconds()) {
                    CoreTechPrint("idle: looking back down\n");
                    _robot->MoveHeadToAngle(diceViewingHeadAngle, 1.5, 10);
                    if(_timesIdle >= numIdleForFrustrated) {
                      SoundManager::getInstance()->Play(SOUND_WAITING4DICE);
                      _waitUntilTime = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + 2;
                      _idleState = IDLE_LOOKING_DOWN;
                    }
                    else {
                      _idleState = IDLE_NONE;
                      _waitUntilTime = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + 5;
                    }
                  }
                  break;
                }

                case IDLE_FACING_USER:
                {
                  // once we get there, look up
                  if(_robot->IsIdle()) {
                    SoundManager::getInstance()->Play(SOUND_WAITING4DICE);
                    CoreTechPrint("idle: looking up\n");
                    _robot->MoveHeadToAngle(headUpWaitingAngleFrustrated, 3.0, 10);
                    _idleState = IDLE_LOOKING_UP;
                    _waitUntilTime = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + 2;
                  }
                  break;
                }

                case IDLE_LOOKING_DOWN:
                {
                  // once we are looking back down, turn back to the original pose
                  if(_waitUntilTime < BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() &&
                     _robot->IsIdle()) {

                    CoreTechPrint("idle: turning back\n");
                    SoundManager::getInstance()->Play(SOUND_WAITING4DICE);
                    _robot->GetActionList().QueueActionAtEnd(TraversalSlot, new DriveToPoseAction(_originalPose));
                    _idleState = IDLE_TURNING_BACK;
                    _waitUntilTime = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + 0.25;
                  }
                  break;
                }

                case IDLE_TURNING_BACK:
                {
                  if (_waitUntilTime < BaseStationTimer::getInstance()->GetCurrentTimeInSeconds()) {
                    if(_robot->IsIdle()) {
                      CoreTechPrint("idle: waiting for dice\n");
                      _timesIdle = 0;
                      _idleState = IDLE_NONE;
                      _waitUntilTime = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + 5;
                    }
                  }
                  break;
                }

                default:
                CoreTechPrint("ERROR: invalid idle state %d\n", _idleState);
              }
            }
          } // IF robot is IDLE
          
          break;
        } // case WAITING_FOR_FIRST_DICE
          
        case BACKING_UP:
        {
          const f32 currentDistance = (_robot->GetPose().GetTranslation() -
                                       _goalPose.GetTranslation()).Length();
          
          if(currentDistance >= _desiredBackupDistance )
          {
            _waitUntilTime = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + 0.5f;
            _robot->DriveWheels(0.f, 0.f);
            _state = _nextState;
          }
          
          break;
        } // case BACKING_UP
        case GOTO_EXPLORATION_POSE:
        {
          const BlockWorld::ObjectsMapByID_t& blocks = _robot->GetBlockWorld().GetExistingObjectsByType(_objectTypeOfInterest);
          if (_robot->IsIdle() || !blocks.empty()) {
            _state = START_EXPLORING_TURN;
          }
          break;
        } // case GOTO_EXPLORATION_POSE
        case BEGIN_EXPLORING:
        {
          // For now, "exploration" is just spinning in place to
          // try to locate blocks
          if(!_robot->IsMoving() && _waitUntilTime < BaseStationTimer::getInstance()->GetCurrentTimeInSeconds()) {
            
            if(_robot->IsCarryingObject()) {
              _objectTypeOfInterest = _objectToPlaceOn;
            } else {
              _objectTypeOfInterest = _objectToPickUp;
            }
            
            
            // If we already know where the blockOfInterest is, then go straight to it
            const BlockWorld::ObjectsMapByID_t& blocks = _robot->GetBlockWorld().GetExistingObjectsByType(_objectTypeOfInterest);
            if(blocks.empty()) {
              // Compute desired pose at mat center
              Pose3d robotPose = _robot->GetPose();
              f32 targetAngle = _explorationStartAngle.ToFloat();
              if (_explorationStartAngle == 0) {
                // If this is the first time we're exploring, then start exploring at the pose
                // we expect to be in when we reach the mat center. Other start exploring at the angle
                // we last stopped exploring.
                targetAngle = atan2(robotPose.GetTranslation().y(), robotPose.GetTranslation().x()) + PI_F;
              }
              Pose3d targetPose(targetAngle, Z_AXIS_3D, Vec3f(0,0,0));
              
              if (ComputeDistanceBetween(targetPose, robotPose) > 50.f) {
                PRINT_INFO("Going to mat center for exploration (%f %f %f)\n", targetPose.GetTranslation().x(), targetPose.GetTranslation().y(), targetAngle);
                _robot->GetPathPlanner()->AddIgnoreType(Block::Type::DICE);
                _robot->GetActionList().QueueActionAtEnd(TraversalSlot, new DriveToPoseAction(targetPose));
              }

              _state = GOTO_EXPLORATION_POSE;
            } else {
              _state = EXPLORING;
            }
          }
          
          break;
        } // case BEGIN_EXPLORING
        case START_EXPLORING_TURN:
        {
          PRINT_INFO("Beginning exploring\n");
          _robot->GetPathPlanner()->RemoveIgnoreType(Block::Type::DICE);
          _robot->DriveWheels(8.f, -8.f);
          _robot->MoveHeadToAngle(DEG_TO_RAD(-10), 1, 1);
          _explorationStartAngle = _robot->GetPose().GetRotationAngle<'Z'>();
          _isTurning = true;
          _state = EXPLORING;
          break;
        }
        case EXPLORING:
        {
          // If we've spotted the block we're looking for, stop exploring, and
          // execute a path to that block
          const BlockWorld::ObjectsMapByID_t& blocks = _robot->GetBlockWorld().GetExistingObjectsByType(_objectTypeOfInterest);
          if(!blocks.empty()) {
            // Dock with the first block of the right type that we see
            // TODO: choose the closest?
            Block* dockBlock = dynamic_cast<Block*>(blocks.begin()->second);
            CORETECH_THROW_IF(dockBlock == nullptr);
            
            _robot->DriveWheels(0.f, 0.f);
            
            _robot->GetActionList().QueueActionAtEnd(TraversalSlot, new DriveToPickAndPlaceObjectAction(dockBlock->GetID()));
            
            _state = EXECUTING_DOCK;
            
            _wasCarryingBlockAtDockingStart = _robot->IsCarryingObject();

            SoundManager::getInstance()->Play(SOUND_OK_GOT_IT);
            
            PRINT_INFO("STARTING DOCKING\n");
            break;
          }
          
          // Repeat turn-stop behavior for more reliable block detection
          Radians currAngle = _robot->GetPose().GetRotationAngle<'Z'>();
          if (_isTurning && (std::abs((_explorationStartAngle - currAngle).ToFloat()) > DEG_TO_RAD(40))) {
            PRINT_INFO("Exploration - pause turning. Looking for %s\n", _objectTypeOfInterest.GetName().c_str());
            _robot->DriveWheels(0.f,0.f);
            _isTurning = false;
            _waitUntilTime = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + 0.5f;
          } else if (!_isTurning && _waitUntilTime < BaseStationTimer::getInstance()->GetCurrentTimeInSeconds()) {
            _state = START_EXPLORING_TURN;
          }
          
          break;
        } // EXPLORING
   
        case EXECUTING_DOCK:
        {
          // Wait for the robot to go back to IDLE
          if(_robot->IsIdle())
          {
            const bool donePickingUp = _robot->IsCarryingObject() &&
                                       _robot->GetBlockWorld().GetObjectByID(_robot->GetCarryingObject())->GetType() == _objectToPickUp;
            if(donePickingUp) {
              PRINT_INFO("Picked up block %d successfully! Going back to exploring for block to place on.\n",
                         _robot->GetCarryingObject().GetValue());
              
              _state = BEGIN_EXPLORING;
              
              SoundManager::getInstance()->Play(SOUND_NOTIMPRESSED);
              
              return;
            } // if donePickingUp
            
            const bool donePlacing = !_robot->IsCarryingObject() && _wasCarryingBlockAtDockingStart;
            if(donePlacing) {
              PRINT_INFO("Placed block %d on %d successfully! Going back to waiting for dice.\n",
                         _objectToPickUp.GetValue(), _objectToPlaceOn.GetValue());

              _robot->MoveHeadToAngle(checkItOutAngleUp, checkItOutSpeed, 10);
              _state = CHECK_IT_OUT_UP;
              _waitUntilTime = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + 2.f;

              // TODO:(bn) sound: minor success??
              
              return;
            } // if donePlacing
            
            
            // Either pickup or placement failed
            const bool pickupFailed = !_robot->IsCarryingObject();
            if (pickupFailed) {
              PRINT_INFO("Block pickup failed. Retrying...\n");
            } else {
              PRINT_INFO("Block placement failed. Retrying...\n");
            }
            
            // Backup to re-explore the block
            _robot->MoveHeadToAngle(DEG_TO_RAD(-5), 10, 10);
            _robot->DriveWheels(-20.f, -20.f);
            _state = BACKING_UP;
            _nextState = BEGIN_EXPLORING;
            _desiredBackupDistance = 30;
            _goalPose = _robot->GetPose();
            
            SoundManager::getInstance()->Play(SOUND_STARTOVER);
            
          } // if robot IDLE
          
          break;
        } // case EXECUTING_DOCK

        case CHECK_IT_OUT_UP:
        {
          // Wait for the robot to go back to IDLE
          if(_robot->IsIdle() &&
             _waitUntilTime < BaseStationTimer::getInstance()->GetCurrentTimeInSeconds())
          {
            // TODO:(bn) small happy chirp sound
            _robot->MoveHeadToAngle(checkItOutAngleDown, checkItOutSpeed, 10);
            _state = CHECK_IT_OUT_DOWN;
            _waitUntilTime = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + 2.f;
          }
          break;
        }

        case CHECK_IT_OUT_DOWN:
        {
          // Wait for the robot to go back to IDLE
          if(_robot->IsIdle() &&
             _waitUntilTime < BaseStationTimer::getInstance()->GetCurrentTimeInSeconds())
          {
            // Compute pose that makes robot face user
            Pose3d userFacingPose = _robot->GetPose();
            userFacingPose.SetRotation(USER_LOC_ANGLE_WRT_MAT, Z_AXIS_3D);
            _robot->GetActionList().QueueActionAtEnd(TraversalSlot, new DriveToPoseAction(userFacingPose));

            SoundManager::getInstance()->Play(SOUND_OK_GOT_IT);
            _state = FACE_USER;
          }
          break;
        }

        case FACE_USER:
        {
          // Wait for the robot to go back to IDLE
          if(_robot->IsIdle())
          {
            // Start nodding
            _robot->PlayAnimation("ANIM_HEAD_NOD");
            _state = HAPPY_NODDING;
            PRINT_INFO("NODDING_HEAD\n");
            SoundManager::getInstance()->Play(SOUND_OK_DONE);
            
            // Compute time to stop nodding
            _waitUntilTime = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + 2;
          }
          break;
        } // case FACE_USER
        case HAPPY_NODDING:
        {
          if (BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() > _waitUntilTime) {
            _robot->PlayAnimation("ANIM_BACK_AND_FORTH_EXCITED");
            _robot->MoveHeadToAngle(DEG_TO_RAD(-10), 1, 1);
            
            // Compute time to stop back and forth
            _waitUntilTime = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + 1.5;
            _state = BACK_AND_FORTH_EXCITED;
          }
          break;
        } // case HAPPY_NODDING
        case BACK_AND_FORTH_EXCITED:
        {
          if (BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() > _waitUntilTime) {
            _robot->PlayAnimation("ANIM_IDLE");
            _robot->GetBlockWorld().ClearAllExistingObjects();
            StartMode(June2014DiceDemo);
          }
          break;
        }
        default:
        {
          PRINT_NAMED_ERROR("BehaviorManager.UnknownBehaviorState",
                            "Transitioned to unknown state %d!\n", _state);
          StartMode(None);
          return;
        }
      } // switch(_state)
      
    } // Update_June2014DiceDemo()

    BehaviorManager::TransitionEventManager::TransitionEventManager()
    : _isTransitioning(false)
    {
      
    }
    
    void BehaviorManager::TransitionEventManager::AddTransition(BehaviorState fromState,
                                                                BehaviorState toState,
                                                                std::function<void ()> eventFcn,
                                                                double duration)
    {
      _transitionEventLUT[fromState][toState] = {eventFcn, duration};
    }
    
    void BehaviorManager::TransitionEventManager::Transition(BehaviorState fromState, BehaviorState toState)
    {
      auto fromIter = _transitionEventLUT.find(fromState);
      if(fromIter != _transitionEventLUT.end()) {
        auto toIter = fromIter->second.find(toState);
        if(toIter != fromIter->second.end()) {
          
          // Call the event function
          toIter->second.first();
          
          // Set the waitUntilTime
          _waitUntilTime = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + toIter->second.second;
          
          _isTransitioning = true;
        } else if(toState != NUM_STATES) {
          // No transition to specific "toState", check to see if there is one
          // defined to "ANY" state for this fromState:
          Transition(fromState, NUM_STATES);
        }
      } else if(fromState != NUM_STATES) {
        // No transition found from specific "fromState", check to see if there
        // is one defined from "ANY" state for this toState:
        Transition(NUM_STATES, toState);
      }
    } // Transition()
    
    
    bool BehaviorManager::TransitionEventManager::IsTransitioning()
    {
      if(_isTransitioning) {
        if (BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() > _waitUntilTime) {
          // Enough time has elapsed, no longer transitioning
          _isTransitioning = false;
        }
      }
      return _isTransitioning;
    }
    
    void BehaviorManager::Update_CREEP()
    {
      // Wait for transition animation to complete if there is one
      if(_transitionManager.IsTransitioning()) {
        return;
      }
      
      if(!_stateAnimStarted) {
        auto animIter = _stateAnimations.find(_state);
        if(animIter != _stateAnimations.end()) {
          _robot->PlayAnimation(animIter->second.c_str(), 0);
        }
        _stateAnimStarted = true;
      }
      
      if(_state != _nextState) {
        _robot->StopAnimation();
        _transitionManager.Transition(_state, _nextState);
        _state = _nextState;
        _stateAnimStarted = false;
        VizManager::getInstance()->SetText(VizManager::BEHAVIOR_STATE, NamedColors::YELLOW, GetBehaviorStateName(_state).c_str());
      }
      
    } // Update_CREEP()
    
  } // namespace Cozmo
} // namespace Anki

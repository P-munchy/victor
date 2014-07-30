
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
        
    BehaviorManager::BehaviorManager()
    : robotMgr_(nullptr)
    , world_(nullptr)
    , mode_(BM_None)
    , distThresh_mm_(20.f)
    , angThresh_(DEG_TO_RAD(10))
//    , objectToPickUp_(Block::UNKNOWN_BLOCK_TYPE)
//    , objectToPlaceOn_(Block::UNKNOWN_BLOCK_TYPE)
    {
      Reset();
    }

    void BehaviorManager::Init(RobotManager* robotMgr, BlockWorld* world)
    {
      robotMgr_ = robotMgr;
      world_ = world;
    }
    
    void BehaviorManager::StartMode(BehaviorMode mode)
    {
      Reset();
      mode_ = mode;
      switch(mode) {
        case BM_None:
          CoreTechPrint("Starting NONE behavior\n");
          break;
        case BM_PickAndPlace:
          CoreTechPrint("Starting PickAndPlace behavior\n");
          nextState_ = WAITING_FOR_DOCK_BLOCK;
          updateFcn_ = &BehaviorManager::Update_PickAndPlaceBlock;
          break;
        case BM_June2014DiceDemo:
          CoreTechPrint("Starting June demo behavior\n");
          state_     = WAITING_FOR_ROBOT;
          nextState_ = DRIVE_TO_START;
          updateFcn_ = &BehaviorManager::Update_June2014DiceDemo;
          idleState_ = IDLE_NONE;
          timesIdle_ = 0;
          SoundManager::getInstance()->Play(SOUND_DEMO_START);
          break;
        case BM_TraverseRamp:
          CoreTechPrint("Starting TraverseRamp behavior\n");
          nextState_ = WAITING_FOR_DOCK_BLOCK;
          updateFcn_ = &BehaviorManager::Update_TraverseRamp;
          break;
        default:
          PRINT_NAMED_ERROR("BehaviorManager.InvalidMode", "Invalid behavior mode");
          return;
      }
      
      //assert(updateFcn_ != nullptr);
      
    } // StartMode()
    
    BehaviorMode BehaviorManager::GetMode() const
    {
      return mode_;
    }
    
    void BehaviorManager::Reset()
    {
      state_ = WAITING_FOR_ROBOT;
      nextState_ = state_;
      updateFcn_ = nullptr;
      robot_ = nullptr;
      
      // Pick and Place
      
      // June2014DiceDemo
      explorationStartAngle_ = 0;
      objectToPickUp_.UnSet();
      objectToPlaceOn_.UnSet();
      
    } // Reset()
    
    
    // TODO: Make this a blockWorld function?
    void BehaviorManager::SelectNextBlockOfInterest()
    {
      bool currBlockOfInterestFound = false;
      bool newBlockOfInterestSet = false;
      
      // Iterate through all the non-Mat objects
      auto const & allObjects = world_->GetAllExistingObjects();
      for(auto const & objectsByFamily : allObjects) {
        if(objectsByFamily.first != BlockWorld::ObjectFamily::MATS)
        {
          for (auto const & objectsByType : objectsByFamily.second) {
            
            //PRINT_INFO("currType: %d\n", blockType.first);
            for (auto const & objectsByID : objectsByType.second) {
              
              const DockableObject* object = dynamic_cast<DockableObject*>(objectsByID.second);
              if(object != nullptr && !object->IsBeingCarried())
              {
                //PRINT_INFO("currID: %d\n", block.first);
                if (currBlockOfInterestFound) {
                  // Current block of interest has been found.
                  // Set the new block of interest to the next block in the list.
                  objectIDofInterest_ = object->GetID();
                  newBlockOfInterestSet = true;
                  //PRINT_INFO("new block found: id %d  type %d\n", block.first, blockType.first);
                  break;
                } else if (object->GetID() == objectIDofInterest_) {
                  currBlockOfInterestFound = true;
                  //PRINT_INFO("curr block found: id %d  type %d\n", block.first, blockType.first);
                }
              }
            } // for each ID
            
            if (newBlockOfInterestSet) {
              break;
            }
            
          } // for each type
        } // if non-MAT
        
        if(newBlockOfInterestSet) {
          break;
        }
        
      } // for each family
      
      // If the current block of interest was found, but a new one was not set
      // it must have been the last block in the map. Set the new block of interest
      // to the first block in the map as long as it's not the same block.
      if (!currBlockOfInterestFound || !newBlockOfInterestSet) {
        
        // Find first block
        ObjectID firstBlock; // initialized to un-set
        for(auto const & objectsByFamily : allObjects) {
          if(objectsByFamily.first != BlockWorld::ObjectFamily::MATS) {
            for (auto const & objectsByType : objectsByFamily.second) {
              for (auto const & objectsByID : objectsByType.second) {
                const DockableObject* object = dynamic_cast<DockableObject*>(objectsByID.second);
                if(object != nullptr && !object->IsBeingCarried())
                {
                  firstBlock = objectsByID.first;
                  break;
                }
              }
              if (firstBlock.IsSet()) {
                break;
              }
            }
          } // if not MAT
          
          if (firstBlock.IsSet()) {
            break;
          }
        } // for each family

        
        if (firstBlock == objectIDofInterest_ || !firstBlock.IsSet()){
          //PRINT_INFO("Only one block in existence.");
        } else {
          //PRINT_INFO("Setting block of interest to first block\n");
          objectIDofInterest_ = firstBlock;
        }
      }
      
      PRINT_INFO("Block of interest: ID = %d\n", objectIDofInterest_.GetValue());
      
      /*
      // Draw BOI
      const Block* block = dynamic_cast<Block*>(world_->GetObservableObjectByID(objectIDofInterest_));
      if(block == nullptr) {
        PRINT_INFO("Failed to find/draw block of interest!\n");
      } else {

        static ObjectID prev_boi = 0;      // Previous block of interest
        static size_t prevNumPreDockPoses = 0;  // Previous number of predock poses

        // Get predock poses
        std::vector<Block::PoseMarkerPair_t> poses;
        block->GetPreDockPoses(PREDOCK_DISTANCE_MM, poses);
        
        // Erase previous predock pose marker for previous block of interest
        if (prev_boi != objectIDofInterest_ || poses.size() != prevNumPreDockPoses) {
          PRINT_INFO("BOI %d (prev %d), numPoses %d (prev %zu)\n", objectIDofInterest_, prev_boi, (u32)poses.size(), prevNumPreDockPoses);
          VizManager::getInstance()->EraseVizObjectType(VIZ_PREDOCKPOSE);
          prev_boi = objectIDofInterest_;
          prevNumPreDockPoses = poses.size();
        }
        
        // Draw predock poses
        u32 poseID = 0;
        for(auto pose : poses) {
          VizManager::getInstance()->DrawPreDockPose(6*block->GetID()+poseID++, pose.first, VIZ_COLOR_PREDOCKPOSE);
          ++poseID;
        }
      
        // Draw cuboid
        VizManager::getInstance()->DrawCuboid(block->GetID(),
                                              block->GetSize(),
                                              block->GetPose().GetWithRespectTo(Pose3d::World),
                                              VIZ_COLOR_SELECTED_OBJECT);
      }
       */
    } // SelectNextObjectOfInterest()
    
    void BehaviorManager::Update()
    {
      // Shared states
      switch(state_) {
        case WAITING_FOR_ROBOT:
        {
          const std::vector<RobotID_t> &robotList = robotMgr_->GetRobotIDList();
          if (!robotList.empty()) {
            robot_ = robotMgr_->GetRobotByID(robotList[0]);
            state_ = nextState_;
          }
          break;
        }
        default:
          if (updateFcn_) {
            (this->*updateFcn_)();
          } else {
            state_ = nextState_ = WAITING_FOR_ROBOT;
          }
          break;
      }

      
    } // Update()
    
    
    /********************************************************
     * PickAndPlaceBlock
     *
     * Looks for a particular block in the world. When it sees
     * that it is at ground-level it
     * 1) Plans a path to a docking pose for that block
     * 2) Docks with the block
     * 3) Places it on any other block in the world
     *
     ********************************************************/
    
    void BehaviorManager::Update_PickAndPlaceBlock()
    {
      // Params for determining whether the predock pose has been reached
      
      robot_->ExecuteDockingSequence(objectIDofInterest_);
      StartMode(BM_None);
      
      /*
      switch(state_) {
        case WAITING_FOR_DOCK_BLOCK:
        {
          // TODO: BlockWorld needs function for retrieving collision-free docking poses for a given block
          //
          // 1) Get all blocks in the world
          // 2) Find one that is on the bottom level and no block is on top
          // 3) Get collision-free docking poses
          // 4) Find a collision-free path to one of the docking poses
          // 5) Command path to that docking pose
          
          
          // Get block object
          DockableObject* object = dynamic_cast<DockableObject*>(world_->GetObjectByID(objectIDofInterest_));
          if (object == nullptr) {
            break;
          }
          
          // Check that we're not already carrying a block if the block of interest is a high block.
          if (object->GetPose().GetTranslation().z() > 44.f && robot_->IsCarryingObject()) {
            PRINT_INFO("Already carrying object. Can't dock to high object. Aborting (0).\n");
            StartMode(BM_None);
            return;
          }

          if(robot_->ExecuteDockingSequence(object->GetID()) != RESULT_OK) {
            PRINT_INFO("Robot::ExecuteDockingSequence() failed. Aborting.\n");
            StartMode(BM_None);
            return;
          }
          
          state_ = EXECUTING_DOCK;
          
          break;
        }
        case EXECUTING_DOCK:
        {
          // Wait until robot finishes (goes back to IDLE)
          if(robot_->GetState() == Robot::IDLE) {
            StartMode(BM_None);
          }
          break;
        }
        default:
        {
          PRINT_NAMED_ERROR("BehaviorManager.UnknownBehaviorState", "Transitioned to unknown state %d!", state_);
          StartMode(BM_None);
          return;
        }
      }
       */
      
    } // Update_PickAndPlaceBlock()
    
    void BehaviorManager::Update_TraverseRamp()
    {
      robot_->ExecuteRampingSequence(objectIDofInterest_);
      StartMode(BM_None);
    }
    
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

      constexpr float checkItOutAngleUp = DEG_TO_RAD(15);
      constexpr float checkItOutAngleDown = DEG_TO_RAD(-10);
      constexpr float checkItOutSpeed = 0.4;

      switch(state_) {

        case DRIVE_TO_START:
        {
          // Wait for robot to be IDLE
          if(robot_->GetState() == Robot::IDLE) {
            Pose3d startPose(JUNE_DEMO_START_THETA,
                             Z_AXIS_3D,
                             {{JUNE_DEMO_START_X, JUNE_DEMO_START_Y, 0.f}});
            CoreTechPrint("Driving to demo start location\n");
            robot_->ExecutePathToPose(startPose);

            state_ = WAITING_TO_SEE_DICE;

            robot_->SetDefaultLights(0x008080, 0x008080);
          }

          break;
        }
          
        case WAITING_FOR_DICE_TO_DISAPPEAR:
        {
          const BlockWorld::ObjectsMapByID_t& diceBlocks = world_->GetExistingObjectsByType(Block::Type::DICE);
          
          if(diceBlocks.empty()) {
            
            // Check to see if the dice block has been gone for long enough
            const TimeStamp_t timeSinceSeenDice_ms = BaseStationTimer::getInstance()->GetCurrentTimeStamp() - diceDeletionTime_;
            if(timeSinceSeenDice_ms > TimeBetweenDice_ms) {
              CoreTechPrint("First dice is gone: ready for next dice!\n");
              state_ = WAITING_TO_SEE_DICE;
            }
          } else {
            world_->ClearObjectsByType(Block::Type::DICE);
            diceDeletionTime_ = BaseStationTimer::getInstance()->GetCurrentTimeStamp();
            if (waitUntilTime_ < BaseStationTimer::getInstance()->GetCurrentTimeInSeconds()) {
              // Keep clearing blocks until we don't see them anymore
              CoreTechPrint("Please move first dice away.\n");
              robot_->SendPlayAnimation(ANIM_HEAD_NOD, 2);
              waitUntilTime_ = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + 5;
              SoundManager::getInstance()->Play(SOUND_WAITING4DICE2DISAPPEAR);
            }
          }
          break;
        }
          
        case WAITING_TO_SEE_DICE:
        {
          /*
          // DEBUG!!!
          objectToPickUp_ = Block::NUMBER5_BLOCK_TYPE;
          objectToPlaceOn_ = Block::NUMBER6_BLOCK_TYPE;
          state_ = BEGIN_EXPLORING;
          break;
          */
          
          const f32 diceViewingHeadAngle = DEG_TO_RAD(-15);

          // Wait for robot to be IDLE
          if(robot_->GetState() == Robot::IDLE)
          {
            const BlockWorld::ObjectsMapByID_t& diceBlocks = world_->GetExistingObjectsByType(Block::Type::DICE);
            if(!diceBlocks.empty()) {
              
              if(diceBlocks.size() > 1) {
                // Multiple dice blocks in the world, keep deleting them all
                // until we only see one
                CoreTechPrint("More than one dice block found!\n");
                world_->ClearObjectsByType(Block::Type::DICE);
                
              } else {
                
                Block* diceBlock = dynamic_cast<Block*>(diceBlocks.begin()->second);
                CORETECH_ASSERT(diceBlock != nullptr);
                
                // Get all the observed markers on the dice and look for the one
                // facing up (i.e. the one that is nearly aligned with the z axis)
                // TODO: expose the threshold here?
                const TimeStamp_t timeWindow = robot_->GetLastMsgTimestamp() - 500;
                const f32 dotprodThresh = 1.f - cos(DEG_TO_RAD(20));
                std::vector<const Vision::KnownMarker*> diceMarkers;
                diceBlock->GetObservedMarkers(diceMarkers, timeWindow);
                
                const Vision::KnownMarker* topMarker = nullptr;
                for(auto marker : diceMarkers) {
                  //const f32 dotprod = DotProduct(marker->ComputeNormal(), Z_AXIS_3D);
                  Pose3d markerWrtRobotOrigin;
                  if(marker->GetPose().GetWithRespectTo(robot_->GetPose().FindOrigin(), markerWrtRobotOrigin) == false) {
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
                  diceBlock->GetObservedMarkers(diceMarkers, robot_->GetLastMsgTimestamp() - 2000);
                  if (diceMarkers.empty()) {
                    CoreTechPrint("Haven't see dice marker for a while. Deleting dice.");
                    world_->ClearObjectsByType(Block::Type::DICE);
                    break;
                  }
                }
                
                if(topMarker != nullptr) {
                  // We found and observed the top marker on the dice. Use it to
                  // set which block we are looking for.
                  
                  // Don't forget to remove the dice as an ignore type for
                  // planning, since we _do_ want to avoid it as an obstacle
                  // when driving to pick and place blocks
                  robot_->GetPathPlanner()->RemoveIgnoreType(Block::Type::DICE);
                  
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
                      StartMode(BM_None);
                      return;
                  } // switch(topMarker->GetCode())
                  
                  CoreTechPrint("Found top marker on dice: %s!\n",
                                Vision::MarkerTypeStrings[topMarker->GetCode()]);
                  
                  if(objectToPickUp_.IsUnknown()) {
                    
                    objectToPickUp_ = blockToLookFor;
                    objectToPlaceOn_.SetToUnknown();
                    
                    CoreTechPrint("Set blockToPickUp = %s\n", objectToPickUp_.GetName().c_str());
                    
                    // Wait for first dice to disappear
                    state_ = WAITING_FOR_DICE_TO_DISAPPEAR;

                    SoundManager::getInstance()->Play(SOUND_OK_GOT_IT);
                    
                    waitUntilTime_ = 0;
                  } else {

                    if(blockToLookFor == objectToPickUp_) {
                      CoreTechPrint("Can't put a object on itself!\n");
                      // TODO:(bn) left and right + sad noise?
                    }
                    else {

                      objectToPlaceOn_ = blockToLookFor;
                    
                      CoreTechPrint("Set objectToPlaceOn = %s\n", objectToPlaceOn_.GetName().c_str());

                      robot_->SendPlayAnimation(ANIM_HEAD_NOD, 2);
                      waitUntilTime_ = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + 2.5;

                      state_ = BEGIN_EXPLORING;

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
                  robot_->GetPathPlanner()->AddIgnoreType(Block::Type::DICE);
                  
                  Vec3f position( robot_->GetPose().GetTranslation() );
                  position -= diceBlock->GetPose().GetTranslation();
                  f32 actualDistToDice = position.Length();
                  f32 desiredDistToDice = ROBOT_BOUNDING_X_FRONT + 0.5f*diceBlock->GetSize().Length() + 5.f;

                  if (actualDistToDice > desiredDistToDice + 5) {
                    position.MakeUnitLength();
                    position *= desiredDistToDice;
                  
                    Radians angle = atan2(position.y(), position.x()) + PI_F;
                    position += diceBlock->GetPose().GetTranslation();
                    
                    goalPose_ = Pose3d(angle, Z_AXIS_3D, {{position.x(), position.y(), 0.f}});
                    
                    robot_->ExecutePathToPose(goalPose_, diceViewingHeadAngle);
                  } else {
                    CoreTechPrint("Move dice closer!\n");
                  }
                  
                } // IF / ELSE top marker seen
                
              } // IF only one dice
              
              timesIdle_ = 0;

            } // IF any diceBlocks available
            
            else {

              constexpr int numIdleForFrustrated = 3;
              constexpr float headUpWaitingAngle = DEG_TO_RAD(20);
              constexpr float headUpWaitingAngleFrustrated = DEG_TO_RAD(25);
              // Can't see dice
              switch(idleState_) {
                case IDLE_NONE:
                {
                  // if its been long enough, look up
                  if (waitUntilTime_ < BaseStationTimer::getInstance()->GetCurrentTimeInSeconds()) {
                    if(++timesIdle_ >= numIdleForFrustrated) {
                      SoundManager::getInstance()->Play(SOUND_WAITING4DICE);

                      originalPose_ = robot_->GetPose();

                      Pose3d userFacingPose = robot_->GetPose();
                      userFacingPose.SetRotation(USER_LOC_ANGLE_WRT_MAT, Z_AXIS_3D);
                      robot_->ExecutePathToPose(userFacingPose);
                      CoreTechPrint("idle: facing user\n");

                      idleState_ = IDLE_FACING_USER;
                    }
                    else {
                      CoreTechPrint("idle: looking up\n");
                      robot_->MoveHeadToAngle(headUpWaitingAngle, 3.0, 10);
                      idleState_ = IDLE_LOOKING_UP;
                      waitUntilTime_ = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + 0.7;
                    }
                  }
                  break;
                }

                case IDLE_LOOKING_UP:
                {
                  // once we get to the top, play the sound

                  if (waitUntilTime_ < BaseStationTimer::getInstance()->GetCurrentTimeInSeconds()) {
                    CoreTechPrint("idle: playing sound\n");
                    SoundManager::getInstance()->Play(SOUND_WAITING4DICE);
                    idleState_ = IDLE_PLAYING_SOUND;
                    if(timesIdle_ >= numIdleForFrustrated) {
                      waitUntilTime_ = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + 2.0;
                      SoundManager::getInstance()->Play(SOUND_WAITING4DICE);
                      SoundManager::getInstance()->Play(SOUND_WAITING4DICE);
                    }
                    else {
                      waitUntilTime_ = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + 0.5;
                    }
                  }
                  break;
                }

                case IDLE_PLAYING_SOUND:
                {
                  // once the sound is done, look back down
                  if (waitUntilTime_ < BaseStationTimer::getInstance()->GetCurrentTimeInSeconds()) {
                    CoreTechPrint("idle: looking back down\n");
                    robot_->MoveHeadToAngle(diceViewingHeadAngle, 1.5, 10);
                    if(timesIdle_ >= numIdleForFrustrated) {
                      SoundManager::getInstance()->Play(SOUND_WAITING4DICE);
                      waitUntilTime_ = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + 2;
                      idleState_ = IDLE_LOOKING_DOWN;
                    }
                    else {
                      idleState_ = IDLE_NONE;
                      waitUntilTime_ = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + 5;
                    }
                  }
                  break;
                }

                case IDLE_FACING_USER:
                {
                  // once we get there, look up
                  if(robot_->GetState() == Robot::IDLE) {
                    SoundManager::getInstance()->Play(SOUND_WAITING4DICE);
                    CoreTechPrint("idle: looking up\n");
                    robot_->MoveHeadToAngle(headUpWaitingAngleFrustrated, 3.0, 10);
                    idleState_ = IDLE_LOOKING_UP;
                    waitUntilTime_ = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + 2;
                  }
                  break;
                }

                case IDLE_LOOKING_DOWN:
                {
                  // once we are looking back down, turn back to the original pose
                  if(waitUntilTime_ < BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() &&
                     robot_->GetState() == Robot::IDLE) {

                    CoreTechPrint("idle: turning back\n");
                    SoundManager::getInstance()->Play(SOUND_WAITING4DICE);
                    robot_->ExecutePathToPose(originalPose_);
                    idleState_ = IDLE_TURNING_BACK;
                    waitUntilTime_ = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + 0.25;
                  }
                  break;
                }

                case IDLE_TURNING_BACK:
                {
                  if (waitUntilTime_ < BaseStationTimer::getInstance()->GetCurrentTimeInSeconds()) {
                    if(robot_->GetState() == Robot::IDLE) {
                      CoreTechPrint("idle: waiting for dice\n");
                      timesIdle_ = 0;
                      idleState_ = IDLE_NONE;
                      waitUntilTime_ = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + 5;
                    }
                  }
                  break;
                }

                default:
                CoreTechPrint("ERROR: invalid idle state %d\n", idleState_);
              }
            }
          } // IF robot is IDLE
          
          break;
        } // case WAITING_FOR_FIRST_DICE
          
        case BACKING_UP:
        {
          const f32 currentDistance = (robot_->GetPose().GetTranslation() -
                                       goalPose_.GetTranslation()).Length();
          
          if(currentDistance >= desiredBackupDistance_ )
          {
            waitUntilTime_ = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + 0.5f;
            robot_->DriveWheels(0.f, 0.f);
            state_ = nextState_;
          }
          
          break;
        } // case BACKING_UP
        case GOTO_EXPLORATION_POSE:
        {
          const BlockWorld::ObjectsMapByID_t& blocks = world_->GetExistingObjectsByType(objectTypeOfInterest_);
          if (robot_->GetState() == Robot::IDLE || !blocks.empty()) {
            state_ = START_EXPLORING_TURN;
          }
          break;
        } // case GOTO_EXPLORATION_POSE
        case BEGIN_EXPLORING:
        {
          // For now, "exploration" is just spinning in place to
          // try to locate blocks
          if(!robot_->IsMoving() && waitUntilTime_ < BaseStationTimer::getInstance()->GetCurrentTimeInSeconds()) {
            
            if(robot_->IsCarryingObject()) {
              objectTypeOfInterest_ = objectToPlaceOn_;
            } else {
              objectTypeOfInterest_ = objectToPickUp_;
            }
            
            
            // If we already know where the blockOfInterest is, then go straight to it
            const BlockWorld::ObjectsMapByID_t& blocks = world_->GetExistingObjectsByType(objectTypeOfInterest_);
            if(blocks.empty()) {
              // Compute desired pose at mat center
              Pose3d robotPose = robot_->GetPose();
              f32 targetAngle = explorationStartAngle_.ToFloat();
              if (explorationStartAngle_ == 0) {
                // If this is the first time we're exploring, then start exploring at the pose
                // we expect to be in when we reach the mat center. Other start exploring at the angle
                // we last stopped exploring.
                targetAngle = atan2(robotPose.GetTranslation().y(), robotPose.GetTranslation().x()) + PI_F;
              }
              Pose3d targetPose(targetAngle, Z_AXIS_3D, Vec3f(0,0,0));
              
              if (ComputeDistanceBetween(targetPose, robotPose) > 50.f) {
                PRINT_INFO("Going to mat center for exploration (%f %f %f)\n", targetPose.GetTranslation().x(), targetPose.GetTranslation().y(), targetAngle);
                robot_->GetPathPlanner()->AddIgnoreType(Block::Type::DICE);
                robot_->ExecutePathToPose(targetPose);
              }

              state_ = GOTO_EXPLORATION_POSE;
            } else {
              state_ = EXPLORING;
            }
          }
          
          break;
        } // case BEGIN_EXPLORING
        case START_EXPLORING_TURN:
        {
          PRINT_INFO("Beginning exploring\n");
          robot_->GetPathPlanner()->RemoveIgnoreType(Block::Type::DICE);
          robot_->DriveWheels(8.f, -8.f);
          robot_->MoveHeadToAngle(DEG_TO_RAD(-10), 1, 1);
          explorationStartAngle_ = robot_->GetPose().GetRotationAngle<'Z'>();
          isTurning_ = true;
          state_ = EXPLORING;
          break;
        }
        case EXPLORING:
        {
          // If we've spotted the block we're looking for, stop exploring, and
          // execute a path to that block
          const BlockWorld::ObjectsMapByID_t& blocks = world_->GetExistingObjectsByType(objectTypeOfInterest_);
          if(!blocks.empty()) {
            // Dock with the first block of the right type that we see
            // TODO: choose the closest?
            Block* dockBlock = dynamic_cast<Block*>(blocks.begin()->second);
            CORETECH_THROW_IF(dockBlock == nullptr);
            
            robot_->DriveWheels(0.f, 0.f);
            
            robot_->ExecuteDockingSequence(dockBlock->GetID());
            
            state_ = EXECUTING_DOCK;
            
            wasCarryingBlockAtDockingStart_ = robot_->IsCarryingObject();

            SoundManager::getInstance()->Play(SOUND_OK_GOT_IT);
            
            PRINT_INFO("STARTING DOCKING\n");
            break;
          }
          
          // Repeat turn-stop behavior for more reliable block detection
          Radians currAngle = robot_->GetPose().GetRotationAngle<'Z'>();
          if (isTurning_ && (std::abs((explorationStartAngle_ - currAngle).ToFloat()) > DEG_TO_RAD(40))) {
            PRINT_INFO("Exploration - pause turning. Looking for %s\n", objectTypeOfInterest_.GetName().c_str());
            robot_->DriveWheels(0.f,0.f);
            isTurning_ = false;
            waitUntilTime_ = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + 0.5f;
          } else if (!isTurning_ && waitUntilTime_ < BaseStationTimer::getInstance()->GetCurrentTimeInSeconds()) {
            state_ = START_EXPLORING_TURN;
          }
          
          break;
        } // EXPLORING
   
        case EXECUTING_DOCK:
        {
          // Wait for the robot to go back to IDLE
          if(robot_->GetState() == Robot::IDLE)
          {
            const bool donePickingUp = robot_->IsCarryingObject() &&
                                       world_->GetObjectByID(robot_->GetCarryingObject())->GetType() == objectToPickUp_;
            if(donePickingUp) {
              PRINT_INFO("Picked up block %d successfully! Going back to exploring for block to place on.\n",
                         robot_->GetCarryingObject().GetValue());
              
              state_ = BEGIN_EXPLORING;
              
              SoundManager::getInstance()->Play(SOUND_NOTIMPRESSED);
              
              return;
            } // if donePickingUp
            
            const bool donePlacing = !robot_->IsCarryingObject() && wasCarryingBlockAtDockingStart_;
            if(donePlacing) {
              PRINT_INFO("Placed block %d on %d successfully! Going back to waiting for dice.\n",
                         objectToPickUp_.GetValue(), objectToPlaceOn_.GetValue());

              robot_->MoveHeadToAngle(checkItOutAngleUp, checkItOutSpeed, 10);
              state_ = CHECK_IT_OUT_UP;
              waitUntilTime_ = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + 2.f;

              // TODO:(bn) sound: minor success??
              
              return;
            } // if donePlacing
            
            
            // Either pickup or placement failed
            const bool pickupFailed = !robot_->IsCarryingObject();
            if (pickupFailed) {
              PRINT_INFO("Block pickup failed. Retrying...\n");
            } else {
              PRINT_INFO("Block placement failed. Retrying...\n");
            }
            
            // Backup to re-explore the block
            robot_->MoveHeadToAngle(DEG_TO_RAD(-5), 10, 10);
            robot_->DriveWheels(-20.f, -20.f);
            state_ = BACKING_UP;
            nextState_ = BEGIN_EXPLORING;
            desiredBackupDistance_ = 30;
            goalPose_ = robot_->GetPose();
            
            SoundManager::getInstance()->Play(SOUND_STARTOVER);
            
          } // if robot IDLE
          
          break;
        } // case EXECUTING_DOCK

        case CHECK_IT_OUT_UP:
        {
          // Wait for the robot to go back to IDLE
          if(robot_->GetState() == Robot::IDLE &&
             waitUntilTime_ < BaseStationTimer::getInstance()->GetCurrentTimeInSeconds())
          {
            // TODO:(bn) small happy chirp sound
            robot_->MoveHeadToAngle(checkItOutAngleDown, checkItOutSpeed, 10);
            state_ = CHECK_IT_OUT_DOWN;
            waitUntilTime_ = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + 2.f;
          }
          break;
        }

        case CHECK_IT_OUT_DOWN:
        {
          // Wait for the robot to go back to IDLE
          if(robot_->GetState() == Robot::IDLE &&
             waitUntilTime_ < BaseStationTimer::getInstance()->GetCurrentTimeInSeconds())
          {
            // Compute pose that makes robot face user
            Pose3d userFacingPose = robot_->GetPose();
            userFacingPose.SetRotation(USER_LOC_ANGLE_WRT_MAT, Z_AXIS_3D);
            robot_->ExecutePathToPose(userFacingPose);

            SoundManager::getInstance()->Play(SOUND_OK_GOT_IT);
            state_ = FACE_USER;
          }
          break;
        }

        case FACE_USER:
        {
          // Wait for the robot to go back to IDLE
          if(robot_->GetState() == Robot::IDLE)
          {
            // Start nodding
            robot_->SendPlayAnimation(ANIM_HEAD_NOD);
            state_ = HAPPY_NODDING;
            PRINT_INFO("NODDING_HEAD\n");
            SoundManager::getInstance()->Play(SOUND_OK_DONE);
            
            // Compute time to stop nodding
            waitUntilTime_ = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + 2;
          }
          break;
        } // case FACE_USER
        case HAPPY_NODDING:
        {
          if (BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() > waitUntilTime_) {
            robot_->SendPlayAnimation(ANIM_BACK_AND_FORTH_EXCITED);
            robot_->MoveHeadToAngle(DEG_TO_RAD(-10), 1, 1);
            
            // Compute time to stop back and forth
            waitUntilTime_ = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + 1.5;
            state_ = BACK_AND_FORTH_EXCITED;
          }
          break;
        } // case HAPPY_NODDING
        case BACK_AND_FORTH_EXCITED:
        {
          if (BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() > waitUntilTime_) {
            robot_->SendPlayAnimation(ANIM_IDLE);
            world_->ClearAllExistingObjects();
            StartMode(BM_June2014DiceDemo);
          }
          break;
        }
        default:
        {
          PRINT_NAMED_ERROR("BehaviorManager.UnknownBehaviorState",
                            "Transitioned to unknown state %d!\n", state_);
          StartMode(BM_None);
          return;
        }
      } // switch(state_)
      
    } // Update_June2014DiceDemo()

    
  } // namespace Cozmo
} // namespace Anki

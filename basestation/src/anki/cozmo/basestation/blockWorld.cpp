/**
 * File: blockWorld.cpp
 *
 * Author: Andrew Stein (andrew)
 * Created: 10/1/2013
 *
 * Description: Implements a container for tracking the state of all objects in Cozmo's world.
 *
 * Copyright: Anki, Inc. 2013
 *
 **/

// TODO: this include is shared b/w BS and Robot.  Move up a level.
#include "anki/cozmo/shared/cozmoConfig.h"
#include "anki/cozmo/shared/cozmoEngineConfig.h"
#include "anki/common/shared/utilities_shared.h"
#include "anki/common/basestation/math/point_impl.h"
#include "anki/common/basestation/math/poseBase_impl.h"
#include "anki/common/basestation/math/quad_impl.h"
#include "anki/common/basestation/math/rect_impl.h"
#include "anki/common/basestation/utils/timer.h"
#include "anki/cozmo/basestation/behaviorSystem/AIWhiteboard.h"
#include "anki/cozmo/basestation/blockWorld.h"
#include "anki/cozmo/basestation/block.h"
#include "anki/cozmo/basestation/components/visionComponent.h"
#include "anki/cozmo/basestation/mat.h"
#include "anki/cozmo/basestation/markerlessObject.h"
#include "anki/cozmo/basestation/robot.h"
#include "anki/cozmo/basestation/navMemoryMap/navMemoryMapFactory.h"
#include "bridge.h"
#include "flatMat.h"
#include "platform.h"
#include "anki/cozmo/basestation/ramp.h"
#include "anki/cozmo/basestation/charger.h"
#include "anki/cozmo/basestation/humanHead.h"
#include "anki/cozmo/basestation/robotInterface/messageHandler.h"
#include "anki/cozmo/basestation/viz/vizManager.h"
#include "anki/cozmo/basestation/externalInterface/externalInterface.h"
#include "anki/cozmo/basestation/navMemoryMap/iNavMemoryMap.h"
#include "anki/cozmo/basestation/navMemoryMap/quadData/navMemoryMapQuadData_Cliff.h"
#include "clad/externalInterface/messageEngineToGame.h"
#include "clad/externalInterface/messageGameToEngine.h"
#include "clad/robotInterface/messageEngineToRobot.h"
#include "anki/vision/basestation/visionMarker.h"
#include "anki/vision/basestation/observableObjectLibrary_impl.h"
#include "util/console/consoleInterface.h"
#include "util/global/globalDefinitions.h"
#include "util/math/math.h"

// The amount of time a proximity obstacle exists beyond the latest detection
#define PROX_OBSTACLE_LIFETIME_MS  4000

// The sensor value that must be met/exceeded in order to have detected an obstacle
#define PROX_OBSTACLE_DETECT_THRESH   5

// Make the (very restrictive) assumption that there is only ever one of each
// type of object in the world at a time (e.g. a single "AngryFace" block or a
// single "Fire" block). So if we see one, always match it to the one we've already
// seen, if it exists.
//  0 = turn this off
//  1 = turn this on just for physical robots
//  2 = turn this on for physical and simulatd robots
#define ONLY_ALLOW_ONE_OBJECT_PER_TYPE 0

#define ENABLE_BLOCK_BASED_LOCALIZATION 1

// TODO: Expose these as parameters
#define BLOCK_IDENTIFICATION_TIMEOUT_MS 500

#define DEBUG_ROBOT_POSE_UPDATES 0
#if DEBUG_ROBOT_POSE_UPDATES
#  define PRINT_LOCALIZATION_INFO(...) PRINT_NAMED_INFO("Localization", __VA_ARGS__)
#else
#  define PRINT_LOCALIZATION_INFO(...)
#endif

namespace Anki {
namespace Cozmo {

CONSOLE_VAR(bool, kEnableMapMemory, "BlockWorld.MapMemory", false); // kEnableMapMemory: if set to true Cozmo creates/uses memory maps
CONSOLE_VAR(bool, kDebugRenderOverheadEdges, "BlockWorld.MapMemory", true); // kDebugRenderOverheadEdges: enables/disables debug render

    BlockWorld::BlockWorld(Robot* robot)
    : _robot(robot)
    , _didObjectsChange(false)
    , _canDeleteObjects(true)
    , _canAddObjects(true)
    , _currentNavMemoryMapOrigin(nullptr)
    , _enableDraw(false)
    {
      CORETECH_ASSERT(_robot != nullptr);
      
      // TODO: Create each known block / matpiece from a configuration/definitions file
      
      //////////////////////////////////////////////////////////////////////////
      // 1x1 Cubes
      //
      
      //blockLibrary_.AddObject(new Block_Cube1x1(Block::FUEL_BLOCK_TYPE));
      
      /*
      _objectLibrary[ObjectFamily::Block].AddObject(new Block_Cube1x1(ObjectType::Block_ANGRYFACE));

      _objectLibrary[ObjectFamily::Block].AddObject(new Block_Cube1x1(ObjectType::Block_BULLSEYE2));
      _objectLibrary[ObjectFamily::Block].AddObject(new Block_Cube1x1(ObjectType::Block_BULLSEYE2_INVERTED));
      
      _objectLibrary[ObjectFamily::Block].AddObject(new Block_Cube1x1(ObjectType::Block_SQTARGET));
      
      _objectLibrary[ObjectFamily::Block].AddObject(new Block_Cube1x1(ObjectType::Block_FIRE));
      
      _objectLibrary[ObjectFamily::Block].AddObject(new Block_Cube1x1(ObjectType::Block_ANKILOGO));
      
      _objectLibrary[ObjectFamily::Block].AddObject(new Block_Cube1x1(ObjectType::Block_STAR5));
      */
      
      //_objectLibrary[ObjectFamily::BLOCKS].AddObject(new Block_Cube1x1(ObjectType::Block_DICE));
      
      /*
      _objectLibrary[ObjectFamily::BLOCKS].AddObject(new Block_Cube1x1(ObjectType::Block_NUMBER1));
      _objectLibrary[ObjectFamily::BLOCKS].AddObject(new Block_Cube1x1(ObjectType::Block_NUMBER2));
      _objectLibrary[ObjectFamily::BLOCKS].AddObject(new Block_Cube1x1(ObjectType::Block_NUMBER3));
      _objectLibrary[ObjectFamily::BLOCKS].AddObject(new Block_Cube1x1(ObjectType::Block_NUMBER4));
      _objectLibrary[ObjectFamily::BLOCKS].AddObject(new Block_Cube1x1(ObjectType::Block_NUMBER5));
      _objectLibrary[ObjectFamily::BLOCKS].AddObject(new Block_Cube1x1(ObjectType::Block_NUMBER6));
       */
      //_objectLibrary[ObjectFamily::BLOCKS].AddObject(new Block_Cube1x1(ObjectType::Block_BANGBANGBANG));
      
      /*
      _objectLibrary[ObjectFamily::Block].AddObject(new Block_Cube1x1(ObjectType::Block_ARROW));
      
      _objectLibrary[ObjectFamily::Block].AddObject(new Block_Cube1x1(ObjectType::Block_FLAG));
      _objectLibrary[ObjectFamily::Block].AddObject(new Block_Cube1x1(ObjectType::Block_FLAG2));
      _objectLibrary[ObjectFamily::Block].AddObject(new Block_Cube1x1(ObjectType::Block_FLAG_INVERTED));
      
      // For CREEP Test
      _objectLibrary[ObjectFamily::Block].AddObject(new Block_Cube1x1(ObjectType::Block_SPIDER));
      _objectLibrary[ObjectFamily::Block].AddObject(new Block_Cube1x1(ObjectType::Block_KITTY));
      _objectLibrary[ObjectFamily::Block].AddObject(new Block_Cube1x1(ObjectType::Block_BEE));
      */
      
      //////////////////////////////////////////////////////////////////////////
      // 1x1 Light Cubes
      //
      
      _objectLibrary[ObjectFamily::LightCube].AddObject(new ActiveCube(ObjectType::Block_LIGHTCUBE1));
      _objectLibrary[ObjectFamily::LightCube].AddObject(new ActiveCube(ObjectType::Block_LIGHTCUBE2));
      _objectLibrary[ObjectFamily::LightCube].AddObject(new ActiveCube(ObjectType::Block_LIGHTCUBE3));      
      
      //////////////////////////////////////////////////////////////////////////
      // 2x1 Blocks
      //
      
      //_objectLibrary[ObjectFamily::Block].AddObject(new Block_2x1(ObjectType::Block_BANGBANGBANG));
      
      
      //////////////////////////////////////////////////////////////////////////
      // Mat Pieces
      //
      
      // Flat mats:
      //_objectLibrary[ObjectFamily::Mat].AddObject(new FlatMat(ObjectType::FlatMat_LETTERS_4x4));
      //_objectLibrary[ObjectFamily::Mat].AddObject(new FlatMat(ObjectType::FlatMat_GEARS_4x4));
      
      // Platform piece:
      //_objectLibrary[ObjectFamily::Mat].AddObject(new Platform(Platform::Type::LARGE_PLATFORM));
      
      // Long Bridge
      //_objectLibrary[ObjectFamily::Mat].AddObject(new Bridge(Bridge::Type::LONG_BRIDGE));
      
      // Short Bridge
      // TODO: Need to update short bridge markers so they don't look so similar to long bridge at oblique viewing angle
      // _objectLibrary[ObjectFamily::Mat].AddObject(new MatPiece(MatPiece::Type::SHORT_BRIDGE));
      
      
      //////////////////////////////////////////////////////////////////////////
      // Ramps
      //
      //_objectLibrary[ObjectFamily::RAMPS].AddObject(new Ramp());
      
      
      //////////////////////////////////////////////////////////////////////////
      // Charger
      //
      _objectLibrary[ObjectFamily::Charger].AddObject(new Charger());
      
      if(_robot->HasExternalInterface())
      {
        SetupEventHandlers(*_robot->GetExternalInterface());
      }
            
    } // BlockWorld() Constructor
  
    void BlockWorld::SetupEventHandlers(IExternalInterface& externalInterface)
    {
      using EventType = AnkiEvent<ExternalInterface::MessageGameToEngine>;
      
      // ClearAllBlocks
      _eventHandles.push_back(externalInterface.Subscribe(ExternalInterface::MessageGameToEngineTag::ClearAllBlocks,
        [this] (const EventType& event)
        {
          _robot->GetContext()->GetVizManager()->EraseAllVizObjects();
          ClearObjectsByFamily(ObjectFamily::Block);
          ClearObjectsByFamily(ObjectFamily::LightCube);
        }));
      
      // ClearAllObjects
      _eventHandles.push_back(externalInterface.Subscribe(ExternalInterface::MessageGameToEngineTag::ClearAllObjects,
        [this] (const EventType& event)
        {
          _robot->GetContext()->GetVizManager()->EraseAllVizObjects();
          ClearAllExistingObjects();
        }));
      
      // SetObjectAdditionAndDeletion
      _eventHandles.push_back(externalInterface.Subscribe(ExternalInterface::MessageGameToEngineTag::SetObjectAdditionAndDeletion,
        [this] (const EventType& event)
        {
          const ExternalInterface::SetObjectAdditionAndDeletion& msg = event.GetData().Get_SetObjectAdditionAndDeletion();
          
          EnableObjectAddition(msg.enableAddition);
          EnableObjectDeletion(msg.enableDeletion);
        }));
      
      // SelectNextObject
      _eventHandles.push_back(externalInterface.Subscribe(ExternalInterface::MessageGameToEngineTag::SelectNextObject,
        [this] (const EventType& event)
        {
          CycleSelectedObject();
        }));
    }
    
    BlockWorld::~BlockWorld()
    {
      
      for(const auto& objectFamily : _existingObjects) {
        for(const auto& objectTypes : objectFamily.second) {
          for(const auto& objectIDs : objectTypes.second) {
            delete objectIDs.second;
          }
        }
      }
      
    } // ~BlockWorld() Destructor
    
    ObservableObject* BlockWorld::GetObjectByIdHelper(const ObjectID objectID) const
    {
      // TODO: Maintain a separate map indexed directly by ID so we don't have to loop over the outer maps?
      
      for(auto & objectsByFamily : _existingObjects) {
        for(auto & objectsByType : objectsByFamily.second) {
          auto objectsByIdIter = objectsByType.second.find(objectID);
          if(objectsByIdIter != objectsByType.second.end()) {
            return objectsByIdIter->second;
          }
        }
      }
      
      return nullptr;
    }
  
    ObservableObject* BlockWorld::GetObjectByIDandFamilyHelper(const ObjectID objectID, const ObjectFamily inFamily) const
    {
      // TODO: Maintain a separate map indexed directly by ID so we don't have to loop over the outer maps?
      
      for(auto & objectsByType : GetExistingObjectsByFamily(inFamily)) {
        auto objectsByIdIter = objectsByType.second.find(objectID);
        if(objectsByIdIter != objectsByType.second.end()) {
          return objectsByIdIter->second;
        }
      }
      
      // ID not found!
      return nullptr;
    }

    ActiveObject* BlockWorld::GetActiveObjectByIDHelper(const ObjectID objectID, const ObjectFamily inFamily) const
    {
      const ObservableObject* object = nullptr;
      const char* familyStr = nullptr;
      if(inFamily == ObjectFamily::Unknown) {
        object = GetObjectByID(objectID);
        familyStr = EnumToString(inFamily);
      } else {
        object = GetObjectByIDandFamily(objectID, inFamily);
        familyStr = "any";
      }
      
      if(object == nullptr) {
        PRINT_NAMED_ERROR("Robot.GetActiveObject",
                          "Object %d does not exist in %s family.",
                          objectID.GetValue(), EnumToString(inFamily));
        return nullptr;
      }
      
      if(!object->IsActive()) {
        PRINT_NAMED_ERROR("Robot.GetActiveObject",
                          "Object %d does not appear to be an active object.",
                          objectID.GetValue());
        return nullptr;
      }
      
      return const_cast<ActiveObject*>(dynamic_cast<const ActiveObject*>(object));
    } // GetActiveObject()
    
    ActiveObject* BlockWorld::GetActiveObjectByActiveIDHelper(const u32 activeID, const ObjectFamily inFamily) const
    {
      for(const auto& objectsByType : _existingObjects) {
        if(inFamily == ObjectFamily::Unknown || inFamily == objectsByType.first) {
          for(const auto& objectsByID : objectsByType.second) {
            for(const auto& objectWithID : objectsByID.second) {
              ObservableObject* object = objectWithID.second;
              if(object->IsActive() && object->GetActiveID() == activeID) {
                return dynamic_cast<ActiveObject*>(object);
              }
            }
          }
        } // if(inFamily == ObjectFamily::Unknown || inFamily == objectsByFamily.first)
      } // for each family
      
      return nullptr;
    } // GetActiveObjectByActiveID()

  
    void CheckForOverlapHelper(const ObservableObject* objectToMatch,
                               ObservableObject* objectToCheck,
                               std::vector<ObservableObject*>& overlappingObjects)
    {
      
      // TODO: smarter block pose comparison
      //const float minDist = 5.f; // TODO: make parameter ... 0.5f*std::min(minDimSeen, objExist->GetMinDim());
      
      //const float distToExist_mm = (objExist.second->GetPose().GetTranslation() -
      //                              <robotThatSawMe???>->GetPose().GetTranslation()).length();
      
      //const float distThresh_mm = distThresholdFraction * distToExist_mm;
      
      //Pose3d P_diff;
      if( objectToCheck->IsSameAs(*objectToMatch) ) {
        overlappingObjects.push_back(objectToCheck);
      } /*else {
         fprintf(stdout, "Not merging: Tdiff = %.1fmm, Angle_diff=%.1fdeg\n",
         P_diff.GetTranslation().length(), P_diff.GetRotationAngle().getDegrees());
         objExist.second->IsSameAs(*objectSeen, distThresh_mm, angleThresh, P_diff);
         }*/
      
    } // CheckForOverlapHelper()
  
    
    void BlockWorld::FindOverlappingObjects(const ObservableObject* objectSeen,
                                            const ObjectsMapByType_t& objectsExisting,
                                            std::vector<ObservableObject*>& overlappingExistingObjects) const
    {
      auto objectsExistingIter = objectsExisting.find(objectSeen->GetType());
      if(objectsExistingIter != objectsExisting.end()) {
        for(const auto& objectToCheck : objectsExistingIter->second) {
          CheckForOverlapHelper(objectSeen, objectToCheck.second, overlappingExistingObjects);
        }
      }
      
    } // FindOverlappingObjects()
    

    void BlockWorld::FindOverlappingObjects(const ObservableObject* objectExisting,
                                            const std::vector<ObservableObject*>& objectsSeen,
                                            std::vector<ObservableObject*>& overlappingSeenObjects) const
    {
      for(const auto& objectToCheck : objectsSeen) {
        CheckForOverlapHelper(objectExisting, objectToCheck, overlappingSeenObjects);
      }
    }
    
    void BlockWorld::FindOverlappingObjects(const ObservableObject* objectExisting,
                                            const std::multimap<f32, ObservableObject*>& objectsSeen,
                                            std::vector<ObservableObject*>& overlappingSeenObjects) const
    {
      for(const auto& objectToCheckPair : objectsSeen) {
        ObservableObject* objectToCheck = objectToCheckPair.second;
        CheckForOverlapHelper(objectExisting, objectToCheck, overlappingSeenObjects);
      }
    }
    
    void BlockWorld::FindIntersectingObjects(const ObservableObject* objectSeen,
                                             std::vector<ObservableObject*>& intersectingExistingObjects,
                                             f32 padding_mm,
                                             const BlockWorldFilter& filter) const
    {
      Quad2f quadSeen = objectSeen->GetBoundingQuadXY(objectSeen->GetPose(), padding_mm);
      
      FindIntersectingObjects(quadSeen,
                              intersectingExistingObjects,
                              padding_mm,
                              filter);
      
    } // FindIntersectingObjects()
    
    
    void BlockWorld::FindIntersectingObjects(const Quad2f& quad,
                                             std::vector<ObservableObject *> &intersectingExistingObjects,
                                             f32 padding_mm,
                                             const BlockWorldFilter& filter) const
    {
      for(auto & objectsByFamily : _existingObjects)
      {
        if(filter.ConsiderFamily(objectsByFamily.first))
        {
          for(auto & objectsByType : objectsByFamily.second)
          {
            if(filter.ConsiderType(objectsByType.first))
            {
              for(auto & objectAndId : objectsByType.second)
              {
                if(filter.ConsiderObject(objectAndId.second))
                {
                  ObservableObject* objExist = objectAndId.second;
                  
                  // If the pose is no longer valid for this object, don't consider it for intersection
                  if (objExist->IsPoseStateUnknown())
                  {
                    continue;
                  }
                  
                  // Get quad of object and check for intersection
                  Quad2f quadExist = objExist->GetBoundingQuadXY(objExist->GetPose(), padding_mm);
                  
                  if( quadExist.Intersects(quad) ) {
                    intersectingExistingObjects.push_back(objExist);
                  }
                } // if useID
              }  // for each object
            }  // if not ignoreType
          }  // for each type
        }  // if not ignoreFamily
      } // for each family
      
    } // FindIntersectingObjects()


  Result BlockWorld::BroadcastObjectObservation(const ObservableObject* observedObject,
                                                bool markersVisible)
  {    
    if(_robot->HasExternalInterface())
    {
      if(observedObject->IsExistenceConfirmed() || markersVisible)
      {
        // Project the observed object into the robot's camera, using its new pose
        std::vector<Point2f> projectedCorners;
        f32 observationDistance = 0;
        _robot->GetVisionComponent().GetCamera().ProjectObject(*observedObject, projectedCorners, observationDistance);
        
        Rectangle<f32> boundingBox(projectedCorners);
        
        Radians topMarkerOrientation(0);
        if(observedObject->IsActive()) {
          if (observedObject->GetFamily() == ObjectFamily::LightCube) {
            const ActiveCube* activeCube = dynamic_cast<const ActiveCube*>(observedObject);
            if(activeCube == nullptr) {
              PRINT_NAMED_ERROR("BlockWorld.AddAndUpdateObjects",
                                "ObservedObject %d with IsActive()==true could not be cast to ActiveCube.",
                                observedObject->GetID().GetValue());
              return RESULT_FAIL;
            } else {
              topMarkerOrientation = activeCube->GetTopMarkerOrientation();
              
              //PRINT_INFO("Object %d's rotation around Z = %.1fdeg\n", obsID.GetValue(),
              //           topMarkerOrientation.getDegrees());
            }
          }
        }
        
        const Vec3f& T = observedObject->GetPose().GetTranslation();
        const UnitQuaternion<float>& q = observedObject->GetPose().GetRotation().GetQuaternion();

        using namespace ExternalInterface;

        RobotObservedObject observation(_robot->GetID(),
                                        observedObject->GetLastObservedTime(),
                                        observedObject->GetFamily(),
                                        observedObject->GetType(),
                                        observedObject->GetID(),
                                        boundingBox.GetX(),
                                        boundingBox.GetY(),
                                        boundingBox.GetWidth(),
                                        boundingBox.GetHeight(),
                                        T.x(), T.y(), T.z(),
                                        q.w(), q.x(), q.y(), q.z(),
                                        topMarkerOrientation.ToFloat(),
                                        markersVisible,
                                        observedObject->IsActive());
        
        if( observedObject->IsExistenceConfirmed()) {
          _robot->Broadcast(MessageEngineToGame(std::move(observation)));
        }
        else if( markersVisible ) {
          // clear the object ID, since it isn't reliable until the existence is confirmed
          observation.objectID = -1;
          _robot->Broadcast(MessageEngineToGame(RobotObservedPossibleObject(std::move(observation))));
        }
      }
    } // if(_robot->HasExternalInterface())
    
    return RESULT_OK;
    
  } // BroadcastObjectObservation()
  
  Result BlockWorld::UpdateObjectOrigins(const Pose3d *oldOrigin,
                                         const Pose3d *newOrigin)
  {
    Result result = RESULT_OK;
    
    if(nullptr == oldOrigin || nullptr == newOrigin) {
      PRINT_NAMED_ERROR("BlockWorld.UpdateObjectOrigins.OriginFail",
                        "Old and new origin must not be NULL");
      
      return RESULT_FAIL;
    }
    
    for(auto & objectsByFamily : _existingObjects)
    {
      for(auto & objectsByType : objectsByFamily.second)
      {
        for(auto & objectsByID : objectsByType.second)
        {
          ObservableObject* object = objectsByID.second;
          if(object->GetPose().GetParent() == oldOrigin)
          {
            
            Pose3d newPose;
            if(false == object->GetPose().GetWithRespectTo(*newOrigin, newPose)) {
              PRINT_NAMED_ERROR("BlockWorld.UpdateObjectOrigins.OriginFail",
                                "Could not get object %d w.r.t new origin %s",
                                object->GetID().GetValue(),
                                newOrigin->GetName().c_str());
              
              result = RESULT_FAIL;
            } else {
              const Vec3f& T_old = object->GetPose().GetTranslation();
              const Vec3f& T_new = newPose.GetTranslation();
              PRINT_NAMED_INFO("BlockWorld.UpdateObjectOrigins.ObjectOriginChanged",
                               "Updating object %d's origin from %s to %s. "
                               "T_old=(%.1f,%.1f,%.1f), T_new=(%.1f,%.1f,%.1f)",
                               object->GetID().GetValue(),
                               oldOrigin->GetName().c_str(),
                               newOrigin->GetName().c_str(),
                               T_old.x(), T_old.y(), T_old.z(),
                               T_new.x(), T_new.y(), T_new.z());
              
              object->SetPose(newPose, -1.f, true);
              
              BroadcastObjectObservation(object, false);
            }
          }
        }
      }
    }
    
    // if memory maps are enabled, we can merge old into new
    if ( kEnableMapMemory )
    {
      // oldOrigin is the pointer/id of the current map
      // worldOrigin is the pointer/id of the map we can merge into/from
      ASSERT_NAMED( _navMemoryMaps.find(oldOrigin) != _navMemoryMaps.end(), "BlockWorld.UpdateObjectOrigins.missingMapOriginOld");
      ASSERT_NAMED( _navMemoryMaps.find(newOrigin) != _navMemoryMaps.end(), "BlockWorld.UpdateObjectOrigins.missingMapOriginNew");
      ASSERT_NAMED( oldOrigin == _currentNavMemoryMapOrigin, "BlockWorld.UpdateObjectOrigins.updatingMapNotCurrent");

      // grab the underlying memory map and merge them
      INavMemoryMap* oldMap = _navMemoryMaps[oldOrigin].get();
      INavMemoryMap* newMap = _navMemoryMaps[newOrigin].get();
      newMap->Merge(oldMap, *oldOrigin);
      
      // switch back to what is becoming the new map
      _currentNavMemoryMapOrigin = newOrigin;
      
      // now we can delete what is become the old map, since we have merged its data into the new one
      _navMemoryMaps.erase( oldOrigin ); // smart pointer will delete memory
    }
    
    return result;
  }
  
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  const INavMemoryMap* BlockWorld::GetNavMemoryMap() const
  {
    const INavMemoryMap* curMap = nullptr;
    if ( nullptr != _currentNavMemoryMapOrigin ) {
      auto matchPair = _navMemoryMaps.find(_currentNavMemoryMapOrigin);
      if ( matchPair != _navMemoryMaps.end() ) {
        curMap = matchPair->second.get();
      } else {
        ASSERT_NAMED(false, "BlockWorld.GetNavMemoryMap.MissingMap");
      }
    }
    return curMap;
  }
  
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  INavMemoryMap* BlockWorld::GetNavMemoryMap()
  {
    INavMemoryMap* curMap = nullptr;
    if ( nullptr != _currentNavMemoryMapOrigin ) {
      auto matchPair = _navMemoryMaps.find(_currentNavMemoryMapOrigin);
      if ( matchPair != _navMemoryMaps.end() ) {
        curMap = matchPair->second.get();
      } else {
        ASSERT_NAMED(false, "BlockWorld.GetNavMemoryMap.MissingMap");
      }
    }
    return curMap;
  }
  
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  void BlockWorld::UpdateNavMemoryMap()
  {
    INavMemoryMap* currentNavMemoryMap = GetNavMemoryMap();
    if ( nullptr != currentNavMemoryMap )
    {
      // cliff quad: clear or cliff
      {
        // TODO configure this size somethere else
        Point3f cliffSize = MarkerlessObject(ObjectType::ProxObstacle).GetSize() * 0.5f;
        
        // cliff quad
        Quad3f cliffquad {
          {+cliffSize.x(), +cliffSize.y(), cliffSize.z()},  // up L
          {-cliffSize.x(), +cliffSize.y(), cliffSize.z()},  // lo L
          {+cliffSize.x(), -cliffSize.y(), cliffSize.z()},  // up R
          {-cliffSize.x(), -cliffSize.y(), cliffSize.z()}}; // lo R
        _robot->GetPose().ApplyTo(cliffquad, cliffquad);
        
        if ( _robot->IsCliffDetected() )
        {
          // build data we want to embed for this quad
          NavMemoryMapQuadData_Cliff cliffData;
          Vec3f rotatedFwdVector = _robot->GetPose().GetRotation() * X_AXIS_3D();
          cliffData.directionality = Vec2f{rotatedFwdVector.x(), rotatedFwdVector.y()};
          currentNavMemoryMap->AddQuad(cliffquad, cliffData);
        }
        else
        {
          currentNavMemoryMap->AddQuad(cliffquad, INavMemoryMap::EContentType::ClearOfCliff);
        }
      }
      
      // forward sensor
      #define TRUST_FORWARD_SENSOR 0
      if( TRUST_FORWARD_SENSOR )
      {
        // - - -
        // ClearOfObstacle from 0                      to _forwardSensorValue_mm
        // Obstacle        from _forwardSensorValue_mm to MaxSensor
        // - - -
        const float sensorValue = ((float)_robot->GetForwardSensorValue());
        const float maxSensorValue = ((float)FORWARD_COLLISION_SENSOR_LENGTH_MM);
      
        // debug?
        const float kDebugRenderZOffset = 25.0f; // Z offset for render only so that it doesn't render underground
        const bool kDebugRenderForwardQuads = false;
        _robot->GetContext()->GetVizManager()->EraseSegments("BlockWorld::UpdateNavMemoryMap");
        
        // fetch vars
        const float kFrontCollisionSensorWidth = 1.0f; // TODO Should this be in CozmoEngineConfig.h ?
        const Pose3d& robotPose = _robot->GetPose();

        // ray we cast from sensor
        const Vec3f forwardRay = robotPose.GetRotation() * X_AXIS_3D();
        
        // robot detection points
        Point3f robotForwardLeft  = robotPose * Vec3f{ 0.0f,  kFrontCollisionSensorWidth, 0.0f};
        Point3f robotForwardRight = robotPose * Vec3f{ 0.0f, -kFrontCollisionSensorWidth, 0.0f};
        const Point3f clearUntilLeft  = robotForwardLeft  + (forwardRay*sensorValue);
        const Point3f clearUntilRight = robotForwardRight + (forwardRay*sensorValue);
        
        // clear
        const bool hasClearInFront = Util::IsFltGTZero(sensorValue);
        if ( hasClearInFront )
        {
          // create quad for ClearOfObstacle
          const Point2f clearQuadBL( robotForwardLeft  );
          const Point2f clearQuadBR( robotForwardRight );
          const Point2f clearQuadTL( clearUntilLeft    );
          const Point2f clearQuadTR( clearUntilRight   );
          Quad2f clearCollisionQuad { clearQuadTL, clearQuadBL, clearQuadTR, clearQuadBR };
          currentNavMemoryMap->AddQuad(clearCollisionQuad, INavMemoryMap::EContentType::ClearOfObstacle);
          
          // also notify behavior whiteboard.
          // rsam: should this information be in the map instead of the whiteboard? It seems a stretch that
          // blockworld knows now about behaviors, maybe all this processing of quads should be done in a separate
          // robot component, like a VisualInformationProcessingComponent
          _robot->GetBehaviorManager().GetWhiteboard().ProcessClearQuad(clearCollisionQuad);
        
          // debug render detection lines
          if ( kDebugRenderForwardQuads )
          {
            _robot->GetContext()->GetVizManager()->DrawSegment("BlockWorld::UpdateNavMemoryMap",
              Point3f{clearQuadBL.x(),clearQuadBL.y(), kDebugRenderZOffset},
              Point3f{clearQuadTL.x(),clearQuadTL.y(), kDebugRenderZOffset},
              Anki::NamedColors::WHITE,
              false);
            _robot->GetContext()->GetVizManager()->DrawSegment("BlockWorld::UpdateNavMemoryMap",
              Point3f{clearQuadBR.x(),clearQuadBR.y(), kDebugRenderZOffset},
              Point3f{clearQuadTR.x(),clearQuadTR.y(), kDebugRenderZOffset},
              Anki::NamedColors::OFFWHITE,
              false);
          }
        }

        // obstacle
        const bool detectedObstacle = Util::IsFltLT(sensorValue, maxSensorValue);
        if ( detectedObstacle )
        {
          // TODO configure this elsewhere. Should not need to create an obstacle just to grab its size
          Point3f obstacleSize = MarkerlessObject(ObjectType::ProxObstacle).GetSize() * 0.5f;
          const float distance = obstacleSize.x();
          const Point3f obstacleUntilLeft  = clearUntilLeft  + (forwardRay*distance);
          const Point3f obstacleUntilRight = clearUntilRight + (forwardRay*distance);
          
          // create quad for ObstacleUnrecognized
          const Point2f obsQuadBL( clearUntilLeft     );
          const Point2f obsQuadBR( clearUntilRight    );
          const Point2f obsQuadTL( obstacleUntilLeft  );
          const Point2f obsQuadTR( obstacleUntilRight );
          Quad2f obsCollisionQuad { obsQuadTL, obsQuadBL, obsQuadTR, obsQuadBR };
          currentNavMemoryMap->AddQuad(obsCollisionQuad, INavMemoryMap::EContentType::ObstacleUnrecognized);
        
          // debug render detection lines
          if ( kDebugRenderForwardQuads )
          {
            _robot->GetContext()->GetVizManager()->DrawSegment("BlockWorld::UpdateNavMemoryMap",
              Point3f{obsQuadBL.x(),obsQuadBL.y(), kDebugRenderZOffset},
              Point3f{obsQuadTL.x(),obsQuadTL.y(), kDebugRenderZOffset},
              Anki::NamedColors::ORANGE,
              false);
            _robot->GetContext()->GetVizManager()->DrawSegment("BlockWorld::UpdateNavMemoryMap",
              Point3f{obsQuadBR.x(),obsQuadBR.y(), kDebugRenderZOffset},
              Point3f{obsQuadTR.x(),obsQuadTR.y(), kDebugRenderZOffset},
              Anki::NamedColors::RED,
              false);
          }
        }
      }
      
      currentNavMemoryMap->AddQuad(_robot->GetBoundingQuadXY(), INavMemoryMap::EContentType::ClearOfObstacle );
      
      // also notify behavior whiteboard.
      // rsam: should this information be in the map instead of the whiteboard? It seems a stretch that
      // blockworld knows now about behaviors, maybe all this processing of quads should be done in a separate
      // robot component, like a VisualInformationProcessingComponent
      _robot->GetBehaviorManager().GetWhiteboard().ProcessClearQuad(_robot->GetBoundingQuadXY());
    }
  }
  
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  void BlockWorld::CreateLocalizedMemoryMap(const Pose3d* worldOriginPtr)
  {
    // can disable the feature completely
    if ( !kEnableMapMemory ) {
      return;
    }
  
    // clear all memory map rendering because indexHints are changing
    #if ANKI_DEVELOPER_CODE
    {
      for ( const auto& memMapPair : _navMemoryMaps )
      {
        memMapPair.second->ClearDraw();
      }
    }
    #endif
    
    // if the origin is null, we would never merge the map, which could leak if a new one was created
    // do not support this by not creating one at all if the origin is null
    ASSERT_NAMED(nullptr != worldOriginPtr, "BlockWorld.CreateLocalizedMemoryMap.NullOrigin");
    if ( nullptr != worldOriginPtr )
    {
      // create a new memory map in the given origin
      VizManager* vizMgr = _robot->GetContext()->GetVizManager();
      INavMemoryMap* navMemoryMap = NavMemoryMapFactory::CreateDefaultNavMemoryMap(vizMgr);
      _navMemoryMaps.emplace( std::make_pair(worldOriginPtr, std::unique_ptr<INavMemoryMap>(navMemoryMap)) );
      _currentNavMemoryMapOrigin = worldOriginPtr;
    }
  }

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  void BlockWorld::DrawNavMemoryMap() const
  {
    #if ANKI_DEVELOPER_CODE
    {
      size_t lastIndexNonCurrent = 0;
    
      // rendering all current maps with indexHint
      for (const auto& memMapPair : _navMemoryMaps)
      {
        const bool isCurrent = memMapPair.first == _currentNavMemoryMapOrigin;
        
        size_t indexHint = isCurrent ? 0 : (++lastIndexNonCurrent);
        memMapPair.second->Draw(indexHint);
      }
    }
    #endif
  }

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  void BlockWorld::AddNewObject(ObjectsMapByType_t& existingFamily, ObservableObject* object)
  {
    if(!object->GetID().IsSet()) {
      object->SetID();
    }
    
    // Set the viz manager on this new object
    object->SetVizManager(_robot->GetContext()->GetVizManager());
    
    // TODO if an object with same ID exists, it will leak
    existingFamily[object->GetType()][object->GetID()] = object;
  }
  
    Result BlockWorld::AddAndUpdateObjects(const std::multimap<f32, ObservableObject*>& objectsSeen,
                                           const ObjectFamily& inFamily,
                                           const TimeStamp_t atTimestamp)
    {
      ObjectsMapByType_t& objectsExisting = _existingObjects[inFamily];
      const Pose3d* currFrame = &_robot->GetPose().FindOrigin();
      
      // Struct for storing pairs of currently observed objects and their
      // matching object that is currently known.
      // To be used for localization after all observed objects have been processed.
      struct ObservedAndMatchedPair {
        ObservableObject* observedObject;
        ObservableObject* matchedObject;
        f32 distance;

        // Sets the pose of matchedObject to that of observedObject
        // and deletes observedObject.
        void MergeAndDelete() {
          matchedObject->SetPose( observedObject->GetPose(), distance );
          delete observedObject;
        }
        
      };
      std::map<const Pose3d*, ObservedAndMatchedPair > potentialObjectsForLocalizingTo;
      

      
      for(const auto& objSeenPair : objectsSeen) {

        ObservableObject* objSeen = objSeenPair.second;
        
        //const float minDimSeen = objSeen->GetMinDim();
        
        ObservableObject* matchingObject = nullptr;
        
        
        // Ignoring any block observed outside of the localization range
        const f32 distToObj = ComputeDistanceBetween(_robot->GetPose(), objSeen->GetPose());
        if (distToObj > MAX_LOCALIZATION_AND_ID_DISTANCE_MM) {
          //PRINT_NAMED_INFO("BlockWorld.AddAndUpdateObjects.IgnoringCuzObjectTooFar", "dist %fmm", distToObj);
          BroadcastObjectObservation(objSeen, true);
          continue;
        }

        if (objSeen->IsActive()) {
          // Find all objects of the same type
          BlockWorldFilter filter;
          filter.SetFilterFcn([objSeen] (ObservableObject* obj) { return objSeen->GetType() == obj->GetType(); });
          std::vector<ObservableObject*> blocks;
          FindMatchingObjects(filter, blocks);
          
          if (blocks.size() > 1) {
            PRINT_NAMED_WARNING("BlockWorld.AddAndUpdateObjects.MultipleMatchesForActiveObject",
                                "Observed active object of type %d matches %lu existing objects. Multiple blocks of same type not currently supported.",
                                objSeen->GetType(), (unsigned long)blocks.size());
            
          } else if (blocks.size() == 0) {
            PRINT_NAMED_WARNING("BlockWorld.AddAndUpdateObjects.NoMatchForActiveObject",
                                "Observed active object of type %d does not match an existing object. Is the battery plugged in?",
                                objSeen->GetType());
          } else {
            matchingObject = blocks.front();
            //PRINT_NAMED_INFO("BlockWorld.AddAndUpdateObjects.FoundMatchingActiveObject",
            //                 "Observed active object of type %d matches existing objectID %d (activeID %d)",
            //                 objSeen->GetType(), matchingObject->GetID().GetValue(), matchingObject->GetActiveID());

          }

          // If this is the object we're carrying observed in the carry position, do nothing and continue to the next observed object.
          // Otherwise, it must've been moved off the lift so unset its carry state.
          if ((matchingObject != nullptr) && (matchingObject->GetID() == _robot->GetCarryingObject())) {
            if (matchingObject->GetPose().IsSameAs(objSeen->GetPose(),
                                                   objSeen->GetSameDistanceTolerance(),
                                                   objSeen->GetSameAngleTolerance())) {
              delete objSeen;
              continue;
            } else {
              _robot->UnSetCarryObject(matchingObject->GetID());
            }
          }

        } else {

          // Store pointers to any existing objects that overlap with this one
          //std::vector<ObservableObject*> overlappingObjects;
          //FindOverlappingObjects(objSeen, objectsExisting, overlappingObjects);
          
          // Override the default filter function to intentionally consider objects
          // that are unknown here. Otherwise, we'd never be able to match new
          // observations to existing objects whose pose has been set to unknown!
          BlockWorldFilter filter;
          filter.SetFilterFcn([] (ObservableObject*) { return true; });
          
          matchingObject = FindClosestMatchingObject(*objSeen,
                                                     objSeen->GetSameDistanceTolerance(),
                                                     objSeen->GetSameAngleTolerance(),
                                                     filter);
          
          // If this is the object we're carrying, do nothing and continue to the next observed object
          if ((matchingObject != nullptr) && (matchingObject->GetID() == _robot->GetCarryingObject())) {
            delete objSeen;
            continue;
          }
          
        }
        
        
        // As of now the object will be w.r.t. the robot's origin.  If we
        // observed it to be on a mat, however, make it relative to that mat.
        const f32 objectDiagonal = objSeen->GetSameDistanceTolerance().Length();
        ObservableObject* parentMat = nullptr;

        for(const auto& objectsByType : _existingObjects[ObjectFamily::Mat]) {
          for(const auto& objectsByID : objectsByType.second) {
            MatPiece* mat = dynamic_cast<MatPiece*>(objectsByID.second);
            assert(mat != nullptr);
            
            // Don't make this mat the parent of any objects until it has been
            // seen enough time
            if(mat->GetNumTimesObserved() >= MIN_TIMES_TO_OBSERVE_OBJECT) {
              Pose3d newPoseWrtMat;
              // TODO: Better height tolerance approach
              if(mat->IsPoseOn(objSeen->GetPose(), objectDiagonal*.5f, objectDiagonal*.5f, newPoseWrtMat)) {
                objSeen->SetPose(newPoseWrtMat);
                parentMat = mat;
              }
            }
          }
        }
        
        std::vector<Point2f> projectedCorners;
        ObservableObject* observedObject = nullptr;

        if(matchingObject == nullptr) {
          
#         if ONLY_ALLOW_ONE_OBJECT_PER_TYPE > 0
          
          // See if there are any existing objects in the world with the same type
          // as the one we're seeing. Also make sure that they have not already
          // been seen in this same frame (to prevent signaling multiple objects
          // of the same type being seen simultaneously). Finally, only do this
          // if it's a physical robot, dependning on the ONLY_ALLOW_ONE_OBJECT_PER_TYPE
          // setting.
          ObjectsMapByID_t objectsWithType = GetExistingObjectsByType(objSeen->GetType());
          if(!objectsWithType.empty()
#            if ONLY_ALLOW_ONE_OBJECT_PER_TYPE == 1
             && _robot->IsPhysical()
#            endif
             )
          {
            // We already know about an object of this type. Assume the one we
            // are seeing is that one. Just update it to be in the pose of the
            // observed object.
            
            // By definition, we can't have more than one object of this type
            assert(objectsWithType.size() == 1);
            
            observedObject = objectsWithType.begin()->second;
            
            if(observedObject->GetLastObservedTime() < objSeen->GetLastObservedTime()) {
              
              assert(observedObject->GetType() == objSeen->GetType());
              
              PRINT_NAMED_WARNING("BlockWorld.AddAndUpdateObjects.UpdatingByType",
                                  "Did not match observed object to existing %s object "
                                  "by pose, but assuming there's only one that must match "
                                  "existing ID = %d. (since ONLY_ALLOW_ONE_OBJECT_PER_TYPE = %d)",
                                  ObjectTypeToString(objSeen->GetType()),
                                  observedObject->GetID().GetValue(),
                                  ONLY_ALLOW_ONE_OBJECT_PER_TYPE);
              
              observedObject->SetPose( objSeen->GetPose() );
              
              // If we are matching based solely on type to an existing object that
              // has only been seen once, don't update the observed time (so that we
              // also don't update the number of times observed), since the poses
              // don't actually match and we really want to increment the number of
              // times of observed only if we re-see an object in the same place.
              // (If we are here, we didn't see it in the same place; we are only
              // updating it because we are assuming it's the same object based on
              // type.)
              if(observedObject->GetNumTimesObserved() >= MIN_TIMES_TO_OBSERVE_OBJECT) {
                // Update lastObserved times of this object
                observedObject->SetLastObservedTime(objSeen->GetLastObservedTime());
                observedObject->UpdateMarkerObservationTimes(*objSeen);
              }
              
              // Project this existing object into the robot's camera, using its new pose
              _robot->GetVisionComponent().GetCamera().ProjectObject(*observedObject, projectedCorners, observationDistance);
              
              // If the object is being carried, uncarry it
              if (_robot->GetCarryingObject() == observedObject->GetID()) {
                PRINT_NAMED_INFO("BlockWorld.AddAndUpdateObjects.SawCarryObject",
                                 "Uncarrying object ID=%d because it was observed", (int)observedObject->GetID());
                _robot->UnSetCarryingObjects();
              }
            } else {
              PRINT_NAMED_WARNING("BlockWorld.AddAndUpdateObjects.UpdatingByType",
                                  "Ignoring the second simultaneously-seen %s object "
                                  "(since ONLY_ALLOW_ONE_OBJECT_PER_TYPE = %d)",
                                  ObjectTypeToString(objSeen->GetType()),
                                  ONLY_ALLOW_ONE_OBJECT_PER_TYPE);
            }
            
            // Now that we've merged in objSeen, we can delete it because we
            // will no longer be using it.  Otherwise, we'd leak.
            delete objSeen;
            
          } else {
            // Otherwise, add a new object
#         endif // ONLY_ALLOW_ONE_OBJECT_PER_TYPE
            
          if(!_canAddObjects) {
            PRINT_NAMED_WARNING("BlockWorld.AddAndUpdateObject.AddingDisabled",
                                "Saw a new %s%s object, but adding objects is disabled.",
                                objSeen->IsActive() ? "active " : "",
                                ObjectTypeToString(objSeen->GetType()));
            
            // Delete this object since we're not going to add it or merge it
            delete objSeen;
            
            // Keep looking through objects we saw
            continue;
          }
            
          // no existing objects overlapped with the objects we saw, so add it
          // as a new object.
          // NOTE: This will also trigger identification of active objects.
          AddNewObject(objectsExisting, objSeen);
          
          PRINT_NAMED_INFO("BlockWorld.AddAndUpdateObjects.AddNewObject",
                           "Adding new %s%s object and ID=%d at (%.1f, %.1f, %.1f), relative to %s mat.",
                           objSeen->IsActive() ? "active " : "",
                           ObjectTypeToString(objSeen->GetType()),
                           objSeen->GetID().GetValue(),
                           objSeen->GetPose().GetTranslation().x(),
                           objSeen->GetPose().GetTranslation().y(),
                           objSeen->GetPose().GetTranslation().z(),
                           parentMat==nullptr ? "NO" : ObjectTypeToString(parentMat->GetType()));
          
          // Don't add as an occluder the very first time we see the object; wait
          // until we've seen it MIN_TIMES_TO_OBSERVE_OBJECT times.
          // // Project this new object into the robot's camera:
          // //_robot->GetVisionComponent().GetCamera().ProjectObject(*objSeen, projectedCorners, observationDistance);
          
          observedObject = objSeen;
            
#         if ONLY_ALLOW_ONE_OBJECT_PER_TYPE
          } // if/else if(!objectsWithType.empty())
#         endif
          
          /*
           PRINT_NAMED_INFO("BlockWorld.AddToOcclusionMaps.AddingObjectOccluder",
           "Adding object %d as an occluder for robot %d.\n",
           object->GetID().GetValue(),
           robot->GetID());
           */
          
        } else { // This is an existing object
          
          // NOTE: Since we're assuming there can only be one instance of each active object type
          //       the matching is done immediately at the top and there is no identification step required.
          //       If that ever comes back we may have to revive this in some form.
          /*
          if(matchingObject->IsActive())
          {
            if(ActiveIdentityState::Identified == matchingObject->GetIdentityState())
            {
              if(_unidentifiedActiveObjects.count(matchingObject->GetID()) > 0)
              {
                // This object just got identified, so make sure it is not an active
                // object we already knew about.
                
                _unidentifiedActiveObjects.erase(matchingObject->GetID());
                
                for(auto & objectIter : _existingObjects[matchingObject->GetFamily()][matchingObject->GetType()])
                {
                  ObservableObject* candidateObject = objectIter.second;
                  assert(candidateObject->IsActive()); // all objects of this type should be active
                  
                  if(candidateObject->GetID() != matchingObject->GetID() &&
                     candidateObject->GetActiveID() == matchingObject->GetActiveID())
                  {
                    PRINT_NAMED_INFO("BlockWorld.AddAndUpdateObject.FoundDuplicateActiveID",
                                     "Found duplicate active ID %d: will use %d and delete %d.",
                                     matchingObject->GetActiveID(),
                                     candidateObject->GetID().GetValue(), matchingObject->GetID().GetValue());
                    DeleteObject(matchingObject->GetID());
                    matchingObject = candidateObject;
                    
                    // If the matching object currently thinks it's being carried, unset its carry state.
                    ActionableObject* actionObject = dynamic_cast<ActionableObject*>(matchingObject);
                    if(actionObject != nullptr) {
                      if(actionObject->IsBeingCarried()) {
                        PRINT_NAMED_WARNING("BlockWorld.AddAndUpdateObject.ObservedObjectMovedOffLift",
                                            "Object %d was probably moved off lift. Setting as uncarried.",
                                            actionObject->GetID().GetValue());
                        _robot->UnSetCarryObject(actionObject->GetID());
                      }
                    }

                    
                    break;
                  }
                }
              }
            } else if(!_robot->IsPickingOrPlacing() && !_robot->GetMoveComponent().IsMoving()) { // Don't do identification if picking and placing or moving
              // Tick the fake identification process for any as-yet-unidentified active
              // objects. This is to simulate the fact that identification is not instantaneous
              // and is asynchronous.
              // TODO: Shouldn't need to do this once we have real block identification

              matchingObject->Identify();
            }
          } // if(matchingObject->IsActive())
          */
          
          // Check if there are objects on top of this object that need to be moved since the
          // object it's resting on has moved.

          // Updates poses of stacks of objects by finding the difference between old object poses and applying that
          // to the new observed poses. Has to use the old object to find the object on top
          ObservableObject* objectOnTop = FindObjectOnTopOf(*matchingObject, STACKED_HEIGHT_TOL_MM);
          ObservableObject* newObjectOnBottom = objSeen;
          ObservableObject* oldObjectOnBottom = matchingObject->CloneType();
          oldObjectOnBottom->SetPose(matchingObject->GetPose());
          // If the object was already updated this timestamp then don't bother doing this.
          while(objectOnTop != nullptr && objectOnTop->GetLastObservedTime() != objSeen->GetLastObservedTime()) {

            // Get difference in position between top object's pose and the previous pose of the observed bottom object.
            // Apply difference to the new observed pose to get the new top object pose.
            Pose3d topPose = objectOnTop->GetPose();
            Pose3d bottomPose = oldObjectOnBottom->GetPose();
            Vec3f diff = topPose.GetTranslation() - bottomPose.GetTranslation();
            topPose.SetTranslation( newObjectOnBottom->GetPose().GetTranslation() + diff );
            Util::SafeDelete(oldObjectOnBottom);
            oldObjectOnBottom = objectOnTop->CloneType();
            oldObjectOnBottom->SetPose(objectOnTop->GetPose());
            objectOnTop->SetPose(topPose);
            
            // See if there's an object above this object
            newObjectOnBottom = objectOnTop;
            objectOnTop = FindObjectOnTopOf(*oldObjectOnBottom, STACKED_HEIGHT_TOL_MM);
          }
          Util::SafeDelete(oldObjectOnBottom);
          // TODO: Do the same adjustment for blocks that are _below_ observed blocks? Does this make sense?
          
          // Update lastObserved times of this object
          // (Do this before possibly attempting to localize to the object below!)
          matchingObject->SetLastObservedTime(objSeen->GetLastObservedTime());
          matchingObject->UpdateMarkerObservationTimes(*objSeen);


          bool useThisObjectToLocalize = false;
#         if ENABLE_BLOCK_BASED_LOCALIZATION
          // Decide whether we will be updating the robot's pose relative to this
          // object or updating the object's pose w.r.t. the robot. We only localize
          // to the object if:
          //  - the object is close enough,
          //  - the object didn't _just_ get identified (since we still need to check if
          //     its active ID matches a pre-existing one (on the next Update)
          //  - if the object is *currently* being observed as flat
          //  - if the object can offer localization info (which it last being
          //     observed as flat)
          //  - the object is neither the object the robot is docking to or tracking to,
          //  - if the robot isn't already localized to an object or it has moved
          //     since the last time it got localized to an object.
          useThisObjectToLocalize = (distToObj <= MAX_LOCALIZATION_AND_ID_DISTANCE_MM &&
                                     //_unidentifiedActiveObjects.count(matchingObject->GetID()) == 0 &&
                                     matchingObject->CanBeUsedForLocalization() &&
                                     matchingObject->GetID() != _robot->GetDockObject() &&
                                     matchingObject->GetID() != _robot->GetMoveComponent().GetTrackToObject() &&
                                     (_robot->GetLocalizedTo().IsUnknown() ||
                                      _robot->HasMovedSinceBeingLocalized()) );
#         endif
          
          
          // Now that we've decided whether or not to use this object for localization,
          // either use it to upate the robot's pose or use the robot's pose to
          // update the object's pose.
          if(useThisObjectToLocalize)
          {
            if (!_robot->GetMoveComponent().IsMoving()) {

              assert(ActiveIdentityState::Identified == matchingObject->GetIdentityState());

              // If the objSeen is no closer to the robot than the object already stored for this frame
              // then just update its pose. Otherwise, set the pose of the stored object and replace
              // it with this one.
              const Pose3d* matchingObjectFrame = &matchingObject->GetPose().FindOrigin();

              if (potentialObjectsForLocalizingTo.count(matchingObjectFrame) > 0) {
                // There's already an ObservedMatchPair for the current frame.
                // Check if it's farther away from robot than this one.
                ObservedAndMatchedPair* obsAndMatchPair = &potentialObjectsForLocalizingTo.at(matchingObjectFrame);
                if (distToObj < obsAndMatchPair->distance ) {
                  // This new one is closer so merge the stored pair and replace it with this one
                  obsAndMatchPair->MergeAndDelete();
                  *obsAndMatchPair = { .observedObject = objSeen, .matchedObject = matchingObject, .distance = distToObj };
                } else {
                  matchingObject->SetPose( objSeen->GetPose(), distToObj );
                  delete objSeen;
                }

              } else {
                potentialObjectsForLocalizingTo[matchingObjectFrame] = { .observedObject = objSeen, .matchedObject = matchingObject, .distance = distToObj };
              }

            }
          } else {
            matchingObject->SetPose( objSeen->GetPose(), distToObj );
            delete objSeen;
          }
          
          observedObject = matchingObject;
          
          /* This is pretty verbose... 
          fprintf(stdout, "Merging observation of object type=%s, with ID=%d at (%.1f, %.1f, %.1f), timestamp=%d\n",
                  objSeen->GetType().GetName().c_str(),
                  overlappingObjects[0]->GetID().GetValue(),
                  objSeen->GetPose().GetTranslation().x(),
                  objSeen->GetPose().GetTranslation().y(),
                  objSeen->GetPose().GetTranslation().z(),
                  overlappingObjects[0]->GetLastObservedTime());
          */
          
          // Project this existing object into the robot's camera, using its new pose
          //_robot->GetVisionComponent().GetCamera().ProjectObject(*matchingObject, projectedCorners, observationDistance);
          
          // Add all observed markers of this object as occluders, once it has been
          // identified (it's possible we're seeing an existing object behind its
          // last-known location, in which case we don't want to delete the existing
          // one before realizing the new observation is the same object):
          if(ActiveIdentityState::Identified == observedObject->GetIdentityState()
             && &observedObject->GetPose().FindOrigin() == currFrame) {
            std::vector<const Vision::KnownMarker *> observedMarkers;
            observedObject->GetObservedMarkers(observedMarkers);
            for(auto marker : observedMarkers) {
              _robot->GetVisionComponent().GetCamera().AddOccluder(*marker);
            }
          }
          
        } // if/else overlapping existing objects found
     
        CORETECH_ASSERT(observedObject != nullptr);
        
        const ObjectID obsID = observedObject->GetID();
        
        // Sanity check: this should not happen, but we're seeing situations where
        // objects think they are being carried when the robot doesn't think it
        // is carrying that object
        // TODO: Eventually, we should be able to remove this check
        ActionableObject* actionObject = dynamic_cast<ActionableObject*>(observedObject);
        if(actionObject != nullptr) {
          if(actionObject->IsBeingCarried() && _robot->GetCarryingObject() != obsID) {
            PRINT_NAMED_WARNING("BlockWorld.AddAndUpdateObject.CarryStateMismatch",
                                "Object %d thinks it is being carried, but does not match "
                                "robot %d's carried object ID (%d). Setting as uncarried.",
                                obsID.GetValue(), _robot->GetID(),
                                _robot->GetCarryingObject().GetValue());
            actionObject->SetBeingCarried(false);
          }
        }
        
        if(obsID.IsUnknown()) {
          PRINT_NAMED_ERROR("BlockWorld.AddAndUpdateObjects.IDnotSet",
                            "ID of new/re-observed object not set.");
          return RESULT_FAIL;
        }


        // Broadcast object's existence as long as it was observed in the current frame.
        if (&observedObject->GetPose().FindOrigin() == currFrame) {
          BroadcastObjectObservation(observedObject, true);
        }
        
        // Update navMemory map
        INavMemoryMap* currentNavMemoryMap = GetNavMemoryMap();
        if ( nullptr != currentNavMemoryMap ) {
          currentNavMemoryMap->AddQuad(observedObject->GetBoundingQuadXY(), INavMemoryMap::EContentType::ObstacleCube);
        }
        
        _didObjectsChange = true;
        _currentObservedObjects.push_back(observedObject);
        
      } // for each object seen
      
      
      // Now process the block(s) that need to be localized to.
      // If there are blocks in other frames then set the pose of the block in the
      // current frame and localize to each of the blocks in the other frames.
      // If there are no blocks in other frames, then localize to the block in the current frame.
      if (potentialObjectsForLocalizingTo.count(currFrame) > 0 && potentialObjectsForLocalizingTo.size() > 1) {
        potentialObjectsForLocalizingTo[currFrame].MergeAndDelete();
        potentialObjectsForLocalizingTo.erase(currFrame);
      }
      
      // Order ObservedAndMatchPairs by distance so that we localize to
      // the closest object last.
      std::map<f32, ObservedAndMatchedPair, std::greater<f32>> objectsToLocalizeToByDist;
      for (auto & obj : potentialObjectsForLocalizingTo) {
        objectsToLocalizeToByDist[obj.second.distance] = obj.second;
      }
      
      for (auto & objPair : objectsToLocalizeToByDist) {
      
        ObservableObject* observedObj = objPair.second.observedObject;
        ObservableObject* matchedObj = objPair.second.matchedObject;

        Result localizeResult = _robot->LocalizeToObject(observedObj, matchedObj);
        if(localizeResult != RESULT_OK) {
          PRINT_NAMED_ERROR("BlockWorld.AddAndUpdateObjects.LocalizeFailure",
                            "Failed to localize to %s object %d.",
                            ObjectTypeToString(matchedObj->GetType()),
                            matchedObj->GetID().GetValue());
          return localizeResult;
        }

        
        delete observedObj;
      }
      
      
      return RESULT_OK;
      
    } // AddAndUpdateObjects()
  
  
    u32 BlockWorld::CheckForUnobservedObjects(TimeStamp_t atTimestamp)
    {
      u32 numVisibleObjects = 0;
      
      if(_robot->IsPickedUp()) {
        // Don't bother if the robot is picked up
        return numVisibleObjects;
      }
      
      // Create a list of unobserved objects for further consideration below.
      struct UnobservedObjectContainer {
        ObjectFamily family;
        ObjectType   type;
        ObservableObject*      object;
        
        UnobservedObjectContainer(ObjectFamily family_, ObjectType type_, ObservableObject* object_)
        : family(family_), type(type_), object(object_) { }
      };
      std::vector<UnobservedObjectContainer> unobservedObjects;
      
      //for(auto & objectTypes : objectsExisting) {
      for(auto & objectFamily : _existingObjects)
      {
        for(auto & objectsByType : objectFamily.second)
        {
          ObjectsMapByID_t& objectIdMap = objectsByType.second;
          for(auto objectIter = objectIdMap.begin();
              objectIter != objectIdMap.end(); )
          {
            ObservableObject* object = objectIter->second;
            
            if(object->GetPoseState() != ObservableObject::PoseState::Unknown &&
               object->GetLastObservedTime() < atTimestamp &&
               &object->GetPose().FindOrigin() == _robot->GetWorldOrigin())
            {
              if(object->GetNumTimesObserved() < MIN_TIMES_TO_OBSERVE_OBJECT) {
                // If this object has only been seen once and that was too long ago,
                // just delete it, but only if this is a non-active object or radio
                // connection has not been established yet
                if (!object->IsActive() || object->GetActiveID() < 0) {
                  PRINT_NAMED_INFO("BlockWorld.CheckForUnobservedObjects",
                                   "Deleting %s object %d that was only observed %d time(s).\n",
                                   ObjectTypeToString(object->GetType()),
                                   object->GetID().GetValue(),
                                   object->GetNumTimesObserved());
                  objectIter = DeleteObject(objectIter, objectsByType.first, objectFamily.first);
                } else {
                  ++objectIter;
                }
              } else if(object->IsActive() &&
                        ActiveIdentityState::WaitingForIdentity == object->GetIdentityState() &&
                        object->GetLastObservedTime() < atTimestamp - BLOCK_IDENTIFICATION_TIMEOUT_MS) {

                // If this is an active object and identification has timed out
                // delete it if radio connection has not been established yet.
                // Otherwise, retry identification.
                if (object->GetActiveID() < 0) {
                  PRINT_NAMED_INFO("BlockWorld.CheckForUnobservedObjects.IdentifyTimedOut",
                                   "Deleting unobserved %s active object %d that has "
                                   "not completed identification in %dms",
                                   EnumToString(object->GetType()),
                                   object->GetID().GetValue(), BLOCK_IDENTIFICATION_TIMEOUT_MS);
                  
                  objectIter = DeleteObject(objectIter, objectsByType.first, objectFamily.first);
                } else {
                  // Don't delete objects that are still in radio communication. Retrigger Identify?
                  //PRINT_NAMED_WARNING("BlockWorld.CheckForUnobservedObjects.RetryIdentify", "Re-attempt identify on object %d (%s)", object->GetID().GetValue(), EnumToString(object->GetType()));
                  //object->Identify();
                  ++objectIter;
                }

              } else {
                // Otherwise, add it to the list for further checks below to see if
                // we "should" have seen the object

                if(_unidentifiedActiveObjects.count(object->GetID()) == 0) {
                  //AddToOcclusionMaps(object, robotMgr_); // TODO: Used to do this too, put it back?
                  unobservedObjects.emplace_back(objectFamily.first, objectsByType.first, objectIter->second);
                }
                ++objectIter;
                
              }
            } else {
              // Object _was_ observed or does not share an origin with the robot,
              // so skip it for analyzing below whether we *should* have seen it
              ++objectIter;
            } // if/else object was not observed
            
          } // for object IDs of this type
        } // for each object type
      } // for each object family
      
      // TODO: Don't bother with this if the robot is docking? (picking/placing)??
      // Now that the occlusion maps are complete, check each unobserved object's
      // visibility in each camera
      const Vision::Camera& camera = _robot->GetVisionComponent().GetCamera();
      ASSERT_NAMED(camera.IsCalibrated(), "BlockWorld.CheckForUnobservedObjects.CameraNotCalibrated");
      for(const auto& unobserved : unobservedObjects) {
        
        // Remove objects that should have been visible based on their last known
        // location, but which must not be there because we saw something behind
        // that location:
        const u16 xBorderPad = static_cast<u16>(0.05*static_cast<f32>(camera.GetCalibration()->GetNcols()));
        const u16 yBorderPad = static_cast<u16>(0.05*static_cast<f32>(camera.GetCalibration()->GetNrows()));
        if(unobserved.object->IsVisibleFrom(camera, DEG_TO_RAD(45), 20.f, true,
                                            xBorderPad, yBorderPad) &&
           (_robot->GetDockObject() != unobserved.object->GetID()))  // We expect a docking block to disappear from view!
        {
          // Make sure there are no currently-observed, (just-)identified objects
          // with the same active ID present. (If there are, we'll reassign IDs
          // on the next update instead of clearing the existing object now.)
          bool matchingActiveIdFound = false;
          if(unobserved.object->IsActive()) {
            for(auto object : _currentObservedObjects) {
              if(ActiveIdentityState::Identified == object->GetIdentityState() &&
                 object->GetActiveID() == unobserved.object->GetActiveID()) {
                matchingActiveIdFound = true;
                break;
              }
            }
          }
          
          if(!matchingActiveIdFound) {
            // We "should" have seen the object! Delete it.
            PRINT_NAMED_INFO("BlockWorld.CheckForUnobservedObjects.RemoveUnobservedObject",
                             "Removing object %d, which should have been seen, "
                             "but wasn't.\n", unobserved.object->GetID().GetValue());
            
            ClearObject(unobserved.object);
          }
        } else if(unobserved.family != ObjectFamily::Mat && _robot->GetCarryingObjects().count(unobserved.object->GetID()) == 0) {
          // If the object should _not_ be visible (i.e. none of its markers project
          // into the camera), but some part of the object is within frame, it is
          // close enough, and was seen fairly recently, then
          // let listeners know it's "visible" but not identifiable, so we can
          // still interact with it in the UI, for example.
          
          // Did we see this currently-unobserved object in the last N seconds?
          // This is to avoid using this feature (reporting unobserved objects
          // that project into the image as observed) too liberally, and instead
          // only for objects seen pretty recently, e.g. for the case that we
          // have driven in too close and can't see an object we were just approaching.
          // TODO: Expose / remove / fine-tune this setting
          const s32 seenWithin_sec = -1; // Set to <0 to disable
          const bool seenRecently = (seenWithin_sec < 0 ||
                                     _robot->GetLastMsgTimestamp() - unobserved.object->GetLastObservedTime() < seenWithin_sec*1000);
          
          // How far away is the object from our current position? Again, to be
          // conservative, we are only going to use this feature if the object is
          // pretty close to the robot.
          // TODO: Expose / remove / fine-tune this setting
          const f32 distThreshold_mm = -1.f; // 150.f; // Set to <0 to disable
          const bool closeEnough = (distThreshold_mm < 0.f ||
                                    (_robot->GetPose().GetTranslation() -
                                    unobserved.object->GetPose().GetTranslation()).LengthSq() < distThreshold_mm*distThreshold_mm);
          
          // Check any of the markers should be visible and that the reason for
          // them not being visible is not occlusion.
          // For now just ignore the left and right 22.5% of the image blocked by the lift,
          // *iff* we are using VGA images, which have a wide enough FOV to be occluded
          // by the lift. (I.e., assume QVGA is a cropped, narrower FOV)
          // TODO: Actually project a lift into the image and figure out what it will occlude
          u16 xBorderPad = 0;
          switch(camera.GetCalibration()->GetNcols())
          {
            case 640:
              xBorderPad = static_cast<u16>(0.225f * static_cast<f32>(camera.GetCalibration()->GetNcols()));
              break;
            case 400:
              // TODO: How much should be occluded?
              xBorderPad = static_cast<u16>(0.20f * static_cast<f32>(camera.GetCalibration()->GetNcols()));
              break;
            case 320:
              // Nothing to do, leave at zero
              break;
            default:
              // Not expecting other resolutions
              PRINT_NAMED_WARNING("BlockWorld.CheckForUnobservedObjects",
                                  "Unexpeted camera calibration ncols=%d.",
                                  camera.GetCalibration()->GetNcols());
          }
          
          Vision::KnownMarker::NotVisibleReason reason;
          bool markersShouldBeVisible = false;
          bool markerIsOccluded = false;
          for(auto & marker : unobserved.object->GetMarkers()) {
            if(marker.IsVisibleFrom(_robot->GetVisionComponent().GetCamera(), DEG_TO_RAD(45), 20.f, false, xBorderPad, 0, reason)) {
              // As soon as one marker is visible, we can stop
              markersShouldBeVisible = true;
              break;
            } else if(reason == Vision::KnownMarker::NotVisibleReason::OCCLUDED) {
              // Flag that any of the markers was not visible because it was occluded
              // If this is the case, then we don't want to signal this object as
              // partially visible.
              markerIsOccluded = true;
            }
            // This should never be true because we set requireSomethingBehind to false in IsVisibleFrom() above.
            assert(reason != Vision::KnownMarker::NotVisibleReason::NOTHING_BEHIND);
          }

          if(seenRecently && closeEnough && !markersShouldBeVisible && !markerIsOccluded)
          {
            // First three checks for object passed, now see if any of the object's
            // corners are in our FOV
            // TODO: Avoid ProjectObject here because it also happens inside BroadcastObjectObservation
            f32 distance;
            std::vector<Point2f> projectedCorners;
            _robot->GetVisionComponent().GetCamera().ProjectObject(*unobserved.object, projectedCorners, distance);
            
            if(distance > 0.f) { // in front of camera?
              for(auto & corner : projectedCorners) {
                
                if(camera.IsWithinFieldOfView(corner)) {
                  
                  BroadcastObjectObservation(unobserved.object, false);
                  ++numVisibleObjects;
                  
                } // if(IsWithinFieldOfView)
              } // for(each projectedCorner)
            } // if(distance > 0)
          }
        }
        
      } // for each unobserved object
      
      return numVisibleObjects;
    } // CheckForUnobservedObjects()
    
    void BlockWorld::GetObsMarkerList(const PoseKeyObsMarkerMap_t& poseKeyObsMarkerMap,
                                      std::list<Vision::ObservedMarker*>& lst)
    {
      lst.clear();
      for(auto & poseKeyMarkerPair : poseKeyObsMarkerMap)
      {
        lst.push_back((Vision::ObservedMarker*)(&(poseKeyMarkerPair.second)));
      }
    }

    void BlockWorld::RemoveUsedMarkers(PoseKeyObsMarkerMap_t& poseKeyObsMarkerMap)
    {
      for(auto poseKeyMarkerPair = poseKeyObsMarkerMap.begin(); poseKeyMarkerPair != poseKeyObsMarkerMap.end();)
      {
        if (poseKeyMarkerPair->second.IsUsed()) {
          poseKeyMarkerPair = poseKeyObsMarkerMap.erase(poseKeyMarkerPair);
        } else {
          ++poseKeyMarkerPair;
        }
      }
    }

    Result BlockWorld::AddMarkerlessObject(const Pose3d& p)
    {
      TimeStamp_t lastTimestamp = _robot->GetLastMsgTimestamp();
  
      // Create an instance of the detected object
      MarkerlessObject *m = new MarkerlessObject(ObjectType::ProxObstacle);
      

      // Raise origin of object above ground.
      // NOTE: Assuming detected obstacle is at ground level no matter what angle the head is at.
      Pose3d raiseObject(0, Z_AXIS_3D(), Vec3f(0,0,0.5f*m->GetSize().z()));
      Pose3d obsPose = p * raiseObject;
      m->SetPose(obsPose);
      m->SetPoseParent(_robot->GetPose().GetParent());
      
      // Check if this prox obstacle already exists
      std::vector<ObservableObject*> existingObjects;
      FindOverlappingObjects(m, _existingObjects[ObjectFamily::MarkerlessObject], existingObjects);
      
      // Update the last observed time of existing overlapping obstacles
      for(auto obj : existingObjects) {
        obj->SetLastObservedTime(lastTimestamp);
      }
      
      // No need to add the obstacle again if it already exists
      if (!existingObjects.empty()) {
        delete m;
        return RESULT_OK;
      }
      
      
      // Check if the obstacle intersects with any other existing objects in the scene.
      BlockWorldFilter filter;
      if(_robot->GetLocalizedTo().IsSet()) {
        // Ignore the mat object that the robot is localized to (?)
        filter.AddIgnoreID(_robot->GetLocalizedTo());
      }
      FindIntersectingObjects(m, existingObjects, 0, filter);
      if (!existingObjects.empty()) {
        delete m;
        return RESULT_OK;
      }

      // HACK: to make it think it was observed enough times so as not to get immediately deleted.
      //       We'll do something better after we figure out how other non-cliff prox obstacles will work.
      for (u8 i=0; i<MIN_TIMES_TO_OBSERVE_OBJECT; ++i) {
        m->SetLastObservedTime(lastTimestamp);
      }

      AddNewObject(m);
      _didObjectsChange = true;
      _currentObservedObjects.push_back(m);
      
      return RESULT_OK;
    }
  
    void BlockWorld::GetObstacles(std::vector<std::pair<Quad2f,ObjectID> >& boundingBoxes, const f32 padding) const
    {
      BlockWorldFilter filter;
      filter.SetIgnoreIDs(std::set<ObjectID>(_robot->GetCarryingObjects()));
      
      // If the robot is localized, check to see if it is "on" the mat it is
      // localized to. If so, ignore the mat as an obstacle.
      // Note that the reason for checking IsPoseOn is that it's possible the
      // robot is localized to a mat it sees but is not on because it has not
      // yet seen the mat it is on. (For example, robot see side of platform
      // and localizes to it because it hasn't seen a marker on the flat mat
      // it is driving on.)
      if(_robot->GetLocalizedTo().IsSet())
      {
        const ObservableObject* object = GetObjectByIDandFamily(_robot->GetLocalizedTo(), ObjectFamily::Mat);
        if(nullptr != object) // If the object localized to exists in the Mat family
        {
          const MatPiece* mat = dynamic_cast<const MatPiece*>(object);
          if(mat != nullptr) {
            if(mat->IsPoseOn(_robot->GetPose(), 0.f, .25*ROBOT_BOUNDING_Z)) {
              // Ignore the ID of the mat we're on
              filter.AddIgnoreID(_robot->GetLocalizedTo());
              
              // Add any "unsafe" regions this mat has
              mat->GetUnsafeRegions(boundingBoxes, padding);
            }
          } else {
            PRINT_NAMED_WARNING("BlockWorld.GetObstacles.DynamicCastFail",
                                "Could not dynamic cast localization object %d to a Mat",
                                _robot->GetLocalizedTo().GetValue());
          }
        }
      }
      
      // Figure out height filters in world coordinates (because GetObjectBoundingBoxesXY()
      // uses heights of objects in world coordinates)
      const Pose3d robotPoseWrtOrigin = _robot->GetPose().GetWithRespectToOrigin();
      const f32 minHeight = robotPoseWrtOrigin.GetTranslation().z();
      const f32 maxHeight = minHeight + _robot->GetHeight();
      
      GetObjectBoundingBoxesXY(minHeight, maxHeight, padding, boundingBoxes, filter);
      
    } // GetObstacles()

    void BlockWorld::FindMatchingObjects(const BlockWorldFilter& filter, std::vector<ObservableObject*>& result) const
    {
      // slight abuse of the FindObjectHelper, I just use it for filtering, then I add everything that passes
      // the filter to the result vector
      FindFcn findLambda = [&result](ObservableObject* candidateObject, ObservableObject* best) {
        result.push_back(candidateObject);
        return false;
      };

      // ignore return value, since the findLambda stored everything in result
      FindObjectHelper(findLambda, filter, false);
    }

    void BlockWorld::GetObjectBoundingBoxesXY(const f32 minHeight,
                                              const f32 maxHeight,
                                              const f32 padding,
                                              std::vector<std::pair<Quad2f,ObjectID> >& rectangles,
                                              const BlockWorldFilter& filter) const
    {
      for(auto & objectsByFamily : _existingObjects)
      {
        if(filter.ConsiderFamily(objectsByFamily.first)) {
          for(auto & objectsByType : objectsByFamily.second)
          {
            if(filter.ConsiderType(objectsByType.first)) {
              for(auto & objectAndId : objectsByType.second)
              {
                if(filter.ConsiderObject(objectAndId.second))
                {
                  ObservableObject* object = objectAndId.second;
                  if(object == nullptr) {
                    PRINT_NAMED_WARNING("BlockWorld.GetObjectBoundingBoxesXY.NullObjectPointer",
                                        "ObjectID %d corresponds to NULL ObservableObject pointer.",
                                        objectAndId.first.GetValue());
                  } else if(object->GetNumTimesObserved() >= MIN_TIMES_TO_OBSERVE_OBJECT
                            && !object->IsPoseStateUnknown()) {
                    const f32 objectHeight = objectAndId.second->GetPose().GetWithRespectToOrigin().GetTranslation().z();
                    if( (objectHeight >= minHeight) && (objectHeight <= maxHeight) )
                    {
                      rectangles.emplace_back(objectAndId.second->GetBoundingQuadXY(padding), objectAndId.first);
                    }
                  }
                } // if useID
              } // for each ID
            } // if(useType)
          } // for each type
        } // if useFamily
      } // for each family
      
    } // GetObjectBoundingBoxesXY()
    
    
    bool BlockWorld::DidObjectsChange() const {
      return _didObjectsChange;
    }

    
    bool BlockWorld::UpdateRobotPose(PoseKeyObsMarkerMap_t& obsMarkersAtTimestamp, const TimeStamp_t atTimestamp)
    {
      bool wasPoseUpdated = false;
      
      // Extract only observed markers from obsMarkersAtTimestamp
      std::list<Vision::ObservedMarker*> obsMarkersListAtTimestamp;
      GetObsMarkerList(obsMarkersAtTimestamp, obsMarkersListAtTimestamp);
      
      // Get all mat objects *seen by this robot's camera*
      std::multimap<f32, ObservableObject*> matsSeen;
      _objectLibrary[ObjectFamily::Mat].CreateObjectsFromMarkers(obsMarkersListAtTimestamp, matsSeen,
                                                                  (_robot->GetVisionComponent().GetCamera().GetID()));

      // Remove used markers from map container
      RemoveUsedMarkers(obsMarkersAtTimestamp);
      
      if(not matsSeen.empty()) {
        /*
        // TODO: False mat marker localization causes the mat to be created in weird places which is messing up game dev.
        //       Seems to happen particular when looking at the number 1. Disabled for now.
        PRINT_NAMED_WARNING("UpdateRobotPose.TempIgnore", "Ignoring mat marker. Robot localization disabled.");
        return false;
        */
        
        // Is the robot "on" any of the mats it sees?
        // TODO: What to do if robot is "on" more than one mat simultaneously?
        MatPiece* onMat = nullptr;
        for(const auto& objectPair : matsSeen) {
          Vision::ObservableObject* object = objectPair.second;
          
          // ObservedObjects are w.r.t. the arbitrary historical origin of the camera
          // that observed them.  Hook them up to the current robot origin now:
          CORETECH_ASSERT(object->GetPose().GetParent() != nullptr &&
                          object->GetPose().GetParent()->IsOrigin());
          object->SetPoseParent(_robot->GetWorldOrigin());
          
          MatPiece* mat = dynamic_cast<MatPiece*>(object);
          CORETECH_ASSERT(mat != nullptr);
          
          // Does this mat pose make sense? I.e., is the top surface flat enough
          // that we could drive on it?
          Vec3f rotAxis;
          Radians rotAngle;
          mat->GetPose().GetRotationVector().GetAngleAndAxis(rotAngle, rotAxis);
          if(std::abs(rotAngle.ToFloat()) > DEG_TO_RAD(5) &&                // There's any rotation to speak of
             !AreUnitVectorsAligned(rotAxis, Z_AXIS_3D(), DEG_TO_RAD(45)))  // That rotation's axis more than 45 degrees from vertical
          {
            PRINT_NAMED_INFO("BlockWorld.UpdateRobotPose",
                             "Refusing to localize to %s mat with rotation %.1f degrees around (%.1f,%.1f,%.1f) axis.",
                             ObjectTypeToString(mat->GetType()),
                             rotAngle.getDegrees(),
                             rotAxis.x(), rotAxis.y(), rotAxis.z());
          }else if(mat->IsPoseOn(_robot->GetPose(), 0, 15.f)) { // TODO: get heightTol from robot
            if(onMat != nullptr) {
              PRINT_NAMED_WARNING("BlockWorld.UpdateRobotPose.OnMultiplMats",
                                  "Robot is 'on' multiple mats at the same time. Will just use the first for now.");
            } else {
              onMat = mat;
            }
          }
        }
        
        // This will point to the mat we decide to localize to below (or will
        // remain null if we choose not to localize to any mat we see)
        MatPiece* matToLocalizeTo = nullptr;
        
        if(onMat != nullptr)
        {
          
          PRINT_LOCALIZATION_INFO("BlockWorld.UpdateRobotPose.OnMatLocalization",
                                  "Robot %d is on a %s mat and will localize to it.",
                                  _robot->GetID(), onMat->GetType().GetName().c_str());
          
          // If robot is "on" one of the mats it is currently seeing, localize
          // the robot to that mat
          matToLocalizeTo = onMat;
        }
        else {
          // If the robot is NOT "on" any of the mats it is seeing...
          
          if(_robot->GetLocalizedTo().IsSet()) {
            // ... and the robot is already localized, then see if it is
            // localized to one of the mats it is seeing (but not "on")
            // Note that we must match seen and existing objects by their pose
            // here, and not by ID, because "seen" objects have not ID assigned
            // yet.

            ObservableObject* existingMatLocalizedTo = GetObjectByID(_robot->GetLocalizedTo());
            if(existingMatLocalizedTo == nullptr) {
              PRINT_NAMED_ERROR("BlockWorld.UpdateRobotPose.ExistingMatLocalizedToNull",
                                "Robot %d is localized to mat with ID=%d, but that mat does not exist in the world.",
                                _robot->GetID(), _robot->GetLocalizedTo().GetValue());
              return false;
            }
            
            std::vector<ObservableObject*> overlappingMatsSeen;
            FindOverlappingObjects(existingMatLocalizedTo, matsSeen, overlappingMatsSeen);
            
            if(overlappingMatsSeen.empty()) {
              // The robot is localized to a mat it is not seeing (and is not "on"
              // any of the mats it _is_ seeing.  Just update the poses of the
              // mats it is seeing, but don't localize to any of them.
              PRINT_LOCALIZATION_INFO("BlockWorld.UpdateRobotPose.NotOnMatNoLocalize",
                                      "Robot %d is localized to a mat it doesn't see, and will not localize to any of the %lu mats it sees but is not on.",
                                      _robot->GetID(), (unsigned long)matsSeen.size());
            }
            else {
              if(overlappingMatsSeen.size() > 1) {
                PRINT_STREAM_WARNING("BlockWorld.UpdateRobotPose.MultipleOverlappingMats",
                                    "Robot " << _robot->GetID() << " is seeing " << overlappingMatsSeen.size() << " (i.e. more than one) mats "
                                    "overlapping with the existing mat it is localized to. "
                                    "Will use first.");
              }
              
              PRINT_LOCALIZATION_INFO("BlockWorld.UpdateRobotPose.NotOnMatLocalization",
                                      "Robot %d will re-localize to the %s mat it is not on, but already localized to.",
                                      _robot->GetID(), overlappingMatsSeen[0]->GetType().GetName().c_str());
              
              // The robot is localized to one of the mats it is seeing, even
              // though it is not _on_ that mat.  Remain localized to that mat
              // and update any others it is also seeing
              matToLocalizeTo = dynamic_cast<MatPiece*>(overlappingMatsSeen[0]);
              CORETECH_ASSERT(matToLocalizeTo != nullptr);
            }
            
            
          } else {
            // ... and the robot is _not_ localized, choose the observed mat
            // with the closest observed marker (since that is likely to be the
            // most accurate) and localize to that one.
            f32 minDistSq = -1.f;
            MatPiece* closestMat = nullptr;
            for(const auto& matPair : matsSeen) {
              Vision::ObservableObject* mat = matPair.second;
              
              std::vector<const Vision::KnownMarker*> observedMarkers;
              mat->GetObservedMarkers(observedMarkers, atTimestamp);
              if(observedMarkers.empty()) {
                PRINT_NAMED_ERROR("BlockWorld.UpdateRobotPose.ObservedMatWithNoObservedMarkers",
                                  "We saw a mat piece but it is returning no observed markers for "
                                  "the current timestamp.");
                CORETECH_ASSERT(false); // TODO: handle this situation
              }
              
              Pose3d markerWrtRobot;
              for(auto obsMarker : observedMarkers) {
                if(obsMarker->GetPose().GetWithRespectTo(_robot->GetPose(), markerWrtRobot) == false) {
                  PRINT_NAMED_ERROR("BlockWorld.UpdateRobotPose.ObsMarkerPoseOriginMisMatch",
                                    "Could not get the pose of an observed marker w.r.t. the robot that "
                                    "supposedly observed it.");
                  CORETECH_ASSERT(false); // TODO: handle this situation
                }
                
                const f32 markerDistSq = markerWrtRobot.GetTranslation().LengthSq();
                if(closestMat == nullptr || markerDistSq < minDistSq) {
                  closestMat = dynamic_cast<MatPiece*>(mat);
                  CORETECH_ASSERT(closestMat != nullptr);
                  minDistSq = markerDistSq;
                }
              } // for each observed marker
            } // for each mat seen
            
            PRINT_LOCALIZATION_INFO("BLockWorld.UpdateRobotPose.NotOnMatLocalizationToClosest",
                                    "Robot %d is not on a mat but will localize to %s mat ID=%d, which is the closest.",
                                    _robot->GetID(), closestMat->GetType().GetName().c_str(), closestMat->GetID().GetValue());
            
            matToLocalizeTo = closestMat;
            
          } // if/else robot is localized
        } // if/else (onMat != nullptr)
        
        ObjectsMapByType_t& existingMatPieces = _existingObjects[ObjectFamily::Mat];
        
        // Keep track of markers we saw on existing/instantiated mats, to use
        // for occlusion checking
        std::vector<const Vision::KnownMarker *> observedMarkers;

        MatPiece* existingMatPiece = nullptr;
        
        // If we found a suitable mat to localize to, and we've seen it enough
        // times, then use it for localizing
        if(matToLocalizeTo != nullptr) {
          
          if(existingMatPieces.empty()) {
            // If this is the first mat piece, add it to the world using the world
            // origin as its pose
            PRINT_STREAM_INFO("BlockWorld.UpdateRobotPose.CreatingFirstMatPiece",
                       "Instantiating first mat piece in the world.");
            
            existingMatPiece = dynamic_cast<MatPiece*>(matToLocalizeTo->CloneType());
            assert(existingMatPiece != nullptr);
            AddNewObject(existingMatPieces, existingMatPiece);
            
            existingMatPiece->SetPose( Pose3d() ); // Not really necessary, but ensures the ID makes it into the pose name, which is helpful for debugging
            assert(existingMatPiece->GetPose().GetParent() == nullptr);
            
          }
          else {
            // We can't look up the existing piece by ID because the matToLocalizeTo
            // is just a mat we _saw_, not one we've instantiated.  So look for
            // one in approximately the same position, of those with the same
            // type:
            //ObservableObject* existingObject = GetObjectByID(matToLocalizeTo->GetID());
            std::vector<ObservableObject*> existingObjects;
            FindOverlappingObjects(matToLocalizeTo, _existingObjects[ObjectFamily::Mat], existingObjects);
          
            if(existingObjects.empty())
            {
              // If the mat we are about to localize to does not exist yet,
              // but it's not the first mat piece in the world, add it to the
              // world, and give it a new origin, relative to the current
              // world origin.
              Pose3d poseWrtWorldOrigin = matToLocalizeTo->GetPose().GetWithRespectToOrigin();
              
              existingMatPiece = dynamic_cast<MatPiece*>(matToLocalizeTo->CloneType());
              assert(existingMatPiece != nullptr);
              AddNewObject(existingMatPieces, existingMatPiece);
              existingMatPiece->SetPose(poseWrtWorldOrigin); // Do after AddNewObject, once ID is set
              
              PRINT_STREAM_INFO("BlockWorld.UpdateRobotPose.LocalizingToNewMat",
                         "Robot " << _robot->GetID() << " localizing to new "
                                << ObjectTypeToString(existingMatPiece->GetType()) << " mat with ID=" << existingMatPiece->GetID().GetValue() << ".");
              
            } else {
              if(existingObjects.size() > 1) {
                PRINT_NAMED_WARNING("BlockWorld.UpdateRobotPose.MultipleExistingObjectMatches",
                              "Robot %d found multiple existing mats matching the one it "
                              "will localize to - using first.", _robot->GetID());
              }
              
              // We are localizing to an existing mat piece: do not attempt to
              // update its pose (we can't both update the mat's pose and use it
              // to update the robot's pose at the same time!)
              existingMatPiece = dynamic_cast<MatPiece*>(existingObjects.front());
              CORETECH_ASSERT(existingMatPiece != nullptr);
              
              PRINT_LOCALIZATION_INFO("BlockWorld.UpdateRobotPose.LocalizingToExistingMat",
                                      "Robot %d localizing to existing %s mat with ID=%d.",
                                      _robot->GetID(), existingMatPiece->GetType().GetName().c_str(),
                                      existingMatPiece->GetID().GetValue());
            }
          } // if/else (existingMatPieces.empty())
          
          existingMatPiece->SetLastObservedTime(matToLocalizeTo->GetLastObservedTime());
          existingMatPiece->UpdateMarkerObservationTimes(*matToLocalizeTo);
          existingMatPiece->GetObservedMarkers(observedMarkers, atTimestamp);
          
          if(existingMatPiece->GetNumTimesObserved() >= MIN_TIMES_TO_OBSERVE_OBJECT) {
            // Now localize to that mat
            //wasPoseUpdated = LocalizeRobotToMat(robot, matToLocalizeTo, existingMatPiece);
            if(_robot->LocalizeToMat(matToLocalizeTo, existingMatPiece) == RESULT_OK) {
              wasPoseUpdated = true;
            }
          }
          
        } // if(matToLocalizeTo != nullptr)
        
        // Update poses of any other mats we saw (but did not localize to),
        // just like they are any "regular" object, unless that mat is the
        // robot's current "world" origin, [TODO:] in which case we will update the pose
        // of the mat we are on w.r.t. that world.
        for(const auto& matSeenPair : matsSeen) {
          ObservableObject* matSeen = matSeenPair.second;
          
          if(matSeen != matToLocalizeTo) {
            
            // TODO: Make this w.r.t. whatever the robot is currently localized to?
            Pose3d poseWrtOrigin = matSeen->GetPose().GetWithRespectToOrigin();
            
            // Does this mat pose make sense? I.e., is the top surface flat enough
            // that we could drive on it?
            Vec3f rotAxis;
            Radians rotAngle;
            poseWrtOrigin.GetRotationVector().GetAngleAndAxis(rotAngle, rotAxis);
            if(std::abs(rotAngle.ToFloat()) > DEG_TO_RAD(5) &&                // There's any rotation to speak of
               !AreUnitVectorsAligned(rotAxis, Z_AXIS_3D(), DEG_TO_RAD(45)))  // That rotation's axis more than 45 degrees from vertical
            {
              PRINT_NAMED_INFO("BlockWorld.UpdateRobotPose",
                               "Ignoring observation of %s mat with rotation %.1f degrees around (%.1f,%.1f,%.1f) axis.",
                               ObjectTypeToString(matSeen->GetType()),
                               rotAngle.getDegrees(),
                               rotAxis.x(), rotAxis.y(), rotAxis.z());
              continue;
            }
            
            // Store pointers to any existing objects that overlap with this one
            std::vector<ObservableObject*> overlappingObjects;
            FindOverlappingObjects(matSeen, existingMatPieces, overlappingObjects);
            
            if(overlappingObjects.empty()) {
              // no existing mats overlapped with the mat we saw, so add it
              // as a new mat piece, relative to the world origin
              ObservableObject* newMatPiece = matSeen->CloneType();
              AddNewObject(existingMatPieces, newMatPiece);
              newMatPiece->SetPose(poseWrtOrigin); // do after AddNewObject, once ID is set
              
              // TODO: Make clone copy the observation times
              newMatPiece->SetLastObservedTime(matSeen->GetLastObservedTime());
              newMatPiece->UpdateMarkerObservationTimes(*matSeen);
              
              PRINT_NAMED_INFO("BlockWorld.UpdateRobotPose",
                               "Adding new %s mat with ID=%d at (%.1f, %.1f, %.1f)",
                               ObjectTypeToString(newMatPiece->GetType()),
                               newMatPiece->GetID().GetValue(),
                               newMatPiece->GetPose().GetTranslation().x(),
                               newMatPiece->GetPose().GetTranslation().y(),
                               newMatPiece->GetPose().GetTranslation().z());
              
              // Add observed mat markers to the occlusion map of the camera that saw
              // them, so we can use them to delete objects that should have been
              // seen between that marker and the robot
              newMatPiece->GetObservedMarkers(observedMarkers, atTimestamp);

            }
            else {
              if(overlappingObjects.size() > 1) {
                PRINT_LOCALIZATION_INFO("BlockWorld.UpdateRobotPose",
                                        "More than one overlapping mat found -- will use first.");
                // TODO: do something smarter here?
              }
              
              if(&(overlappingObjects[0]->GetPose()) != _robot->GetWorldOrigin()) {
                // The overlapping mat object is NOT the world origin mat, whose
                // pose we don't want to update.
                // Update existing observed mat we saw but are not on w.r.t.
                // the robot's current world origin
                
                // TODO: better way of merging existing/observed object pose
                overlappingObjects[0]->SetPose( poseWrtOrigin );
                
              } else {
                /* PUNT - not sure this is workign, nor we want to bother with this for now...
                CORETECH_ASSERT(robot->IsLocalized());
                
                // Find the mat the robot is currently localized to
                MatPiece* localizedToMat = nullptr;
                for(auto & objectsByType : existingMatPieces) {
                  auto objectsByIdIter = objectsByType.second.find(robot->GetLocalizedTo());
                  if(objectsByIdIter != objectsByType.second.end()) {
                    localizedToMat = dynamic_cast<MatPiece*>(objectsByIdIter->second);
                  }
                }

                CORETECH_ASSERT(localizedToMat != nullptr);
                
                // Update the mat we are localized to (but may not have seen) w.r.t. the existing
                // observed world origin mat we did see from it.  This should in turn
                // update the pose of everything on that mat.
                Pose3d newPose;
                if(localizedToMat->GetPose().GetWithRespectTo(matSeen->GetPose(), newPose) == false) {
                  PRINT_NAMED_ERROR("BlockWorld.UpdateRobotPose.FailedToUpdateWrtObservedOrigin",
                                    "Robot %d failed to get pose of existing %s mat it is on w.r.t. observed world origin mat.",
                                    robot->GetID(), existingMatPiece->GetType().GetName().c_str());
                }
                newPose.SetParent(robot->GetWorldOrigin());
                // TODO: Switch new pose to be w.r.t. whatever robot is localized to??
                localizedToMat->SetPose(newPose);
                 */
              }
              
              overlappingObjects[0]->SetLastObservedTime(matSeen->GetLastObservedTime());
              overlappingObjects[0]->UpdateMarkerObservationTimes(*matSeen);
              overlappingObjects[0]->GetObservedMarkers(observedMarkers, atTimestamp);
              
            } // if/else overlapping existing mats found
          } // if matSeen != matToLocalizeTo
          
          delete matSeen;
        }
        
        // Add observed mat markers to the occlusion map of the camera that saw
        // them, so we can use them to delete objects that should have been
        // seen between that marker and the robot
        for(auto obsMarker : observedMarkers) {
          /*
          PRINT_NAMED_INFO("BlockWorld.UpdateRobotPose.AddingMatMarkerOccluder",
                           "Adding mat marker '%s' as an occluder for robot %d.",
                           Vision::MarkerTypeStrings[obsMarker->GetCode()],
                           robot->GetID());
           */
          _robot->GetVisionComponent().GetCamera().AddOccluder(*obsMarker);
        }
        
        /* Always re-drawing everything now
        // If the robot just re-localized, trigger a draw of all objects, since
        // we may have seen things while de-localized whose locations can now be
        // snapped into place.
        if(!wasLocalized && robot->IsLocalized()) {
          PRINT_NAMED_INFO("BlockWorld.UpdateRobotPose.RobotRelocalized",
                           "Robot %d just localized after being de-localized.", robot->GetID());
          DrawAllObjects();
        }
        */
      } // IF any mat piece was seen

      if(wasPoseUpdated) {
        PRINT_LOCALIZATION_INFO("BlockWorld.UpdateRobotPose.RobotPoseChain", "%s",
                                _robot->GetPose().GetNamedPathToOrigin(true).c_str());
      }
      
      return wasPoseUpdated;
      
    } // UpdateRobotPose()
    
    Result BlockWorld::UpdateObjectPoses(PoseKeyObsMarkerMap_t& obsMarkersAtTimestamp,
                                         const ObjectFamily& inFamily,
                                         const TimeStamp_t atTimestamp)
    {
      const ObservableObjectLibrary& objectLibrary = _objectLibrary[inFamily];
      
      // Keep the objects sorted by increasing distance from the robot.
      // This will allow us to only use the closest object that can provide
      // localization information (if any) to update the robot's pose.
      // Note that we use a multimap to handle the corner case that there are two
      // objects that have the exact same distance. (We don't want to only report
      // seeing one of them and it doesn't matter which we use to localize.)
      std::multimap<f32, ObservableObject*> objectsSeen;
      
      // Don't bother with this update at all if we didn't see at least one
      // marker (which is our indication we got an update from the robot's
      // vision system
      if(not _obsMarkers.empty()) {
        
        // Extract only observed markers from obsMarkersAtTimestamp
        std::list<Vision::ObservedMarker*> obsMarkersListAtTimestamp;
        GetObsMarkerList(obsMarkersAtTimestamp, obsMarkersListAtTimestamp);
        
        objectLibrary.CreateObjectsFromMarkers(obsMarkersListAtTimestamp, objectsSeen);
        
        // Remove used markers from map
        RemoveUsedMarkers(obsMarkersAtTimestamp);
      
        for(const auto& objectPair : objectsSeen) {
          Vision::ObservableObject* object = objectPair.second;
          
          // ObservedObjects are w.r.t. the arbitrary historical origin of the camera
          // that observed them.  Hook them up to the current robot origin now:
          CORETECH_ASSERT(object->GetPose().GetParent() != nullptr &&
                          object->GetPose().GetParent()->IsOrigin());
          object->SetPoseParent(_robot->GetWorldOrigin());

          // update navmesh with a quadrilateral between the robot and the seen object
          INavMemoryMap* currentNavMemoryMap = GetNavMemoryMap();
          if ( nullptr != currentNavMemoryMap )
          {
            // robot corners
            const Quad2f& robotQuad = _robot->GetBoundingQuadXY();
            Point3f cornerBR{ robotQuad[Quad::TopLeft   ].x(), robotQuad[Quad::TopLeft ].y(), 0};
            Point3f cornerBL{ robotQuad[Quad::BottomLeft].x(), robotQuad[Quad::BottomLeft].y(), 0};
          
            std::vector<const Vision::KnownMarker *> observedMarkers;
            object->GetObservedMarkers(observedMarkers);
            for ( const auto& observedMarkerIt : observedMarkers )
            {
              // marker corners
              const Quad3f& markerCorners = observedMarkerIt->Get3dCorners(observedMarkerIt->GetPose().GetWithRespectToOrigin());
              Point3f cornerTL = markerCorners[Quad::BottomLeft];
              Point3f cornerTR = markerCorners[Quad::BottomRight];
              
              // Create a quad between the bottom corners of a marker and the robot forward corners, and tell
              // the navmesh that it should be clear, since we saw the marker
              Quad2f clearVisionQuad { cornerTL, cornerBL, cornerTR, cornerBR };
              currentNavMemoryMap->AddQuad(clearVisionQuad, INavMemoryMap::EContentType::ClearOfObstacle);
              
              // also notify behavior whiteboard.
              // rsam: should this information be in the map instead of the whiteboard? It seems a stretch that
              // blockworld knows now about behaviors, maybe all this processing of quads should be done in a separate
              // robot component, like a VisualInformationProcessingComponent
              _robot->GetBehaviorManager().GetWhiteboard().ProcessClearQuad(clearVisionQuad);
            }
          }
          
        }
        
        // Use them to add or update existing blocks in our world
        Result lastResult = AddAndUpdateObjects(objectsSeen, inFamily, atTimestamp);
        if(lastResult != RESULT_OK) {
          PRINT_NAMED_ERROR("BlockWorld.UpdateObjectPoses.AddAndUpdateFailed", "");
          return lastResult;
        }
      }
      
      return RESULT_OK;
      
    } // UpdateObjectPoses()

    /*
    Result BlockWorld::UpdateProxObstaclePoses()
    {
      TimeStamp_t lastTimestamp = _robot->GetLastMsgTimestamp();
      
      // Add prox obstacle if detected and one doesn't already exist
      for (ProxSensor_t sensor = (ProxSensor_t)(0); sensor < NUM_PROX; sensor = (ProxSensor_t)(sensor + 1)) {
        if (!_robot->IsProxSensorBlocked(sensor) && _robot->GetProxSensorVal(sensor) >= PROX_OBSTACLE_DETECT_THRESH) {
          
          // Create an instance of the detected object
          MarkerlessObject *m = new MarkerlessObject(ObjectType::ProxObstacle);
          
          // Get pose of detected object relative to robot according to which sensor it was detected by.
          Pose3d proxTransform = Robot::ProxDetectTransform[sensor];
          
          // Raise origin of object above ground.
          // NOTE: Assuming detected obstacle is at ground level no matter what angle the head is at.
          Pose3d raiseObject(0, Z_AXIS_3D(), Vec3f(0,0,0.5f*m->GetSize().z()));
          proxTransform = proxTransform * raiseObject;
          
          proxTransform.SetParent(_robot->GetPose().GetParent());
          
          // Compute pose of detected object
          Pose3d obsPose(_robot->GetPose());
          obsPose = obsPose * proxTransform;
          m->SetPose(obsPose);
          m->SetPoseParent(_robot->GetPose().GetParent());
          
          // Check if this prox obstacle already exists
          std::vector<ObservableObject*> existingObjects;
          FindOverlappingObjects(m, _existingObjects[ObjectFamily::MarkerlessObject], existingObjects);
          
          // Update the last observed time of existing overlapping obstacles
          for(auto obj : existingObjects) {
            obj->SetLastObservedTime(lastTimestamp);
          }
          
          // No need to add the obstacle again if it already exists
          if (!existingObjects.empty()) {
            delete m;
            return RESULT_OK;
          }
          
          
          // Check if the obstacle intersects with any other existing objects in the scene.
          std::set<ObjectFamily> ignoreFamilies;
          std::set<ObjectType> ignoreTypes;
          std::set<ObjectID> ignoreIDs;
          if(_robot->IsLocalized()) {
            // Ignore the mat object that the robot is localized to (?)
            ignoreIDs.insert(_robot->GetLocalizedTo());
          }
          FindIntersectingObjects(m, existingObjects, 0, ignoreFamilies, ignoreTypes, ignoreIDs);
          if (!existingObjects.empty()) {
            delete m;
            return RESULT_OK;
          }

          
          m->SetLastObservedTime(lastTimestamp);
          AddNewObject(ObjectFamily::MarkerlessObject, m);
          _didObjectsChange = true;
        }
      } // end for all prox sensors
      
      // Delete any existing prox objects that are too old.
      // Note that we use find() here because there may not be any markerless objects
      // yet, and using [] indexing will create things.
      auto markerlessFamily = _existingObjects.find(ObjectFamily::MarkerlessObject);
      if(markerlessFamily != _existingObjects.end())
      {
        auto proxTypeMap = markerlessFamily->second.find(ObjectType::ProxObstacle);
        if(proxTypeMap != markerlessFamily->second.end())
        {
          for (auto proxObsIter = proxTypeMap->second.begin();
               proxObsIter != proxTypeMap->second.end();
                   */
/* increment iter in loop, depending on erase*/    /*
)
          {
            if (lastTimestamp - proxObsIter->second->GetLastObservedTime() > PROX_OBSTACLE_LIFETIME_MS)
            {
              proxObsIter = ClearObject(proxObsIter, ObjectType::ProxObstacle,
                                        ObjectFamily::MarkerlessObject);
              
            } else {
              // Didn't erase anything, increment iterator
              ++proxObsIter;
            }
          }
        }
      }
      
      return RESULT_OK;
    }
    */

  
    ObjectID BlockWorld::AddActiveObject(ActiveID activeID, FactoryID factoryID, ActiveObjectType activeObjectType)
    {
      if (activeID >= 4 || activeID < 0) {
        PRINT_NAMED_WARNING("BlockWorld.AddActiveObject.InvalidActiveID", "activeID %d", activeID);
        return ObjectID();
      }
      
      // Is there an active object with the same activeID that already exists?
      ObjectType objType = ActiveObject::GetTypeFromActiveObjectType(activeObjectType);
      const char* objTypeStr = EnumToString(objType);
      ActiveObject* matchingObject = GetActiveObjectByActiveID(activeID);
      if (matchingObject == nullptr) {
        // If no match found, find one of the same type with an invalid activeID and assume it's that
        const ObjectsMapByID_t& objectsOfSameType = GetExistingObjectsByType(objType);
        for (auto& objIt : objectsOfSameType) {
          ObservableObject* sameTypeObject = objIt.second;
          if (sameTypeObject->GetActiveID() < 0) {
            sameTypeObject->SetActiveID(activeID);
            PRINT_NAMED_INFO("BlockWorld.AddActiveObject.FoundMatchingObjectWithNoActiveID",
                             "objectID %d, activeID %d, type %s",
                             sameTypeObject->GetID().GetValue(), sameTypeObject->GetActiveID(), objTypeStr);
            return sameTypeObject->GetID();
          } else {
            // If found an existing object of the same type but not same factoryID then ignore it
            // until we figure out how to deal with multiple objects of same type.
            if ( sameTypeObject->GetFactoryID() != factoryID ) {
              PRINT_NAMED_WARNING("BlockWorld.AddActiveObject.FoundOtherActiveObjectOfSameType",
                                  "ActiveID %d (factoryID 0x%x) is same type as another existing object (objectID %d, activeID %d, factoryID 0x%x, type %s). Multiple objects of same type not supported!",
                                  activeID, factoryID,
                                  sameTypeObject->GetID().GetValue(), sameTypeObject->GetActiveID(), sameTypeObject->GetFactoryID(), objTypeStr);
              return ObjectID();
            } else {
              PRINT_NAMED_INFO("BlockWorld.AddActiveObject.FoundIdenticalObjectOnDifferentSlot",
                               "Updating activeID of block with factoryID 0x%x from %d to %d",
                               sameTypeObject->GetFactoryID(), sameTypeObject->GetActiveID(), activeID);
              sameTypeObject->SetActiveID(activeID);
              return sameTypeObject->GetID();
            }
          }
        }
      } else {
        // A match was found but does it have the same factory ID?
        if(matchingObject->GetFactoryID() == factoryID) {
          PRINT_NAMED_INFO("BlockWorld.AddActiveObject.FoundMatchingActiveObject",
                           "objectID %d, activeID %d, type %s, factoryID 0x%x",
                           matchingObject->GetID().GetValue(), matchingObject->GetActiveID(), objTypeStr, matchingObject->GetFactoryID());
          return matchingObject->GetID();
        } else if (matchingObject->GetFactoryID() == 0) {
          // Existing object was only previously observed, never connected, so its factoryID is 0
          PRINT_NAMED_INFO("BlockWorld.AddActiveObject.FoundMatchingActiveObjectThatWasNeverConnected",
                           "objectID %d, activeID %d, type %s, factoryID 0x%x",
                           matchingObject->GetID().GetValue(), matchingObject->GetActiveID(), objTypeStr, matchingObject->GetFactoryID());
          return matchingObject->GetID();
        } else {
          // FactoryID mismatch. Delete the current object and fall through to add a new one
          PRINT_NAMED_WARNING("BlockWorld.AddActiveObject.MismatchedFactoryID",
                              "objectID %d, activeID %d, type %s, factoryID 0x%x (expected 0x%x)",
                              matchingObject->GetID().GetValue(), matchingObject->GetActiveID(), objTypeStr, factoryID, matchingObject->GetFactoryID());
          DeleteObject(matchingObject->GetID());
        }
      }
  
      // An existing object with activeID was not found so add it
      ObservableObject* newObject = nullptr;
      switch(objType) {
        case ObjectType::Block_LIGHTCUBE1:
        case ObjectType::Block_LIGHTCUBE2:
        case ObjectType::Block_LIGHTCUBE3:
        {
          newObject = new ActiveCube(activeID, factoryID, activeObjectType);
          break;
        }
        case ObjectType::Charger_Basic:
        {
          newObject = new Charger(activeID, factoryID, activeObjectType);
          break;
        }
        default:
          PRINT_NAMED_WARNING("BlockWorld.AddActiveObject.UnsupportedActiveObjectType", "%s (ActiveObjectType: 0x%hx)", objTypeStr, activeObjectType);
          return ObjectID();
      }
      
      newObject->SetPoseParent(_robot->GetWorldOrigin());
      newObject->SetPoseState(ObservableObject::PoseState::Unknown);
      AddNewObject(newObject);
      PRINT_NAMED_INFO("BlockWorld.AddActiveObject.AddedNewObject",
                       "objectID %d, type %s, activeID %d, factoryID 0x%x",
                       newObject->GetID().GetValue(), objTypeStr, newObject->GetActiveID(), newObject->GetFactoryID());
      return newObject->GetID();
      


    }
  
    Result BlockWorld::AddCliff(const Pose3d& p)
    {
      // at the moment we treat them as markerless objects
      const Result ret = AddMarkerlessObject(p);
      return ret;
    }
  
    Result BlockWorld::AddProxObstacle(const Pose3d& p)
    {
      // add markerless object
      const Result ret = AddMarkerlessObject(p);
      return ret;
    }

    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    Result BlockWorld::ProcessVisionOverheadEdges(const OverheadEdgeFrame& frameInfo)
    {
      Result ret = RESULT_OK;
      if ( frameInfo.groundPlaneValid ) {
        if ( !frameInfo.chains.empty() ) {
          ret = AddVisionOverheadEdges(frameInfo);
        } else {
          // we expect lack of borders to be reported as !isBorder chains
          ASSERT_NAMED(false, "ProcessVisionOverheadEdges.ValidPlaneWithNoChains");
        }
      } else {
        // ground plane was invalid (atm we don't use this). It's probably only useful if we are debug-rendering
        // the ground plane
        _robot->GetContext()->GetVizManager()->EraseSegments("BlockWorld.AddVisionOverheadEdges");
      }
      return ret;
    }
  
    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    namespace {

      // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
      inline Point3f EdgePointToPoint3f(const OverheadEdgePoint& point, const Pose3d& pose, float z=0.0f) {
        Point3f ret = pose * Point3f(point.position.x(), point.position.y(), z);
        return ret;
      }
      
    } // anonymous namespace

    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    Result BlockWorld::AddVisionOverheadEdges(const OverheadEdgeFrame& frameInfo)
    {
      _robot->GetContext()->GetVizManager()->EraseSegments("BlockWorld.AddVisionOverheadEdges");
      
      // check conditions to add edges
      ASSERT_NAMED(!frameInfo.chains.empty(), "AddVisionOverheadEdges.NoEdges");
      ASSERT_NAMED(frameInfo.groundPlaneValid, "AddVisionOverheadEdges.InvalidGroundPlane");
      
      // we are only processing edges for the memory map, so if there's no map, don't do anything
      INavMemoryMap* currentNavMemoryMap = GetNavMemoryMap();
      if( nullptr == currentNavMemoryMap && !kDebugRenderOverheadEdges )
      {
        return RESULT_OK;
      }

      // grab the robot pose at the timestamp of this frame
      TimeStamp_t t;
      RobotPoseStamp* p = nullptr;
      HistPoseKey poseKey;
      const Result poseRet = _robot->GetPoseHistory()->ComputeAndInsertPoseAt(frameInfo.timestamp, t, &p, &poseKey, true);
      const bool poseIsGood = ( RESULT_OK == poseRet ) && (p != nullptr);
      if ( !poseIsGood ) {
        PRINT_NAMED_ERROR("BlockWorld.AddVisionOverheadEdges.PoseNotGood", "Pose not good for timestamp %d", frameInfo.timestamp);
        return RESULT_FAIL;
      }
      const Pose3d& observedPose = p->GetPose();
      const Point3f& cameraOrigin = observedPose.GetTranslation();
      
      // Ideally we would do clamping with quad in robot coordinates, but this is an optimization to prevent
      // having to transform segments twice. We transform the segments to world space so that we can
      // calculate variations in angles, in order to merge together small variations. Once we have transformed
      // the segments, we can clamp the merged segments. We could do this in 2D, but we would have to transform
      // those segments again into world space. As a minor optimization, transform ground-plane's near-plane instead
      Point2f nearPlaneLeft  = observedPose *
        Point3f(frameInfo.groundplane[Quad::BottomLeft].x(),
                frameInfo.groundplane[Quad::BottomLeft].y(),
                0.0f);
      Point2f nearPlaneRight = observedPose *
        Point3f(frameInfo.groundplane[Quad::BottomRight].x(),
                frameInfo.groundplane[Quad::BottomRight].y(),
                0.0f);

      const float kBorderDepth = 1.0f;
      
      // TODO reserve some quads in each vector (what makes sense?)
      std::vector<Quad2f> visionQuadsClear;
      std::vector<Quad2f> visionQuadsWithBorders;
      for( const auto& chain : frameInfo.chains )
      {

        // debug render
        if ( kDebugRenderOverheadEdges )
        {
//          VizManager* vizManager = _robot->GetContext()->GetVizManager();
//          vizManager->DrawQuadAsSegments("BlockWorld.AddVisionOverheadEdges", frameInfo.groundplane, 3.0f, NamedColors::CYAN, false);
          
          // renders every segment reported by vision
          for (size_t i=0; i<chain.points.size()-1; ++i)
          {
            const float z = 4.0f;
            Point3f start = EdgePointToPoint3f(chain.points[i], observedPose, z);
            Point3f end   = EdgePointToPoint3f(chain.points[i+1], observedPose, z);
            ColorRGBA color = ((i%2) == 0) ? NamedColors::YELLOW : NamedColors::ORANGE;
            VizManager* vizManager = _robot->GetContext()->GetVizManager();
            vizManager->DrawSegment("BlockWorld.AddVisionOverheadEdges", start, end, color, false);
          }
        }

        ASSERT_NAMED(chain.points.size() > 2,"AddVisionOverheadEdges.ChainWithTooLittlePoints");
        
        // iterate the chain merging points together
        Point3f segmentStart = EdgePointToPoint3f(chain.points[0], observedPose);
        Point3f segmentEnd   = EdgePointToPoint3f(chain.points[1], observedPose);
        Vec3f segmentNormal  = (segmentEnd-segmentStart);
        segmentNormal.MakeUnitLength();
        size_t curIdx = 2;
        bool doneWithChain = false;
        do
        {
          // epsilon to merge points into the same edge segment. If adding a point to a segment creates
          // a deviation with respect to the first direction of the segment bigger than this epsilon,
          // then the point will not be added to that segment
          // cos(10deg) = 0.984807...
          // cos(20deg) = 0.939692...
          // cos(30deg) = 0.866025...
          // cos(40deg) = 0.766044...
          const float kDotBorderEpsilon = 0.7660f;
        
          // get candidate point to merge into previous segment
          Point3f candidateEnd = EdgePointToPoint3f(chain.points[curIdx], observedPose);
          Vec3f candidateNormal = candidateEnd - segmentEnd;
          candidateNormal.MakeUnitLength();
          
          // check if can be merged
          const float dotProduct = DotProduct(segmentNormal, candidateNormal);
          bool canMergePoint = (dotProduct >= kDotBorderEpsilon); // if dotProduct is bigger, angle between is smaller
          if ( canMergePoint )
          {
            // it can, advance to next
            segmentEnd = candidateEnd;
            segmentNormal = candidateNormal;
          }
          else
          {
            // can't merge the point, add current segment and restart
            Quad2f clearQuad = { segmentStart, cameraOrigin, segmentEnd, cameraOrigin }; // TL, BL, TR, BR
            bool success = GroundPlaneROI::ClampQuad(clearQuad, nearPlaneLeft, nearPlaneRight);
            ASSERT_NAMED(success, "AddVisionOverheadEdges.FailedQuadClamp");
            if ( success ) {
              visionQuadsClear.emplace_back(clearQuad);
            }
            if ( chain.isBorder ) {
              Vec3f segStartDepthDir = (segmentStart - cameraOrigin);
              segStartDepthDir.MakeUnitLength();
              Vec3f segEndDepthDir = (segmentEnd - cameraOrigin);
              segEndDepthDir.MakeUnitLength();
              // TL, BL, TR, BR
              Quad2f borderQuad = { segmentStart + segStartDepthDir*kBorderDepth, segmentStart,
                                    segmentEnd   + segEndDepthDir*kBorderDepth,   segmentEnd };
              visionQuadsWithBorders.emplace_back(borderQuad);
            }
            
            // restart
            segmentStart = segmentEnd; // segmentEnd becomes the new start
            segmentEnd = candidateEnd; // candidateEnd becomes the new end
            segmentNormal = segmentEnd-segmentStart;
            segmentNormal.MakeUnitLength();
          }
          
          // we are done if this point is the last one
          doneWithChain = (curIdx == chain.points.size()-1);
          if ( doneWithChain )
          {
            Quad2f clearQuad = { segmentStart, cameraOrigin, segmentEnd, cameraOrigin }; // TL, BL, TR, BR
            bool success = GroundPlaneROI::ClampQuad(clearQuad, nearPlaneLeft, nearPlaneRight);
            ASSERT_NAMED(success, "AddVisionOverheadEdges.FailedQuadClamp");
            if ( success ) {
              visionQuadsClear.emplace_back(clearQuad);
            }
            if ( chain.isBorder ) {
              Vec3f segStartDepthDir = (segmentStart - cameraOrigin);
              segStartDepthDir.MakeUnitLength();
              Vec3f segEndDepthDir = (segmentEnd - cameraOrigin);
              segEndDepthDir.MakeUnitLength();
              // TL, BL, TR, BR
              Quad2f borderQuad = { segmentStart + segStartDepthDir*kBorderDepth, segmentStart,
                                    segmentEnd   + segEndDepthDir*kBorderDepth,   segmentEnd };
              visionQuadsWithBorders.emplace_back(borderQuad);
            }
          }
          else
          {
            // not done, move to next point
            ++curIdx;
          }
          
        } while (!doneWithChain);
      }
      
      // send quads to memory map
      for ( const auto& clearQuad2D : visionQuadsClear )
      {
        if ( kDebugRenderOverheadEdges )
        {
          ColorRGBA color = Anki::NamedColors::GREEN;
          const float z = 2.0f;
          VizManager* vizManager = _robot->GetContext()->GetVizManager();
          vizManager->DrawQuadAsSegments("BlockWorld.AddVisionOverheadEdges", clearQuad2D, z, color, false);
        }

        // add clear info to map
        if ( currentNavMemoryMap ) {
          currentNavMemoryMap->AddQuad(clearQuad2D, INavMemoryMap::EContentType::ClearOfObstacle);
        }
        
        // also notify behavior whiteboard.
        // rsam: should this information be in the map instead of the whiteboard? It seems a stretch that
        // blockworld knows now about behaviors, maybe all this processing of quads should be done in a separate
        // robot component, like a VisualInformationProcessingComponent
        _robot->GetBehaviorManager().GetWhiteboard().ProcessClearQuad(clearQuad2D);
      }
      
      // send quads to memory map
      for ( const auto& borderQuad2D : visionQuadsWithBorders )
      {
        if ( kDebugRenderOverheadEdges )
        {
          ColorRGBA color = Anki::NamedColors::BLUE;
          const float z = 2.0f;
          VizManager* vizManager = _robot->GetContext()->GetVizManager();
          vizManager->DrawQuadAsSegments("BlockWorld.AddVisionOverheadEdges", borderQuad2D, z, color, false);
        }
      
        // add interesting edge
        if ( currentNavMemoryMap ) {
          currentNavMemoryMap->AddQuad(borderQuad2D, INavMemoryMap::EContentType::InterestingEdge);
        }
      }
      
      return RESULT_OK;
    }
  
    void BlockWorld::RemoveMarkersWithinMarkers(PoseKeyObsMarkerMap_t& currentObsMarkers)
    {
      for(auto markerIter1 = currentObsMarkers.begin(); markerIter1 != currentObsMarkers.end(); ++markerIter1)
      {
        const Vision::ObservedMarker& marker1    = markerIter1->second;
        const TimeStamp_t             timestamp1 = markerIter1->first;
        
        for(auto markerIter2 = currentObsMarkers.begin(); markerIter2 != currentObsMarkers.end(); /* incrementing decided in loop */ )
        {
          const Vision::ObservedMarker& marker2    = markerIter2->second;
          const TimeStamp_t             timestamp2 = markerIter2->first;
          
          // These two markers must be different and observed at the same time
          if(markerIter1 != markerIter2 && timestamp1 == timestamp2) {
            
            // See if #2 is inside #1
            bool marker2isInsideMarker1 = true;
            for(auto & corner : marker2.GetImageCorners()) {
              if(marker1.GetImageCorners().Contains(corner) == false) {
                marker2isInsideMarker1 = false;
                break;
              }
            }
            
            if(marker2isInsideMarker1) {
              PRINT_NAMED_INFO("BlockWorld.Update",
                               "Removing %s marker completely contained within %s marker.\n",
                               marker2.GetCodeName(), marker1.GetCodeName());
              // Note: erase does increment of iterator for us
              markerIter2 = currentObsMarkers.erase(markerIter2);
            } else {
              // Need to iterate marker2
              ++markerIter2;
            } // if/else marker2isInsideMarker1
          } else {
            // Need to iterate marker2
            ++markerIter2;
          } // if/else marker1 != marker2 && time1 != time2
        } // for markerIter2
      } // for markerIter1
      
    } // RemoveMarkersWithinMarkers()
  
  
    Result BlockWorld::Update()
    {
      // New timestep, new set of occluders.  Get rid of anything registered as
      // an occluder with the robot's camera
      _robot->GetVisionComponent().GetCamera().ClearOccluders();
      
      // New timestep, clear list of observed object bounding boxes
      //_obsProjectedObjects.clear();
      //_currentObservedObjectIDs.clear();
      
      static TimeStamp_t lastObsMarkerTime = 0;
      
      _currentObservedObjects.clear();
      
      // Now we're going to process all the observed messages, grouped by
      // timestamp
      size_t numUnusedMarkers = 0;
      for(auto obsMarkerListMapIter = _obsMarkers.begin();
          obsMarkerListMapIter != _obsMarkers.end();
          ++obsMarkerListMapIter)
      {
        PoseKeyObsMarkerMap_t& currentObsMarkers = obsMarkerListMapIter->second;
        const TimeStamp_t atTimestamp = obsMarkerListMapIter->first;
        
        lastObsMarkerTime = std::max(lastObsMarkerTime, atTimestamp);
        
        //
        // Localize robots using mat observations
        //
        
        // Remove observed markers whose historical poses have become invalid.
        // This shouldn't happen! If it does, robotStateMsgs may be buffering up somewhere.
        // Increasing history time window would fix this, but it's not really a solution.
        for(auto poseKeyMarkerPair = currentObsMarkers.begin(); poseKeyMarkerPair != currentObsMarkers.end();) {
          if ((poseKeyMarkerPair->second.GetSeenBy().GetID() == _robot->GetVisionComponent().GetCamera().GetID()) &&
              !_robot->IsValidPoseKey(poseKeyMarkerPair->first)) {
            PRINT_NAMED_WARNING("BlockWorld.Update.InvalidHistPoseKey", "key=%d", poseKeyMarkerPair->first);
            poseKeyMarkerPair = currentObsMarkers.erase(poseKeyMarkerPair);
          } else {
            ++poseKeyMarkerPair;
          }
        }
        
        // Optional: don't allow markers seen enclosed in other markers
        //RemoveMarkersWithinMarkers(currentObsMarkers);
        
        // Only update robot's poses using VisionMarkers while not on a ramp
        if(!_robot->IsOnRamp()) {
          if (!_robot->IsPhysical() || !SKIP_PHYS_ROBOT_LOCALIZATION) {
            // Note that this removes markers from the list that it uses
            UpdateRobotPose(currentObsMarkers, atTimestamp);
          }
        }
        
        // Reset the flag telling us objects changed here, before we update any objects:
        _didObjectsChange = false;

        Result updateResult;

        //
        // Find any observed active blocks from the remaining markers.
        // Do these first because they can update our localization, meaning that
        // other objects found below will be more accurately localized.
        //
        // Note that this removes markers from the list that it uses
        updateResult = UpdateObjectPoses(currentObsMarkers, ObjectFamily::LightCube, atTimestamp);
        if(updateResult != RESULT_OK) {
          return updateResult;
        }
        
        //
        // Find any observed blocks from the remaining markers
        //
        // Note that this removes markers from the list that it uses
        updateResult = UpdateObjectPoses(currentObsMarkers, ObjectFamily::Block, atTimestamp);
        if(updateResult != RESULT_OK) {
          return updateResult;
        }
        
        //
        // Find any observed ramps from the remaining markers
        //
        // Note that this removes markers from the list that it uses
        updateResult = UpdateObjectPoses(currentObsMarkers, ObjectFamily::Ramp, atTimestamp);
        if(updateResult != RESULT_OK) {
          return updateResult;
        }
        
        //
        // Find any observed chargers from the remaining markers
        //
        // Note that this removes markers from the list that it uses
        updateResult = UpdateObjectPoses(currentObsMarkers, ObjectFamily::Charger, atTimestamp);
        if(updateResult != RESULT_OK) {
          return updateResult;
        }
        

        // TODO: Deal with unknown markers?
        
        // Keep track of how many markers went unused by either robot or block
        // pose updating processes above
        numUnusedMarkers += currentObsMarkers.size();
        
        for(auto & unusedMarker : currentObsMarkers) {
          PRINT_NAMED_INFO("BlockWorld.Update.UnusedMarker",
                           "An observed %s marker went unused.",
                           unusedMarker.second.GetCodeName());
        }
        
        // Delete any objects that should have been observed but weren't,
        // visualize objects that were observed:
        CheckForUnobservedObjects(atTimestamp);
        
      } // for element in _obsMarkers
      
      if(_obsMarkers.empty()) {
        // Even if there were no markers observed, check to see if there are
        // any previously-observed objects that are partially visible (some part
        // of them projects into the image even if none of their markers fully do)
        CheckForUnobservedObjects(_robot->GetLastImageTimeStamp());
      }
      
      if(_currentObservedObjects.empty()) {
        // If we didn't see/update anything, send a signal saying so
        _robot->Broadcast(ExternalInterface::MessageEngineToGame(ExternalInterface::RobotObservedNothing(_robot->GetID())));
      } 
      
      //PRINT_NAMED_INFO("BlockWorld.Update.NumBlocksObserved", "Saw %d blocks", numBlocksObserved);
      
      // Check for unobserved, uncarried objects that overlap with any robot's position
      // TODO: better way of specifying which objects are obstacles and which are not
      // TODO: Move this giant loop to its own method
      for(auto & objectsByFamily : _existingObjects)
      {
        // For now, look for collision with anything other than Mat objects
        // NOTE: This assumes all other objects are DockableObjects below!!! (Becuase of IsBeingCarried() check)
        // TODO: How can we delete Mat objects (like platforms) whose positions we drive through
        if(objectsByFamily.first != ObjectFamily::Mat &&
           objectsByFamily.first != ObjectFamily::MarkerlessObject)
        {
          for(auto & objectsByType : objectsByFamily.second)
          {
            for(auto & objectIdPair : objectsByType.second)
            {
              ActionableObject* object = dynamic_cast<ActionableObject*>(objectIdPair.second);
              if(object == nullptr) {
                PRINT_NAMED_ERROR("BlockWorld.Update.ExpectingActionableObject",
                                  "In robot/object collision check, can currently only "
                                  "handle ActionableObjects.");
                continue;
              }
              
              if(object->GetLastObservedTime() < _robot->GetLastImageTimeStamp() &&
                 !object->IsBeingCarried() &&
                 !object->IsPoseStateUnknown() &&
                 object->GetID() != _robot->GetDockObject() &&
                 !object->CanIntersectWithRobot())
              {
                // Don't worry about collision while picking or placing since we
                // are trying to get close to blocks in these modes.
                // TODO: specify whether we are picking/placing _this_ block
                if(!_robot->IsPickingOrPlacing())
                {
                  // Check block's bounding box in same coordinates as this robot to
                  // see if it intersects with the robot's bounding box. Also check to see
                  // block and the robot are at overlapping heights.  Skip this check
                  // entirely if the block isn't in the same coordinate tree as the
                  // robot.
                  Pose3d objectPoseWrtRobotOrigin;
                  if(object->GetPose().GetWithRespectTo(*_robot->GetWorldOrigin(), objectPoseWrtRobotOrigin) == true)
                  {
                    const Quad2f objectBBox = object->GetBoundingQuadXY(objectPoseWrtRobotOrigin);
                    const f32    objectHeight = objectPoseWrtRobotOrigin.GetTranslation().z();
                    /*
                     const f32    blockSize   = 0.5f*object->GetSize().Length();
                     const f32    blockTop    = objectHeight + blockSize;
                     const f32    blockBottom = objectHeight - blockSize;
                     */
                    const f32 robotBottom = _robot->GetPose().GetTranslation().z();
                    const f32 robotTop    = robotBottom + ROBOT_BOUNDING_Z;
                    
                    // TODO: Better check for being in the same plane that takes the
                    //       vertical extent of the object (in its current pose) into account
                    const bool inSamePlane = (objectHeight >= robotBottom && objectHeight <= robotTop);
                    /*
                     const bool topIntersects    = (((blockTop >= robotBottom) && (blockTop <= robotTop)) ||
                     ((robotTop >= blockBottom) && (robotTop <= blockTop)));
                     
                     const bool bottomIntersects = (((blockBottom >= robotBottom) && (blockBottom <= robotTop)) ||
                     ((robotBottom >= blockBottom) && (robotBottom <= blockTop)));
                     */
                    
                    const Quad2f robotBBox = _robot->GetBoundingQuadXY(_robot->GetPose().GetWithRespectToOrigin(),
                                                                       ROBOT_BBOX_PADDING_FOR_OBJECT_DELETION);
                    
                    const bool bboxIntersects = robotBBox.Intersects(objectBBox);
                    
                    if( inSamePlane && bboxIntersects )
                    {
                      PRINT_NAMED_INFO("BlockWorld.Update",
                                       "Removing object %d, which intersects robot %d's bounding quad.",
                                       object->GetID().GetValue(), _robot->GetID());
                      
                      // Erase the vizualized block and its projected quad
                      //V_robot->GetContext()->GetVizManager()->EraseCuboid(object->GetID());

                      // Clear object and everything on top of it, indicating we don't know where it went
                      ObservableObject* objectOnTop = object;
                      BOUNDED_WHILE(20, objectOnTop != nullptr) {
                        ClearObject(objectOnTop);
                        objectOnTop = FindObjectOnTopOf(*objectOnTop, STACKED_HEIGHT_TOL_MM);
                      }
                    } // if quads intersect
                  } // if we got block pose wrt robot origin
                } // if robot is not picking or placing

              } // if block was not observed
              
            } // for each object of this type
          } // for each object type
        } // if not in the Mat family
      } // for each object family
      
      if(numUnusedMarkers > 0) {
        if (!_robot->IsPhysical() || !SKIP_PHYS_ROBOT_LOCALIZATION) {
          PRINT_NAMED_WARNING("BlockWorld.Update.UnusedMarkers",
                              "%zu observed markers did not match any known objects and went unused.",
                              numUnusedMarkers);
        }
      }
     
      // Toss any remaining markers?
      ClearAllObservedMarkers();      
      
      /*
      Result lastResult = UpdateProxObstaclePoses();
      if(lastResult != RESULT_OK) {
        return lastResult;
      }
      */

      return RESULT_OK;
      
    } // Update()
    
    
    Result BlockWorld::QueueObservedMarker(HistPoseKey& poseKey, Vision::ObservedMarker& marker)
    {
      Result lastResult = RESULT_OK;
      
      // Finally actually queue the marker
      _obsMarkers[marker.GetTimeStamp()].emplace(poseKey, marker);
            
      
      return lastResult;
      
    } // QueueObservedMarker()
    
    void BlockWorld::ClearAllObservedMarkers()
    {
      _obsMarkers.clear();
    }
    
    void BlockWorld::ClearAllExistingObjects()
    {
      if(_canDeleteObjects) {
        for(auto & objectsByFamily : _existingObjects) {
          for(const auto& objectsByType : objectsByFamily.second) {
            for(const auto& objectsByID : objectsByType.second) {
              ClearObjectHelper(objectsByID.second);
            }
          }
        }
      }  else {
        PRINT_NAMED_WARNING("BlockWorld.ClearAllExistingObjects.DeleteDisabled",
                            "Will not clear all objects because object deletion is disabled.");
      }
    }
    
    void BlockWorld::ClearObjectHelper(ObservableObject* object)
    {
      if(object == nullptr) {
        PRINT_NAMED_WARNING("BlockWorld.ClearObjectHelper.NullObjectPointer",
                            "BlockWorld asked to clear a null object pointer.");
      } else {
        // Check to see if this object is the one the robot is localized to.
        // If so, the robot needs to be marked as localized to nothing.
        if(_robot->GetLocalizedTo() == object->GetID()) {
          PRINT_NAMED_INFO("BlockWorld.ClearObjectHelper.LocalizeRobotToNothing",
                           "Setting robot %d as localized to no object, because it "
                           "is currently localized to %s object with ID=%d, which is "
                           "about to be cleared.",
                           _robot->GetID(), ObjectTypeToString(object->GetType()), object->GetID().GetValue());
          _robot->SetLocalizedTo(nullptr);
        }
        
        // TODO: If this is a mat piece, check to see if there are any objects "on" it (COZMO-138)
        // If so, clear them too or update their poses somehow? (Deleting seems easier)
        
        // Check to see if this object is the one the robot is carrying.
        if(_robot->GetCarryingObject() == object->GetID()) {
          PRINT_NAMED_INFO("BlockWorld.ClearObjectHelper.ClearingCarriedObject",
                           "Clearing %s object %d which robot %d thinks it is carrying.",
                           ObjectTypeToString(object->GetType()),
                           object->GetID().GetValue(),
                           _robot->GetID());
          _robot->UnSetCarryingObjects();
        }
        
        if(_selectedObject == object->GetID()) {
          PRINT_NAMED_INFO("BlockWorld.ClearObjectHelper.ClearingSelectedObject",
                           "Clearing %s object %d which is currently selected.",
                           ObjectTypeToString(object->GetType()),
                           object->GetID().GetValue());
          _selectedObject.UnSet();
        }


        // Setting pose to unknown makes the object no longer "existence confirmed", so save value now
        bool wasExistenceConfirmed = object->IsExistenceConfirmed();
        
        object->SetPoseState(ObservableObject::PoseState::Unknown);
        
        ObservableObject* objectOnTop = FindObjectOnTopOf(*object, STACKED_HEIGHT_TOL_MM);
        if(objectOnTop != nullptr)
        {
          ClearObject(objectOnTop);
        }
        
        // Notify any listeners that this object no longer has a valid Pose
        // (Only notify for objects that were broadcast in the first place, meaning
        //  they must have been seen the minimum number of times and not be in the
        //  process of being identified)
        if(wasExistenceConfirmed)
        {
          using namespace ExternalInterface;
          _robot->Broadcast(MessageEngineToGame(RobotMarkedObjectPoseUnknown(
            _robot->GetID(), object->GetID().GetValue()
          )));
        }
        
        // Flag that we removed an object
        _didObjectsChange = true;
      }
    }
  
  ObservableObject* BlockWorld::FindObjectHelper(FindFcn findFcn, const BlockWorldFilter& filter, bool returnFirstFound) const
  {
    ObservableObject* matchingObject = nullptr;
    
    if(filter.IsOnlyConsideringLatestUpdate()) {
      
      for(auto candidate : _currentObservedObjects) {
        if(filter.ConsiderFamily(candidate->GetFamily()) &&
           filter.ConsiderType(candidate->GetType()) &&
           filter.ConsiderObject(candidate))
        {
          if(findFcn(candidate, matchingObject)) {
            matchingObject = candidate;
            if(returnFirstFound) {
              return matchingObject;
            }
          }
        }
      }
      
    } else {
      for(auto & objectsByFamily : _existingObjects) {
        if(filter.ConsiderFamily(objectsByFamily.first)) {
          for(auto & objectsByType : objectsByFamily.second) {
            if(filter.ConsiderType(objectsByType.first)) {
              for(auto & objectsByID : objectsByType.second) {
                if(filter.ConsiderObject(objectsByID.second))
                {
                  if(findFcn(objectsByID.second, matchingObject)) {
                    matchingObject = objectsByID.second;
                    if(returnFirstFound) {
                      return matchingObject;
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
    
    return matchingObject;
  }
  
    ObservableObject* BlockWorld::FindObjectOnTopOf(const ObservableObject& objectOnBottom,
                                                    f32 zTolerance,
                                                    const BlockWorldFilter& filterIn) const
    {
      Point3f sameDistTol(objectOnBottom.GetSize());
      sameDistTol.x() *= 0.5f;  // An object should only be considered to be on top if it's midpoint is actually on top of the bottom object's top surface.
      sameDistTol.y() *= 0.5f;
      sameDistTol.z() = zTolerance;
      sameDistTol = objectOnBottom.GetPose().GetRotation() * sameDistTol;
      sameDistTol.Abs();
      
      // Find the point at the top middle of the object on bottom
      Point3f rotatedBtmSize(objectOnBottom.GetPose().GetRotation() * objectOnBottom.GetSize());
      Point3f topOfObjectOnBottom(objectOnBottom.GetPose().GetTranslation());
      topOfObjectOnBottom.z() += 0.5f*std::abs(rotatedBtmSize.z());
      
      BlockWorldFilter filter(filterIn);
      filter.AddIgnoreID(objectOnBottom.GetID());
      
      FindFcn findLambda = [&topOfObjectOnBottom, &sameDistTol](ObservableObject* candidateObject, ObservableObject* best)
      {
        // Find the point at bottom middle of the object we're checking to be on top
        Point3f rotatedTopSize(candidateObject->GetPose().GetRotation() * candidateObject->GetSize());
        Point3f bottomOfCandidateObject(candidateObject->GetPose().GetTranslation());
        bottomOfCandidateObject.z() -= 0.5f*std::abs(rotatedTopSize.z());
        
        // If the top of the bottom object and the bottom the candidate top object are
        // close enough together, return this as the object on top
        Point3f dist(topOfObjectOnBottom);
        dist -= bottomOfCandidateObject;
        dist.Abs();
        
        if(dist < sameDistTol) {
          return true;
        } else {
          return false;
        }
      };
      
      return FindObjectHelper(findLambda, filter, true);
    }
  
  
    ObservableObject* BlockWorld::FindObjectUnderneath(const ObservableObject& objectOnTop,
                                                       f32 zTolerance,
                                                       const BlockWorldFilter& filterIn) const
    {
      Point3f sameDistTol(objectOnTop.GetSize());
      sameDistTol.x() *= 0.5f;  // An object should only be considered to be on top if it's midpoint is actually on top of the bottom object's top surface.
      sameDistTol.y() *= 0.5f;
      sameDistTol.z() = zTolerance;
      sameDistTol = objectOnTop.GetPose().GetRotation() * sameDistTol;
      sameDistTol.Abs();
      
      // Find the point at the top middle of the object on bottom
      Point3f rotatedBtmSize(objectOnTop.GetPose().GetRotation() * objectOnTop.GetSize());
      Point3f bottomOfObjectOnTop(objectOnTop.GetPose().GetTranslation());
      bottomOfObjectOnTop.z() -= 0.5f*std::abs(rotatedBtmSize.z());
      
      BlockWorldFilter filter(filterIn);
      filter.AddIgnoreID(objectOnTop.GetID());
      
      FindFcn findLambda = [&bottomOfObjectOnTop, &sameDistTol](ObservableObject* candidateObject, ObservableObject* best)
      {
        // Find the point at top middle of the object we're checking to be underneath
        Point3f rotatedBtmSize(candidateObject->GetPose().GetRotation() * candidateObject->GetSize());
        Point3f topOfCandidateObject(candidateObject->GetPose().GetTranslation());
        topOfCandidateObject.z() += 0.5f*std::abs(rotatedBtmSize.z());
        
        // If the top of the bottom object and the bottom the candidate top object are
        // close enough together, return this as the object on top
        Point3f dist(bottomOfObjectOnTop);
        dist -= topOfCandidateObject;
        dist.Abs();
        
        if(dist < sameDistTol) {
          return true;
        } else {
          return false;
        }
      };
      
      return FindObjectHelper(findLambda, filter, true);
    }

    ObservableObject* BlockWorld::FindObjectClosestTo(const Pose3d& pose,
                                                      const BlockWorldFilter& filter) const
    {
      return FindObjectClosestTo(pose, Vec3f{FLT_MAX}, filter);
    }


    ObservableObject* BlockWorld::FindObjectClosestTo(const Pose3d& pose,
                                                      const Vec3f&  distThreshold,
                                                      const BlockWorldFilter& filter) const
    {
      // TODO: Keep some kind of OctTree data structure to make these queries faster?
      
      Vec3f closestDist(distThreshold);
      //ObservableObject* matchingObject = nullptr;
      
      FindFcn findLambda = [&pose, &closestDist](ObservableObject* current, ObservableObject* best)
      {
        Vec3f dist = ComputeVectorBetween(pose, current->GetPose());
        dist.Abs();
        if(dist.Length() < closestDist.Length()) {
          closestDist = dist;
          return true;
        } else {
          return false;
        }
      };
      
      return FindObjectHelper(findLambda, filter);
    }
  
    bool BlockWorld::AnyRemainingLocalizableObjects() const
    {
      // There's no real find: we're relying entirely on the filter function here
      FindFcn findLambda = [](ObservableObject*, ObservableObject*) {
        return true;
      };
      
      // Filter out anything that can't be used for localization
      BlockWorldFilter::FilterFcn filterLambda = [](ObservableObject* obj) {
        return obj->CanBeUsedForLocalization();
      };
      
      BlockWorldFilter filter;
      filter.SetFilterFcn(filterLambda);
      filter.SetIgnoreFamilies({
        ObjectFamily::Block,
        ObjectFamily::Charger,
        ObjectFamily::MarkerlessObject,
        ObjectFamily::Ramp,
      });
      
      if(nullptr != FindObjectHelper(findLambda, filter, true)) {
        return true;
      } else {
        return false;
      }
    }
  
    ObservableObject* BlockWorld::FindMostRecentlyObservedObject(const BlockWorldFilter& filter) const
    {
      FindFcn findLambda = [](ObservableObject* current, ObservableObject* best)
      {
        if(best == nullptr || current->GetLastObservedTime() > best->GetLastObservedTime()) {
          return true;
        } else {
          return false;
        }
      };
      
      return FindObjectHelper(findLambda, filter);
    }
  
    ObservableObject* BlockWorld::FindClosestMatchingObject(const ObservableObject& object,
                                                            const Vec3f& distThreshold,
                                                            const Radians& angleThreshold,
                                                            const BlockWorldFilter& filterIn)
    {
      Vec3f closestDist(distThreshold);
      Radians closestAngle(angleThreshold);
      
      // Don't check the object we're using as the comparison
      BlockWorldFilter filter(filterIn);
      filter.AddIgnoreID(object.GetID());
      
      FindFcn findLambda = [&object,&closestDist,&closestAngle](ObservableObject* current, ObservableObject* best)
      {
        Vec3f Tdiff;
        Radians angleDiff;
        if(current->IsSameAs(object, closestDist, closestAngle, Tdiff, angleDiff)) {
          closestDist = Tdiff.GetAbs();
          closestAngle = angleDiff.getAbsoluteVal();
          return true;
        } else {
          return false;
        }
      };
      
      ObservableObject* closestObject = FindObjectHelper(findLambda, filter);
      return closestObject;
    } // FindClosestMatchingObject(given object)
  
    ObservableObject* BlockWorld::FindClosestMatchingObject(ObjectType withType,
                                                            const Pose3d& pose,
                                                            const Vec3f& distThreshold,
                                                            const Radians& angleThreshold,
                                                            const BlockWorldFilter& filter)
    {
      Vec3f closestDist(distThreshold);
      Radians closestAngle(angleThreshold);
      
      FindFcn findLambda = [withType,&pose,&closestDist,&closestAngle](ObservableObject* current, ObservableObject* best)
      {
        Vec3f Tdiff;
        Radians angleDiff;
        if(current->GetType() == withType &&
           current->GetPose().IsSameAs(pose, closestDist, closestAngle, Tdiff, angleDiff))
        {
          closestDist = Tdiff.GetAbs();
          closestAngle = angleDiff.getAbsoluteVal();
          return true;
        } else {
          return false;
        }
      };
      
      ObservableObject* closestObject = FindObjectHelper(findLambda, filter);
      return closestObject;
    } // FindClosestMatchingObject(given pose)
  
    void BlockWorld::ClearObjectsByFamily(const ObjectFamily family)
    {
      if(_canDeleteObjects) {
        ObjectsMapByFamily_t::iterator objectsWithFamily = _existingObjects.find(family);
        if(objectsWithFamily != _existingObjects.end()) {
          for(auto & objectsByType : objectsWithFamily->second) {
            for(auto & objectsByID : objectsByType.second) {
              ClearObjectHelper(objectsByID.second);
            }
          }
        }
      } else {
        PRINT_NAMED_WARNING("BlockWorld.ClearObjectsByFamily.ClearDisabled",
                            "Will not clear family %d objects because object deletion is disabled.",
                            family);
      }
    }
    
    void BlockWorld::ClearObjectsByType(const ObjectType type)
    {
      if(_canDeleteObjects) {
        for(auto & objectsByFamily : _existingObjects) {
          ObjectsMapByType_t::iterator objectsWithType = objectsByFamily.second.find(type);
          if(objectsWithType != objectsByFamily.second.end()) {
            for(auto & objectsByID : objectsWithType->second) {
              ClearObjectHelper(objectsByID.second);
            }
            
            // Types are unique.  No need to keep looking
            return;
          }
        }
      } else {
        PRINT_NAMED_WARNING("BlockWorld.ClearObjectsByType.DeleteDisabled",
                            "Will not clear %s objects because object deletion is disabled.",
                            ObjectTypeToString(type));

      }
    } // ClearBlocksByType()

  
    void BlockWorld::DeleteObjectsByFamily(const ObjectFamily family)
    {
      if(_canDeleteObjects) {
        ObjectsMapByFamily_t::iterator objectsWithFamily = _existingObjects.find(family);
        if(objectsWithFamily != _existingObjects.end()) {
          for(auto & objectsByType : objectsWithFamily->second) {
            for(auto & objectsByID : objectsByType.second) {
              ClearObjectHelper(objectsByID.second);
            }
          }
          _existingObjects.erase(objectsWithFamily);
        }
      } else {
        PRINT_NAMED_WARNING("BlockWorld.DeleteObjectsByFamily.ClearDisabled",
                            "Will not delete family %d objects because object deletion is disabled.",
                            family);
      }
    }
    
    void BlockWorld::DeleteObjectsByType(const ObjectType type) {
      if(_canDeleteObjects) {
        for(auto & objectsByFamily : _existingObjects) {
          ObjectsMapByType_t::iterator objectsWithType = objectsByFamily.second.find(type);
          if(objectsWithType != objectsByFamily.second.end()) {
            for(auto & objectsByID : objectsWithType->second) {
              ClearObjectHelper(objectsByID.second);
            }
            
            objectsByFamily.second.erase(objectsWithType);
            
            // Types are unique.  No need to keep looking
            return;
          }
        }
      } else {
        PRINT_NAMED_WARNING("BlockWorld.DeleteObjectsByType.DeleteDisabled",
                            "Will not delete %s objects because object deletion is disabled.",
                            ObjectTypeToString(type));
        
      }
    }
  
    bool BlockWorld::DeleteObject(const ObjectID withID)
    {
      bool retval = false;
      ObservableObject* object = GetObjectByIdHelper(withID);
      
      if(nullptr != object)
      {
        // Inform caller that we found the requested ID:
        retval = true;
        
        // Need to do all the same cleanup as Clear() calls
        ClearObjectHelper(object);
        
        // Actually delete the object we found
        ObjectFamily inFamily = object->GetFamily();
        ObjectType   withType = object->GetType();
        delete object;
        
        // And remove it from the container
        _existingObjects[inFamily][withType].erase(withID);
      }
      
      return retval;
    } // DeleteObject()

    bool BlockWorld::ClearObject(ObservableObject* object)
    {
      if(nullptr == object) {
        return false;
      } else if(_canDeleteObjects || object->GetNumTimesObserved() < MIN_TIMES_TO_OBSERVE_OBJECT) {
        ClearObjectHelper(object);
        return true;
      } else {
        PRINT_NAMED_WARNING("BlockWorld.ClearObject.DeleteDisabled",
                            "Will not clear object %d because object deletion is disabled.",
                            object->GetID().GetValue());
        return false;
      }
    }
  
    bool BlockWorld::ClearObject(const ObjectID withID)
    {
      return ClearObject(GetObjectByID(withID));
    } // ClearObject()
    
  
    BlockWorld::ObjectsMapByID_t::iterator BlockWorld::DeleteObject(const ObjectsMapByID_t::iterator objIter,
                                                                    const ObjectType&   withType,
                                                                    const ObjectFamily& fromFamily)
    {
      ObservableObject* object = objIter->second;
      
      if(_canDeleteObjects || object->GetNumTimesObserved() < MIN_TIMES_TO_OBSERVE_OBJECT)
      {
        ClearObjectHelper(object);
        
        // Delete the object
        delete object;
        
        // Erase from the container and return the iterator to the next element
        return _existingObjects[fromFamily][withType].erase(objIter);
      } else {
        PRINT_NAMED_WARNING("BlockWorld.DeleteObject.DeleteDisabled",
                            "Will not delete object %d because object deletion is disabled.",
                            object->GetID().GetValue());
        auto retIter(objIter);
        return ++retIter;
      }
    }

    void BlockWorld::DeselectCurrentObject()
    {
      if(_selectedObject.IsSet()) {
        ActionableObject* curSel = dynamic_cast<ActionableObject*>(GetObjectByID(_selectedObject));
        if(curSel != nullptr) {
          curSel->SetSelected(false);
        }
        _selectedObject.UnSet();
      }
    }

  
    bool BlockWorld::SelectObject(const ObjectID objectID)
    {
      ActionableObject* newSelection = dynamic_cast<ActionableObject*>(GetObjectByID(objectID));
      
      if(newSelection != nullptr) {
        // Unselect current object of interest, if it still exists (Note that it may just get
        // reselected here, but I don't think we care.)
        // Mark new object of interest as selected so it will draw differently
        DeselectCurrentObject();
        
        newSelection->SetSelected(true);
        _selectedObject = objectID;
        PRINT_STREAM_INFO("BlockWorld.SelectObject", "Selected Object with ID=" << objectID.GetValue());
        
        return true;
      } else {
        PRINT_STREAM_WARNING("BlockWorld.SelectObject.InvalidID",
                            "Object with ID=" << objectID.GetValue() << " not found. Not updating selected object.");
        return false;
      }
    } // SelectObject()
    
    void BlockWorld::CycleSelectedObject()
    {
      if(_selectedObject.IsSet()) {
        // Unselect current object of interest, if it still exists (Note that it may just get
        // reselected here, but I don't think we care.)
        // Mark new object of interest as selected so it will draw differently
        ActionableObject* object = dynamic_cast<ActionableObject*>(GetObjectByID(_selectedObject));
        if(object != nullptr) {
          object->SetSelected(false);
        }
      }
      
      bool currSelectedObjectFound = false;
      bool newSelectedObjectSet = false;
      
      // Iterate through all the objects
      auto const & allObjects = GetAllExistingObjects();
      for(auto const & objectsByFamily : allObjects)
      {
        // Markerless objects are not Actionable, so ignore them for selection
        if(objectsByFamily.first != ObjectFamily::MarkerlessObject)
        {
          for (auto const & objectsByType : objectsByFamily.second){
            
            //PRINT_INFO("currType: %d", blockType.first);
            for (auto const & objectsByID : objectsByType.second) {
              
              ActionableObject* object = dynamic_cast<ActionableObject*>(objectsByID.second);
              if(object != nullptr && object->HasPreActionPoses() && !object->IsBeingCarried() &&
                 object->IsExistenceConfirmed())
              {
                //PRINT_INFO("currID: %d", block.first);
                if (currSelectedObjectFound) {
                  // Current block of interest has been found.
                  // Set the new block of interest to the next block in the list.
                  _selectedObject = object->GetID();
                  newSelectedObjectSet = true;
                  //PRINT_INFO("new block found: id %d  type %d", block.first, blockType.first);
                  break;
                } else if (object->GetID() == _selectedObject) {
                  currSelectedObjectFound = true;
                  //PRINT_INFO("curr block found: id %d  type %d", block.first, blockType.first);
                }
              }
            } // for each ID
            
            if (newSelectedObjectSet) {
              break;
            }
            
          } // for each type
          
          if(newSelectedObjectSet) {
            break;
          }
        } // if family != MARKERLESS_OBJECTS
      } // for each family
      
      // If the current object of interest was found, but a new one was not set
      // it must have been the last block in the map. Set the new object of interest
      // to the first object in the map as long as it's not the same object.
      if (!currSelectedObjectFound || !newSelectedObjectSet) {
        
        // Find first object
        ObjectID firstObject; // initialized to un-set
        for(auto const & objectsByFamily : allObjects) {
          for (auto const & objectsByType : objectsByFamily.second) {
            for (auto const & objectsByID : objectsByType.second) {
              const ActionableObject* object = dynamic_cast<ActionableObject*>(objectsByID.second);
              if(object != nullptr && object->HasPreActionPoses() && !object->IsBeingCarried() &&
                object->IsExistenceConfirmed())
              {
                firstObject = objectsByID.first;
                break;
              }
            }
            if (firstObject.IsSet()) {
              break;
            }
          }
          
          if (firstObject.IsSet()) {
            break;
          }
        } // for each family
        
        
        if (firstObject == _selectedObject || !firstObject.IsSet()){
          //PRINT_INFO("Only one object in existence.");
        } else {
          //PRINT_INFO("Setting object of interest to first block");
          _selectedObject = firstObject;
        }
      }
      
      // Mark new object of interest as selected so it will draw differently
      ActionableObject* object = dynamic_cast<ActionableObject*>(GetObjectByID(_selectedObject));
      if (object != nullptr) {
        object->SetSelected(true);
        PRINT_STREAM_INFO("BlockWorld.CycleSelectedObject", "Object of interest: ID = " << _selectedObject.GetValue());
      } else {
        PRINT_STREAM_INFO("BlockWorld.CycleSelectedObject", "No object of interest found");
      }
  
    } // CycleSelectedObject()
    
    
    void BlockWorld::EnableDraw(bool on)
    {
      _enableDraw = on;
    }
    
    void BlockWorld::DrawObsMarkers() const
    {
      if (_enableDraw) {
        for (const auto& poseKeyMarkerMapAtTimestamp : _obsMarkers) {
          for (const auto& poseKeyMarkerMap : poseKeyMarkerMapAtTimestamp.second) {
            const Quad2f& q = poseKeyMarkerMap.second.GetImageCorners();
            f32 scaleF = 1.0f;
            switch(IMG_STREAM_RES) {
              case ImageResolution::CVGA:
              case ImageResolution::QVGA:
                break;
              case ImageResolution::QQVGA:
                scaleF *= 0.5;
                break;
              case ImageResolution::QQQVGA:
                scaleF *= 0.25;
                break;
              case ImageResolution::QQQQVGA:
                scaleF *= 0.125;
                break;
              default:
                printf("WARNING (DrawObsMarkers): Unsupported streaming res %d\n", (int)IMG_STREAM_RES);
                break;
            }
            _robot->GetContext()->GetVizManager()->SendTrackerQuad(q[Quad::TopLeft].x()*scaleF,     q[Quad::TopLeft].y()*scaleF,
                                                                   q[Quad::TopRight].x()*scaleF,    q[Quad::TopRight].y()*scaleF,
                                                                   q[Quad::BottomRight].x()*scaleF, q[Quad::BottomRight].y()*scaleF,
                                                                   q[Quad::BottomLeft].x()*scaleF,  q[Quad::BottomLeft].y()*scaleF);
          }
        }
      }
    }
    
    void BlockWorld::DrawAllObjects() const
    {
      for(auto & objectsByFamily : _existingObjects) {
        for(auto & objectsByType : objectsByFamily.second) {
          for(auto & objectsByID : objectsByType.second) {
            ObservableObject* object = objectsByID.second;
            if(object->IsExistenceConfirmed()) {
              object->Visualize();
            } else {
              // Draw unconfirmed objects in a special color
              object->Visualize(NamedColors::LIGHTGRAY);
            }
          }
        }
      }
      
      // (Re)Draw the selected object separately so we can get its pre-action poses
      if(GetSelectedObject().IsSet())
      {
        const ActionableObject* selectedObject = dynamic_cast<const ActionableObject*>(GetObjectByID(GetSelectedObject()));
        if(selectedObject == nullptr) {
          PRINT_NAMED_ERROR("BlockWorld.DrawAllObjects.NullSelectedObject",
                            "Selected object ID = %d, but it came back null.",
                            GetSelectedObject().GetValue());
        } else {
          if(selectedObject->IsSelected() == false) {
            PRINT_NAMED_WARNING("BlockWorld.DrawAllObjects.SelectionMisMatch",
                                "Object %d is selected in BlockWorld but does not have its "
                                "selection flag set.", GetSelectedObject().GetValue());
          }
          
          std::vector<std::pair<Quad2f,ObjectID> > obstacles;
          _robot->GetBlockWorld().GetObstacles(obstacles);
          selectedObject->VisualizePreActionPoses(obstacles, &_robot->GetPose());
        }
      } // if selected object is set
      
      // (Re)Draw the localization object separately so we can show it in a different color
      if(_robot->GetLocalizedTo().IsSet()) {
        const Vision::ObservableObject* locObject = GetObjectByID(_robot->GetLocalizedTo());
        locObject->Visualize(NamedColors::LOCALIZATION_OBJECT);
      }
      
    } // DrawAllObjects()
  
} // namespace Cozmo
} // namespace Anki

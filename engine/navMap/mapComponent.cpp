/**
 * File: mapComponent.cpp
 *
 * Author: Michael Willett
 * Created: 2017-09-11
 *
 * Description: Component for consuming new sensor data and processing the information into
 *              the appropriate map objects
 *
 * Copyright: Anki, Inc. 2017
 *
 **/
 
#include "engine/navMap/mapComponent.h"

#include "anki/vision/basestation/observableObjectLibrary.h"
#include "anki/common/basestation/math/poseOriginList.h"
#include "anki/common/basestation/math/polygon_impl.h"
#include "anki/common/basestation/utils/timer.h"

#include "engine/navMap/iNavMap.h"
#include "engine/navMap/navMapFactory.h"
#include "engine/navMap/memoryMap/data/memoryMapData_Cliff.h"
#include "engine/navMap/memoryMap/data/memoryMapData_ProxObstacle.h"
#include "engine/navMap/memoryMap/data/memoryMapData_ObservableObject.h"

#include "engine/block.h"
#include "engine/robot.h"
#include "engine/robotStateHistory.h"
#include "engine/cozmoContext.h"
#include "engine/markerlessObject.h"
#include "engine/components/cliffSensorComponent.h"
#include "engine/aiComponent/aiComponent.h"
#include "engine/aiComponent/AIWhiteboard.h"
#include "engine/groundPlaneROI.h"
#include "engine/blockWorld/blockWorld.h"

#include "util/cpuProfiler/cpuProfiler.h"
#include "util/console/consoleInterface.h"

// Giving this its own local define, in case we want to control it independently of DEV_CHEATS / SHIPPING, etc.
#define ENABLE_DRAWING ANKI_DEV_CHEATS

namespace Anki {
namespace Cozmo {
  
// how often we request redrawing maps. Added because I think clad is getting overloaded with the amount of quads
CONSOLE_VAR(float, kMapRenderRate_sec, "MapComponent", 0.25f);
  
// kObjectRotationChangeToReport_deg: if the rotation of an object changes by this much, memory map will be notified
CONSOLE_VAR(float, kObjectRotationChangeToReport_deg, "MapComponent", 10.0f);
// kObjectPositionChangeToReport_mm: if the position of an object changes by this much, memory map will be notified
CONSOLE_VAR(float, kObjectPositionChangeToReport_mm, "MapComponent", 5.0f);


// kOverheadEdgeCloseMaxLenForTriangle_mm: maximum length of the close edge to be considered a triangle instead of a quad
CONSOLE_VAR(float, kOverheadEdgeCloseMaxLenForTriangle_mm, "MapComponent", 15.0f);
// kOverheadEdgeFarMaxLenForLine_mm: maximum length of the far edge to be considered a line instead of a triangle or a quad
CONSOLE_VAR(float, kOverheadEdgeFarMaxLenForLine_mm, "MapComponent", 15.0f);
// kOverheadEdgeFarMinLenForLine_mm: minimum length of the far edge to even report the line
CONSOLE_VAR(float, kOverheadEdgeFarMinLenForClearReport_mm, "MapComponent", 3.0f); // tested 5 and was too big
// kOverheadEdgeSegmentNoiseLen_mm: segments whose length is smaller than this will be considered noise
CONSOLE_VAR(float, kOverheadEdgeSegmentNoiseLen_mm, "MapComponent", 6.0f);

// kDebugRenderOverheadEdges: enables/disables debug render of points reported from vision
CONSOLE_VAR(bool, kDebugRenderOverheadEdges, "MapComponent", false);
// kDebugRenderOverheadEdgeClearQuads: enables/disables debug render of nonBorder quads from overhead detection (clear)
CONSOLE_VAR(bool, kDebugRenderOverheadEdgeClearQuads, "MapComponent", false);
// kDebugRenderOverheadEdgeBorderQuads: enables/disables debug render of border quads only (interesting edges)
CONSOLE_VAR(bool, kDebugRenderOverheadEdgeBorderQuads, "MapComponent", false);

// kReviewInterestingEdges: if set to true, interesting edges are reviewed after adding new ones to see whether they are still interesting
CONSOLE_VAR(bool, kReviewInterestingEdges, "MapComponent", true);

// Whether or not to put unrecognized markerless objects like collision/prox obstacles and cliffs into the memory map
CONSOLE_VAR(bool, kAddUnrecognizedMarkerlessObjectsToMemMap, "MapComponent", false);
// Whether or not to put custom objects in the memory map (COZMO-9360)
CONSOLE_VAR(bool, kAddCustomObjectsToMemMap, "MapComponent", false);

// kRobotRotationChangeToReport_deg: if the rotation of the robot changes by this much, memory map will be notified
CONSOLE_VAR(float, kRobotRotationChangeToReport_deg, "MapComponent", 20.0f);
// kRobotPositionChangeToReport_mm: if the position of the robot changes by this much, memory map will be notified
CONSOLE_VAR(float, kRobotPositionChangeToReport_mm, "MapComponent", 8.0f);


namespace {

// return the content type we would set in the memory type for each object family
MemoryMapTypes::EContentType ObjectFamilyToMemoryMapContentType(ObjectFamily family, bool isAdding)
{
  using ContentType = MemoryMapTypes::EContentType;
  ContentType retType = ContentType::Unknown;
  switch(family)
  {
    case ObjectFamily::Block:
    case ObjectFamily::LightCube:
      // pick depending on addition or removal
      retType = isAdding ? ContentType::ObstacleCube : ContentType::ObstacleCubeRemoved;
      break;
    case ObjectFamily::Charger:
      retType = isAdding ? ContentType::ObstacleCharger : ContentType::ObstacleChargerRemoved;
      break;
    case ObjectFamily::MarkerlessObject:
    {
      // old .badIsAdding message
      if(!isAdding)
      {
        PRINT_NAMED_WARNING("ObjectFamilyToMemoryMapContentType.MarkerlessObject.RemovalNotSupported",
                            "ContentType MarkerlessObject removal is not supported. kAddUnrecognizedMarkerlessObjectsToMemMap was (%s)",
                            kAddUnrecognizedMarkerlessObjectsToMemMap ? "true" : "false");
      }
      else
      {
        PRINT_NAMED_WARNING("ObjectFamilyToMemoryMapContentType.MarkerlessObject.AdditionNotSupported",
                            "ContentType MarkerlessObject addition is not supported. kAddUnrecognizedMarkerlessObjectsToMemMap was (%s)",
                            kAddUnrecognizedMarkerlessObjectsToMemMap ? "true" : "false");
        // retType = ContentType::ObstacleUnrecognized;
      }
      break;
    }
      
    case ObjectFamily::CustomObject:
    {
      // old .badIsAdding message
      if(!isAdding)
      {
        PRINT_NAMED_WARNING("ObjectFamilyToMemoryMapContentType.CustomObject.RemovalNotSupported",
                            "ContentType CustomObject removal is not supported. kCustomObjectsToMemMap was (%s)",
                            kAddCustomObjectsToMemMap ? "true" : "false");
      }
      else
      {
        PRINT_NAMED_WARNING("ObjectFamilyToMemoryMapContentType.CustomObject.AdditionNotSupported",
                            "ContentType CustomObject addition is not supported. kCustomObjectsToMemMap was (%s)",
                            kAddCustomObjectsToMemMap ? "true" : "false");
      }
      break;
    }
      
    case ObjectFamily::Invalid:
    case ObjectFamily::Unknown:
    case ObjectFamily::Ramp:
    case ObjectFamily::Mat:
    break;
  };

  return retType;
}

};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
MapComponent::MapComponent(Robot* robot)
: _robot(robot)
, _currentMapOriginID(PoseOriginList::UnknownOriginID)
, _isRenderEnabled(true)
, _broadcastRate_sec(-1.0f)
, _nextBroadcastTimeStamp(0)
{
  if(_robot->HasExternalInterface())
  {    
    using namespace ExternalInterface;
    IExternalInterface& externalInterface = *_robot->GetExternalInterface();
    auto helper = MakeAnkiEventUtil(externalInterface, *this, _eventHandles);
    helper.SubscribeGameToEngine<MessageGameToEngineTag::SetMemoryMapRenderEnabled>();
    helper.SubscribeGameToEngine<MessageGameToEngineTag::SetMemoryMapBroadcastFrequency_sec>();
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
MapComponent::~MapComponent()
{
}

template<>
void MapComponent::HandleMessage(const ExternalInterface::SetMemoryMapRenderEnabled& msg)
{
  SetRenderEnabled(msg.enabled);
};

template<>
void MapComponent::HandleMessage(const ExternalInterface::SetMemoryMapBroadcastFrequency_sec& msg)
{
  _broadcastRate_sec = msg.frequency;
  _nextBroadcastTimeStamp = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Result MapComponent::Update()
{ 
  // Currently this is not doing anything, but ultimately we might want to add timers to certain object types
  // or update other state generically, and it could all go here.
  return RESULT_OK;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MapComponent::UpdateMapOrigins(PoseOriginID_t oldOriginID, PoseOriginID_t newOriginID)
{
  // oldOrigin is the pointer/id of the map we were just building, and it's going away. It's the current map
  // newOrigin is the pointer/id of the map that is staying, it's the one we rejiggered to, and we haven't changed in a while
  DEV_ASSERT(_navMaps.find(oldOriginID) != _navMaps.end(),
             "MapComponent.UpdateObjectOrigins.missingMapOriginOld");
  DEV_ASSERT(_navMaps.find(newOriginID) != _navMaps.end(),
             "MapComponent.UpdateObjectOrigins.missingMapOriginNew");
  DEV_ASSERT(oldOriginID == _currentMapOriginID, "MapComponent.UpdateObjectOrigins.updatingMapNotCurrent");

  const Pose3d& oldOrigin = _robot->GetPoseOriginList().GetOriginByID(oldOriginID);
  const Pose3d& newOrigin = _robot->GetPoseOriginList().GetOriginByID(newOriginID);
  
  // before we merge the object information from the memory maps, apply rejiggering also to their
  // reported poses
  UpdateOriginsOfObjects(oldOriginID, newOriginID);

  // grab the underlying memory map and merge them
  INavMap* oldMap = _navMaps[oldOriginID].get();
  INavMap* newMap = _navMaps[newOriginID].get();
  
  // COZMO-6184: the issue localizing to a zombie map was related to a cube being disconnected while we delocalized.
  // The issue has been fixed, but this code here would have prevented a crash and produce an error instead, so I
  // am going to keep the code despite it may not run anymore
  if ( nullptr == newMap )
  {
    // error to identify the issue
    PRINT_NAMED_ERROR("MapComponent.UpdateObjectOrigins.NullMapFound",
                      "Origin '%s' did not have a memory map. Creating empty",
                      newOrigin.GetName().c_str());
    
    // create empty map since somehow we lost the one we had
    VizManager* vizMgr = _robot->GetContext()->GetVizManager();
    INavMap* emptyMemoryMap = NavMapFactory::CreateMemoryMap(vizMgr, _robot);
    
    // set in the container of maps
    _navMaps[newOriginID].reset(emptyMemoryMap);
    // set the pointer to this newly created instance
    newMap = emptyMemoryMap;
  }

  // continue the merge as we were going to do, so at least we don't lose the information we were just collecting
  Pose3d oldWrtNew;
  const bool success = oldOrigin.GetWithRespectTo(newOrigin, oldWrtNew);
  DEV_ASSERT(success, "MapComponent.UpdateObjectOrigins.BadOldWrtNull");
  newMap->Merge(oldMap, oldWrtNew);
  
  // switch back to what is becoming the new map
  _currentMapOriginID = newOriginID;
  
  // now we can delete what is become the old map, since we have merged its data into the new one
  _navMaps.erase( oldOriginID ); // smart pointer will delete memory
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MapComponent::UpdateRobotPose()
{
  using namespace MemoryMapTypes;
  ANKI_CPU_PROFILE("MapComponent::UpdateRobotPoseInMemoryMap");
  
  // grab current robot pose
  DEV_ASSERT(_robot->GetPoseOriginList().GetCurrentOriginID() == _currentMapOriginID,
             "MapComponent.OnRobotPoseChanged.InvalidWorldOrigin");
  const Pose3d& robotPose = _robot->GetPose();
  const Pose3d& robotPoseWrtOrigin = robotPose.GetWithRespectToRoot();
  
  // check if we have moved far enough that we need to resend
  const Point3f distThreshold(kRobotPositionChangeToReport_mm, kRobotPositionChangeToReport_mm, kRobotPositionChangeToReport_mm);
  const Radians angleThreshold( DEG_TO_RAD(kRobotRotationChangeToReport_deg) );
  const bool isPrevSet = _reportedRobotPose.HasParent();
  const bool isFarFromPrev = !isPrevSet || (!robotPoseWrtOrigin.IsSameAs(_reportedRobotPose, distThreshold, angleThreshold));
    
  // if we need to add
  const bool addAgain = isFarFromPrev;
  if ( addAgain )
  {
    INavMap* currentNavMemoryMap = GetCurrentMemoryMap();
    TimeStamp_t currentTimestamp = _robot->GetLastMsgTimestamp();
    DEV_ASSERT(currentNavMemoryMap, "MapComponent.UpdateRobotPoseInMemoryMap.NoMemoryMap");
    // cliff quad: clear or cliff
    {
      // TODO configure this size somethere else
      Point3f cliffSize = MarkerlessObject(ObjectType::ProxObstacle).GetSize() * 0.5f;
      Quad3f cliffquad {
        {+cliffSize.x(), +cliffSize.y(), cliffSize.z()},  // up L
        {-cliffSize.x(), +cliffSize.y(), cliffSize.z()},  // lo L
        {+cliffSize.x(), -cliffSize.y(), cliffSize.z()},  // up R
        {-cliffSize.x(), -cliffSize.y(), cliffSize.z()}}; // lo R
      robotPoseWrtOrigin.ApplyTo(cliffquad, cliffquad);

      // depending on cliff on/off, add as ClearOfCliff or as Cliff
      if ( _robot->GetCliffSensorComponent().IsCliffDetected() )
      {
        // build data we want to embed for this quad
        Vec3f rotatedFwdVector = robotPoseWrtOrigin.GetRotation() * X_AXIS_3D();
        MemoryMapData_Cliff cliffData(Vec2f{rotatedFwdVector.x(), rotatedFwdVector.y()}, currentTimestamp);
        currentNavMemoryMap->AddQuad(cliffquad, cliffData);
      }
      else
      {
        currentNavMemoryMap->AddQuad(cliffquad, MemoryMapData(EContentType::ClearOfCliff, currentTimestamp));
      }
    }

    const Quad2f& robotQuad = _robot->GetBoundingQuadXY(robotPoseWrtOrigin);

    // regular clear of obstacle
    currentNavMemoryMap->AddQuad(robotQuad, MemoryMapData(EContentType::ClearOfObstacle, currentTimestamp));

    // also notify behavior whiteboard.
    // rsam: should this information be in the map instead of the whiteboard? It seems a stretch that
    // blockworld knows now about behaviors, maybe all this processing of quads should be done in a separate
    // robot component, like a VisualInformationProcessingComponent
    _robot->GetAIComponent().GetWhiteboard().ProcessClearQuad(robotQuad);

    // update las reported pose
    _reportedRobotPose = robotPoseWrtOrigin;
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MapComponent::FlagGroundPlaneROIInterestingEdgesAsUncertain()
{
  using namespace MemoryMapTypes;
  // get quad wrt robot
  const Pose3d& curRobotPose = _robot->GetPose().GetWithRespectToRoot();
  Quad3f groundPlaneWrtRobot;
  curRobotPose.ApplyTo(GroundPlaneROI::GetGroundQuad(), groundPlaneWrtRobot);
  
  // ask memory map to clear
  INavMap* currentNavMemoryMap = GetCurrentMemoryMap();
  DEV_ASSERT(currentNavMemoryMap, "MapComponent.FlagGroundPlaneROIInterestingEdgesAsUncertain.NullMap");
  TimeStamp_t t = _robot->GetLastImageTimeStamp();
  
  NodeTransformFunction transform = [t] (MemoryMapDataPtr oldData) -> MemoryMapDataPtr
    {
        if (EContentType::InterestingEdge == oldData->type) {
          return std::make_shared<MemoryMapData>(EContentType::Unknown, t);
        }
        return oldData;
    };
  
  Poly2f poly;
  poly.ImportQuad2d(groundPlaneWrtRobot);
  currentNavMemoryMap->TransformContent(poly, transform);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MapComponent::FlagQuadAsNotInterestingEdges(const Quad2f& quadWRTOrigin)
{
  using namespace MemoryMapTypes;
  INavMap* currentNavMemoryMap = GetCurrentMemoryMap();
  DEV_ASSERT(currentNavMemoryMap, "MapComponent.FlagQuadAsNotInterestingEdges.NullMap");
  currentNavMemoryMap->AddQuad(quadWRTOrigin, MemoryMapData(EContentType::NotInterestingEdge, _robot->GetLastImageTimeStamp()));
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MapComponent::FlagInterestingEdgesAsUseless()
{
  // flag all content as Unknown: ideally we would add a new type (SmallInterestingEdge), so that we know
  // we detected something, but we discarded it because it didn't have enough info; however that increases
  // complexity when raycasting, finding boundaries, readding edges, etc. By flagging Unknown we simply say
  // "there was something here, but we are not sure what it was", which can be good to re-explore the area
  using namespace MemoryMapTypes;
    
  INavMap* currentNavMemoryMap = GetCurrentMemoryMap();
  DEV_ASSERT(currentNavMemoryMap, "MapComponent.FlagInterestingEdgesAsUseless.NullMap");
  TimeStamp_t t = _robot->GetLastImageTimeStamp();
  
  NodeTransformFunction transform = [t] (MemoryMapDataPtr oldData) -> MemoryMapDataPtr
    {
        if (EContentType::InterestingEdge == oldData->type) {
          return std::make_shared<MemoryMapData>(EContentType::Unknown, t);
        }
        return oldData;
    };
    
  currentNavMemoryMap->TransformContent(transform);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MapComponent::CreateLocalizedMemoryMap(PoseOriginID_t worldOriginID)
{
  DEV_ASSERT_MSG(_robot->GetPoseOriginList().ContainsOriginID(worldOriginID),
                 "MapComponent.CreateLocalizedMemoryMap.BadWorldOriginID",
                 "ID:%d", worldOriginID);
  
  // Since we are going to create a new memory map, check if any of the existing ones have become a zombie
  // This could happen if either the current map never saw a localizable object, or if objects in previous maps
  // have been moved or deactivated, which invalidates them as localizable
  MapTable::iterator iter = _navMaps.begin();
  while ( iter != _navMaps.end() )
  {
    const bool isZombie = _robot->GetBlockWorld().IsZombiePoseOrigin( iter->first );
    if ( isZombie ) {
      // PRINT_CH_DEBUG("MapComponent", "CreateLocalizedMemoryMap", "Deleted map (%p) because it was zombie", iter->first);
      LOG_EVENT("MapComponent.memory_map.deleting_zombie_map", "%d", worldOriginID );
      iter = _navMaps.erase(iter);
      
      // also remove the reported poses in this origin for every object (fixes a leak, and better tracks where objects are)
      for( auto& posesForObjectIt : _reportedPoses ) {
        OriginToPoseInMapInfo& posesPerOriginForObject = posesForObjectIt.second;
        const PoseOriginID_t zombieOriginID = iter->first;
        posesPerOriginForObject.erase( zombieOriginID );
      }
    } else {
      //PRINT_CH_DEBUG("MapComponent", "CreateLocalizedMemoryMap", "Map (%p) is still good", iter->first);
      LOG_EVENT("MapComponent.memory_map.keeping_alive_map", "%d", worldOriginID );
      ++iter;
    }
  }
  
  // clear all memory map rendering because indexHints are changing
  ClearRender();
  
  // if the origin is null, we would never merge the map, which could leak if a new one was created
  // do not support this by not creating one at all if the origin is null
  if ( PoseOriginList::UnknownOriginID != worldOriginID )
  {
    // create a new memory map in the given origin
    VizManager* vizMgr = _robot->GetContext()->GetVizManager();
    INavMap* navMemoryMap = NavMapFactory::CreateMemoryMap(vizMgr, _robot);
    _navMaps.emplace( std::make_pair(worldOriginID, std::unique_ptr<INavMap>(navMemoryMap)) );
    _currentMapOriginID = worldOriginID;
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MapComponent::DrawMap() const
{
  if(ENABLE_DRAWING)
  {
    if ( _isRenderEnabled )
    {
      // check refresh rate
      static f32 nextDrawTimeStamp = 0;
      const f32 currentTimeInSeconds = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
      if ( nextDrawTimeStamp > currentTimeInSeconds ) {
        return;
      }
      // we are rendering reset refresh time
      nextDrawTimeStamp = currentTimeInSeconds + kMapRenderRate_sec;
   
      size_t lastIndexNonCurrent = 0;
    
      // rendering all current maps with indexHint
      for (const auto& memMapPair : _navMaps)
      {
        const PoseOriginID_t originID = memMapPair.first;
        
        const bool isCurrent = (originID == _currentMapOriginID);
        size_t indexHint = isCurrent ? 0 : (++lastIndexNonCurrent);
        memMapPair.second->DrawDebugProcessorInfo(indexHint);
        memMapPair.second->BroadcastMemoryMapDraw(originID, indexHint);
      }
    }
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MapComponent::BroadcastMap()
{
  if (_broadcastRate_sec >= 0.0f)
  {
    const float currentTimeInSeconds = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
    if (FLT_GT(_nextBroadcastTimeStamp, currentTimeInSeconds)) {
      return;
    }
    // Reset the timer but don't accumulate error
    do {
      _nextBroadcastTimeStamp += _broadcastRate_sec;
    } while (FLT_LE(_nextBroadcastTimeStamp, currentTimeInSeconds));

    // Send only the current map
    const auto& currentOriginMap = _navMaps.find(_currentMapOriginID);
    if (currentOriginMap != _navMaps.end()) {
      // Look up and send the origin ID also
      const PoseOriginID_t originID = currentOriginMap->first;
      if (originID != PoseOriginList::UnknownOriginID) {
        currentOriginMap->second->Broadcast(originID);
      }
    }
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MapComponent::ClearRender() const
{
  if(ENABLE_DRAWING)
  {
    for ( const auto& memMapPair : _navMaps )
    {
      memMapPair.second->ClearDraw();
    }
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MapComponent::SetRenderEnabled(bool enabled)
{
  // if disabling, clear render now. If enabling wait until next render time
  if ( _isRenderEnabled && !enabled ) {
    ClearRender();
  }

  // set new value
  _isRenderEnabled = enabled;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
INavMap* MapComponent::GetCurrentMemoryMapHelper() const
{    
  // current map (if any) must match current robot origin
  DEV_ASSERT((PoseOriginList::UnknownOriginID == _currentMapOriginID) ||
             (_robot->GetPoseOriginList().GetCurrentOriginID() == _currentMapOriginID),
             "MapComponent.GetNavMemoryMap.BadOrigin");
    
  INavMap* curMap = nullptr;
  if ( PoseOriginList::UnknownOriginID != _currentMapOriginID ) {
    auto matchPair = _navMaps.find(_currentMapOriginID);
    if ( matchPair != _navMaps.end() ) {
      curMap = matchPair->second.get();
    } else {
      DEV_ASSERT(false, "MapComponent.GetNavMemoryMap.MissingMap");
    }
  }
  
  return curMap;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MapComponent::UpdateObjectPose(const ObservableObject& object, const Pose3d* oldPose, PoseState oldPoseState)
{
  const ObjectID& objectID = object.GetID();
  DEV_ASSERT(objectID.IsSet(), "MapComponent.OnObjectPoseChanged.InvalidObjectID");
  
  const PoseState newPoseState = object.GetPoseState();
  const ObjectFamily family = object.GetFamily();
  bool objectTrackedInMemoryMap = true;
  if (family == ObjectFamily::CustomObject && !kAddCustomObjectsToMemMap) {
    objectTrackedInMemoryMap = false; // COZMO-9360
  }
  else if (family == ObjectFamily::MarkerlessObject && !kAddUnrecognizedMarkerlessObjectsToMemMap) {
    objectTrackedInMemoryMap = false; // COZMO-7496?
  }
  
  if( objectTrackedInMemoryMap )
  {	
    /*
      Three things can happen:
       a) first time we see an object: OldPoseState=!Valid, NewPoseState= Valid
       b) updating an object:          OldPoseState= Valid, NewPoseState= Valid
       c) deleting an object:          OldPoseState= Valid, NewPoseState=!Valid
     */
    const bool oldValid = ObservableObject::IsValidPoseState(oldPoseState);
    const bool newValid = ObservableObject::IsValidPoseState(newPoseState);
    if ( !oldValid && newValid )
    {
      // first time we see the object, add report
      AddObservableObject(object, object.GetPose());
    }
    else if ( oldValid && newValid )
    {
      // updating the pose of an object, decide if we update the report. As an optimization, we don't update
      // it if the poses are close enough
      const int objectIdInt = objectID.GetValue();
      const OriginToPoseInMapInfo& reportedPosesForObject = _reportedPoses[objectIdInt];
      const PoseOrigin& curOrigin = object.GetPose().FindRoot();
      const PoseOriginID_t curOriginID = curOrigin.GetID();
      DEV_ASSERT_MSG(_robot->GetPoseOriginList().ContainsOriginID(curOriginID),
                     "MapComponent.OnObjectPoseChanged.ObjectOriginNotInOriginList",
                     "ID:%d", curOriginID);
      const auto poseInNewOriginIter = reportedPosesForObject.find( curOriginID );

      if ( newPoseState == PoseState::Dirty && poseInNewOriginIter != reportedPosesForObject.end() )
      {
        // Object is dirty, so remove it so we dont try to plan around it. Ideally we would differentiate 
        // between "object moved" and "object seen from far away", but that distinction is not available now,
        // so just keep fully verified cubes in the map.
        RemoveObservableObject(object, curOriginID);
      } else {
        if ( poseInNewOriginIter != reportedPosesForObject.end() )
        {
          // note that for distThreshold, since Z affects whether we add to the memory map, distThreshold should
          // be smaller than the threshold to not report
          DEV_ASSERT(kObjectPositionChangeToReport_mm < object.GetDimInParentFrame<'Z'>()*0.5f,
                    "OnObjectPoseChanged.ChangeThresholdTooBig");
          const float distThreshold = kObjectPositionChangeToReport_mm;
          const Radians angleThreshold( DEG_TO_RAD(kObjectRotationChangeToReport_deg) );

          // compare new pose with previous entry and decide if isFarFromPrev
          const PoseInMapInfo& info = poseInNewOriginIter->second;
          const bool isFarFromPrev =
            ( !info.isInMap || (!object.GetPose().IsSameAs(info.pose, Point3f(distThreshold), angleThreshold)));
          
          // if it is far from previous (or previous was not in the map, remove-add)
          if ( isFarFromPrev ) {
            RemoveObservableObject(object, curOriginID);
            AddObservableObject(object, object.GetPose());
          }
        }
        else
        {
          // did not find an entry in the current origin for this object, add it now
          AddObservableObject(object, object.GetPose());
        }
      }
    }
    else if ( oldValid && !newValid )
    {
      // deleting an object, remove its report using oldOrigin (the origin it was removed from)
      const PoseOriginID_t oldOriginID = oldPose->GetRootID();
      RemoveObservableObject(object, oldOriginID);
    }
    else
    {
      // not possible
      PRINT_NAMED_ERROR("MapComponent.OnObjectPoseChanged.BothStatesAreInvalid",
                        "Object %d changing from Invalid to Invalid", objectID.GetValue());
    }
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MapComponent::AddObservableObject(const ObservableObject& object, const Pose3d& newPose)
{
  const ObjectFamily objectFam = object.GetFamily();
  const MemoryMapTypes::EContentType addType = ObjectFamilyToMemoryMapContentType(objectFam, true);
  if ( addType == MemoryMapTypes::EContentType::Unknown )
  {
    // this is ok, this obstacle family is not tracked in the memory map
    PRINT_CH_INFO("MapComponent", "MapComponent.AddObservableObject.InvalidAddType",
                  "Family '%s' is not known in memory map",
                  ObjectFamilyToString(objectFam) );
    return;
  }

  const int objectId = object.GetID().GetValue();

  // find the memory map for the given origin
  const PoseOriginID_t originID = newPose.GetRootID();
  auto matchPair = _navMaps.find(originID);
  if ( matchPair != _navMaps.end() )
  {
    // in order to properly handle stacks, do not add the quad to the memory map for objects that are not
    // on the floor
    Pose3d objWrtRobot;
    if ( newPose.GetWithRespectTo(_robot->GetPose(), objWrtRobot) )
    {
      INavMap* memoryMap = matchPair->second.get();
      
      const bool isFloating = object.IsPoseTooHigh(objWrtRobot, 1.f, STACKED_HEIGHT_TOL_MM, 0.f);
      if ( isFloating )
      {
        // store in as a reported pose, but set as not in map (the pose value is not relevant)
        _reportedPoses[objectId][originID] = PoseInMapInfo(newPose, false);
      }
      else
      {
        // add to memory map flattened out wrt origin
        Pose3d newPoseWrtOrigin = newPose.GetWithRespectToRoot();
        const Quad2f& newQuad = object.GetBoundingQuadXY(newPoseWrtOrigin);
        switch (addType) {
          case MemoryMapTypes::EContentType::ObstacleCube:
          {
            // eventually we will want to store multiple ID's to the node data in the case for multiple blocks
            // however, we have no mechanism for merging data, so for now we just replace with the new id
            Poly2f boundingPoly;
            boundingPoly.ImportQuad2d(newQuad);
            MemoryMapData_ObservableObject data(addType, object.GetID(), boundingPoly, _robot->GetLastImageTimeStamp());
            memoryMap->AddQuad(newQuad, data);
            break;
          }
          case MemoryMapTypes::EContentType::ObstacleCubeRemoved:
            PRINT_NAMED_WARNING("MapComponent.AddObservableObject.AddedRemovalType",
                                "Called add on removal type rather than explicit RemoveObservableObject.");
            break;
          default:
            PRINT_NAMED_WARNING("MapComponent.AddObservableObject.AddedNonObservableType",
                                "AddObservableObject was called to add a non observable object");
            memoryMap->AddQuad(newQuad, MemoryMapData(addType, _robot->GetLastImageTimeStamp()));
            break;
        }
        
        // store in as a reported pose
        _reportedPoses[objectId][originID] = PoseInMapInfo(newPoseWrtOrigin, true);
        
        // since we added an obstacle, any borders we saw while dropping it should not be interesting
        const float kScaledQuadToIncludeEdges = 2.0f;
        // kScaledQuadToIncludeEdges: we want to consider interesting edges around this obstacle as non-interesting,
        // since we know they belong to this object. The quad to search for these edges has to be equal to the
        // obstacle quad plus the margin in which we would find edges. For example, a good tight limit would be the size
        // of the smallest quad in the memory map, since edges should be adjacent to the cube. This quad however is merely
        // to limit the search for interesting edges, so it being bigger than the tightest threshold should not
        // incur in a big penalty hit
        const Quad2f& edgeQuad = newQuad.GetScaled(kScaledQuadToIncludeEdges);
        if (memoryMap)
        {
          ReviewInterestingEdges(edgeQuad, memoryMap);
        }
      }
    }
    else
    {
      // should not happen, so warn about it
      PRINT_NAMED_WARNING("MapComponent.AddObservableObject.InvalidPose",
                          "Could not get object's new pose wrt robot. Won't add to map");
    }
  }
  else
  {
    // if the map was removed (for zombies), we shouldn't be asking to add an object to it
    DEV_ASSERT(matchPair == _navMaps.end(), "MapComponent.AddObservableObject.NoMapForOrigin");
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MapComponent::RemoveObservableObject(const ObservableObject& object, PoseOriginID_t originID)
{
  using namespace MemoryMapTypes;
    
  const ObjectFamily objectFam = object.GetFamily();
  const MemoryMapTypes::EContentType removalType = ObjectFamilyToMemoryMapContentType(objectFam, false);
  if ( removalType == MemoryMapTypes::EContentType::Unknown )
  {
    // this is not ok, this obstacle family can be added but can't be removed from the map
    PRINT_NAMED_WARNING("MapComponent.RemoveObservableObject.InvalidRemovalType",
                        "Family '%s' does not have a removal type in memory map",
                        ObjectFamilyToString(objectFam) );
    return;
  }

  const ObjectID id = object.GetID();

  // find the memory map for the given origin
  auto matchPair = _navMaps.find(originID);
  if ( matchPair != _navMaps.end() )
  {
    TimeStamp_t timeStamp = _robot->GetLastImageTimeStamp();
    NodeTransformFunction transform = [id, removalType, timeStamp](MemoryMapDataPtr data)
    {
      if (data->type == MemoryMapTypes::EContentType::ObstacleCube) 
      {
        // eventually we will want to store multiple ID's to the node data in the case for multiple blocks
        // however, we have no mechanism for merging data, so for now we are just completely replacing
        // the NodeContent if the ID matches.
        const auto currentCubeData = MemoryMapData::MemoryMapDataCast<const MemoryMapData_ObservableObject>(data);
        if (currentCubeData->id == id) 
        {
          return std::make_shared<MemoryMapData>( MemoryMapData(removalType, timeStamp) ); 
        } 
      } 
      return data;
    };
     
    matchPair->second->TransformContent(transform);
  } else {
    // if the map was removed (for zombies), we shouldn't be asking to remove an object from it
    DEV_ASSERT(matchPair == _navMaps.end(), "MapComponent.RemoveObservableObject.NoMapForOrigin");
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MapComponent::UpdateOriginsOfObjects(PoseOriginID_t curOriginID, PoseOriginID_t relocalizedOriginID)
{
  // for every object in the current map, we have a decision to make. We are going to bring that memory map
  // into what is becoming the current one. That means also bringing the last reported pose of every object
  // onto the new map. The current map is obviously more up to date than the map we merge into, since the map
  // we merge into is map we identified a while ago. This means that if an object moved and we now know where
  // it is, the good pose is in the currentMap, not in the mapWeMergeInto. So, for every object in the currentMap
  // we are going to remove their pose from the mapWeMergeInto. This will make the map we merge into gain the new
  // info, at the same time that we remove info known to not be the most accurate
  
  // for every object in the current origin
  for ( auto& pairIdToPoseInfoByOrigin : _reportedPoses )
  {
    // find object in the world
    const ObservableObject* object = _robot->GetBlockWorld().GetLocatedObjectByID(pairIdToPoseInfoByOrigin.first);
    if ( nullptr == object )
    {
      PRINT_CH_INFO("MapComponent", "MapComponent.UpdateOriginsOfObjects.NotAnObject",
                    "Could not find object ID '%d' in MapComponent updating their quads", pairIdToPoseInfoByOrigin.first );
      continue;
    }
  
    // find the reported pose for this object in the current origin
    OriginToPoseInMapInfo& poseInfoByOriginForObj = pairIdToPoseInfoByOrigin.second;
    const auto& matchInCurOrigin = poseInfoByOriginForObj.find(curOriginID);
    const bool isObjectReportedInCurrent = (matchInCurOrigin != poseInfoByOriginForObj.end());
    if ( isObjectReportedInCurrent )
    {
      // we have an entry in the current origin. We don't care if `isInMap` is true or false. If it's true
      // it means we have a better position available in this frame, if it's false it means we saw the object
      // in this frame, but somehow it became unknown. If it became unknown, the position it had in the origin
      // we are relocalizing to is old and not to be trusted. This is the reason why we don't erase reported poses,
      // but rather flag them as !isInMap.
      // Additionally we don't have to worry about the container we are iterating changing, since iterators are not
      // affected by changing a boolean, but are if we erased from it.
      RemoveObservableObject(*object, relocalizedOriginID);
      
      // we are bringing over the current info into the relocalized origin, update the reported pose in the
      // relocalized origin to be that of the newest information
      poseInfoByOriginForObj[relocalizedOriginID].isInMap = matchInCurOrigin->second.isInMap;
      if ( matchInCurOrigin->second.isInMap ) {
        // bring over the pose if it's in map (otherwise we don't care about the pose)
        // when we bring it, flatten out to the relocalized origin
        DEV_ASSERT(_robot->GetPoseOriginList().GetOriginByID(relocalizedOriginID).HasSameRootAs(matchInCurOrigin->second.pose),
                   "MapComponent.UpdateOriginsOfObjects.PoseDidNotHookGrandpa");
        poseInfoByOriginForObj[relocalizedOriginID].pose = matchInCurOrigin->second.pose.GetWithRespectToRoot();
      }
      // also, erase the current origin from the reported poses of this object, since we will never use it after this
      // Note this should not alter the iterators we are using for _reportedPoses
      poseInfoByOriginForObj.erase(curOriginID);
    }
    else
    {
      // we don't have this object in the current memory map. The info from this object if at all is in the previous
      // origin (then one we are relocalizing to), or another origin not related to these two, do nothing in those
      // cases
    }
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MapComponent::ClearRobotToMarkers(const ObservableObject* object)
{
  using namespace MemoryMapTypes;
    
  // the newPose should be directly in the robot's origin
  DEV_ASSERT(object->GetPose().IsChildOf(_robot->GetWorldOrigin()),
             "MapComponent.ClearRobotToMarkers.ObservedObjectParentNotRobotOrigin");

  INavMap* currentNavMemoryMap = GetCurrentMemoryMap();
  
  // we are creating a quad projected on the ground from the robot to every marker we see. In order to do so
  // the bottom corners of the ground quad match the forward ones of the robot (robotQuad::xLeft). The names
  // cornerBR, cornerBL are the corners in the ground quads (BottomRight and BottomLeft).
  // Later on we will generate the top corners for the ground quad (cornerTL, corner TR)
  const Quad2f& robotQuad = _robot->GetBoundingQuadXY(_robot->GetPose().GetWithRespectToRoot());
  Point3f cornerBR{ robotQuad[Quad::TopLeft   ].x(), robotQuad[Quad::TopLeft ].y(), 0};
  Point3f cornerBL{ robotQuad[Quad::BottomLeft].x(), robotQuad[Quad::BottomLeft].y(), 0};

  // get the markers we have seen from this object
  std::vector<const Vision::KnownMarker*> observedMarkers;
  object->GetObservedMarkers(observedMarkers);

  for ( const auto& observedMarkerIt : observedMarkers )
  {
    // An observed marker. Assign the marker's bottom corners as the top corners for the ground quad
    // The names of the corners (cornerTL and cornerTR) are those of the ground quad: TopLeft and TopRight
    DEV_ASSERT(_robot->IsPoseInWorldOrigin(observedMarkerIt->GetPose()),
               "MapComponent.ClearRobotToMarkers.MarkerOriginShouldBeRobotOrigin");
    
    const Quad3f& markerCorners = observedMarkerIt->Get3dCorners(observedMarkerIt->GetPose().GetWithRespectToRoot());
    Point3f cornerTL = markerCorners[Quad::BottomLeft];
    Point3f cornerTR = markerCorners[Quad::BottomRight];
    
    // Create a quad between the bottom corners of a marker and the robot forward corners, and tell
    // the navmesh that it should be clear, since we saw the marker
    Quad2f clearVisionQuad { cornerTL, cornerBL, cornerTR, cornerBR };
    
    // update navmesh with a quadrilateral between the robot and the seen object
    currentNavMemoryMap->AddQuad(clearVisionQuad, MemoryMapData(EContentType::ClearOfObstacle, _robot->GetLastImageTimeStamp()));
    
    // also notify behavior whiteboard.
    // rsam: should this information be in the map instead of the whiteboard? It seems a stretch that
    // blockworld knows now about behaviors, maybe all this processing of quads should be done in a separate
    // robot component, like a VisualInformationProcessingComponent
    _robot->GetAIComponent().GetWhiteboard().ProcessClearQuad(clearVisionQuad);
  }
}


// NOTE: mrw: we probably want to separate the vision processing code into its own file at some point.

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
namespace {
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  inline Point3f EdgePointToPoint3f(const OverheadEdgePoint& point, const Pose3d& pose, float z=0.0f) {
    Point3f ret = pose * Point3f(point.position.x(), point.position.y(), z);
    return ret;
  }
} // anonymous namespace

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Result MapComponent::ProcessVisionOverheadEdges(const OverheadEdgeFrame& frameInfo)
{
  Result ret = RESULT_OK;
  if ( frameInfo.groundPlaneValid ) {
    if ( !frameInfo.chains.empty() ) {
      ret = AddVisionOverheadEdges(frameInfo);
    } else {
      // we expect lack of borders to be reported as !isBorder chains
      DEV_ASSERT(false, "ProcessVisionOverheadEdges.ValidPlaneWithNoChains");
    }
  } else {
    // ground plane was invalid (atm we don't use this). It's probably only useful if we are debug-rendering
    // the ground plane
    _robot->GetContext()->GetVizManager()->EraseSegments("MapComponent.AddVisionOverheadEdges");
  }
  return ret;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MapComponent::ReviewInterestingEdges(const Quad2f& withinQuad, INavMap* map)
{
  // Note1: Not using withinQuad, but should. I plan on using once the memory map allows local queries and
  // modifications. Leave here for legacy purposes. We surely enable it soon, because performance needs
  // improvement.
  // Note2: Actually FindBorder is very fast compared to having to check each node against the quad, depending
  // on how many nodes of each type there are (interesting vs quads within 'withinQuad'), so it can potentially
  // be faster depending on the case. Unless profiling shows up for this, no need to listen to Note

    
  // check if merge is enabled
  if ( !kReviewInterestingEdges ) {
    return;
  }
  
  // ask the memory map to do the merge
  // some implementations make require parameters like max distance to merge, but for now trust continuity
  if( map )
  {	    
    using namespace MemoryMapTypes;
    
    // interesting edges adjacent to any of these types will be deemed not interesting
    constexpr FullContentArray typesWhoseEdgesAreNotInteresting =
    {
      {EContentType::Unknown               , false},
      {EContentType::ClearOfObstacle       , false},
      {EContentType::ClearOfCliff          , false},
      {EContentType::ObstacleCube          , true },
      {EContentType::ObstacleCubeRemoved   , false},
      {EContentType::ObstacleCharger       , true },
      {EContentType::ObstacleChargerRemoved, true },
      {EContentType::ObstacleProx          , true },
      {EContentType::ObstacleUnrecognized  , true },
      {EContentType::Cliff                 , false},
      {EContentType::InterestingEdge       , false},
      {EContentType::NotInterestingEdge    , true }
    };
    static_assert(IsSequentialArray(typesWhoseEdgesAreNotInteresting),
      "This array does not define all types once and only once.");

    // fill border in memory map
    map->FillBorder(EContentType::InterestingEdge, typesWhoseEdgesAreNotInteresting, EContentType::NotInterestingEdge, _robot->GetLastImageTimeStamp());
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Result MapComponent::AddVisionOverheadEdges(const OverheadEdgeFrame& frameInfo)
{
  using namespace MemoryMapTypes;
    
  ANKI_CPU_PROFILE("MapComponent::AddVisionOverheadEdges");
  _robot->GetContext()->GetVizManager()->EraseSegments("MapComponent.AddVisionOverheadEdges");
  
  // check conditions to add edges
  DEV_ASSERT(!frameInfo.chains.empty(), "AddVisionOverheadEdges.NoEdges");
  DEV_ASSERT(frameInfo.groundPlaneValid, "AddVisionOverheadEdges.InvalidGroundPlane");
  
  // we are only processing edges for the memory map, so if there's no map, don't do anything
  INavMap* currentNavMemoryMap = GetCurrentMemoryMap();
  if( nullptr == currentNavMemoryMap && !kDebugRenderOverheadEdges )
  {
    return RESULT_OK;
  }
  const float kDebugRenderOverheadEdgesZ_mm = 31.0f;

  // grab the robot pose at the timestamp of this frame
  TimeStamp_t t;
  HistRobotState* histState = nullptr;
  HistStateKey histStateKey;
  const Result poseRet = _robot->GetStateHistory()->ComputeAndInsertStateAt(frameInfo.timestamp, t, &histState, &histStateKey, true);
  if(RESULT_FAIL_ORIGIN_MISMATCH == poseRet)
  {
    // "Failing" because of an origin mismatch is OK, so don't freak out, but don't
    // go any further either.
    return RESULT_OK;
  }
  
  const bool poseIsGood = ( RESULT_OK == poseRet ) && (histState != nullptr);
  if ( !poseIsGood ) {
    // this can happen if robot status messages are lost
    PRINT_CH_INFO("MapComponent", "MapComponent.AddVisionOverheadEdges.HistoricalPoseNotFound",
                  "Pose not found for timestamp %u (hist: %u to %u). Edges ignored for this timestamp.",
                  frameInfo.timestamp,
                  _robot->GetStateHistory()->GetOldestTimeStamp(),
                  _robot->GetStateHistory()->GetNewestTimeStamp());
    return RESULT_OK;
  }
  
  // If we can't transfor the observedPose to the current origin, it's ok, that means that the timestamp
  // for the edges we just received is from before delocalizing, so we should discard it.
  Pose3d observedPose;
  if ( !histState->GetPose().GetWithRespectTo( _robot->GetWorldOrigin(), observedPose) ) {
    PRINT_CH_INFO("MapComponent", "MapComponent.AddVisionOverheadEdges.NotInThisWorld",
                  "Received timestamp %d, but could not translate that timestamp into current origin.", frameInfo.timestamp);
    return RESULT_OK;
  }
  
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

  // quads that are clear, either because there are not borders behind them or from the camera to that border
  std::vector<Quad2f> visionQuadsClear;
 
  // detected borders are simply lines
  struct Segment {
    Segment(const Point2f& f, const Point2f& t) : from(f), to(t) {}
    Point2f from;
    Point2f to;
  };
  std::vector<Segment> visionSegmentsWithInterestingBorders;
  
  // we store the closest detected edge every time in the whiteboard
  float closestPointDist_mm2 = std::numeric_limits<float>::quiet_NaN();
  
  // iterate every chain finding contiguous segments
  for( const auto& chain : frameInfo.chains )
  {

    // debug render
    if ( kDebugRenderOverheadEdges )
    {
      // renders every segment reported by vision
      for (size_t i=0; i<chain.points.size()-1; ++i)
      {
        Point3f start = EdgePointToPoint3f(chain.points[i], observedPose, kDebugRenderOverheadEdgesZ_mm);
        Point3f end   = EdgePointToPoint3f(chain.points[i+1], observedPose, kDebugRenderOverheadEdgesZ_mm);
        ColorRGBA color = ((i%2) == 0) ? NamedColors::YELLOW : NamedColors::ORANGE;
        VizManager* vizManager = _robot->GetContext()->GetVizManager();
        vizManager->DrawSegment("MapComponent.AddVisionOverheadEdges", start, end, color, false);
      }
    }
    else if ( kDebugRenderOverheadEdgeBorderQuads || kDebugRenderOverheadEdgeClearQuads )
    {
      Point2f wrtOrigin2DTL = observedPose * Point3f(frameInfo.groundplane[Quad::TopLeft].x(),
                                                     frameInfo.groundplane[Quad::TopLeft].y(),
                                                     0.0f);
      Point2f wrtOrigin2DBL = observedPose * Point3f(frameInfo.groundplane[Quad::BottomLeft].x(),
                                                     frameInfo.groundplane[Quad::BottomLeft].y(),
                                                     0.0f);
      Point2f wrtOrigin2DTR = observedPose * Point3f(frameInfo.groundplane[Quad::TopRight].x(),
                                                     frameInfo.groundplane[Quad::TopRight].y(),
                                                     0.0f);
      Point2f wrtOrigin2DBR = observedPose * Point3f(frameInfo.groundplane[Quad::BottomRight].x(),
                                                     frameInfo.groundplane[Quad::BottomRight].y(),
                                                     0.0f);
      
      Quad2f groundPlaneWRTOrigin(wrtOrigin2DTL, wrtOrigin2DBL, wrtOrigin2DTR, wrtOrigin2DBR);
      VizManager* vizManager = _robot->GetContext()->GetVizManager();
      vizManager->DrawQuadAsSegments("MapComponent.AddVisionOverheadEdges", groundPlaneWRTOrigin, kDebugRenderOverheadEdgesZ_mm, NamedColors::BLACK, false);
    }

    DEV_ASSERT(chain.points.size() > 2,"AddVisionOverheadEdges.ChainWithTooLittlePoints");
    
    // when we are processing a non-edge chain, points can be discarded. Variables to track segments
    bool hasValidSegmentStart = false;
    Point3f segmentStart;
    bool hasValidSegmentEnd   = false;
    Point3f segmentEnd;
    Vec3f segmentNormal;
    size_t curPointInChainIdx = 0;
    do
    {
      // get candidate point to merge into previous segment
      Point3f candidate3D = EdgePointToPoint3f(chain.points[curPointInChainIdx], observedPose);
      
      // this is to prevent vision clear quads that cross an object from clearing that object. This could be
      // optimized by passing in a flag to AddQuad that tells the QuadTree that it should not override
      // these types. However, if we have not seen an edge, and we crossed an object, it can potentially clear
      // behind that object, which is equaly wrong. Ideally, HasCollisionWithRay would provide the closest
      // collision point to "from", so that we can clear up to that point, and discard any information after.
      // Consider that in the future if performance-wise it's ok top have these checks here, and the memory
      // map can efficiently figure out the order in which to check for collision (there's a fast check
      // I can think of that involves simply knowing from which quadrant the ray starts)
      
      bool occludedBeforeNearPlane = false;
      bool occludedInsideROI = false;
      Vec2f innerRayFrom = cameraOrigin; // it will be updated with the intersection with the near plane (if found)

      // - calculate occlusion between camera and near plane of ROI
      {
        // check if we cross something between the camera and the near plane (outside of ROI plane)
        // from camera to candidate
        kmRay2 fullRay;
        kmVec2 kmFrom{cameraOrigin.x(), cameraOrigin.y()};
        kmVec2 kmTo  {candidate3D.x() , candidate3D.y() };
        kmRay2FillWithEndpoints(&fullRay, &kmFrom, &kmTo);
        
        // near plane segment
        kmRay2 nearPlaneSegment;
        kmVec2 kmNearL{nearPlaneLeft.x() , nearPlaneLeft.y() };
        kmVec2 kmNearR{nearPlaneRight.x(), nearPlaneRight.y()};
        kmRay2FillWithEndpoints(&nearPlaneSegment, &kmNearL, &kmNearR);
        
        // find the intersection between the two
        kmVec2 rayAtNearPlane;
        const kmBool foundNearPlane = kmSegment2WithSegmentIntersection(&fullRay, &nearPlaneSegment , &rayAtNearPlane);
        if ( foundNearPlane )
        {
          /*
            Note on occludedBeforeNearPlane vs occludedInsideROI:
            We want to check two different zones: one from cameraOrigin to nearPlane and another from nearPlane to candidate3D. 
            The first one, being out of the current ground ROI can be more restrictive (fail on borders), since we
            literally have no information to back up a ClearOfObstacle, however the second one can't fail on borders,
            since borders are exactly what we are detecting, so the point can't become invalid when a border is detected.
            That should be the main difference between typesThatOccludeValidInfoOutOfROI vs typesThatOccludeValidInfoInsideROI.
          */
          // typesThatOccludeValidInfoOutOfROI = types to check against outside ground ROI:
          constexpr MemoryMapTypes::FullContentArray typesThatOccludeValidInfoOutOfROI =
          {
            {MemoryMapTypes::EContentType::Unknown               , false},
            {MemoryMapTypes::EContentType::ClearOfObstacle       , false},
            {MemoryMapTypes::EContentType::ClearOfCliff          , false},
            {MemoryMapTypes::EContentType::ObstacleCube          , true },
            {MemoryMapTypes::EContentType::ObstacleCubeRemoved   , false},
            {MemoryMapTypes::EContentType::ObstacleCharger       , true },
            {MemoryMapTypes::EContentType::ObstacleChargerRemoved, true },
            {MemoryMapTypes::EContentType::ObstacleProx          , true },
            {MemoryMapTypes::EContentType::ObstacleUnrecognized  , true },
            {MemoryMapTypes::EContentType::Cliff                 , true },
            {MemoryMapTypes::EContentType::InterestingEdge       , true },
            {MemoryMapTypes::EContentType::NotInterestingEdge    , true }
          };
          static_assert(MemoryMapTypes::IsSequentialArray(typesThatOccludeValidInfoOutOfROI),
            "This array does not define all types once and only once.");
          
          // check if it's occluded before the near plane
          const Vec2f& outerRayFrom = cameraOrigin;
          const Vec2f outerRayTo(rayAtNearPlane.x, rayAtNearPlane.y);
          occludedBeforeNearPlane = currentNavMemoryMap->HasCollisionRayWithTypes(outerRayFrom, outerRayTo, typesThatOccludeValidInfoOutOfROI);
          
          // update innerRayFrom so that the second ray (if needed) only checks the inside of the ROI plane
          innerRayFrom = outerRayTo; // start inner (innerFrom) where the outer ends (outerTo)
        }
      }
  
      // - calculate occlusion inside ROI
      if ( !occludedBeforeNearPlane )
      {
        /*
          Note on occludedBeforeNearPlane vs occludedInsideROI:
          We want to check two different zones: one from cameraOrigin to nearPlane and another from nearPlane to candidate3D. 
          The first one, being out of the current ground ROI can be more restrictive (fail on borders), since we
          literally have no information to back up a ClearOfObstacle, however the second one can't fail on borders,
          since borders are exactly what we are detecting, so the point can't become invalid when a border is detected.
          That should be the main difference between typesThatOccludeValidInfoOutOfROI vs typesThatOccludeValidInfoInsideROI.
        */
        // typesThatOccludeValidInfoInsideROI = types to check against inside ground ROI:
        constexpr MemoryMapTypes::FullContentArray typesThatOccludeValidInfoInsideROI =
        {
          {MemoryMapTypes::EContentType::Unknown               , false},
          {MemoryMapTypes::EContentType::ClearOfObstacle       , false},
          {MemoryMapTypes::EContentType::ClearOfCliff          , false},
          {MemoryMapTypes::EContentType::ObstacleCube          , true },
          {MemoryMapTypes::EContentType::ObstacleCubeRemoved   , false},
          {MemoryMapTypes::EContentType::ObstacleCharger       , true },
          {MemoryMapTypes::EContentType::ObstacleChargerRemoved, true },
          {MemoryMapTypes::EContentType::ObstacleProx          , true },
          {MemoryMapTypes::EContentType::ObstacleUnrecognized  , true },
          {MemoryMapTypes::EContentType::Cliff                 , false},
          {MemoryMapTypes::EContentType::InterestingEdge       , false },
          {MemoryMapTypes::EContentType::NotInterestingEdge    , false }
        };
        static_assert(MemoryMapTypes::IsSequentialArray(typesThatOccludeValidInfoInsideROI),
          "This array does not define all types once and only once.");
        
        // Vec2f innerRayFrom: already calculated for us
        const Vec2f& innerRayTo = candidate3D;
        occludedInsideROI = currentNavMemoryMap->HasCollisionRayWithTypes(innerRayFrom, innerRayTo, typesThatOccludeValidInfoInsideROI);
      }
      
      // if we cross an object, ignore this point, regardless of whether we saw a border or not
      // this is because if we are crossing an object, chances are we are seeing its border, of we should have,
      // so the info is more often disrupting than helpful
      bool isValidPoint = !occludedBeforeNearPlane && !occludedInsideROI;
      
      // this flag is set by a point that can't merge into the previous segment and wants to start one
      // on its own
      bool shouldCreateNewSegment = false;

      // valid points have to be checked to see what to do with them
      if ( isValidPoint )
      {
        // this point is valid, check whether:
        // a) it's the first of a segment
        // b) it's the second of a segment, which defines the normal of the running segment
        // c) it can be merged into a running segment
        // d) it can't be merged into a running segment
        if ( !hasValidSegmentStart )
        {
          // it's the first of a segment
          segmentStart = candidate3D;
          hasValidSegmentStart = true;
        }
        else if ( !hasValidSegmentEnd )
        {
          // it's the second of a segment (defines normal)
          segmentEnd = candidate3D;
          hasValidSegmentEnd = true;
          // calculate normal now
          segmentNormal = (segmentEnd-segmentStart);
          segmentNormal.MakeUnitLength();
        }
        else
        {
          // there's a running segment already, check if we can merge into it
    
          // epsilon to merge points into the same edge segment. If adding a point to a segment creates
          // a deviation with respect to the first direction of the segment bigger than this epsilon,
          // then the point will not be added to that segment
          // cos(10deg) = 0.984807...
          // cos(20deg) = 0.939692...
          // cos(30deg) = 0.866025...
          // cos(40deg) = 0.766044...
          const float kDotBorderEpsilon = 0.7660f;
    
          // calculate the normal that this candidate would have with respect to the previous point
          Vec3f candidateNormal = candidate3D - segmentEnd;
          candidateNormal.MakeUnitLength();
      
          // check the dot product of that normal with respect to the running segment's normal
          const float dotProduct = DotProduct(segmentNormal, candidateNormal);
          bool canMerge = (dotProduct >= kDotBorderEpsilon); // if dotProduct is bigger, angle between is smaller
          if ( canMerge )
          {
            // it can merge into the previous point because the angle between the running normal and the new one
            // is within the threshold. Make this point the new end and update the running normal
            segmentEnd = candidate3D;
            segmentNormal = candidateNormal;
          }
          else
          {
            // it can't merge into the previous segment, set the flag that we want a new segment
            shouldCreateNewSegment = true;
          }
        }
        
        // store distance to valid points that belong to a detected border for behaviors quick access
        if ( chain.isBorder )
        {
          // compute distance from current robot position to the candidate3D, because whiteboard likes to know
          // the closest border at all times
          const Pose3d& currentRobotPose = _robot->GetPose();
          const float candidateDist_mm2 = (currentRobotPose.GetTranslation() - candidate3D).LengthSq();
          if ( std::isnan(closestPointDist_mm2) || FLT_LT(candidateDist_mm2, closestPointDist_mm2) )
          {
            closestPointDist_mm2 = candidateDist_mm2;
          }
        }
      }

      // should we send the segment we have so far as a quad to the memory map?
      const bool isLastPoint = (curPointInChainIdx == (chain.points.size()-1));
      const bool sendSegmentToMap = shouldCreateNewSegment || isLastPoint || !isValidPoint;
      if ( sendSegmentToMap )
      {
        // check if we have a valid segment so far
        const bool isValidSegment = hasValidSegmentStart && hasValidSegmentEnd;
        if ( isValidSegment )
        {
          // min length of the segment so that we can discard noise
          const float segLenSQ_mm = (segmentStart - segmentEnd).LengthSq();
          const bool isLongSegment = FLT_GT(segLenSQ_mm, kOverheadEdgeSegmentNoiseLen_mm);
          if ( isLongSegment )
          {
            // we have a valid and long segment add clear from camera to segment
            Quad2f clearQuad = { segmentStart, cameraOrigin, segmentEnd, cameraOrigin }; // TL, BL, TR, BR
            bool success = GroundPlaneROI::ClampQuad(clearQuad, nearPlaneLeft, nearPlaneRight);
            DEV_ASSERT(success, "AddVisionOverheadEdges.FailedQuadClamp");
            if ( success ) {
              visionQuadsClear.emplace_back(clearQuad);
            }
            // if it's a detected border, add the segment
            if ( chain.isBorder ) {
              visionSegmentsWithInterestingBorders.emplace_back(segmentStart, segmentEnd);
            }
          }
        }
        // else { not enough points in the segment to send. That's ok, just do not send }
        
        // if it was a valid point but could not merge, it wanted to start a new segment
        if ( shouldCreateNewSegment )
        {
          // if we can reuse the last point from the previous segment
          if ( hasValidSegmentEnd )
          {
            // then that one becomes the start
            // and we become the end of the new segment
            segmentStart = segmentEnd;
            hasValidSegmentStart = true;
            segmentEnd = candidate3D;
            hasValidSegmentEnd = true;
            // and update normal for this new segment
            segmentNormal = (segmentEnd-segmentStart);
            segmentNormal.MakeUnitLength();
          }
          else
          {
            // need to create a new segment, and there was not a valid one previously
            // this should be impossible, since we would have become either start or end
            // of a segment, and shouldCreateNewSegment would have been false
            DEV_ASSERT(false, "AddVisionOverheadEdges.NewSegmentCouldNotFindPreviousEnd");
          }
        }
        else
        {
          // we don't want to start a new segment, either we are the last point or we are not a valid point
          DEV_ASSERT(!isValidPoint || isLastPoint, "AddVisionOverheadEdges.ValidPointNotStartingSegment");
          hasValidSegmentStart = false;
          hasValidSegmentEnd   = false;
        }
      } // sendSegmentToMap?
        
      // move to next point
      ++curPointInChainIdx;
    } while (curPointInChainIdx < chain.points.size()); // while we still have points
  }

  // send clear quads/triangles to memory map
  // clear information should be quads, since the ground plane has a min dist that truncates the cone that spans
  // from the camera to the max ground plane distance. If the quad close segment is short though, it becomes
  // narrower, thus more similar to a triangle. As a performance optimization we are going to consider narrow
  // quads as triangles. This should be ok since the information is currently stored in quads that have a minimum
  // size, so the likelyhood that a quad that should be covered with the quads won't be hit by the triangles is
  // very small, and we are willing to take that risk to optimize this.
  /*
        Quad as it should be         Quad split into 3 quads with
                                        narrow close segment
                v                                 v
             ________                         __     --
             \      /                         \ |---| /
              \    /                           \ | | /
               \__/                             \_|_/
   
  */
  // for the same reason, if the far edge is too small, the triangle is very narrow, so it can be turned into a line,
  // and thanks to intersection we will probably not notice anything
  
  // Any quad whose closest edge is smaller than kOverheadEdgeNarrowCone_mm will be considered a triangle
  // const float minFarLenToBeReportedSq = kOverheadEdgeFarMinLenForClearReport_mm*kOverheadEdgeFarMinLenForClearReport_mm;
  const float maxFarLenToBeLineSq = kOverheadEdgeFarMaxLenForLine_mm*kOverheadEdgeFarMaxLenForLine_mm;
  const float maxCloseLenToBeTriangleSq = kOverheadEdgeCloseMaxLenForTriangle_mm*kOverheadEdgeCloseMaxLenForTriangle_mm;
  for ( const auto& potentialClearQuad2D : visionQuadsClear )
  {
  
// rsam note: I want to filter out small ones, but there were instances were this was not very good, for example if it
// happened at the border, since we would completely miss it. I will leave this out unless profiling says we need
// more optimizations
//      // quads that are too small are discarded. This is because in the general case they will be covered by nearby
//      // borders. If we discarded all because detection was too fine-grained, then we could run this loop again
//      // without this min restriction, but I don't think it will be an issue based on my tests, and the fact that we
//      // care more about big detectable borders, rather than small differences in the image (for example, we want
//      // to detect objects in real life that are similar to Cozmo's size)
//      const float farLenSq   = (potentialClearQuad2D.GetTopLeft() - potentialClearQuad2D.GetTopRight()).LengthSq();
//      const bool isTooSmall = FLT_LE(farLenSq, minFarLenToBeReportedSq);
//      if ( isTooSmall ) {
//        if ( kDebugRenderOverheadEdgeClearQuads )
//        {
//          ColorRGBA color = Anki::NamedColors::DARKGRAY;
//          VizManager* vizManager = _robot->GetContext()->GetVizManager();
//          vizManager->DrawQuadAsSegments("MapComponent.AddVisionOverheadEdges", potentialClearQuad2D, kDebugRenderOverheadEdgesZ_mm, color, false);
//        }
//        continue;
//      }
    const float farLenSq   = (potentialClearQuad2D.GetTopLeft() - potentialClearQuad2D.GetTopRight()).LengthSq();
    
    // test whether we can report as Line, Triangle or Quad
    const bool isLine = FLT_LE(farLenSq, maxFarLenToBeLineSq);
    if ( isLine )
    {
      // far segment is small enough that a single line would be fine
      const Point2f& clearFrom = (potentialClearQuad2D.GetBottomLeft() + potentialClearQuad2D.GetBottomRight()) * 0.5f;
      const Point2f& clearTo   = (potentialClearQuad2D.GetTopLeft()    + potentialClearQuad2D.GetTopRight()   ) * 0.5f;
    
      if ( kDebugRenderOverheadEdgeClearQuads )
      {
        ColorRGBA color = Anki::NamedColors::CYAN;
        VizManager* vizManager = _robot->GetContext()->GetVizManager();
        vizManager->DrawSegment("MapComponent.AddVisionOverheadEdges",
          Point3f(clearFrom.x(), clearFrom.y(), kDebugRenderOverheadEdgesZ_mm),
          Point3f(clearTo.x(), clearTo.y(), kDebugRenderOverheadEdgesZ_mm),
          color, false);
      }

      // add clear info to map
      if ( currentNavMemoryMap ) {
        currentNavMemoryMap->AddLine(clearFrom, clearTo, MemoryMapData(EContentType::ClearOfObstacle, frameInfo.timestamp));
      }
    }
    else
    {
      const float closeLenSq = (potentialClearQuad2D.GetBottomLeft() - potentialClearQuad2D.GetBottomRight()).LengthSq();
      const bool isTriangle = FLT_LE(closeLenSq, maxCloseLenToBeTriangleSq);
      if ( isTriangle )
      {
        // far segment is big, but close one is small enough that a triangle would be fine
        const Point2f& triangleClosePoint = (potentialClearQuad2D.GetBottomLeft() + potentialClearQuad2D.GetBottomRight()) * 0.5f;
        
        Triangle2f clearTri2D(triangleClosePoint, potentialClearQuad2D.GetTopLeft(), potentialClearQuad2D.GetTopRight());
        if ( kDebugRenderOverheadEdgeClearQuads )
        {
          ColorRGBA color = Anki::NamedColors::DARKGREEN;
          VizManager* vizManager = _robot->GetContext()->GetVizManager();
          vizManager->DrawSegment("MapComponent.AddVisionOverheadEdges",
            Point3f(clearTri2D[0].x(), clearTri2D[0].y(), kDebugRenderOverheadEdgesZ_mm),
            Point3f(clearTri2D[1].x(), clearTri2D[1].y(), kDebugRenderOverheadEdgesZ_mm),
            color, false);
          vizManager->DrawSegment("MapComponent.AddVisionOverheadEdges",
            Point3f(clearTri2D[1].x(), clearTri2D[1].y(), kDebugRenderOverheadEdgesZ_mm),
            Point3f(clearTri2D[2].x(), clearTri2D[2].y(), kDebugRenderOverheadEdgesZ_mm),
            color, false);
          vizManager->DrawSegment("MapComponent.AddVisionOverheadEdges",
            Point3f(clearTri2D[2].x(), clearTri2D[2].y(), kDebugRenderOverheadEdgesZ_mm),
            Point3f(clearTri2D[0].x(), clearTri2D[0].y(), kDebugRenderOverheadEdgesZ_mm),
            color, false);
        }

        // add clear info to map
        if ( currentNavMemoryMap ) {
          currentNavMemoryMap->AddTriangle(clearTri2D, MemoryMapData(EContentType::ClearOfObstacle, frameInfo.timestamp));
        }
      }
      else
      {
        // segments are too big, we need to report as quad
        if ( kDebugRenderOverheadEdgeClearQuads )
        {
          ColorRGBA color = Anki::NamedColors::GREEN;
          VizManager* vizManager = _robot->GetContext()->GetVizManager();
          vizManager->DrawQuadAsSegments("MapComponent.AddVisionOverheadEdges", potentialClearQuad2D, kDebugRenderOverheadEdgesZ_mm, color, false);
        }

        // add clear info to map
        if ( currentNavMemoryMap ) {
          currentNavMemoryMap->AddQuad(potentialClearQuad2D, MemoryMapData(EContentType::ClearOfObstacle, frameInfo.timestamp));
        }
      }
    }
    
    // also notify behavior whiteboard.
    // rsam: should this information be in the map instead of the whiteboard? It seems a stretch that
    // blockworld knows now about behaviors, maybe all this processing of quads should be done in a separate
    // robot component, like a VisualInformationProcessingComponent
    // Note: we always consider quad here since whiteboard does not need the triangle optiomization
    _robot->GetAIComponent().GetWhiteboard().ProcessClearQuad(potentialClearQuad2D);
  }
  
  // send border segments to memory map
  for ( const auto& borderSegment : visionSegmentsWithInterestingBorders )
  {
    if ( kDebugRenderOverheadEdgeBorderQuads )
    {
      ColorRGBA color = Anki::NamedColors::BLUE;
      VizManager* vizManager = _robot->GetContext()->GetVizManager();
      vizManager->DrawSegment("MapComponent.AddVisionOverheadEdges",
        Point3f(borderSegment.from.x(), borderSegment.from.y(), kDebugRenderOverheadEdgesZ_mm),
        Point3f(borderSegment.to.x(), borderSegment.to.y(), kDebugRenderOverheadEdgesZ_mm),
        color, false);
    }
  
    // add interesting edge
    if ( currentNavMemoryMap ) {
        currentNavMemoryMap->AddLine(borderSegment.from, borderSegment.to, MemoryMapData(EContentType::InterestingEdge, frameInfo.timestamp));
    }
  }
  
  // now merge interesting edges into non-interesting
  const bool addedEdges = !visionSegmentsWithInterestingBorders.empty();
  if ( addedEdges )
  {
    // TODO Optimization, build bounding box from detected edges, rather than doing the whole ground plane
    Point2f wrtOrigin2DTL = observedPose * Point3f(frameInfo.groundplane[Quad::TopLeft].x(),
                                                   frameInfo.groundplane[Quad::TopLeft].y(),
                                                   0.0f);
    Point2f wrtOrigin2DBL = observedPose * Point3f(frameInfo.groundplane[Quad::BottomLeft].x(),
                                                   frameInfo.groundplane[Quad::BottomLeft].y(),
                                                   0.0f);
    Point2f wrtOrigin2DTR = observedPose * Point3f(frameInfo.groundplane[Quad::TopRight].x(),
                                                   frameInfo.groundplane[Quad::TopRight].y(),
                                                   0.0f);
    Point2f wrtOrigin2DBR = observedPose * Point3f(frameInfo.groundplane[Quad::BottomRight].x(),
                                                   frameInfo.groundplane[Quad::BottomRight].y(),
                                                   0.0f);
    
    Quad2f groundPlaneWRTOrigin(wrtOrigin2DTL, wrtOrigin2DBL, wrtOrigin2DTR, wrtOrigin2DBR);
    if (currentNavMemoryMap)
    {
      ReviewInterestingEdges(groundPlaneWRTOrigin, currentNavMemoryMap);
    }
  }
  
  // notify the whiteboard we just processed edge information from a frame
  const float closestPointDist_mm = std::isnan(closestPointDist_mm2) ?
    std::numeric_limits<float>::quiet_NaN() : sqrt(closestPointDist_mm2);
  _robot->GetAIComponent().GetWhiteboard().SetLastEdgeInformation(frameInfo.timestamp, closestPointDist_mm);
  
  return RESULT_OK;
}


}
}

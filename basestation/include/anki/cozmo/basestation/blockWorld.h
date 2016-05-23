/**
 * File: blockWorld.h
 *
 * Author: Andrew Stein (andrew)
 * Created: 10/1/2013
 *
 * Description: Defines a container for tracking the state of all objects in Cozmo's world.
 *
 * Copyright: Anki, Inc. 2013
 *
 **/

#ifndef ANKI_COZMO_BLOCKWORLD_H
#define ANKI_COZMO_BLOCKWORLD_H

#include <queue>
#include <map>
#include <vector>

#include "anki/common/types.h"
#include "anki/common/basestation/exceptions.h"

#include "anki/vision/basestation/observableObjectLibrary.h"
#include "anki/cozmo/basestation/namedColors/namedColors.h"
#include "anki/cozmo/basestation/activeCube.h"
#include "anki/cozmo/basestation/block.h"
#include "anki/cozmo/basestation/overheadEdge.h"
#include "anki/cozmo/basestation/mat.h"
#include "anki/cozmo/basestation/blockWorldFilter.h"
#include "util/signals/simpleSignal_fwd.h"
#include "clad/types/actionTypes.h"

#include <vector>

namespace Anki
{
  namespace Cozmo
  {
    // Forward declarations:
    class Robot;
    class RobotManager;
    class RobotMessageHandler;
    class ActiveCube;
    class IExternalInterface;
    class INavMemoryMap;
    
    class BlockWorld
    {
    public:
      
      using ObjectsMapByID_t     = std::map<ObjectID, ObservableObject*>;
      using ObjectsMapByType_t   = std::map<ObjectType, ObjectsMapByID_t >;
      using ObjectsMapByFamily_t = std::map<ObjectFamily, ObjectsMapByType_t>;
      
      using ObservableObjectLibrary = Vision::ObservableObjectLibrary<ObservableObject>;
      
      BlockWorld(Robot* robot);
      ~BlockWorld();
      
      // Update the BlockWorld's state by processing all queued ObservedMarkers
      // and updating robots' poses and blocks' poses from them.
      Result Update();
      
      // Empties the queue of all observed markers
      void ClearAllObservedMarkers();
      
      Result QueueObservedMarker(HistPoseKey& poseKey, Vision::ObservedMarker& marker);

      // Adds a proximity obstacle (like random objects detected in front of the robot with the IR sensor) at the given pose.
      Result AddProxObstacle(const Pose3d& p);
      
      // Adds a cliff (detected with cliff detector)
      Result AddCliff(const Pose3d& p);
      
      // Processes the edges found in the given frame
      Result ProcessVisionOverheadEdges(const OverheadEdgeFrame& frameInfo);
      
      // Adds an active object of the appropriate type based on factoryID at
      // an unknown pose. To be used when the active object first comes into radio contact.
      // This function does nothing if an active object of the same type with the active ID already exists.
      ObjectID AddActiveObject(ActiveID activeID, FactoryID factoryID, ActiveObjectType activeObjectType);
      
      //
      // Object Access
      //
      
      // Clearing objects: all, by type, by family, or by ID.
      // NOTE: Clearing does not _delete_ an object; it marks its pose as unknown.
      void ClearAllExistingObjects();
      void ClearObjectsByFamily(const ObjectFamily family);
      void ClearObjectsByType(const ObjectType type);
      bool ClearObject(const ObjectID withID); // Returns true if object with ID is found and cleared, false otherwise.
      bool ClearObject(ObservableObject* object);
      

      // First clears the object and then actually deletes it, removing it from
      // BlockWorld entirely.
      bool DeleteObject(const ObjectID withID);
      
      void DeleteObjectsByFamily(const ObjectFamily family);
      void DeleteObjectsByType(const ObjectType type);
      
      // Get objects that exist in the world, by family, type, ID, etc.
      // NOTE: Like IDs, object types are unique across objects so they can be
      //       used without specifying which family.
      const ObservableObjectLibrary& GetObjectLibrary(ObjectFamily whichFamily) const;
      const ObjectsMapByFamily_t& GetAllExistingObjects() const;
      const ObjectsMapByType_t& GetExistingObjectsByFamily(const ObjectFamily whichFamily) const;
      const ObjectsMapByID_t& GetExistingObjectsByType(const ObjectType whichType) const;
      
      // Return a pointer to an object with the specified ID. If that object
      // does not exist, nullptr is returned.  Be sure to ALWAYS check
      // for the return being null!
      ObservableObject* GetObjectByID(const ObjectID objectID);
      const ObservableObject* GetObjectByID(const ObjectID objectID) const;
      
      // Same as above, but only searches a given family of objects
      ObservableObject* GetObjectByIDandFamily(const ObjectID objectID, const ObjectFamily inFamily);
      const ObservableObject* GetObjectByIDandFamily(const ObjectID objectID, const ObjectFamily inFamily) const;
      
      // Dynamically cast the given object ID into the templated active object type
      // Return nullptr on failure to find ActiveObject
      ActiveObject* GetActiveObjectByID(const ObjectID objectID, const ObjectFamily inFamily = ObjectFamily::Unknown);
      const ActiveObject* GetActiveObjectByID(const ObjectID objectID, const ObjectFamily inFamily = ObjectFamily::Unknown) const;
      
      // Same as above, but search by active ID instead of (BlockWorld-assigned) object ID.
      ActiveObject* GetActiveObjectByActiveID(const u32 activeID, const ObjectFamily inFamily = ObjectFamily::Unknown);
      const ActiveObject* GetActiveObjectByActiveID(const u32 activeID, const ObjectFamily inFamily = ObjectFamily::Unknown) const;
      
      
      // returns (in arguments) all objects matching a filter
      // NOTE: does not clear result (thus can be used multiple times with the same vector)
      void FindMatchingObjects(const BlockWorldFilter& filter, std::vector<ObservableObject*>& result) const;
      
      // Finds all blocks in the world whose centers are within the specified
      // heights off the ground (z dimension, relative to world origin!) and
      // returns a vector of quads of their outlines on the ground plane (z=0).
      // Can also pad the bounding boxes by a specified amount.
      // Optionally, will filter according to given BlockWorldFilter.
      void GetObjectBoundingBoxesXY(const f32 minHeight,
                                    const f32 maxHeight,
                                    const f32 padding,
                                    std::vector<std::pair<Quad2f,ObjectID> >& boundingBoxes,
                                    const BlockWorldFilter& filter = BlockWorldFilter()) const;

      // Finds an object nearest the specified distance (and optionally, rotation -- not implemented yet)
      // of the given pose. Returns nullptr if no objects match. Returns closest
      // if multiple matches are found.
      ObservableObject* FindObjectClosestTo(const Pose3d& pose,
                                            const BlockWorldFilter& filter = BlockWorldFilter()) const;

      ObservableObject* FindObjectClosestTo(const Pose3d& pose,
                                            const Vec3f&  distThreshold,
                                            const BlockWorldFilter& filter = BlockWorldFilter()) const;
      
      // Finds a matching object (one with the same type) that is closest to the
      // given object, within the specified distance and angle thresholds.
      // Returns nullptr if none found.
      ObservableObject* FindClosestMatchingObject(const ObservableObject& object,
                                                  const Vec3f& distThreshold,
                                                  const Radians& angleThreshold,
                                                  const BlockWorldFilter& filter = BlockWorldFilter());
      
      // Same as above, except type and pose are specified directly
      ObservableObject* FindClosestMatchingObject(ObjectType withType,
                                                  const Pose3d& pose,
                                                  const Vec3f& distThreshold,
                                                  const Radians& angleThreshold,
                                                  const BlockWorldFilter& filter = BlockWorldFilter());
      
      ObservableObject* FindMostRecentlyObservedObject(const BlockWorldFilter& filter = BlockWorldFilter()) const;
      
      // Finds existing objects whose XY bounding boxes intersect with objectSeen's
      // XY bounding box, with the exception of those that are of ignoreFamilies or
      // ignoreTypes.
      void FindIntersectingObjects(const ObservableObject* objectSeen,
                                   std::vector<ObservableObject*>& intersectingExistingObjects,
                                   f32 padding_mm,
                                   const BlockWorldFilter& filter = BlockWorldFilter()) const;
      
      void FindIntersectingObjects(const Quad2f& quad,
                                   std::vector<ObservableObject *> &intersectingExistingObjects,
                                   f32 padding,
                                   const BlockWorldFilter& filter = BlockWorldFilter()) const;
      
      // Returns true if there are remaining objects that the robot could potentially
      // localize to
      bool AnyRemainingLocalizableObjects() const;
      
      // Find an object on top of the given object, using a given height tolerance
      // between the top of the given object on bottom and the bottom of existing
      // candidate objects on top. Returns nullptr if no object is found.
      ObservableObject* FindObjectOnTopOf(const ObservableObject& objectOnBottom,
                                          f32 zTolerance) const;
      
      // Wrapper for above that returns bounding boxes of objects that are
      // obstacles given the robot's current z height. Objects being carried
      // and the object the robot is localized to are not considered obstacles.
      void GetObstacles(std::vector<std::pair<Quad2f,ObjectID> >& boundingBoxes,
                        const f32 padding = 0.f) const;
      
      // Get objects newly-observed or re-observed objects in the last Update.
      /*
      using ObservedObjectBoundingBoxes = std::vector<std::pair<ObjectID, Rectangle<f32> > >;
      const ObservedObjectBoundingBoxes& GetProjectedObservedObjects() const;
      const std::vector<ObjectID>& GetObservedObjectIDs() const;
      */
      
      // Returns true if any blocks were moved, added, or deleted on the
      // last call to Update().
      bool DidObjectsChange() const;
      
      // Get/Set currently-selected object
      ObjectID GetSelectedObject() const { return _selectedObject; }
      void     CycleSelectedObject();
      
      // Try to select the object with the specified ID. Return true if that
      // object ID is found and the object is successfully selected.
      bool SelectObject(const ObjectID objectID);
      void DeselectCurrentObject();
      
      void EnableObjectDeletion(bool enable);
      void EnableObjectAddition(bool enable);
      
      // Find all objects with the given parent and update them to have flatten
      // their objects w.r.t. the origin. Call this when the robot rejiggers
      // origins.
      Result UpdateObjectOrigins(const Pose3d* oldOrigin,
                                 const Pose3d* newOrigin);
      
      // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
      // Navigation memory
      // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

      // return pointer to current INavMemoryMap (it may be null if not enabled)
      const INavMemoryMap* GetNavMemoryMap() const;
      INavMemoryMap* GetNavMemoryMap();
      
      // update memory map
      void UpdateNavMemoryMap();      

      // create a new memory map from current robot frame of reference. The pointer is used as an identifier
      void CreateLocalizedMemoryMap(const Pose3d* worldOriginPtr);
      
      // Visualize the navigation memory information
      void DrawNavMemoryMap() const;
      
      //
      // Visualization
      //

      void EnableDraw(bool on);

      // Visualize markers in image display
      void DrawObsMarkers() const;
      
      // Call every existing object's Visualize() method and call the
      // VisualizePreActionPoses() on the currently-selected ActionableObject.
      void DrawAllObjects() const;
      
    protected:
      
      // Typedefs / Aliases
      //using ObsMarkerContainer_t = std::multiset<Vision::ObservedMarker, Vision::ObservedMarker::Sorter()>;
      //using ObsMarkerList_t = std::list<Vision::ObservedMarker>;
      using PoseKeyObsMarkerMap_t = std::multimap<HistPoseKey, Vision::ObservedMarker>;
      using ObsMarkerListMap_t = std::map<TimeStamp_t, PoseKeyObsMarkerMap_t>;
      
      //
      // Member Methods
      //
      
      // Note these are marked const but return non-const pointers.
      ObservableObject* GetObjectByIdHelper(const ObjectID objectID) const;
      ObservableObject* GetObjectByIDandFamilyHelper(const ObjectID objectID, const ObjectFamily inFamily) const;
      ActiveObject* GetActiveObjectByIDHelper(const ObjectID objectID, const ObjectFamily inFamily) const;
      ActiveObject* GetActiveObjectByActiveIDHelper(const u32 activeID, const ObjectFamily inFamily) const;
      
      bool UpdateRobotPose(PoseKeyObsMarkerMap_t& obsMarkers, const TimeStamp_t atTimestamp);
      
      Result UpdateObjectPoses(PoseKeyObsMarkerMap_t& obsMarkersAtTimestamp,
                               const ObjectFamily& inFamily,
                               const TimeStamp_t atTimestamp);
      
      /*
      // Adds/Removes proxObstacles based on current sensor readings and age of existing proxObstacles
      Result UpdateProxObstaclePoses();
      */

      // Finds existing objects that overlap with and are of the same type as objectSeen,
      // where overlap is defined by the IsSameAs() function.
      void FindOverlappingObjects(const ObservableObject* objectSeen,
                                  const ObjectsMapByType_t& objectsExisting,
                                  std::vector<ObservableObject*>& overlappingExistingObjects) const;
      
      void FindOverlappingObjects(const ObservableObject* objectExisting,
                                  const std::vector<ObservableObject*>& objectsSeen,
                                  std::vector<ObservableObject*>& overlappingSeenObjects) const;
      
      void FindOverlappingObjects(const ObservableObject* objectExisting,
                                  const std::multimap<f32, ObservableObject*>& objectsSeen,
                                  std::vector<ObservableObject*>& overlappingSeenObjects) const;
      
      // Helper for removing markers that are inside other detected markers
      static void RemoveMarkersWithinMarkers(PoseKeyObsMarkerMap_t& currentObsMarkers);
      
      // 1. Looks for objects that should have been seen (markers should have been visible
      //    but something was seen through/behind their last known location) and delete
      //    them.
      // 2. Looks for objects whose markers are not visible but which still have
      //    a corner in the camera's field of view, so the _object_ is technically
      //    still visible. Return the number of these.
      u32 CheckForUnobservedObjects(TimeStamp_t atTimestamp);
      
      // Helpers for actually inserting a new object into a new family using
      // its type and ID. Object's ID will be set if it isn't already.
      void AddNewObject(ObservableObject* object);
      void AddNewObject(ObjectsMapByType_t& existingFamily, ObservableObject* object);
      
      //template<class ObjectType>
      Result AddAndUpdateObjects(const std::multimap<f32, ObservableObject*>& objectsSeen,
                                 const ObjectFamily& inFamily,
                                 const TimeStamp_t atTimestamp);
      
      // Remove all posekey-marker pairs from the map if marker is marked used
      void RemoveUsedMarkers(PoseKeyObsMarkerMap_t& poseKeyObsMarkerMap);

      // adds a markerless object at the given pose
      Result AddMarkerlessObject(const Pose3d& pose);
      
      // Generates a list of ObservedMarker pointers that reference the actual ObservedMarkers
      // stored in poseKeyObsMarkerMap
      void GetObsMarkerList(const PoseKeyObsMarkerMap_t& poseKeyObsMarkerMap,
                            std::list<Vision::ObservedMarker*>& lst);
      
      void ClearObjectHelper(ObservableObject* object);
      
      // Delete an object when you have a direct iterator pointing to it. Returns
      // the iterator to the next object in the container.
      ObjectsMapByID_t::iterator DeleteObject(const ObjectsMapByID_t::iterator objIter,
                                              const ObjectType&    withType,
                                              const ObjectFamily&  fromFamily);
      
      Result BroadcastObjectObservation(const ObservableObject* observedObject,
                                        bool markersVisible);
      
      using FindFcn = std::function<bool(ObservableObject* current, ObservableObject* best)>;
      
      ObservableObject* FindObjectHelper(FindFcn findFcn, const BlockWorldFilter& filter = BlockWorldFilter(),
                                         bool returnFirstFound = false) const;
      
      void SetupEventHandlers(IExternalInterface& externalInterface);
      
      // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
      // Vision border detection
      // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
      
      // adds edges from the given frame to the world info
      Result AddVisionOverheadEdges(const OverheadEdgeFrame& frameInfo);
      
      //
      // Member Variables
      //
      
      Robot*             _robot;
      
      ObsMarkerListMap_t _obsMarkers;
      
      // A place to keep up with all objects' IDs and bounding boxes observed
      // in a single call to Update()
      //ObservedObjectBoundingBoxes _obsProjectedObjects;
      //std::vector<ObjectID> _currentObservedObjectIDs;
      
      // Store all known observable objects (these are everything we know about,
      // separated by class of object, not necessarily what we've actually seen
      // yet, but what everything we are aware of)
      std::map<ObjectFamily, ObservableObjectLibrary> _objectLibrary;
      //Vision::Vision::ObservableObjectLibrary blockLibrary_;
      //Vision::Vision::ObservableObjectLibrary matLibrary_;
      //Vision::Vision::ObservableObjectLibrary rampLibrary_;
      
      
      // Store all observed objects, indexed first by Type, then by ID
      // NOTE: If a new ObjectsMap_t is added here, a pointer to it needs to
      //   be stored in allExistingObjects_ (below), initialized in the
      //   BlockWorld cosntructor.
      ObjectsMapByFamily_t _existingObjects;
      // ObjectsMapByID_t _existingObjectsByID; TODO: flat list for faster finds?
      
      //ObjectsMap_t existingBlocks_;
      //ObjectsMap_t existingMatPieces_;
      //ObjectsMap_t existingRamps_;
      
      // An array storing pointers to all the ObjectsMap_t's above so that we
      // we can easily loop over all types of objects.
      //std::array<ObjectsMap_t*, 3> allExistingObjects_;
      
      bool _didObjectsChange;
      bool _canDeleteObjects;
      bool _canAddObjects;
      
      ObjectID _selectedObject;

      // For tracking, keep track of the id of the actions we are doing
      u32 _lastTrackingActionTag = static_cast<u32>(ActionConstants::INVALID_TAG);
      
      // Map the world knows the robot has traveled
      using NavMemoryMapTable = std::map<const Pose3d*, std::unique_ptr<INavMemoryMap>>;
      NavMemoryMapTable _navMemoryMaps;
      const Pose3d* _currentNavMemoryMapOrigin;
      
      // For allowing the calling of VizManager draw functions
      bool _enableDraw;
      
      std::set<ObjectID> _unidentifiedActiveObjects;
      
      std::vector<Signal::SmartHandle> _eventHandles;
      
      // Contains the list of added/updated objects from the last Update()
      std::list<ObservableObject*> _currentObservedObjects;
      
    }; // class BlockWorld

    
    inline const BlockWorld::ObservableObjectLibrary& BlockWorld::GetObjectLibrary(ObjectFamily whichFamily) const
    {
      auto objectsWithFamilyIter = _objectLibrary.find(whichFamily);
      if(objectsWithFamilyIter != _objectLibrary.end()) {
        return objectsWithFamilyIter->second;
      } else {
        static const ObservableObjectLibrary EmptyObjectLibrary;
        return EmptyObjectLibrary;
      }
    }
    
    inline const BlockWorld::ObjectsMapByFamily_t& BlockWorld::GetAllExistingObjects() const
    {
      return _existingObjects;
    }
    
    inline const BlockWorld::ObjectsMapByType_t& BlockWorld::GetExistingObjectsByFamily(const ObjectFamily whichFamily) const
    {
      auto objectsWithFamilyIter = _existingObjects.find(whichFamily);
      if(objectsWithFamilyIter != _existingObjects.end()) {
        return objectsWithFamilyIter->second;
      } else {
        static const BlockWorld::ObjectsMapByType_t EmptyObjectMapByType;
        return EmptyObjectMapByType;
      }
    }
    
    inline const BlockWorld::ObjectsMapByID_t& BlockWorld::GetExistingObjectsByType(const ObjectType whichType) const
    {
      for(auto & objectsByFamily : _existingObjects) {
        auto objectsWithType = objectsByFamily.second.find(whichType);
        if(objectsWithType != objectsByFamily.second.end()) {
          return objectsWithType->second;
        }
      }
      
      // Type not found!
      static const BlockWorld::ObjectsMapByID_t EmptyObjectMapByID;
      return EmptyObjectMapByID;
    }
    
    inline ObservableObject* BlockWorld::GetObjectByID(const ObjectID objectID) {
      return GetObjectByIdHelper(objectID); // returns non-const*
    }
    
    inline const ObservableObject* BlockWorld::GetObjectByID(const ObjectID objectID) const {
      return GetObjectByIdHelper(objectID); // returns const*
    }
    
    inline const ObservableObject* BlockWorld::GetObjectByIDandFamily(const ObjectID objectID, const ObjectFamily inFamily) const {
      return GetObjectByIDandFamilyHelper(objectID, inFamily); // returns const*
    }
    
    inline ObservableObject* BlockWorld::GetObjectByIDandFamily(const ObjectID objectID, const ObjectFamily inFamily) {
      return GetObjectByIDandFamilyHelper(objectID, inFamily); // returns non-const*
    }

    inline ActiveObject* BlockWorld::GetActiveObjectByID(const ObjectID objectID, const ObjectFamily inFamily) {
      return GetActiveObjectByIDHelper(objectID, inFamily); // returns non-const*
    }
    
    inline const ActiveObject* BlockWorld::GetActiveObjectByID(const ObjectID objectID, const ObjectFamily inFamily) const {
      return GetActiveObjectByIDHelper(objectID, inFamily); // returns const*
    }
    
    inline ActiveObject* BlockWorld::GetActiveObjectByActiveID(const u32 activeID, const ObjectFamily inFamily) {
      return GetActiveObjectByActiveIDHelper(activeID, inFamily); // returns non-const*
    }
    
    inline const ActiveObject* BlockWorld::GetActiveObjectByActiveID(const u32 activeID, const ObjectFamily inFamily) const {
      return GetActiveObjectByActiveIDHelper(activeID, inFamily); // returns const*
    }
    
    inline void BlockWorld::AddNewObject(ObservableObject* object)
    {
      AddNewObject(_existingObjects[object->GetFamily()], object);
    }

    /*
    inline const BlockWorld::ObservedObjectBoundingBoxes& BlockWorld::GetProjectedObservedObjects() const
    {
      return _obsProjectedObjects;
    }
     */
    
    /*
    inline const std::vector<ObjectID>& BlockWorld::GetObservedObjectIDs() const {
      return _currentObservedObjectIDs;
    }
     */
    
    inline void BlockWorld::EnableObjectAddition(bool enable) {
      _canAddObjects = enable;
    }
    
    inline void BlockWorld::EnableObjectDeletion(bool enable) {
      _canDeleteObjects = enable;
    }
    
  } // namespace Cozmo
} // namespace Anki



#endif // ANKI_COZMO_BLOCKWORLD_H

/**
 * File: faceWorld.h
 *
 * Author: Andrew Stein (andrew)
 * Created: 2014
 *
 * Description: Implements a container for mirroring on the main thread, the known faces 
 *              from the vision system (which generally runs on another thread).
 *
 * Copyright: Anki, Inc. 2014
 *
 **/


#ifndef __Anki_Cozmo_FaceWorld_H__
#define __Anki_Cozmo_FaceWorld_H__

#include "coretech/vision/engine/trackedFace.h"

#include "engine/ankiEventUtil.h"
#include "engine/smartFaceId.h"
#include "engine/viz/vizManager.h"

#include "clad/types/actionTypes.h"

#include <map>
#include <set>
#include <vector>

namespace Anki {
namespace Cozmo {
  
  // Forward declarations:
  class Robot;
  
  class FaceWorld
  {
  public:
    static const s32 MinTimesToSeeFace = 4;

    // NOTE: many functions in this API have two versions, one which takes a Vision::FaceID_t and one which
    // takes a SmartFaceID. The use of SmartFaceID is preferred because it automatically handles face id
    // changes and deleted faces. The raw face id API is maintained only for backwards
    // compatibility. COZMO-10839 is the task that will eventually remove this old interface
    
    FaceWorld(Robot& robot);
    
    Result Update(const std::list<Vision::TrackedFace>& observedFaces);
    Result AddOrUpdateFace(const Vision::TrackedFace& face);
  
    Result ChangeFaceID(const Vision::UpdatedFaceID& update);
    
    // Called when robot delocalizes
    void OnRobotDelocalized(PoseOriginID_t worldOriginID);
    
    // Called when Robot rejiggers its pose. Returns number of faces updated
    int UpdateFaceOrigins(PoseOriginID_t oldOriginID, PoseOriginID_t newOriginID);

    // create a smart face ID or update an existing ID from a raw ID (useful, for example for IDs from CLAD
    // messages)
    SmartFaceID GetSmartFaceID(Vision::FaceID_t faceID) const;
    void UpdateSmartFaceToID(const Vision::FaceID_t faceID, SmartFaceID& smartFaceID);

    // Returns nullptr if not found
    const Vision::TrackedFace* GetFace(Vision::FaceID_t faceID) const;
    const Vision::TrackedFace* GetFace(const SmartFaceID& faceID) const;
    
    // Returns set of face IDs present in the world.
    // Set includeRecognizableOnly=true to only return faces that have been (or can be) recognized.
    // NOTE: This does not necessarily mean they have been recognized as a _named_ person introduced via
    //       MeetCozmo. They could simply be recognized as a session-only person already seen in this session.
    std::set<Vision::FaceID_t> GetFaceIDs(bool includeRecognizableOnly = false) const;
    
    // Returns face IDs observed since seenSinceTime_ms (inclusive)
    std::set<Vision::FaceID_t> GetFaceIDsObservedSince(TimeStamp_t seenSinceTime_ms,
                                                       bool includeRecognizableOnly = false) const;

    // Returns true if any faces are in the world
    bool HasAnyFaces(TimeStamp_t seenSinceTime_ms = 0, bool includeRecognizableOnly = false) const;

    // If the robot has observed a face, sets poseWrtRobotOrigin to the pose of the last observed face
    // and returns the timestamp when that face was last seen. Otherwise, returns 0. Normally,
    // inRobotOrigin=true, so that the last observed pose is required to be w.r.t. the current origin.
    //
    // If inRobotOriginOnly=false, the returned pose is allowed to be that of a face observed w.r.t. a
    // different coordinate frame, modified such that its parent is the robot's current origin. This
    // could be a completely inaccurate guess for the last observed face pose, but may be "good enough"
    // for some uses.
    TimeStamp_t GetLastObservedFace(Pose3d& poseWrtRobotOrigin, bool inRobotOriginOnly = true) const;

    // Returns true if any action has turned towards this face
    bool HasTurnedTowardsFace(Vision::FaceID_t faceID) const;
    bool HasTurnedTowardsFace(const SmartFaceID& faceID) const;

    // Tell FaceWorld that the robot has turned towards this face (or not, if val=false)
    void SetTurnedTowardsFace(Vision::FaceID_t faceID, bool val = true);
    void SetTurnedTowardsFace(const SmartFaceID& faceID, bool val = true);
    
    // Removes all faces and resets the last observed face timer to 0, so
    // GetLastObservedFace() will return 0.
    void ClearAllFaces();
    
    // Specify a faceID to start an enrollment of a specific ID, i.e. with the intention
    // of naming that person.
    // Use UnknownFaceID to enable (or return to) ongoing "enrollment" of session-only / unnamed faces.
    void Enroll(Vision::FaceID_t faceID);
    void Enroll(const SmartFaceID& faceID);
    
    bool IsFaceEnrollmentComplete() const { return _lastEnrollmentCompleted; }
    void SetFaceEnrollmentComplete(bool complete) { _lastEnrollmentCompleted = complete; }
    
    // template for all events we subscribe to
    template<typename T>
    void HandleMessage(const T& msg);
    
  private:
    
    Robot& _robot;
    
    // FaceEntry is the internal storage for faces in FaceWorld, which include
    // the public-facing TrackedFace plus additional bookkeeping
    struct FaceEntry {
      Vision::TrackedFace      face;
      VizManager::Handle_t     vizHandle;
      s32                      numTimesObserved = 0;
      s32                      numTimesObservedFacingCamera = 0;
      bool                     hasTurnedTowards = false;

      FaceEntry(const Vision::TrackedFace& faceIn);
      bool IsNamed() const { return !face.GetName().empty(); }
      bool HasStableID() const;
    };
    
    using FaceContainer = std::map<Vision::FaceID_t, FaceEntry>;
    using FaceEntryIter = FaceContainer::iterator;
    
    FaceContainer _faceEntries;
    
    Vision::FaceID_t _idCtr = 0;
    
    Pose3d      _lastObservedFacePose;
    TimeStamp_t _lastObservedFaceTimeStamp = 0;
    
    bool _lastEnrollmentCompleted = false;
    
    // Helper used by public Get() methods to determine if an entry should be returned
    bool ShouldReturnFace(const FaceEntry& faceEntry, TimeStamp_t seenSinceTime_ms, bool includeRecognizableOnly) const;
    
    // Removes the face and advances the iterator. Notifies any listeners that
    // the face was removed if broadcast==true.
    void RemoveFace(FaceEntryIter& faceIter, bool broadcast = true);
    
    void RemoveFaceByID(Vision::FaceID_t faceID);

    void SetupEventHandlers(IExternalInterface& externalInterface);
    
    void DrawFace(FaceEntry& knownFace, bool drawInImage = true);
    void EraseFaceViz(FaceEntry& faceEntry);
    
    std::vector<Signal::SmartHandle> _eventHandles;
    
  }; // class FaceWorld
  
} // namespace Cozmo
} // namespace Anki

#endif // __Anki_Cozmo_FaceWorld_H__

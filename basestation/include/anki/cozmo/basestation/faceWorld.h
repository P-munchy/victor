#ifndef __Anki_Cozmo_FaceWorld_H__
#define __Anki_Cozmo_FaceWorld_H__

#include "anki/vision/basestation/trackedFace.h"

#include "anki/cozmo/basestation/viz/vizManager.h"

#include "clad/types/actionTypes.h"

#include <map>
#include <vector>

namespace Anki {
namespace Cozmo {
  
  // Forward declaration:
  class Robot;
  
  class FaceWorld
  {
  public:
    static const s32 MinTimesToSeeFace = 4;
    
    FaceWorld(Robot& robot);
    
    Result Update();
    Result AddOrUpdateFace(Vision::TrackedFace& face);
  
    // Returns nullptr if not found
    const Vision::TrackedFace* GetFace(Vision::TrackedFace::ID_t faceID) const;
    
    // Returns number of known faces
    // Actual face IDs returned in faceIDs
    std::vector<Vision::TrackedFace::ID_t> GetKnownFaceIDs() const;
    
    Vision::TrackedFace::ID_t GetOwnerID() const                            { return _ownerID; }
    void                      SetOwnerID(Vision::TrackedFace::ID_t ownerID) { _ownerID = ownerID; }
    
    // Returns number of known faces observed since seenSinceTime_ms
    std::map<TimeStamp_t, Vision::TrackedFace::ID_t> GetKnownFaceIDsObservedSince(TimeStamp_t seenSinceTime_ms) const;

    // If the robot has observed a face, sets p to the pose of the last observed face and returns the
    // timestamp when that face was last seen. Otherwise, returns 0    
    TimeStamp_t GetLastObservedFace(Pose3d& p) const;

    // like GetLastObservedFace, but returns a pose with respect to the current robot pose. If they have
    // different origins (e.g. the robot was picked up and hasn't seen a face since), this code will assume
    // that the origins are the same (even though they aren't).
    TimeStamp_t GetLastObservedFaceWithRespectToRobot(Pose3d& p) const;
    
    // Removes all known faces and resets the last observed face timer to 0, so
    // GetLastObservedFace() will return 0.
    void ClearAllFaces();
    
  private:
    
    Robot& _robot;
    
    Vision::TrackedFace::ID_t  _ownerID = Vision::TrackedFace::UnknownFace;
    
    struct KnownFace {
      Vision::TrackedFace      face;
      VizManager::Handle_t     vizHandle;
      s32                      numTimesObserved = 0;

      KnownFace(Vision::TrackedFace& faceIn);
    };
    
    using FaceContainer = std::map<Vision::TrackedFace::ID_t, KnownFace>;
    using KnownFaceIter = FaceContainer::iterator;
    FaceContainer _knownFaces;
    
    TimeStamp_t _deletionTimeout_ms = 4000;

    Vision::TrackedFace::ID_t _idCtr = 0;
    
    Pose3d      _lastObservedFacePose;
    TimeStamp_t _lastObservedFaceTimeStamp = 0;
    
    // The distance (in mm) threshold inside of which to head positions are considered to be the same face
    static constexpr float headCenterPointThreshold = 220.f;
    
    // Removes the face and advances the iterator. Notifies any listeners that
    // the face was removed.
    void RemoveFace(KnownFaceIter& faceIter);
    
    void RemoveFaceByID(Vision::TrackedFace::ID_t faceID);

    void SetupEventHandlers(IExternalInterface& externalInterface);
    
    std::vector<Signal::SmartHandle> _eventHandles;
    
  }; // class FaceWorld
  
} // namespace Cozmo
} // namespace Anki

#endif // __Anki_Cozmo_FaceWorld_H__

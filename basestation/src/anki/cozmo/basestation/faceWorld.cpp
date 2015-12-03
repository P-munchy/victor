#include "anki/cozmo/basestation/faceWorld.h"
#include "anki/cozmo/basestation/robot.h"
#include "anki/cozmo/basestation/externalInterface/externalInterface.h"
#include "anki/cozmo/basestation/cozmoActions.h"
#include "anki/common/basestation/math/point_impl.h"
#include "clad/externalInterface/messageEngineToGame.h"

#include "anki/cozmo/shared/cozmoConfig.h"

namespace Anki {
namespace Cozmo {
  
  FaceWorld::KnownFace::KnownFace(Vision::TrackedFace& faceIn)
  : face(faceIn)
  , vizHandle(VizManager::INVALID_HANDLE)
  {
  
  }

  
  FaceWorld::FaceWorld(Robot& robot)
  : _robot(robot)
  {
    
  }
  
  Result FaceWorld::UpdateFaceTracking(const Vision::TrackedFace& face)
  {
    const Vec3f& robotTrans = _robot.GetPose().GetTranslation();
    
    Pose3d headPose;
    if(false == face.GetHeadPose().GetWithRespectTo(*_robot.GetWorldOrigin(), headPose)) {
      PRINT_NAMED_ERROR("BlockWorld.UpdateTrackToObject",
                        "Could not get pose of observed marker w.r.t. world for head tracking.\n");
      return RESULT_FAIL;
    }
    
    const f32 xDist = headPose.GetTranslation().x() - robotTrans.x();
    const f32 yDist = headPose.GetTranslation().y() - robotTrans.y();
    
    const f32 minDist = std::sqrt(xDist*xDist + yDist*yDist);
  
    // NOTE: This isn't perfectly accurate since it doesn't take into account the
    // the head angle and is simply using the neck joint (which should also
    // probably be queried from the robot instead of using the constant here)
    const f32 zDist = headPose.GetTranslation().z() - (robotTrans.z() + NECK_JOINT_POSITION[2]);
    
    const Radians headAngle = std::atan(zDist/(minDist + 1e-6f));
    const Radians panAngle = std::atan2(yDist, xDist);
    
    static const Radians minHeadAngle(DEG_TO_RAD(1.f));
    static const Radians minBodyAngle(DEG_TO_RAD(1.f));
    
    PanAndTiltAction* action = new PanAndTiltAction(panAngle, headAngle, true, true);
    action->EnableMessageDisplay(false);
    action->SetPanTolerance(minBodyAngle);
    action->SetTiltTolerance(minHeadAngle);
    _robot.GetActionList().QueueActionNow(Robot::DriveAndManipulateSlot, action);
                                          
    return RESULT_OK;
  } // UpdateFaceTracking()
  
  Result FaceWorld::AddOrUpdateFace(Vision::TrackedFace& face)
  {
    // The incoming TrackedFace should be w.r.t. the arbitrary historical world origin.
    if(face.GetHeadPose().GetParent() == nullptr || !face.GetHeadPose().GetParent()->IsOrigin())
    {
      PRINT_NAMED_ERROR("FaceWorld.AddOrUpdateFace.BadPoseParent",
                        "TrackedFace's head pose parent should be an origin.");
      return RESULT_FAIL;
    }

    // Head pose is stored w.r.t. historical world origin, but needs its parent
    // set up to be the robot's world origin here:
    Pose3d headPose = face.GetHeadPose();
    headPose.SetParent(_robot.GetWorldOrigin());
    face.SetHeadPose(headPose);

    static const Point3f humanHeadSize{148.f, 225.f, 195.f};
    static const bool usePoseToMatchIDs = true; // if true, ignores IDs from face tracker
    
    KnownFace* knownFace = nullptr;
    
    if(usePoseToMatchIDs) {
      
      // Look through known faces and compare pose:
      bool foundMatch = false;
      for(auto knownFaceIter = _knownFaces.begin(); knownFaceIter != _knownFaces.end(); ++knownFaceIter)
      {
        // Note we're using really loose thresholds for checking pose sameness
        // since our ability to accurately localize face's 3D pose is limited.
        if(knownFaceIter->second.face.GetHeadPose().IsSameAs(face.GetHeadPose(),
                                                             humanHeadSize,
                                                             DEG_TO_RAD(90)))
        {
          knownFace = &knownFaceIter->second;
          
          const Vision::TrackedFace::ID_t matchedID = knownFace->face.GetID();
          //PRINT_NAMED_INFO("FaceWorld.UpdateFace.UpdatingKnownFace",
          //                 "Updating face with ID=%lld from t=%d to %d",
          //                 matchedID, knownFace->face.GetTimeStamp(), face.GetTimeStamp());
          knownFace->face = face;
          knownFace->face.SetID(matchedID);
          foundMatch = true;
          break;
        }
      }
      
      // Didn't find a match based on pose, so add a new face with a new ID:
      if(!foundMatch) {
        PRINT_NAMED_INFO("FaceWorld.UpdateFace.NewFace",
                         "Added new face with ID=%lld at t=%d.", _idCtr, face.GetTimeStamp());
        face.SetID(_idCtr); // Use our own ID here for the new face
        auto insertResult = _knownFaces.insert({_idCtr, face});
        if(insertResult.second == false) {
          PRINT_NAMED_ERROR("FaceWorld.UpdateFace.ExistingID",
                            "Did not find a match by pose, but ID %lld already in use.",
                            _idCtr);
          return RESULT_FAIL;
        }
        knownFace = &insertResult.first->second;
        ++_idCtr;
      }
      
    } else { // Use ID coming from face tracker / recognizer
      auto insertResult = _knownFaces.insert({face.GetID(), face});
      
      if(insertResult.second) {
        PRINT_NAMED_INFO("FaceWorld.UpdateFace.NewFace", "Added new face with ID=%lld at t=%d.", face.GetID(), face.GetTimeStamp());
      } else {
        // Update the existing face:
        insertResult.first->second = face;
      }
      
      knownFace = &insertResult.first->second;
    }
    
    // By now, we should have either created a new face or be pointing at an
    // existing one!
    assert(knownFace != nullptr);
    
    // Draw 3D face
    knownFace->vizHandle = VizManager::getInstance()->DrawHumanHead(1+static_cast<u32>(knownFace->face.GetID()),
                                                                   humanHeadSize,
                                                                   knownFace->face.GetHeadPose(),
                                                                   ::Anki::NamedColors::GREEN);
    
    if((_robot.GetMoveComponent().GetTrackToFace() != Vision::TrackedFace::UnknownFace) &&
       (_robot.GetMoveComponent().GetTrackToFace() == knownFace->face.GetID()))
    {
      UpdateFaceTracking(knownFace->face);
    }
    
    // Send out an event about this face being observed
    using namespace ExternalInterface;
    const Vec3f& trans = knownFace->face.GetHeadPose().GetTranslation();
    const UnitQuaternion<f32>& q = knownFace->face.GetHeadPose().GetRotation().GetQuaternion();
    _robot.Broadcast(MessageEngineToGame(RobotObservedFace(knownFace->face.GetID(),
                                                           _robot.GetID(),
                                                           face.GetTimeStamp(),
                                                           trans.x(),
                                                           trans.y(),
                                                           trans.z(),
                                                           q.w(),
                                                           q.x(),
                                                           q.y(),
                                                           q.z())));


    return RESULT_OK;
  }
  
  Result FaceWorld::Update()
  {
    // Delete any faces we haven't seen in awhile
    for(auto faceIter = _knownFaces.begin(); faceIter != _knownFaces.end(); )
    {
      if(_robot.GetLastImageTimeStamp() > _deletionTimeout_ms + faceIter->second.face.GetTimeStamp()) {
        
        PRINT_NAMED_INFO("FaceWorld.Update.DeletingFace",
                         "Removing face %llu at t=%d, because it hasn't been seen since t=%d.",
                         faceIter->first, _robot.GetLastImageTimeStamp(),
                         faceIter->second.face.GetTimeStamp());
        
        using namespace ExternalInterface;
        _robot.Broadcast(MessageEngineToGame(RobotDeletedFace(faceIter->second.face.GetID(), _robot.GetID())));
        
        VizManager::getInstance()->EraseVizObject(faceIter->second.vizHandle);
        faceIter = _knownFaces.erase(faceIter);

      } else {
        ++faceIter;
      }
    }
    
    return RESULT_OK;
  } // Update()
    
  const Vision::TrackedFace* FaceWorld::GetFace(Vision::TrackedFace::ID_t faceID) const
  {
    auto faceIter = _knownFaces.find(faceID);
    if(faceIter == _knownFaces.end()) {
      return nullptr;
    } else {
      return &faceIter->second.face;
    }
  }

  
} // namespace Cozmo
} // namespace Anki


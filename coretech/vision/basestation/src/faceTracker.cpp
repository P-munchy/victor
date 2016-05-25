/**
 * File: faceTracker.cpp
 *
 * Author: Andrew Stein
 * Date:   8/18/2015
 *
 * Description: Implements the wrappers for the private implementation of
 *              FaceTracker::Impl. The various implementations of Impl are 
 *              in separate faceTrackerImpl_<PROVIDER>.h files, which are 
 *              included here based on the defined FACE_TRACKER_PROVIDER.
 *
 * Copyright: Anki, Inc. 2015
 **/

#include "anki/vision/basestation/faceTracker.h"
#include "anki/vision/basestation/image.h"
#include "anki/vision/basestation/trackedFace.h"

#include "util/fileUtils/fileUtils.h"

#include <list>
#include <fstream>

#if FACE_TRACKER_PROVIDER == FACE_TRACKER_FACIOMETRIC || \
FACE_TRACKER_PROVIDER == FACE_TRACKER_OPENCV
  // Parameters used by cv::detectMultiScale for both of these face trackers
  static const f32 opencvDetectScaleFactor = 1.3f;
  static const cv::Size opencvDetectMinFaceSize(48,48);
#endif

#if FACE_TRACKER_PROVIDER == FACE_TRACKER_FACIOMETRIC
#  include "faceTrackerImpl_faciometric.h"
#elif FACE_TRACKER_PROVIDER == FACE_TRACKER_FACESDK
#  include "faceTrackerImpl_facesdk.h"
#elif FACE_TRACKER_PROVIDER == FACE_TRACKER_OKAO
#  include "faceTrackerImpl_okao.h"
#elif FACE_TRACKER_PROVIDER == FACE_TRACKER_OPENCV
#  include "faceTrackerImpl_opencv.h"
#else 
#  error Unknown FACE_TRACKER_PROVIDER set!
#endif

namespace Anki {
namespace Vision {
  
  FaceTracker::FaceTracker(const std::string& modelPath, const Json::Value& config)
  : _pImpl(new Impl(modelPath, config))
  {
    
  }
  
  FaceTracker::~FaceTracker()
  {

  }
  
  Result FaceTracker::Update(const Vision::Image&        frameOrig,
                             std::list<TrackedFace>&     faces,
                             std::list<UpdatedFaceID>&   updatedIDs)
  {
    return _pImpl->Update(frameOrig, faces, updatedIDs);
  }
  
  bool FaceTracker::IsRecognitionSupported()
  {
    return Impl::IsRecognitionSupported();
  }
  
  float FaceTracker::GetMinEyeDistanceForEnrollment()
  {
    return Impl::GetMinEyeDistanceForEnrollment();
  }
  
  Result FaceTracker::AssignNameToID(FaceID_t faceID, const std::string &name)
  {
    return _pImpl->AssignNameToID(faceID, name);
  }

  FaceID_t FaceTracker::EraseFace(const std::string& name)
  {
    return _pImpl->EraseFace(name);
  }

  Result FaceTracker::EraseFace(FaceID_t faceID)
  {
    return _pImpl->EraseFace(faceID);
  }
  
  void FaceTracker::EraseAllFaces()
  {
    return _pImpl->EraseAllFaces();
  }
  
  Result FaceTracker::SaveAlbum(const std::string& albumName)
  {
    return _pImpl->SaveAlbum(albumName);
  }
  
  Result FaceTracker::LoadAlbum(const std::string& albumName, std::list<FaceNameAndID>& namesAndIDs)
  {
    return _pImpl->LoadAlbum(albumName, namesAndIDs);
  }
  
  void FaceTracker::PrintTiming()
  {
    _pImpl->PrintAverageTiming();
  }
  
  void FaceTracker::SetFaceEnrollmentMode(Vision::FaceEnrollmentPose pose,
 																						  Vision::FaceID_t forFaceID,
																						  s32 numEnrollments)
  {
    _pImpl->SetFaceEnrollmentMode(pose, forFaceID, numEnrollments);
  }
  
  Result FaceTracker::GetSerializedData(std::vector<u8>& albumData,
                                        std::vector<u8>& enrollData)
  {
    return _pImpl->GetSerializedData(albumData, enrollData);
  }
  
  Result FaceTracker::SetSerializedData(const std::vector<u8>& albumData,
                                        const std::vector<u8>& enrollData,
                                        std::list<FaceNameAndID>& namesAndIDs)
  {
    return _pImpl->SetSerializedData(albumData, enrollData, namesAndIDs);
  }
  
  /*
  void FaceTracker::EnableDisplay(bool enabled) {
    _pImpl->EnableDisplay(enabled);
  }
   */
  
} // namespace Vision
} // namespace Anki

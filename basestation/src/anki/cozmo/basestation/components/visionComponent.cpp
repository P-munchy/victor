/**
 * File: visionComponent.cpp
 *
 * Author: Andrew Stein
 * Date:   11/20/2014
 *
 * Description: Container for the thread containing the basestation vision
 *              system, which provides methods for managing and communicating
 *              with it.
 *
 * Copyright: Anki, Inc. 2014
 **/

#include "anki/cozmo/basestation/robot.h"
#include "anki/cozmo/basestation/robotPoseHistory.h"
#include "anki/cozmo/basestation/cozmoContext.h"
#include "anki/cozmo/basestation/components/visionComponent.h"
#include "anki/cozmo/basestation/visionSystem.h"
#include "anki/cozmo/basestation/actions/basicActions.h"

#include "anki/vision/basestation/image_impl.h"
#include "anki/vision/basestation/trackedFace.h"
#include "anki/vision/MarkerCodeDefinitions.h"
#include "anki/vision/basestation/observableObjectLibrary_impl.h"

#include "anki/common/basestation/math/point_impl.h"
#include "anki/common/basestation/math/quad_impl.h"
#include "anki/common/robot/config.h"

#include "util/logging/logging.h"
#include "util/helpers/templateHelpers.h"
#include "anki/common/basestation/utils/helpers/boundedWhile.h"
#include "anki/common/basestation/utils/data/dataPlatform.h"
#include "anki/common/basestation/utils/timer.h"
#include "anki/cozmo/basestation/externalInterface/externalInterface.h"
#include "clad/externalInterface/messageEngineToGame.h"
#include "clad/externalInterface/messageGameToEngine.h"
#include "clad/robotInterface/messageEngineToRobot.h"
#include "clad/types/imageTypes.h"

#include "anki/cozmo/basestation/viz/vizManager.h"

#include "opencv2/highgui/highgui.hpp"

namespace Anki {
namespace Cozmo {
  
  VisionComponent::VisionComponent(Robot& robot, RunMode mode, const CozmoContext* context)
  : _robot(robot)
  , _vizManager(context->GetVizManager())
  , _camera(robot.GetID())
  , _runMode(mode)
  {
    std::string dataPath("");
    if(context->GetDataPlatform() != nullptr) {
      dataPath = context->GetDataPlatform()->pathToResource(Util::Data::Scope::Resources,
                                              "/config/basestation/vision");
    } else {
      PRINT_NAMED_WARNING("VisionComponent.Constructor.NullDataPlatform",
                          "Insantiating VisionSystem with no context and/or data platform.");
    }
    
    _visionSystem = new VisionSystem(dataPath, _vizManager);
    
    // Set up event handlers
    if(nullptr != context && nullptr != context->GetExternalInterface())
    {
      using namespace ExternalInterface;
      
      // EnableVisionMode
      _signalHandles.push_back(context->GetExternalInterface()->Subscribe(MessageGameToEngineTag::EnableVisionMode,
        [this] (const AnkiEvent<MessageGameToEngine>& event)
        {
          auto const& payload = event.GetData().Get_EnableVisionMode();
          EnableMode(payload.mode, payload.enable);
        }));
      
      // AssignNameToFace
      _signalHandles.push_back(context->GetExternalInterface()->Subscribe(MessageGameToEngineTag::AssignNameToFace,
        [this] (const AnkiEvent<MessageGameToEngine>& event)
        {
          const ExternalInterface::AssignNameToFace& msg = event.GetData().Get_AssignNameToFace();
          Lock();
          _visionSystem->AssignNameToFace(msg.faceID, msg.name);
          Unlock();
        }));
      
      // EnableNewFaceEnrollment
      _signalHandles.push_back(context->GetExternalInterface()->Subscribe(MessageGameToEngineTag::EnableNewFaceEnrollment,
        [this] (const AnkiEvent<MessageGameToEngine>& event) {
          const ExternalInterface::EnableNewFaceEnrollment& msg = event.GetData().Get_EnableNewFaceEnrollment();
          _visionSystem->EnableNewFaceEnrollment(msg.numToEnroll);
        }));
      
      // StartFaceTracking
      _signalHandles.push_back(context->GetExternalInterface()->Subscribe(MessageGameToEngineTag::EnableVisionMode,
         [this] (const AnkiEvent<MessageGameToEngine>& event)
         {
           auto const& payload = event.GetData().Get_EnableVisionMode();
           EnableMode(payload.mode, payload.enable);
         }));
      
      // VisionWhileMoving
      _signalHandles.push_back(context->GetExternalInterface()->Subscribe(MessageGameToEngineTag::VisionWhileMoving,
         [this] (const AnkiEvent<MessageGameToEngine>& event)
         {
           const ExternalInterface::VisionWhileMoving& msg = event.GetData().Get_VisionWhileMoving();
           EnableVisionWhileMoving(msg.enable);
         }));
      
      // VisionRunMode
      _signalHandles.push_back(context->GetExternalInterface()->Subscribe(ExternalInterface::MessageGameToEngineTag::VisionRunMode,
         [this] (const AnkiEvent<MessageGameToEngine>& event)
         {
           const ExternalInterface::VisionRunMode& msg = event.GetData().Get_VisionRunMode();
           SetRunMode((msg.isSync ? RunMode::Synchronous : RunMode::Asynchronous));
         }));
    }
    
  } // VisionSystem()

  void VisionComponent::SetCameraCalibration(const Vision::CameraCalibration& camCalib)
  {
    if(_camCalib != camCalib || !_isCamCalibSet)
    {
      _camCalib = camCalib;
      _camera.SetSharedCalibration(&_camCalib);
      _isCamCalibSet = true;
      
      _visionSystem->UnInit();
      
      // Got a new calibration: rebuild the LUT for ground plane homographies
      PopulateGroundPlaneHomographyLUT();
      
      // Fine-tune calibration using tool code dots
      //_robot.GetActionList().QueueActionNext(new ReadToolCodeAction(_robot));
    }
  }
  
  
  void VisionComponent::SetRunMode(RunMode mode) {
    if(mode == RunMode::Synchronous && _runMode == RunMode::Asynchronous) {
      PRINT_NAMED_INFO("VisionComponent.SetRunMode.SwitchToSync", "");
      if(_running) {
        //Save old dataPath before destroying current vision system
        std::string dataPath = _visionSystem->GetDataPath();
        Stop();
        _visionSystem = new VisionSystem(dataPath, _vizManager);
      }
      _runMode = mode;
    }
    else if(mode == RunMode::Asynchronous && _runMode == RunMode::Synchronous) {
      PRINT_NAMED_INFO("VisionComponent.SetRunMode.SwitchToAsync", "");
      _runMode = mode;
    }
  }
  
  void VisionComponent::Start()
  {
    if(!_isCamCalibSet) {
      PRINT_NAMED_ERROR("VisionComponent.Start",
                        "Camera calibration must be set to start VisionComponent.");
      return;
    }
    
    if(_running) {
      PRINT_NAMED_INFO("VisionComponent.Start.Restarting",
                       "Thread already started, call Stop() and then restarting.");
      Stop();
    } else {
      PRINT_NAMED_INFO("VisionComponent.Start",
                       "Starting vision processing thread.");
    }
    
    _running = true;
    _paused = false;
    
    // Note that we're giving the Processor a pointer to "this", so we
    // have to ensure this VisionSystem object outlives the thread.
    _processingThread = std::thread(&VisionComponent::Processor, this);
    //_processingThread.detach();
    
  }

  void VisionComponent::Stop()
  {
    _running = false;
    
    // Wait for processing thread to die before destructing since we gave it
    // a reference to *this
    if(_processingThread.joinable()) {
      _processingThread.join();
    }
    
    _currentImg = {};
    _nextImg    = {};
    _lastImg    = {};
  }


  VisionComponent::~VisionComponent()
  {
    Stop();
    
    Util::SafeDelete(_visionSystem);
  } // ~VisionSystem()
 
  
  void VisionComponent::SetMarkerToTrack(const Vision::Marker::Code&  markerToTrack,
                        const Point2f&               markerSize_mm,
                        const Point2f&               imageCenter,
                        const f32                    radius,
                        const bool                   checkAngleX,
                        const f32                    postOffsetX_mm,
                        const f32                    postOffsetY_mm,
                        const f32                    postOffsetAngle_rad)
  {
    if(_visionSystem != nullptr) {
      Embedded::Point2f pt(imageCenter.x(), imageCenter.y());
      Vision::MarkerType markerType = static_cast<Vision::MarkerType>(markerToTrack);
      _visionSystem->SetMarkerToTrack(markerType, markerSize_mm,
                                      pt, radius, checkAngleX,
                                      postOffsetX_mm,
                                      postOffsetY_mm,
                                      postOffsetAngle_rad);
    } else {
      PRINT_NAMED_ERROR("VisionComponent.SetMarkerToTrack.NullVisionSystem",
                        "Cannot set vision marker to track before vision system is instantiated.\n");
    }
  }
  
  
  bool VisionComponent::GetCurrentImage(Vision::ImageRGB& img, TimeStamp_t newerThanTimestamp)
  {
    bool retVal = false;
    
    Lock();
    if(_running && !_currentImg.IsEmpty() && _currentImg.GetTimestamp() > newerThanTimestamp) {
      _currentImg.CopyTo(img);
      img.SetTimestamp(_currentImg.GetTimestamp());
      retVal = true;
    } else {
      img = {};
      retVal = false;
    }
    Unlock();
    
    return retVal;
  }
  
  bool VisionComponent::GetLastProcessedImage(Vision::ImageRGB& img,
                                              TimeStamp_t newerThanTimestamp)
  {
    bool retVal = false;
    
    Lock();
    if(!_lastImg.IsEmpty() && _lastImg.GetTimestamp() > newerThanTimestamp) {
      _lastImg.CopyTo(img);
      img.SetTimestamp(_lastImg.GetTimestamp());
      retVal = true;
    }
    Unlock();
    
    return retVal;
  }

  TimeStamp_t VisionComponent::GetLastProcessedImageTimeStamp()
  {
    
    Lock();
    const TimeStamp_t t = (_lastImg.IsEmpty() ? 0 : _lastImg.GetTimestamp());
    Unlock();

    return t;
  }
  
  TimeStamp_t VisionComponent::GetProcessingPeriod()
  {
    Lock();
    const TimeStamp_t t = _processingPeriod;
    Unlock();
    return t;
  }

  void VisionComponent::Lock()
  {
    _lock.lock();
  }

  void VisionComponent::Unlock()
  {
    _lock.unlock();
  }

  Result VisionComponent::EnableMode(VisionMode mode, bool enable)
  {
    if(nullptr != _visionSystem) {
      return _visionSystem->EnableMode(mode, enable);
    } else {
      PRINT_NAMED_ERROR("VisionComponent.EnableMode.NullVisionSystem", "");
      return RESULT_FAIL;
    }
  }
  
  bool VisionComponent::IsModeEnabled(VisionMode mode) const
  {
    if(nullptr != _visionSystem) {
      return _visionSystem->IsModeEnabled(mode);
    } else {
      return false;
    }
  }
  
  u32 VisionComponent::GetEnabledModes() const
  {
    if(nullptr != _visionSystem) {
      return _visionSystem->GetEnabledModes();
    } else {
      return 0;
    }
  }
  
  Result VisionComponent::SetModes(u32 modes)
  {
    if(nullptr != _visionSystem) {
      _visionSystem->SetModes(modes);
      return RESULT_OK;
    } else {
      return RESULT_FAIL;
    }
  }
  
  Result VisionComponent::SetNextImage(const Vision::ImageRGB& image)
  {
    if(_isCamCalibSet) {
      ASSERT_NAMED(nullptr != _visionSystem, "VisionComponent.SetNextImage.NullVisionSystem");
      if(!_visionSystem->IsInitialized()) {
        _visionSystem->Init(_camCalib);
        
        // Wait for initialization to complete (i.e. Matlab to start up, if needed)
        while(!_visionSystem->IsInitialized()) {
          std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
        
        if(_runMode == RunMode::Asynchronous) {
          Start();
        }
      }

      // Fill in the pose data for the given image, by querying robot history
      RobotPoseStamp imagePoseStamp;
      TimeStamp_t imagePoseStampTimeStamp;
      Result lastResult = _robot.GetPoseHistory()->ComputePoseAt(image.GetTimestamp(), imagePoseStampTimeStamp, imagePoseStamp, true);

      if(lastResult != RESULT_OK) {
        PRINT_NAMED_ERROR("VisionComponent.SetNextImage.PoseHistoryFail",
                          "Unable to get computed pose at image timestamp of %d. (rawPoses: have %zu from %d:%d) (visionPoses: have %zu from %d:%d)\n",
                          image.GetTimestamp(),
                          _robot.GetPoseHistory()->GetNumRawPoses(),
                          _robot.GetPoseHistory()->GetOldestTimeStamp(),
                          _robot.GetPoseHistory()->GetNewestTimeStamp(),
                          _robot.GetPoseHistory()->GetNumVisionPoses(),
                          _robot.GetPoseHistory()->GetOldestVisionOnlyTimeStamp(),
                          _robot.GetPoseHistory()->GetNewestVisionOnlyTimeStamp());
        return lastResult;
      }
      
      // Get most recent pose data in history
      Anki::Cozmo::RobotPoseStamp lastPoseStamp;
      _robot.GetPoseHistory()->GetLastPoseWithFrameID(_robot.GetPoseFrameID(), lastPoseStamp);
      
      // Compare most recent pose and pose at time of image to see if robot has moved in the short time
      // time since the image was taken. If it has, this suppresses motion detection inside VisionSystem.
      // This is necessary because the image might contain motion, but according to the pose there was none.
      // Whether this is due to inaccurate timestamping of images or low-resolution pose reporting from
      // the robot, this additional info allows us to know if motion in the image was likely due to actual
      // robot motion.
      const bool headSame =  NEAR(lastPoseStamp.GetHeadAngle(),
                                  imagePoseStamp.GetHeadAngle(), DEG_TO_RAD(0.1));
      
      const bool poseSame = (NEAR(lastPoseStamp.GetPose().GetTranslation().x(),
                                  imagePoseStamp.GetPose().GetTranslation().x(), .5f) &&
                             NEAR(lastPoseStamp.GetPose().GetTranslation().y(),
                                  imagePoseStamp.GetPose().GetTranslation().y(), .5f) &&
                             NEAR(lastPoseStamp.GetPose().GetRotation().GetAngleAroundZaxis(),
                                  imagePoseStamp.GetPose().GetRotation().GetAngleAroundZaxis(),
                                  DEG_TO_RAD(0.1)));
      
      Lock();
      _nextPoseData.poseStamp = imagePoseStamp;
      _nextPoseData.timeStamp = imagePoseStampTimeStamp;
      _nextPoseData.isMoving = !headSame || !poseSame;
      _nextPoseData.cameraPose = _robot.GetHistoricalCameraPose(_nextPoseData.poseStamp, _nextPoseData.timeStamp);
      _nextPoseData.groundPlaneVisible = LookupGroundPlaneHomography(_nextPoseData.poseStamp.GetHeadAngle(),
                                                                     _nextPoseData.groundPlaneHomography);
      Unlock();
      
      // Experimental:
      //UpdateOverheadMap(image, _nextPoseData);
      
      switch(_runMode)
      {
        case RunMode::Synchronous:
        {
          if(!_paused) {
            _visionSystem->Update(_nextPoseData, image);
            _lastImg = image;
            
            _vizManager->SetText(VizManager::VISION_MODE, NamedColors::CYAN,
                                               "Vision: %s", _visionSystem->GetCurrentModeName().c_str());
          }
          break;
        }
        case RunMode::Asynchronous:
        {
          if(!_paused) {
            Lock();
            
            if(!_nextImg.IsEmpty()) {
              PRINT_NAMED_INFO("VisionComponent.SetNextImage.DroppedFrame",
                               "Setting next image with t=%d, but existing next image from t=%d not yet processed (currently on t=%d).",
                               image.GetTimestamp(), _nextImg.GetTimestamp(), _currentImg.GetTimestamp());
            }
            
            // TODO: Avoid the copying here (shared memory?)
            image.CopyTo(_nextImg);
            
            Unlock();
          }
          break;
        }
        default:
          PRINT_NAMED_ERROR("VisionComponent.SetNextImage.InvalidRunMode", "");
      } // switch(_runMode)
      
      // Display any debug images left by the vision system
      std::pair<const char*, Vision::Image>    debugGray;
      std::pair<const char*, Vision::ImageRGB> debugRGB;
      while(_visionSystem->CheckDebugMailbox(debugGray)) {
        debugGray.second.Display(debugGray.first);
      }
      while(_visionSystem->CheckDebugMailbox(debugRGB)) {
        debugRGB.second.Display(debugRGB.first);
      }
      
    } else {
      PRINT_NAMED_ERROR("VisionComponent.Update.NoCamCalib",
                        "Camera calibration must be set before calling Update().\n");
      return RESULT_FAIL;
    }
    
    return RESULT_OK;
    
  } // SetNextImage()
  
  void VisionComponent::PopulateGroundPlaneHomographyLUT(f32 angleResolution_rad)
  {
    const Pose3d& robotPose = _robot.GetPose();
    
    ASSERT_NAMED(_camera.IsCalibrated(), "VisionComponent.PopulateGroundPlaneHomographyLUT.CameraNotCalibrated");
    
    const Matrix_3x3f K = _camera.GetCalibration()->GetCalibrationMatrix();
    
    GroundPlaneROI groundPlaneROI;
    
    // Loop over all possible head angles at the specified resolution and store
    // the ground plane homography for each.
    for(f32 headAngle_rad = MIN_HEAD_ANGLE; headAngle_rad <= MAX_HEAD_ANGLE;
        headAngle_rad += angleResolution_rad)
    {
      // Get the robot origin w.r.t. the camera position with the camera at
      // the current head angle
      Pose3d robotPoseWrtCamera;
#if ANKI_DEBUG_LEVEL >= ANKI_DEBUG_ERRORS_AND_WARNS_AND_ASSERTS
      bool result = robotPose.GetWithRespectTo(_robot.GetCameraPose(headAngle_rad), robotPoseWrtCamera);
      assert(result == true); // this really shouldn't fail! camera has to be in the robot's pose tree
#else
      robotPose.GetWithRespectTo(_robot.GetCameraPose(headAngle_rad), robotPoseWrtCamera);
#endif
      const RotationMatrix3d& R = robotPoseWrtCamera.GetRotationMatrix();
      const Vec3f&            T = robotPoseWrtCamera.GetTranslation();
      
      // Construct the homography mapping points on the ground plane into the
      // image plane
      const Matrix_3x3f H = K*Matrix_3x3f{R.GetColumn(0),R.GetColumn(1),T};
      
      Quad2f imgQuad;
      groundPlaneROI.GetImageQuad(H, _camCalib.GetNcols(), _camCalib.GetNrows(), imgQuad);
      
      if(_camera.IsWithinFieldOfView(imgQuad[Quad::CornerName::TopLeft]) ||
         _camera.IsWithinFieldOfView(imgQuad[Quad::CornerName::BottomLeft]))
      {
        // Only store this homography if the ROI still projects into the image
        _groundPlaneHomographyLUT[headAngle_rad] = H;
      } else {
        PRINT_NAMED_INFO("VisionComponent.PopulateGroundPlaneHomographyLUT.MaxHeadAngleReached",
                         "Stopping at %.1fdeg", RAD_TO_DEG(headAngle_rad));
        break;
      }
    }
    
  } // PopulateGroundPlaneHomographyLUT()
  
  bool VisionComponent::LookupGroundPlaneHomography(f32 atHeadAngle, Matrix_3x3f& H) const
  {
    if(atHeadAngle > _groundPlaneHomographyLUT.rbegin()->first) {
      // Head angle too large
      return false;
    }
    
    auto iter = _groundPlaneHomographyLUT.lower_bound(atHeadAngle);
    
    if(iter == _groundPlaneHomographyLUT.end()) {
      PRINT_NAMED_WARNING("VisionComponent.LookupGroundPlaneHomography.KeyNotFound",
                          "Failed to find homogrphay using headangle of %.2frad (%.1fdeg) as lower bound",
                          atHeadAngle, RAD_TO_DEG(atHeadAngle));
      --iter;
    } else {
      auto nextIter = iter; ++nextIter;
      if(nextIter != _groundPlaneHomographyLUT.end()) {
        if(std::abs(atHeadAngle - iter->first) > std::abs(atHeadAngle - nextIter->first)) {
          iter = nextIter;
        }
      }
    }
    
    //      PRINT_NAMED_DEBUG("VisionComponent.LookupGroundPlaneHomography.HeadAngleDiff",
    //                        "Requested = %.2fdeg, Returned = %.2fdeg, Diff = %.2fdeg",
    //                        RAD_TO_DEG(atHeadAngle), RAD_TO_DEG(iter->first),
    //                        RAD_TO_DEG(std::abs(atHeadAngle - iter->first)));
    
    H = iter->second;
    return true;
    
  } // LookupGroundPlaneHomography()

  void VisionComponent::Processor()
  {
    PRINT_NAMED_INFO("VisionComponent.Processor",
                     "Starting Robot VisionComponent::Processor thread...");
    
    ASSERT_NAMED(_visionSystem != nullptr && _visionSystem->IsInitialized(),
                 "VisionComponent.Processor.VisionSystemNotReady");
    
    while (_running) {
      
      if(_paused) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        continue;
      }
      
      //if(_currentImg != nullptr) {
      if(!_currentImg.IsEmpty()) {
        // There is an image to be processed:
        
        //assert(_currentImg != nullptr);
        _visionSystem->Update(_currentPoseData, _currentImg);
        
        _vizManager->SetText(VizManager::VISION_MODE, NamedColors::CYAN,
                                           "Vision: %s", _visionSystem->GetCurrentModeName().c_str());
        
        Lock();
        // Store frame rate
        _processingPeriod = _currentImg.GetTimestamp() - _lastImg.GetTimestamp();
        
        // Save the image we just processed
        _lastImg = _currentImg;
        ASSERT_NAMED(_lastImg.GetTimestamp() == _currentImg.GetTimestamp(),
                     "VisionComponent.Processor.WrongImageTimestamp");
        
        // Clear it when done.
        _currentImg = {};
        _nextImg = {};
        
        Unlock();
        
      } else if(!_nextImg.IsEmpty()) {
        Lock();
        _currentImg        = _nextImg;
        _currentPoseData   = _nextPoseData;
        _nextImg = {};
        Unlock();
      } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
      
    } // while(_running)
    
    if(_visionSystem != nullptr) {
      delete _visionSystem;
      _visionSystem = nullptr;
    }
    
    PRINT_NAMED_INFO("VisionComponent.Processor",
                     "Terminated Robot VisionComponent::Processor thread");
  } // Processor()
  
  static f32 ComputePoseAngularSpeed(const RobotPoseStamp& p1, const RobotPoseStamp& p2, const f32 dt)
  {
    const Radians poseAngle1( p1.GetPose().GetRotationAngle<'Z'>() );
    const Radians poseAngle2( p2.GetPose().GetRotationAngle<'Z'>() );
    const f32 poseAngSpeed = std::abs((poseAngle1-poseAngle2).ToFloat()) / dt;
    
    return poseAngSpeed;
  }
  
  static f32 ComputeHeadAngularSpeed(const RobotPoseStamp& p1, const RobotPoseStamp& p2, const f32 dt)
  {
    const f32 headAngSpeed = std::abs((Radians(p1.GetHeadAngle()) - Radians(p2.GetHeadAngle())).ToFloat()) / dt;
    return headAngSpeed;
  }
  
  
  Result VisionComponent::QueueObservedMarker(const Vision::ObservedMarker& markerOrig)
  {
    Result lastResult = RESULT_OK;
    
    // Get historical robot pose at specified timestamp to get
    // head angle and to attach as parent of the camera pose.
    TimeStamp_t t;
    RobotPoseStamp* p = nullptr;
    HistPoseKey poseKey;
    lastResult = _robot.GetPoseHistory()->ComputeAndInsertPoseAt(markerOrig.GetTimeStamp(), t, &p, &poseKey, true);

    if(lastResult != RESULT_OK) {
      PRINT_NAMED_WARNING("VisionComponent.QueueObservedMarker.HistoricalPoseNotFound",
                          "Time: %d, hist: %d to %d\n",
                          markerOrig.GetTimeStamp(),
                          _robot.GetPoseHistory()->GetOldestTimeStamp(),
                          _robot.GetPoseHistory()->GetNewestTimeStamp());
      return lastResult;
    }
    
    // If we get here, ComputeAndInsertPoseIntoHistory() should have succeeded
    // and this should be true
    assert(markerOrig.GetTimeStamp() == t);
    
    // If we were moving too fast at timestamp t then don't queue this marker
    if(WasMovingTooFast(t, p))
    {
      return RESULT_OK;
    }
    
    // Update the marker's camera to use a pose from pose history, and
    // create a new marker with the updated camera
    assert(nullptr != p);
    Vision::ObservedMarker marker(markerOrig.GetTimeStamp(), markerOrig.GetCode(),
                                  markerOrig.GetImageCorners(),
                                  _robot.GetHistoricalCamera(*p, markerOrig.GetTimeStamp()),
                                  markerOrig.GetUserHandle());
    
    // Queue the marker for processing by the blockWorld
    _robot.GetBlockWorld().QueueObservedMarker(poseKey, marker);
    
    /*
    // React to the marker if there is a callback for it
    auto reactionIter = _reactionCallbacks.find(marker.GetCode());
    if(reactionIter != _reactionCallbacks.end()) {
      // Run each reaction for this code, in order:
      for(auto & reactionCallback : reactionIter->second) {
        lastResult = reactionCallback(this, &marker);
        if(lastResult != RESULT_OK) {
          PRINT_NAMED_WARNING("Robot.Update.ReactionCallbackFailed",
                              "Reaction callback failed for robot %d observing marker with code %d.\n",
                              robot.GetID(), marker.GetCode());
        }
      }
    }
     */
    
    // Visualize the marker in 3D
    // TODO: disable this block when not debugging / visualizing
    if(true){
      
      // Note that this incurs extra computation to compute the 3D pose of
      // each observed marker so that we can draw in the 3D world, but this is
      // purely for debug / visualization
      u32 quadID = 0;
      
      // When requesting the markers' 3D corners below, we want them
      // not to be relative to the object the marker is part of, so we
      // will request them at a "canonical" pose (no rotation/translation)
      const Pose3d canonicalPose;
      
      
      // Block Markers
      std::set<const ObservableObject*> const& blocks = _robot.GetBlockWorld().GetObjectLibrary(ObjectFamily::Block).GetObjectsWithMarker(marker);
      for(auto block : blocks) {
        std::vector<Vision::KnownMarker*> const& blockMarkers = block->GetMarkersWithCode(marker.GetCode());
        
        for(auto blockMarker : blockMarkers) {
          
          Pose3d markerPose;
          Result poseResult = marker.GetSeenBy().ComputeObjectPose(marker.GetImageCorners(),
                                                                   blockMarker->Get3dCorners(canonicalPose),
                                                                   markerPose);
          if(poseResult != RESULT_OK) {
            PRINT_NAMED_WARNING("BlockWorld.QueueObservedMarker",
                                "Could not estimate pose of block marker. Not visualizing.\n");
          } else {
            if(markerPose.GetWithRespectTo(marker.GetSeenBy().GetPose().FindOrigin(), markerPose) == true) {
              _robot.GetContext()->GetVizManager()->DrawGenericQuad(quadID++, blockMarker->Get3dCorners(markerPose), NamedColors::OBSERVED_QUAD);
            } else {
              PRINT_NAMED_WARNING("BlockWorld.QueueObservedMarker.MarkerOriginNotCameraOrigin",
                                  "Cannot visualize a Block marker whose pose origin is not the camera's origin that saw it.\n");
            }
          }
        }
      }
      
      
      // Mat Markers
      std::set<const ObservableObject*> const& mats = _robot.GetBlockWorld().GetObjectLibrary(ObjectFamily::Mat).GetObjectsWithMarker(marker);
      for(auto mat : mats) {
        std::vector<Vision::KnownMarker*> const& matMarkers = mat->GetMarkersWithCode(marker.GetCode());
        
        for(auto matMarker : matMarkers) {
          Pose3d markerPose;
          Result poseResult = marker.GetSeenBy().ComputeObjectPose(marker.GetImageCorners(),
                                                                   matMarker->Get3dCorners(canonicalPose),
                                                                   markerPose);
          if(poseResult != RESULT_OK) {
            PRINT_NAMED_WARNING("BlockWorld.QueueObservedMarker",
                                "Could not estimate pose of mat marker. Not visualizing.\n");
          } else {
            if(markerPose.GetWithRespectTo(marker.GetSeenBy().GetPose().FindOrigin(), markerPose) == true) {
              _robot.GetContext()->GetVizManager()->DrawMatMarker(quadID++, matMarker->Get3dCorners(markerPose), NamedColors::RED);
            } else {
              PRINT_NAMED_WARNING("BlockWorld.QueueObservedMarker.MarkerOriginNotCameraOrigin",
                                  "Cannot visualize a Mat marker whose pose origin is not the camera's origin that saw it.\n");
            }
          }
        }
      }
      
    } // 3D marker visualization
    
    return lastResult;
    
  } // QueueObservedMarker()
  
  Result VisionComponent::UpdateVisionMarkers()
  {
    Result lastResult = RESULT_OK;
    if(_visionSystem != nullptr)
    {
      Vision::ObservedMarker visionMarker;
      while(true == _visionSystem->CheckMailbox(visionMarker)) {
        
        lastResult = QueueObservedMarker(visionMarker);
        if(lastResult != RESULT_OK) {
          PRINT_NAMED_ERROR("VisionComponent.Update.FailedToQueueVisionMarker",
                            "Got VisionMarker message from vision processing thread but failed to queue it.");
          return lastResult;
        }
        
        const Quad2f& corners = visionMarker.GetImageCorners();
        const ColorRGBA& drawColor = (visionMarker.GetCode() == Vision::MARKER_UNKNOWN ?
                                      NamedColors::BLUE : NamedColors::RED);
        _vizManager->DrawCameraQuad(corners, drawColor, NamedColors::GREEN);
        
        const bool drawMarkerNames = false;
        if(drawMarkerNames)
        {
          Rectangle<f32> boundingRect(corners);
          std::string markerName(visionMarker.GetCodeName());
          _vizManager->DrawCameraText(boundingRect.GetTopLeft(),
                                      markerName.substr(strlen("MARKER_"),std::string::npos),
                                      drawColor);
        }
      }
    }
    return lastResult;
  } // UpdateVisionMarkers()
  
  Result VisionComponent::UpdateFaces()
  {
    Result lastResult = RESULT_OK;
    if(_visionSystem != nullptr)
    {
      Vision::FaceTracker::UpdatedID updatedID;
      while(true == _visionSystem->CheckMailbox(updatedID))
      {
        _robot.GetFaceWorld().ChangeFaceID(updatedID.oldID, updatedID.newID);
      }

      Vision::TrackedFace faceDetection;
      while(true == _visionSystem->CheckMailbox(faceDetection))
      {
        /*
         PRINT_NAMED_INFO("VisionComponent.Update",
                          "Saw face at (x,y,w,h)=(%.1f,%.1f,%.1f,%.1f), "
                          "at t=%d Pose: roll=%.1f, pitch=%.1f yaw=%.1f, T=(%.1f,%.1f,%.1f).",
                          faceDetection.GetRect().GetX(), faceDetection.GetRect().GetY(),
                          faceDetection.GetRect().GetWidth(), faceDetection.GetRect().GetHeight(),
                          faceDetection.GetTimeStamp(),
                          faceDetection.GetHeadRoll().getDegrees(),
                          faceDetection.GetHeadPitch().getDegrees(),
                          faceDetection.GetHeadYaw().getDegrees(),
                          faceDetection.GetHeadPose().GetTranslation().x(),
                          faceDetection.GetHeadPose().GetTranslation().y(),
                          faceDetection.GetHeadPose().GetTranslation().z());
         */
        
        // Get historical robot pose at specified timestamp to get
        // head angle and to attach as parent of the camera pose.
        TimeStamp_t t;
        RobotPoseStamp* p = nullptr;
        HistPoseKey poseKey;
        _robot.GetPoseHistory()->ComputeAndInsertPoseAt(faceDetection.GetTimeStamp(), t, &p, &poseKey, true);
        // If we were moving too fast at the timestamp the face was detected then don't update it
        if(WasMovingTooFast(faceDetection.GetTimeStamp(), p))
        {
          return RESULT_OK;
        }
        
        // Use the faceDetection to update FaceWorld:
        lastResult = _robot.GetFaceWorld().AddOrUpdateFace(faceDetection);
        if(lastResult != RESULT_OK) {
          PRINT_NAMED_ERROR("VisionComponent.Update.FailedToUpdateFace",
                            "Got FaceDetection from vision processing but failed to update it.");
          return lastResult;
        }
      }
      
    } // if(_visionSystem != nullptr)
    return lastResult;
  } // UpdateFaces()
  
  Result VisionComponent::UpdateTrackingQuad()
  {
    if(_visionSystem != nullptr)
    {
      VizInterface::TrackerQuad trackerQuad;
      if(true == _visionSystem->CheckMailbox(trackerQuad)) {
        // Send tracker quad info to viz
        _vizManager->SendTrackerQuad(trackerQuad.topLeft_x, trackerQuad.topLeft_y,
                                                   trackerQuad.topRight_x, trackerQuad.topRight_y,
                                                   trackerQuad.bottomRight_x, trackerQuad.bottomRight_y,
                                                   trackerQuad.bottomLeft_x, trackerQuad.bottomLeft_y);
      }
    }
    return RESULT_OK;
  } // UpdateTrackingQuad()
  
  Result VisionComponent::UpdateDockingErrorSignal()
  {
    if(_visionSystem != nullptr)
    {
      std::pair<Pose3d, TimeStamp_t> markerPoseWrtCamera;
      if(true == _visionSystem->CheckMailbox(markerPoseWrtCamera)) {
        
        // Hook the pose coming out of the vision system up to the historical
        // camera at that timestamp
        Vision::Camera histCamera(_robot.GetHistoricalCamera(markerPoseWrtCamera.second));
        markerPoseWrtCamera.first.SetParent(&histCamera.GetPose());
        /*
         // Get the pose w.r.t. the (historical) robot pose instead of the camera pose
         Pose3d markerPoseWrtRobot;
         if(false == markerPoseWrtCamera.first.GetWithRespectTo(p.GetPose(), markerPoseWrtRobot)) {
         PRINT_NAMED_ERROR("VisionComponent.Update.PoseOriginFail",
         "Could not get marker pose w.r.t. robot.");
         return RESULT_FAIL;
         }
         */
        //Pose3d poseWrtRobot = poseWrtCam;
        //poseWrtRobot.PreComposeWith(camWrtRobotPose);
        Pose3d markerPoseWrtRobot(markerPoseWrtCamera.first);
        markerPoseWrtRobot.PreComposeWith(histCamera.GetPose());
        
        DockingErrorSignal dockErrMsg;
        dockErrMsg.timestamp = markerPoseWrtCamera.second;
        dockErrMsg.x_distErr = markerPoseWrtRobot.GetTranslation().x();
        dockErrMsg.y_horErr  = markerPoseWrtRobot.GetTranslation().y();
        dockErrMsg.z_height  = markerPoseWrtRobot.GetTranslation().z();
        dockErrMsg.angleErr  = markerPoseWrtRobot.GetRotation().GetAngleAroundZaxis().ToFloat() + M_PI_2;
        
        // Visualize docking error signal
        _vizManager->SetDockingError(dockErrMsg.x_distErr,
                                                   dockErrMsg.y_horErr,
                                                   dockErrMsg.angleErr);

        // Try to use this for closed-loop control by sending it on to the robot
        _robot.SendRobotMessage<DockingErrorSignal>(std::move(dockErrMsg));
      }
    } // if(_visionSystem != nullptr)
    return RESULT_OK;
  } // UpdateDockingErrorSignal()
  
  Result VisionComponent::UpdateMotionCentroid()
  {
    if(_visionSystem != nullptr)
    {
      ExternalInterface::RobotObservedMotion motionCentroid;
      if (true == _visionSystem->CheckMailbox(motionCentroid))
      {
        _robot.Broadcast(ExternalInterface::MessageEngineToGame(std::move(motionCentroid)));
      }
    } // if(_visionSystem != nullptr)
    return RESULT_OK;
  } // UpdateMotionCentroid()
  
  Result VisionComponent::UpdateOverheadEdges()
  {
    if(_visionSystem != nullptr)
    {
      OverheadEdgeFrame edgeFrame;
      while(true == _visionSystem->CheckMailbox(edgeFrame))
      {
        _robot.GetBlockWorld().ProcessVisionOverheadEdges(edgeFrame);
      }      
    }
    return RESULT_OK;
  }
  
  Result VisionComponent::UpdateOverheadMap(const Vision::ImageRGB& image,
                                            const VisionSystem::PoseData& poseData)
  {
    if(poseData.groundPlaneVisible)
    {
      const Matrix_3x3f& H = poseData.groundPlaneHomography;
      
      const GroundPlaneROI& roi = poseData.groundPlaneROI;
      
      Quad2f imgGroundQuad;
      roi.GetImageQuad(H, image.GetNumCols(), image.GetNumRows(), imgGroundQuad);
      
      static Vision::ImageRGB overheadMap(1000.f, 1000.f);
      
      // Need to apply a shift after the homography to put things in image
      // coordinates with (0,0) at the upper left (since groundQuad's origin
      // is not upper left). Also mirror Y coordinates since we are looking
      // from above, not below
      Matrix_3x3f InvShift{
        1.f, 0.f, roi.GetDist(), // Negated b/c we're using inv(Shift)
        0.f,-1.f, roi.GetWidthFar()*0.5f,
        0.f, 0.f, 1.f};

      Pose3d worldPoseWrtRobot = poseData.poseStamp.GetPose().GetInverse();
      for(s32 i=0; i<roi.GetWidthFar(); ++i) {
        const u8* mask_i = roi.GetOverheadMask().GetRow(i);
        const f32 y = static_cast<f32>(i) - 0.5f*roi.GetWidthFar();
        for(s32 j=0; j<roi.GetLength(); ++j) {
          if(mask_i[j] > 0) {
            // Project ground plane point in robot frame to image
            const f32 x = static_cast<f32>(j) + roi.GetDist();
            Point3f imgPoint = H * Point3f(x,y,1.f);
            assert(imgPoint.z() > 0.f);
            const f32 divisor = 1.f / imgPoint.z();
            imgPoint.x() *= divisor;
            imgPoint.y() *= divisor;
            const s32 x_img = std::round(imgPoint.x());
            const s32 y_img = std::round(imgPoint.y());
            if(x_img >= 0 && y_img >= 0 &&
               x_img < image.GetNumCols() && y_img < image.GetNumRows())
            {
              const Vision::PixelRGB value = image(y_img, x_img);
              
              // Get corresponding map point in world coords
              Point3f mapPoint = poseData.poseStamp.GetPose() * Point3f(x,y,0.f);
              const s32 x_map = std::round( mapPoint.x() + static_cast<f32>(overheadMap.GetNumCols())*0.5f);
              const s32 y_map = std::round(-mapPoint.y() + static_cast<f32>(overheadMap.GetNumRows())*0.5f);
              if(x_map >= 0 && y_map >= 0 &&
                 x_map < overheadMap.GetNumCols() && y_map < overheadMap.GetNumRows())
              {
                overheadMap(y_map, x_map).AlphaBlendWith(value, 0.5f);
              }
            }
          }
        }
      }
      
      Vision::ImageRGB overheadImg = roi.GetOverheadImage(image, H);
      
      static s32 updateFreq = 0;
      if(updateFreq++ == 8){ // DEBUG
        updateFreq = 0;
        Vision::ImageRGB dispImg;
        image.CopyTo(dispImg);
        dispImg.DrawQuad(imgGroundQuad, NamedColors::RED, 1);
        dispImg.Display("GroundQuad");
        overheadImg.Display("OverheadView");
        
        // Display current map with the last updated region highlighted with
        // a red border
        overheadMap.CopyTo(dispImg);
        Quad3f lastUpdate;
        poseData.poseStamp.GetPose().ApplyTo(roi.GetGroundQuad(), lastUpdate);
        for(auto & point : lastUpdate) {
          point.x() += static_cast<f32>(overheadMap.GetNumCols()*0.5f);
          point.y() *= -1.f;
          point.y() += static_cast<f32>(overheadMap.GetNumRows()*0.5f);
        }
        dispImg.DrawQuad(lastUpdate, NamedColors::RED, 2);
        dispImg.Display("OverheadMap");
      }
    } // if ground plane is visible
    
    return RESULT_OK;
  } // UpdateOverheadMap()
  
  Result VisionComponent::UpdateToolCode()
  {
    if(_visionSystem != nullptr)
    {
      ToolCode code;
      if(true == _visionSystem->CheckMailbox(code))
      {
        ExternalInterface::RobotReadToolCode msg;
        msg.code = code;
        _robot.Broadcast(ExternalInterface::MessageEngineToGame(std::move(msg)));
      }
    }

    return RESULT_OK;
  }
  
  bool VisionComponent::WasMovingTooFast(TimeStamp_t t, RobotPoseStamp* p)
  {
    // Check to see if the robot's body or head are
    // moving too fast to queue this marker
    if(!_visionWhileMovingEnabled && !_robot.IsPickingOrPlacing())
    {
      TimeStamp_t t_prev, t_next;
      RobotPoseStamp p_prev, p_next;
      
      Result lastResult = _robot.GetPoseHistory()->GetRawPoseBeforeAndAfter(t, t_prev, p_prev, t_next, p_next);
      if(lastResult != RESULT_OK) {
        PRINT_NAMED_WARNING("VisionComponent.QueueObservedMarker.HistoricalPoseNotFound",
                            "Could not get next/previous poses for t = %d, so "
                            "cannot compute angular velocity. Ignoring marker.\n", t);
        
        // Don't return failure, but don't queue the marker either (since we
        // couldn't check the angular velocity while seeing it
        return true;
      }
      
      const f32 ANGULAR_VELOCITY_THRESHOLD_DEG_PER_SEC = 5.f;
      const f32 HEAD_ANGULAR_VELOCITY_THRESHOLD_DEG_PER_SEC = 10.f;
      
      assert(t_prev < t);
      assert(t_next > t);
      const f32 dtPrev_sec = static_cast<f32>(t - t_prev) * 0.001f;
      const f32 dtNext_sec = static_cast<f32>(t_next - t) * 0.001f;
      const f32 headSpeedPrev = ComputeHeadAngularSpeed(*p, p_prev, dtPrev_sec);
      const f32 headSpeedNext = ComputeHeadAngularSpeed(*p, p_next, dtNext_sec);
      const f32 turnSpeedPrev = ComputePoseAngularSpeed(*p, p_prev, dtPrev_sec);
      const f32 turnSpeedNext = ComputePoseAngularSpeed(*p, p_next, dtNext_sec);
      
      if(turnSpeedNext > DEG_TO_RAD(ANGULAR_VELOCITY_THRESHOLD_DEG_PER_SEC) ||
         turnSpeedPrev > DEG_TO_RAD(ANGULAR_VELOCITY_THRESHOLD_DEG_PER_SEC))
      {
        //          PRINT_NAMED_WARNING("VisionComponent.QueueObservedMarker",
        //                              "Ignoring vision marker seen while turning with angular "
        //                              "velocity = %.1f/%.1f deg/sec (thresh = %.1fdeg)\n",
        //                              RAD_TO_DEG(turnSpeedPrev), RAD_TO_DEG(turnSpeedNext),
        //                              ANGULAR_VELOCITY_THRESHOLD_DEG_PER_SEC);
        return true;
      } else if(headSpeedNext > DEG_TO_RAD(HEAD_ANGULAR_VELOCITY_THRESHOLD_DEG_PER_SEC) ||
                headSpeedPrev > DEG_TO_RAD(HEAD_ANGULAR_VELOCITY_THRESHOLD_DEG_PER_SEC))
      {
        //          PRINT_NAMED_WARNING("VisionComponent.QueueObservedMarker",
        //                              "Ignoring vision marker seen while head moving with angular "
        //                              "velocity = %.1f/%.1f deg/sec (thresh = %.1fdeg)\n",
        //                              RAD_TO_DEG(headSpeedPrev), RAD_TO_DEG(headSpeedNext),
        //                              HEAD_ANGULAR_VELOCITY_THRESHOLD_DEG_PER_SEC);
        return true;
      }
      
    } // if(!_visionWhileMovingEnabled)
    return false;
  }

  template<class PixelType>
  Result VisionComponent::CompressAndSendImage(const Vision::ImageBase<PixelType>& img, s32 quality)
  {
    if(!_robot.HasExternalInterface()) {
      PRINT_NAMED_ERROR("VisionComponent.CompressAndSendImage.NoExternalInterface", "");
      return RESULT_FAIL;
    }
    
    Result result = RESULT_OK;
    
    ImageChunk m;
    
    const s32 captureHeight = img.GetNumRows();
    const s32 captureWidth  = img.GetNumCols();
    
    switch(captureHeight) {
      case 240:
        if (captureWidth!=320) {
          result = RESULT_FAIL;
        } else {
          m.resolution = ImageResolution::QVGA;
        }
        break;
        
      case 296:
        if (captureWidth!=400) {
          result = RESULT_FAIL;
        } else {
          m.resolution = ImageResolution::CVGA;
        }
        break;
        
      case 480:
        if (captureWidth!=640) {
          result = RESULT_FAIL;
        } else {
          m.resolution = ImageResolution::VGA;
        }
        break;
        
      default:
        result = RESULT_FAIL;
    }
    
    if(RESULT_OK != result) {
      PRINT_NAMED_ERROR("VisionComponent.CompressAndSendImage",
                        "Unrecognized resolution: %dx%d.\n", captureWidth, captureHeight);
      return result;
    }
    
    static u32 imgID = 0;
    const std::vector<int> compressionParams = {
      CV_IMWRITE_JPEG_QUALITY, quality
    };
    
    cv::cvtColor(img.get_CvMat_(), img.get_CvMat_(), CV_BGR2RGB);
    
    std::vector<u8> compressedBuffer;
    cv::imencode(".jpg",  img.get_CvMat_(), compressedBuffer, compressionParams);
    
    const u32 numTotalBytes = static_cast<u32>(compressedBuffer.size());
    
    //PRINT("Sending frame with capture time = %d at time = %d\n", captureTime, HAL::GetTimeStamp());
    
    m.frameTimeStamp = img.GetTimestamp();
    m.imageId = ++imgID;
    m.chunkId = 0;
    m.imageChunkCount = ceilf((f32)numTotalBytes / (f32)ImageConstants::IMAGE_CHUNK_SIZE);
    if(img.GetNumChannels() == 1) {
      m.imageEncoding = ImageEncoding::JPEGGray;
    } else {
      m.imageEncoding = ImageEncoding::JPEGColor;
    }
    m.data.reserve((size_t)ImageConstants::IMAGE_CHUNK_SIZE);
    
    u32 totalByteCnt = 0;
    u32 chunkByteCnt = 0;
    
    for(s32 i=0; i<numTotalBytes; ++i)
    {
      m.data.push_back(compressedBuffer[i]);
      
      ++chunkByteCnt;
      ++totalByteCnt;
      
      if (chunkByteCnt == (s32)ImageConstants::IMAGE_CHUNK_SIZE) {
        //PRINT("Sending image chunk %d\n", m.chunkId);
        _robot.GetContext()->GetExternalInterface()->Broadcast(ExternalInterface::MessageEngineToGame(ImageChunk(m)));
        ++m.chunkId;
        chunkByteCnt = 0;
      } else if (totalByteCnt == numTotalBytes) {
        // This should be the last message!
        //PRINT("Sending LAST image chunk %d\n", m.chunkId);
        _robot.GetContext()->GetExternalInterface()->Broadcast(ExternalInterface::MessageEngineToGame(ImageChunk(m)));
      }
    } // for each byte in the compressed buffer
    
    return RESULT_OK;
  } // CompressAndSendImage()
  
  // Explicit instantiation for grayscale and RGB
  template Result VisionComponent::CompressAndSendImage<u8>(const Vision::ImageBase<u8>& img,
                                                            s32 quality);
  
  template Result VisionComponent::CompressAndSendImage<Vision::PixelRGB>(const Vision::ImageBase<Vision::PixelRGB>& img,
                                                                          s32 quality);
  
  
} // namespace Cozmo
} // namespace Anki

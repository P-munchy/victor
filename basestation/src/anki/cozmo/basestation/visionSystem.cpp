/**
 * File: visionSystem.cpp [Basestation]
 *
 * Author: Andrew Stein
 * Date:   (various)
 *
 * Description: High-level module that controls the basestation vision system
 *              Runs on its own thread inside VisionProcessingThread.
 *
 *  NOTE: Current implementation is basically a copy of the Embedded vision system
 *    on the robot, so we can first see if vision-over-WiFi is feasible before a
 *    native Basestation implementation of everything.
 *
 * Copyright: Anki, Inc. 2014
 **/

#include "visionSystem.h"
#include "anki/cozmo/basestation/robot.h"
#include "anki/common/basestation/mailbox_impl.h"
#include "anki/vision/basestation/image_impl.h"
#include "anki/common/basestation/math/point_impl.h"
#include "anki/common/basestation/math/quad_impl.h"
#include "anki/common/basestation/math/rect_impl.h"
#include "clad/vizInterface/messageViz.h"
#include "clad/robotInterface/messageEngineToRobot.h"
#include "clad/types/robotStatusAndActions.h"
#include "util/helpers/templateHelpers.h"

//
// Embedded implementation holdovers:
//  (these should probably all go away once basestation vision is natively implemented)

#include "anki/common/robot/config.h"
// Coretech Vision Includes
#include "anki/vision/MarkerCodeDefinitions.h"
#include "anki/vision/robot/fiducialDetection.h"
#include "anki/vision/robot/fiducialMarkers.h"
#include "anki/vision/robot/imageProcessing.h"
#include "anki/vision/robot/perspectivePoseEstimation.h"
#include "anki/vision/robot/classifier.h"
#include "anki/vision/robot/lbpcascade_frontalface.h"
#include "anki/vision/robot/cameraImagingPipeline.h"

// CoreTech Common Includes
#include "anki/common/shared/radians.h"
#include "anki/common/robot/benchmarking.h"
#include "anki/common/robot/memory.h"
#include "anki/common/robot/utilities.h"

// Cozmo-Specific Library Includes
#include "anki/cozmo/shared/cozmoConfig.h"

#define DEBUG_MOTION_DETECTION 0
#define DEBUG_FACE_DETECTION   0

#define USE_CONNECTED_COMPONENTS_FOR_MOTION_CENTROID 0
#define USE_THREE_FRAME_MOTION_DETECTION 0

#if USE_MATLAB_TRACKER || USE_MATLAB_DETECTOR
#include "matlabVisionProcessor.h"
#endif

namespace Anki {
namespace Cozmo {
  
  using namespace Embedded;
  
  VisionSystem::VisionSystem(const std::string& dataPath, VizManager* vizMan)
  : _isInitialized(false)
  , _dataPath(dataPath)
  , _faceTracker(nullptr)
  , _vizManager(vizMan)
  {
    PRINT_NAMED_INFO("VisionSystem.Constructor", "");
   
#   if RECOGNITION_METHOD == RECOGNITION_METHOD_NEAREST_NEIGHBOR
    // Force the NN library to load _now_, not on first use
    VisionMarker::GetNearestNeighborLibrary();
#   endif
    
  } // VisionSystem()
  
  VisionSystem::~VisionSystem()
  {
    Util::SafeDelete(_faceTracker);
  }
  
  
  // WARNING: ResetBuffers should be used with caution
  Result VisionSystem::VisionMemory::ResetBuffers()
  {
    _offchipScratch = MemoryStack(offchipBuffer, OFFCHIP_BUFFER_SIZE);
    _onchipScratch  = MemoryStack(onchipBuffer, ONCHIP_BUFFER_SIZE);
    _ccmScratch     = MemoryStack(ccmBuffer, CCM_BUFFER_SIZE);
    
    if(!_offchipScratch.IsValid() || !_onchipScratch.IsValid() || !_ccmScratch.IsValid()) {
      PRINT_STREAM_INFO("VisionSystem.VisionMemory.ResetBuffers", "Error: InitializeScratchBuffers");
      return RESULT_FAIL;
    }
    
    _markers = FixedLengthList<VisionMarker>(VisionMemory::MAX_MARKERS, _offchipScratch);
    
    return RESULT_OK;
  }
  
  Result VisionSystem::VisionMemory::Initialize()
  {
    return ResetBuffers();
  }
  
  
  //
  // Implementation of MarkerToTrack methods:
  //
  
  VisionSystem::MarkerToTrack::MarkerToTrack()
  {
    Clear();
  }
  
  void VisionSystem::MarkerToTrack::Clear() {
    type        = Anki::Vision::MARKER_UNKNOWN;
    width_mm    = 0;
    imageCenter = Embedded::Point2f(-1.f, -1.f);
    imageSearchRadius = -1.f;
    checkAngleX = true;
  }
  
  bool VisionSystem::MarkerToTrack::Matches(const VisionMarker& marker) const
  {
    bool doesMatch = false;
    
    if(marker.markerType == this->type) {
      if(this->imageCenter.x >= 0.f && this->imageCenter.y >= 0.f &&
         this->imageSearchRadius > 0.f)
      {
        // There is an image position specified, check to see if the
        // marker's centroid is close enough to it
        Embedded::Point2f centroid = marker.corners.ComputeCenter<f32>();
        if( (centroid - this->imageCenter).Length() < this->imageSearchRadius ) {
          doesMatch = true;
        }
      } else {
        // No image position specified, just return true since the
        // types match
        doesMatch = true;
      }
    }
    
    return doesMatch;
  } // MarkerToTrack::Matches()
  
#if 0
#pragma mark --- Mode Controls ---
#endif
  
  Result VisionSystem::EnableMode(VisionMode whichMode, bool enabled)
  {
    if(whichMode == VisionMode::Tracking) {
      // Tracking enable/disable is a special case
      if(enabled) {
        if(!_markerToTrack.IsSpecified()) {
          PRINT_NAMED_ERROR("VisionSystem.EnableMode.NoMarkerToTrack",
                            "Cannot enable Tracking mode without MarkerToTrack specified.");
          return RESULT_FAIL;
        }
        
        // store the current mode so we can put it back when done tracking
        _modeBeforeTracking = _mode;
        
        // TODO: Log or issue message?
        // NOTE: this disables any other modes so we are *only* tracking
        _mode = static_cast<u32>(whichMode);
      } else {
        StopTracking();
      }
    } else if(whichMode == VisionMode::Idle) {
      if(enabled) {
        // "Enabling" idle means to turn everything off
        PRINT_NAMED_INFO("VisionSystem.EnableMode.Idle",
                         "Disabling all vision modes");
        _mode = static_cast<u32>(VisionMode::Idle);
      } else {
        PRINT_NAMED_WARNING("VisionSystem.EnableMode.InvalidRequest", "Ignoring request to 'disable' idle mode.");
      }
    }else {
      if(enabled) {
        const bool modeAlreadyEnabled = _mode & static_cast<u32>(whichMode);
        if(!modeAlreadyEnabled) {
          PRINT_NAMED_INFO("VisionSystem.EnableModeHelper",
                           "Adding mode %s to current mode %s.",
                           VisionSystem::GetModeName(whichMode).c_str(),
                           VisionSystem::GetModeName(static_cast<VisionMode>(_mode)).c_str());
          
          _mode |= static_cast<u32>(whichMode);
        }
      } else {
        const bool modeAlreadyDisabled = !(_mode & static_cast<u32>(whichMode));
        if(!modeAlreadyDisabled) {
          PRINT_NAMED_WARNING("VisionSystem.EnableMode.DisablingMode",
                              "Removing mode %s from current mode %s.",
                              VisionSystem::GetModeName(whichMode).c_str(),
                              VisionSystem::GetModeName(static_cast<VisionMode>(_mode)).c_str());
          _mode &= ~static_cast<u32>(whichMode);
        }
      }
    }
    return RESULT_OK;
  } // EnableMode()
  
  void VisionSystem::StopTracking()
  {
    SetMarkerToTrack(Vision::MARKER_UNKNOWN, 0.f, true);
    RestoreNonTrackingMode();
  }
  
  void VisionSystem::RestoreNonTrackingMode()
  {
    // Restore whatever we were doing before tracking
    if(IsModeEnabled(VisionMode::Tracking))
    {
      _mode = _modeBeforeTracking;
      
      if(IsModeEnabled(VisionMode::Tracking))
      {
        PRINT_NAMED_ERROR("VisionSystem.StopTracking","Restored mode before tracking but it still includes tracking!");
      }
    }
  }
  
  
#if 0
#pragma mark --- Simulator-Related Definitions ---
#endif
  // This little namespace is just for simulated processing time for
  // tracking and detection (since those run far faster in simulation on
  // a PC than they do on embedded hardware. Basically, this is used by
  // Update() below to wait until a frame is ready before proceeding.
  namespace Simulator {

    static Result Initialize() { return RESULT_OK; }
    static bool IsFrameReady() { return true; }
    static void SetDetectionReadyTime() { }
    static void SetTrackingReadyTime() { }
    static void SetFaceDetectionReadyTime() {}

  } // namespace Simulator
  
  
  Embedded::Quadrilateral<f32> VisionSystem::GetTrackerQuad(MemoryStack scratch)
  {
#if USE_MATLAB_TRACKER
    return MatlabVisionProcessor::GetTrackerQuad();
#else
    return _tracker.get_transformation().get_transformedCorners(scratch);
#endif
  } // GetTrackerQuad()
  
  Result VisionSystem::UpdatePoseData(const PoseData& poseData)
  {
    std::swap(_prevPoseData, _poseData);
    _poseData = poseData;
    
    if(_wasCalledOnce) {
      _havePrevPoseData = true;
    } else {
      _wasCalledOnce = true;
    }
    
    return RESULT_OK;
  } // UpdateRobotState()
  
  
  void VisionSystem::GetPoseChange(f32& xChange, f32& yChange, Radians& angleChange)
  {
    AnkiAssert(_havePrevPoseData);
    
    const Pose3d& crntPose = _poseData.poseStamp.GetPose();
    const Pose3d& prevPose = _prevPoseData.poseStamp.GetPose();
    const Radians crntAngle = crntPose.GetRotation().GetAngleAroundZaxis();
    const Radians prevAngle = prevPose.GetRotation().GetAngleAroundZaxis();
    const Vec3f& crntT = crntPose.GetTranslation();
    const Vec3f& prevT = prevPose.GetTranslation();
    
    angleChange = crntAngle - prevAngle;
    
    //PRINT_STREAM_INFO("angleChange = %.1f", angleChange.getDegrees());
    
    // Position change in world (mat) coordinates
    const f32 dx = crntT.x() - prevT.x();
    const f32 dy = crntT.y() - prevT.y();
    
    // Get change in robot coordinates
    const f32 cosAngle = cosf(-prevAngle.ToFloat());
    const f32 sinAngle = sinf(-prevAngle.ToFloat());
    xChange = dx*cosAngle - dy*sinAngle;
    yChange = dx*sinAngle + dy*cosAngle;
  } // GetPoseChange()
  
  
  // This function actually swaps in the new marker to track, and should
  // not be made available as part of the public API since it could get
  // interrupted by main and we want all this stuff updated at once.
  Result VisionSystem::UpdateMarkerToTrack()
  {
    if(_newMarkerToTrackWasProvided) {
      
      RestoreNonTrackingMode();
      EnableMode(VisionMode::DetectingMarkers, true); // Make sure we enable marker detection
      _numTrackFailures  =  0;
      
      _markerToTrack = _newMarkerToTrack;
      
      if(_markerToTrack.IsSpecified()) {
        
        AnkiConditionalErrorAndReturnValue(_markerToTrack.width_mm > 0.f,
                                           RESULT_FAIL_INVALID_PARAMETER,
                                           "VisionSystem::UpdateMarkerToTrack()",
                                           "Invalid marker width specified.");
        
        // Set canonical 3D marker's corner coordinates
        const P3P_PRECISION markerHalfWidth = _markerToTrack.width_mm * P3P_PRECISION(0.5);
        _canonicalMarker3d[0] = Embedded::Point3<P3P_PRECISION>(-markerHalfWidth, -markerHalfWidth, 0);
        _canonicalMarker3d[1] = Embedded::Point3<P3P_PRECISION>(-markerHalfWidth,  markerHalfWidth, 0);
        _canonicalMarker3d[2] = Embedded::Point3<P3P_PRECISION>( markerHalfWidth, -markerHalfWidth, 0);
        _canonicalMarker3d[3] = Embedded::Point3<P3P_PRECISION>( markerHalfWidth,  markerHalfWidth, 0);
      } // if markerToTrack is valid
      
      _newMarkerToTrack.Clear();
      _newMarkerToTrackWasProvided = false;
    } // if newMarker provided
    
    return RESULT_OK;
    
  } // UpdateMarkerToTrack()
  
  
  Radians VisionSystem::GetCurrentHeadAngle()
  {
    return _poseData.poseStamp.GetHeadAngle();
  }
  
  
  Radians VisionSystem::GetPreviousHeadAngle()
  {
    return _prevPoseData.poseStamp.GetHeadAngle();
  }
  

  bool VisionSystem::CheckMailbox(std::pair<Pose3d, TimeStamp_t>& markerPoseWrtCamera)
  {
    bool retVal = false;
    if(IsInitialized()) {
      retVal = _dockingMailbox.getMessage(markerPoseWrtCamera);
    }
    return retVal;
  }
  
  /*
  bool VisionSystem::CheckMailbox(Viz::FaceDetection&       msg)
  {
    bool retVal = false;
    if(IsInitialized()) {
      retVal = _faceDetectMailbox.getMessage(msg);
    }
    return retVal;
  }
   */
  
  bool VisionSystem::CheckMailbox(Vision::ObservedMarker&        msg)
  {
    bool retVal = false;
    if(IsInitialized()) {
      retVal = _visionMarkerMailbox.getMessage(msg);
    }
    return retVal;
  }
  
  bool VisionSystem::CheckMailbox(VizInterface::TrackerQuad&         msg)
  {
    bool retVal = false;
    if(IsInitialized()) {
      retVal = _trackerMailbox.getMessage(msg);
    }
    return retVal;
  }
  
  bool VisionSystem::CheckMailbox(ExternalInterface::RobotObservedMotion& msg)
  {
    bool retVal = false;
    if(IsInitialized()) {
      retVal = _motionMailbox.getMessage(msg);
    }
    return retVal;
  }
  
  bool VisionSystem::CheckMailbox(Vision::TrackedFace&      msg)
  {
    bool retVal = false;
    if(IsInitialized()) {
      retVal = _faceMailbox.getMessage(msg);
    }
    return retVal;
  }
  
  bool VisionSystem::CheckDebugMailbox(std::pair<const char*, Vision::Image>& msg)
  {
    bool retVal = false;
    if(IsInitialized()) {
      retVal = _debugImageMailbox.getMessage(msg);
    }
    return retVal;
  }
  
  bool VisionSystem::CheckDebugMailbox(std::pair<const char*, Vision::ImageRGB>& msg)
  {
    bool retVal = false;
    if(IsInitialized()) {
      retVal = _debugImageRGBMailbox.getMessage(msg);
    }
    return retVal;
  }
  
  bool VisionSystem::IsInitialized() const
  {
    bool retVal = _isInitialized;
#   if ANKI_COZMO_USE_MATLAB_VISION
    retVal &= _matlab.ep != NULL;
#   endif
    return retVal;
  }
  
  Result VisionSystem::DetectMarkers(const Vision::Image& inputImageGray,
                                      std::vector<Quad2f>& markerQuads)
  {
    Result lastResult = RESULT_OK;
    
    Simulator::SetDetectionReadyTime(); // no-op on real hardware
    
    BeginBenchmark("VisionSystem_LookForMarkers");
    
    AnkiAssert(_detectionParameters.isInitialized);
    
    _memory.ResetBuffers();
    
    // Convert to an Embedded::Array<u8> so the old embedded methods can use the
    // image data.
    const s32 captureHeight = Vision::CameraResInfo[static_cast<size_t>(_captureResolution)].height;
    const s32 captureWidth  = Vision::CameraResInfo[static_cast<size_t>(_captureResolution)].width;
    
    Array<u8> grayscaleImage(captureHeight, captureWidth,
                             _memory._onchipScratch, Flags::Buffer(false,false,false));
    
    std::list<bool> imageInversions;
    switch(_detectionParameters.markerAppearance)
    {
      case VisionMarkerAppearance::BLACK_ON_WHITE:
        // "Normal" appearance
        imageInversions.push_back(false);
        break;
        
      case VisionMarkerAppearance::WHITE_ON_BLACK:
        // Use same code as for black-on-white, but invert the image first
        imageInversions.push_back(true);
        break;
        
      case VisionMarkerAppearance::BOTH:
        // Will run detection twice, with and without inversion
        imageInversions.push_back(false);
        imageInversions.push_back(true);
        break;
        
      default:
        PRINT_NAMED_WARNING("VisionSystem.DetectMarkers.BadMarkerAppearanceSetting",
                            "Will use normal processing without inversion.");
        imageInversions.push_back(false);
        break;
    }
    
    for(auto invertImage : imageInversions)
    {
      if(invertImage) {
        GetImageHelper(inputImageGray.GetNegative(), grayscaleImage);
      } else {
        GetImageHelper(inputImageGray, grayscaleImage);
      }
      
      PreprocessImage(grayscaleImage);
      
      Embedded::FixedLengthList<Embedded::VisionMarker>& markers = _memory._markers;
      const s32 maxMarkers = markers.get_maximumSize();
      
      FixedLengthList<Array<f32> > homographies(maxMarkers, _memory._ccmScratch);
      
      markers.set_size(maxMarkers);
      homographies.set_size(maxMarkers);
      
      for(s32 i=0; i<maxMarkers; i++) {
        Array<f32> newArray(3, 3, _memory._ccmScratch);
        homographies[i] = newArray;
      }
      
      // TODO: Re-enable DebugStream for Basestation
      //MatlabVisualization::ResetFiducialDetection(grayscaleImage);
      
#     if USE_MATLAB_DETECTOR
      const Result result = MatlabVisionProcessor::DetectMarkers(grayscaleImage, markers, homographies, ccmScratch);
#     else
      const CornerMethod cornerMethod = CORNER_METHOD_LAPLACIAN_PEAKS; // {CORNER_METHOD_LAPLACIAN_PEAKS, CORNER_METHOD_LINE_FITS};
      
      // Convert "basestation" detection parameters to "embedded" parameters
      // TODO: Merge the fiducial detection parameters structs
      Embedded::FiducialDetectionParameters embeddedParams;
      embeddedParams.useIntegralImageFiltering = true;
      embeddedParams.scaleImage_numPyramidLevels = _detectionParameters.scaleImage_numPyramidLevels;
      embeddedParams.scaleImage_thresholdMultiplier = _detectionParameters.scaleImage_thresholdMultiplier;
      embeddedParams.component1d_minComponentWidth = _detectionParameters.component1d_minComponentWidth;
      embeddedParams.component1d_maxSkipDistance =  _detectionParameters.component1d_maxSkipDistance;
      embeddedParams.component_minimumNumPixels = _detectionParameters.component_minimumNumPixels;
      embeddedParams.component_maximumNumPixels = _detectionParameters.component_maximumNumPixels;
      embeddedParams.component_sparseMultiplyThreshold = _detectionParameters.component_sparseMultiplyThreshold;
      embeddedParams.component_solidMultiplyThreshold = _detectionParameters.component_solidMultiplyThreshold;
      embeddedParams.component_minHollowRatio = _detectionParameters.component_minHollowRatio;
      embeddedParams.cornerMethod = cornerMethod;
      embeddedParams.minLaplacianPeakRatio = _detectionParameters.minLaplacianPeakRatio;
      embeddedParams.quads_minQuadArea = _detectionParameters.quads_minQuadArea;
      embeddedParams.quads_quadSymmetryThreshold = _detectionParameters.quads_quadSymmetryThreshold;
      embeddedParams.quads_minDistanceFromImageEdge = _detectionParameters.quads_minDistanceFromImageEdge;
      embeddedParams.decode_minContrastRatio = _detectionParameters.decode_minContrastRatio;
      embeddedParams.maxConnectedComponentSegments = _detectionParameters.maxConnectedComponentSegments;
      embeddedParams.maxExtractedQuads = _detectionParameters.maxExtractedQuads;
      embeddedParams.refine_quadRefinementIterations = _detectionParameters.quadRefinementIterations;
      embeddedParams.refine_numRefinementSamples = _detectionParameters.numRefinementSamples;
      embeddedParams.refine_quadRefinementMaxCornerChange = _detectionParameters.quadRefinementMaxCornerChange;
      embeddedParams.refine_quadRefinementMinCornerChange = _detectionParameters.quadRefinementMinCornerChange;
      embeddedParams.returnInvalidMarkers = _detectionParameters.keepUnverifiedMarkers;
      embeddedParams.doCodeExtraction = true;
      
      const Result result = DetectFiducialMarkers(grayscaleImage,
                                                  markers,
                                                  homographies,
                                                  embeddedParams,
                                                  _memory._ccmScratch,
                                                  _memory._onchipScratch,
                                                  _memory._offchipScratch);
#     endif // USE_MATLAB_DETECTOR
      
      if(result != RESULT_OK) {
        return result;
      }
      
      EndBenchmark("VisionSystem_LookForMarkers");
      
      // TODO: Re-enable DebugStream for Basestation
      /*
       DebugStream::SendFiducialDetection(grayscaleImage, markers, ccmScratch, onchipScratch, offchipScratch);
       
       for(s32 i_marker = 0; i_marker < markers.get_size(); ++i_marker) {
       const VisionMarker crntMarker = markers[i_marker];
       
       MatlabVisualization::SendFiducialDetection(crntMarker.corners, crntMarker.markerType);
       }
       
       MatlabVisualization::SendDrawNow();
       */
      
      const s32 numMarkers = _memory._markers.get_size();
      markerQuads.reserve(numMarkers);
      
      for(s32 i_marker = 0; i_marker < numMarkers; ++i_marker)
      {
        const VisionMarker& crntMarker = _memory._markers[i_marker];
        
        // Construct a basestation quad from an embedded one:
        Quad2f quad({crntMarker.corners[Embedded::Quadrilateral<f32>::TopLeft].x,
          crntMarker.corners[Embedded::Quadrilateral<f32>::TopLeft].y},
                    {crntMarker.corners[Embedded::Quadrilateral<f32>::BottomLeft].x,
                      crntMarker.corners[Embedded::Quadrilateral<f32>::BottomLeft].y},
                    {crntMarker.corners[Embedded::Quadrilateral<f32>::TopRight].x,
                      crntMarker.corners[Embedded::Quadrilateral<f32>::TopRight].y},
                    {crntMarker.corners[Embedded::Quadrilateral<f32>::BottomRight].x,
                      crntMarker.corners[Embedded::Quadrilateral<f32>::BottomRight].y});
        
        markerQuads.emplace_back(quad);
        
        Vision::ObservedMarker obsMarker(inputImageGray.GetTimestamp(),
                                         crntMarker.markerType,
                                         quad, _camera);
        
        _visionMarkerMailbox.putMessage(obsMarker);
        
        // Was the desired marker found? If so, start tracking it -- if not already in tracking mode!
        if(!IsModeEnabled(VisionMode::Tracking)     &&
           _markerToTrack.IsSpecified() &&
           _markerToTrack.Matches(crntMarker))
        {
          if((lastResult = InitTemplate(grayscaleImage, crntMarker.corners)) != RESULT_OK) {
            PRINT_NAMED_ERROR("VisionSystem.LookForMarkers.InitTemplateFailed","");
            return lastResult;
          }
          
          // Template initialization succeeded, switch to tracking mode:
          EnableMode(VisionMode::Tracking, true);
          
        } // if(isTrackingMarkerSpecified && !isTrackingMarkerFound && markerType == markerToTrack)
      } // for(each marker)
    } // for(invertImage)
    
    return RESULT_OK;
  } // DetectMarkers()
  
  
  
  // Divide image by mean of whatever is inside the trackingQuad
  Result VisionSystem::BrightnessNormalizeImage(Embedded::Array<u8>& image,
                                         const Embedded::Quadrilateral<f32>& quad)
  {
    //Debug: image.Show("OriginalImage", false);
    
#   define USE_VARIANCE 0
    
    // Compute mean of data inside the bounding box of the tracking quad
    const Embedded::Rectangle<s32> bbox = quad.ComputeBoundingRectangle<s32>();
    
    ConstArraySlice<u8> imageROI = image(bbox.top, bbox.bottom, bbox.left, bbox.right);
    
#   if USE_VARIANCE
    // Playing with normalizing using std. deviation as well
    s32 mean, var;
    Matrix::MeanAndVar<u8, s32>(imageROI, mean, var);
    const f32 stddev = sqrt(static_cast<f32>(var));
    const f32 oneTwentyEightOverStdDev = 128.f / stddev;
    //PRINT("Initial mean/std = %d / %.2f", mean, sqrt(static_cast<f32>(var)));
#   else
    const u8 mean = Embedded::Matrix::Mean<u8, u32>(imageROI);
    //PRINT("Initial mean = %d", mean);
#   endif
    
    //PRINT("quad mean = %d", mean);
    //const f32 oneOverMean = 1.f / static_cast<f32>(mean);
    
    // Remove mean (and variance) from image
    for(s32 i=0; i<image.get_size(0); ++i)
    {
      u8 * restrict img_i = image.Pointer(i, 0);
      
      for(s32 j=0; j<image.get_size(1); ++j)
      {
        f32 value = static_cast<f32>(img_i[j]);
        value -= static_cast<f32>(mean);
#       if USE_VARIANCE
        value *= oneTwentyEightOverStdDev;
#       endif
        value += 128.f;
        img_i[j] = saturate_cast<u8>(value) ;
      }
    }
    
    // Debug:
    /*
     #if USE_VARIANCE
     Matrix::MeanAndVar<u8, s32>(imageROI, mean, var);
     PRINT("Final mean/std = %d / %.2f", mean, sqrt(static_cast<f32>(var)));
     #else
     PRINT("Final mean = %d", Matrix::Mean<u8,u32>(imageROI));
     #endif
     */
    
    //Debug: image.Show("NormalizedImage", true);
    
#   undef USE_VARIANCE
    return RESULT_OK;
    
  } // BrightnessNormalizeImage()
  
  
  Result VisionSystem::BrightnessNormalizeImage(Array<u8>& image, const Embedded::Quadrilateral<f32>& quad,
                                         const f32 filterWidthFraction,
                                         MemoryStack scratch)
  {
    if(filterWidthFraction > 0.f) {
      //Debug:
      image.Show("OriginalImage", false);
      
      // TODO: Add the ability to only normalize within the vicinity of the quad
      // Note that this requires templateQuad to be sorted!
      const s32 filterWidth = static_cast<s32>(filterWidthFraction*((quad[3] - quad[0]).Length()));
      AnkiAssert(filterWidth > 0.f);
      
      Array<u8> imageNormalized(image.get_size(0), image.get_size(1), scratch);
      
      AnkiConditionalErrorAndReturnValue(imageNormalized.IsValid(),
                                         RESULT_FAIL_OUT_OF_MEMORY,
                                         "VisionSystem::BrightnessNormalizeImage",
                                         "Out of memory allocating imageNormalized.");
      
      BeginBenchmark("BoxFilterNormalize");
      
      ImageProcessing::BoxFilterNormalize(image, filterWidth, static_cast<u8>(128),
                                          imageNormalized, scratch);
      
      EndBenchmark("BoxFilterNormalize");
      
      { // DEBUG
        /*
         static Matlab matlab(false);
         matlab.PutArray(grayscaleImage, "grayscaleImage");
         matlab.PutArray(grayscaleImageNormalized, "grayscaleImageNormalized");
         matlab.EvalString("subplot(121), imagesc(grayscaleImage), axis image, colorbar, "
         "subplot(122), imagesc(grayscaleImageNormalized), colorbar, axis image, "
         "colormap(gray)");
         */
        
        //image.Show("GrayscaleImage", false);
        //imageNormalized.Show("GrayscaleImageNormalized", false);
      }
      
      image.Set(imageNormalized);
      
      //Debug:
      //image.Show("NormalizedImage", true);
      
    } // if(filterWidthFraction > 0)
    
    return RESULT_OK;
  } // BrightnessNormalizeImage()
  
  Result VisionSystem::InitTemplate(Array<u8> &grayscaleImage,
                                    const Embedded::Quadrilateral<f32> &trackingQuad)
  {
    Result lastResult = RESULT_OK;
    
    AnkiAssert(_trackerParameters.isInitialized);
    AnkiAssert(_markerToTrack.width_mm > 0);

    MemoryStack ccmScratch = _memory._ccmScratch;
    MemoryStack &onchipMemory = _memory._onchipScratch; //< NOTE: onchip is a reference
    MemoryStack &offchipMemory = _memory._offchipScratch;
    
    // We will start tracking the _first_ marker of the right type that
    // we see.
    // TODO: Something smarter to track the one closest to the image center or to the expected location provided by the basestation?
    _isTrackingMarkerFound = true;
    
    // Normalize the image
    // NOTE: This will change grayscaleImage!
    if(_trackerParameters.normalizationFilterWidthFraction < 0.f) {
      // Faster: normalize using mean of quad
      lastResult = BrightnessNormalizeImage(grayscaleImage, trackingQuad);
    } else {
      // Slower: normalize using local averages
      // NOTE: This is currently off-chip for memory reasons, so it's slow!
      lastResult = BrightnessNormalizeImage(grayscaleImage, trackingQuad,
                                            _trackerParameters.normalizationFilterWidthFraction,
                                            offchipMemory);
    }
    
    AnkiConditionalErrorAndReturnValue(lastResult == RESULT_OK, lastResult,
                                       "VisionSystem::Update::BrightnessNormalizeImage",
                                       "BrightnessNormalizeImage failed.\n");
    
    _trackingIteration = 0;
    
#   if USE_MATLAB_TRACKER
    
    return MatlabVisionProcessor::InitTemplate(grayscaleImage, trackingQuad, ccmScratch);
    
#   else
    
    AnkiConditionalErrorAndReturnValue(_camera.IsCalibrated(), RESULT_FAIL, "VisionSystem.Update.", "Camera not calibrated");
    
    auto const& calib = _camera.GetCalibration();
    
    _tracker = TemplateTracker::LucasKanadeTracker_SampledPlanar6dof(grayscaleImage,
                                                                    trackingQuad,
                                                                    _trackerParameters.scaleTemplateRegionPercent,
                                                                    _trackerParameters.numPyramidLevels,
                                                                    Transformations::TRANSFORM_PROJECTIVE,
                                                                    _trackerParameters.numFiducialEdgeSamples,
                                                                    FIDUCIAL_SQUARE_WIDTH_FRACTION,
                                                                    _trackerParameters.numInteriorSamples,
                                                                    _trackerParameters.numSamplingRegions,
                                                                    calib.GetFocalLength_x(),
                                                                    calib.GetFocalLength_y(),
                                                                    calib.GetCenter_x(),
                                                                    calib.GetCenter_y(),
                                                                    _markerToTrack.width_mm,
                                                                    ccmScratch,
                                                                    onchipMemory,
                                                                    offchipMemory);
    
    /*
     // TODO: Set this elsewhere
     const f32 Kp_min = 0.05f;
     const f32 Kp_max = 0.75f;
     const f32 tz_min = 30.f;
     const f32 tz_max = 150.f;
     tracker.SetGainScheduling(tz_min, tz_max, Kp_min, Kp_max);
     */
    
    if(!_tracker.IsValid()) {
      PRINT_NAMED_ERROR("VisionSystem.InitTemplate", "Failed to initialize valid tracker.");
      return RESULT_FAIL;
    }
    
#   endif // USE_MATLAB_TRACKER
    
    /*
     // TODO: Re-enable visualization/debugstream on basestation
     MatlabVisualization::SendTrackInit(grayscaleImage, tracker, onchipMemory);
     
     #if DOCKING_ALGORITHM == DOCKING_BINARY_TRACKER
     DebugStream::SendBinaryTracker(tracker, ccmScratch, onchipMemory, offchipMemory);
     #endif
     */
    
    _trackingQuad = trackingQuad;
    _trackerJustInitialized = true;
    
    return RESULT_OK;
  } // InitTemplate()
  
  
  
  Result VisionSystem::TrackTemplate(const Vision::Image& inputImageGray)
  {
    Result lastResult = RESULT_OK;
    Simulator::SetTrackingReadyTime(); // no-op on real hardware
    
    MemoryStack ccmScratch = _memory._ccmScratch;
    MemoryStack onchipScratch(_memory._onchipScratch);
    MemoryStack offchipScratch(_memory._offchipScratch);

    // Convert to an Embedded::Array<u8> so the old embedded methods can use the
    // image data.
    const s32 captureHeight = Vision::CameraResInfo[static_cast<size_t>(_captureResolution)].height;
    const s32 captureWidth  = Vision::CameraResInfo[static_cast<size_t>(_captureResolution)].width;
    
    Array<u8> grayscaleImage(captureHeight, captureWidth,
                             onchipScratch, Flags::Buffer(false,false,false));
    
    GetImageHelper(inputImageGray, grayscaleImage);
    
    PreprocessImage(grayscaleImage);

    bool trackingSucceeded = false;
    if(_trackerJustInitialized)
    {
      trackingSucceeded = true;
    } else {
      
      // Normalize the image
      // NOTE: This will change grayscaleImage!
      if(_trackerParameters.normalizationFilterWidthFraction < 0.f) {
        // Faster: normalize using mean of quad
        lastResult = BrightnessNormalizeImage(grayscaleImage, _trackingQuad);
      } else {
        // Slower: normalize using local averages
        // NOTE: This is currently off-chip for memory reasons, so it's slow!
        lastResult = BrightnessNormalizeImage(grayscaleImage, _trackingQuad,
                                              _trackerParameters.normalizationFilterWidthFraction,
                                              offchipScratch);
      }
      
      AnkiConditionalErrorAndReturnValue(lastResult == RESULT_OK, lastResult,
                                         "VisionSystem::Update::BrightnessNormalizeImage",
                                         "BrightnessNormalizeImage failed.\n");
      
      //
      // Tracker Prediction
      //
      // Adjust the tracker transformation by approximately how much we
      // think we've moved since the last tracking call.
      //
      
      if((lastResult =TrackerPredictionUpdate(grayscaleImage, onchipScratch)) != RESULT_OK) {
        PRINT_STREAM_INFO("VisionSystem.Update", " TrackTemplate() failed.\n");
        return lastResult;
      }
      
      BeginBenchmark("VisionSystem_TrackTemplate");
      
      AnkiAssert(_trackerParameters.isInitialized);
      
#     if USE_MATLAB_TRACKER
      return MatlabVisionProcessor::TrackTemplate(grayscaleImage, converged, ccmScratch);
#     endif
      
      trackingSucceeded = false;
      s32 verify_meanAbsoluteDifference;
      s32 verify_numInBounds;
      s32 verify_numSimilarPixels;
      
      const Radians initAngleX(_tracker.get_angleX());
      const Radians initAngleY(_tracker.get_angleY());
      const Radians initAngleZ(_tracker.get_angleZ());
      const Embedded::Point3<f32>& initTranslation = _tracker.GetTranslation();
      
      bool converged = false;
      ++_trackingIteration;
      const Result trackerResult = _tracker.UpdateTrack(grayscaleImage,
                                                        _trackerParameters.maxIterations,
                                                        _trackerParameters.convergenceTolerance_angle,
                                                        _trackerParameters.convergenceTolerance_distance,
                                                        _trackerParameters.verify_maxPixelDifference,
                                                        converged,
                                                        verify_meanAbsoluteDifference,
                                                        verify_numInBounds,
                                                        verify_numSimilarPixels,
                                                        onchipScratch);
      
      // TODO: Do we care if converged == false?
      
      //
      // Go through a bunch of checks to see whether the tracking succeeded
      //
      
      if(fabs((initAngleX - _tracker.get_angleX()).ToFloat()) > _trackerParameters.successTolerance_angle ||
         fabs((initAngleY - _tracker.get_angleY()).ToFloat()) > _trackerParameters.successTolerance_angle ||
         fabs((initAngleZ - _tracker.get_angleZ()).ToFloat()) > _trackerParameters.successTolerance_angle)
      {
        PRINT_STREAM_INFO("VisionSystem.TrackTemplate", "Tracker failed: angle(s) changed too much.");
        trackingSucceeded = false;
      }
      else if(_tracker.GetTranslation().z < TrackerParameters::MIN_TRACKER_DISTANCE)
      {
        PRINT_STREAM_INFO("VisionSystem.TrackTemplate", "Tracker failed: final distance too close.");
        trackingSucceeded = false;
      }
      else if(_tracker.GetTranslation().z > TrackerParameters::MAX_TRACKER_DISTANCE)
      {
        PRINT_STREAM_INFO("VisionSystem.TrackTemplate", "Tracker failed: final distance too far away.");
        trackingSucceeded = false;
      }
      else if((initTranslation - _tracker.GetTranslation()).Length() > _trackerParameters.successTolerance_distance)
      {
        PRINT_STREAM_INFO("VisionSystem.TrackTemplate", "Tracker failed: position changed too much.");
        trackingSucceeded = false;
      }
      else if(_markerToTrack.checkAngleX && fabs(_tracker.get_angleX()) > TrackerParameters::MAX_BLOCK_DOCKING_ANGLE)
      {
        PRINT_STREAM_INFO("VisionSystem.TrackTemplate", "Tracker failed: target X angle too large.");
        trackingSucceeded = false;
      }
      else if(fabs(_tracker.get_angleY()) > TrackerParameters::MAX_BLOCK_DOCKING_ANGLE)
      {
        PRINT_STREAM_INFO("VisionSystem.TrackTemplate", "Tracker failed: target Y angle too large.");
        trackingSucceeded = false;
      }
      else if(fabs(_tracker.get_angleZ()) > TrackerParameters::MAX_BLOCK_DOCKING_ANGLE)
      {
        PRINT_STREAM_INFO("VisionSystem.TrackTemplate", "Tracker failed: target Z angle too large.");
        trackingSucceeded = false;
      }
      else if(atan(fabs(_tracker.GetTranslation().x) / _tracker.GetTranslation().z) > TrackerParameters::MAX_DOCKING_FOV_ANGLE)
      {
        PRINT_STREAM_INFO("VisionSystem.TrackTemplate", "Tracker failed: FOV angle too large.");
        trackingSucceeded = false;
      }
      else if( (static_cast<f32>(verify_numSimilarPixels) /
                static_cast<f32>(verify_numInBounds)) < _trackerParameters.successTolerance_matchingPixelsFraction)
      {
        PRINT_STREAM_INFO("VisionSystem.TrackTemplate", "Tracker failed: too many in-bounds pixels failed intensity verification (" << verify_numSimilarPixels << " / " << verify_numInBounds << " < " << _trackerParameters.successTolerance_matchingPixelsFraction << ").");
        trackingSucceeded = false;
      }
      else {
        // Everything seems ok!
        PRINT_STREAM_INFO("Tracker succeeded", _trackingIteration);
        trackingSucceeded = true;
      }
      
      if(trackerResult != RESULT_OK) {
        return RESULT_FAIL;
      }
      
      EndBenchmark("VisionSystem_TrackTemplate");
      
      // TODO: Re-enable tracker debugstream/vizualization on basestation
      /*
       MatlabVisualization::SendTrack(grayscaleImage, tracker, trackingSucceeded, offchipScratch);
       
       //MatlabVisualization::SendTrackerPrediction_Compare(tracker, offchipScratch);
       
       DebugStream::SendTrackingUpdate(grayscaleImage, tracker, parameters, verify_meanAbsoluteDifference, static_cast<f32>(verify_numSimilarPixels) / static_cast<f32>(verify_numInBounds), ccmScratch, onchipScratch, offchipScratch);
       */
    } // if(_trackingJustInitialized)
    
    if(trackingSucceeded)
    {
      Embedded::Quadrilateral<f32> currentQuad = GetTrackerQuad(onchipScratch);
      
      //FillDockErrMsg(currentQuad, dockErrMsg, _memory._onchipScratch);
      
      // Convert to Pose3d and put it in the docking mailbox for the robot to
      // get and send off to the real robot for docking. Note the pose should
      // really have the camera pose as its parent, but we'll let the robot
      // take care of that, since the vision system is running off on its own
      // thread.
      Array<f32> R(3,3,_memory._onchipScratch);
      lastResult = _tracker.GetRotationMatrix(R);
      if(RESULT_OK != lastResult) {
        PRINT_NAMED_ERROR("VisionSystem.Update.TrackerRotationFail",
                          "Could not get Rotation matrix from 6DoF tracker.");
        return lastResult;
      }
      RotationMatrix3d Rmat{
        R[0][0], R[0][1], R[0][2],
        R[1][0], R[1][1], R[1][2],
        R[2][0], R[2][1], R[2][2]
      };
      Pose3d markerPoseWrtCamera(Rmat, {
        _tracker.GetTranslation().x, _tracker.GetTranslation().y, _tracker.GetTranslation().z
      });
      
      // Add docking offset:
      if(_markerToTrack.postOffsetAngle_rad != 0.f ||
         _markerToTrack.postOffsetX_mm != 0.f ||
         _markerToTrack.postOffsetY_mm != 0.f)
      {
        // Note that the tracker effectively uses camera coordinates for the
        // marker, so the requested "X" offset (which is distance away from
        // the marker's face) is along its negative "Z" axis.
        Pose3d offsetPoseWrtMarker(_markerToTrack.postOffsetAngle_rad, Y_AXIS_3D(),
                                   {-_markerToTrack.postOffsetY_mm, 0.f, -_markerToTrack.postOffsetX_mm});
        markerPoseWrtCamera *= offsetPoseWrtMarker;
      }
      
      // Send tracker quad if image streaming
      if (_imageSendMode == ImageSendMode::Stream) {
        f32 scale = 1.f;
        for (u8 s = (u8)ImageResolution::CVGA; s<(u8)_nextSendImageResolution; ++s) {
          scale *= 0.5f;
        }
        
        VizInterface::TrackerQuad m;
        m.topLeft_x     = static_cast<u16>(currentQuad[Embedded::Quadrilateral<f32>::TopLeft].x * scale);
        m.topLeft_y     = static_cast<u16>(currentQuad[Embedded::Quadrilateral<f32>::TopLeft].y * scale);
        m.topRight_x    = static_cast<u16>(currentQuad[Embedded::Quadrilateral<f32>::TopRight].x * scale);
        m.topRight_y    = static_cast<u16>(currentQuad[Embedded::Quadrilateral<f32>::TopRight].y * scale);
        m.bottomRight_x = static_cast<u16>(currentQuad[Embedded::Quadrilateral<f32>::BottomRight].x * scale);
        m.bottomRight_y = static_cast<u16>(currentQuad[Embedded::Quadrilateral<f32>::BottomRight].y * scale);
        m.bottomLeft_x  = static_cast<u16>(currentQuad[Embedded::Quadrilateral<f32>::BottomLeft].x * scale);
        m.bottomLeft_y  = static_cast<u16>(currentQuad[Embedded::Quadrilateral<f32>::BottomLeft].y * scale);
        
        //HAL::RadioSendMessage(GET_MESSAGE_ID(Messages::TrackerQuad), &m);
        _trackerMailbox.putMessage(m);
      }
      
      // Reset the failure counter
      _numTrackFailures = 0;
      
      _dockingMailbox.putMessage({markerPoseWrtCamera, inputImageGray.GetTimestamp()});
    }
    else {
      _numTrackFailures += 1;
      
      if(_numTrackFailures == MAX_TRACKING_FAILURES)
      {
        PRINT_NAMED_INFO("VisionSystem.Update", "Reached max number of tracking "
                         "failures (%d). Switching back to looking for markers.\n",
                         MAX_TRACKING_FAILURES);
        
        // This resets docking, puttings us back in VISION_MODE_DETECTING_MARKERS mode
        SetMarkerToTrack(_markerToTrack.type,
                         _markerToTrack.width_mm,
                         _markerToTrack.imageCenter,
                         _markerToTrack.imageSearchRadius,
                         _markerToTrack.checkAngleX,
                         _markerToTrack.postOffsetX_mm,
                         _markerToTrack.postOffsetY_mm,
                         _markerToTrack.postOffsetAngle_rad);
      }
    }
    
    return RESULT_OK;
  } // TrackTemplate()
  
  template<typename T>
  static void GetVizQuad(const Embedded::Quadrilateral<T>&  embeddedQuad,
                         Anki::Quadrilateral<2, T>&         vizQuad)
  {
    vizQuad[Quad::TopLeft].x() = embeddedQuad[Quad::TopLeft].x;
    vizQuad[Quad::TopLeft].y() = embeddedQuad[Quad::TopLeft].y;
    
    vizQuad[Quad::TopRight].x() = embeddedQuad[Quad::TopRight].x;
    vizQuad[Quad::TopRight].y() = embeddedQuad[Quad::TopRight].y;
    
    vizQuad[Quad::BottomLeft].x() = embeddedQuad[Quad::BottomLeft].x;
    vizQuad[Quad::BottomLeft].y() = embeddedQuad[Quad::BottomLeft].y;
    
    vizQuad[Quad::BottomRight].x() = embeddedQuad[Quad::BottomRight].x;
    vizQuad[Quad::BottomRight].y() = embeddedQuad[Quad::BottomRight].y;
  }
  
  
  //
  // Tracker Prediction
  //
  // Adjust the tracker transformation by approximately how much we
  // think we've moved since the last tracking call.
  //
  Result VisionSystem::TrackerPredictionUpdate(const Array<u8>& grayscaleImage, MemoryStack scratch)
  {
    Result result = RESULT_OK;
    
    const Embedded::Quadrilateral<f32> currentQuad = GetTrackerQuad(scratch);
    
    // TODO: Re-enable tracker prediction viz on Basestation
    // MatlabVisualization::SendTrackerPrediction_Before(grayscaleImage, currentQuad);
    Anki::Quad2f vizQuad;
    GetVizQuad(currentQuad, vizQuad);
    _vizManager->DrawCameraQuad(vizQuad, ::Anki::NamedColors::BLUE);
    
    // Ask VisionState how much we've moved since last call (in robot coordinates)
    Radians theta_robot;
    f32 T_fwd_robot, T_hor_robot;
    
    GetPoseChange(T_fwd_robot, T_hor_robot, theta_robot);
    
#   if USE_MATLAB_TRACKER
    MatlabVisionProcessor::UpdateTracker(T_fwd_robot, T_hor_robot,
                                         theta_robot, theta_head);
#   else
    Radians theta_head2 = GetCurrentHeadAngle();
    Radians theta_head1 = GetPreviousHeadAngle();
    
    const f32 cH1 = cosf(theta_head1.ToFloat());
    const f32 sH1 = sinf(theta_head1.ToFloat());
    
    const f32 cH2 = cosf(theta_head2.ToFloat());
    const f32 sH2 = sinf(theta_head2.ToFloat());
    
    const f32 cR = cosf(theta_robot.ToFloat());
    const f32 sR = sinf(theta_robot.ToFloat());
    
    // NOTE: these "geometry" entries were computed symbolically with Sage
    // In the derivation, it was assumed the head and neck positions' Y
    // components are zero.
    //
    // From Sage:
    // [cos(thetaR)                 sin(thetaH1)*sin(thetaR)       cos(thetaH1)*sin(thetaR)]
    // [-sin(thetaH2)*sin(thetaR)   cos(thetaR)*sin(thetaH1)*sin(thetaH2) + cos(thetaH1)*cH2  cos(thetaH1)*cos(thetaR)*sin(thetaH2) - cos(thetaH2)*sin(thetaH1)]
    // [-cos(thetaH2)*sin(thetaR)   cos(thetaH2)*cos(thetaR)*sin(thetaH1) - cos(thetaH1)*sin(thetaH2) cos(thetaH1)*cos(thetaH2)*cos(thetaR) + sin(thetaH1)*sin(thetaH2)]
    //
    // T_blockRelHead_new =
    // [T_hor*cos(thetaR) + (Hx*cos(thetaH1) - Hz*sin(thetaH1) + Nx)*sin(thetaR) - T_fwd*sin(thetaR)]
    // [(Hx*cos(thetaH1) - Hz*sin(thetaH1) + Nx)*cos(thetaR)*sin(thetaH2) - (Hz*cos(thetaH1) + Hx*sin(thetaH1) + Nz)*cos(thetaH2) + (Hz*cos(thetaH2) + Hx*sin(thetaH2) + Nz)*cos(thetaH2) - (Hx*cos(thetaH2) - Hz*sin(thetaH2) + Nx)*sin(thetaH2) - (T_fwd*cos(thetaR) + T_hor*sin(thetaR))*sin(thetaH2)]
    // [(Hx*cos(thetaH1) - Hz*sin(thetaH1) + Nx)*cos(thetaH2)*cos(thetaR) - (Hx*cos(thetaH2) - Hz*sin(thetaH2) + Nx)*cos(thetaH2) - (T_fwd*cos(thetaR) + T_hor*sin(thetaR))*cos(thetaH2) + (Hz*cos(thetaH1) + Hx*sin(thetaH1) + Nz)*sin(thetaH2) - (Hz*cos(thetaH2) + Hx*sin(thetaH2) + Nz)*sin(thetaH2)]
    
    AnkiAssert(HEAD_CAM_POSITION[1] == 0.f && NECK_JOINT_POSITION[1] == 0.f);
    Array<f32> R_geometry = Array<f32>(3,3,scratch);
    R_geometry[0][0] = cR;     R_geometry[0][1] = sH1*sR;             R_geometry[0][2] = cH1*sR;
    R_geometry[1][0] = -sH2*sR; R_geometry[1][1] = cR*sH1*sH2 + cH1*cH2;  R_geometry[1][2] = cH1*cR*sH2 - cH2*sH1;
    R_geometry[2][0] = -cH2*sR; R_geometry[2][1] = cH2*cR*sH1 - cH1*sH2;  R_geometry[2][2] = cH1*cH2*cR + sH1*sH2;
    
    const f32 term1 = (HEAD_CAM_POSITION[0]*cH1 - HEAD_CAM_POSITION[2]*sH1 + NECK_JOINT_POSITION[0]);
    const f32 term2 = (HEAD_CAM_POSITION[2]*cH1 + HEAD_CAM_POSITION[0]*sH1 + NECK_JOINT_POSITION[2]);
    const f32 term3 = (HEAD_CAM_POSITION[2]*cH2 + HEAD_CAM_POSITION[0]*sH2 + NECK_JOINT_POSITION[2]);
    const f32 term4 = (HEAD_CAM_POSITION[0]*cH2 - HEAD_CAM_POSITION[2]*sH2 + NECK_JOINT_POSITION[0]);
    const f32 term5 = (T_fwd_robot*cR + T_hor_robot*sR);
    
    Embedded::Point3<f32> T_geometry(T_hor_robot*cR + term1*sR - T_fwd_robot*sR,
                                     term1*cR*sH2 - term2*cH2 + term3*cH2 - term4*sH2 - term5*sH2,
                                     term1*cH2*cR - term4*cH2 - term5*cH2 + term2*sH2 - term3*sH2);
    
    Array<f32> R_blockRelHead = Array<f32>(3,3,scratch);
    _tracker.GetRotationMatrix(R_blockRelHead);
    const Embedded::Point3<f32>& T_blockRelHead = _tracker.GetTranslation();
    
    Array<f32> R_blockRelHead_new = Array<f32>(3,3,scratch);
    Embedded::Matrix::Multiply(R_geometry, R_blockRelHead, R_blockRelHead_new);
    
    Embedded::Point3<f32> T_blockRelHead_new = R_geometry*T_blockRelHead + T_geometry;
    
    if(_tracker.UpdateRotationAndTranslation(R_blockRelHead_new,
                                             T_blockRelHead_new,
                                             scratch) == RESULT_OK)
    {
      result = RESULT_OK;
    }
    
#   endif // #if USE_MATLAB_TRACKER
    
    // TODO: Re-enable tracker prediction viz on basestation
    //MatlabVisualization::SendTrackerPrediction_After(GetTrackerQuad(scratch));
    GetVizQuad(GetTrackerQuad(scratch), vizQuad);
    _vizManager->DrawCameraQuad(vizQuad, ::Anki::NamedColors::GREEN);
    
    return result;
  } // TrackerPredictionUpdate()
  
  void VisionSystem::AssignNameToFace(Vision::TrackedFace::ID_t faceID, const std::string& name)
  {
    if(!_isInitialized) {
      PRINT_NAMED_WARNING("VisionSystem.AssignNameToFace.NotInitialized",
                          "Cannot assign name '%s' to face ID %llu before being initialized",
                          name.c_str(), faceID);
      return;
    }
    
    ASSERT_NAMED(_faceTracker != nullptr, "FaceTracker should not be null.");
    
    _faceTracker->AssignNameToID(faceID, name);
  }
  
  void VisionSystem::EnableNewFaceEnrollment(s32 numToEnroll)
  {
    _faceTracker->EnableNewFaceEnrollment(numToEnroll);
  }
  
  Result VisionSystem::DetectFaces(const Vision::Image& grayImage,
                                   const std::vector<Quad2f>& markerQuads)
  {
    ASSERT_NAMED(_faceTracker != nullptr, "FaceTracker should not be null.");
   
    /*
    // Periodic printouts of face tracker timings
    static TimeStamp_t lastProfilePrint = 0;
    if(grayImage.GetTimestamp() - lastProfilePrint > 2000) {
      _faceTracker->PrintTiming();
      lastProfilePrint = grayImage.GetTimestamp();
    }
     */
    
    Simulator::SetFaceDetectionReadyTime();
    
    if(_faceTracker == nullptr) {
      PRINT_NAMED_ERROR("VisionSystem.Update.NullFaceTracker",
                        "In detecting faces mode, but face tracker is null.");
      return RESULT_FAIL;
    }
    
    if(!markerQuads.empty())
    {
      // Black out detected markers so we don't find faces in them
      Vision::Image maskedImage = grayImage;
      ASSERT_NAMED(maskedImage.GetTimestamp() == grayImage.GetTimestamp(),
                   "Image timestamps should match after assignment.");
      
      const cv::Rect_<f32> imgRect(0,0,grayImage.GetNumCols(),grayImage.GetNumRows());
      
      for(auto & quad : markerQuads)
      {
        Anki::Rectangle<f32> rect(quad);
        cv::Mat roi = maskedImage.get_CvMat_()(rect.get_CvRect_() & imgRect);
        roi.setTo(0);
      }
      
#     if DEBUG_FACE_DETECTION
      //_debugImageMailbox.putMessage({"MaskedFaceImage", maskedImage});
#     endif
      
      _faceTracker->Update(maskedImage);
    } else {
      // No markers were detected, so nothing to black out before looking
      // for faces
      _faceTracker->Update(grayImage);
    }
    
    for(auto & currentFace : _faceTracker->GetFaces())
    {
      ASSERT_NAMED(currentFace.GetTimeStamp() == grayImage.GetTimestamp(),
                   "Timestamp error.");
      
      // Use a camera from the robot's pose history to estimate the head's
      // 3D translation, w.r.t. that camera. Also puts the face's pose in
      // the camera's pose chain.
      currentFace.UpdateTranslation(_camera);
      
      // Make head pose w.r.t. the historical world origin
      Pose3d headPose = currentFace.GetHeadPose();
      headPose.SetParent(&_poseData.cameraPose);
      headPose = headPose.GetWithRespectToOrigin();
      currentFace.SetHeadPose(headPose);
      
      _faceMailbox.putMessage(currentFace);
    }

    return RESULT_OK;
  } // DetectFaces()
  
  
#if USE_CONNECTED_COMPONENTS_FOR_MOTION_CENTROID
  static size_t FindLargestRegionCentroid(const std::vector<std::vector<Anki::Point2i>>& regionPoints,
                                        size_t minArea, Anki::Point2f& centroid)
  {
    size_t largestRegion = 0;
    
    for(auto & region : regionPoints) {
      //PRINT_NAMED_INFO("VisionSystem.Update.FoundMotionRegion",
      //                 "Area=%lu", region.size());
      if(region.size() > minArea && region.size() > largestRegion) {
        centroid = 0.f;
        for(auto & point : region) {
          centroid += point;
        }
        centroid /= static_cast<f32>(region.size());
        largestRegion = region.size();
      }
    } // for each region
    
    return largestRegion;
  }
#endif
  
  size_t GetCentroid(const Vision::Image& motionImg, size_t minArea, Anki::Point2f& centroid)
  {
#   if USE_CONNECTED_COMPONENTS_FOR_MOTION_CENTROID
    Array2d<s32> motionRegions(motionImg.GetNumRows(), motionImg.GetNumCols());
    std::vector<std::vector<Point2<s32>>> regionPoints;
    motionImg.GetConnectedComponents(motionRegions, regionPoints);
    
    return FindLargestRegionCentroid(regionPoints, minArea, centroid);
#   else
    size_t area = 0;
    centroid = 0.f;
    
    for(s32 y=0; y<motionImg.GetNumRows(); ++y)
    {
      const u8* motionData_y = motionImg.GetRow(y);
      for(s32 x=0; x<motionImg.GetNumCols(); ++x) {
        if(motionData_y[x] != 0) {
          centroid += Anki::Point2f(x,y);
          ++area;
        }
      }
    }
    if(area > minArea) {
      centroid /= static_cast<f32>(area);
      return area;
    } else {
      centroid = 0.f;
      return 0;
    }
#   endif
  }
  
  
  Result VisionSystem::DetectMotion(const Vision::ImageRGB &imageIn)
  {
    const bool headSame =  NEAR(_poseData.poseStamp.GetHeadAngle(),
                                _prevPoseData.poseStamp.GetHeadAngle(), DEG_TO_RAD(0.1));
    
    const bool poseSame = (NEAR(_poseData.poseStamp.GetPose().GetTranslation().x(),
                                _prevPoseData.poseStamp.GetPose().GetTranslation().x(), .5f) &&
                           NEAR(_poseData.poseStamp.GetPose().GetTranslation().y(),
                                _prevPoseData.poseStamp.GetPose().GetTranslation().y(), .5f) &&
                           NEAR(_poseData.poseStamp.GetPose().GetRotation().GetAngleAroundZaxis(),
                                _prevPoseData.poseStamp.GetPose().GetRotation().GetAngleAroundZaxis(),
                                DEG_TO_RAD(0.1)));
    Vision::ImageRGB image;
    f32 scaleMultiplier = 1.f;
    const bool useHalfRes = true;
    if(useHalfRes) {
      image = Vision::ImageRGB(imageIn.GetNumRows()/2,imageIn.GetNumCols()/2);
      imageIn.Resize(image, Vision::ResizeMethod::NearestNeighbor);
      scaleMultiplier = 2.f;
    } else {
      image = imageIn;
    }
    //PRINT_STREAM_INFO("pose_angle diff = %.1f\n", RAD_TO_DEG(std::abs(_robotState.pose_angle - _prevRobotState.pose_angle)));
    
    if(headSame && poseSame && !_poseData.isMoving && !_prevImage.IsEmpty() &&
#      if USE_THREE_FRAME_MOTION_DETECTION
       !_prevPrevImage.IsEmpty() &&
#      endif
       image.GetTimestamp() - _lastMotionTime > 500)
    {
      s32 numAboveThresh = 0;
      
      std::function<u8(const Vision::PixelRGB& thisElem, const Vision::PixelRGB& otherElem)> ratioTest = [&numAboveThresh](const Vision::PixelRGB& p1, const Vision::PixelRGB& p2)
      {
        auto ratioTestHelper = [](u8 value1, u8 value2)
        {
          if(value1 > value2) {
            return static_cast<f32>(value1) / std::max(1.f, static_cast<f32>(value2));
          } else {
            return static_cast<f32>(value2) / std::max(1.f, static_cast<f32>(value1));
          }
        };
        
        u8 retVal = 0;
        const u8 minBrightness = 10;
        if(p1.IsBrighterThan(minBrightness) && p2.IsBrighterThan(minBrightness)) {
          
          const f32 ratioThreshold = 1.25f; // TODO: pass in or capture?
          const f32 ratioR = ratioTestHelper(p1.r(), p2.r());
          const f32 ratioG = ratioTestHelper(p1.g(), p2.g());
          const f32 ratioB = ratioTestHelper(p1.b(), p2.b());
          if(ratioR > ratioThreshold || ratioG > ratioThreshold || ratioB > ratioThreshold) {
            ++numAboveThresh;
            retVal = 255; // use 255 because it will actually display
          }
        } // if both pixels are bright enough
        
        return retVal;
      };
      
      Vision::Image ratio12(image.GetNumRows(), image.GetNumCols());
      image.ApplyScalarFunction(ratioTest, _prevImage, ratio12);
      
#     if USE_THREE_FRAME_MOTION_DETECTION
      Vision::Image ratio01(image.GetNumRows(), image.GetNumCols());
      _prevImage.ApplyScalarFunction(ratioTest, _prevPrevImage, ratio01);
#     endif
      
      static const cv::Matx<u8, 3, 3> kernel(cv::Matx<u8, 3, 3>::ones());
      cv::morphologyEx(ratio12.get_CvMat_(), ratio12.get_CvMat_(), cv::MORPH_OPEN, kernel);
      
#     if USE_THREE_FRAME_MOTION_DETECTION
      cv::morphologyEx(ratio01.get_CvMat_(), ratio01.get_CvMat_(), cv::MORPH_OPEN, kernel);
      cv::Mat_<u8> cvAND(255*(ratio01.get_CvMat_() & ratio12.get_CvMat_()));
      cv::Mat_<u8> cvDIFF(ratio12.get_CvMat_() - cvAND);
      Vision::Image foregroundMotion(cvDIFF);
#     else
      Vision::Image foregroundMotion = ratio12;
#     endif
      
      Anki::Point2f centroid(0.f,0.f); // Not Embedded::
      Anki::Point2f groundPlaneCentroid(0.f,0.f);
      
      // Get overall image centroid
      //#       if USE_CONNECTED_COMPONENTS_FOR_MOTION_CENTROID
      const size_t minAreaDivisor = 225; // 1/15 of each image dimension
                                         //#       else
                                         //        const size_t minAreaDivisor = 36; // 1/6 of each image dimension
                                         //#       endif
      const size_t minArea = image.GetNumElements() / minAreaDivisor;
      f32 imgRegionArea    = 0.f;
      f32 groundRegionArea = 0.f;
      if(numAboveThresh > minArea) {
        imgRegionArea = GetCentroid(foregroundMotion, minArea, centroid);
      }
      
      // Get centroid of all the motion within the ground plane, if we have one to reason about
      if(_poseData.groundPlaneVisible && _prevPoseData.groundPlaneVisible)
      {
        Quad2f imgQuad = _poseData.groundPlaneROI.GetImageQuad(_poseData.groundPlaneHomography);
        
        imgQuad *= 1.f / scaleMultiplier;
        
        const Anki::Rectangle<s32> boundingRect(imgQuad); // Not Embedded::
        Vision::Image groundPlaneForegroundMotion;
        foregroundMotion.GetROI(boundingRect).CopyTo(groundPlaneForegroundMotion);
        
        // Zero out everything in the ratio image that's not inside the ground plane quad
        imgQuad -= boundingRect.GetTopLeft();
        Vision::Image mask(groundPlaneForegroundMotion.GetNumRows(),
                           groundPlaneForegroundMotion.GetNumCols());
        mask.FillWith(0);
        cv::fillConvexPoly(mask.get_CvMat_(), std::vector<cv::Point>{
          imgQuad[Quad::CornerName::TopLeft].get_CvPoint_(),
          imgQuad[Quad::CornerName::TopRight].get_CvPoint_(),
          imgQuad[Quad::CornerName::BottomRight].get_CvPoint_(),
          imgQuad[Quad::CornerName::BottomLeft].get_CvPoint_(),
        }, 255);
        
        for(s32 i=0; i<mask.GetNumRows(); ++i) {
          const u8* maskData_i = mask.GetRow(i);
          u8* fgMotionData_i = groundPlaneForegroundMotion.GetRow(i);
          for(s32 j=0; j<mask.GetNumCols(); ++j) {
            if(maskData_i[j] == 0) {
              fgMotionData_i[j] = 0;
            }
          }
        }
        
        // Find centroid of largest connected component inside the ground plane
        const f32 imgQuadArea = imgQuad.ComputeArea();
        groundRegionArea = GetCentroid(groundPlaneForegroundMotion,
                                       imgQuadArea/static_cast<f32>(minAreaDivisor),
                                       groundPlaneCentroid);
        
        // Move back to image coordinates from ROI coordinates
        groundPlaneCentroid += boundingRect.GetTopLeft();
        
        /* Experimental: Try computing moments in an overhead warped view of the ratio image
         groundPlaneRatioImg = _poseData.groundPlaneROI.GetOverheadImage(ratioImg, _poseData.groundPlaneHomography);
         
         cv::Moments moments = cv::moments(groundPlaneRatioImg.get_CvMat_(), true);
         if(moments.m00 > 0) {
         groundMotionAreaFraction = moments.m00 / static_cast<f32>(groundPlaneRatioImg.GetNumElements());
         groundPlaneCentroid.x() = moments.m10 / moments.m00;
         groundPlaneCentroid.y() = moments.m01 / moments.m00;
         groundPlaneCentroid += _poseData.groundPlaneROI.GetOverheadImageOrigin();
         
         // TODO: return other moments?
         }
         */
        
        if(groundRegionArea > 0.f)
        {
          // Switch centroid back to original resolution, since that's where the
          // homography information is valid
          groundPlaneCentroid *= scaleMultiplier;
          
          // Make ground region area into a fraction of the ground ROI area
          groundRegionArea /= imgQuadArea;
          
          // Map the centroid onto the ground plane
          Matrix_3x3f invH;
          _poseData.groundPlaneHomography.GetInverse(invH);
          Point3f temp = invH * Point3f{groundPlaneCentroid.x(), groundPlaneCentroid.y(), 1.f};
          ASSERT_NAMED(temp.z() > 0, "Projected 'z' should be > 0.");
          const f32 divisor = 1.f/temp.z();
          groundPlaneCentroid.x() = temp.x() * divisor;
          groundPlaneCentroid.y() = temp.y() * divisor;
          
          ASSERT_NAMED(Quad2f(_poseData.groundPlaneROI.GetGroundQuad()).Contains(groundPlaneCentroid),
                       "GroundQuad should contain the ground plane centroid.");
        }
      } // if(groundPlaneVisible)
      
      if(imgRegionArea > 0 || groundRegionArea > 0.f)
      {
        PRINT_NAMED_INFO("VisionSystem.DetectMotion.FoundCentroid",
                         "Found motion centroid for %.1f-pixel area region at (%.1f,%.1f) "
                         "-- %.1f%% of ground area at (%.1f,%.1f)",
                         imgRegionArea, centroid.x(), centroid.y(),
                         groundRegionArea*100.f, groundPlaneCentroid.x(), groundPlaneCentroid.y());
        
        _lastMotionTime = image.GetTimestamp();
        
        ExternalInterface::RobotObservedMotion msg;
        msg.timestamp = image.GetTimestamp();
        
        if(imgRegionArea > 0)
        {
          ASSERT_NAMED(centroid.x() > 0.f && centroid.x() < image.GetNumCols() &&
                       centroid.y() > 0.f && centroid.y() < image.GetNumRows(),
                       "Motion centroid should be within image bounds.");
          
          // make relative to image center *at processing resolution*
          centroid -= _camera.GetCalibration().GetCenter() * (1.f/scaleMultiplier);
          
          // Filter so as not to move too much from last motion detection,
          // IFF we observed motion in the previous check
          if(_prevCentroidFilterWeight > 0.f) {
            centroid = (centroid * (1.f-_prevCentroidFilterWeight) +
                        _prevMotionCentroid * _prevCentroidFilterWeight);
            _prevMotionCentroid = centroid;
          } else {
            _prevCentroidFilterWeight = 0.1f;
          }
          
          // Convert area to fraction of image area (to be resolution-independent)
          // Using scale multiplier to return the coordinates in original image coordinates
          msg.img_x = centroid.x() * scaleMultiplier;
          msg.img_y = centroid.y() * scaleMultiplier;
          msg.img_area = imgRegionArea / static_cast<f32>(image.GetNumElements());
        } else {
          msg.img_area = 0;
          msg.img_x = 0;
          msg.img_y = 0;
          _prevCentroidFilterWeight = 0.f;
        }
        
        if(groundRegionArea > 0.f)
        {
          // Filter so as not to move too much from last motion detection,
          // IFF we observed motion in the previous check
          if(_prevGroundCentroidFilterWeight > 0.f) {
            groundPlaneCentroid = (groundPlaneCentroid * (1.f - _prevGroundCentroidFilterWeight) +
                                   _prevGroundMotionCentroid * _prevGroundCentroidFilterWeight);
            _prevGroundMotionCentroid = groundPlaneCentroid;
          } else {
            _prevGroundCentroidFilterWeight = 0.1f;
          }
          
          msg.ground_x = std::round(groundPlaneCentroid.x());
          msg.ground_y = std::round(groundPlaneCentroid.y());
          msg.ground_area = groundRegionArea;
        } else {
          msg.ground_area = 0;
          msg.ground_x = 0;
          msg.ground_y = 0;
          _prevGroundCentroidFilterWeight = 0.f;
        }
        
        _motionMailbox.putMessage(std::move(msg));
      }
      
#     if DEBUG_MOTION_DETECTION
      {
        Vision::ImageRGB ratioImgDisp(foregroundMotion);
        ratioImgDisp.DrawPoint(centroid + _camera.GetCalibration().GetCenter(), NamedColors::RED, 4);
        cv::putText(ratioImgDisp.get_CvMat_(), "Area: " + std::to_string(imgRegionArea),
                    cv::Point(0,ratioImgDisp.GetNumRows()), CV_FONT_NORMAL, .5f, CV_RGB(0,255,0));
        _debugImageRGBMailbox.putMessage({"RatioImg", ratioImgDisp});
        
        //_debugImageMailbox.putMessage({"PrevRatioImg", _prevRatioImg});
        //_debugImageMailbox.putMessage({"ForegroundMotion", foregroundMotion});
        //_debugImageMailbox.putMessage({"AND", cvAND});
        
        //          ratioImgDisp = Vision::ImageRGB(_poseData.groundPlaneROI.GetOverheadMask());
        //          if(groundRegionArea > 0.f) {
        //            ratioImgDisp.DrawPoint(groundPlaneCentroid-_poseData.groundPlaneROI.GetOverheadImageOrigin(),
        //                                   NamedColors::RED, 2);
        //
        //            cv::putText(ratioImgDisp.get_CvMat_(), "Area: " + std::to_string(groundRegionArea),
        //                        cv::Point(0,_poseData.groundPlaneROI.GetWidthFar()), CV_FONT_NORMAL, .5f,
        //                        CV_RGB(0,255,0));
        //          }
        //          _debugImageRGBMailbox.putMessage({"RatioImgGround", ratioImgDisp});
        //
        //_debugImageRGBMailbox.putMessage({"CurrentImg", image});
      }
#     endif
      
      //_prevRatioImg = ratio12;
      
    } // if(headSame && poseSame)
    
    // Store a copy of the current image for next time (at correct resolution!)
    // NOTE: Now _prevImage should correspond to _prevRobotState
    // TODO: switch to just swapping pointers between current and previous image
#   if USE_THREE_FRAME_MOTION_DETECTION
    _prevImage.CopyTo(_prevPrevImage);
#   endif
    image.CopyTo(_prevImage);
    
    return RESULT_OK;
  } // DetectMotion()
  
#if 0
#pragma mark --- Public VisionSystem API Implementations ---
#endif
  
  u32 VisionSystem::DownsampleHelper(const Array<u8>& in,
                                     Array<u8>& out,
                                     MemoryStack scratch)
  {
    const s32 inWidth  = in.get_size(1);
    //const s32 inHeight = in.get_size(0);
    
    const s32 outWidth  = out.get_size(1);
    //const s32 outHeight = out.get_size(0);
    
    const u32 downsampleFactor = inWidth / outWidth;
    
    const u32 downsamplePower = Log2u32(downsampleFactor);
    
    if(downsamplePower > 0) {
      //PRINT("Downsampling [%d x %d] frame by %d.\n", inWidth, inHeight, (1 << downsamplePower));
      
      ImageProcessing::DownsampleByPowerOfTwo<u8,u32,u8>(in,
                                                         downsamplePower,
                                                         out,
                                                         scratch);
    } else {
      // No need to downsample, just copy the buffer
      out.Set(in);
    }
    
    return downsampleFactor;
  }
  
  f32 VisionSystem::GetTrackingMarkerWidth() {
    return _markerToTrack.width_mm;
  }
  
  f32 VisionSystem::GetVerticalFOV() {
    return _headCamFOV_ver;
  }
  
  f32 VisionSystem::GetHorizontalFOV() {
    return _headCamFOV_hor;
  }
  
  const FaceDetectionParameters& VisionSystem::GetFaceDetectionParams() {
    return _faceDetectionParameters;
  }
  
  std::string VisionSystem::GetCurrentModeName() const {
    return VisionSystem::GetModeName(static_cast<VisionMode>(_mode));
  }
  
  std::string VisionSystem::GetModeName(VisionMode mode) const
  {
    
    static const std::map<VisionMode, std::string> LUT = {
      {VisionMode::Idle,                  "IDLE"}
      ,{VisionMode::DetectingMarkers,     "MARKERS"}
      ,{VisionMode::Tracking,             "TRACKING"}
      ,{VisionMode::DetectingFaces,       "FACES"}
      ,{VisionMode::DetectingMotion,      "MOTION"}
    };

    std::string retStr("");
    
    if(mode == VisionMode::Idle) {
      return LUT.at(VisionMode::Idle);
    } else {
      for(auto possibleMode : LUT) {
        if(possibleMode.first != VisionMode::Idle &&
           static_cast<u32>(mode) & static_cast<u32>(possibleMode.first))
        {
          if(!retStr.empty()) {
            retStr += "+";
          }
          retStr += possibleMode.second;
        }
      }
      return retStr;
    }
    
  } // GetModeName()
  
  Result VisionSystem::Init(const Vision::CameraCalibration& camCalib)
  {
    Result result = RESULT_OK;
    
    bool calibSizeValid = false;
    switch(camCalib.GetNcols())
    {
      case 640:
        calibSizeValid = camCalib.GetNrows() == 480;
        _captureResolution = ImageResolution::VGA;
        break;
      case 400:
        calibSizeValid = camCalib.GetNrows() == 296;
        _captureResolution = ImageResolution::CVGA;
        break;
      case 320:
        calibSizeValid = camCalib.GetNrows() == 240;
        _captureResolution = ImageResolution::QVGA;
        break;
    }
    AnkiConditionalErrorAndReturnValue(calibSizeValid, RESULT_FAIL_INVALID_SIZE,
                                       "VisionSystem.InvalidCalibrationResolution",
                                       "Unexpected calibration resolution (%dx%d)\n",
                                       camCalib.GetNcols(), camCalib.GetNrows());
    
    // WARNING: the order of these initializations matter!
    
    //
    // Initialize the VisionSystem's state (i.e. its "private member variables")
    //
    
    EnableMode(VisionMode::DetectingMarkers, true);
    //EnableMode(VisionMode::DetectingMotion,  true);
    EnableMode(VisionMode::DetectingFaces,   true);
    
    _markerToTrack.Clear();
    _numTrackFailures          = 0;
    
    _wasCalledOnce             = false;
    _havePrevPoseData          = false;
    
    PRINT_NAMED_INFO("VisionSystem.Constructor.InstantiatingFaceTracker",
                     "With model path %s.", _dataPath.c_str());
    _faceTracker = new Vision::FaceTracker(_dataPath);
    PRINT_NAMED_INFO("VisionSystem.Constructor.DoneInstantiatingFaceTracker", "");
    
    _camera.SetCalibration(camCalib);
    
    // Compute FOV from focal length (currently used for tracker prediciton)
    _headCamFOV_ver = _camera.GetCalibration().ComputeVerticalFOV().ToFloat();
    _headCamFOV_hor = _camera.GetCalibration().ComputeHorizontalFOV().ToFloat();
    
    _exposureTime = 0.2f; // TODO: pick a reasonable start value
    _frameNumber = 0;
    
    // Just make all the vision parameters' resolutions match capture resolution:
    _detectionParameters.Initialize(_captureResolution);
    _trackerParameters.Initialize(_captureResolution);
    _faceDetectionParameters.Initialize(_captureResolution);
    
    Simulator::Initialize();
    
#   ifdef RUN_SIMPLE_TRACKING_TEST
    Anki::Cozmo::VisionSystem::SetMarkerToTrack(Vision::MARKER_FIRE, DEFAULT_BLOCK_MARKER_WIDTH_MM);
#   endif
    
    result = _memory.Initialize();
    if(result != RESULT_OK) { return result; }
    
    // TODO: Re-enable debugstream/MatlabViz on Basestation visionSystem
    /*
     result = DebugStream::Initialize();
     if(result != RESULT_OK) { return result; }
     
     result = MatlabVisualization::Initialize();
     if(result != RESULT_OK) { return result; }
     */
    
#   if USE_MATLAB_TRACKER || USE_MATLAB_DETECTOR
    result = MatlabVisionProcessor::Initialize();
    if(result != RESULT_OK) { return result; }
#   endif
    
    _RcamWrtRobot = Array<f32>(3,3,_memory._onchipScratch);
    
    _markerToTrack.Clear();
    _newMarkerToTrack.Clear();
    _newMarkerToTrackWasProvided = false;
    
    // NOTE: we do NOT want to give our bogus camera its own calibration, b/c the camera
    // gets copied out in Vision::ObservedMarkers we leave in the mailbox for
    // the main engine thread. We don't want it referring to any memory allocated
    // here.
    _camera.SetSharedCalibration(&camCalib);
    
    VisionMarker::SetDataPath(_dataPath);
    
    _isInitialized = true;
    
    return result;
  } // Init()
  
  
  Result VisionSystem::SetMarkerToTrack(const Vision::MarkerType& markerTypeToTrack,
                                        const f32 markerWidth_mm,
                                        const bool checkAngleX)
  {
    const Embedded::Point2f imageCenter(-1.f, -1.f);
    const f32     searchRadius = -1.f;
    return SetMarkerToTrack(markerTypeToTrack, markerWidth_mm,
                            imageCenter, searchRadius, checkAngleX);
  }
  
  Result VisionSystem::SetMarkerToTrack(const Vision::MarkerType& markerTypeToTrack,
                                        const f32 markerWidth_mm,
                                        const Embedded::Point2f& atImageCenter,
                                        const f32 imageSearchRadius,
                                        const bool checkAngleX,
                                        const f32 postOffsetX_mm,
                                        const f32 postOffsetY_mm,
                                        const f32 postOffsetAngle_rad)
  {
    _newMarkerToTrack.type              = markerTypeToTrack;
    _newMarkerToTrack.width_mm          = markerWidth_mm;
    _newMarkerToTrack.imageCenter       = atImageCenter;
    _newMarkerToTrack.imageSearchRadius = imageSearchRadius;
    _newMarkerToTrack.checkAngleX       = checkAngleX;
    _newMarkerToTrack.postOffsetX_mm    = postOffsetX_mm;
    _newMarkerToTrack.postOffsetY_mm    = postOffsetY_mm;
    _newMarkerToTrack.postOffsetAngle_rad = postOffsetAngle_rad;
    
    // Next call to Update(), we will call UpdateMarkerToTrack() and
    // actually replace the current _markerToTrack with the one set here.
    _newMarkerToTrackWasProvided = true;
    
    return RESULT_OK;
  }
  
  const Embedded::FixedLengthList<Embedded::VisionMarker>& VisionSystem::GetObservedMarkerList()
  {
    return _memory._markers;
  } // GetObservedMarkerList()
  
  
  Result VisionSystem::GetVisionMarkerPose(const Embedded::VisionMarker& marker,
                                           const bool ignoreOrientation,
                                           Embedded::Array<f32>&  rotation,
                                           Embedded::Point3<f32>& translation)
  {
    Embedded::Quadrilateral<f32> sortedQuad;
    if(ignoreOrientation) {
      sortedQuad = marker.corners.ComputeClockwiseCorners<f32>();
    } else {
      sortedQuad = marker.corners;
    }
    
    auto const& calib = _camera.GetCalibration();
    
    return P3P::computePose(sortedQuad,
                            _canonicalMarker3d[0], _canonicalMarker3d[1],
                            _canonicalMarker3d[2], _canonicalMarker3d[3],
                            calib.GetFocalLength_x(), calib.GetFocalLength_y(),
                            calib.GetCenter_x(), calib.GetCenter_y(),
                            rotation, translation);
  } // GetVisionMarkerPose()
  
#if defined(SEND_IMAGE_ONLY)
#  error SEND_IMAGE_ONLY doesn't really make sense for Basestation vision system.
#elif defined(RUN_GROUND_TRUTHING_CAPTURE)
#  error RUN_GROUND_TRUTHING_CAPTURE not implemented in Basestation vision system.
#endif
  
  Result VisionSystem::GetImageHelper(const Vision::Image& srcImage,
                      Array<u8>& destArray)
  {
    const s32 captureHeight = destArray.get_size(0);
    const s32 captureWidth  = destArray.get_size(1);
    
    if(srcImage.GetNumRows() != captureHeight || srcImage.GetNumCols() != captureWidth) {
      PRINT_NAMED_ERROR("VisionSystem.GetImageHelper.MismatchedImageSizes",
                        "Source Vision::Image and destination Embedded::Array should "
                        "be the same size (source is %dx%d and destinatinon is %dx%d\n",
                        srcImage.GetNumRows(), srcImage.GetNumCols(),
                        captureHeight, captureWidth);
      return RESULT_FAIL_INVALID_SIZE;
    }
    
    memcpy(reinterpret_cast<u8*>(destArray.get_buffer()),
           srcImage.GetDataPointer(),
           captureHeight*captureWidth*sizeof(u8));
    
    return RESULT_OK;
    
  } // GetImageHelper()

  Result VisionSystem::PreprocessImage(Array<u8>& grayscaleImage)
  {
    
    if(_vignettingCorrection == VignettingCorrection_Software) {
      BeginBenchmark("VisionSystem_CameraImagingPipeline_Vignetting");
      
      MemoryStack _onchipScratchlocal = _memory._onchipScratch;
      FixedLengthList<f32> polynomialParameters(5, _onchipScratchlocal, Flags::Buffer(false, false, true));
      
      for(s32 i=0; i<5; i++)
        polynomialParameters[i] = _vignettingCorrectionParameters[i];
      
      CorrectVignetting(grayscaleImage, polynomialParameters);
      
      EndBenchmark("VisionSystem_CameraImagingPipeline_Vignetting");
    } // if(_vignettingCorrection == VignettingCorrection_Software)
    
    if(_autoExposure_enabled && (_frameNumber % _autoExposure_adjustEveryNFrames) == 0) {
      BeginBenchmark("VisionSystem_CameraImagingPipeline_AutoExposure");
      
      ComputeBestCameraParameters(grayscaleImage,
                                  Embedded::Rectangle<s32>(0, grayscaleImage.get_size(1)-1, 0, grayscaleImage.get_size(0)-1),
                                  _autoExposure_integerCountsIncrement,
                                  _autoExposure_highValue,
                                  _autoExposure_percentileToMakeHigh,
                                  _autoExposure_minExposureTime,
                                  _autoExposure_maxExposureTime,
                                  _autoExposure_tooHighPercentMultiplier,
                                  _exposureTime,
                                  _memory._ccmScratch);
      
      EndBenchmark("VisionSystem_CameraImagingPipeline_AutoExposure");
    }
    
    return RESULT_OK;
  } // PreprocessImage()
  
  // This is the regular Update() call
  Result VisionSystem::Update(const PoseData&            poseData,
                              const Vision::ImageRGB&    inputImage)
  {
    Result lastResult = RESULT_OK;
    
    AnkiConditionalErrorAndReturnValue(IsInitialized(), RESULT_FAIL,
                                       "VisionSystem.Update", "VisionSystem not initialized.\n");
    
    _frameNumber++;
    
    // no-op on real hardware
    if(!Simulator::IsFrameReady()) {
      return RESULT_OK;
    }
    
    // Store the new robot state and keep a copy of the previous one
    UpdatePoseData(poseData);
    
    // prevent us from trying to update a tracker we just initialized in the same
    // frame
    _trackerJustInitialized = false;
    
    // If SetMarkerToTrack() was called by main() during previous Update(),
    // actually swap in the new marker now.
    lastResult = UpdateMarkerToTrack();
    AnkiConditionalErrorAndReturnValue(lastResult == RESULT_OK, lastResult,
                                       "VisionSystem::Update()", "UpdateMarkerToTrack failed.\n");
    
    // Lots of the processing below needs a grayscale version of the image:
    const Vision::Image inputImageGray = inputImage.ToGray();
    
    // TODO: Provide a way to specify camera parameters from basestation
    //HAL::CameraSetParameters(_exposureTime, _vignettingCorrection == VignettingCorrection_CameraHardware);
    
    EndBenchmark("VisionSystem_CameraImagingPipeline");
    
    std::vector<Quad2f> markerQuads;

    if(IsModeEnabled(VisionMode::DetectingMarkers)) {
      if((lastResult = DetectMarkers(inputImageGray, markerQuads)) != RESULT_OK) {
        PRINT_NAMED_ERROR("VisionSystem.Update.LookForMarkersFailed", "");
        return lastResult;
      }
    }
    
    if(IsModeEnabled(VisionMode::Tracking)) {
      // Update the tracker transformation using this image
      if((lastResult = TrackTemplate(inputImageGray)) != RESULT_OK) {
        PRINT_NAMED_ERROR("VisionSystem.Update.TrackTemplateFailed", "");
        return lastResult;
      }
    }
    
    if(IsModeEnabled(VisionMode::DetectingFaces)) {
      if((lastResult = DetectFaces(inputImageGray, markerQuads)) != RESULT_OK) {
        PRINT_NAMED_ERROR("VisionSystem.Update.DetectFacesFailed", "");
        return lastResult;
      }
    }
    
    // DEBUG!!!!
    //EnableMode(VisionMode::DetectingMotion, true);
    
    if(IsModeEnabled(VisionMode::DetectingMotion))
    {
      if((lastResult = DetectMotion(inputImage)) != RESULT_OK) {
        PRINT_NAMED_ERROR("VisionSystem.Update.DetectMotionFailed", "");
        return lastResult;
      }
    }
    
    /*
    // Store a copy of the current image for next time
    // NOTE: Now _prevImage should correspond to _prevRobotState
    // TODO: switch to just swapping pointers between current and previous image
#   if USE_THREE_FRAME_MOTION_DETECTION
    _prevImage.CopyTo(_prevPrevImage);
#   endif
    inputImage.CopyTo(_prevImage);
    */
    
    return lastResult;
  } // Update()
  
  
  void VisionSystem::SetParams(const bool autoExposureOn,
                     const f32 exposureTime,
                     const s32 integerCountsIncrement,
                     const f32 minExposureTime,
                     const f32 maxExposureTime,
                     const u8 highValue,
                     const f32 percentileToMakeHigh)
  {
    _autoExposure_enabled = autoExposureOn;
    _exposureTime = exposureTime;
    _autoExposure_integerCountsIncrement = integerCountsIncrement;
    _autoExposure_minExposureTime = minExposureTime;
    _autoExposure_maxExposureTime = maxExposureTime;
    _autoExposure_highValue = highValue;
    _autoExposure_percentileToMakeHigh = percentileToMakeHigh;
    
    PRINT_NAMED_INFO("VisionSystem.SetParams", "Changed VisionSystem params: autoExposureOn %d exposureTime %f integerCountsInc %d, minExpTime %f, maxExpTime %f, highVal %d, percToMakeHigh %f\n",
               _autoExposure_enabled,
               _exposureTime,
               _autoExposure_integerCountsIncrement,
               _autoExposure_minExposureTime,
               _autoExposure_maxExposureTime,
               _autoExposure_highValue,
               _autoExposure_percentileToMakeHigh);
  }
  
  void VisionSystem::SetFaceDetectParams(const f32 scaleFactor,
                                         const s32 minNeighbors,
                                         const s32 minObjectHeight,
                                         const s32 minObjectWidth,
                                         const s32 maxObjectHeight,
                                         const s32 maxObjectWidth)
  {
    PRINT_STREAM_INFO("VisionSystem::SetFaceDetectParams", "Updated VisionSystem FaceDetect params");
    _faceDetectionParameters.scaleFactor = scaleFactor;
    _faceDetectionParameters.minNeighbors = minNeighbors;
    _faceDetectionParameters.minHeight = minObjectHeight;
    _faceDetectionParameters.minWidth = minObjectWidth;
    _faceDetectionParameters.maxHeight = maxObjectHeight;
    _faceDetectionParameters.maxWidth = maxObjectWidth;
  }
  
} // namespace Cozmo
} // namespace Anki

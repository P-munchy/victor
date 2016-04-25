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
#include "anki/common/basestation/math/linearAlgebra_impl.h"
#include "clad/vizInterface/messageViz.h"
#include "clad/robotInterface/messageEngineToRobot.h"
#include "clad/types/robotStatusAndActions.h"
#include "util/helpers/templateHelpers.h"
#include "util/helpers/cleanupHelper.h"
#include "util/console/consoleInterface.h"

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
#include "opencv2/calib3d/calib3d.hpp"

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

#define DRAW_TOOL_CODE_DEBUG 0
#define DRAW_CALIB_IMAGES 0

#if USE_MATLAB_TRACKER || USE_MATLAB_DETECTOR
#include "matlabVisionProcessor.h"
#endif

namespace Anki {
namespace Cozmo {
  
  CONSOLE_VAR(f32, kEdgeThreshold,  "Vision.OverheadEdges", 50.f);
  CONSOLE_VAR(u32, kMinChainLength, "Vision.OverheadEdges", 3); // in number of edge pixels
  
  CONSOLE_VAR(f32, kCalibDotSearchSize_mm,    "Vision.ToolCode",  4.5f);
  CONSOLE_VAR(f32, kCalibDotMinContrastRatio, "Vision.ToolCode",  1.1f);
  
  CONSOLE_VAR(bool, kUseHalfResMotionDetection,       "Vision.MotionDetection", true);
  CONSOLE_VAR(u32,  kLastMotionDelay_ms,              "Vision.MotionDetection", 500);
  CONSOLE_VAR(u8,   kMinBrightnessForMotionDetection, "Vision.MotionDetection", 10);
  CONSOLE_VAR(f32,  kMotionDetectRatioThreshold,      "Vision.MotionDetection", 1.25f);
  CONSOLE_VAR(f32,  kMinMotionAreaFraction,           "Vision.MotionDetection", 1.f/225.f); // 1/15 of each image dimension
  
CONSOLE_VAR(float, kMaxCalibBlobPixelArea, "kMaxCalibBlobPixelArea", 800.f); // max number of pixels in calibration pattern blob
CONSOLE_VAR(float, kMinCalibBlobPixelArea, "kMinCalibBlobPixelArea", 20.f); // min number of pixels in calibration pattern blob
CONSOLE_VAR(float, kMinCalibPixelDistBetweenBlobs, "kMinCalibPixelDistBetweenBlobs", 5.f); // min pixel distance between calibration pattern blobs
  
  using namespace Embedded;
  
  VisionSystem::VisionSystem(const std::string& dataPath, VizManager* vizMan)
  : _rollingShutterCorrector()
  , _isInitialized(false)
  , _dataPath(dataPath)
  , _faceTracker(nullptr)
  , _vizManager(vizMan)
  {
    PRINT_NAMED_INFO("VisionSystem.Constructor", "");
   
#   if RECOGNITION_METHOD == RECOGNITION_METHOD_NEAREST_NEIGHBOR
    // Force the NN library to load _now_, not on first use
    PRINT_NAMED_INFO("VisionSystem.Constructor.LoadNearestNeighborLibrary",
                     "Markers generated on %s", Vision::MarkerDefinitionVersionString);
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
    size_mm     = 0;
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
          PRINT_NAMED_INFO("VisionSystem.EnablingMode",
                           "Adding mode %s to current mode %s.",
                           VisionSystem::GetModeName(whichMode).c_str(),
                           VisionSystem::GetModeName(static_cast<VisionMode>(_mode)).c_str());
          
          _mode |= static_cast<u32>(whichMode);
        }
      } else {
        const bool modeAlreadyDisabled = !(_mode & static_cast<u32>(whichMode));
        if(!modeAlreadyDisabled) {
          PRINT_NAMED_INFO("VisionSystem.DisablingMode",
                           "Removing mode %s from current mode %s.",
                           VisionSystem::GetModeName(whichMode).c_str(),
                           VisionSystem::GetModeName(static_cast<VisionMode>(_mode)).c_str());
          _mode &= ~static_cast<u32>(whichMode);
        }
      }
    }
    return RESULT_OK;
  } // EnableMode()
  
  Result VisionSystem::EnableToolCodeCalibration(bool enable)
  {
    if(IsModeEnabled(VisionMode::ReadingToolCode)) {
      PRINT_NAMED_WARNING("VisionSystem.EnableToolCodeCalibration.AlreadyReadingToolCode",
                          "Cannot enable/disable tool code calibration while in the middle "
                          "of reading tool code.");
      return RESULT_FAIL;
    }
    
    _calibrateFromToolCode = enable;
    return RESULT_OK;
  }
  
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
  
  Result VisionSystem::UpdatePoseData(const VisionPoseData& poseData)
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
        
        AnkiConditionalErrorAndReturnValue(_markerToTrack.size_mm > 0.f,
                                           RESULT_FAIL_INVALID_PARAMETER,
                                           "VisionSystem::UpdateMarkerToTrack()",
                                           "Invalid marker size specified.");
        
        // Set canonical 3D marker's corner coordinates)
        const Anki::Point<2,P3P_PRECISION> markerHalfSize = _markerToTrack.size_mm * P3P_PRECISION(0.5);
        _canonicalMarker3d[0] = Embedded::Point3<P3P_PRECISION>(-markerHalfSize.x(), -markerHalfSize.y(), 0);
        _canonicalMarker3d[1] = Embedded::Point3<P3P_PRECISION>(-markerHalfSize.x(),  markerHalfSize.y(), 0);
        _canonicalMarker3d[2] = Embedded::Point3<P3P_PRECISION>( markerHalfSize.x(), -markerHalfSize.y(), 0);
        _canonicalMarker3d[3] = Embedded::Point3<P3P_PRECISION>( markerHalfSize.x(),  markerHalfSize.y(), 0);
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
  
  bool VisionSystem::CheckMailbox(Vision::UpdatedFaceID&  msg)
  {
    bool retVal = false;
    if(IsInitialized()) {
      retVal = _updatedFaceIdMailbox.getMessage(msg);
    }
    return retVal;
  }
  
  bool VisionSystem::CheckMailbox(OverheadEdgeFrame& msg)
  {
    bool retVal = false;
    if(IsInitialized()) {
      retVal = _overheadEdgeFrameMailbox.getMessage(msg);
    }
    return retVal;
  }
  
  bool VisionSystem::CheckMailbox(ToolCodeInfo& msg)
  {
    bool retVal = false;
    if(IsInitialized()) {
      retVal = _toolCodeMailbox.getMessage(msg);
    }
    return retVal;
  }

  bool VisionSystem::CheckMailbox(Vision::CameraCalibration& msg)
  {
    bool retVal = false;
    if(IsInitialized()) {
      retVal = _calibrationMailbox.getMessage(msg);
    }
    return retVal;
  }
  
  bool VisionSystem::CheckDebugMailbox(std::pair<std::string, Vision::Image>& msg)
  {
    bool retVal = false;
    if(IsInitialized()) {
      retVal = _debugImageMailbox.getMessage(msg);
    }
    return retVal;
  }
  
  bool VisionSystem::CheckDebugMailbox(std::pair<std::string, Vision::ImageRGB>& msg)
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
      
      markers.set_size(maxMarkers);
      for(s32 i=0; i<maxMarkers; i++) {
        Array<f32> newArray(3, 3, _memory._ccmScratch);
        markers[i].homography = newArray;
      }

      // TODO: Re-enable DebugStream for Basestation
      //MatlabVisualization::ResetFiducialDetection(grayscaleImage);
      
#     if USE_MATLAB_DETECTOR
      const Result result = MatlabVisionProcessor::DetectMarkers(grayscaleImage, markers, homographies, ccmScratch);
#     else
      const CornerMethod cornerMethod = CORNER_METHOD_LINE_FITS; // {CORNER_METHOD_LAPLACIAN_PEAKS, CORNER_METHOD_LINE_FITS};
      
      ASSERT_NAMED(_detectionParameters.fiducialThicknessFraction.x() > 0 &&
                   _detectionParameters.fiducialThicknessFraction.y(),
                   "VisionSystem.DetectMarkers.FiducialThicknessFractionParameterNotInitialized");
      
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
      embeddedParams.fiducialThicknessFraction.x = _detectionParameters.fiducialThicknessFraction.x();
      embeddedParams.fiducialThicknessFraction.y = _detectionParameters.fiducialThicknessFraction.y();
      embeddedParams.roundedCornersFraction.x = _detectionParameters.roundedCornersFraction.x();
      embeddedParams.roundedCornersFraction.y = _detectionParameters.roundedCornersFraction.y();
      embeddedParams.returnInvalidMarkers = _detectionParameters.keepUnverifiedMarkers;
      embeddedParams.doCodeExtraction = true;
      
      const Result result = DetectFiducialMarkers(grayscaleImage,
                                                  markers,
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
        
        // Instead of correcting the entire image only correct the quads
        // Apply the appropriate shift to each of the corners of the quad to get a shifted quad
        if(_doRollingShutterCorrection)
        {
          for(auto iter = quad.begin(); iter != quad.end(); iter++)
          {
            int warpIndex = floor(iter->y() / (inputImageGray.GetNumRows() / _rollingShutterCorrector.GetNumDivisions()));
            iter->x() -= _rollingShutterCorrector.GetPixelShifts()[warpIndex].x();
            iter->y() -= _rollingShutterCorrector.GetPixelShifts()[warpIndex].y();
          }
        }
        
        // The warped quad is drawn in red in the simulator
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
          // Switch to tracking mode
          EnableMode(VisionMode::Tracking, true);
          
          Anki::Embedded::Quadrilateral<f32> quad = crntMarker.corners;
          
          if(_doRollingShutterCorrection)
          {
            // The tracker needs corrected images and since we were doing marker detection (ie not warping images)
            // we need to warp the image and the marker's quad that get passed into InitTemplate
            Vision::Image warpedImage = _rollingShutterCorrector.WarpImage(inputImageGray);
            
            GetImageHelper(warpedImage, grayscaleImage);
            PreprocessImage(grayscaleImage);
            
            for(int i=0; i<4; i++)
            {
              int warpIndex = floor(quad.corners[i].y / (inputImageGray.GetNumRows() / _rollingShutterCorrector.GetNumDivisions()));
              quad.corners[i].x -= _rollingShutterCorrector.GetPixelShifts()[warpIndex].x();
              quad.corners[i].y -= _rollingShutterCorrector.GetPixelShifts()[warpIndex].y();
              
              if(quad.corners[i].x >= inputImageGray.GetNumCols() ||
                 quad.corners[i].y >= inputImageGray.GetNumRows() ||
                 quad.corners[i].x < 0 ||
                 quad.corners[i].y < 0)
              {
                quad = crntMarker.corners;
                PRINT_NAMED_INFO("VisionSystem.DetectMarkers", "Reiniting tracker and warping quad put it off the image using unwarped quad");
                break;
              }
            }
          }
          
          if((lastResult = InitTemplate(grayscaleImage, quad)) != RESULT_OK) {
            PRINT_NAMED_ERROR("VisionSystem.LookForMarkers.InitTemplateFailed","");
            // If InitTemplate failed then make sure to disable tracking mode
            EnableMode(VisionMode::Tracking, false);
            return lastResult;
          }
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
    AnkiAssert(_markerToTrack.size_mm > 0);

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
    
    auto calib = _camera.GetCalibration();
    
    AnkiConditionalErrorAndReturnValue(calib != nullptr, RESULT_FAIL, "VisionSystem.Update", "Camera calib is nullptr");
    
    _tracker = TemplateTracker::LucasKanadeTracker_SampledPlanar6dof(grayscaleImage,
                                                                    trackingQuad,
                                                                    _trackerParameters.scaleTemplateRegionPercent,
                                                                    _trackerParameters.numPyramidLevels,
                                                                    Transformations::TRANSFORM_PROJECTIVE,
                                                                    _trackerParameters.numFiducialEdgeSamples,
                                                                     Embedded::Point<f32>(_detectionParameters.fiducialThicknessFraction.x(),
                                                                                          _detectionParameters.fiducialThicknessFraction.y()),
                                                                     Embedded::Point<f32>(_trackerParameters.roundedCornersFraction.x(),
                                                                                          _trackerParameters.roundedCornersFraction.y()),
                                                                    _trackerParameters.numInteriorSamples,
                                                                    _trackerParameters.numSamplingRegions,
                                                                    calib->GetFocalLength_x(),
                                                                    calib->GetFocalLength_y(),
                                                                    calib->GetCenter_x(),
                                                                    calib->GetCenter_y(),
                                                                    Embedded::Point2f(_markerToTrack.size_mm.x(),
                                                                                      _markerToTrack.size_mm.y()),
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
    
    // Draw the converged tracker quad in either cyan or magenta
    Embedded::Quadrilateral<f32> quad = GetTrackerQuad(onchipScratch);
    Anki::Quad2f vizQuad;
    GetVizQuad(quad, vizQuad);
    _vizManager->DrawCameraQuad(vizQuad, (trackingSucceeded ? Anki::NamedColors::CYAN : Anki::NamedColors::MAGENTA));
    
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
                         _markerToTrack.size_mm,
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
  
  void VisionSystem::AssignNameToFace(Vision::FaceID_t faceID, const std::string& name)
  {
    if(!_isInitialized) {
      PRINT_NAMED_WARNING("VisionSystem.AssignNameToFace.NotInitialized",
                          "Cannot assign name '%s' to face ID %d before being initialized",
                          name.c_str(), faceID);
      return;
    }
    
    ASSERT_NAMED(_faceTracker != nullptr, "VisionSystem.AssignNameToFace.NullFaceTracker");
    
    _faceTracker->AssignNameToID(faceID, name);
  }
    
  void VisionSystem::EraseFace(const std::string& name)
  {
    _faceTracker->EraseName(name);
  }
  
  void VisionSystem::SetFaceEnrollmentMode(Vision::FaceEnrollmentMode mode)
  {
    _faceTracker->SetFaceEnrollmentMode(mode);
  }
  
  Result VisionSystem::DetectFaces(const Vision::Image& grayImage,
                                   const std::vector<Quad2f>& markerQuads)
  {
    ASSERT_NAMED(_faceTracker != nullptr, "VisionSystem.DetectFaces.NullFaceTracker");
   
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
    
    std::list<Vision::TrackedFace> faces;
    std::list<Vision::UpdatedFaceID> updatedIDs;
    
    if(!markerQuads.empty())
    {
      // Black out detected markers so we don't find faces in them
      Vision::Image maskedImage = grayImage;
      ASSERT_NAMED(maskedImage.GetTimestamp() == grayImage.GetTimestamp(),
                   "VisionSystem.DetectFaces.BadImageTimestamp");
      
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
      
      _faceTracker->Update(maskedImage, faces, updatedIDs);
    } else {
      // No markers were detected, so nothing to black out before looking
      // for faces
      _faceTracker->Update(grayImage, faces, updatedIDs);
    }
    
    for(auto & currentFace : faces)
    {
      ASSERT_NAMED(currentFace.GetTimeStamp() == grayImage.GetTimestamp(),
                   "VisionSystem.DetectFaces.BadFaceTimestamp");
      
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
    
    for(auto & updatedID : updatedIDs)
    {
      _updatedFaceIdMailbox.putMessage(updatedID);
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
      //                 "Area=%lu", (unsigned long)region.size());
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
                                _prevPoseData.poseStamp.GetHeadAngle(), DEG_TO_RAD_F32(0.1));
    
    const bool poseSame = (NEAR(_poseData.poseStamp.GetPose().GetTranslation().x(),
                                _prevPoseData.poseStamp.GetPose().GetTranslation().x(), .5f) &&
                           NEAR(_poseData.poseStamp.GetPose().GetTranslation().y(),
                                _prevPoseData.poseStamp.GetPose().GetTranslation().y(), .5f) &&
                           NEAR(_poseData.poseStamp.GetPose().GetRotation().GetAngleAroundZaxis().ToFloat(),
                                _prevPoseData.poseStamp.GetPose().GetRotation().GetAngleAroundZaxis().ToFloat(),
                                DEG_TO_RAD_F32(0.1)));
    Vision::ImageRGB image;
    f32 scaleMultiplier = 1.f;
    if(kUseHalfResMotionDetection) {
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
       image.GetTimestamp() - _lastMotionTime > kLastMotionDelay_ms)
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
        if(p1.IsBrighterThan(kMinBrightnessForMotionDetection) &&
           p2.IsBrighterThan(kMinBrightnessForMotionDetection))
        {
          const f32 ratioR = ratioTestHelper(p1.r(), p2.r());
          const f32 ratioG = ratioTestHelper(p1.g(), p2.g());
          const f32 ratioB = ratioTestHelper(p1.b(), p2.b());
          if(ratioR > kMotionDetectRatioThreshold || ratioG > kMotionDetectRatioThreshold || ratioB > kMotionDetectRatioThreshold) {
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
      const size_t minArea = (f32)image.GetNumElements() * kMinMotionAreaFraction;
      f32 imgRegionArea    = 0.f;
      f32 groundRegionArea = 0.f;
      if(numAboveThresh > minArea) {
        imgRegionArea = GetCentroid(foregroundMotion, minArea, centroid);
      }
      
      // Get centroid of all the motion within the ground plane, if we have one to reason about
      if(_poseData.groundPlaneVisible && _prevPoseData.groundPlaneVisible)
      {
        Quad2f imgQuad;
        _poseData.groundPlaneROI.GetImageQuad(_poseData.groundPlaneHomography,
                                              image.GetNumCols(), image.GetNumRows(),
                                              imgQuad);
        
        imgQuad *= 1.f / scaleMultiplier;
        
        Anki::Rectangle<s32> boundingRect(imgQuad); // Not Embedded::
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
                                       imgQuadArea*kMinMotionAreaFraction,
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
          ASSERT_NAMED(temp.z() > 0, "VisionSystem.DetectMotion.BadProjectedZ");
          const f32 divisor = 1.f/temp.z();
          groundPlaneCentroid.x() = temp.x() * divisor;
          groundPlaneCentroid.y() = temp.y() * divisor;
          
          ASSERT_NAMED(Quad2f(_poseData.groundPlaneROI.GetGroundQuad()).Contains(groundPlaneCentroid),
                       "VisionSystem.DetectMotion.BadGroundPlaneCentroid");
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
                       "VisionSystem.DetectMotion.CentroidOOB");
          
          // make relative to image center *at processing resolution*
          ASSERT_NAMED(_camera.IsCalibrated(), "Camera must be calibrated");
          centroid -= _camera.GetCalibration()->GetCenter() * (1.f/scaleMultiplier);
          
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
  

  
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  void AddEdgePoint(const OverheadEdgePoint& pointInfo, bool isBorder, std::vector<OverheadEdgePointChain>& imageChains )
  {
    const f32 kMaxDistBetweenEdges_mm = 5.f; // start new chain after this distance seen
    
    // can we add to the current image chain?
    bool addToCurrentChain = false;
    if ( !imageChains.empty() )
    {
      OverheadEdgePointChain& currentChain = imageChains.back();
      if ( currentChain.points.empty() )
      {
        // current chain does not have points yet, we can add this one as the first one
        addToCurrentChain = true;
      }
      else
      {
        // there are points, does the chain and this point match border vs no_border flag?
        if ( isBorder == currentChain.isBorder )
        {
          // they do, is the new point close enough to the last point in the current chain?
          const f32 distToPrevPoint = ComputeDistanceBetween(pointInfo.position, imageChains.back().points.back().position);
          if ( distToPrevPoint <= kMaxDistBetweenEdges_mm )
          {
            // it is close, this point should be added to the current chain
            addToCurrentChain = true;
          }
        }
      }
    }
    
    // if we don't want to add the point to the current chain, then we need to start a new chain
    if ( !addToCurrentChain )
    {
      imageChains.emplace_back();
      imageChains.back().isBorder = isBorder;
    }
    
    // add to current chain (can be the newly created for this border)
    OverheadEdgePointChain& newCurrentChain = imageChains.back();
    
    // if we have an empty chain, set isBorder now
    if ( newCurrentChain.points.empty() ) {
      newCurrentChain.isBorder = isBorder;
    } else {
      ASSERT_NAMED(newCurrentChain.isBorder == isBorder, "VisionSystem.AddEdgePoint.BadBorderFlag");
    }
    
    // now add this point
    newCurrentChain.points.emplace_back( pointInfo );
  }
  
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 
#define DRAW_OVERHEAD_IMAGE_EDGES_DEBUG 0
  
  namespace {
    
    inline void SetEdgePosition(const Matrix_3x3f& invH,
                                s32 i, s32 j,
                                OverheadEdgePoint& edgePoint)
    {
      // Project point onto ground plane
      // Note that b/c we are working transposed, i is x and j is y in the
      // original image.
      Point3f temp = invH * Point3f(i, j, 1.f);
      ASSERT_NAMED(temp.z() > 0, "VisionSystem.SetEdgePositionHelper.BadProjectedZ");
      const f32 divisor = 1.f / temp.z();
      
      edgePoint.position.x() = temp.x() * divisor;
      edgePoint.position.y() = temp.y() * divisor;
    }
    
  } // anonymous namespace
  
  Result VisionSystem::DetectOverheadEdges(const Vision::ImageRGB &image)
  {
    // if the ground plane is not currently visible, do not detect edges
    if ( !_poseData.groundPlaneVisible )
    {
      OverheadEdgeFrame edgeFrame;
      edgeFrame.timestamp = image.GetTimestamp();
      edgeFrame.groundPlaneValid = false;
      _overheadEdgeFrameMailbox.putMessage( std::move(edgeFrame) );
      return RESULT_OK;
    }
    
    // Get ROI around ground plane quad in image
    const Matrix_3x3f& H = _poseData.groundPlaneHomography;
    const GroundPlaneROI& roi = _poseData.groundPlaneROI;
    Quad2f groundInImage;
    roi.GetImageQuad(H, image.GetNumCols(), image.GetNumRows(), groundInImage);
    
    Anki::Rectangle<s32> bbox(groundInImage);
    Vision::ImageRGB imageROI = image.GetROI(bbox);
    
    // Find edges in that ROI
    // Custom Gaussian derivative in x direction, sigma=1, with a little extra space
    // in the middle to help detect soft edges
    // (scaled such that each half has absolute sum of 1.0, so it's normalized)
    Tic("EdgeDetection");
    const SmallMatrix<7,5, f32> kernel{
      0.0168,    0.0754,    0.1242,   0.0754,  0.0168,
      0.0377,    0.1689,    0.2784,   0.1689,  0.0377,
      0,0,0,0,0,
      0,0,0,0,0,
      0,0,0,0,0,
      -0.0377,  -0.1689,   -0.2784,  -0.1689, -0.0377,
      -0.0168,  -0.0754,   -0.1242,  -0.0754, -0.0168,
    };
    
    /*
    const SmallMatrix<7, 5, s16> kernel{
      9,    39,    64,    39,     9,
      19,    86,   143,    86,    19,
      0,0,0,0,0,
      0,0,0,0,0,
      0,0,0,0,0,
      -19,   -86,  -143,   -86,   -19,
      -9,   -39,   -64,   -39,    -9
    };
    */
    
    Array2d<Vision::PixelRGB_<s16>> edgeImgX(image.GetNumRows(), image.GetNumCols());
    cv::filter2D(imageROI.get_CvMat_(), edgeImgX.GetROI(bbox).get_CvMat_(), CV_16S, kernel.get_CvMatx_());
    Toc("EdgeDetection");
    
    Tic("GroundQuadEdgeMasking");
    // Remove edges that aren't in the ground plane quad (as opposed to its bounding rectangle)
    Vision::Image mask(edgeImgX.GetNumRows(), edgeImgX.GetNumCols());
    mask.FillWith(255);
    cv::fillConvexPoly(mask.get_CvMat_(), std::vector<cv::Point>{
        groundInImage[Quad::CornerName::TopLeft].get_CvPoint_(),
        groundInImage[Quad::CornerName::TopRight].get_CvPoint_(),
        groundInImage[Quad::CornerName::BottomRight].get_CvPoint_(),
        groundInImage[Quad::CornerName::BottomLeft].get_CvPoint_(),
      }, 0);
    
    edgeImgX.SetMaskTo(mask, 0);
    Toc("GroundQuadEdgeMasking");
    
    std::vector<OverheadEdgePointChain> candidateChains;
    
    // Find first strong edge in each column, in the ground plane mask, working
    // upward from bottom.
    // Note: looping only over the ROI portion of full image, but working in
    //       full-image coordinates so that H directly applies
    // Note: transposing so we can work along rows, which is more efficient.
    //       (this also means using bbox.X for transposed rows and bbox.Y for transposed cols)
    Tic("FindingGroundEdgePoints");
    Matrix_3x3f invH;
    H.GetInverse(invH);
    Array2d<Vision::PixelRGB_<f32>> edgeTrans(edgeImgX.get_CvMat_().t());
    OverheadEdgePoint edgePoint;
    for(s32 i=bbox.GetX(); i<bbox.GetXmax(); ++i)
    {
      bool foundBorder = false;
      const Vision::PixelRGB_<f32>* edgeTrans_i = edgeTrans.GetRow(i);
      
      // Right to left in transposed image ==> bottom to top in original image
      for(s32 j=bbox.GetYmax()-1; j>=bbox.GetY(); --j)
      {
        auto & edgePixelX = edgeTrans_i[j];
        if(std::abs(edgePixelX.r()) > kEdgeThreshold ||
           std::abs(edgePixelX.g()) > kEdgeThreshold ||
           std::abs(edgePixelX.b()) > kEdgeThreshold)
        {
          // Project point onto ground plane
          // Note that b/c we are working transposed, i is x and j is y in the
          // original image.
          SetEdgePosition(invH, i, j, edgePoint);
          edgePoint.gradient = {edgePixelX.r(), edgePixelX.g(), edgePixelX.b()};
          
          foundBorder = true;
          AddEdgePoint(edgePoint, foundBorder, candidateChains);
          break; // only keep first edge found in each row (working right to left)
        }
      }
      
      // if we did not find border, report lack of border for this row
      if ( !foundBorder )
      {
        const bool isInsideGroundQuad = (i >= groundInImage[Quad::TopLeft].x() &&
                                         i <= groundInImage[Quad::TopRight].x());
        
        if(isInsideGroundQuad)
        {
          // Project point onto ground plane
          // Note that b/c we are working transposed, i is x and j is y in the
          // original image.
          SetEdgePosition(invH, i, bbox.GetY(), edgePoint);
          edgePoint.gradient = 0;
          AddEdgePoint(edgePoint, foundBorder, candidateChains);
        }
      }
      
    }
    Toc("FindingGroundEdgePoints");
    
    if(DRAW_OVERHEAD_IMAGE_EDGES_DEBUG)
    {
      Vision::ImageRGB overheadImg = roi.GetOverheadImage(image, H);
      
      static const std::vector<ColorRGBA> lineColorList = {
        NamedColors::RED, NamedColors::GREEN, NamedColors::BLUE,
        NamedColors::ORANGE, NamedColors::CYAN, NamedColors::YELLOW,
      };
      auto color = lineColorList.begin();
      Vision::ImageRGB dispImg(overheadImg.GetNumRows(), overheadImg.GetNumCols());
      overheadImg.CopyTo(dispImg);
      static const Anki::Point2f dispOffset(-roi.GetDist(), roi.GetWidthFar()*0.5f);
      Quad2f tempQuad(roi.GetGroundQuad());
      tempQuad += dispOffset;
      dispImg.DrawQuad(tempQuad, NamedColors::RED, 1);
      
      for(auto & chain : candidateChains)
      {
        if(chain.points.size() >= kMinChainLength)
        {
          for(s32 i=1; i<chain.points.size(); ++i) {
            Anki::Point2f startPoint(chain.points[i-1].position);
            startPoint.y() = -startPoint.y();
            startPoint += dispOffset;
            Anki::Point2f endPoint(chain.points[i].position);
            endPoint.y() = -endPoint.y();
            endPoint += dispOffset;
            dispImg.DrawLine(startPoint, endPoint, *color, 1);
          }
          ++color;
          if(color == lineColorList.end()) {
            color = lineColorList.begin();
          }
        }
      }
      Vision::ImageRGB dispEdgeImg(edgeImgX.GetNumRows(), edgeImgX.GetNumCols());
      std::function<Vision::PixelRGB(const Vision::PixelRGB_<s16>&)> fcn = [](const Vision::PixelRGB_<s16>& pixelS16)
      {
        return Vision::PixelRGB((u8)std::abs(pixelS16.r()),
                                (u8)std::abs(pixelS16.g()),
                                (u8)std::abs(pixelS16.b()));
      };
      edgeImgX.ApplyScalarFunction(fcn, dispEdgeImg);
      
      // Project edges on the ground back into image for display
      for(auto & chain : candidateChains)
      {
        for(s32 i=0; i<chain.points.size(); ++i) {
          const Anki::Point2f& groundPoint = chain.points[i].position;
          Point3f temp = H * Anki::Point3f(groundPoint.x(), groundPoint.y(), 1.f);
          ASSERT_NAMED(temp.z() > 0.f, "VisionSystem.DetectOverheadEdges.BadDisplayZ");
          const f32 divisor = 1.f / temp.z();
          dispEdgeImg.DrawPoint({temp.x()*divisor, temp.y()*divisor}, NamedColors::RED, 1);
        }
      }
      dispEdgeImg.DrawQuad(groundInImage, NamedColors::GREEN, 1);
      //dispImg.Display("OverheadImage", 1);
      //dispEdgeImg.Display("OverheadEdgeImage");
      _debugImageRGBMailbox.putMessage({"OverheadImage", dispImg});
      _debugImageRGBMailbox.putMessage({"EdgeImage", dispEdgeImg});
    } // if(DRAW_OVERHEAD_IMAGE_EDGES_DEBUG)
    
    // create edge frame info to send
    OverheadEdgeFrame edgeFrame;
    edgeFrame.timestamp = image.GetTimestamp();
    edgeFrame.groundPlaneValid = true;
    
    roi.GetVisibleGroundQuad(H, image.GetNumCols(), image.GetNumRows(), edgeFrame.groundplane);
    
    // Copy only the chains with at least k points (less is considered noise)
    for(auto& chain : candidateChains)
    {
      // filter chains that don't have a minimum number of points
      if ( chain.points.size() >= kMinChainLength ) {
        edgeFrame.chains.emplace_back( std::move(chain) );
      }
    }
    candidateChains.clear(); // some chains are in undefined state after std::move, clear them now
    
    // put in mailbox
    _overheadEdgeFrameMailbox.putMessage( std::move(edgeFrame) );
    
    return RESULT_OK;
  }
  
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
  
  const Anki::Point2f& VisionSystem::GetTrackingMarkerSize() {
    return _markerToTrack.size_mm;
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
      {VisionMode::Idle,                     "IDLE"}
      ,{VisionMode::DetectingMarkers,        "MARKERS"}
      ,{VisionMode::Tracking,                "TRACKING"}
      ,{VisionMode::DetectingFaces,          "FACES"}
      ,{VisionMode::DetectingMotion,         "MOTION"}
      ,{VisionMode::ReadingToolCode,         "READTOOLCODE"}
      ,{VisionMode::ComputingCalibration,    "CALIBRATION"}
      ,{VisionMode::DetectingOverheadEdges,  "OVERHEADEDGES"}
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
  
  Result VisionSystem::Init(Vision::CameraCalibration& camCalib)
  {
    Result result = RESULT_OK;
    
    Profiler::SetProfileGroupName("VisionSystem");
    
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
                                       "VisionSystem.Init.InvalidCalibrationResolution",
                                       "Unexpected calibration resolution (%dx%d)\n",
                                       camCalib.GetNcols(), camCalib.GetNrows());
    
    // WARNING: the order of these initializations matter!
    
    //
    // Initialize the VisionSystem's state (i.e. its "private member variables")
    //
    
    // Default processing modes to enable
    EnableMode(VisionMode::DetectingMarkers, true);
    //EnableMode(VisionMode::DetectingMotion,  true);
    EnableMode(VisionMode::DetectingFaces,   true);
    //EnableMode(VisionMode::DetectingOverheadEdges, true);
    
    _markerToTrack.Clear();
    _numTrackFailures          = 0;
    
    _wasCalledOnce             = false;
    _havePrevPoseData          = false;
    
    PRINT_NAMED_INFO("VisionSystem.Init.InstantiatingFaceTracker",
                     "With model path %s.", _dataPath.c_str());
    
    // Read in parameters from the Json config file:
    Json::Value config;
    const std::string paramsFilename(_dataPath + "/visionParameters.json");
    std::ifstream jsonFile(paramsFilename);
    Json::Reader reader;
    const bool success = reader.parse(jsonFile, config);
    jsonFile.close();
    if(! success) {
      PRINT_NAMED_ERROR("VisionSystem.Init.JsonReadFail",
                        "Failed to parse Json file %s.", paramsFilename.c_str());
      return RESULT_FAIL;
    }

    _faceTracker = new Vision::FaceTracker(_dataPath, config);
    PRINT_NAMED_INFO("VisionSystem.Constructor.DoneInstantiatingFaceTracker", "");
    
    // NOTE: we do NOT want to give our bogus camera its own calibration, b/c the camera
    // gets copied out in Vision::ObservedMarkers we leave in the mailbox for
    // the main engine thread. We don't want it referring to any memory allocated
    // here.
    _camera.SetSharedCalibration(&camCalib);
    
    // Compute FOV from focal length (currently used for tracker prediciton)
    _headCamFOV_ver = _camera.GetCalibration()->ComputeVerticalFOV().ToFloat();
    _headCamFOV_hor = _camera.GetCalibration()->ComputeHorizontalFOV().ToFloat();
    
    _exposureTime = 0.2f; // TODO: pick a reasonable start value
    _frameNumber = 0;
    
    // Just make all the vision parameters' resolutions match capture resolution:
    _detectionParameters.Initialize(_captureResolution);
    _trackerParameters.Initialize(_captureResolution,
                                  _detectionParameters.fiducialThicknessFraction,
                                  _detectionParameters.roundedCornersFraction);
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
    
    VisionMarker::SetDataPath(_dataPath);
    
    _isCalibrating = false;
    
    _isInitialized = true;
    
    return result;
  } // Init()
  
  
  Result VisionSystem::SetMarkerToTrack(const Vision::MarkerType& markerTypeToTrack,
                                        const Anki::Point2f& markerSize_mm,
                                        const bool checkAngleX)
  {
    const Embedded::Point2f imageCenter(-1.f, -1.f);
    const f32     searchRadius = -1.f;
    return SetMarkerToTrack(markerTypeToTrack, markerSize_mm,
                            imageCenter, searchRadius, checkAngleX);
  }
  
  Result VisionSystem::SetMarkerToTrack(const Vision::MarkerType& markerTypeToTrack,
                                        const Anki::Point2f& markerSize_mm,
                                        const Embedded::Point2f& atImageCenter,
                                        const f32 imageSearchRadius,
                                        const bool checkAngleX,
                                        const f32 postOffsetX_mm,
                                        const f32 postOffsetY_mm,
                                        const f32 postOffsetAngle_rad)
  {
    _newMarkerToTrack.type              = markerTypeToTrack;
    _newMarkerToTrack.size_mm           = markerSize_mm;
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
    
    ASSERT_NAMED(_camera.IsCalibrated(), "VisionSystem.GetVisionMarkerPose.CameraNotCalibrated");
    auto calib = _camera.GetCalibration();
    ASSERT_NAMED(calib != nullptr, "VisionSystem.GetVisionMarkerPose.NullCalibration");
    
    return P3P::computePose(sortedQuad,
                            _canonicalMarker3d[0], _canonicalMarker3d[1],
                            _canonicalMarker3d[2], _canonicalMarker3d[3],
                            calib->GetFocalLength_x(), calib->GetFocalLength_y(),
                            calib->GetCenter_x(), calib->GetCenter_y(),
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
  
  Result VisionSystem::AddCalibrationImage(const Vision::Image& calibImg)
  {
    if(_isCalibrating) {
      PRINT_NAMED_INFO("VisionSystem.AddCalibrationImage.AlreadyCalibrating",
                       "Cannot add calibration image while already in the middle of doing calibration.");
      return RESULT_FAIL;
    }
    
    _calibImages.push_back(calibImg);
    PRINT_NAMED_INFO("VisionSystem.AddCalibrationImage",
                     "Num images including this: %u", (u32)_calibImages.size());
    return RESULT_OK;
  } // AddCalibrationImage()
  
  Result VisionSystem::ClearCalibrationImages()
  {
    if(_isCalibrating) {
      PRINT_NAMED_INFO("VisionSystem.ClearCalibrationImages.AlreadyCalibrating",
                       "Cannot clear calibration images while already in the middle of doing calibration.");
      return RESULT_FAIL;
    }
    
    _calibImages.clear();
    return RESULT_OK;
  }
  
  // This is the regular Update() call
  Result VisionSystem::Update(const VisionPoseData&      poseData,
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
    //const Vision::Image inputImageGray = inputImage.ToGray();
    
    Vision::Image inputImageGray = inputImage.ToGray();
    if(_doRollingShutterCorrection)
    {
      Tic("RollingShutterComputePixelShifts");
      _rollingShutterCorrector.ComputePixelShifts(poseData, _prevPoseData);
      Toc("RollingShutterComputePixelShifts");
      
      if(IsModeEnabled(VisionMode::Tracking))
      {
        Tic("RollingShutterWarpImage");
        inputImageGray = _rollingShutterCorrector.WarpImage(inputImageGray);
        Toc("RollingShutterWarpImage");
      }
    }
    
    // TODO: Provide a way to specify camera parameters from basestation
    //HAL::CameraSetParameters(_exposureTime, _vignettingCorrection == VignettingCorrection_CameraHardware);
    
    EndBenchmark("VisionSystem_CameraImagingPipeline");
    
    std::vector<Quad2f> markerQuads;

    if(IsModeEnabled(VisionMode::DetectingMarkers)) {
      Tic("TotalDetectingMarkers");
      if((lastResult = DetectMarkers(inputImageGray, markerQuads)) != RESULT_OK) {
        PRINT_NAMED_ERROR("VisionSystem.Update.LookForMarkersFailed", "");
        return lastResult;
      }
      Toc("TotalDetectingMarkers");
    }
    
    if(IsModeEnabled(VisionMode::Tracking)) {
      // Update the tracker transformation using this image
      if((lastResult = TrackTemplate(inputImageGray)) != RESULT_OK) {
        PRINT_NAMED_ERROR("VisionSystem.Update.TrackTemplateFailed", "");
        return lastResult;
      }
    }
    
    if(IsModeEnabled(VisionMode::DetectingFaces)) {
      Tic("TotalDetectingFaces");
      if((lastResult = DetectFaces(inputImageGray, markerQuads)) != RESULT_OK) {
        PRINT_NAMED_ERROR("VisionSystem.Update.DetectFacesFailed", "");
        return lastResult;
      }
      Toc("TotalDetectingFaces");
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
    
    if(IsModeEnabled(VisionMode::DetectingOverheadEdges))
    {
      Tic("TotalDetectingOverheadEdges");
      if((lastResult = DetectOverheadEdges(inputImage)) != RESULT_OK) {
        PRINT_NAMED_ERROR("VisionSystem.Update.DetectOverheadEdgesFailed", "");
        return lastResult;
      }
      Toc("TotalDetectingOverheadEdges");
    }
    
    if(IsModeEnabled(VisionMode::ReadingToolCode))
    {
      if((lastResult = ReadToolCode(inputImageGray)) != RESULT_OK) {
        PRINT_NAMED_ERROR("VisionSystem.Update.ReadToolCodeFailed", "");
        return lastResult;
      }
    }
    
    if(IsModeEnabled(VisionMode::ComputingCalibration) && _calibImages.size() >= _kMinNumCalibImagesRequired)
    {
      if((lastResult = ComputeCalibration()) != RESULT_OK) {
        PRINT_NAMED_ERROR("VisionSystem.Update.ComputeCalibrationFailed", "");
        return lastResult;
      }
    }

    static s32 profilePrintCtr = 600;
    if(--profilePrintCtr == 0) {
      Profiler::PrintAverageTiming();
      profilePrintCtr = 60;
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
  
  Result VisionSystem::ReadToolCode(const Vision::Image& image)
  {
    ToolCodeInfo readToolCodeMessage;
    readToolCodeMessage.code = ToolCode::UnknownTool;
    
    // Guarantee CheckingToolCode mode gets disabled and code read gets sent,
    // no matter how we return from this function
    Util::CleanupHelper disableCheckToolCode([this,&readToolCodeMessage]() {
      this->_toolCodeMailbox.putMessage(readToolCodeMessage);
      this->EnableMode(VisionMode::ReadingToolCode, false);
      PRINT_NAMED_INFO("VisionSystem.ReadToolCode.DisabledReadingToolCode", "");
    });
    
    // All the conditions that must be met to bother trying to read the tool code:
    const bool liftMoving = !NEAR_ZERO(_poseData.poseStamp.GetLiftAngle()-_prevPoseData.poseStamp.GetLiftAngle());
    
    const bool headMoving = !NEAR_ZERO(_poseData.poseStamp.GetHeadAngle()-_prevPoseData.poseStamp.GetHeadAngle());
    
    //const bool longEnoughSinceLastRead = image.GetTimestamp() - _lastToolCodeReadTime_ms > kToolCodeReadPeriod_ms;
    
    const bool liftDown = Robot::ConvertLiftAngleToLiftHeightMM(_poseData.poseStamp.GetLiftAngle()) <= LIFT_HEIGHT_LOWDOCK;
    
    // Sanity checks: we should not even be calling ReadToolCode if everybody
    // hasn't done their job and got us into position
    if(liftMoving || headMoving || !liftDown)
    {
      PRINT_NAMED_ERROR("VisionSystem.ReadToolCode.NotInPosition",
                        "LiftMoving=%d, HeadMoving=%d, LiftDown=%d",
                        liftMoving, headMoving, liftDown);
      return RESULT_FAIL;
    }
    
    // Center points of the calibration dots, in lift coordinate frame
    // TODO: Move these to be defined elsewhere
    const s32 LEFT_DOT = 0, RIGHT_DOT = 1;
    const std::vector<Point3f> toolCodeDotsWrtLift = {
      {1.5f,  10.f, LIFT_XBAR_HEIGHT_WRT_WRIST_JOINT}, // Left in image
      {1.5f, -10.f, LIFT_XBAR_HEIGHT_WRT_WRIST_JOINT}, // Right in image
    };
    
    const Pose3d liftBasePose(0.f, Y_AXIS_3D(), {LIFT_BASE_POSITION[0], LIFT_BASE_POSITION[1], LIFT_BASE_POSITION[2]}, &_poseData.poseStamp.GetPose(), "RobotLiftBase");
    
    Pose3d liftPose(0.f, Y_AXIS_3D(), {LIFT_ARM_LENGTH, 0.f, 0.f}, &liftBasePose, "RobotLift");
    
    Robot::ComputeLiftPose(_poseData.poseStamp.GetLiftAngle(), liftPose);
    
    Pose3d liftPoseWrtCam;
    if(false == liftPose.GetWithRespectTo(_poseData.cameraPose, liftPoseWrtCam)) {
      PRINT_NAMED_ERROR("VisionSystem.ReadToolCode.PoseTreeError",
                        "Could not get lift pose w.r.t. camera pose.");
      return RESULT_FAIL;
    }
    
    // Put tool code dots in camera coordinate frame
    std::vector<Point3f> toolCodeDotsWrtCam;
    liftPoseWrtCam.ApplyTo(toolCodeDotsWrtLift, toolCodeDotsWrtCam);
    
    // Project into camera
    std::vector<Anki::Point2f> projectedToolCodeDots;
    _camera.Project3dPoints(toolCodeDotsWrtCam, projectedToolCodeDots);
    
    // Only proceed if all dots are visible with the current head/lift pose
    for(auto & point : projectedToolCodeDots) {
      if(!_camera.IsWithinFieldOfView(point)) {
        PRINT_NAMED_ERROR("VisionSystem.ReadToolCode.DotsNotInFOV", "");
        return RESULT_FAIL;
      }
    }
    
    readToolCodeMessage.expectedCalibDotLeft_x  = projectedToolCodeDots[LEFT_DOT].x();
    readToolCodeMessage.expectedCalibDotLeft_y  = projectedToolCodeDots[LEFT_DOT].y();
    readToolCodeMessage.expectedCalibDotRight_x = projectedToolCodeDots[RIGHT_DOT].x();
    readToolCodeMessage.expectedCalibDotRight_y = projectedToolCodeDots[RIGHT_DOT].y();
    
#   if DRAW_TOOL_CODE_DEBUG
    Vision::ImageRGB dispImg(image);
#   endif
    
    // Tool code calibration dot parameters
    const f32 kDotWidth_mm = 2.5f;
    const f32 kDotHole_mm  = 2.5f/3.f;
    const f32 kDotAreaFrac = ((kDotWidth_mm*kDotWidth_mm - kDotHole_mm*kDotHole_mm) /
                              (4.f*kCalibDotSearchSize_mm * kCalibDotSearchSize_mm));
    const f32 kMinDotAreaFrac   = 0.5f * kDotAreaFrac;
    const f32 kMaxDotAreaFrac   = 1.5f * kDotAreaFrac;
    const f32 kHoleAreaFrac     = kDotHole_mm * kDotHole_mm / (kDotWidth_mm*kDotWidth_mm);
    const f32 kMaxHoleAreaFrac  = 2.f * kHoleAreaFrac;
    
    Anki::Point2f camCen;
    std::vector<Anki::Point2f> observedPoints;
    for(size_t iDot=0; iDot<projectedToolCodeDots.size(); ++iDot)
    {
      // Get an ROI around where we expect to see the dot in the image
      const Point3f& dotWrtLift3d = toolCodeDotsWrtLift[iDot];
      Quad3f dotQuadRoi3d = {
        {dotWrtLift3d.x() - kCalibDotSearchSize_mm, dotWrtLift3d.y() - kCalibDotSearchSize_mm, dotWrtLift3d.z()},
        {dotWrtLift3d.x() - kCalibDotSearchSize_mm, dotWrtLift3d.y() + kCalibDotSearchSize_mm, dotWrtLift3d.z()},
        {dotWrtLift3d.x() + kCalibDotSearchSize_mm, dotWrtLift3d.y() - kCalibDotSearchSize_mm, dotWrtLift3d.z()},
        {dotWrtLift3d.x() + kCalibDotSearchSize_mm, dotWrtLift3d.y() + kCalibDotSearchSize_mm, dotWrtLift3d.z()},
      };
      
      Quad3f dotQuadRoi3dWrtCam;
      liftPoseWrtCam.ApplyTo(dotQuadRoi3d, dotQuadRoi3dWrtCam);
      
      if(DRAW_TOOL_CODE_DEBUG)
      {
        Quad3f dotQuadRoi3dWrtWorld;
        liftPose.GetWithRespectToOrigin().ApplyTo(dotQuadRoi3d, dotQuadRoi3dWrtWorld);
        dotQuadRoi3dWrtWorld += Point3f(0,0,0.5f);
        _vizManager->DrawQuad(VizQuadType::VIZ_QUAD_GENERIC_3D, 9324+(u32)iDot, dotQuadRoi3dWrtWorld, NamedColors::RED);
        
        Quad3f dotQuad3d = {
          {dotWrtLift3d.x() - kDotWidth_mm*0.5f, dotWrtLift3d.y() - kDotWidth_mm*0.5f, dotWrtLift3d.z()},
          {dotWrtLift3d.x() - kDotWidth_mm*0.5f, dotWrtLift3d.y() + kDotWidth_mm*0.5f, dotWrtLift3d.z()},
          {dotWrtLift3d.x() + kDotWidth_mm*0.5f, dotWrtLift3d.y() - kDotWidth_mm*0.5f, dotWrtLift3d.z()},
          {dotWrtLift3d.x() + kDotWidth_mm*0.5f, dotWrtLift3d.y() + kDotWidth_mm*0.5f, dotWrtLift3d.z()},
        };
        Quad3f dotQuadWrtWorld;
        liftPose.GetWithRespectToOrigin().ApplyTo(dotQuad3d, dotQuadWrtWorld);
        dotQuadWrtWorld += Point3f(0,0,0.5f);
        _vizManager->DrawQuad(VizQuadType::VIZ_QUAD_GENERIC_3D, 9337+(u32)iDot, dotQuadWrtWorld, NamedColors::GREEN);
      }
      
      Quad2f dotQuadRoi2d;
      _camera.Project3dPoints(dotQuadRoi3dWrtCam, dotQuadRoi2d);
      
      Anki::Rectangle<s32> dotRectRoi(dotQuadRoi2d);
      const Vision::Image dotRoi = image.GetROI(dotRectRoi);
      
      // Simple global threshold for binarization
      //Vision::Image invertedDotRoi = dotRoi.GetNegative();
      //double maxVal = 0, minVal = 0;
      //cv::minMaxIdx(invertedDotRoi.get_CvMat_(), &minVal, &maxVal);
      //invertedDotRoi.Threshold((maxVal + minVal)*0.5);
      
      // Perform local binarization:
      Vision::Image dotRoi_blurred;
      cv::GaussianBlur(dotRoi.get_CvMat_(), dotRoi_blurred.get_CvMat_(), cv::Size(15,15), 11);
      Vision::Image binarizedDotRoi(dotRoi.GetNumRows(), dotRoi.GetNumCols());
      binarizedDotRoi.get_CvMat_() = dotRoi.get_CvMat_() < dotRoi_blurred.get_CvMat_();
      
      if(false && DRAW_TOOL_CODE_DEBUG) {
        _debugImageMailbox.putMessage({(iDot==0 ? "dotRoi0" : "dotRoi1"), dotRoi});
        _debugImageMailbox.putMessage({(iDot==0 ? "dotRoi0_blurred" : "dotRoi1_blurred"), dotRoi_blurred});
        _debugImageMailbox.putMessage({(iDot==0 ? "InvertedDotROI0" : "InvertedDotRoi1"), binarizedDotRoi});
      }
      
      // Get connected components in the ROI
      Array2d<s32> labels;
      cv::Mat stats, centroids;
      const s32 numComponents = cv::connectedComponentsWithStats(binarizedDotRoi.get_CvMat_(), labels.get_CvMat_(), stats, centroids);
      
      s32 dotLabel = -1;
      
      // Filter out components based on a variety of checks:
      //  area, solidity, existence and size of a hole that is fully surrounded
      Anki::Point2f roiCen(binarizedDotRoi.GetNumCols(), binarizedDotRoi.GetNumRows());
      roiCen *= 0.5f;
      f32 distToCenterSq = FLT_MAX;
      for(s32 iComp=1; iComp < numComponents; ++iComp)
      {
        const s32* compStats = stats.ptr<s32>(iComp);
        const s32 compArea = compStats[cv::CC_STAT_AREA];
        const s32 bboxArea = compStats[cv::CC_STAT_HEIGHT]*compStats[cv::CC_STAT_WIDTH];
        const f32 solidity = (f32)compArea/(f32)bboxArea;
        if(compArea > kMinDotAreaFrac*binarizedDotRoi.GetNumElements() &&
           compArea < kMaxDotAreaFrac*binarizedDotRoi.GetNumElements() &&
           solidity > 0.5f*(1.f-kHoleAreaFrac))
        {
          const f64* dotCentroid = centroids.ptr<f64>(iComp);
          const f32 distSq = (Anki::Point2f(dotCentroid[0], dotCentroid[1]) - roiCen).LengthSq();
          if(distSq < distToCenterSq)
          {
            // Check to see if center point is "empty" (has background label)
            // Note the x/y vs. row/col switch here
            const s32 centerLabel = labels(std::round(dotCentroid[1]),
                                           std::round(dotCentroid[0]));
            if(centerLabel == 0)
            {
              // Verify if we flood fill from center that we get a hole of
              // reasonable size that doesn't "leak" outside of this component
              cv::floodFill(labels.get_CvMat_(), cv::Point(dotCentroid[0], dotCentroid[1]),
                            numComponents+1);
              
              Anki::Rectangle<s32> compRect(compStats[cv::CC_STAT_LEFT],  compStats[cv::CC_STAT_TOP],
                                            compStats[cv::CC_STAT_WIDTH], compStats[cv::CC_STAT_HEIGHT]);
              
              Vision::Image compBrightnessROI = dotRoi.GetROI(compRect);
              Array2d<s32> labelROI = labels.GetROI(compRect);
              
              // Loop over an even smaller ROI right around the component to
              // compute the hole size, the brightness of that hole vs.
              // the component itself, and whether the hole is completely
              // surrounded by the component or touches the edge of ROI.
              s32 avgDotBrightness = 0;
              s32 avgHoleBrightness = 0;
              s32 holeArea = 0;
              bool touchesEdge = false;
              for(s32 i=0; i<labelROI.GetNumRows() && !touchesEdge; ++i)
              {
                const u8* brightness_i = compBrightnessROI.GetRow(i);
                s32* label_i = labelROI.GetRow(i);
                
                for(s32 j=0; j<labelROI.GetNumCols() && !touchesEdge; ++j)
                {
                  if(label_i[j] == numComponents+1)
                  {
                    ++holeArea;
                    avgHoleBrightness += brightness_i[j];
                    
                    if(i==0 || i==labelROI.GetNumRows()-1 ||
                       j==0 || j==labelROI.GetNumCols()-1)
                    {
                      touchesEdge = true;
                    }
                    label_i[j] = 0; // un-fill
                  }
                  else if(label_i[j] == iComp)  {
                    avgDotBrightness += brightness_i[j];
                  }
                }
              }
              
              if(!touchesEdge)
              {
                avgHoleBrightness /= holeArea;
                avgDotBrightness  /= compArea;
                
                // Hole should neither leak to the outside, nor should it be too big,
                // and its brightness should be sufficiently brighter than the dot
                const bool holeSmallEnough = holeArea < compArea * kMaxHoleAreaFrac;
                const bool enoughContrast = (f32)avgHoleBrightness > kCalibDotMinContrastRatio * (f32)avgDotBrightness;
                if(holeSmallEnough && enoughContrast)
                {
                  // Yay, passed all checks! Thus "must" be a tool code.
                  dotLabel = iComp;
                  distToCenterSq = distSq;
                } else if(!enoughContrast) {
                  PRINT_NAMED_INFO("VisionSystem.ReadToolCode.BadContrast",
                                   "Dot %lu: Contrast for comp %d = %f",
                                   (unsigned long)iDot, iComp, (f32)avgHoleBrightness / (f32)avgDotBrightness);
                } else if(!holeSmallEnough) {
                  PRINT_NAMED_INFO("VisionSystem.ReadToolCode.HoleTooLarge",
                                   "Dot %lu: hole too large %d > %f*%d (=%f)",
                                   (unsigned long)iDot, holeArea, kMaxHoleAreaFrac, compArea,
                                   kMaxHoleAreaFrac*compArea);
                }
              }
            }
          } // dist to center check
        } // area check
      } // for each component iComp
      
      if(DRAW_TOOL_CODE_DEBUG) {
        Vision::ImageRGB roiImgDisp(binarizedDotRoi);
        // Function to color component with dotLabel green, and white for all others
        std::function<Vision::PixelRGB(const s32&)> fcn = [dotLabel](const s32& label)
        {
          if(label == dotLabel) {
            return Vision::PixelRGB(0,255,0);
          } else if(label == 0) {
            return Vision::PixelRGB(0,0,0);
          } else {
            return Vision::PixelRGB(255,255,255);
          }
        };
        labels.ApplyScalarFunction(fcn, roiImgDisp);
        if(dotLabel != -1) {
          const f64* dotCentroid = centroids.ptr<f64>(dotLabel);
          roiImgDisp.DrawPoint(Anki::Point2f(dotCentroid[0], dotCentroid[1]), NamedColors::RED, 1);
          
          const s32* compStats = stats.ptr<s32>(dotLabel);
          Anki::Rectangle<f32> compRect(compStats[cv::CC_STAT_LEFT],  compStats[cv::CC_STAT_TOP],
                                        compStats[cv::CC_STAT_WIDTH], compStats[cv::CC_STAT_HEIGHT]);
          roiImgDisp.DrawRect(compRect, NamedColors::RED, 1);
        }
        _debugImageRGBMailbox.putMessage({(iDot==0 ? "DotROI0withCentroid" : "DotROI1withCentroid"), roiImgDisp});
      } // if(DRAW_TOOL_CODE_DEBUG)
      
      if(dotLabel == -1) {
        // TODO: Return failure instead?
        PRINT_NAMED_WARNING("VisionSystem.ReadToolCode.DotsNotFound",
                            "Failed to find valid dot");
        return RESULT_OK;
      }
      
      ASSERT_NAMED(centroids.type() == CV_64F, "VisionSystem.ReadToolCode.CentroidTypeNotDouble");
      const f64* dotCentroid = centroids.ptr<f64>(dotLabel);
      observedPoints.push_back(Anki::Point2f(dotCentroid[0] + dotRectRoi.GetX(),
                                             dotCentroid[1] + dotRectRoi.GetY()));
      
#     if DRAW_TOOL_CODE_DEBUG
      dispImg.DrawPoint(observedPoints.back(), NamedColors::ORANGE, 1);
      dispImg.DrawPoint(projectedToolCodeDots[iDot], NamedColors::BLUE,   2);
      dispImg.DrawQuad(dotQuadRoi2d, NamedColors::CYAN, 1);
#     endif
    } // for each tool code dot iDot
    
    ASSERT_NAMED(observedPoints.size() == 2,
                 "VisionSystem.ReadToolCode.WrongNumDotsObserved");
    
    readToolCodeMessage.observedCalibDotLeft_x  = observedPoints[LEFT_DOT].x();
    readToolCodeMessage.observedCalibDotLeft_y  = observedPoints[LEFT_DOT].y();
    readToolCodeMessage.observedCalibDotRight_x = observedPoints[RIGHT_DOT].x();
    readToolCodeMessage.observedCalibDotRight_y = observedPoints[RIGHT_DOT].y();
    
    // TODO: Actually read the code and put corresponding result in the mailbox (once we have more than one)
    // NOTE: This gets put in the mailbox by the Cleanup object defined at the top of the function
    readToolCodeMessage.code = ToolCode::CubeLiftingTool;
    _lastToolCodeReadTime_ms = image.GetTimestamp();
    
    if(_calibrateFromToolCode)
    {
      // Solve for camera center and focal length as a system of equations
      //
      // Let:
      //   (x_i, y_i, z_i) = 3D location of tool code dot i
      //   (u_i, v_i)      = observed 2D projection tool code dot i
      //   (cx,cy)         = calibration center point
      //   f               = calibration focal length
      //
      // Then:
      //
      //   [z_i  0   x_i] [cx]   [z_i * u_i]
      //   [0   z_i  y_i] [cy] = [z_i * v_i]
      //                  [f ]
      
      SmallMatrix<4, 3, f32> A;
      Anki::Point<4, f32> b;
      Anki::Point<3, f32> calibParams;
      
      for(s32 iDot=0; iDot<2; ++iDot)
      {
        A(iDot*2,0)   = toolCodeDotsWrtCam[iDot].z();
        A(iDot*2,1)   = 0.f;
        A(iDot*2,2)   = toolCodeDotsWrtCam[iDot].x();
        b[iDot*2]     = toolCodeDotsWrtCam[iDot].z() * observedPoints[iDot].x();
        
        A(iDot*2+1,0) = 0.f;
        A(iDot*2+1,1) = toolCodeDotsWrtCam[iDot].z();
        A(iDot*2+1,2) = toolCodeDotsWrtCam[iDot].y();
        b[iDot*2+1]   = toolCodeDotsWrtCam[iDot].z() * observedPoints[iDot].y();
      }
      
      Result lsqResult = LeastSquares(A,b,calibParams);
      ASSERT_NAMED(lsqResult == RESULT_OK, "LeastSquares failed");
      
      camCen.x()  = calibParams[0];
      camCen.y()  = calibParams[1];
      const f32 f = calibParams[2];
      
#     if DRAW_TOOL_CODE_DEBUG
      {
        char dispStr[256];
        snprintf(dispStr, 255, "f=%.1f, cen=(%.1f,%.1f)",
                 f, camCen.x(), camCen.y());
        dispImg.DrawText(Anki::Point2f(0, 15), dispStr, NamedColors::RED, 0.6);
        _debugImageRGBMailbox.putMessage({"ToolCode", dispImg});
      }
#     endif
      
      if(std::isnan(camCen.x()) || std::isnan(camCen.y())) {
        PRINT_NAMED_ERROR("VisionSystem.ReadToolCode.CamCenNaN", "");
        return RESULT_FAIL;
      } else if(std::isnan(f) || f <= 0.f) {
        PRINT_NAMED_ERROR("VisionSystem.ReadToolCode.BadFocalLength", "");
        return RESULT_FAIL;
      } else {
        // Make sure we're not changing too drastically
        const f32 kMaxChangeFraction = 0.25f;
        const f32 fChangeFrac = f/_camera.GetCalibration()->GetFocalLength_x();
        const f32 xChangeFrac = camCen.x() / _camera.GetCalibration()->GetCenter_x();
        const f32 yChangeFrac = camCen.y() / _camera.GetCalibration()->GetCenter_y();
        if(!NEAR(fChangeFrac, 1.f, kMaxChangeFraction) ||
           !NEAR(xChangeFrac, 1.f, kMaxChangeFraction) ||
           !NEAR(yChangeFrac, 1.f, kMaxChangeFraction))
        {
          PRINT_NAMED_ERROR("VisionSystem.ReadToolCode.ChangeTooLarge",
                            "Calibration change too large from current: f=%f vs %f, "
                            "cen=(%f,%f) vs (%f,%f)",
                            f, _camera.GetCalibration()->GetFocalLength_x(),
                            xChangeFrac, yChangeFrac,
                            _camera.GetCalibration()->GetCenter_x(),
                            _camera.GetCalibration()->GetCenter_y());
          return RESULT_FAIL;
        }
        
        
        // Sanity check the new calibration:
        if(true) // TODO: Only in debug?
        {
          Vision::Camera tempCamera;
          Vision::CameraCalibration tempCalib(_camera.GetCalibration()->GetNrows(),
                                              _camera.GetCalibration()->GetNcols(),
                                              _camera.GetCalibration()->GetFocalLength_x(),
                                              _camera.GetCalibration()->GetFocalLength_y(),
                                              _camera.GetCalibration()->GetCenter_x(),
                                              _camera.GetCalibration()->GetCenter_y());
          tempCalib.SetFocalLength(f,f);
          tempCalib.SetCenter(camCen);
          tempCamera.SetCalibration(tempCalib);
          std::vector<Anki::Point2f> sanityCheckPoints;
          tempCamera.Project3dPoints(toolCodeDotsWrtCam, sanityCheckPoints);
          for(s32 i=0; i<2; ++i)
          {
            const f32 reprojErrorSq = (sanityCheckPoints[i] - observedPoints[i]).LengthSq();
            if(reprojErrorSq > 5*5)
            {
              if(DRAW_TOOL_CODE_DEBUG)
              {
                Vision::ImageRGB dispImg(image);
                dispImg.DrawPoint(sanityCheckPoints[0], NamedColors::RED, 1);
                dispImg.DrawPoint(sanityCheckPoints[1], NamedColors::RED, 1);
                dispImg.DrawPoint(observedPoints[0], NamedColors::GREEN, 1);
                dispImg.DrawPoint(observedPoints[1], NamedColors::GREEN, 1);
                _debugImageRGBMailbox.putMessage({"SanityCheck", dispImg});
              }
              PRINT_NAMED_ERROR("VisionSystem.ReadToolCode.BadProjection",
                                "Reprojection error of point %d = %f",
                                i, std::sqrtf(reprojErrorSq));
              return RESULT_FAIL;
            }
          }
        } // if sanity checking the new calibration
        
        // Update the camera calibration
        PRINT_NAMED_INFO("VisionSystem.ReadToolCode.CameraCalibUpdated",
                         "OldCen=(%f,%f), NewCen=(%f,%f), OldF=(%f,%f), NewF=(%f,%f), t=%dms",
                         _camera.GetCalibration()->GetCenter_x(),
                         _camera.GetCalibration()->GetCenter_y(),
                         camCen.x(), camCen.y(),
                         _camera.GetCalibration()->GetFocalLength_x(),
                         _camera.GetCalibration()->GetFocalLength_y(),
                         f, f,
                         image.GetTimestamp());
        
        _camera.GetCalibration()->SetCenter(camCen);
        _camera.GetCalibration()->SetFocalLength(f, f);
    
      } // if(new calib values pass sanity / nan checks)
    } // if tool code calibration is enabled
  
    return RESULT_OK;
  } // ReadToolCode()
  
  
  // Helper function for computing "corner" positions of the calibration board
  void CalcBoardCornerPositions(cv::Size boardSize, float squareSize, std::vector<cv::Point3f>& corners)
  {
    corners.clear();
    
    //    switch(patternType)
    //    {
    //      case Settings::CHESSBOARD:
    //      case Settings::CIRCLES_GRID:
    //        for( int i = 0; i < boardSize.height; ++i )
    //          for( int j = 0; j < boardSize.width; ++j )
    //            corners.push_back(Point3f(float( j*squareSize ), float( i*squareSize ), 0));
    //        break;
    //
    //      case Settings::ASYMMETRIC_CIRCLES_GRID:
    for( int i = 0; i < boardSize.height; i++ )
      for( int j = 0; j < boardSize.width; j++ )
        corners.push_back(cv::Point3f(float((2*j + i % 2)*squareSize), float(i*squareSize), 0));
    //        break;
    //    }
  }

  
  Result VisionSystem::ComputeCalibration()
  {
    Vision::CameraCalibration calibration;
    _isCalibrating = true;
    
    // Guarantee ComputingCalibration mode gets disabled and computed calibration gets sent
    // no matter how we return from this function
    Util::CleanupHelper disableComputingCalibration([this,&calibration]() {
      this->_calibrationMailbox.putMessage(calibration);
      this->EnableMode(VisionMode::ComputingCalibration, false);
      _isCalibrating = false;
    });
    
    // Check that there are enough images
    if (_calibImages.size() < _kMinNumCalibImagesRequired) {
      PRINT_NAMED_INFO("VisionSystem.ComputeCalibration.NotEnoughImages", "Got %u. Need %u.", (u32)_calibImages.size(), _kMinNumCalibImagesRequired);
      return RESULT_FAIL;
    }
    PRINT_NAMED_INFO("VisionSystem.ComputeCalibration.NumImages", "%u.", (u32)_calibImages.size());
    
    
    // Description of asymmetric circles calibration target
    cv::Size boardSize(4,11);
    static constexpr f32 squareSize = 0.01; // TODO: Doesn't really matter for camera matrix intrinsics computation, but should probably measure this.
    cv::Size imageSize(_calibImages.front().GetNumCols(), _calibImages.front().GetNumRows());
    
    
    std::vector<std::vector<cv::Point2f> > imagePoints;
    std::vector<std::vector<cv::Point3f> > objectPoints(1);
    
    // Parameters for circle grid search
    cv::SimpleBlobDetector::Params params;
    params.maxArea = kMaxCalibBlobPixelArea;
    params.minArea = kMinCalibBlobPixelArea;
    params.minDistBetweenBlobs = kMinCalibPixelDistBetweenBlobs;
    cv::Ptr<cv::SimpleBlobDetector> blobDetector = cv::SimpleBlobDetector::create(params);
    int findCirclesFlags = cv::CALIB_CB_ASYMMETRIC_GRID | cv::CALIB_CB_CLUSTERING;
    
    int imgCnt = 0;
    for (auto img : _calibImages) {
      
      // Get image points
      std::vector<cv::Point2f> pointBuf;
      bool found = cv::findCirclesGrid(img.get_CvMat_(), boardSize, pointBuf, findCirclesFlags, blobDetector);

      if (found) {
        PRINT_NAMED_INFO("VisionSystem.ComputeCalibration.FoundPoints", "");
        imagePoints.push_back(pointBuf);
      } else {
        PRINT_NAMED_INFO("VisionSystem.ComputeCalibration.NoPointsFound", "");
      }
      
      
      // Draw image
      if (DRAW_CALIB_IMAGES) {
        Vision::ImageRGB dispImg;
        cv::cvtColor(img.get_CvMat_(), dispImg.get_CvMat_(), cv::COLOR_GRAY2BGR);
        if (found) {
          cv::drawChessboardCorners(dispImg.get_CvMat_(), boardSize, cv::Mat(pointBuf), found);
        }
        _debugImageRGBMailbox.putMessage({std::string("CalibImage") + std::to_string(imgCnt), dispImg});
      }
      ++imgCnt;
    }
    
    // Were points found in enough of the images?
    if (imagePoints.size() < _kMinNumCalibImagesRequired) {
      PRINT_NAMED_INFO("VisionSystem.ComputeCalibration.InsufficientImagesWithPoints",
                       "Points detected in only %u images. Need %u.",
                       (u32)imagePoints.size(), _kMinNumCalibImagesRequired);
      return RESULT_FAIL;
    }
    
    
    // Get object points
    CalcBoardCornerPositions(boardSize, squareSize, objectPoints[0]);
    objectPoints.resize(imagePoints.size(), objectPoints[0]);
    

    // Compute calibration
    std::vector<cv::Mat> rvecs, tvecs;
    cv::Mat cameraMatrix = cv::Mat::eye(3, 3, CV_64F);
    cv::Mat distCoeffs = cv::Mat::zeros(8, 1, CV_64F);
    
    double rms = cv::calibrateCamera(objectPoints, imagePoints, imageSize, cameraMatrix, distCoeffs, rvecs, tvecs);
    
    
    calibration = Vision::CameraCalibration(imageSize.height, imageSize.width,
                                            cameraMatrix.at<double>(0,0), cameraMatrix.at<double>(1,1),
                                            cameraMatrix.at<double>(0,2), cameraMatrix.at<double>(1,2));

    PRINT_NAMED_INFO("VisionSystem.ComputeCalibration.CalibValues",
                     "fx: %f, fy: %f, cx: %f, cy: %f (rms %f)",
                     calibration.GetFocalLength_x(), calibration.GetFocalLength_y(),
                     calibration.GetCenter_x(), calibration.GetCenter_y(), rms);
    
                          
    // Check if average reprojection error is too high
    if (rms > 1.f) {
      PRINT_NAMED_INFO("VisionSystem.ComputeCalibration.ReprojectionErrorTooHigh", "%f", rms);
      return RESULT_FAIL;
    }
    
    return RESULT_OK;
  }
  
  
} // namespace Cozmo
} // namespace Anki

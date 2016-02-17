/**
 * File: visionSystem.h [Basestation]
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

#ifndef ANKI_COZMO_BASESTATION_VISIONSYSTEM_H
#define ANKI_COZMO_BASESTATION_VISIONSYSTEM_H

#if ANKICORETECH_USE_MATLAB
   // You can manually adjust this one
#  define ANKI_COZMO_USE_MATLAB_VISION 0
#else
   // Leave this one always set to 0
#  define ANKI_COZMO_USE_MATLAB_VISION 0
#endif


#include "anki/common/types.h"

#include "anki/common/basestation/mailbox.h"

// Robot includes should eventually go away once Basestation vision is natively
// implemented
#include "anki/common/robot/fixedLengthList.h"
#include "anki/common/robot/geometry_declarations.h"

#include "anki/cozmo/basestation/robotPoseHistory.h"
#include "anki/cozmo/basestation/groundPlaneROI.h"

#include "anki/common/basestation/matlabInterface.h"

#include "anki/vision/robot/fiducialMarkers.h"

#include "anki/cozmo/shared/cozmoConfig.h"

#include "anki/vision/basestation/camera.h"
#include "anki/vision/basestation/cameraCalibration.h"
#include "anki/vision/basestation/image.h"
#include "anki/vision/basestation/faceTracker.h"
#include "anki/vision/basestation/visionMarker.h"

#include "visionParameters.h"
#include "clad/vizInterface/messageViz.h"
#include "clad/robotInterface/messageEngineToRobot.h"
#include "clad/types/imageTypes.h"
#include "clad/types/visionModes.h"
#include "clad/externalInterface/messageEngineToGame.h"


namespace Anki {
namespace Embedded {
  typedef Point<float> Point2f;
}
namespace Cozmo {
    
  // Forward declaration:
  class Robot;
  class VizManager;

  class VisionSystem
  {
  public:

    VisionSystem(const std::string& dataPath, VizManager* vizMan);
    ~VisionSystem();
    
    //
    // Methods:
    //
    
    Result Init(const Vision::CameraCalibration& camCalib);
    void UnInit() { _isInitialized = false; };
    
    bool IsInitialized() const;
    
    Result EnableMode(VisionMode whichMode, bool enabled);
    bool   IsModeEnabled(VisionMode whichMode) const { return _mode & static_cast<u32>(whichMode); }
    u32    GetEnabledModes() const { return _mode; }
    void   SetModes(u32 modes) { _mode = modes; }
    
    // Accessors
    f32 GetTrackingMarkerWidth();
    
    struct PoseData {
      TimeStamp_t      timeStamp;
      RobotPoseStamp   poseStamp;  // contains historical head/lift/pose info
      Pose3d           cameraPose; // w.r.t. pose in poseStamp
      bool             groundPlaneVisible;
      Matrix_3x3f      groundPlaneHomography;
      GroundPlaneROI   groundPlaneROI;
      bool             isMoving;
    };
    
    // This is main Update() call to be called in a loop from above.

    Result Update(const PoseData&            robotState,
                  const Vision::ImageRGB&    inputImg);
    
    void StopTracking();

    // Select a block type to look for to dock with.
    // Use MARKER_UNKNOWN to disable.
    // Next time the vision system sees a block of this type while looking
    // for blocks, it will initialize a template tracker and switch to
    // docking mode.
    // If checkAngleX is true, then tracking will be considered as a failure if
    // the X angle is greater than TrackerParameters::MAX_BLOCK_DOCKING_ANGLE.
    Result SetMarkerToTrack(const Vision::MarkerType&  markerToTrack,
                            const f32                  markerWidth_mm,
                            const bool                 checkAngleX);
    
    // Same as above, except the robot will only start tracking the marker
    // if its observed centroid is within the specified radius (in pixels)
    // from the given image point.
    Result SetMarkerToTrack(const Vision::MarkerType&  markerToTrack,
                            const f32                  markerWidth_mm,
                            const Embedded::Point2f&   imageCenter,
                            const f32                  radius,
                            const bool                 checkAngleX,
                            const f32                  postOffsetX_mm = 0,
                            const f32                  postOffsetY_mm = 0,
                            const f32                  posttOffsetAngle_rad = 0);
    
    u32 DownsampleHelper(const Embedded::Array<u8>& imageIn,
                         Embedded::Array<u8>&       imageOut,
                         Embedded::MemoryStack      scratch);
    
    
    // Returns a const reference to the list of the most recently observed
    // vision markers.
    const Embedded::FixedLengthList<Embedded::VisionMarker>& GetObservedMarkerList();
    
    // Return a const pointer to the largest (in terms of image size)
    // VisionMarker of the specified type.  Pointer is NULL if there is none.
    const Embedded::VisionMarker* GetLargestVisionMarker(const Vision::MarkerType withType);
    
    // Compute the 3D pose of a VisionMarker w.r.t. the camera.
    // - If the pose w.r.t. the robot is required, follow this with a call to
    //    the GetWithRespectToRobot() method below.
    // - If ignoreOrientation=true, the orientation of the marker within the
    //    image plane will be ignored (by sorting the marker's corners such
    //    that they always represent an upright marker).
    // NOTE: rotation should already be allocated as a 3x3 array.
    Result GetVisionMarkerPose(const Embedded::VisionMarker& marker,
                               const bool                    ignoreOrientation,
                               Embedded::Array<f32>&         rotationWrtCamera,
                               Embedded::Point3<f32>&        translationWrtCamera);
    
    // Find the VisionMarker with the specified type whose 3D pose is closest
    // to the given 3D position (with respect to *robot*) and also within the
    // specified maxDistance (in mm).  If such a marker is found, the pose is
    // returned and the "markerFound" flag will be true.
    // NOTE: rotation should already be allocated as a 3x3 array.
    Result GetVisionMarkerPoseNearestTo(const Embedded::Point3<f32>&  atPosition,
                                        const Vision::MarkerType&     withType,
                                        const f32                     maxDistance_mm,
                                        Embedded::Array<f32>&         rotationWrtRobot,
                                        Embedded::Point3<f32>&        translationWrtRobot,
                                        bool&                         markerFound);
    
    // Convert a point or pose in camera coordinates to robot coordinates,
    // using the kinematic chain of the neck and head geometry.
    // NOTE: the rotation matrices should already be allocated as 3x3 arrays.
    Result GetWithRespectToRobot(const Embedded::Point3<f32>& pointWrtCamera,
                                 Embedded::Point3<f32>&       pointWrtRobot);
    
    Result GetWithRespectToRobot(const Embedded::Array<f32>&  rotationWrtCamera,
                                 const Embedded::Point3<f32>& translationWrtCamera,
                                 Embedded::Array<f32>&        rotationWrtRobot,
                                 Embedded::Point3<f32>&       translationWrtRobot);
    
    // Returns field of view (radians) of camera
    f32 GetVerticalFOV();
    f32 GetHorizontalFOV();
    
    const FaceDetectionParameters& GetFaceDetectionParams();
    
    std::string GetModeName(VisionMode mode) const;
    std::string GetCurrentModeName() const;
    
    void EnableNewFaceEnrollment(s32 numToEnroll);
    
    void SetParams(const bool autoExposureOn,
                   const f32 exposureTime,
                   const s32 integerCountsIncrement,
                   const f32 minExposureTime,
                   const f32 maxExposureTime,
                   const u8 highValue,
                   const f32 percentileToMakeHigh);

    void SetFaceDetectParams(const f32 scaleFactor,
                             const s32 minNeighbors,
                             const s32 minObjectHeight,
                             const s32 minObjectWidth,
                             const s32 maxObjectHeight,
                             const s32 maxObjectWidth);
  
    // These return true if a mailbox messages was available, and they copy
    // that message into the passed-in message struct.
    //bool CheckMailbox(ImageChunk&          msg);
    //bool CheckMailbox(MessageFaceDetection&       msg);
    bool CheckMailbox(std::pair<Pose3d, TimeStamp_t>& markerPoseWrtCamera);
    bool CheckMailbox(Vision::ObservedMarker&     msg);
    bool CheckMailbox(VizInterface::TrackerQuad&  msg);
    //bool CheckMailbox(RobotInterface::PanAndTilt& msg);
    bool CheckMailbox(ExternalInterface::RobotObservedMotion& msg);
    bool CheckMailbox(Vision::TrackedFace&        msg);
    bool CheckMailbox(Vision::FaceTracker::UpdatedID&  msg);
    
    bool CheckDebugMailbox(std::pair<const char*, Vision::Image>& msg);
    bool CheckDebugMailbox(std::pair<const char*, Vision::ImageRGB>& msg);
    
  protected:
    
#   if ANKI_COZMO_USE_MATLAB_VISION
    // For prototyping with Matlab
    Matlab _matlab;
#   endif
    
    // Previous image for doing background subtraction, e.g. for saliency
    // NOTE: previous images stored at resolution of motion detection processing.
    Vision::ImageRGB _prevImage;
    Vision::ImageRGB _prevPrevImage;
    TimeStamp_t      _lastMotionTime = 0;
    //Vision::Image    _prevRatioImg;
    Anki::Point2f    _prevMotionCentroid;
    Anki::Point2f    _prevGroundMotionCentroid;
    f32              _prevCentroidFilterWeight = 0.f;
    f32              _prevGroundCentroidFilterWeight = 0.f;
    //
    // Formerly in Embedded VisionSystem "private" namespace:
    //
    
    bool _isInitialized;
    const std::string _dataPath;
    
    Vision::Camera _camera;
    
    enum VignettingCorrection
    {
      VignettingCorrection_Off,
      VignettingCorrection_CameraHardware,
      VignettingCorrection_Software
    };
    
    // The tracker can fail to converge this many times before we give up
    // and reset the docker
    // TODO: Move this to visionParameters
    const s32 MAX_TRACKING_FAILURES = 1;
    
    //const Anki::Cozmo::HAL::CameraInfo* _headCamInfo;
    f32 _headCamFOV_ver;
    f32 _headCamFOV_hor;
    Embedded::Array<f32> _RcamWrtRobot;
    
    u32 _mode = static_cast<u32>(VisionMode::Idle);
    u32 _modeBeforeTracking = static_cast<u32>(VisionMode::Idle);
    
    // Camera parameters
    // TODO: Should these be moved to (their own struct in) visionParameters.h/cpp?
    f32 _exposureTime;
    
    VignettingCorrection _vignettingCorrection = VignettingCorrection_Off;

    const f32 _vignettingCorrectionParameters[5] = {0,0,0,0,0};
    
    s32 _frameNumber;
    bool _autoExposure_enabled = true;
    s32 _trackingIteration; // Simply for display at this point
    
    // TEMP: Un-const-ing these so that we can adjust them from basestation for dev purposes.
    /*
     const s32 autoExposure_integerCountsIncrement = 3;
     const f32 autoExposure_minExposureTime = 0.02f;
     const f32 autoExposure_maxExposureTime = 0.98f;
     const u8 autoExposure_highValue = 250;
     const f32 autoExposure_percentileToMakeHigh = 0.97f;
     const s32 autoExposure_adjustEveryNFrames = 1;
     */
    s32 _autoExposure_integerCountsIncrement = 3;
    f32 _autoExposure_minExposureTime = 0.02f;
    f32 _autoExposure_maxExposureTime = 0.50f;
    u8  _autoExposure_highValue = 250;
    f32 _autoExposure_percentileToMakeHigh = 0.95f;
    f32 _autoExposure_tooHighPercentMultiplier = 0.7f;
    s32 _autoExposure_adjustEveryNFrames = 2;
    
    // Tracking marker related members
    struct MarkerToTrack {
      Anki::Vision::MarkerType  type;
      f32                       width_mm;
      Embedded::Point2f         imageCenter;
      f32                       imageSearchRadius;
      bool                      checkAngleX;
      f32                       postOffsetX_mm;
      f32                       postOffsetY_mm;
      f32                       postOffsetAngle_rad;
      
      MarkerToTrack();
      bool IsSpecified() const {
        return type != Anki::Vision::MARKER_UNKNOWN;
      }
      void Clear();
      bool Matches(const Embedded::VisionMarker& marker) const;
    };
    
    MarkerToTrack _markerToTrack;
    MarkerToTrack _newMarkerToTrack;
    bool          _newMarkerToTrackWasProvided = false;
    
    Embedded::Quadrilateral<f32>    _trackingQuad;
    s32                             _numTrackFailures ;
    Tracker                         _tracker;
    bool                            _trackerJustInitialized = false;
    bool                            _isTrackingMarkerFound = false;
    
    Embedded::Point3<P3P_PRECISION> _canonicalMarker3d[4];
    
    // Snapshots of robot state
    bool _wasCalledOnce = false;
    bool _havePrevPoseData = false;
    PoseData _poseData, _prevPoseData;
    
    // Parameters defined in visionParameters.h
    DetectFiducialMarkersParameters _detectionParameters;
    TrackerParameters               _trackerParameters;
    FaceDetectionParameters         _faceDetectionParameters;
    ImageResolution                 _captureResolution;
    
    // For sending images to basestation
    ImageSendMode                 _imageSendMode = ImageSendMode::Off;
    ImageResolution               _nextSendImageResolution = ImageResolution::ImageResolutionNone;
    
    // FaceTracking
    Vision::FaceTracker*          _faceTracker;
    
    // We hold a reference to the VizManager since we often want to draw to it
    VizManager*                   _vizManager = nullptr;

    struct VisionMemory {
      /* 10X the memory for debugging on a PC
       static const s32 OFFCHIP_BUFFER_SIZE = 20000000;
       static const s32 ONCHIP_BUFFER_SIZE = 1700000; // The max here is somewhere between 175000 and 180000 bytes
       static const s32 CCM_BUFFER_SIZE = 500000; // The max here is probably 65536 (0x10000) bytes
       */
      static const s32 OFFCHIP_BUFFER_SIZE = 4000000;
      static const s32 ONCHIP_BUFFER_SIZE  = 600000;
      static const s32 CCM_BUFFER_SIZE     = 200000; 

      static const s32 MAX_MARKERS = 100; // TODO: this should probably be in visionParameters
      
      OFFCHIP char offchipBuffer[OFFCHIP_BUFFER_SIZE];
      ONCHIP  char onchipBuffer[ONCHIP_BUFFER_SIZE];
      CCM     char ccmBuffer[CCM_BUFFER_SIZE];
      
      Embedded::MemoryStack _offchipScratch;
      Embedded::MemoryStack _onchipScratch;
      Embedded::MemoryStack _ccmScratch;
      
      // Markers is the one things that can move between functions, so it is always allocated in memory
      Embedded::FixedLengthList<Embedded::VisionMarker> _markers;
      
      // WARNING: ResetBuffers should be used with caution
      Result ResetBuffers();
      
      Result Initialize();
    }; // VisionMemory
    
    VisionMemory _memory;
    
    Embedded::Quadrilateral<f32> GetTrackerQuad(Embedded::MemoryStack scratch);
    Result UpdatePoseData(const PoseData& newPoseData);
    void GetPoseChange(f32& xChange, f32& yChange, Radians& angleChange);
    Result UpdateMarkerToTrack();
    Radians GetCurrentHeadAngle();
    Radians GetPreviousHeadAngle();
    
    static Result GetImageHelper(const Vision::Image& srcImage,
                                 Embedded::Array<u8>& destArray);

    //void DownsampleAndSendImage(const Embedded::Array<u8> &img);
    Result PreprocessImage(Embedded::Array<u8>& grayscaleImage);
    
    static Result BrightnessNormalizeImage(Embedded::Array<u8>& image,
                                           const Embedded::Quadrilateral<f32>& quad);

    static Result BrightnessNormalizeImage(Embedded::Array<u8>& image,
                                           const Embedded::Quadrilateral<f32>& quad,
                                           const f32 filterWidthFraction,
                                           Embedded::MemoryStack scratch);
    
    
    Result DetectMarkers(const Vision::Image& inputImage,
                          std::vector<Quad2f>& markerQuads);

    Result InitTemplate(Embedded::Array<u8> &grayscaleImage,
                        const Embedded::Quadrilateral<f32> &trackingQuad);
    
    Result TrackTemplate(const Vision::Image& inputImage);
    
    Result TrackerPredictionUpdate(const Embedded::Array<u8>& grayscaleImage,
                                   Embedded::MemoryStack scratch);
    
    Result DetectFaces(const Vision::Image& grayImage,
                       const std::vector<Quad2f>& markerQuads);
    
    Result DetectMotion(const Vision::ImageRGB& image);
    
    void FillDockErrMsg(const Embedded::Quadrilateral<f32>& currentQuad,
                        DockingErrorSignal& dockErrMsg,
                        Embedded::MemoryStack scratch);
    
    // Mailboxes for different types of messages that the vision
    // system communicates back to the vision processing thread
    Mailbox<VizInterface::TrackerQuad>        _trackerMailbox;
    Mailbox<ExternalInterface::RobotObservedMotion>  _motionMailbox;
    Mailbox<std::pair<Pose3d, TimeStamp_t> >  _dockingMailbox; // holds timestamped marker pose w.r.t. camera
    MultiMailbox<Vision::ObservedMarker, DetectFiducialMarkersParameters::MAX_MARKERS>   _visionMarkerMailbox;
    //MultiMailbox<MessageFaceDetection, FaceDetectionParameters::MAX_FACE_DETECTIONS>   _faceDetectMailbox;
    
    MultiMailbox<Vision::TrackedFace, FaceDetectionParameters::MAX_FACE_DETECTIONS> _faceMailbox;
    MultiMailbox<Vision::FaceTracker::UpdatedID, FaceDetectionParameters::MAX_FACE_DETECTIONS> _updatedFaceIdMailbox;
    
    MultiMailbox<std::pair<const char*, Vision::Image>, 10>     _debugImageMailbox;
    MultiMailbox<std::pair<const char*, Vision::ImageRGB>, 10>  _debugImageRGBMailbox;
    
    void RestoreNonTrackingMode();
    
  }; // class VisionSystem
  
      
} // namespace Cozmo
} // namespace Anki

#endif // ANKI_COZMO_BASESTATION_VISIONSYSTEM_H

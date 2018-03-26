/**
 * File: visionSystem.h [Basestation]
 *
 * Author: Andrew Stein
 * Date:   (various)
 *
 * Description: High-level module that controls the basestation vision system
 *              Runs on its own thread inside VisionComponent.
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

#include "coretech/common/engine/math/polygon.h"
#include "coretech/common/shared/types.h"

#include "anki/cozmo/shared/cozmoConfig.h"

#include "engine/debugImageList.h"
#include "engine/groundPlaneROI.h"
#include "engine/overheadEdge.h"
#include "engine/robotStateHistory.h"
#include "engine/rollingShutterCorrector.h"
#include "engine/vision/visionModeSchedule.h"
#include "engine/vision/visionPoseData.h"
#include "engine/vision/cameraCalibrator.h"

#include "coretech/common/engine/matlabInterface.h"

#include "coretech/vision/engine/camera.h"
#include "coretech/vision/engine/cameraCalibration.h"
#include "coretech/vision/engine/image.h"
#include "coretech/vision/engine/profiler.h"
#include "coretech/vision/engine/trackedFace.h"
#include "coretech/vision/engine/trackedPet.h"
#include "coretech/vision/engine/visionMarker.h"

#include "clad/vizInterface/messageViz.h"
#include "clad/robotInterface/messageEngineToRobot.h"
#include "clad/types/cameraParams.h"
#include "clad/types/faceEnrollmentPoses.h"
#include "clad/types/imageTypes.h"
#include "clad/types/loadedKnownFace.h"
#include "clad/types/visionModes.h"
#include "clad/types/toolCodes.h"
#include "clad/externalInterface/messageEngineToGame.h"

#include "util/bitFlags/bitFlags.h"

#include <mutex>
#include <queue>

namespace Anki {
 
namespace Vision {
  class Benchmark;
  class FaceTracker;
  class ImageCache;
  class ImagingPipeline;
  class MarkerDetector;
  class ObjectDetector;
  class PetTracker;
}
  
namespace Cozmo {
    
  // Forward declaration:
  class CameraCalibrator;
  class CozmoContext;
  class LaserPointDetector;
  class MotionDetector;
  class OverheadEdgesDetector;
  class OverheadMap;
  class Robot;
  class VizManager;
  class GroundPlaneClassifier;
  
  // Everything that can be generated from one image in one big package:
  struct VisionProcessingResult
  {
    TimeStamp_t timestamp; // Always set, even if all the lists below are empty (e.g. nothing is found)
    Util::BitFlags32<VisionMode> modesProcessed;
    
    ImageQuality imageQuality;
    CameraParams cameraParams;
    u8 imageMean;
    
    std::list<ExternalInterface::RobotObservedMotion>           observedMotions;
    std::list<Vision::ObservedMarker>                           observedMarkers;
    std::list<Vision::TrackedFace>                              faces;
    std::list<Vision::TrackedPet>                               pets;
    std::list<OverheadEdgeFrame>                                overheadEdges;
    std::list<Vision::UpdatedFaceID>                            updatedFaceIDs;
    std::list<ToolCodeInfo>                                     toolCodes;
    std::list<ExternalInterface::RobotObservedLaserPoint>       laserPoints;
    std::list<Vision::CameraCalibration>                        cameraCalibration;
    std::list<ExternalInterface::RobotObservedGenericObject>    generalObjects;
    std::list<OverheadEdgeFrame>                                visualObstacles;
    
    // Used to pass debug images back to main thread for display:
    DebugImageList<Vision::Image>    debugImages;
    DebugImageList<Vision::ImageRGB> debugImageRGBs;
  };
  

  class VisionSystem : public Vision::Profiler
  {
  public:

    VisionSystem(const CozmoContext* context);
    ~VisionSystem();
    
    //
    // Methods:
    //
    
    Result Init(const Json::Value& config);
    bool   IsInitialized() const;
    
    Result UpdateCameraCalibration(std::shared_ptr<Vision::CameraCalibration> camCalib);
    
    Result SetNextMode(VisionMode mode, bool enable);
    bool   IsModeEnabled(VisionMode whichMode) const { return _mode.IsBitFlagSet(whichMode); }
    
    Result PushNextModeSchedule(AllVisionModesSchedule&& schedule);
    Result PopModeSchedule();
    
    Result EnableToolCodeCalibration(bool enable);
    
    // This is main Update() call to be called in a loop from above.

    Result Update(const VisionPoseData&      robotState,
                  Vision::ImageCache&        imageCache);
    
    // First decodes the image then calls Update() above
    Result Update(const VisionPoseData&   robotState,
                  const Vision::ImageRGB& image);
    
    // Wrappers for camera calibration
    Result AddCalibrationImage(const Vision::Image& calibImg, const Anki::Rectangle<s32>& targetROI) { return _cameraCalibrator->AddCalibrationImage(calibImg, targetROI); }
    Result ClearCalibrationImages() { return _cameraCalibrator->ClearCalibrationImages(); }
    size_t GetNumStoredCalibrationImages() const { return _cameraCalibrator->GetNumStoredCalibrationImages(); }
    const std::vector<CameraCalibrator::CalibImage>& GetCalibrationImages() const {return _cameraCalibrator->GetCalibrationImages();}
    const std::vector<Pose3d>& GetCalibrationPoses() const { return _cameraCalibrator->GetCalibrationPoses();}

    Result ClearToolCodeImages();
    size_t GetNumStoredToolCodeImages() const {return _toolCodeImages.size();}
    const std::vector<Vision::Image>& GetToolCodeImages() const {return _toolCodeImages;}

    // VisionMode <-> String Lookups
    std::string GetModeName(Util::BitFlags32<VisionMode> mode) const;
    std::string GetCurrentModeName() const;
    VisionMode  GetModeFromString(const std::string& str) const;
    
    Result AssignNameToFace(Vision::FaceID_t faceID, const std::string& name, Vision::FaceID_t mergeWithID);
    
    // Enable face enrollment mode and optionally specify the ID for which 
    // enrollment is allowed (use UnknownFaceID to indicate "any" ID).
    // Enrollment will automatically disable after numEnrollments. (Use 
    // a value < 0 to enable ongoing enrollments.)
    void SetFaceEnrollmentMode(Vision::FaceEnrollmentPose pose,
                               Vision::FaceID_t forFaceID = Vision::UnknownFaceID,
                               s32 numEnrollments = -1);
    
    void SetFaceRecognitionIsSynchronous(bool isSynchronous);
    
    Result LoadFaceAlbum(const std::string& albumName, std::list<Vision::LoadedKnownFace>& loadedFaces);
    
    Result SaveFaceAlbum(const std::string& albumName);
    
    Result GetSerializedFaceData(std::vector<u8>& albumData,
                                 std::vector<u8>& enrollData) const;
    
    Result SetSerializedFaceData(const std::vector<u8>& albumData,
                                 const std::vector<u8>& enrollData,
                                 std::list<Vision::LoadedKnownFace>& loadedFaces);

    Result EraseFace(Vision::FaceID_t faceID);
    void   EraseAllFaces();
    
    std::vector<Vision::LoadedKnownFace> GetEnrolledNames() const;
    
    Result RenameFace(Vision::FaceID_t faceID, const std::string& oldName, const std::string& newName,
                      Vision::RobotRenamedEnrolledFace& renamedFace);
    
    // Parameters for camera hardware exposure values
    static constexpr size_t GAMMA_CURVE_SIZE = 17;
    using GammaCurve = std::array<u8, GAMMA_CURVE_SIZE>;
    Result SetCameraExposureParams(const s32 currentExposureTime_ms,
                                   const s32 minExposureTime_ms,
                                   const s32 maxExposureTime_ms,
                                   const f32 currentGain,
                                   const f32 minGain,
                                   const f32 maxGain,
                                   const GammaCurve& gammaCurve);

    // Parameters for how we compute new exposure from image data
    Result SetAutoExposureParams(const s32 subSample,
                                 const u8  midValue,
                                 const f32 midPercentile,
                                 const f32 maxChangeFraction);
    
    // Just specify what the current values are (don't actually change the robot's camera)
    Result SetNextCameraExposure(s32 exposure_ms, f32 gain);
    Result SetNextCameraWhiteBalance(f32 whiteBalanceGainR, 
                                     f32 whiteBalanceGainG, 
                                     f32 whiteBalanceGainB);
    
    // When SavingImages mode is enabled:
    //  saveMode: SingleShot=save one image and wait for this call again
    //            Stream=save according to the mode schedule
    //            Off=no saving until this is called again with one of the above
    //  path: Where to save images (relative to <Cache>/camera/images)
    //  quality: -1=PNG, 0-100=JPEG quality
    void SetSaveParameters(const ImageSendMode saveMode, const std::string& path, const int8_t quality);

    CameraParams GetCurrentCameraParams() const;
  
    bool CheckMailbox(VisionProcessingResult& result);
    
    const RollingShutterCorrector& GetRollingShutterCorrector() { return _rollingShutterCorrector; }
    void  ShouldDoRollingShutterCorrection(bool b) { _doRollingShutterCorrection = b; }
    bool  IsDoingRollingShutterCorrection() const { return _doRollingShutterCorrection; }
    
    Result CheckImageQuality(const Vision::Image& inputImage,
                             const std::vector<Anki::Rectangle<s32>>& detectionRects);
    
    // Will use color if not empty, or gray otherwise
    Result DetectLaserPoints(Vision::ImageCache& imageCache);
    
    bool IsExposureValid(s32 exposure) const;
    
    bool IsGainValid(f32 gain) const;
    
    s32 GetMinCameraExposureTime_ms() const { return _minCameraExposureTime_ms; }
    s32 GetMaxCameraExposureTime_ms() const { return _maxCameraExposureTime_ms; }
    
    f32 GetMinCameraGain() const { return _minCameraGain; }
    f32 GetMaxCameraGain() const { return _maxCameraGain; }
    
  protected:
  
    RollingShutterCorrector _rollingShutterCorrector;

    bool _doRollingShutterCorrection = false;
    
#   if ANKI_COZMO_USE_MATLAB_VISION
    // For prototyping with Matlab
    Matlab _matlab;
#   endif
    
    std::unique_ptr<Vision::ImageCache> _imageCache;
    
    bool _isInitialized = false;
    const CozmoContext* _context = nullptr;
    
    Vision::Camera _camera;
    
    // Camera parameters
    std::unique_ptr<Vision::ImagingPipeline> _imagingPipeline;
    s32 _maxCameraExposureTime_ms = 66;
    s32 _minCameraExposureTime_ms = 1;
    
    // These baseline defaults are overridden by whatever we receive from the camera
    f32 _minCameraGain     = 0.1f; 
    f32 _maxCameraGain     = 3.8f;
    
    CameraParams _currentCameraParams{31, 1.0, 2.0, 1.0, 2.0};
    std::pair<bool,CameraParams> _nextCameraParams{false, _currentCameraParams}; // bool represents if set but not yet sent
    
    Util::BitFlags32<VisionMode> _mode;
    std::queue<std::pair<VisionMode, bool>> _nextModes;
    
    using ModeScheduleStack = std::list<AllVisionModesSchedule>;
    ModeScheduleStack _modeScheduleStack;
    std::queue<std::pair<bool,AllVisionModesSchedule>> _nextSchedules;
    
    bool _calibrateFromToolCode = false;
    
    s32 _frameNumber = 0;

    ImageSendMode  _imageSaveMode = ImageSendMode::Off;
    s8             _imageSaveQuality = -1;
    std::string    _imageSavePath;
    
    // Snapshots of robot state
    bool _wasCalledOnce    = false;
    bool _havePrevPoseData = false;
    const Pose3d _poseOrigin;
    VisionPoseData _poseData, _prevPoseData;
  
    // For sending images to basestation
    ImageSendMode                 _imageSendMode = ImageSendMode::Off;
    
    // We hold a pointer to the VizManager since we often want to draw to it
    VizManager*                   _vizManager = nullptr;

    // Sub-components for detection/tracking/etc:
    std::unique_ptr<Vision::FaceTracker>    _faceTracker;
    std::unique_ptr<Vision::PetTracker>     _petTracker;
    std::unique_ptr<Vision::MarkerDetector> _markerDetector;
    std::unique_ptr<LaserPointDetector>     _laserPointDetector;
    std::unique_ptr<MotionDetector>         _motionDetector;
    std::unique_ptr<OverheadEdgesDetector>  _overheadEdgeDetector;
    std::unique_ptr<CameraCalibrator>       _cameraCalibrator;
    std::unique_ptr<OverheadMap>            _overheadMap;
    std::unique_ptr<GroundPlaneClassifier>  _groundPlaneClassifier;

    std::unique_ptr<Vision::Benchmark>      _benchmark;
    std::unique_ptr<Vision::ObjectDetector> _generalObjectDetector;
    
    TimeStamp_t                   _generalObjectDetectionTimestamp = 0;
    
    // Tool code stuff
    TimeStamp_t                   _firstReadToolCodeTime_ms = 0;
    const TimeStamp_t             kToolCodeMotionTimeout_ms = 1000;
    std::vector<Vision::Image>    _toolCodeImages;
    bool                          _isReadingToolCode;
    
    Result UpdatePoseData(const VisionPoseData& newPoseData);
    Radians GetCurrentHeadAngle();
    Radians GetPreviousHeadAngle();
    
    // NOTE: CLAHE is NOT used when MarkerDetector is in LightOnDark mode
    enum class MarkerDetectionCLAHE : u8 {
      Off         = 0, // Do detection in original image only
      On          = 1, // Do detection in CLAHE image only
      Both        = 2, // Run detection twice: using original image and CLAHE image
      Alternating = 3, // Alternate using CLAHE vs. original in each successive frame
      WhenDark    = 4, // Only if mean of image is below kClaheWhenDarkThreshold
      Count
    };
    
    Result ApplyCLAHE(Vision::ImageCache& imageCache, const MarkerDetectionCLAHE useCLAHE, Vision::Image& claheImage);
    
    Result DetectMarkersWithCLAHE(Vision::ImageCache& imageCache,
                                  const Vision::Image& claheImage,
                                  std::vector<Anki::Rectangle<s32>>& detectionRects,
                                  MarkerDetectionCLAHE useCLAHE);
    
    static u8 ComputeMean(const Vision::Image& inputImageGray, const s32 sampleInc);
    
    Result DetectFaces(const Vision::Image& grayImage,
                       std::vector<Anki::Rectangle<s32>>& detectionRects);
                       
    Result DetectPets(const Vision::Image& grayImage,
                      std::vector<Anki::Rectangle<s32>>& ignoreROIs);
    
    // Will use color if not empty, or gray otherwise
    Result DetectMotion(Vision::ImageCache& imageCache);

    Result UpdateOverheadMap(const Vision::ImageRGB& image);

    Result UpdateGroundPlaneClassifier(const Vision::ImageRGB& image);
    
    void CheckForGeneralObjectDetections();
    
    Result ReadToolCode(const Vision::Image& image);
    
    bool ShouldProcessVisionMode(VisionMode mode);
    
    Result EnableMode(VisionMode whichMode, bool enabled);

    Result SaveSensorData() const;
    
    // Contrast-limited adaptive histogram equalization (CLAHE)
    cv::Ptr<cv::CLAHE> _clahe;
    s32 _lastClaheTileSize;
    s32 _lastClaheClipLimit;
    bool _currentUseCLAHE = true;

    // "Mailbox" for passing things out to main thread
    std::mutex _mutex;
    std::queue<VisionProcessingResult> _results;
    VisionProcessingResult _currentResult;

    std::string GetFileNameBasedOnFrameNumber(const char *extension) const;
}; // class VisionSystem
  
} // namespace Cozmo
} // namespace Anki

#endif // ANKI_COZMO_BASESTATION_VISIONSYSTEM_H

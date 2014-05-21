/**
* File: visionSystem.cpp
*
* Author: Andrew Stein
* Date:   (various)
*
* Description: High-level module that controls the vision system and switches
*              between fiducial detection and tracking and feeds results to
*              main execution thread via message mailboxes.
*
* Copyright: Anki, Inc. 2014
**/

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
#include "anki/cozmo/robot/cozmoConfig.h"
#include "anki/cozmo/robot/hal.h"

// Local Cozmo Includes
#include "headController.h"
#include "matlabVisualization.h"
#include "messages.h"
#include "visionSystem.h"
#include "visionDebugStream.h"

#if USE_MATLAB_TRACKER || USE_MATLAB_DETECTOR
#include "matlabVisionProcessor.h"
#endif

#if DOCKING_ALGORITHM == DOCKING_LUCAS_KANADE_AFFINE && !USE_APPROXIMATE_DOCKING_ERROR_SIGNAL
#error Affine tracker requires that USE_APPROXIMATE_DOCKING_ERROR_SIGNAL = 1.
#endif

static bool isInitialized_ = false;

namespace Anki {
  namespace Cozmo {
    namespace VisionSystem {
      using namespace Embedded;

      typedef enum {
        VISION_MODE_IDLE,
        VISION_MODE_LOOKING_FOR_MARKERS,
        VISION_MODE_TRACKING
      } VisionSystemMode;

#if 0
#pragma mark --- VisionMemory ---
#endif

      namespace VisionMemory
      {
        /* 10X the memory for debugging on a PC
        static const s32 OFFCHIP_BUFFER_SIZE = 20000000;
        static const s32 ONCHIP_BUFFER_SIZE = 1700000; // The max here is somewhere between 175000 and 180000 bytes
        static const s32 CCM_BUFFER_SIZE = 500000; // The max here is probably 65536 (0x10000) bytes
        */
        static const s32 OFFCHIP_BUFFER_SIZE = 2000000;
        static const s32 ONCHIP_BUFFER_SIZE  = 170000; // The max here is somewhere between 175000 and 180000 bytes
        static const s32 CCM_BUFFER_SIZE     = 50000; // The max here is probably 65536 (0x10000) bytes

        static const s32 MAX_MARKERS = 100; // TODO: this should probably be in visionParameters

        static OFFCHIP char offchipBuffer[OFFCHIP_BUFFER_SIZE];
        static ONCHIP char onchipBuffer[ONCHIP_BUFFER_SIZE];
        static CCM char ccmBuffer[CCM_BUFFER_SIZE];

        static MemoryStack offchipScratch_;
        static MemoryStack onchipScratch_;
        static MemoryStack ccmScratch_;

        // Markers is the one things that can move between functions, so it is always allocated in memory
        static FixedLengthList<VisionMarker> markers_;

        // WARNING: ResetBuffers should be used with caution
        static Result ResetBuffers()
        {
          offchipScratch_ = MemoryStack(offchipBuffer, OFFCHIP_BUFFER_SIZE);
          onchipScratch_  = MemoryStack(onchipBuffer, ONCHIP_BUFFER_SIZE);
          ccmScratch_     = MemoryStack(ccmBuffer, CCM_BUFFER_SIZE);

          if(!offchipScratch_.IsValid() || !onchipScratch_.IsValid() || !ccmScratch_.IsValid()) {
            PRINT("Error: InitializeScratchBuffers\n");
            return RESULT_FAIL;
          }

          markers_ = FixedLengthList<VisionMarker>(VisionMemory::MAX_MARKERS, offchipScratch_);

          return RESULT_OK;
        }

        static Result Initialize()
        {
          return ResetBuffers();
        }
      } // namespace VisionMemory

      // This private namespace stores all the "member" or "state" variables
      // with scope restricted to this file. There should be no globals
      // defined outside this namespace.
      // TODO: I don't think we really need _both_ a private namespace and static
      namespace {
        enum VignettingCorrection
        {
          VignettingCorrection_Off,
          VignettingCorrection_CameraHardware,
          VignettingCorrection_Software
        };

        // The tracker can fail to converge this many times before we give up
        // and reset the docker
        // TODO: Move this to visionParameters
        static const s32 MAX_TRACKING_FAILURES = 1;

        static const Anki::Cozmo::HAL::CameraInfo* headCamInfo_;
        static f32 headCamFOV_ver_;
        static f32 headCamFOV_hor_;
        static Array<f32> RcamWrtRobot_;

        static VisionSystemMode mode_;

        // Camera parameters
        // TODO: Should these be moved to (their own struct in) visionParameters.h/cpp?
        static f32 exposureTime;

#ifdef SIMULATOR
        // Simulator doesn't need vignetting correction on by default
        static VignettingCorrection vignettingCorrection = VignettingCorrection_Off;
#else
        static VignettingCorrection vignettingCorrection = VignettingCorrection_Software;
#endif
        static const f32 vignettingCorrectionParameters[5] = {1.56852140958887f, -0.00619880766167132f, -0.00364222219719291f, 2.75640497906470e-05f, 1.75476361058157e-05f}; //< for vignettingCorrection == VignettingCorrection_Software, computed by fit2dCurve.m

        static s32 frameNumber;
        static const bool autoExposure_enabled = true;
        static const s32 autoExposure_integerCountsIncrement = 2;
        static const f32 autoExposure_minExposureTime = 0.03f;
        static const f32 autoExposure_maxExposureTime = 0.97f;
        static const f32 autoExposure_percentileToSaturate = 0.95f;
        static const s32 autoExposure_adjustEveryNFrames = 1;

        // Tracking marker related members
        struct MarkerToTrack {
          Anki::Vision::MarkerType  type;
          f32                       width_mm;
          Point2f                   imageCenter;
          f32                       imageSearchRadius;

          MarkerToTrack();
          bool IsSpecified() const;
          void Clear();
          bool Matches(const VisionMarker& marker) const;
        };

        static MarkerToTrack markerToTrack_;

        static Quadrilateral<f32>          trackingQuad_;
        static s32                         numTrackFailures_ ;
        static Tracker                     tracker_;

        static Point3<P3P_PRECISION>       canonicalMarker3d_[4];

        // Snapshots of robot state
        static bool wasCalledOnce_, havePreviousRobotState_;
        static Messages::RobotState robotState_, prevRobotState_;

        // Parameters defined in visionParameters.h
        static DetectFiducialMarkersParameters detectionParameters_;
        static TrackerParameters               trackerParameters_;
        static Vision::CameraResolution        captureResolution_;
        static Vision::CameraResolution        faceDetectionResolution_;

        // For sending images to basestation
        static ImageSendMode_t                 imageSendMode_ = ISM_OFF;
        static Vision::CameraResolution        nextSendImageResolution_ = Vision::CAMERA_RES_NONE;

        /* Only using static members of SimulatorParameters now
        #ifdef SIMULATOR
        static SimulatorParameters             simulatorParameters_;
        #endif
        */

        //
        // Implementation of MarkerToTrack methods:
        //

        MarkerToTrack::MarkerToTrack()
        {
          Clear();
        }

        inline bool MarkerToTrack::IsSpecified() const {
          return type != Anki::Vision::MARKER_UNKNOWN;
        }

        void MarkerToTrack::Clear() {
          type        = Anki::Vision::MARKER_UNKNOWN;
          width_mm    = 0;
          imageCenter = Point2f(-1.f, -1.f);
          imageSearchRadius = -1.f;
        }

        bool MarkerToTrack::Matches(const VisionMarker& marker) const
        {
          bool doesMatch = false;

          if(marker.markerType == this->type) {
            if(this->imageCenter.x >= 0.f && this->imageCenter.y >= 0.f &&
              this->imageSearchRadius > 0.f)
            {
              // There is an image position specified, check to see if the
              // marker's centroid is close enough to it
              Point2f centroid = marker.corners.ComputeCenter<f32>();
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
      } // private namespace for VisionSystem state

#if 0
#pragma mark --- Simulator-Related Definitions ---
#endif
      // This little namespace is just for simulated processing time for
      // tracking and detection (since those run far faster in simulation on
      // a PC than they do on embedded hardware. Basically, this is used by
      // Update() below to wait until a frame is ready before proceeding.
      namespace Simulator {
#ifdef SIMULATOR
        static u32 frameReadyTime_;

        static Result Initialize() {
          frameReadyTime_ = 0;
          return RESULT_OK;
        }

        // Returns true if we are past the last set time for simulated processing
        static bool IsFrameReady() {
          return (HAL::GetMicroCounter() >= frameReadyTime_);
        }

        static void SetDetectionReadyTime() {
          frameReadyTime_ = HAL::GetMicroCounter() + SimulatorParameters::FIDUCIAL_DETECTION_PERIOD_US;
        }
        static void SetTrackingReadyTime() {
          frameReadyTime_ = HAL::GetMicroCounter() + SimulatorParameters::TRACK_BLOCK_PERIOD_US;
        }
#else
        static Result Initialize() { return RESULT_OK; }
        static bool IsFrameReady() { return true; }
        static void SetDetectionReadyTime() { }
        static void SetTrackingReadyTime() { }
#endif
      } // namespace Simulator

#if 0
#pragma mark --- Private (Static) Helper Function Implementations ---
#endif

      static Quadrilateral<f32> GetTrackerQuad(MemoryStack scratch)
      {
#if USE_MATLAB_TRACKER
        return MatlabVisionProcessor::GetTrackerQuad();
#else
        return tracker_.get_transformation().get_transformedCorners(scratch);
#endif
      } // GetTrackerQuad()

      static Result UpdateRobotState(const Messages::RobotState newRobotState)
      {
        prevRobotState_ = robotState_;
        robotState_     = newRobotState;

        if(wasCalledOnce_) {
          havePreviousRobotState_ = true;
        } else {
          wasCalledOnce_ = true;
        }

        return RESULT_OK;
      } // UpdateRobotState()

      static void GetPoseChange(f32& xChange, f32& yChange, Radians& angleChange)
      {
        AnkiAssert(havePreviousRobotState_);

        angleChange = Radians(robotState_.pose_angle) - Radians(prevRobotState_.pose_angle);

        // Position change in world (mat) coordinates
        const f32 dx = robotState_.pose_x - prevRobotState_.pose_x;
        const f32 dy = robotState_.pose_y - prevRobotState_.pose_y;

        // Get change in robot coordinates
        const f32 cosAngle = cosf(-prevRobotState_.pose_angle);
        const f32 sinAngle = sinf(-prevRobotState_.pose_angle);
        xChange = dx*cosAngle - dy*sinAngle;
        yChange = dx*sinAngle + dy*cosAngle;
      } // GetPoseChange()

      static Radians GetCurrentHeadAngle()
      {
        return robotState_.headAngle;
      }

      static Radians GetPreviousHeadAngle()
      {
        return prevRobotState_.headAngle;
      }

      void SetImageSendMode(ImageSendMode_t mode, Vision::CameraResolution res)
      {
        if (res == Vision::CAMERA_RES_QVGA ||
          res == Vision::CAMERA_RES_QQVGA ||
          res == Vision::CAMERA_RES_QQQVGA ||
          res == Vision::CAMERA_RES_QQQQVGA) {
            imageSendMode_ = mode;
            nextSendImageResolution_ = res;
        }
      }

      void DownsampleAndSendImage(Array<u8> &img)
      {
        // Only downsample if normal capture res is QVGA
        if (imageSendMode_ != ISM_OFF && captureResolution_ == Vision::CAMERA_RES_QVGA) {
          static u8 imgID = 0;

          // Downsample and split into image chunk message
          const u32 xRes = CameraModeInfo[nextSendImageResolution_].width;
          const u32 yRes = CameraModeInfo[nextSendImageResolution_].height;

          const u32 xSkip = 320 / xRes;
          const u32 ySkip = 240 / yRes;

          const u32 numTotalBytes = xRes*yRes;

          Messages::ImageChunk m;
          m.resolution = nextSendImageResolution_;
          m.imageId = ++imgID;
          m.chunkId = 0;
          m.chunkSize = IMAGE_CHUNK_SIZE;

          u32 totalByteCnt = 0;
          u32 chunkByteCnt = 0;

          //PRINT("Downsample: from %d x %d  to  %d x %d\n", img.get_size(1), img.get_size(0), xRes, yRes);

          u32 dataY = 0;
          for (u32 y = 0; y < 240; y += ySkip, dataY++)
          {
            const u8* restrict rowPtr = img.Pointer(y, 0);

            u32 dataX = 0;
            for (u32 x = 0; x < 320; x += xSkip, dataX++)
            {
              m.data[chunkByteCnt] = rowPtr[x];
              ++chunkByteCnt;
              ++totalByteCnt;

              if (chunkByteCnt == IMAGE_CHUNK_SIZE) {
                //PRINT("Sending image chunk %d\n", m.chunkId);
                HAL::RadioSendMessage(GET_MESSAGE_ID(Messages::ImageChunk), &m);
                ++m.chunkId;
                chunkByteCnt = 0;
              } else if (totalByteCnt == numTotalBytes) {
                // This should be the last message!
                //PRINT("Sending LAST image chunk %d\n", m.chunkId);
                m.chunkSize = chunkByteCnt;
                HAL::RadioSendMessage(GET_MESSAGE_ID(Messages::ImageChunk), &m);
              }
            }
          }

          // Turn off image sending if sending single image only.
          if (imageSendMode_ == ISM_SINGLE_SHOT) {
            imageSendMode_ = ISM_OFF;
          }
        }
      }

      static Result LookForMarkers(
        const Array<u8> &grayscaleImage,
        const DetectFiducialMarkersParameters &parameters,
        FixedLengthList<VisionMarker> &markers,
        MemoryStack ccmScratch,
        MemoryStack onchipScratch,
        MemoryStack offchipScratch)
      {
        BeginBenchmark("VisionSystem_LookForMarkers");

        AnkiAssert(parameters.isInitialized);

        const s32 maxMarkers = markers.get_maximumSize();

        FixedLengthList<Array<f32> > homographies(maxMarkers, ccmScratch);

        markers.set_size(maxMarkers);
        homographies.set_size(maxMarkers);

        for(s32 i=0; i<maxMarkers; i++) {
          Array<f32> newArray(3, 3, ccmScratch);
          homographies[i] = newArray;
        }

        MatlabVisualization::ResetFiducialDetection(grayscaleImage);

#if USE_MATLAB_DETECTOR
        const Result result = MatlabVisionProcessor::DetectMarkers(grayscaleImage, markers, homographies, ccmScratch);
#else
        const Result result = DetectFiducialMarkers(
          grayscaleImage,
          markers,
          homographies,
          parameters.scaleImage_numPyramidLevels, parameters.scaleImage_thresholdMultiplier,
          parameters.component1d_minComponentWidth, parameters.component1d_maxSkipDistance,
          parameters.component_minimumNumPixels, parameters.component_maximumNumPixels,
          parameters.component_sparseMultiplyThreshold, parameters.component_solidMultiplyThreshold,
          parameters.component_minHollowRatio,
          parameters.quads_minQuadArea, parameters.quads_quadSymmetryThreshold, parameters.quads_minDistanceFromImageEdge,
          parameters.decode_minContrastRatio,
          parameters.maxConnectedComponentSegments,
          parameters.maxExtractedQuads,
          parameters.quadRefinementIterations,
          false,
          ccmScratch, onchipScratch, offchipScratch);
#endif
        
        if(result != RESULT_OK) {
          return result;
        }

        EndBenchmark("VisionSystem_LookForMarkers");

        DebugStream::SendFiducialDetection(grayscaleImage, markers, ccmScratch, onchipScratch, offchipScratch);

        for(s32 i_marker = 0; i_marker < markers.get_size(); ++i_marker) {
          const VisionMarker crntMarker = markers[i_marker];

          MatlabVisualization::SendFiducialDetection(crntMarker.corners, crntMarker.markerType);
        }

        MatlabVisualization::SendDrawNow();

        return RESULT_OK;
      } // LookForMarkers()

      static Result BrightnessNormalizeImage(Array<u8>& image, const Quadrilateral<f32>& quad,
        const f32 filterWidthFraction,
        MemoryStack scratch)
      {
        if(filterWidthFraction > 0.f) {
          // TODO: Add the ability to only normalize within the vicinity of the quad
          // Note that this requires templateQuad to be sorted!
          const s32 filterWidth = static_cast<s32>(filterWidthFraction*((quad[3] - quad[0]).Length()));
          AnkiAssert(filterWidth > 0.f);

          Array<u8> imageNormalized(image.get_size(0), image.get_size(1), scratch);

          AnkiConditionalErrorAndReturnValue(imageNormalized.IsValid(),
            RESULT_FAIL_OUT_OF_MEMORY,
            "VisionSystem::BrightnessNormalizeImage",
            "Out of memory allocating imageNormalized.\n");

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
        } // if(filterWidthFraction > 0)

        return RESULT_OK;
      } // BrightnessNormalizeImage()

      static Result InitTemplate(
        const Array<u8> &grayscaleImage,
        const Quadrilateral<f32> &trackingQuad,
        const TrackerParameters &parameters,
        Tracker &tracker,
        MemoryStack ccmScratch,
        MemoryStack &onchipMemory, //< NOTE: onchip is a reference
        MemoryStack &offchipMemory)
      {
        AnkiAssert(parameters.isInitialized);

#if USE_MATLAB_TRACKER
        return MatlabVisionProcessor::InitTemplate(grayscaleImage, trackingQuad, ccmScratch);
#endif

#if DOCKING_ALGORITHM == DOCKING_LUCAS_KANADE_SLOW || DOCKING_ALGORITHM == DOCKING_LUCAS_KANADE_AFFINE || DOCKING_ALGORITHM == DOCKING_LUCAS_KANADE_PROJECTIVE
        // TODO: At some point template initialization should happen at full detection resolution but for
        //       now, we have to downsample to tracking resolution

        Array<u8> grayscaleImageSmall(parameters.trackingImageHeight, parameters.trackingImageWidth, ccmScratch);
        u32 downsampleFactor = DownsampleHelper(grayscaleImage, grayscaleImageSmall, ccmScratch);

        AnkiAssert(downsampleFactor > 0);
        // Note that the templateRegion and the trackingQuad are both at DETECTION_RESOLUTION, not
        // necessarily the resolution of the frame.
        //const u32 downsampleFactor = parameters.detectionWidth / parameters.trackingImageWidth;
        //const u32 downsamplePower = Log2u32(downsampleFactor);

        /*Quadrilateral<f32> trackingQuadSmall;

        for(s32 i=0; i<4; ++i) {
        trackingQuadSmall[i].x = trackingQuad[i].x / downsampleFactor;
        trackingQuadSmall[i].y = trackingQuad[i].y / downsampleFactor;
        }*/

#endif // #if DOCKING_ALGORITHM == DOCKING_LUCAS_KANADE_SLOW || DOCKING_ALGORITHM == DOCKING_LUCAS_KANADE_AFFINE || DOCKING_ALGORITHM == DOCKING_LUCAS_KANADE_PROJECTIVE

#if DOCKING_ALGORITHM == DOCKING_LUCAS_KANADE_SLOW
        tracker = TemplateTracker::LucasKanadeTracker_Slow(
          grayscaleImageSmall,
          trackingQuad,
          parameters.scaleTemplateRegionPercent,
          parameters.numPyramidLevels,
          Transformations::TRANSFORM_TRANSLATION,
          0.0,
          onchipMemory);
#elif DOCKING_ALGORITHM == DOCKING_LUCAS_KANADE_AFFINE
        tracker = TemplateTracker::LucasKanadeTracker_Affine(
          grayscaleImageSmall,
          trackingQuad,
          parameters.scaleTemplateRegionPercent,
          parameters.numPyramidLevels,
          Transformations::TRANSFORM_AFFINE,
          onchipMemory);
#elif DOCKING_ALGORITHM == DOCKING_LUCAS_KANADE_PROJECTIVE
        tracker = TemplateTracker::LucasKanadeTracker_Projective(
          grayscaleImageSmall,
          trackingQuad,
          parameters.scaleTemplateRegionPercent,
          parameters.numPyramidLevels,
          Transformations::TRANSFORM_PROJECTIVE,
          onchipMemory);
#elif DOCKING_ALGORITHM == DOCKING_LUCAS_KANADE_SAMPLED_PROJECTIVE
        tracker = TemplateTracker::LucasKanadeTracker_SampledProjective(
          grayscaleImage,
          trackingQuad,
          parameters.scaleTemplateRegionPercent,
          parameters.numPyramidLevels,
          Transformations::TRANSFORM_PROJECTIVE,
          parameters.maxSamplesAtBaseLevel,
          ccmScratch,
          onchipMemory,
          offchipMemory);
#elif DOCKING_ALGORITHM == DOCKING_BINARY_TRACKER
#ifdef USE_HEADER_TEMPLATE
        tracker = TemplateTracker::BinaryTracker(
          Vision::MARKER_BATTERIES,
          grayscaleImage,
          trackingQuad,
          parameters.scaleTemplateRegionPercent,
          parameters.edgeDetectionParams_template,
          onchipMemory, offchipMemory);
#else // #ifdef USE_HEADER_TEMPLATE
        tracker = TemplateTracker::BinaryTracker(
          grayscaleImage,
          trackingQuad,
          parameters.scaleTemplateRegionPercent,
          parameters.edgeDetectionParams_template,
          onchipMemory, offchipMemory);
#endif // #ifdef USE_HEADER_TEMPLATE ... #else

#elif DOCKING_ALGORITHM == DOCKING_LUCAS_KANADE_SAMPLED_PLANAR6DOF

        tracker = TemplateTracker::LucasKanadeTracker_SampledPlanar6dof(grayscaleImage,
          trackingQuad,
          parameters.scaleTemplateRegionPercent,
          parameters.numPyramidLevels,
          Transformations::TRANSFORM_PROJECTIVE,
          parameters.numFiducialEdgeSamples,
          FIDUCIAL_SQUARE_WIDTH_FRACTION,
          parameters.numInteriorSamples,
          parameters.numSamplingRegions,
          headCamInfo_->focalLength_x,
          headCamInfo_->focalLength_y,
          headCamInfo_->center_x,
          headCamInfo_->center_y,
          markerToTrack_.width_mm,
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
#else
#error Unknown DOCKING_ALGORITHM.
#endif

        if(!tracker.IsValid()) {
          return RESULT_FAIL;
        }

        MatlabVisualization::SendTrackInit(grayscaleImage, tracker, onchipMemory);

#if DOCKING_ALGORITHM == DOCKING_BINARY_TRACKER
        DebugStream::SendBinaryTracker(tracker, ccmScratch, onchipMemory, offchipMemory);
#endif

        return RESULT_OK;
      } // InitTemplate()

      static Result TrackTemplate(const Array<u8> &grayscaleImage,
        const Quadrilateral<f32> &trackingQuad,
        const TrackerParameters &parameters,
        Tracker &tracker,
        bool &trackingSucceeded,
        MemoryStack ccmScratch,
        MemoryStack onchipScratch,
        MemoryStack offchipScratch)
      {
        BeginBenchmark("VisionSystem_TrackTemplate");

        AnkiAssert(parameters.isInitialized);

#if USE_MATLAB_TRACKER
        return MatlabVisionProcessor::TrackTemplate(grayscaleImage, converged, ccmScratch);
#endif

#if DOCKING_ALGORITHM == DOCKING_LUCAS_KANADE_SLOW || DOCKING_ALGORITHM == DOCKING_LUCAS_KANADE_AFFINE || DOCKING_ALGORITHM == DOCKING_LUCAS_KANADE_PROJECTIVE
        // TODO: At some point template initialization should happen at full detection resolution
        //       but for now, we have to downsample to tracking resolution
        Array<u8> grayscaleImageSmall(parameters.trackingImageHeight, parameters.trackingImageWidth, ccmScratch);
        DownsampleHelper(grayscaleImage, grayscaleImageSmall, ccmScratch);

        //DebugStream::SendArray(grayscaleImageSmall);
#endif

        trackingSucceeded = false;
        s32 verify_meanAbsoluteDifference;
        s32 verify_numInBounds;
        s32 verify_numSimilarPixels;

#if DOCKING_ALGORITHM == DOCKING_LUCAS_KANADE_SLOW
        const Result trackerResult = tracker.UpdateTrack(
          grayscaleImage,
          parameters.maxIterations,
          parameters.convergenceTolerance,
          parameters.useWeights,
          trackingSucceeded,
          onchipScratch);

#elif DOCKING_ALGORITHM == DOCKING_LUCAS_KANADE_AFFINE
        const Result trackerResult = tracker.UpdateTrack(
          grayscaleImageSmall,
          parameters.maxIterations,
          parameters.convergenceTolerance,
          parameters.verify_maxPixelDifference,
          trackingSucceeded,
          verify_meanAbsoluteDifference,
          verify_numInBounds,
          verify_numSimilarPixels,
          onchipScratch);

        //tracker.get_transformation().Print("track");

#elif DOCKING_ALGORITHM == DOCKING_LUCAS_KANADE_PROJECTIVE
        const Result trackerResult = tracker.UpdateTrack(
          grayscaleImageSmall,
          parameters.maxIterations,
          parameters.convergenceTolerance,
          parameters.verify_maxPixelDifference,
          trackingSucceeded,
          verify_meanAbsoluteDifference,
          verify_numInBounds,
          verify_numSimilarPixels,
          onchipScratch);

        //tracker.get_transformation().Print("track");

#elif DOCKING_ALGORITHM == DOCKING_LUCAS_KANADE_SAMPLED_PROJECTIVE

        const Result trackerResult = tracker.UpdateTrack(
          grayscaleImage,
          parameters.maxIterations,
          parameters.convergenceTolerance,
          parameters.verify_maxPixelDifference,
          trackingSucceeded,
          verify_meanAbsoluteDifference,
          verify_numInBounds,
          verify_numSimilarPixels,
          onchipScratch);

        //tracker.get_transformation().Print("track");

#elif DOCKING_ALGORITHM == DOCKING_BINARY_TRACKER
        s32 numMatches = -1;

        const Result trackerResult = tracker.UpdateTrack_Normal(
          grayscaleImage,
          parameters.edgeDetectionParams_update,
          parameters.matching_maxTranslationDistance,
          parameters.matching_maxProjectiveDistance,
          parameters.verify_maxTranslationDistance,
          parameters.verify_maxPixelDifference,
          parameters.verify_coordinateIncrement,
          numMatches,
          verify_meanAbsoluteDifference,
          verify_numInBounds,
          verify_numSimilarPixels,
          ccmScratch, offchipScratch);

        //tracker.get_transformation().Print("track");

        const s32 numTemplatePixels = tracker.get_numTemplatePixels();

        const f32 percentMatchedPixels = static_cast<f32>(numMatches) / static_cast<f32>(numTemplatePixels);

        if(percentMatchedPixels >= parameters.percentMatchedPixelsThreshold) {
          trackingSucceeded = true;
        } else {
          trackingSucceeded = false;
        }

#elif DOCKING_ALGORITHM == DOCKING_LUCAS_KANADE_SAMPLED_PLANAR6DOF

        const Radians initAngleX(tracker.get_angleX());
        const Radians initAngleY(tracker.get_angleY());
        const Radians initAngleZ(tracker.get_angleZ());
        const Point3<f32>& initTranslation = tracker.get_translation();

        bool converged = false;
        const Result trackerResult = tracker.UpdateTrack(grayscaleImage,
          parameters.maxIterations,
          parameters.convergenceTolerance_angle,
          parameters.convergenceTolerance_distance,
          parameters.verify_maxPixelDifference,
          converged,
          verify_meanAbsoluteDifference,
          verify_numInBounds,
          verify_numSimilarPixels,
          onchipScratch);

        // TODO: Do we care if converged == false?

        //
        // Go through a bunch of checks to see whether the tracking succeeded
        //

        if(fabs((initAngleX - tracker.get_angleX()).ToFloat()) > parameters.successTolerance_angle ||
           fabs((initAngleY - tracker.get_angleY()).ToFloat()) > parameters.successTolerance_angle ||
           fabs((initAngleZ - tracker.get_angleZ()).ToFloat()) > parameters.successTolerance_angle)
        {
          PRINT("Tracker failed: angle(s) changed too much.\n");
          trackingSucceeded = false;
        }
        else if(tracker.get_translation().z < TrackerParameters::MIN_TRACKER_DISTANCE)
        {
          PRINT("Tracker failed: final distance too close.\n");
          trackingSucceeded = false;
        }
        else if(tracker.get_translation().z > TrackerParameters::MAX_TRACKER_DISTANCE)
        {
          PRINT("Tracker failed: final distance too far away.\n");
          trackingSucceeded = false;
        }
        else if((initTranslation - tracker.get_translation()).Length() > parameters.successTolerance_distance)
        {
          PRINT("Tracker failed: position changed too much.\n");
          trackingSucceeded = false;
        }
        else if(fabs(tracker.get_angleX()) > TrackerParameters::MAX_BLOCK_DOCKING_ANGLE)
        {
          PRINT("Tracker failed: target X angle too large.\n");
          trackingSucceeded = false;
        }
        else if(fabs(tracker.get_angleY()) > TrackerParameters::MAX_BLOCK_DOCKING_ANGLE)
        {
          PRINT("Tracker failed: target Y angle too large.\n");
          trackingSucceeded = false;
        }
        else if(fabs(tracker.get_angleZ()) > TrackerParameters::MAX_BLOCK_DOCKING_ANGLE)
        {
          PRINT("Tracker failed: target Z angle too large.\n");
          trackingSucceeded = false;
        }
        else if(atan_fast(fabs(tracker.get_translation().x) / tracker.get_translation().z) > TrackerParameters::MAX_DOCKING_FOV_ANGLE)
        {
          PRINT("Tracker failed: FOV angle too large.\n");
          trackingSucceeded = false;
        }
        else if( (static_cast<f32>(verify_numSimilarPixels) /
          static_cast<f32>(verify_numInBounds)) < parameters.successTolerance_matchingPixelsFraction)
        {
          PRINT("Tracker failed: too many in-bounds pixels failed intensity verification (%d / %d < %f).\n",
            verify_numSimilarPixels, verify_numInBounds, parameters.successTolerance_matchingPixelsFraction);
          trackingSucceeded = false;
        }
        else {
          // Everything seems ok!
          trackingSucceeded = true;
        }

#else
#error Unknown DOCKING_ALGORITHM!
#endif

        if(trackerResult != RESULT_OK) {
          return RESULT_FAIL;
        }

        // Sanity check on tracker result
#if DOCKING_ALGORITHM != DOCKING_LUCAS_KANADE_SAMPLED_PLANAR6DOF

        // Check for a super shrunk or super large template
        // (I don't think this works for planar 6dof homographies?  Try dividing by h22?)
#warning broken
        /*{
        // TODO: make not hacky
        const Array<f32> &homography = tracker.get_transformation().get_homography();

        const s32 numValues = 4;
        const s32 numMaxValues = 2;
        f32 values[numValues] = {ABS(homography[0][0]), ABS(homography[0][1]), ABS(homography[1][0]), ABS(homography[1][1])};
        s32 maxInds[numMaxValues] = {0, 1};
        for(s32 i=1; i<numValues; i++) {
        if(values[i] > values[maxInds[0]]) {
        maxInds[0] = i;
        }
        }

        for(s32 i=0; i<numValues; i++) {
        if(i == maxInds[0])
        continue;

        if(values[i] > values[maxInds[1]]) {
        maxInds[1] = i;
        }
        }

        const f32 secondValue = values[maxInds[1]];

        if(secondValue < 0.1f || secondValue > 40.0f) {
        converged = false;
        }
        }*/
#endif // #if DOCKING_ALGORITHM != DOCKING_LUCAS_KANADE_SAMPLED_PLANAR6DOF

        EndBenchmark("VisionSystem_TrackTemplate");

        MatlabVisualization::SendTrack(grayscaleImage, tracker, trackingSucceeded, offchipScratch);

        //MatlabVisualization::SendTrackerPrediction_Compare(tracker, offchipScratch);

        DebugStream::SendTrackingUpdate(grayscaleImage, tracker, parameters, verify_meanAbsoluteDifference, static_cast<f32>(verify_numSimilarPixels) / static_cast<f32>(verify_numInBounds), ccmScratch, onchipScratch, offchipScratch);

        return RESULT_OK;
      } // TrackTemplate()

      //
      // Tracker Prediction
      //
      // Adjust the tracker transformation by approximately how much we
      // think we've moved since the last tracking call.
      //
      static Result TrackerPredictionUpdate(const Array<u8>& grayscaleImage, MemoryStack scratch)
      {
        Result result = RESULT_OK;

        const Quadrilateral<f32> currentQuad = GetTrackerQuad(scratch);

        MatlabVisualization::SendTrackerPrediction_Before(grayscaleImage, currentQuad);

        // Ask VisionState how much we've moved since last call (in robot coordinates)
        Radians theta_robot;
        f32 T_fwd_robot, T_hor_robot;

        GetPoseChange(T_fwd_robot, T_hor_robot, theta_robot);

#if DOCKING_ALGORITHM == DOCKING_LUCAS_KANADE_SAMPLED_PLANAR6DOF

#if USE_MATLAB_TRACKER
        MatlabVisionProcessor::UpdateTracker(T_fwd_robot, T_hor_robot,
          theta_robot, theta_head);
#else
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

        Point3<f32> T_geometry(T_hor_robot*cR + term1*sR - T_fwd_robot*sR,
          term1*cR*sH2 - term2*cH2 + term3*cH2 - term4*sH2 - term5*sH2,
          term1*cH2*cR - term4*cH2 - term5*cH2 + term2*sH2 - term3*sH2);

        Array<f32> R_blockRelHead = Array<f32>(3,3,scratch);
        tracker_.get_rotationMatrix(R_blockRelHead);
        const Point3<f32>& T_blockRelHead = tracker_.get_translation();

        Array<f32> R_blockRelHead_new = Array<f32>(3,3,scratch);
        Matrix::Multiply(R_geometry, R_blockRelHead, R_blockRelHead_new);

        Point3<f32> T_blockRelHead_new = R_geometry*T_blockRelHead + T_geometry;

        if(tracker_.UpdateRotationAndTranslation(R_blockRelHead_new,
          T_blockRelHead_new,
          scratch) == RESULT_OK)
        {
          result = RESULT_OK;
        }

#endif // #if USE_MATLAB_TRACKER

#else
        const Quadrilateral<f32> sortedQuad  = currentQuad.ComputeClockwiseCorners<f32>();

        f32 dx = sortedQuad[3].x - sortedQuad[0].x;
        f32 dy = sortedQuad[3].y - sortedQuad[0].y;
        const f32 observedVerticalSize_pix = sqrtf( dx*dx + dy*dy );

        // Compare observed vertical size to actual block marker size (projected
        // to be orthogonal to optical axis, using head angle) to approximate the
        // distance to the marker along the camera's optical axis
        Radians theta_head = GetCurrentHeadAngle();
        const f32 cosHeadAngle = cosf(theta_head.ToFloat());
        const f32 sinHeadAngle = sinf(theta_head.ToFloat());
        const f32 d = (trackingMarkerWidth_mm* cosHeadAngle *
          headCamInfo_->focalLength_y /
          observedVerticalSize_pix);

        // Convert to how much we've moved along (and orthogonal to) the camera's optical axis
        const f32 T_fwd_cam =  T_fwd_robot*cosHeadAngle;
        const f32 T_ver_cam = -T_fwd_robot*sinHeadAngle;

        // Predict approximate horizontal shift from two things:
        // 1. The rotation of the robot
        //    Compute pixel-per-degree of the camera and multiply by degrees rotated
        // 2. Convert horizontal shift of the robot to pixel shift, using
        //    focal length
        f32 horizontalShift_pix = (static_cast<f32>(headCamInfo_->ncols/2) * theta_robot.ToFloat() /
          headCamFOV_hor_) + (T_hor_robot*headCamInfo_->focalLength_x/d);

        // Predict approximate scale change by comparing the distance to the
        // object before and after forward motion
        const f32 scaleChange = d / (d - T_fwd_cam);

        // Predict approximate vertical shift in the camera plane by comparing
        // vertical motion (orthogonal to camera's optical axis) to the focal
        // length
        const f32 verticalShift_pix = T_ver_cam * headCamInfo_->focalLength_y/d;

        PRINT("Adjusting transformation: %.3fpix H shift for %.3fdeg rotation, "
          "%.3f scaling and %.3f V shift for %.3f translation forward (%.3f cam)\n",
          horizontalShift_pix, theta_robot.getDegrees(), scaleChange,
          verticalShift_pix, T_fwd_robot, T_fwd_cam);

        // Adjust the Transformation
        // Note: UpdateTransformation is doing *inverse* composition (thus using the negatives)
        if(tracker_.get_transformation().get_transformType() == Transformations::TRANSFORM_TRANSLATION) {
          Array<f32> update(1,2,scratch);
          update[0][0] = -horizontalShift_pix;
          update[0][1] = -verticalShift_pix;

#if USE_MATLAB_TRACKER
          MatlabVisionProcessor::UpdateTracker(update);
#else
          tracker_.UpdateTransformation(update, 1.f, scratch,
            Transformations::TRANSFORM_TRANSLATION);
#endif
        }
        else {
          // Inverse update we are composing is:
          //
          //                  [s 0 0]^(-1)     [0 0 h_shift]^(-1)
          //   updateMatrix = [0 s 0]       *  [0 0 v_shift]
          //                  [0 0 1]          [0 0    1   ]
          //
          //      [1/s  0  -h_shift/s]   [ update_0  update_1  update_2 ]
          //   =  [ 0  1/2 -v_shift/s] = [ update_3  update_4  update_5 ]
          //      [ 0   0      1     ]   [    0         0         1     ]
          //
          // Note: UpdateTransformation adds 1.0 to the diagonal scale terms
          Array<f32> update(1,6,scratch);
          update.Set(0.f);
          update[0][0] = 1.f/scaleChange - 1.f;               // first row, first col
          update[0][2] = -horizontalShift_pix/scaleChange;    // first row, last col
          update[0][4] = 1.f/scaleChange - 1.f;               // second row, second col
          update[0][5] = -verticalShift_pix/scaleChange;      // second row, last col

#if USE_MATLAB_TRACKER
          MatlabVisionProcessor::UpdateTracker(update);
#else
          tracker_.UpdateTransformation(update, 1.f, scratch,
            Transformations::TRANSFORM_AFFINE);
#endif
        } // if(tracker transformation type == TRANSLATION...)

#endif // if DOCKING_ALGORITHM == DOCKING_LUCAS_KANADE_SAMPLED_PLANAR6DOF

        MatlabVisualization::SendTrackerPrediction_After(GetTrackerQuad(scratch));

        return result;
      } // TrackerPredictionUpdate()

      static void FillDockErrMsg(const Quadrilateral<f32>& currentQuad,
        Messages::DockingErrorSignal& dockErrMsg,
        MemoryStack scratch)
      {
        dockErrMsg.isApproximate = false;

#if USE_APPROXIMATE_DOCKING_ERROR_SIGNAL
        dockErrMsg.isApproximate = true;

        const bool useTopBar = false; // TODO: pass in? make a docker parameter?
        const f32 focalLength_x = headCamInfo_->focalLength_x;
        const f32 imageResolutionWidth_pix = detectionParameters_.detectionWidth;

        Quadrilateral<f32> sortedQuad = currentQuad.ComputeClockwiseCorners<f32>();
        const Point<f32>& lineLeft  = (useTopBar ? sortedQuad[0] : sortedQuad[3]); // topLeft  or bottomLeft
        const Point<f32>& lineRight = (useTopBar ? sortedQuad[1] : sortedQuad[2]); // topRight or bottomRight

        AnkiAssert(lineRight.x > lineLeft.x);

        //L = sqrt(sum( (upperRight-upperLeft).^2) );
        const f32 lineDx = lineRight.x - lineLeft.x;
        const f32 lineDy = lineRight.y - lineLeft.y;
        const f32 lineLength = sqrtf(lineDx*lineDx + lineDy*lineDy);

        // Get the angle from vertical of the top or bottom bar of the marker
        //we're tracking

        //angleError = -asin( (upperRight(2)-upperLeft(2)) / L);
        //const f32 angleError = -asinf( (upperRight.y-upperLeft.y) / lineLength);
        const f32 angleError = -asinf( (lineRight.y-lineLeft.y) / lineLength) * 4;  // Multiply by scalar which makes angleError a little more accurate.  TODO: Something smarter than this.

        //currentDistance = BlockMarker3D.ReferenceWidth * this.calibration.fc(1) / L;
        const f32 distanceError = trackingMarkerWidth_mm * focalLength_x / lineLength;

        //ANS: now returning error in terms of camera. mainExecution converts to robot coords
        // //distError = currentDistance - CozmoDocker.LIFT_DISTANCE;
        // const f32 distanceError = currentDistance - cozmoLiftDistanceInMM;

        // TODO: should I be comparing to ncols/2 or calibration center?

        //midPointErr = -( (upperRight(1)+upperLeft(1))/2 - this.trackingResolution(1)/2 );
        f32 midpointError = ( (lineRight.x+lineLeft.x)/2 - imageResolutionWidth_pix/2 );

        //midPointErr = midPointErr * currentDistance / this.calibration.fc(1);
        midpointError *= distanceError / focalLength_x;

        // Go ahead and put the errors in the robot centric coordinates (other
        // than taking head angle into account)
        dockErrMsg.x_distErr = distanceError;
        dockErrMsg.y_horErr  = -midpointError;
        dockErrMsg.angleErr  = angleError;
        dockErrMsg.z_height  = -1.f; // unknown for approximate error signal

#elif DOCKING_ALGORITHM == DOCKING_LUCAS_KANADE_SAMPLED_PLANAR6DOF

#if USE_MATLAB_TRACKER
        MatlabVisionProcessor::ComputeProjectiveDockingSignal(currentQuad,
          dockErrMsg.x_distErr,
          dockErrMsg.y_horErr,
          dockErrMsg.z_height,
          dockErrMsg.angleErr);
#else
        // Despite the names, fill the elements of the message with camera-centric coordinates
        dockErrMsg.x_distErr = tracker_.get_translation().x;
        dockErrMsg.y_horErr  = tracker_.get_translation().y;
        dockErrMsg.z_height  = tracker_.get_translation().z;

        dockErrMsg.angleErr  = tracker_.get_angleY();

#endif // if USE_MATLAB_TRACKER

#elif DOCKING_ALGORITHM == DOCKING_LUCAS_KANADE_PROJECTIVE || DOCKING_ALGORITHM == DOCKING_LUCAS_KANADE_SAMPLED_PROJECTIVE || DOCKING_ALGORITHM == DOCKING_BINARY_TRACKER

#if USE_MATLAB_TRACKER
        MatlabVisionProcessor::ComputeProjectiveDockingSignal(currentQuad,
          dockErrMsg.x_distErr,
          dockErrMsg.y_horErr,
          dockErrMsg.z_height,
          dockErrMsg.angleErr);
#else

        // Compute the current pose of the block relative to the camera:
        Array<P3P_PRECISION> R = Array<P3P_PRECISION>(3,3, scratch);
        Point3<P3P_PRECISION> T;
        Quadrilateral<P3P_PRECISION> currentQuad_atPrecision(Point<P3P_PRECISION>(currentQuad[0].x, currentQuad[0].y),
          Point<P3P_PRECISION>(currentQuad[1].x, currentQuad[1].y),
          Point<P3P_PRECISION>(currentQuad[2].x, currentQuad[2].y),
          Point<P3P_PRECISION>(currentQuad[3].x, currentQuad[3].y));

#warning broken
        /*P3P::computePose(currentQuad_atPrecision,
        canonicalMarker3d_[0], canonicalMarker3d_[1],
        canonicalMarker3d_[2], canonicalMarker3d_[3],
        headCamInfo_->focalLength_x, headCamInfo_->focalLength_y,
        headCamInfo_->center_x, headCamInfo_->center_y,
        R, T, scratch);*/

        // Extract what we need for the docking error signal from the block's pose:
        dockErrMsg.x_distErr = T.x;
        dockErrMsg.y_horErr  = T.y;
        dockErrMsg.z_height  = T.z;
        dockErrMsg.angleErr  = asinf(R[2][0]);

#endif // if USE_MATLAB_TRACKER

#endif // if USE_APPROXIMATE_DOCKING_ERROR_SIGNAL
      } // FillDockErrMsg()

#if 0
#pragma mark --- Public VisionSystem API Implementations ---
#endif

      u32 DownsampleHelper(const Array<u8>& in,
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

      const HAL::CameraInfo* GetCameraCalibration() {
        // TODO: is just returning the pointer to HAL's camera info struct kosher?
        return headCamInfo_;
      }

      f32 GetTrackingMarkerWidth() {
        return markerToTrack_.width_mm;
      }

      f32 GetVerticalFOV() {
        return headCamFOV_ver_;
      }

      f32 GetHorizontalFOV() {
        return headCamFOV_hor_;
      }

      Result Init()
      {
        Result result = RESULT_OK;

        if(!isInitialized_) {
          captureResolution_ = Vision::CAMERA_RES_QVGA;
          faceDetectionResolution_ = Vision::CAMERA_RES_QVGA;

          // WARNING: the order of these initializations matter!

          //
          // Initialize the VisionSystem's state (i.e. its "private member variables")
          //

          mode_                      = VISION_MODE_LOOKING_FOR_MARKERS;
          markerToTrack_.Clear();
          numTrackFailures_          = 0;

          wasCalledOnce_             = false;
          havePreviousRobotState_    = false;

          headCamInfo_ = HAL::GetHeadCamInfo();
          if(headCamInfo_ == NULL) {
            PRINT("Initialize() - HeadCam Info pointer is NULL!\n");
            return RESULT_FAIL;
          }

          // Compute FOV from focal length (currently used for tracker prediciton)
          headCamFOV_ver_ = 2.f * atanf(static_cast<f32>(headCamInfo_->nrows) /
            (2.f * headCamInfo_->focalLength_y));
          headCamFOV_hor_ = 2.f * atanf(static_cast<f32>(headCamInfo_->ncols) /
            (2.f * headCamInfo_->focalLength_x));

          exposureTime = 0.2f; // TODO: pick a reasonable start value
          frameNumber = 0;

          detectionParameters_.Initialize();
          trackerParameters_.Initialize();

          Simulator::Initialize();

#ifdef RUN_SIMPLE_TRACKING_TEST
          Anki::Cozmo::VisionSystem::SetMarkerToTrack(Vision::MARKER_BATTERIES,
            DEFAULT_BLOCK_MARKER_WIDTH_MM);
#endif

          result = VisionMemory::Initialize();
          if(result != RESULT_OK) { return result; }

          result = DebugStream::Initialize();
          if(result != RESULT_OK) { return result; }

          result = MatlabVisualization::Initialize();
          if(result != RESULT_OK) { return result; }

#if USE_MATLAB_TRACKER || USE_MATLAB_DETECTOR
          result = MatlabVisionProcessor::Initialize();
          if(result != RESULT_OK) { return result; }
#endif

          RcamWrtRobot_ = Array<f32>(3,3,VisionMemory::onchipScratch_);

          isInitialized_ = true;
        }

        return result;
      }

      Result SetMarkerToTrack(const Vision::MarkerType& markerTypeToTrack,
        const f32 markerWidth_mm)
      {
        const Point2f imageCenter(-1.f, -1.f);
        const f32     searchRadius = -1.f;
        return SetMarkerToTrack(markerTypeToTrack, markerWidth_mm,
          imageCenter, searchRadius);
      }

      Result SetMarkerToTrack(const Vision::MarkerType& markerTypeToTrack,
        const f32 markerWidth_mm,
        const Point2f& atImageCenter,
        const f32 imageSearchRadius)
      {
        markerToTrack_.type              = markerTypeToTrack;
        markerToTrack_.width_mm          = markerWidth_mm;
        markerToTrack_.imageCenter       = atImageCenter;
        markerToTrack_.imageSearchRadius = imageSearchRadius;

        mode_                  = VISION_MODE_LOOKING_FOR_MARKERS;
        numTrackFailures_      = 0;

        // If the marker type is valid, start looking for it
        if(markerToTrack_.IsSpecified())
        {
          // Set canonical 3D marker's corner coordinates
          const P3P_PRECISION markerHalfWidth = markerToTrack_.width_mm * P3P_PRECISION(0.5);
          canonicalMarker3d_[0] = Point3<P3P_PRECISION>(-markerHalfWidth, -markerHalfWidth, 0);
          canonicalMarker3d_[1] = Point3<P3P_PRECISION>(-markerHalfWidth,  markerHalfWidth, 0);
          canonicalMarker3d_[2] = Point3<P3P_PRECISION>( markerHalfWidth, -markerHalfWidth, 0);
          canonicalMarker3d_[3] = Point3<P3P_PRECISION>( markerHalfWidth,  markerHalfWidth, 0);
        }

        return RESULT_OK;
      } // SetMarkerToTrack()

      void StopTracking()
      {
        markerToTrack_.Clear();
        mode_ = VISION_MODE_LOOKING_FOR_MARKERS;
      }

      const Embedded::FixedLengthList<Embedded::VisionMarker>& GetObservedMarkerList()
      {
        return VisionMemory::markers_;
      } // GetObservedMarkerList()

      Result GetVisionMarkerPoseNearestTo(const Embedded::Point3<f32>&  atPosition,
        const Vision::MarkerType&     withType,
        const f32                     maxDistance_mm,
        Embedded::Array<f32>&         rotationWrtRobot,
        Embedded::Point3<f32>&        translationWrtRobot,
        bool&                         markerFound)
      {
        using namespace Embedded;

        Result lastResult = RESULT_OK;
        markerFound = false;

        if(VisionMemory::markers_.get_size() > 0)
        {
          FixedLengthList<VisionMarker*> markersWithType(VisionMemory::markers_.get_size(),
            VisionMemory::onchipScratch_);

          AnkiConditionalErrorAndReturnValue(markersWithType.IsValid(),
            RESULT_FAIL_MEMORY,
            "GetVisionMarkerPoseNearestTo",
            "Failed to allocate markersWithType FixedLengthList.");

          // Find all markers with specified type
          s32 numFound = 0;
          VisionMarker  * restrict pMarker = VisionMemory::markers_.Pointer(0);
          VisionMarker* * restrict pMarkerWithType = markersWithType.Pointer(0);

          for(s32 i=0; i<VisionMemory::markers_.get_size(); ++i)
          {
            if(pMarker[i].markerType == withType) {
              pMarkerWithType[numFound++] = pMarker + i;
            }
          }
          markersWithType.set_size(numFound);

          // If any were found, find the one that is closest to the specified
          // 3D point and within the specified max distance
          if(numFound > 0) {
            // Create a little MemoryStack for allocating temporary
            // rotation matrix
            const s32 SCRATCH_BUFFER_SIZE = 128;
            char scratchBuffer[SCRATCH_BUFFER_SIZE];
            MemoryStack scratch(scratchBuffer, SCRATCH_BUFFER_SIZE);

            // Create temporary pose storage (wrt camera)
            Point3<f32> translationWrtCamera;
            Array<f32> rotationWrtCamera(3,3,scratch);
            AnkiConditionalErrorAndReturnValue(rotationWrtCamera.IsValid(),
              RESULT_FAIL_MEMORY,
              "GetVisionMarkerPoseNearestTo",
              "Failed to allocate rotationWrtCamera Array.");

            VisionMarker* const* restrict pMarkerWithType = markersWithType.Pointer(0);

            f32 closestDistance = maxDistance_mm;

            for(s32 i=0; i<numFound; ++i) {
              // Compute this marker's pose WRT camera
              if((lastResult = GetVisionMarkerPose(*(pMarkerWithType[i]), true,
                rotationWrtCamera, translationWrtCamera)) != RESULT_OK) {
                  return lastResult;
              }

              // Convert it to pose WRT robot
              if((lastResult = GetWithRespectToRobot(rotationWrtCamera, translationWrtCamera,
                rotationWrtRobot, translationWrtRobot)) != RESULT_OK) {
                  return lastResult;
              }

              // See how far it is from the specified position
              const f32 currentDistance = (translationWrtRobot - atPosition).Length();
              if(currentDistance < closestDistance) {
                closestDistance = currentDistance;
                markerFound = true;
              }
            } // for each marker with type
          } // if numFound > 0
        } // if(VisionMemory::markers_.get_size() > 0)

        return RESULT_OK;
      } // GetVisionMarkerPoseNearestTo()

      template<typename PRECISION>
      static Result GetCamPoseWrtRobot(Array<PRECISION>& RcamWrtRobot,
        Point3<PRECISION>& TcamWrtRobot)
      {
        AnkiConditionalErrorAndReturnValue(RcamWrtRobot.get_size(0)==3 &&
          RcamWrtRobot.get_size(1)==3,
          RESULT_FAIL_INVALID_SIZE,
          "VisionSystem::GetCamPoseWrtRobot",
          "Rotation matrix must already be 3x3.");

        const f32 headAngle = HeadController::GetAngleRad();
        const f32 cosH = cosf(headAngle);
        const f32 sinH = sinf(headAngle);

        RcamWrtRobot[0][0] = 0;  RcamWrtRobot[0][1] = sinH;  RcamWrtRobot[0][2] = cosH;
        RcamWrtRobot[1][0] = -1; RcamWrtRobot[1][1] = 0;     RcamWrtRobot[1][2] = 0;
        RcamWrtRobot[2][0] = 0;  RcamWrtRobot[2][1] = -cosH; RcamWrtRobot[2][2] = sinH;

        TcamWrtRobot.x = HEAD_CAM_POSITION[0]*cosH - HEAD_CAM_POSITION[2]*sinH + NECK_JOINT_POSITION[0];
        TcamWrtRobot.y = 0;
        TcamWrtRobot.z = HEAD_CAM_POSITION[2]*cosH + HEAD_CAM_POSITION[0]*sinH + NECK_JOINT_POSITION[2];

        return RESULT_OK;
      }

      Result GetWithRespectToRobot(const Embedded::Point3<f32>& pointWrtCamera,
        Embedded::Point3<f32>&       pointWrtRobot)
      {
        Point3<f32> TcamWrtRobot;

        Result lastResult;
        if((lastResult = GetCamPoseWrtRobot(RcamWrtRobot_, TcamWrtRobot)) != RESULT_OK) {
          return lastResult;
        }

        pointWrtRobot = RcamWrtRobot_*pointWrtCamera + TcamWrtRobot;

        return RESULT_OK;
      }

      Result GetWithRespectToRobot(const Embedded::Array<f32>&  rotationWrtCamera,
        const Embedded::Point3<f32>& translationWrtCamera,
        Embedded::Array<f32>&        rotationWrtRobot,
        Embedded::Point3<f32>&       translationWrtRobot)
      {
        Point3<f32> TcamWrtRobot;

        Result lastResult;
        if((lastResult = GetCamPoseWrtRobot(RcamWrtRobot_, TcamWrtRobot)) != RESULT_OK) {
          return lastResult;
        }

        if((lastResult = Matrix::Multiply(RcamWrtRobot_, rotationWrtCamera, rotationWrtRobot)) != RESULT_OK) {
          return lastResult;
        }

        translationWrtRobot = RcamWrtRobot_*translationWrtCamera + TcamWrtRobot;

        return RESULT_OK;
      }

      Result GetVisionMarkerPose(const Embedded::VisionMarker& marker,
        const bool ignoreOrientation,
        Embedded::Array<f32>&  rotation,
        Embedded::Point3<f32>& translation)
      {
        Quadrilateral<f32> sortedQuad;
        if(ignoreOrientation) {
          sortedQuad = marker.corners.ComputeClockwiseCorners<f32>();
        } else {
          sortedQuad = marker.corners;
        }

        return P3P::computePose(sortedQuad,
          canonicalMarker3d_[0], canonicalMarker3d_[1],
          canonicalMarker3d_[2], canonicalMarker3d_[3],
          headCamInfo_->focalLength_x, headCamInfo_->focalLength_y,
          headCamInfo_->center_x, headCamInfo_->center_y,
          rotation, translation);
      } // GetVisionMarkerPose()

#ifdef SEND_IMAGE_ONLY
      // In SEND_IMAGE_ONLY mode, just create a special version of update

      Result Update(const Messages::RobotState robotState)
      {
        // This should be called from elsewhere first, but calling it again won't hurt
        Init();

        VisionMemory::ResetBuffers();

        frameNumber++;

        const s32 captureHeight = CameraModeInfo[captureResolution_].height;
        const s32 captureWidth  = CameraModeInfo[captureResolution_].width;

        Array<u8> grayscaleImage(captureHeight, captureWidth,
          VisionMemory::onchipScratch_, Flags::Buffer(false,false,false));

        HAL::CameraGetFrame(reinterpret_cast<u8*>(grayscaleImage.get_rawDataPointer()),
          captureResolution_, false);

        BeginBenchmark("VisionSystem_CameraImagingPipeline");

        if(vignettingCorrection == VignettingCorrection_Software) {
          BeginBenchmark("VisionSystem_CameraImagingPipeline_Vignetting");

          MemoryStack onchipScratch_local = VisionMemory::onchipScratch_;
          FixedLengthList<f32> polynomialParameters(5, onchipScratch_local, Flags::Buffer(false, false, true));

          for(s32 i=0; i<5; i++)
            polynomialParameters[i] = vignettingCorrectionParameters[i];

          CorrectVignetting(grayscaleImage, polynomialParameters);

          EndBenchmark("VisionSystem_CameraImagingPipeline_Vignetting");
        } // if(vignettingCorrection == VignettingCorrection_Software)

        if(autoExposure_enabled && (frameNumber % autoExposure_adjustEveryNFrames) == 0) {
          BeginBenchmark("VisionSystem_CameraImagingPipeline_AutoExposure");

          ComputeBestCameraParameters(
            grayscaleImage,
            Rectangle<s32>(0, grayscaleImage.get_size(1)-1, 0, grayscaleImage.get_size(0)-1),
            autoExposure_integerCountsIncrement,
            autoExposure_percentileToSaturate,
            autoExposure_minExposureTime, autoExposure_maxExposureTime,
            exposureTime,
            VisionMemory::ccmScratch_);

          EndBenchmark("VisionSystem_CameraImagingPipeline_AutoExposure");
        }

        //if(frameNumber % 10 == 0) {
        //if(vignettingCorrection == VignettingCorrection_Off)
        //vignettingCorrection = VignettingCorrection_CameraHardware;
        //else if(vignettingCorrection == VignettingCorrection_CameraHardware)
        //vignettingCorrection = VignettingCorrection_Software;
        //else if(vignettingCorrection == VignettingCorrection_Software)
        //vignettingCorrection = VignettingCorrection_Off;
        //}

        //if(vignettingCorrection == VignettingCorrection_Software) {
        //  for(s32 y=0; y<3; y++) {
        //    for(s32 x=0; x<3; x++) {
        //      grayscaleImage[y][x] = 0;
        //    }
        //  }
        //}

        //if(frameNumber % 25 == 0) {
        //  if(vignettingCorrection == VignettingCorrection_Off)
        //    vignettingCorrection = VignettingCorrection_Software;
        //  else if(vignettingCorrection == VignettingCorrection_Software)
        //    vignettingCorrection = VignettingCorrection_Off;
        //}

        HAL::CameraSetParameters(exposureTime, vignettingCorrection == VignettingCorrection_CameraHardware);

        EndBenchmark("VisionSystem_CameraImagingPipeline");

#ifdef SEND_BINARY_IMAGE_ONLY
        DebugStream::SendBinaryImage(grayscaleImage, "Binary Robot Image", tracker_, trackerParameters_, VisionMemory::ccmScratch_, VisionMemory::onchipScratch_, VisionMemory::offchipScratch_);
        HAL::MicroWait(250000);
#else
        DebugStream::SendImage(grayscaleImage, exposureTime, "Robot Image", VisionMemory::offchipScratch_);
        HAL::MicroWait(166666); // 6fps
        //HAL::MicroWait(140000); //7fps
        //HAL::MicroWait(125000); //8fps
#endif

        return RESULT_OK;
      } // Update() [SEND_IMAGE_ONLY]

#elif defined(RUN_SIMPLE_FACE_DETECTION_TEST) // #ifdef SEND_IMAGE_ONLY

      Result Update(const Messages::RobotState robotState)
      {
        // This should be called from elsewhere first, but calling it again won't hurt
        Init();

        VisionMemory::ResetBuffers();

        frameNumber++;

        const s32 captureHeight = CameraModeInfo[captureResolution_].height;
        const s32 captureWidth  = CameraModeInfo[captureResolution_].width;

        Array<u8> grayscaleImage(captureHeight, captureWidth,
          VisionMemory::offchipScratch_, Flags::Buffer(false,false,false));

        HAL::CameraGetFrame(reinterpret_cast<u8*>(grayscaleImage.get_rawDataPointer()),
          captureResolution_, false);

        BeginBenchmark("VisionSystem_CameraImagingPipeline");

        if(vignettingCorrection == VignettingCorrection_Software) {
          BeginBenchmark("VisionSystem_CameraImagingPipeline_Vignetting");

          MemoryStack onchipScratch_local = VisionMemory::onchipScratch_;
          FixedLengthList<f32> polynomialParameters(5, onchipScratch_local, Flags::Buffer(false, false, true));

          for(s32 i=0; i<5; i++)
            polynomialParameters[i] = vignettingCorrectionParameters[i];

          CorrectVignetting(grayscaleImage, polynomialParameters);

          EndBenchmark("VisionSystem_CameraImagingPipeline_Vignetting");
        } // if(vignettingCorrection == VignettingCorrection_Software)

        if(autoExposure_enabled && (frameNumber % autoExposure_adjustEveryNFrames) == 0) {
          BeginBenchmark("VisionSystem_CameraImagingPipeline_AutoExposure");

          ComputeBestCameraParameters(
            grayscaleImage,
            Rectangle<s32>(0, grayscaleImage.get_size(1)-1, 0, grayscaleImage.get_size(0)-1),
            autoExposure_integerCountsIncrement,
            autoExposure_percentileToSaturate,
            autoExposure_minExposureTime, autoExposure_maxExposureTime,
            exposureTime,
            VisionMemory::ccmScratch_);

          EndBenchmark("VisionSystem_CameraImagingPipeline_AutoExposure");
        }

        HAL::CameraSetParameters(exposureTime, vignettingCorrection == VignettingCorrection_CameraHardware);

        EndBenchmark("VisionSystem_CameraImagingPipeline");

        const s32 faceDetectionHeight = CameraModeInfo[faceDetectionResolution_].height;
        const s32 faceDetectionWidth  = CameraModeInfo[faceDetectionResolution_].width;

        const double scaleFactor = 1.1;
        const int minNeighbors = 2;
        const s32 minHeight = 30;
        const s32 minWidth = 30;
        const s32 maxHeight = faceDetectionHeight;
        const s32 maxWidth = faceDetectionWidth;
        const s32 MAX_CANDIDATES = 5000;

        Array<u8> smallImage(
          faceDetectionHeight, faceDetectionWidth,
          VisionMemory::onchipScratch_, Flags::Buffer(false,false,false));

        DownsampleHelper(grayscaleImage, smallImage, VisionMemory::ccmScratch_);

        const FixedLengthList<Classifier::CascadeClassifier::Stage> &stages = FixedLengthList<Classifier::CascadeClassifier::Stage>(lbpcascade_frontalface_stages_length, const_cast<Classifier::CascadeClassifier::Stage*>(&lbpcascade_frontalface_stages_data[0]), lbpcascade_frontalface_stages_length*sizeof(Classifier::CascadeClassifier::Stage) + MEMORY_ALIGNMENT_RAW, Flags::Buffer(false,false,true));
        const FixedLengthList<Classifier::CascadeClassifier::DTree> &classifiers = FixedLengthList<Classifier::CascadeClassifier::DTree>(lbpcascade_frontalface_classifiers_length, const_cast<Classifier::CascadeClassifier::DTree*>(&lbpcascade_frontalface_classifiers_data[0]), lbpcascade_frontalface_classifiers_length*sizeof(Classifier::CascadeClassifier::DTree) + MEMORY_ALIGNMENT_RAW, Flags::Buffer(false,false,true));
        const FixedLengthList<Classifier::CascadeClassifier::DTreeNode> &nodes =  FixedLengthList<Classifier::CascadeClassifier::DTreeNode>(lbpcascade_frontalface_nodes_length, const_cast<Classifier::CascadeClassifier::DTreeNode*>(&lbpcascade_frontalface_nodes_data[0]), lbpcascade_frontalface_nodes_length*sizeof(Classifier::CascadeClassifier::DTreeNode) + MEMORY_ALIGNMENT_RAW, Flags::Buffer(false,false,true));;
        const FixedLengthList<f32> &leaves = FixedLengthList<f32>(lbpcascade_frontalface_leaves_length, const_cast<f32*>(&lbpcascade_frontalface_leaves_data[0]), lbpcascade_frontalface_leaves_length*sizeof(f32) + MEMORY_ALIGNMENT_RAW, Flags::Buffer(false,false,true));
        const FixedLengthList<s32> &subsets = FixedLengthList<s32>(lbpcascade_frontalface_subsets_length, const_cast<s32*>(&lbpcascade_frontalface_subsets_data[0]), lbpcascade_frontalface_subsets_length*sizeof(s32) + MEMORY_ALIGNMENT_RAW, Flags::Buffer(false,false,true));
        const FixedLengthList<Rectangle<s32> > &featureRectangles = FixedLengthList<Rectangle<s32> >(lbpcascade_frontalface_featureRectangles_length, const_cast<Rectangle<s32>*>(reinterpret_cast<const Rectangle<s32>*>(&lbpcascade_frontalface_featureRectangles_data[0])), lbpcascade_frontalface_featureRectangles_length*sizeof(Rectangle<s32>) + MEMORY_ALIGNMENT_RAW, Flags::Buffer(false,false,true));

        Classifier::CascadeClassifier_LBP cc(
          lbpcascade_frontalface_isStumpBased,
          lbpcascade_frontalface_stageType,
          lbpcascade_frontalface_featureType,
          lbpcascade_frontalface_ncategories,
          lbpcascade_frontalface_origWinHeight,
          lbpcascade_frontalface_origWinWidth,
          stages,
          classifiers,
          nodes,
          leaves,
          subsets,
          featureRectangles,
          VisionMemory::ccmScratch_);

        FixedLengthList<Rectangle<s32> > detectedFaces(MAX_CANDIDATES, VisionMemory::offchipScratch_);

        const Result result = cc.DetectMultiScale(
          smallImage,
          static_cast<f32>(scaleFactor),
          minNeighbors,
          minHeight, minWidth,
          maxHeight, maxWidth,
          detectedFaces,
          VisionMemory::onchipScratch_,
          VisionMemory::offchipScratch_);

        DebugStream::SendFaceDetections(
          grayscaleImage,
          detectedFaces,
          smallImage.get_size(1),
          VisionMemory::ccmScratch_,
          VisionMemory::onchipScratch_,
          VisionMemory::offchipScratch_);

        return RESULT_OK;
      } // Update() [SEND_IMAGE_ONLY]

#else // #elseif RUN_SIMPLE_FACE_DETECTION_TEST

      // This is the regular Update() call
      Result Update(const Messages::RobotState robotState)
      {
        Result lastResult = RESULT_OK;

        // This should be called from elsewhere first, but calling it again won't hurt
        Init();

        frameNumber++;

        // no-op on real hardware
        if(!Simulator::IsFrameReady()) {
          return RESULT_OK;
        }

        UpdateRobotState(robotState);

        const TimeStamp_t imageTimeStamp = HAL::GetTimeStamp();

        if(mode_ == VISION_MODE_IDLE) {
          // Nothing to do!
        }
        else if(mode_ == VISION_MODE_LOOKING_FOR_MARKERS) {
          Simulator::SetDetectionReadyTime(); // no-op on real hardware

          VisionMemory::ResetBuffers();

          //MemoryStack offchipScratch_local(VisionMemory::offchipScratch_);

          const s32 captureHeight = CameraModeInfo[captureResolution_].height;
          const s32 captureWidth  = CameraModeInfo[captureResolution_].width;

          Array<u8> grayscaleImage(captureHeight, captureWidth,
            VisionMemory::offchipScratch_, Flags::Buffer(false,false,false));

          HAL::CameraGetFrame(reinterpret_cast<u8*>(grayscaleImage.get_rawDataPointer()),
            captureResolution_, false);

          BeginBenchmark("VisionSystem_CameraImagingPipeline");

          if(vignettingCorrection == VignettingCorrection_Software) {
            BeginBenchmark("VisionSystem_CameraImagingPipeline_Vignetting");

            MemoryStack onchipScratch_local = VisionMemory::onchipScratch_;
            FixedLengthList<f32> polynomialParameters(5, onchipScratch_local, Flags::Buffer(false, false, true));

            for(s32 i=0; i<5; i++)
              polynomialParameters[i] = vignettingCorrectionParameters[i];

            CorrectVignetting(grayscaleImage, polynomialParameters);

            EndBenchmark("VisionSystem_CameraImagingPipeline_Vignetting");
          } // if(vignettingCorrection == VignettingCorrection_Software)

          if(autoExposure_enabled && (frameNumber % autoExposure_adjustEveryNFrames) == 0) {
            BeginBenchmark("VisionSystem_CameraImagingPipeline_AutoExposure");

            ComputeBestCameraParameters(
              grayscaleImage,
              Rectangle<s32>(0, grayscaleImage.get_size(1)-1, 0, grayscaleImage.get_size(0)-1),
              autoExposure_integerCountsIncrement,
              autoExposure_percentileToSaturate,
              autoExposure_minExposureTime, autoExposure_maxExposureTime,
              exposureTime,
              VisionMemory::ccmScratch_);

            EndBenchmark("VisionSystem_CameraImagingPipeline_AutoExposure");
          }

          HAL::CameraSetParameters(exposureTime, vignettingCorrection == VignettingCorrection_CameraHardware);

          EndBenchmark("VisionSystem_CameraImagingPipeline");

          DownsampleAndSendImage(grayscaleImage);

          if((lastResult = LookForMarkers(grayscaleImage,
            detectionParameters_,
            VisionMemory::markers_,
            VisionMemory::ccmScratch_,
            VisionMemory::onchipScratch_,
            VisionMemory::offchipScratch_)) != RESULT_OK)
          {
            return lastResult;
          }

          const s32 numMarkers = VisionMemory::markers_.get_size();
          bool isTrackingMarkerFound = false;
          for(s32 i_marker = 0; i_marker < numMarkers; ++i_marker)
          {
            const VisionMarker& crntMarker = VisionMemory::markers_[i_marker];

            // Create a vision marker message and process it (which just queues it
            // in the mailbox to be picked up and sent out by main execution)
            {
              Messages::VisionMarker msg;
              msg.timestamp  = imageTimeStamp;
              msg.markerType = crntMarker.markerType;

              msg.x_imgLowerLeft = crntMarker.corners[Quadrilateral<f32>::BottomLeft].x;
              msg.y_imgLowerLeft = crntMarker.corners[Quadrilateral<f32>::BottomLeft].y;

              msg.x_imgUpperLeft = crntMarker.corners[Quadrilateral<f32>::TopLeft].x;
              msg.y_imgUpperLeft = crntMarker.corners[Quadrilateral<f32>::TopLeft].y;

              msg.x_imgUpperRight = crntMarker.corners[Quadrilateral<f32>::TopRight].x;
              msg.y_imgUpperRight = crntMarker.corners[Quadrilateral<f32>::TopRight].y;

              msg.x_imgLowerRight = crntMarker.corners[Quadrilateral<f32>::BottomRight].x;
              msg.y_imgLowerRight = crntMarker.corners[Quadrilateral<f32>::BottomRight].y;

              HAL::RadioSendMessage(GET_MESSAGE_ID(Messages::VisionMarker),&msg);
            }

            // Was the desired marker found? If so, start tracking it.
            if(markerToTrack_.IsSpecified() && !isTrackingMarkerFound &&
              markerToTrack_.Matches(crntMarker))
            {
              // We will start tracking the _first_ marker of the right type that
              // we see.
              // TODO: Something smarter to track the one closest to the image center or to the expected location provided by the basestation?
              isTrackingMarkerFound = true;

              // I'd rather only initialize trackingQuad_ if InitTemplate() succeeds, but
              // InitTemplate downsamples it for the time being, since we're still doing template
              // initialization at tracking resolution instead of the eventual goal of doing it at
              // full detection resolution.
              trackingQuad_ = crntMarker.corners;

              // NOTE: This will change grayscaleImage!
              // NOTE: This is currently off-chip for memory reasons, so it's slow!
              if((lastResult = BrightnessNormalizeImage(grayscaleImage, trackingQuad_,
                trackerParameters_.normalizationFilterWidthFraction,
                VisionMemory::offchipScratch_)) != RESULT_OK)
              {
                return lastResult;
              }

              if((lastResult = InitTemplate(grayscaleImage,
                trackingQuad_,
                trackerParameters_,
                tracker_,
                VisionMemory::ccmScratch_,
                VisionMemory::onchipScratch_, //< NOTE: onchip is a reference
                VisionMemory::offchipScratch_)) != RESULT_OK)
              {
                return lastResult;
              }

              // Template initialization succeeded, switch to tracking mode:
              // TODO: Log or issue message?
              mode_ = VISION_MODE_TRACKING;
            } // if(isTrackingMarkerSpecified && !isTrackingMarkerFound && markerType == markerToTrack)
          } // for(each marker)
        } else if(mode_ == VISION_MODE_TRACKING) {
          Simulator::SetTrackingReadyTime(); // no-op on real hardware

          //
          // Capture image for tracking
          //

          MemoryStack offchipScratch_local(VisionMemory::offchipScratch_);
          MemoryStack onchipScratch_local(VisionMemory::onchipScratch_);

          const s32 captureHeight = CameraModeInfo[captureResolution_].height;
          const s32 captureWidth  = CameraModeInfo[captureResolution_].width;

          Array<u8> grayscaleImage(captureHeight, captureWidth,
            onchipScratch_local, Flags::Buffer(false,false,false));

          HAL::CameraGetFrame(reinterpret_cast<u8*>(grayscaleImage.get_rawDataPointer()),
            captureResolution_, false);

          BeginBenchmark("VisionSystem_CameraImagingPipeline");

          if(vignettingCorrection == VignettingCorrection_Software) {
            BeginBenchmark("VisionSystem_CameraImagingPipeline_Vignetting");

            MemoryStack onchipScratch_local = VisionMemory::onchipScratch_;
            FixedLengthList<f32> polynomialParameters(5, onchipScratch_local, Flags::Buffer(false, false, true));

            for(s32 i=0; i<5; i++)
              polynomialParameters[i] = vignettingCorrectionParameters[i];

            CorrectVignetting(grayscaleImage, polynomialParameters);

            EndBenchmark("VisionSystem_CameraImagingPipeline_Vignetting");
          } // if(vignettingCorrection == VignettingCorrection_Software)

          // TODO: allow tracking to work with exposure changes
          /*if(autoExposure_enabled && (frameNumber % autoExposure_adjustEveryNFrames) == 0) {
          BeginBenchmark("VisionSystem_CameraImagingPipeline_AutoExposure");

          ComputeBestCameraParameters(
          grayscaleImage,
          Rectangle<s32>(0, grayscaleImage.get_size(1)-1, 0, grayscaleImage.get_size(0)-1),
          autoExposure_integerCountsIncrement,
          autoExposure_percentileToSaturate,
          autoExposure_minExposureTime, autoExposure_maxExposureTime,
          exposureTime,
          VisionMemory::ccmScratch_);

          EndBenchmark("VisionSystem_CameraImagingPipeline_AutoExposure");
          }*/

          EndBenchmark("VisionSystem_CameraImagingPipeline");

          HAL::CameraSetParameters(exposureTime, vignettingCorrection == VignettingCorrection_CameraHardware);

          DownsampleAndSendImage(grayscaleImage);
          
          // NOTE: This will change grayscaleImage!
          // NOTE: This is currently off-chip for memory reasons, so it's slow!
          if((lastResult = BrightnessNormalizeImage(grayscaleImage, trackingQuad_,
            trackerParameters_.normalizationFilterWidthFraction,
            VisionMemory::offchipScratch_)) != RESULT_OK)
          {
            return lastResult;
          }

          //
          // Tracker Prediction
          //
          // Adjust the tracker transformation by approximately how much we
          // think we've moved since the last tracking call.
          //

          if((lastResult =TrackerPredictionUpdate(grayscaleImage, onchipScratch_local)) != RESULT_OK) {
            PRINT("VisionSystem::Update(): TrackTemplate() failed.\n");
            return lastResult;
          }

          //
          // Update the tracker transformation using this image
          //

          // Set by TrackTemplate() call
          bool converged = false;

          if((lastResult = TrackTemplate(grayscaleImage,
            trackingQuad_,
            trackerParameters_,
            tracker_,
            converged,
            VisionMemory::ccmScratch_,
            onchipScratch_local,
            offchipScratch_local)) != RESULT_OK) {
              PRINT("VisionSystem::Update(): TrackTemplate() failed.\n");
              return lastResult;
          }

          //
          // Create docking error signal from tracker
          //

          Messages::DockingErrorSignal dockErrMsg;
          dockErrMsg.timestamp = imageTimeStamp;
          dockErrMsg.didTrackingSucceed = static_cast<u8>(converged);

          if(converged)
          {
            Quadrilateral<f32> currentQuad = GetTrackerQuad(VisionMemory::onchipScratch_);
            FillDockErrMsg(currentQuad, dockErrMsg, VisionMemory::onchipScratch_);
            
            // Send tracker quad if image streaming
            if (imageSendMode_ == ISM_STREAM) {
              f32 scale = 1.f;
              for (u8 s = (u8)Vision::CAMERA_RES_QVGA; s<(u8)nextSendImageResolution_; ++s) {
                scale *= 0.5f;
              }
              
              Messages::TrackerQuad m;
              m.topLeft_x = static_cast<u16>(currentQuad[Quadrilateral<f32>::TopLeft].x * scale);
              m.topLeft_y = static_cast<u16>(currentQuad[Quadrilateral<f32>::TopLeft].y * scale);
              m.topRight_x = static_cast<u16>(currentQuad[Quadrilateral<f32>::TopRight].x * scale);
              m.topRight_y = static_cast<u16>(currentQuad[Quadrilateral<f32>::TopRight].y * scale);
              m.bottomRight_x = static_cast<u16>(currentQuad[Quadrilateral<f32>::BottomRight].x * scale);
              m.bottomRight_y = static_cast<u16>(currentQuad[Quadrilateral<f32>::BottomRight].y * scale);
              m.bottomLeft_x = static_cast<u16>(currentQuad[Quadrilateral<f32>::BottomLeft].x * scale);
              m.bottomLeft_y = static_cast<u16>(currentQuad[Quadrilateral<f32>::BottomLeft].y * scale);
              
              HAL::RadioSendMessage(GET_MESSAGE_ID(Messages::TrackerQuad), &m);
            }

            // Reset the failure counter
            numTrackFailures_ = 0;
          }
          else {
            numTrackFailures_ += 1;

            if(numTrackFailures_ == MAX_TRACKING_FAILURES) {
              // This resets docking, puttings us back in VISION_MODE_LOOKING_FOR_MARKERS mode
              SetMarkerToTrack(markerToTrack_.type,
                markerToTrack_.width_mm,
                markerToTrack_.imageCenter,
                markerToTrack_.imageSearchRadius);
            }
          }

          Messages::ProcessDockingErrorSignalMessage(dockErrMsg);
        } else {
          PRINT("VisionSystem::Update(): reached default case in switch statement.");
          return RESULT_FAIL;
        } // if(converged)

        return RESULT_OK;
      } // Update() [Real]

#endif // #ifdef SEND_IMAGE_ONLY
    } // namespace VisionSystem
  } // namespace Cozmo
} // namespace Anki

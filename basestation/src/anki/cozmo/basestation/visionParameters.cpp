/**
 * File: visionParameters.cpp [Basestation]
 *
 * Author: Andrew Stein
 * Date:   3/28/2014
 *
 * Description: High-level vision system parameter definitions, including the
 *              type of tracker to use.
 *
 * Copyright: Anki, Inc. 2014
 **/

#include "anki/common/basestation/math/point_impl.h"
#include "anki/common/robot/utilities.h"

#include "anki/vision/robot/fiducialDetection.h" // just for FIDUCIAL_SQUARE_WIDTH_FRACTION

#include "anki/cozmo/basestation/visionParameters.h"

namespace Anki {
  namespace Cozmo {
      
      using namespace Embedded;
      
      
#if 0
#pragma mark --- DetectFiducialMarkersParameters ---
#endif

      
      DetectFiducialMarkersParameters::DetectFiducialMarkersParameters()
      : isInitialized(false)
      {
      
      }
        
      void DetectFiducialMarkersParameters::Initialize(ImageResolution resolution)
      {
        detectionResolution = resolution;
        detectionWidth  = Vision::CameraResInfo[static_cast<size_t>(detectionResolution)].width;
        detectionHeight = Vision::CameraResInfo[static_cast<size_t>(detectionResolution)].height;

        markerAppearance = VisionMarkerAppearance::BLACK_ON_WHITE;
        
#ifdef SIMULATOR
        scaleImage_thresholdMultiplier = 32768; // 0.5*(2^16)=32768
  #error "SIMULATOR flag not supported in basestation"
#else
        scaleImage_thresholdMultiplier = static_cast<s32>(65536.f * 0.9f);
        // scaleImage_thresholdMultiplier = 65536; // 1.0*(2^16)=65536
        //scaleImage_thresholdMultiplier = 49152; // 0.75*(2^16)=49152
#endif
        scaleImage_numPyramidLevels = 3;
        
        component1d_minComponentWidth = 0;
        component1d_maxSkipDistance = 0;
        
        minSideLength = 0.03f*static_cast<f32>(MAX(detectionWidth,detectionHeight));
        maxSideLength = 0.97f*static_cast<f32>(MIN(detectionWidth,detectionHeight));
        
        component_minimumNumPixels = Round<s32>(minSideLength*minSideLength - (0.8f*minSideLength)*(0.8f*minSideLength));
        component_maximumNumPixels = Round<s32>(maxSideLength*maxSideLength - (0.8f*maxSideLength)*(0.8f*maxSideLength));
        component_sparseMultiplyThreshold = 1000 << 5;
        component_solidMultiplyThreshold = 2 << 5;
        
        component_minHollowRatio = 1.0f;
        
        // Ratio of 4th to 5th biggest Laplacian peak must be greater than this
        // for a quad to be extracted from a connected component
        minLaplacianPeakRatio = 5;
        
        maxExtractedQuads = 1000/2;
        quads_minQuadArea = 100/4;
        quads_quadSymmetryThreshold = 512; // ANS: corresponds to 2.0, loosened from 384 (1.5), for large mat markers at extreme perspective distortion
        quads_minDistanceFromImageEdge = 2;
        
        decode_minContrastRatio = 1.25;
        
        maxConnectedComponentSegments = 39000; // 322*240/2 = 38640
        
        // Maximum number of refinement iterations (i.e. if convergence is not
        // detected in the meantime according to minCornerChange parameter below)
        quadRefinementIterations = 25;
        
        // TODO: Could this be fewer samples?
        numRefinementSamples = 100;
        
        // If quad refinment moves any corner by more than this (in pixels), the
        // original quad/homography are restored.
        quadRefinementMaxCornerChange = 5.f;
        
        // If quad refinement moves all corners by less than this (in pixels),
        // the refinment is considered converged and stops immediately
        quadRefinementMinCornerChange = 0.005f;
        
        // Return unknown/unverified markers (e.g. for display)
        keepUnverifiedMarkers = false;
        
        // Thickness of the fiducial rectangle, relative to its width/height
        fiducialThicknessFraction.x() = fiducialThicknessFraction.y() = 0.1f;
        
        // Radius of rounds as a fraction of the height/width of the fiducial rectangle
        roundedCornersFraction.x() = roundedCornersFraction.y() = 0.15f;
        
        isInitialized = true;
      } // DetectFiducialMarkersParameters::Initialize()

    
    
    
#if 0
#pragma mark --- TrackerParameters ---
#endif
    
      const f32 TrackerParameters::MIN_TRACKER_DISTANCE = 10.f;
      const f32 TrackerParameters::MAX_TRACKER_DISTANCE = 200.f;
      const f32 TrackerParameters::MAX_BLOCK_DOCKING_ANGLE = DEG_TO_RAD(45);
      const f32 TrackerParameters::MAX_DOCKING_FOV_ANGLE = DEG_TO_RAD(60);
      
      TrackerParameters::TrackerParameters()
      : isInitialized(false)
      {
        
      }
      
      void TrackerParameters::Initialize(ImageResolution resolution,
                                         const Point2f& fiducialThicknessFraction,
                                         const Point2f& roundedCornersFractionArg)
      {
        // This is size of the box filter used to locally normalize the image
        // as a fraction of the size of the current tracking quad.
        // Set to zero to disable normalization.
        // Set to a negative value to simply use (much faster) mean normalization,
        // which just makes the mean of the pixels within the tracking quad equal 128.
        normalizationFilterWidthFraction = -1.f; //0.5f;
        
        // LK tracker parameter initialization
        trackingResolution   = resolution;
        numPyramidLevels     = 3; // TODO: Compute from resolution to get down to a given size?
        
        trackingImageWidth   = Vision::CameraResInfo[static_cast<size_t>(trackingResolution)].width;
        trackingImageHeight  = Vision::CameraResInfo[static_cast<size_t>(trackingResolution)].height;
        
        maxIterations             = 50;
        verify_maxPixelDifference = 30;
        useWeights                = true;
       
        convergenceTolerance_angle    = DEG_TO_RAD(0.05f);
        convergenceTolerance_distance = 0.05f; // mm
        
        numSamplingRegions            = 5;
        
        // Split total samples between fiducial and interior
        numInteriorSamples            = 500;
        numFiducialEdgeSamples        = 500;
        
        roundedCornersFraction = roundedCornersFractionArg;
        
        if(numFiducialEdgeSamples > 0) {
          scaleTemplateRegionPercent    = 1.f - 0.5f*(fiducialThicknessFraction.x() +
                                                      fiducialThicknessFraction.y());
        } else {
          scaleTemplateRegionPercent = 1.1f;
        }
        
        successTolerance_angle        = DEG_TO_RAD(30);
        successTolerance_distance     = 20.f;
        successTolerance_matchingPixelsFraction = 0.75f;
        
        isInitialized = true;
      }
      
      
#if 0
#pragma mark --- Face Detection Parameters ----
#endif
      
      FaceDetectionParameters::FaceDetectionParameters()
      : isInitialized(false)
      {
        
      } // FaceDetectionParameters()
      
      void FaceDetectionParameters::Initialize(ImageResolution resolution)
      {
        detectionResolution = resolution;
        
        faceDetectionHeight = Vision::CameraResInfo[static_cast<size_t>(detectionResolution)].height;
        faceDetectionWidth  = Vision::CameraResInfo[static_cast<size_t>(detectionResolution)].width;

        scaleFactor    = 1.1;
        minNeighbors   = 2;
        minHeight      = 30;
        minWidth       = 30;
        maxHeight      = faceDetectionHeight;
        maxWidth       = faceDetectionWidth;
        MAX_CANDIDATES = 5000;
                
        isInitialized = true;
        
      } // FaceDetectionParameters::Initialize()
      

  } // namespace Cozmo
} // namespace Anki

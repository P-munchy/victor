/**
File: detectFiducialMarkers.cpp
Author: Peter Barnum
Created: 2013

Copyright Anki, Inc. 2013
For internal use only. No part of this code may be used without a signed non-disclosure agreement with Anki, inc.
**/

#include "anki/common/robot/config.h"
#include "anki/common/robot/benchmarking.h"

#include "anki/vision/robot/fiducialMarkers.h"
#include "anki/vision/robot/fiducialDetection.h"
#include "anki/vision/robot/draw_vision.h"
#include "anki/vision/robot/transformations.h"

#include "anki/common/robot/matlabInterface.h"

//#define SHOW_DRAWN_COMPONENTS

namespace Anki
{
  namespace Embedded
  {
    Result DetectFiducialMarkers(
      const Array<u8> &image,
      FixedLengthList<VisionMarker> &markers,
      FixedLengthList<Array<f32> > &homographies,
      const s32 scaleImage_numPyramidLevels, const s32 scaleImage_thresholdMultiplier,
      const s16 component1d_minComponentWidth, const s16 component1d_maxSkipDistance,
      const s32 component_minimumNumPixels, const s32 component_maximumNumPixels,
      const s32 component_sparseMultiplyThreshold, const s32 component_solidMultiplyThreshold,
      const f32 component_minHollowRatio,
      const s32 quads_minQuadArea, const s32 quads_quadSymmetryThreshold, const s32 quads_minDistanceFromImageEdge,
      const f32 decode_minContrastRatio,
      const s32 maxConnectedComponentSegments, //< If this number is above 2^16-1, then it will use 25% more memory per component
      const s32 maxExtractedQuads,
      const s32 refine_quadRefinementIterations,
      const s32 refine_numRefinementSamples,
      const f32 refine_quadRefinementMaxCornerChange,
      const f32 refine_quadRefinementMinCornerChange,
      const bool returnInvalidMarkers,
      MemoryStack scratchCcm,
      MemoryStack scratchOnchip,
      MemoryStack scratchOffChip)
    {
      const bool useIntegralImageFiltering = true;
      return DetectFiducialMarkers(image, markers, homographies, useIntegralImageFiltering, scaleImage_numPyramidLevels, scaleImage_thresholdMultiplier, component1d_minComponentWidth, component1d_maxSkipDistance, component_minimumNumPixels, component_maximumNumPixels, component_sparseMultiplyThreshold, component_solidMultiplyThreshold, component_minHollowRatio, quads_minQuadArea, quads_quadSymmetryThreshold, quads_minDistanceFromImageEdge, decode_minContrastRatio, maxConnectedComponentSegments, maxExtractedQuads, refine_quadRefinementIterations, refine_numRefinementSamples, refine_quadRefinementMaxCornerChange, refine_quadRefinementMinCornerChange, returnInvalidMarkers, scratchCcm, scratchOnchip, scratchOffChip);
    }

    Result DetectFiducialMarkers(
      const Array<u8> &image,
      FixedLengthList<VisionMarker> &markers,
      FixedLengthList<Array<f32> > &homographies,
      const bool useIntegralImageFiltering,
      const s32 scaleImage_numPyramidLevels, const s32 scaleImage_thresholdMultiplier,
      const s16 component1d_minComponentWidth, const s16 component1d_maxSkipDistance,
      const s32 component_minimumNumPixels, const s32 component_maximumNumPixels,
      const s32 component_sparseMultiplyThreshold, const s32 component_solidMultiplyThreshold,
      const f32 component_minHollowRatio,
      const s32 quads_minQuadArea, const s32 quads_quadSymmetryThreshold, const s32 quads_minDistanceFromImageEdge,
      const f32 decode_minContrastRatio,
      const s32 maxConnectedComponentSegments,
      const s32 maxExtractedQuads,
      const s32 refine_quadRefinementIterations,
      const s32 refine_numRefinementSamples,
      const f32 refine_quadRefinementMaxCornerChange,
      const f32 refine_quadRefinementMinCornerChange,
      const bool returnInvalidMarkers,
      MemoryStack scratchCcm,
      MemoryStack scratchOnchip,
      MemoryStack scratchOffChip)
    {
      const f32 maxProjectiveTermValue = 8.0f;

      Result lastResult;

      BeginBenchmark("DetectFiducialMarkers");

      const s32 imageHeight = image.get_size(0);
      const s32 imageWidth = image.get_size(1);

      AnkiConditionalErrorAndReturnValue(image.IsValid() && markers.IsValid() && homographies.IsValid() && scratchOffChip.IsValid() && scratchOnchip.IsValid() && scratchCcm.IsValid(),
        RESULT_FAIL_INVALID_OBJECT, "DetectFiducialMarkers", "Some input is invalid");

      // On the robot, we don't have enough memory for resolutions over QVGA
      if(scratchOffChip.get_totalBytes() < 1000000 && scratchOnchip.get_totalBytes() < 1000000 && scratchCcm.get_totalBytes() < 1000000) {
        AnkiConditionalErrorAndReturnValue(imageHeight <= 240 && imageWidth <= 320,
          RESULT_FAIL_INVALID_SIZE, "DetectFiducialMarkers", "The image is too large to test");

        AnkiConditionalErrorAndReturnValue(scaleImage_numPyramidLevels <= 3,
          RESULT_FAIL_INVALID_SIZE, "DetectFiducialMarkers", "Only 3 pyramid levels are supported");
      }

      BeginBenchmark("ExtractComponentsViaCharacteristicScale");

      ConnectedComponents extractedComponents;
      if(maxConnectedComponentSegments <= u16_MAX) {
        extractedComponents = ConnectedComponents(maxConnectedComponentSegments, imageWidth, true, scratchOffChip);
      } else {
        extractedComponents = ConnectedComponents(maxConnectedComponentSegments, imageWidth, false, scratchOffChip);
      }

      AnkiConditionalErrorAndReturnValue(extractedComponents.IsValid(),
        RESULT_FAIL_OUT_OF_MEMORY, "DetectFiducialMarkers", "extractedComponents could not be allocated");

      if(useIntegralImageFiltering) {
        FixedLengthList<s32> filterHalfWidths(scaleImage_numPyramidLevels+2, scratchOnchip, Flags::Buffer(false, false, true));

        AnkiConditionalErrorAndReturnValue(filterHalfWidths.IsValid(),
          RESULT_FAIL_OUT_OF_MEMORY, "DetectFiducialMarkers", "filterHalfWidths could not be allocated");

        for(s32 i=0; i<(scaleImage_numPyramidLevels+2); i++) {
          filterHalfWidths[i] = 1 << (i);
        }

        //const s32 halfWidthData[] = {1,2,3,4,6,8,12,16};
        //for(s32 i=0; i<8; i++) {
        //  filterHalfWidths[i] = halfWidthData[i];
        //}

        // 1. Compute the Scale image
        // 2. Binarize the Scale image
        // 3. Compute connected components from the binary image
        if((lastResult = ExtractComponentsViaCharacteristicScale(
          image,
          filterHalfWidths, scaleImage_thresholdMultiplier,
          component1d_minComponentWidth, component1d_maxSkipDistance,
          extractedComponents,
          scratchCcm, scratchOnchip, scratchOffChip)) != RESULT_OK)
        {
          /* // DEBUG: drop a display of extracted components into matlab
          Embedded::Matlab matlab(false);
          matlab.PutArray(image, "image");
          Array<u8> empty(image.get_size(0), image.get_size(1), scratchOnchip);
          Embedded::DrawComponents<u8>(empty, extractedComponents, 64, 255);
          matlab.PutArray(empty, "empty");
          matlab.EvalStringEcho("desktop; keyboard");
          */
          return lastResult;
        }
      } else { // if(useIntegralImageFiltering)
        if((lastResult = ExtractComponentsViaCharacteristicScale_binomial(
          image,
          scaleImage_numPyramidLevels, scaleImage_thresholdMultiplier,
          component1d_minComponentWidth, component1d_maxSkipDistance,
          extractedComponents,
          scratchCcm, scratchOnchip, scratchOffChip)) != RESULT_OK)
        {
          /* // DEBUG: drop a display of extracted components into matlab
          Embedded::Matlab matlab(false);
          matlab.PutArray(image, "image");
          Array<u8> empty(image.get_size(0), image.get_size(1), scratchOnchip);
          Embedded::DrawComponents<u8>(empty, extractedComponents, 64, 255);
          matlab.PutArray(empty, "empty");
          matlab.EvalStringEcho("desktop; keyboard");
          */
          return lastResult;
        }
      } // if(useIntegralImageFiltering) ... else

#ifdef SHOW_DRAWN_COMPONENTS
      {
        const s32 bigScratchSize = 1024 + image.get_size(0) * RoundUp<s32>(image.get_size(1), MEMORY_ALIGNMENT);
        MemoryStack bigScratch(malloc(bigScratchSize), bigScratchSize);
        Array<u8> empty(image.get_size(0), image.get_size(1), bigScratch);
        Embedded::DrawComponents<u8>(empty, extractedComponents, 64, 255);
        image.Show("image", false, false, true);
        empty.Show("components orig", false, false, true);
        free(bigScratch.get_buffer());
      }
#endif

      EndBenchmark("ExtractComponentsViaCharacteristicScale");

      { // 3b. Remove poor components
        BeginBenchmark("CompressConnectedComponentSegmentIds1");
        extractedComponents.CompressConnectedComponentSegmentIds(scratchOnchip);
        EndBenchmark("CompressConnectedComponentSegmentIds1");

        BeginBenchmark("InvalidateSmallOrLargeComponents");
        if((lastResult = extractedComponents.InvalidateSmallOrLargeComponents(component_minimumNumPixels, component_maximumNumPixels, scratchOnchip)) != RESULT_OK)
          return lastResult;
        EndBenchmark("InvalidateSmallOrLargeComponents");

        BeginBenchmark("CompressConnectedComponentSegmentIds2");
        extractedComponents.CompressConnectedComponentSegmentIds(scratchOnchip);
        EndBenchmark("CompressConnectedComponentSegmentIds2");

        BeginBenchmark("InvalidateSolidOrSparseComponents");
        if((lastResult = extractedComponents.InvalidateSolidOrSparseComponents(component_sparseMultiplyThreshold, component_solidMultiplyThreshold, scratchOnchip)) != RESULT_OK)
          return lastResult;
        EndBenchmark("InvalidateSolidOrSparseComponents");

        BeginBenchmark("CompressConnectedComponentSegmentIds3");
        extractedComponents.CompressConnectedComponentSegmentIds(scratchOnchip);
        EndBenchmark("CompressConnectedComponentSegmentIds3");

        BeginBenchmark("InvalidateFilledCenterComponents_hollowRows");
        if((lastResult = extractedComponents.InvalidateFilledCenterComponents_hollowRows(component_minHollowRatio, scratchOnchip)) != RESULT_OK)
          return lastResult;
        EndBenchmark("InvalidateFilledCenterComponents_hollowRows");

        BeginBenchmark("CompressConnectedComponentSegmentIds4");
        extractedComponents.CompressConnectedComponentSegmentIds(scratchOnchip);
        EndBenchmark("CompressConnectedComponentSegmentIds4");

        BeginBenchmark("SortConnectedComponentSegmentsById");
        extractedComponents.SortConnectedComponentSegmentsById(scratchOnchip);
        EndBenchmark("SortConnectedComponentSegmentsById");
      } // 3b. Remove poor components

#ifdef SHOW_DRAWN_COMPONENTS
      {
        //CoreTechPrint("Components\n");
        const s32 bigScratchSize = 1024 + image.get_size(0) * RoundUp<s32>(image.get_size(1), MEMORY_ALIGNMENT);
        MemoryStack bigScratch(malloc(bigScratchSize), bigScratchSize);
        Array<u8> empty(image.get_size(0), image.get_size(1), bigScratch);
        Embedded::DrawComponents<u8>(empty, extractedComponents, 64, 255);
        empty.Show("components good", true, false, true);
        free(bigScratch.get_buffer());
      }
#endif

      // 4. Compute candidate quadrilaterals from the connected components
      {
        BeginBenchmark("ComputeQuadrilateralsFromConnectedComponents");
        FixedLengthList<Quadrilateral<s16> > extractedQuads(maxExtractedQuads, scratchOnchip);

        if((lastResult = ComputeQuadrilateralsFromConnectedComponents(extractedComponents, quads_minQuadArea, quads_quadSymmetryThreshold, quads_minDistanceFromImageEdge, imageHeight, imageWidth, extractedQuads, scratchOnchip)) != RESULT_OK)
          return lastResult;

        markers.set_size(extractedQuads.get_size());

        EndBenchmark("ComputeQuadrilateralsFromConnectedComponents");

        // 4b. Compute a homography for each extracted quadrilateral
        BeginBenchmark("ComputeHomographyFromQuad");
        for(s32 iQuad=0; iQuad<extractedQuads.get_size(); iQuad++) {
          Array<f32> &currentHomography = homographies[iQuad];
          VisionMarker &currentMarker = markers[iQuad];

          bool numericalFailure;
          if((lastResult = Transformations::ComputeHomographyFromQuad(extractedQuads[iQuad], currentHomography, numericalFailure, scratchOnchip)) != RESULT_OK) {
            return lastResult;
          }

          markers[iQuad] = VisionMarker(extractedQuads[iQuad], VisionMarker::UNKNOWN);

          if(numericalFailure) {
            currentMarker.validity = VisionMarker::NUMERICAL_FAILURE;
          } else {
            if(currentHomography[2][0] > maxProjectiveTermValue || currentHomography[2][1] > maxProjectiveTermValue) {
              AnkiWarn("DetectFiducialMarkers", "Homography projective terms are unreasonably large");
              currentMarker.validity = VisionMarker::NUMERICAL_FAILURE;
            }
          }

          //currentHomography.Print("currentHomography");
        } // for(iQuad=0; iQuad<; iQuad++)
        EndBenchmark("ComputeHomographyFromQuad");
      }

      // 5. Decode fiducial markers from the candidate quadrilaterals

      BeginBenchmark("ExtractVisionMarker");

      // refinedHomography and meanGrayvalueThreshold are computed by currentMarker.RefineCorners(), then used by currentMarker.Extract()
      Array<f32> refinedHomography(3, 3, scratchOnchip);
      u8 meanGrayvalueThreshold;

      for(s32 iMarker=0; iMarker<markers.get_size(); iMarker++) {
        const Array<f32> &currentHomography = homographies[iMarker];
        VisionMarker &currentMarker = markers[iMarker];

        if(currentMarker.validity == VisionMarker::UNKNOWN) {
          // If refine_quadRefinementIterations > 0, then make this marker's corners more accurate
          if((lastResult = currentMarker.RefineCorners(
            image,
            currentHomography,
            decode_minContrastRatio,
            refine_quadRefinementIterations, refine_numRefinementSamples, refine_quadRefinementMaxCornerChange, refine_quadRefinementMinCornerChange,
            quads_minQuadArea, quads_quadSymmetryThreshold, quads_minDistanceFromImageEdge,
            refinedHomography, meanGrayvalueThreshold,
            scratchOnchip)) != RESULT_OK)
          {
            return lastResult;
          }

          if(currentMarker.validity == VisionMarker::LOW_CONTRAST) {
            currentMarker.markerType = Anki::Vision::MARKER_UNKNOWN;
          } else {
            if((lastResult = currentMarker.Extract(
              image,
              refinedHomography, meanGrayvalueThreshold,
              decode_minContrastRatio,
              scratchOnchip)) != RESULT_OK)
            {
              return lastResult;
            }
          }
        } // if(currentMarker.validity == VisionMarker::UNKNOWN)
      } // for(s32 iMarker=0; iMarker<markers.get_size(); iMarker++)

      // Remove invalid markers from the list
      if(!returnInvalidMarkers) {
        for(s32 iMarker=0; iMarker<markers.get_size(); iMarker++) {
          if(markers[iMarker].validity != VisionMarker::VALID) {
            for(s32 jQuad=iMarker; jQuad<markers.get_size(); jQuad++) {
              markers[jQuad] = markers[jQuad+1];
              homographies[jQuad].Set(homographies[jQuad+1]);
            }
            //extractedQuads.set_size(extractedQuads.get_size()-1);
            markers.set_size(markers.get_size()-1);
            homographies.set_size(homographies.get_size()-1);
            iMarker--;
          }
        }
      } // if(!returnInvalidMarkers)

      EndBenchmark("ExtractVisionMarker");

      EndBenchmark("DetectFiducialMarkers");

      return RESULT_OK;
    } // DetectFiducialMarkers()
  } // namespace Embedded
} // namespace Anki

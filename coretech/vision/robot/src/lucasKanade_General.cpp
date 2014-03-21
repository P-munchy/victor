/**
File: lucasKanade_General.cpp
Author: Peter Barnum
Created: 2014-03-18

Copyright Anki, Inc. 2014
For internal use only. No part of this code may be used without a signed non-disclosure agreement with Anki, inc.
**/

#include "anki/vision/robot/lucasKanade.h"
#include "anki/common/robot/matlabInterface.h"
#include "anki/common/robot/interpolate.h"
#include "anki/common/robot/arrayPatterns.h"
#include "anki/common/robot/find.h"
#include "anki/common/robot/benchmarking_c.h"
#include "anki/common/robot/draw.h"
#include "anki/common/robot/comparisons.h"
#include "anki/common/robot/errorHandling.h"

#include "anki/vision/robot/fiducialDetection.h"
#include "anki/vision/robot/imageProcessing.h"
#include "anki/vision/robot/transformations.h"

namespace Anki
{
  namespace Embedded
  {
    namespace TemplateTracker
    {
      f32 UpdatePreviousCorners(
        const Transformations::PlanarTransformation_f32 &transformation,
        FixedLengthList<Quadrilateral<f32> > &previousCorners,
        MemoryStack scratch)
      {
        const f32 baseImageHalfWidth = static_cast<f32>(BASE_IMAGE_WIDTH) / 2.0f;
        const f32 baseImageHalfHeight = static_cast<f32>(BASE_IMAGE_HEIGHT) / 2.0f;

        Quadrilateral<f32> in(
          Point<f32>(-baseImageHalfWidth,-baseImageHalfHeight),
          Point<f32>(baseImageHalfWidth,-baseImageHalfHeight),
          Point<f32>(baseImageHalfWidth,baseImageHalfHeight),
          Point<f32>(-baseImageHalfWidth,baseImageHalfHeight));

        Quadrilateral<f32> newCorners = transformation.TransformQuadrilateral(in, scratch, 1.0f);

        //const f32 change = sqrtf(Matrix::Mean<f32,f32>(tmp1));
        f32 minChange = 1e10f;
        for(s32 iPrevious=0; iPrevious<NUM_PREVIOUS_QUADS_TO_COMPARE; iPrevious++) {
          f32 change = 0.0f;
          for(s32 i=0; i<4; i++) {
            const f32 dx = previousCorners[iPrevious][i].x - newCorners[i].x;
            const f32 dy = previousCorners[iPrevious][i].y - newCorners[i].y;
            change += sqrtf(dx*dx + dy*dy);
          }
          change /= 4;

          minChange = MIN(minChange, change);
        }

        for(s32 iPrevious=0; iPrevious<(NUM_PREVIOUS_QUADS_TO_COMPARE-1); iPrevious++) {
          previousCorners[iPrevious] = previousCorners[iPrevious+1];
        }
        previousCorners[NUM_PREVIOUS_QUADS_TO_COMPARE-1] = newCorners;

        return minChange;
      } // f32 ComputeMinChange()

      LucasKanadeTracker_Fast::LucasKanadeTracker_Fast(const Transformations::TransformType maxSupportedTransformType)
        : maxSupportedTransformType(maxSupportedTransformType), isValid(false)
      {
      }

      LucasKanadeTracker_Fast::LucasKanadeTracker_Fast(
        const Transformations::TransformType maxSupportedTransformType,
        const Array<u8> &templateImage,
        const Quadrilateral<f32> &templateQuad,
        const f32 scaleTemplateRegionPercent,
        const s32 numPyramidLevels,
        const Transformations::TransformType transformType,
        MemoryStack &memory)
        : maxSupportedTransformType(maxSupportedTransformType), numPyramidLevels(numPyramidLevels), templateImageHeight(templateImage.get_size(0)), templateImageWidth(templateImage.get_size(1)), isValid(false)
      {
        Result lastResult;

        BeginBenchmark("LucasKanadeTracker_Fast");

        AnkiConditionalErrorAndReturn(templateImageHeight > 0 && templateImageWidth > 0,
          "LucasKanadeTracker_Fast::LucasKanadeTracker_Fast", "template widths and heights must be greater than zero, and multiples of %d", ANKI_VISION_IMAGE_WIDTH_MULTIPLE);

        AnkiConditionalErrorAndReturn(numPyramidLevels > 0,
          "LucasKanadeTracker_Fast::LucasKanadeTracker_Fast", "numPyramidLevels must be greater than zero");

        AnkiConditionalErrorAndReturn(transformType <= maxSupportedTransformType,
          "LucasKanadeTracker_Fast::LucasKanadeTracker_Fast", "Transform type %d not supported", transformType);

        const s32 initialImageScaleS32 = BASE_IMAGE_WIDTH / templateImage.get_size(1);
        const s32 initialImagePowerS32 = Log2u32(static_cast<u32>(initialImageScaleS32));
        const f32 initialImageScaleF32 = static_cast<f32>(initialImageScaleS32);

        AnkiConditionalErrorAndReturn(((1<<initialImagePowerS32)*templateImage.get_size(1)) == BASE_IMAGE_WIDTH,
          "LucasKanadeTracker_Fast::LucasKanadeTracker_Fast", "The templateImage must be a power of two smaller than BASE_IMAGE_WIDTH");

        templateRegion = templateQuad.ComputeBoundingRectangle().ComputeScaledRectangle(scaleTemplateRegionPercent);

        templateRegion.left /= initialImageScaleF32;
        templateRegion.right /= initialImageScaleF32;
        templateRegion.top /= initialImageScaleF32;
        templateRegion.bottom /= initialImageScaleF32;

        // All pyramid width except the last one must be divisible by two
        for(s32 i=0; i<(numPyramidLevels-1); i++) {
          const s32 curTemplateHeight = templateImageHeight >> i;
          const s32 curTemplateWidth = templateImageWidth >> i;

          AnkiConditionalErrorAndReturn(!IsOdd(curTemplateHeight) && !IsOdd(curTemplateWidth),
            "LucasKanadeTracker_Fast::LucasKanadeTracker_Fast", "Template widths and height must divisible by 2^numPyramidLevels");
        }

        this->templateRegionHeight = templateRegion.bottom - templateRegion.top + 1.0f;
        this->templateRegionWidth = templateRegion.right - templateRegion.left + 1.0f;

        this->transformation = Transformations::PlanarTransformation_f32(transformType, templateQuad, memory);

        // Allocate the memory for the pyramid lists
        templateCoordinates = FixedLengthList<Meshgrid<f32> >(numPyramidLevels, memory);
        templateImagePyramid = FixedLengthList<Array<u8> >(numPyramidLevels, memory);
        templateImageXGradientPyramid = FixedLengthList<Array<s16> >(numPyramidLevels, memory);
        templateImageYGradientPyramid = FixedLengthList<Array<s16> >(numPyramidLevels, memory);

        templateCoordinates.set_size(numPyramidLevels);
        templateImagePyramid.set_size(numPyramidLevels);
        templateImageXGradientPyramid.set_size(numPyramidLevels);
        templateImageYGradientPyramid.set_size(numPyramidLevels);

        AnkiConditionalErrorAndReturn(templateImagePyramid.IsValid() && templateImageXGradientPyramid.IsValid() && templateImageYGradientPyramid.IsValid() && templateCoordinates.IsValid(),
          "LucasKanadeTracker_Fast::LucasKanadeTracker_Fast", "Could not allocate pyramid lists");

        // Allocate the memory for all the images
        for(s32 iScale=0; iScale<numPyramidLevels; iScale++) {
          const f32 scale = static_cast<f32>(1 << iScale);

          // Unused, remove?
          //const s32 curTemplateHeight = templateImageHeight >> iScale;
          //const s32 curTemplateWidth = templateImageWidth >> iScale;

          templateCoordinates[iScale] = Meshgrid<f32>(
            Linspace(-this->templateRegionWidth/2.0f, this->templateRegionWidth/2.0f, static_cast<s32>(FLT_FLOOR(this->templateRegionWidth/scale))),
            Linspace(-this->templateRegionHeight/2.0f, this->templateRegionHeight/2.0f, static_cast<s32>(FLT_FLOOR(this->templateRegionHeight/scale))));

          const s32 numPointsY = templateCoordinates[iScale].get_yGridVector().get_size();
          const s32 numPointsX = templateCoordinates[iScale].get_xGridVector().get_size();

          templateImagePyramid[iScale] = Array<u8>(numPointsY, numPointsX, memory);
          templateImageXGradientPyramid[iScale] = Array<s16>(numPointsY, numPointsX, memory);
          templateImageYGradientPyramid[iScale] = Array<s16>(numPointsY, numPointsX, memory);

          AnkiConditionalErrorAndReturn(templateImagePyramid[iScale].IsValid() && templateImageXGradientPyramid[iScale].IsValid() && templateImageYGradientPyramid[iScale].IsValid(),
            "LucasKanadeTracker_Fast::LucasKanadeTracker_Fast", "Could not allocate pyramid images");
        }

        // Sample all levels of the pyramid images
        for(s32 iScale=0; iScale<numPyramidLevels; iScale++) {
          if((lastResult = Interp2_Affine<u8,u8>(templateImage, templateCoordinates[iScale], transformation.get_homography(), this->transformation.get_centerOffset(initialImageScaleF32), this->templateImagePyramid[iScale], INTERPOLATE_LINEAR)) != RESULT_OK) {
            AnkiError("LucasKanadeTracker_Fast::LucasKanadeTracker_Fast", "Interp2_Affine failed with code 0x%x", lastResult);
            return;
          }
        }

        // Compute the spatial derivatives
        // TODO: compute without borders?
        for(s32 i=0; i<numPyramidLevels; i++) {
          if((lastResult = ImageProcessing::ComputeXGradient<u8,s16,s16>(templateImagePyramid[i], templateImageXGradientPyramid[i])) != RESULT_OK) {
            AnkiError("LucasKanadeTracker_Fast::LucasKanadeTracker_Fast", "ComputeXGradient failed with code 0x%x", lastResult);
            return;
          }

          if((lastResult = ImageProcessing::ComputeYGradient<u8,s16,s16>(templateImagePyramid[i], templateImageYGradientPyramid[i])) != RESULT_OK) {
            AnkiError("LucasKanadeTracker_Fast::LucasKanadeTracker_Fast", "ComputeYGradient failed with code 0x%x", lastResult);
            return;
          }
        }

        this->isValid = true;

        EndBenchmark("LucasKanadeTracker_Fast");
      }

      bool LucasKanadeTracker_Fast::IsValid() const
      {
        if(!this->isValid)
          return false;

        if(!templateImagePyramid.IsValid())
          return false;

        if(!templateImageXGradientPyramid.IsValid())
          return false;

        if(!templateImageYGradientPyramid.IsValid())
          return false;

        for(s32 i=0; i<numPyramidLevels; i++) {
          if(!templateImagePyramid[i].IsValid())
            return false;

          if(!templateImageXGradientPyramid[i].IsValid())
            return false;

          if(!templateImageYGradientPyramid[i].IsValid())
            return false;
        }

        return true;
      }

      Result LucasKanadeTracker_Fast::UpdateTransformation(const Array<f32> &update, const f32 scale, MemoryStack scratch, Transformations::TransformType updateType)
      {
        return this->transformation.Update(update, scale, scratch, updateType);
      }

      Result LucasKanadeTracker_Fast::VerifyTrack_Projective(
        const Array<u8> &nextImage,
        const u8 verify_maxPixelDifference,
        s32 &verify_meanAbsoluteDifference,
        s32 &verify_numInBounds,
        s32 &verify_numSimilarPixels,
        MemoryStack scratch) const
      {
        // This method is heavily based on Interp2_Projective
        // The call would be like: Interp2_Projective<u8,u8>(nextImage, originalCoordinates, interpolationHomography, centerOffset, nextImageTransformed2d, INTERPOLATE_LINEAR, 0);

        const s32 verify_maxPixelDifferenceS32 = verify_maxPixelDifference;

        const s32 nextImageHeight = nextImage.get_size(0);
        const s32 nextImageWidth = nextImage.get_size(1);

        const s32 whichScale = 1;
        const f32 scale = static_cast<f32>(1 << whichScale);

        const s32 initialImageScaleS32 = BASE_IMAGE_WIDTH / nextImageWidth;
        const f32 initialImageScaleF32 = static_cast<f32>(initialImageScaleS32);

        const f32 oneOverTwoFiftyFive = 1.0f / 255.0f;
        const f32 scaleOverFiveTen = scale / (2.0f*255.0f);

        //const Point<f32>& centerOffset = this->transformation.get_centerOffset();
        const Point<f32> centerOffsetScaled = this->transformation.get_centerOffset(initialImageScaleF32);

        // Initialize with some very extreme coordinates
        FixedLengthList<Quadrilateral<f32> > previousCorners(NUM_PREVIOUS_QUADS_TO_COMPARE, scratch);

        for(s32 i=0; i<NUM_PREVIOUS_QUADS_TO_COMPARE; i++) {
          previousCorners[i] = Quadrilateral<f32>(Point<f32>(-1e10f,-1e10f), Point<f32>(-1e10f,-1e10f), Point<f32>(-1e10f,-1e10f), Point<f32>(-1e10f,-1e10f));
        }

        Meshgrid<f32> originalCoordinates(
          Linspace(-this->templateRegionWidth/2.0f, this->templateRegionWidth/2.0f, static_cast<s32>(FLT_FLOOR(this->templateRegionWidth/scale))),
          Linspace(-this->templateRegionHeight/2.0f, this->templateRegionHeight/2.0f, static_cast<s32>(FLT_FLOOR(this->templateRegionHeight/scale))));

        // Unused, remove?
        //const s32 outHeight = originalCoordinates.get_yGridVector().get_size();
        //const s32 outWidth = originalCoordinates.get_xGridVector().get_size();

        const f32 xyReferenceMin = 0.0f;
        const f32 xReferenceMax = static_cast<f32>(nextImageWidth) - 1.0f;
        const f32 yReferenceMax = static_cast<f32>(nextImageHeight) - 1.0f;

        const LinearSequence<f32> &yGridVector = originalCoordinates.get_yGridVector();
        const LinearSequence<f32> &xGridVector = originalCoordinates.get_xGridVector();

        const f32 yGridStart = yGridVector.get_start();
        const f32 xGridStart = xGridVector.get_start();

        const f32 yGridDelta = yGridVector.get_increment();
        const f32 xGridDelta = xGridVector.get_increment();

        const s32 yIterationMax = yGridVector.get_size();
        const s32 xIterationMax = xGridVector.get_size();

        const Array<f32> &homography = this->transformation.get_homography();
        const f32 h00 = homography[0][0]; const f32 h01 = homography[0][1]; const f32 h02 = homography[0][2] / initialImageScaleF32;
        const f32 h10 = homography[1][0]; const f32 h11 = homography[1][1]; const f32 h12 = homography[1][2] / initialImageScaleF32;
        const f32 h20 = homography[2][0] * initialImageScaleF32; const f32 h21 = homography[2][1] * initialImageScaleF32; //const f32 h22 = 1.0f;

        verify_numInBounds = 0;
        verify_numSimilarPixels = 0;
        s32 totalGrayvalueDifference = 0;

        // TODO: make the x and y limits from 1 to end-2

        f32 yOriginal = yGridStart;
        for(s32 y=0; y<yIterationMax; y++) {
          const u8 * restrict pTemplateImage = this->templateImagePyramid[whichScale].Pointer(y, 0);

          const s16 * restrict pTemplateImageXGradient = this->templateImageXGradientPyramid[whichScale].Pointer(y, 0);
          const s16 * restrict pTemplateImageYGradient = this->templateImageYGradientPyramid[whichScale].Pointer(y, 0);

          f32 xOriginal = xGridStart;

          for(s32 x=0; x<xIterationMax; x++) {
            // TODO: These two could be strength reduced
            const f32 xTransformedRaw = h00*xOriginal + h01*yOriginal + h02;
            const f32 yTransformedRaw = h10*xOriginal + h11*yOriginal + h12;

            const f32 normalization = h20*xOriginal + h21*yOriginal + 1.0f;

            const f32 xTransformed = (xTransformedRaw / normalization) + centerOffsetScaled.x;
            const f32 yTransformed = (yTransformedRaw / normalization) + centerOffsetScaled.y;

            xOriginal += xGridDelta;

            const f32 x0 = FLT_FLOOR(xTransformed);
            const f32 x1 = ceilf(xTransformed); // x0 + 1.0f;

            const f32 y0 = FLT_FLOOR(yTransformed);
            const f32 y1 = ceilf(yTransformed); // y0 + 1.0f;

            // If out of bounds, continue
            if(x0 < xyReferenceMin || x1 > xReferenceMax || y0 < xyReferenceMin || y1 > yReferenceMax) {
              continue;
            }

            verify_numInBounds++;

            const f32 alphaX = xTransformed - x0;
            const f32 alphaXinverse = 1 - alphaX;

            const f32 alphaY = yTransformed - y0;
            const f32 alphaYinverse = 1.0f - alphaY;

            const s32 y0S32 = static_cast<s32>(Round(y0));
            const s32 y1S32 = static_cast<s32>(Round(y1));
            const s32 x0S32 = static_cast<s32>(Round(x0));

            const u8 * restrict pReference_y0 = nextImage.Pointer(y0S32, x0S32);
            const u8 * restrict pReference_y1 = nextImage.Pointer(y1S32, x0S32);

            const f32 pixelTL = *pReference_y0;
            const f32 pixelTR = *(pReference_y0+1);
            const f32 pixelBL = *pReference_y1;
            const f32 pixelBR = *(pReference_y1+1);

            const s32 interpolatedPixelValue = RoundS32(InterpolateBilinear2d<f32>(pixelTL, pixelTR, pixelBL, pixelBR, alphaY, alphaYinverse, alphaX, alphaXinverse));
            const s32 templatePixelValue = pTemplateImage[x];
            const s32 grayvalueDifference = ABS(interpolatedPixelValue - templatePixelValue);

            totalGrayvalueDifference += grayvalueDifference;

            if(grayvalueDifference <= verify_maxPixelDifferenceS32) {
              verify_numSimilarPixels++;
            }
          } // for(s32 x=0; x<xIterationMax; x++)

          yOriginal += yGridDelta;
        } // for(s32 y=0; y<yIterationMax; y++)

        verify_meanAbsoluteDifference = totalGrayvalueDifference / verify_numInBounds;

        return RESULT_OK;
      } // Result LucasKanadeTracker_Projective::IterativelyRefineTrack_Projective()

      Result LucasKanadeTracker_Fast::set_transformation(const Transformations::PlanarTransformation_f32 &transformation)
      {
        Result lastResult;

        const Transformations::TransformType originalType = this->transformation.get_transformType();

        if((lastResult = this->transformation.set_transformType(transformation.get_transformType())) != RESULT_OK) {
          this->transformation.set_transformType(originalType);
          return lastResult;
        }

        if((lastResult = this->transformation.set_homography(transformation.get_homography())) != RESULT_OK) {
          this->transformation.set_transformType(originalType);
          return lastResult;
        }

        return RESULT_OK;
      }

      Transformations::PlanarTransformation_f32 LucasKanadeTracker_Fast::get_transformation() const
      {
        return transformation;
      }

      s32 LucasKanadeTracker_Fast::get_numTemplatePixels() const
      {
        return RoundS32(templateRegionHeight * templateRegionWidth);
      }
    } // namespace TemplateTracker
  } // namespace Embedded
} // namespace Anki

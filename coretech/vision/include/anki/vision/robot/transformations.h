/**
File: transformations.h
Author: Peter Barnum
Created: 2014-02-18

Various utilities for transforming coordinates between different reference frames

Copyright Anki, Inc. 2013
For internal use only. No part of this code may be used without a signed non-disclosure agreement with Anki, inc.
**/

#ifndef _ANKICORETECHEMBEDDED_VISION_TRANSFORMATIONS_H_
#define _ANKICORETECHEMBEDDED_VISION_TRANSFORMATIONS_H_

#include "anki/common/robot/config.h"
#include "anki/common/robot/array2d.h"

namespace Anki
{
  namespace Embedded
  {
    class SerializedBuffer;

    namespace Transformations
    {
      // The type of transformation.
      //
      // The first byte is the degrees of freedom of the transformation, so if it bit-shifted right
      // by 8, it is equal to the number of parameters
      enum TransformType
      {
        TRANSFORM_UNKNOWN     = 0x0000,
        TRANSFORM_TRANSLATION = 0x0200,
        TRANSFORM_AFFINE      = 0x0600,
        TRANSFORM_PROJECTIVE  = 0x0800
      };

      // Simple helper function to compute the homography from an input quad. The input quad point should be ordered in the non-rotated, corner-opposite format
      Result ComputeHomographyFromQuad(const Quadrilateral<s16> &quad, Array<f32> &homography, MemoryStack scratch);

      // A PlanarTransformation object can do the following:
      // 1. Hold the current planar transformation, and optionally the initial extents of the quadrilateral
      // 2. Update the planar transformation with an update delta
      // 3. Transform a set of points, quadrilateral, or image to the new coordinate frame
      //
      // NOTE: All coordinates for images should be stored in the standard resolution
      //       BASE_IMAGE_WIDTH X BASE_IMAGE_HEIGHT (currently, this is QVGA)
      class PlanarTransformation_f32
      {
      public:

        // Initialize with input corners and homography, or if not input, set to zero or identity
        // If the input corners and homography are input, they are copied to object-local copies
        PlanarTransformation_f32(const TransformType transformType, const Quadrilateral<f32> &initialCorners, const Array<f32> &initialHomography, MemoryStack &memory);
        PlanarTransformation_f32(const TransformType transformType, const Quadrilateral<f32> &initialCorners, MemoryStack &memory);
        PlanarTransformation_f32(const TransformType transformType, MemoryStack &memory);

        // Same as the above, but explicitly sets the center of the transformation
        PlanarTransformation_f32(const TransformType transformType, const Quadrilateral<f32> &initialCorners, const Array<f32> &initialHomography, const Point<f32> &centerOffset, MemoryStack &memory);
        PlanarTransformation_f32(const TransformType transformType, const Quadrilateral<f32> &initialCorners, const Point<f32> &centerOffset, MemoryStack &memory);
        PlanarTransformation_f32(const TransformType transformType, const Point<f32> &centerOffset, MemoryStack &memory);

        // Initialize as invalid
        PlanarTransformation_f32();

        // Using the current transformation, warp the In points to the Out points.
        // xIn, yIn, xOut, and yOut must be 1xN.
        // xOut and yOut must be allocated before calling
        // Requires at least N*sizeof(f32) bytes of scratch
        Result TransformPoints(
          const Array<f32> &xIn, const Array<f32> &yIn,
          const f32 scale,
          const bool inputPointsAreZeroCentered,
          const bool outputPointsAreZeroCentered,
          Array<f32> &xOut, Array<f32> &yOut) const;

        // Update the transformation. The format of the update should be as follows:
        // TRANSFORM_TRANSLATION: [-dx, -dy]
        // TRANSFORM_AFFINE: [h00, h01, h02, h10, h11, h12]
        // TRANSFORM_PROJECTIVE: [h00, h01, h02, h10, h11, h12, h20, h21]
        Result Update(const Array<f32> &update, const f32 scale, MemoryStack scratch, TransformType updateType=TRANSFORM_UNKNOWN);

        Result Print(const char * const variableName = "Transformation") const;

        // Transform the input Quadrilateral, using this object's transformation
        Quadrilateral<f32> TransformQuadrilateral(
          const Quadrilateral<f32> &in,
          MemoryStack scratch,
          const f32 scale=1.0f) const;

        // Transform an array (like an image)
        Result TransformArray(
          const Array<u8> &in,
          Array<u8> &out,
          MemoryStack scratch,
          const f32 scale=1.0f) const;

        bool IsValid() const;

        // Set this object's transformType, centerOffset, initialCorners, and homography
        Result Set(const PlanarTransformation_f32 &newTransformation);

        Result Serialize(SerializedBuffer &buffer) const;
        const void* Deserialize(const void* buffer, const s32 bufferLength);

        Result set_transformType(const TransformType transformType);
        TransformType get_transformType() const;

        Result set_homography(const Array<f32>& in);
        const Array<f32>& get_homography() const;

        Result set_initialCorners(const Quadrilateral<f32> &initialCorners);
        const Quadrilateral<f32>& get_initialCorners() const;

        Result set_centerOffset(const Point<f32> &centerOffset);
        Point<f32> get_centerOffset(const f32 scale) const;

        // Transform this object's initialCorners, based on its current homography
        Quadrilateral<f32> get_transformedCorners(MemoryStack scratch) const;

      protected:
        bool isValid;

        TransformType transformType;

        // All types of plane transformations are stored in a 3x3 homography matrix, though some values may be zero (or ones for diagonals)
        Array<f32> homography;

        // The initial corners of the valid region
        Quadrilateral<f32> initialCorners;

        // The offset applied to an image, so that origin of the coordinate system is at the center
        // of the quadrilateral
        Point<f32> centerOffset;

        static Result TransformPointsStatic(
          const Array<f32> &xIn, const Array<f32> &yIn,
          const f32 scale,
          const Point<f32>& centerOffset,
          const TransformType transformType,
          const Array<f32> &homography,
          const bool inputPointsAreZeroCentered,
          const bool outputPointsAreZeroCentered,
          Array<f32> &xOut, Array<f32> &yOut);

        Result Init(const TransformType transformType, const Quadrilateral<f32> &initialCorners, const Array<f32> &initialHomography, const Point<f32> &centerOffset, MemoryStack &memory);
      };
    } // namespace Transformations
  } // namespace Embedded
} // namespace Anki

#endif //_ANKICORETECHEMBEDDED_VISION_TRANSFORMATIONS_H_

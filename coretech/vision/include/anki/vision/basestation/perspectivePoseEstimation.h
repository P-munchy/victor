/**
 * File: perspectivePoseEstimation.h
 *
 * Author: Andrew Stein
 * Date:   04-01-2014
 *
 * Description: Methods for the three-point perspective pose estimation problem.
 *
 *
 * Copyright: Anki, Inc. 2014
 **/

#ifndef ANKI_VISION_PERSPECTIVEPOSEESTIMATION_H
#define ANKI_VISION_PERSPECTIVEPOSEESTIMATION_H

#include <array>
#include "anki/common/basestation/math/point.h"
#include "anki/common/basestation/math/pose.h"
#include "anki/common/basestation/math/quad.h"

namespace Anki {
  namespace Vision {
    namespace P3P {

      // Take in 3 image rays and 3 corresponding world points, output
      // 4 possible pose solutions.  PRECISION is either double or float.
      // Image rays should be unit vectors, computed as follows from 2D image
      // coordinates (u,v) using intrinsic calibration information stored in a
      // 3x3 matrix, "K":
      //
      //   [u' v' w']^T = K^(-1) * [u v 1]^T
      //
      //   [u' v' w']^T /= norm([u' v' w']);
      //
      // Reference:
      //   "A Novel Parametrization of the Perspective-Three-Point Problem for
      //    a Direct Computation of Absolute Camera Position and Orientation"
      //   by Kneip et al.
      //
      template<typename PRECISION>
      ReturnCode computePossiblePoses(const std::array<Point<3,PRECISION>,3>& worldPoints,
                                      const std::array<Point<3,PRECISION>,3>& imageRays,
                                      std::array<Pose3d,4>& poses);
      
      
      template<typename PRECISION>
      ReturnCode computePossiblePoses(const Point<3,PRECISION>& worldPoint1,
                                      const Point<3,PRECISION>& worldPoint2,
                                      const Point<3,PRECISION>& worldPoint3,
                                      const Point<3,PRECISION>& imageRay1,
                                      const Point<3,PRECISION>& imageRay2,
                                      const Point<3,PRECISION>& imageRay3,
                                      std::array<Pose3d,4>& poses);
      
      
    } // namespace P3P
  } // namespace Vision
} // namespace Anki


#endif // ANKI_VISION_PERSPECTIVEPOSEESTIMATION_H
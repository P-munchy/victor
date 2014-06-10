﻿//
//  camera.cpp
//  CoreTech_Vision
//
//  Created by Andrew Stein on 8/23/13.
//  Copyright (c) 2013 Anki, Inc. All rights reserved.
//

#include <algorithm>
#include <list>

#include "anki/common/basestation/jsonTools.h"
#include "anki/common/basestation/math/point_impl.h"

#if ANKICORETECH_USE_OPENCV
#include "opencv2/calib3d/calib3d.hpp"
#endif

#include "anki/vision/basestation/camera.h"
#include "anki/vision/basestation/observableObject.h"
#include "anki/vision/basestation/perspectivePoseEstimation.h"


// Set to 1 to use OpenCV's iterative pose estimation for quads.
// Otherwise, the closed form P3P solution is used.
// NOTE: this currently only affects the ComputeObjectPose() that takes in quads.
#define USE_ITERATIVE_QUAD_POSE_ESTIMATION 1

namespace Anki {
  
  namespace Vision {
    
    Camera::Camera(void)
    : _camID(0)
    , _isCalibrated(false) //, _calibration(nullptr)
    {
      
    } // Constructor: Camera()
    
    Camera::Camera(const CameraID_t cam_id,
                   const CameraCalibration &calibration,
                   const Pose3d& pose_in)
    : _camID(cam_id)
    , _isCalibrated(true)
    , _calibration(calibration)
    , _pose(pose_in)
    {
      
    } // Constructor: Camera(calibration, pose)
    
    
    
#if ANKICORETECH_USE_OPENCV
    Pose3d Camera::ComputeObjectPoseHelper(const std::vector<cv::Point2f>& cvImagePoints,
                                           const std::vector<cv::Point3f>& cvObjPoints) const
    {
      CORETECH_THROW_IF(this->IsCalibrated() == false);
      
      cv::Vec3d cvRvec, cvTranslation;
      
      Matrix_3x3f calibMatrix(_calibration.GetCalibrationMatrix());
      
      cv::Mat distortionCoeffs; // TODO: currently empty, use radial distoration?
      cv::solvePnP(cvObjPoints, cvImagePoints,
                   calibMatrix.get_CvMatx_(), distortionCoeffs,
                   cvRvec, cvTranslation,
                   false, CV_ITERATIVE);
      
      RotationVector3d rvec(Vec3f(cvRvec[0], cvRvec[1], cvRvec[2]));
      Vec3f translation(cvTranslation[0], cvTranslation[1], cvTranslation[2]);
      
      // Return Pose object w.r.t. the camera's pose
      const Pose3d pose(rvec, translation, &(_pose));
      
      return pose;
      
    } // ComputeObjectPoseHelper()
    
#endif
    
    
    Pose3d Camera::ComputeObjectPose(const std::vector<Point2f>& imgPoints,
                                     const std::vector<Point3f>& objPoints) const
    {
      if(this->IsCalibrated() == false) {
        CORETECH_THROW("Camera::ComputeObjectPose() called before calibration set.");
      }
      
#if ANKICORETECH_USE_OPENCV
      std::vector<cv::Point2f> cvImagePoints;
      std::vector<cv::Point3f> cvObjPoints;
      
      for(const Point2f & imgPt : imgPoints) {
        cvImagePoints.emplace_back(imgPt.get_CvPoint_());
      }
      
      for(const Point3f & objPt : objPoints) {
        cvObjPoints.emplace_back(objPt.get_CvPoint3_());
      }
      
      return ComputeObjectPoseHelper(cvImagePoints, cvObjPoints);
      
#else
      // TODO: Implement!
      CORETECH_THROW("Unimplemented!")
      return Pose3d();
#endif
      
    } // ComputeObjectPose(from std::vectors)
    
    template<typename WORKING_PRECISION>
    Pose3d Camera::ComputeObjectPose(const Quad2f& imgQuad,
                                     const Quad3f& worldQuad) const
    {
      if(this->IsCalibrated() == false) {
        CORETECH_THROW("Camera::ComputeObjectPose() called before calibration set.");
      }
      
#if USE_ITERATIVE_QUAD_POSE_ESTIMATION
      
      std::vector<cv::Point2f> cvImagePoints;
      std::vector<cv::Point3f> cvObjPoints;
      
      cvImagePoints.emplace_back(imgQuad[Quad::TopLeft].get_CvPoint_());
      cvImagePoints.emplace_back(imgQuad[Quad::BottomLeft].get_CvPoint_());
      cvImagePoints.emplace_back(imgQuad[Quad::TopRight].get_CvPoint_());
      cvImagePoints.emplace_back(imgQuad[Quad::BottomRight].get_CvPoint_());
      
      cvObjPoints.emplace_back(worldQuad[Quad::TopLeft].get_CvPoint3_());
      cvObjPoints.emplace_back(worldQuad[Quad::BottomLeft].get_CvPoint3_());
      cvObjPoints.emplace_back(worldQuad[Quad::TopRight].get_CvPoint3_());
      cvObjPoints.emplace_back(worldQuad[Quad::BottomRight].get_CvPoint3_());
      
      return ComputeObjectPoseHelper(cvImagePoints, cvObjPoints);

#else
      
      Pose3d pose;
      
      
      // Turn the three image points into unit vectors corresponding to rays
      // in the direction of the image points
      const SmallSquareMatrix<3,WORKING_PRECISION> invK = this->GetCalibration().GetInvCalibrationMatrix<WORKING_PRECISION>();
      
      Quadrilateral<3, WORKING_PRECISION> imgRays, worldPoints;
      
      for(Quad::CornerName i_corner=Quad::FirstCorner; i_corner < Quad::NumCorners; ++i_corner)
      {
        // Get unit vector pointing along each image ray
        //   imgRay = K^(-1) * [u v 1]^T
        imgRays[i_corner].x() = static_cast<WORKING_PRECISION>(imgQuad[i_corner].x());
        imgRays[i_corner].y() = static_cast<WORKING_PRECISION>(imgQuad[i_corner].y());
        imgRays[i_corner].z() = WORKING_PRECISION(1);
        
        imgRays[i_corner] = invK * imgRays[i_corner];
        
        /*
        printf("point %d (%f, %f) became ray (%f, %f, %f) ",
               i_corner,
               imgQuad[i_corner].x(), imgQuad[i_corner].y(),
               imgRays[i_corner].x(), imgRays[i_corner].y(), imgRays[i_corner].z());
        */
        
        imgRays[i_corner].MakeUnitLength();
        
        //printf(" which normalized to (%f, %f, %f)\n",
        //       imgRays[i_corner].x(), imgRays[i_corner].y(), imgRays[i_corner].z());
        
        // cast each world quad into working precision quad
        worldPoints[i_corner].x() = static_cast<WORKING_PRECISION>(worldQuad[i_corner].x());
        worldPoints[i_corner].y() = static_cast<WORKING_PRECISION>(worldQuad[i_corner].y());
        worldPoints[i_corner].z() = static_cast<WORKING_PRECISION>(worldQuad[i_corner].z());
      }
      
      
      // Compute best pose from each subset of three corners, keeping the one
      // with the lowest error
      float minErrorOuter = std::numeric_limits<float>::max();
    
      std::array<Quad::CornerName,4> cornerList = {
        {Quad::TopLeft, Quad::BottomLeft, Quad::TopRight, Quad::BottomRight}
      };
  
      for(s32 i=0; i<4; ++i)
      {
        // Use the first corner in the current corner list as the validation
        // corner. Use the remaining three to estimate the pose.
        const Quad::CornerName i_validate = cornerList[0];
        
        //printf("Validating with %d, estimating with %d, %d, %d\n",
        //       i_validate, cornerList[1], cornerList[2], cornerList[3]);
        
        std::array<Pose3d,4> possiblePoses;
        P3P::computePossiblePoses(worldPoints[cornerList[1]],
                                  worldPoints[cornerList[2]],
                                  worldPoints[cornerList[3]],
                                  imgRays[cornerList[1]],
                                  imgRays[cornerList[2]],
                                  imgRays[cornerList[3]],
                                  possiblePoses);
        
        // Find the pose with the least reprojection error for the 4th
        // validation corner (which was not used in estimating the pose)
        s32 bestSolution = -1;
        float minErrorInner = std::numeric_limits<float>::max();
        
        for(s32 i_solution=0; i_solution<4; ++i_solution)
        {
          // First make sure this solution puts the object in front of the
          // camera (this should be true for two solutions)
          if(possiblePoses[i_solution].get_translation().z() > 0)
          {
            Point2f projectedPoint;
            this->Project3dPoint(possiblePoses[i_solution]*worldQuad[i_validate], projectedPoint);
            
            float error = (projectedPoint - imgQuad[i_validate]).Length();
            
            if(error < minErrorInner) {
              minErrorInner = error;
              bestSolution = i_solution;
            }
          } // if translation.z > 0
        } // for each solution
        
        CORETECH_ASSERT(bestSolution >= 0);
        
        // If the pose using this validation corner is better than the
        // best so far, keep it
        if(minErrorInner < minErrorOuter) {
          minErrorOuter = minErrorInner;
          pose = possiblePoses[bestSolution];
        }
        
        if(i<3) {
          // Rearrange corner list for next loop, to get a different
          // validation corner each time
          std::swap(cornerList[0], cornerList[i+1]);
        }
        
      } // for each validation corner
      
      // Make sure to make the returned pose w.r.t. the camera!
      pose.set_parent(&_pose);
      
      return pose;
      
#endif // #if USE_ITERATIVE_QUAD_POSE_ESTIMATION
      
    } // ComputeObjectPose(from quads)
 
    
    // Explicit instantiation for single and double precision
    template Pose3d Camera::ComputeObjectPose<float>(const Quad2f& imgQuad,
                                                     const Quad3f& worldQuad) const;

    template Pose3d Camera::ComputeObjectPose<double>(const Quad2f& imgQuad,
                                                      const Quad3f& worldQuad) const;

    
    bool Camera::IsWithinFieldOfView(const Point2f &projectedPoint) const
    {
      CORETECH_THROW_IF(this->IsCalibrated() == false);
      
      return (not std::isnan(projectedPoint.x()) &&
              not std::isnan(projectedPoint.y()) &&
              projectedPoint.x() >= 0.f && projectedPoint.y() >= 0.f &&
              projectedPoint.x() < _calibration.GetNcols() &&
              projectedPoint.y() < _calibration.GetNrows());
      
    } // Camera::IsVisible()

    
    void Camera::Project3dPoint(const Point3f& objPoint,
                                Point2f&       imgPoint) const
    {
      if(this->IsCalibrated() == false) {
        CORETECH_THROW("Camera::Project3dPoint() called before calibration set.");
      }
      
      const f32 BEHIND_OR_OCCLUDED = std::numeric_limits<f32>::quiet_NaN();
      
      if(objPoint.z() <= 0.f)
      {
        // Point not visible (not in front of camera)
        imgPoint = BEHIND_OR_OCCLUDED;

      } else {
        // Point visible, project it
        imgPoint.x() = (objPoint.x() / objPoint.z());
        imgPoint.y() = (objPoint.y() / objPoint.z());
        
        // TODO: Add radial distortion here
        //DistortCoordinate(imgPoints[i_corner], imgPoints[i_corner]);
        
        imgPoint.x() *= _calibration.GetFocalLength_x();
        imgPoint.y() *= _calibration.GetFocalLength_y();
        
        imgPoint += _calibration.GetCenter();
      }
      
    } // Project3dPoint()
    
    
    /* This doesn't work but it would be nice to have a "generic"
     wrapper here for looping over various containers of points and projecting
     them.
     
    template<template<class PointType> class PointContainer>
    void Project3dPointContainer(const PointContainer<Point3f>& objPoints,
                                 PointContainer<Point2f>& imgPoints)
    {
      CORETECH_ASSERT(objPoints.size() == imgPoints.size());
      
      auto objPointIter = objPoints.begin();
      auto imgPointIter = imgPoints.begin();
      
      while(objPointIter != objPoints.end()) {
        
        Project3dPoint(*objPointIter, *imgPointIter);
        
        ++objPointIter;
        ++imgPointIter;
      }
    }
     */
    
    template<class PointContainer3d, class PointContainer2d>
    void Camera::Project3dPointHelper(const PointContainer3d& objPoints,
                                      PointContainer2d& imgPoints) const
    {
      CORETECH_ASSERT(objPoints.size() == imgPoints.size());
      
      auto objPointIter = objPoints.begin();
      auto imgPointIter = imgPoints.begin();
      
      while(objPointIter != objPoints.end()) {
        
        Project3dPoint(*objPointIter, *imgPointIter);
        
        ++objPointIter;
        ++imgPointIter;
      }
    } // Camera::Project3dPointHelper()
    
    
    // Compute the projected image locations of a set of 3D points:
    void Camera::Project3dPoints(const std::vector<Point3f>& objPoints,
                                 std::vector<Point2f>&       imgPoints) const
    {
      imgPoints.resize(objPoints.size());
      Project3dPointHelper(objPoints, imgPoints);
    } // Project3dPoints(std::vectors)
    
    void Camera::Project3dPoints(const Quad3f& objPoints,
                                 Quad2f&       imgPoints) const
    {
      for(Quad::CornerName i_corner=Quad::FirstCorner;
          i_corner < Quad::NumCorners; ++i_corner)
      {
        Project3dPoint(objPoints[i_corner], imgPoints[i_corner]);
      }
    } // Project3dPoints(Quads)
    
    
    void Camera::ClearOccluders()
    {
      _occluderList.Clear();
    }
    
    
    void Camera::AddOccluder(const ObservableObject& object)
    {
      const Pose3d objectPoseWrtCamera(object.GetPose().getWithRespectTo(&_pose));
      
      std::vector<Point3f> cornersAtPose;
      std::vector<Point2f> projectedCorners;
      
      // Project the objects's corners into the image and create an occluding
      // bounding rectangle from that
      object.GetCorners(objectPoseWrtCamera, cornersAtPose);
      Project3dPoints(cornersAtPose, projectedCorners);
      
      _occluderList.AddOccluder(projectedCorners, objectPoseWrtCamera.get_translation().z());
      
    } // AddOccluder(ObservableObject)
    
    
    void Camera::AddOccluder(const KnownMarker& marker)
    {
      const Pose3d markerPoseWrtCamera = marker.GetPose().getWithRespectTo(&_pose);
      
      const Quad3f markerCorners = marker.Get3dCorners(markerPoseWrtCamera);
      
      Quad2f imgCorners;
      Project3dPoints(markerCorners, imgCorners);

      // Use closest point as the distance to the quad
      auto cornerIter = markerCorners.begin();
      f32 atDistance = cornerIter->z();
      ++cornerIter;
      while(cornerIter != markerCorners.end()) {
        if(cornerIter->z() < atDistance) {
          atDistance = cornerIter->z();
        }
        ++cornerIter;
      }
      
      _occluderList.AddOccluder(imgCorners, atDistance);

    } // AddOccluder(Quad3f)
    
    
  } // namespace Vision
} // namespace Anki
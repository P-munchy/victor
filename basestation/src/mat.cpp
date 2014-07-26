/**
 * File: mat.cpp
 *
 * Author: Andrew Stein
 * Date:   (various)
 *
 * Description: Implements a MatPiece object, which is a "mat" that Cozmo drives
 *              around on with VisionMarkers at known locations for localization.
 *
 *              MatPiece inherits from the generic Vision::ObservableObject.
 *
 *
 * Copyright: Anki, Inc. 2014
 **/



#include "anki/cozmo/basestation/mat.h"

#include "anki/vision/MarkerCodeDefinitions.h"
#include "anki/common/basestation/math/point_impl.h"
#include "anki/common/basestation/math/poseBase_impl.h"

namespace Anki {
  namespace Cozmo {
    
    // Instantiate static const MatPiece types here:
    const MatPiece::Type MatPiece::Type::INVALID;
    const MatPiece::Type MatPiece::Type::LETTERS_4x4;
    const MatPiece::Type MatPiece::Type::LARGE_PLATFORM;
    
    // MatPiece has no rotation ambiguities but we still need to define this
    // static const here to instatiate an empty list.
    const std::vector<RotationMatrix3d> MatPiece::_rotationAmbiguities;
    
    const std::array<Point3f, MatPiece::NUM_CORNERS> MatPiece::_canonicalCorners = {{
      Point3f(-0.5f, -0.5f,  0.5f),
      Point3f( 0.5f, -0.5f,  0.5f),
      Point3f(-0.5f, -0.5f, -0.5f),
      Point3f( 0.5f, -0.5f, -0.5f),
      Point3f(-0.5f,  0.5f,  0.5f),
      Point3f( 0.5f,  0.5f,  0.5f),
      Point3f(-0.5f,  0.5f, -0.5f),
      Point3f( 0.5f,  0.5f, -0.5f)
    }};
    
    
    MatPiece::MatPiece(ObjectType type, bool isFirstPiece)
    : Vision::ObservableObject(type)
    {
      
      // TODO: Use a MatTypeLUT and MatDefinitions file, like we do with blocks
      if(Type::LETTERS_4x4 == type) {
        
        _size = {1000.f, 1000.f, 2.5f};
        
        //#include "anki/cozmo/basestation/Mat_AnkiLogoPlus8Bits_8x8.def"
#       include "anki/cozmo/basestation/Mat_Letters_30mm_4x4.def"
     
      }
      else if(Type::LARGE_PLATFORM == type) {
        AddMarker(Vision::MARKER_A,
                  Pose3d(1.570796, {-1.000000,0.000000,0.000000}, {-200.000000,-200.000000,0.000000}),
                  30.000000);
        
        _size = {240.f, 240.f, 44.f};
        
        const f32& length = _size.x();
        const f32& width  = _size.y();
        const f32& height = _size.z();

        const f32 markerSize_sides = 25.f;
        const f32 markerSize_top   = 25.f;
        
        // Front Face
        AddMarker(Vision::MARKER_INVERTED_RAMPFRONT,
                  Pose3d(M_PI_2, Z_AXIS_3D, {length*.5f, 0.f, 0.f}),
                  markerSize_sides);
        
        // Back Face
        AddMarker(Vision::MARKER_INVERTED_RAMPBACK,
                  Pose3d(-M_PI_2, Z_AXIS_3D, {-length*.5f, 0.f, 0.f}),
                  markerSize_sides);

        // Right Face
        AddMarker(Vision::MARKER_INVERTED_RAMPRIGHT,
                  Pose3d(M_PI, Z_AXIS_3D, {0, width*.5f, 0}),
                  markerSize_sides);
        
        // Left Face
        AddMarker(Vision::MARKER_INVERTED_RAMPLEFT,
                  Pose3d(0.f, Z_AXIS_3D, {0, -width*.5f, 0}),
                  markerSize_sides);
        
        // Top Faces:
        AddMarker(Vision::MARKER_INVERTED_A,
                  Pose3d(M_PI_2, X_AXIS_3D, {-length*.25f, -width*.25f, height*.5f}),
                  markerSize_top);
        AddMarker(Vision::MARKER_INVERTED_B,
                  Pose3d(M_PI_2, X_AXIS_3D, {-length*.25f,  width*.25f, height*.5f}),
                  markerSize_top);
        AddMarker(Vision::MARKER_INVERTED_C,
                  Pose3d(M_PI_2, X_AXIS_3D, { length*.25f, -width*.25f, height*.5f}),
                  markerSize_top);
        AddMarker(Vision::MARKER_INVERTED_D,
                  Pose3d(M_PI_2, X_AXIS_3D, { length*.25f,  width*.25f, height*.5f}),
                  markerSize_top);
      }
      else {
        PRINT_NAMED_ERROR("MatPiece.UnrecognizedType",
                          "Trying to instantiate a MatPiece with an unknown Type = %d.\n", int(type));
      }
      
      /*
      if(isFirstPiece) {
        // If this is the first mat piece, we want to use its pose as the world's
        // origin directly.
        pose_.SetParent(Pose3d::GetWorldOrigin());
      } else {
        // Add an origin to use as this mat piece's reference, until such time
        // that we want to make it relative to another mat piece or some
        // common origin
        pose_.SetParent(&Pose3d::AddOrigin());
      }
       */
      
    };
    
    void MatPiece::SetOrigin(const Pose3d* origin)
    {
      pose_.SetParent(origin);
    }
    
    std::vector<RotationMatrix3d> const& MatPiece::GetRotationAmbiguities() const
    {
      return MatPiece::_rotationAmbiguities;
    }
    
    void MatPiece::Visualize()
    {
      Pose3d vizPose = pose_.GetWithRespectToOrigin();
      _vizHandle = VizManager::getInstance()->DrawCuboid(GetID().GetValue(), _size, vizPose, VIZ_COLOR_DEFAULT);
    }
    
    void MatPiece::EraseVisualization()
    {
      // Erase the main object
      if(_vizHandle != VizManager::INVALID_HANDLE) {
        VizManager::getInstance()->EraseVizObject(_vizHandle);
        _vizHandle = VizManager::INVALID_HANDLE;
      }
    }
    
    Quad2f MatPiece::GetBoundingQuadXY(const Pose3d& atPose, const f32 padding_mm) const
    {
      const RotationMatrix3d& R = atPose.GetRotationMatrix();
      
      Point3f paddedSize(_size);
      paddedSize += 2.f*padding_mm;
      
      std::vector<Point2f> points;
      points.reserve(MatPiece::NUM_CORNERS);
      for(auto corner : MatPiece::_canonicalCorners) {
        // Scale canonical point to correct (padded) size
        corner *= paddedSize;
        
        // Rotate to given pose
        corner = R*corner;
        
        // Project onto XY plane, i.e. just drop the Z coordinate
        points.emplace_back(corner.x(), corner.y());
      }
      
      Quad2f boundingQuad = GetBoundingQuad(points);
      
      // Re-center
      Point2f center(atPose.GetTranslation().x(), atPose.GetTranslation().y());
      boundingQuad += center;
      
      return boundingQuad;
    }
    
    
    ObjectType MatPiece::GetTypeByName(const std::string& name)
    {
      // TODO: Support other types/names
      if(name == "LETTERS_4x4") {
        return MatPiece::Type::LETTERS_4x4;
      } else if(name == "LARGE_PLATFORM") {
        return MatPiece::Type::LARGE_PLATFORM;
      } else {
        PRINT_NAMED_ERROR("MatPiece.NoTypeForName",
                          "No MatPiece Type registered for name '%s'.\n", name.c_str());
        return MatPiece::Type::INVALID;
      }
    }
    
    void MatPiece::GetCorners(const Pose3d& atPose, std::vector<Point3f>& corners) const {
      corners.resize(NUM_CORNERS);
      for(s32 i=0; i<NUM_CORNERS; ++i) {
        corners[i] = MatPiece::_canonicalCorners[i];
        corners[i] *= _size;
        corners[i] = atPose * corners[i];
      }
    }
    
    bool MatPiece::IsPoseOn(const Anki::Pose3d &pose, const f32 heightTol) const
    {
      Pose3d poseWrtMat;
      if(pose.GetWithRespectTo(pose_, poseWrtMat) == false) {
        return false;
      }
      
      const Point2f pt(poseWrtMat.GetTranslation().x(), poseWrtMat.GetTranslation().y());
      const bool withinBBox   = GetBoundingQuadXY(Pose3d()).Contains(pt);
      
      const bool withinHeight = (poseWrtMat.GetTranslation().z() >= _size.z()*.5f - heightTol &&
                                 poseWrtMat.GetTranslation().z() <= _size.z()*.5f + heightTol);
      
      // Make sure the given pose's rotation axis is well aligned with the mat's Z axis
      // TODO: make alignment a parameter?
      // TODO: const bool zAligned     = poseWrtMat.GetRotationAxis().z() >= std::cos(DEG_TO_RAD(25));
      const bool zAligned = true;
      
      return withinBBox && withinHeight && zAligned;
      
    } // IsPoseOn()
    
    f32 MatPiece::GetDrivingSurfaceHeight() const
    {
      Pose3d poseWrtOrigin = pose_.GetWithRespectToOrigin();
      return _size.z()*.5f + poseWrtOrigin.GetTranslation().z();
    } // GetDrivingSurfaceHeight()
    
  } // namespace Cozmo
  
} // namespace Anki

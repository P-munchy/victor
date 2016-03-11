/**
 * File: platform.cpp
 *
 * Author: Andrew Stein
 * Date:   9/15/2014
 *
 * Description: Implements a Platform object.
 *
 *
 * Copyright: Anki, Inc. 2014
 **/

#include "anki/common/types.h"

#include "anki/common/basestation/math/pose.h"
#include "anki/common/basestation/math/poseBase_impl.h"
#include "anki/common/basestation/math/quad_impl.h"

#include "anki/vision/MarkerCodeDefinitions.h"

#include "anki/cozmo/shared/cozmoConfig.h"

#include "platform.h"

namespace Anki {
  namespace Cozmo {
    
    static const Point3f& GetPlatformSize(Platform::Type type)
    {
      static const std::map<Platform::Type, Point3f> Sizes = {
        {ObjectType::Platform_LARGE, {252.f, 252.f, 44.f}},
      };
      
      auto iter = Sizes.find(type);
      if(iter == Sizes.end()) {
        PRINT_NAMED_ERROR("Platform.GetSize.UnknownPlatformType",
                          "No size defined for platform type %s (%d).\n",
                          ObjectTypeToString(type), type);
        
        static const Point3f DefaultSize(0.f, 0.f, 0.f);
        return DefaultSize;
      } else {
        return iter->second;
      }
    } // GetSize()
    
    Platform::Platform(Type type)
    : MatPiece(type, GetPlatformSize(type))
    {
      const f32& length = GetSize().x();
      const f32& width  = GetSize().y();
      const f32& height = GetSize().z();
      
      const f32 markerSize_sides = 30.f;
      const f32 markerSize_top   = 30.f;
      
      // TODO: Set to actual markers once we support platforms
      const Vision::MarkerType frontSideMarker = Vision::MARKER_UNKNOWN;
      const Vision::MarkerType backSideMarker  = Vision::MARKER_UNKNOWN;
      const Vision::MarkerType rightSideMarker = Vision::MARKER_UNKNOWN;
      const Vision::MarkerType leftSideMarker  = Vision::MARKER_UNKNOWN;
      
      const Vision::MarkerType topMarkerUL = Vision::MARKER_UNKNOWN;
      const Vision::MarkerType topMarkerUR = Vision::MARKER_UNKNOWN;
      const Vision::MarkerType topMarkerLL = Vision::MARKER_UNKNOWN;
      const Vision::MarkerType topMarkerLR = Vision::MARKER_UNKNOWN;
      
      // Front Face
      AddMarker(frontSideMarker,
                Pose3d(M_PI_2, Z_AXIS_3D(), {length*.5f, 0.f, -.5f*height}),
                markerSize_sides);
      
      // Back Face
      AddMarker(backSideMarker,
                Pose3d(-M_PI_2, Z_AXIS_3D(), {-length*.5f, 0.f, -.5f*height}),
                markerSize_sides);
      
      // Right Face
      AddMarker(rightSideMarker,
                Pose3d(M_PI, Z_AXIS_3D(), {0, width*.5f, -.5f*height}),
                markerSize_sides);
      
      // Left Face
      AddMarker(leftSideMarker,
                Pose3d(0.f, Z_AXIS_3D(), {0, -width*.5f, -.5f*height}),
                markerSize_sides);

      // Top Faces:
      AddMarker(topMarkerUL,
                Pose3d(-M_PI_2, X_AXIS_3D(), {-length*.25f, -width*.25f, 0.f}),
                markerSize_top);
      AddMarker(topMarkerLL,
                Pose3d(-M_PI_2, X_AXIS_3D(), {-length*.25f,  width*.25f, 0.f}),
                markerSize_top);
      AddMarker(topMarkerLR,
                Pose3d(-M_PI_2, X_AXIS_3D(), { length*.25f, -width*.25f, 0.f}),
                markerSize_top);
      AddMarker(topMarkerUR,
                Pose3d(-M_PI_2, X_AXIS_3D(), { length*.25f,  width*.25f, 0.f}),
                markerSize_top);
      
    } // Platform(type) Constructor
    
    
    void Platform::GetCanonicalUnsafeRegions(const f32 padding_mm,
                                           std::vector<Quad3f>& regions) const
    {
      // TODO: Define these geometry parameters elsewhere
      const f32 wallThickness = 3.f;
      const f32 grooveWidth = 3.75f;
      
      // Platforms have four unsafe regions around the edges, inset by the space
      // taken up by the inset for the lip (or "tongue-n-groove")
      const f32 xdim = 0.5f*GetSize().x()-wallThickness-grooveWidth;
      const f32 ydim = 0.5f*GetSize().y()-wallThickness-grooveWidth;
      regions = {{
        Quad3f({-xdim - padding_mm, ydim + padding_mm, 0.f},
               {-xdim - padding_mm, ydim - padding_mm, 0.f},
               { xdim + padding_mm, ydim + padding_mm, 0.f},
               { xdim + padding_mm, ydim - padding_mm, 0.f}),
      
        Quad3f({-xdim - padding_mm,-ydim + padding_mm, 0.f},
               {-xdim - padding_mm,-ydim - padding_mm, 0.f},
               { xdim + padding_mm,-ydim + padding_mm, 0.f},
               { xdim + padding_mm,-ydim - padding_mm, 0.f}),
        
        Quad3f({-xdim - padding_mm, ydim + padding_mm, 0.f},
               {-xdim - padding_mm,-ydim - padding_mm, 0.f},
               {-xdim + padding_mm, ydim + padding_mm, 0.f},
               {-xdim + padding_mm,-ydim - padding_mm, 0.f}),
        
        Quad3f({ xdim - padding_mm, ydim + padding_mm, 0.f},
               { xdim - padding_mm,-ydim - padding_mm, 0.f},
               { xdim + padding_mm, ydim + padding_mm, 0.f},
               { xdim + padding_mm,-ydim - padding_mm, 0.f})
      }};

    }
    
    
  } // namespace Cozmo
} // namespace Anki

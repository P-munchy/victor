/**
 * File: ramp.h
 *
 * Author: Andrew Stein
 * Date:   7/9/2014
 *
 * Description: Defines a Ramp object, which is a type of DockableObject
 *
 *
 * Copyright: Anki, Inc. 2014
 **/

#ifndef ANKI_COZMO_BASESTATION_RAMP_H
#define ANKI_COZMO_BASESTATION_RAMP_H

#include "anki/common/basestation/math/pose.h"
#include "anki/common/basestation/math/quad.h"

#include "anki/vision/basestation/observableObject.h"

#include "anki/cozmo/basestation/messages.h"

#include "anki/vision/MarkerCodeDefinitions.h"

#include "dockableObject.h"
#include "vizManager.h"

namespace Anki {
  
  namespace Cozmo {
    
    // Note that a ramp's origin (o) is the center of the "block" that makes up
    // its platform portion.
    //
    //   +------------+
    //   |              .
    //   |                .
    //   |     o            .
    //   |                    .
    //   |                      .
    //   *------------------------+
    //   <= Platform =><= Slope ==>
    //
    
    class Ramp : public DockableObject
    {
    public:
      
      class Type : public ObjectType {
        Type() { }
      public:
        static const Type BASIC_RAMP;
      };
      
      Ramp();
      Ramp(const Ramp& otherRamp);
      
      // Return start poses (at Ramp's current position) for going up or down
      // the ramp. The distance for ascent is from the tip of the slope.  The
      // distance for descent is from the opposite edge of the ramp.
      Pose3d GetPreAscentPose(const float distance) const;
      Pose3d GetPreDescentPose(const float distance) const;
      
      //
      // Inherited Virtual Methods
      //
      virtual ~Ramp();
      
      virtual Ramp*  Clone() const override;
      virtual void   GetCorners(const Pose3d& atPose, std::vector<Point3f>& corners) const override;
      virtual void   Visualize() override;
      virtual void   Visualize(VIZ_COLOR_ID color) override;
      virtual void   Visualize(const VIZ_COLOR_ID color, const f32 preDockPoseDistance) override;
      virtual void   EraseVisualization() override;
      virtual Quad2f GetBoundingQuadXY(const Pose3d& atPose, const f32 padding_mm = 0.f) const override;
      
      virtual void GetPreDockPoses(const float distance_mm,
                                   std::vector<PoseMarkerPair_t>& poseMarkerPairs,
                                   const Vision::Marker::Code withCode = Vision::Marker::ANY_CODE) const override;
      
      
      static ObjectType GetTypeByName(const std::string& name);


    protected:
      static const s32 NUM_CORNERS = 8;
      
      // Model dimensions in mm (perhaps these should come from a configuration
      // file instead?)
      constexpr static const f32 Width          = 60.f;
      constexpr static const f32 Height         = 44.f;
      constexpr static const f32 SlopeLength    = 88.f;
      constexpr static const f32 PlatformLength = 44.f;
      constexpr static const f32 MarkerSize     = 25.f;
      constexpr static const f32 FrontMarkerDistance = 50.f;
      static const f32 Angle;
        
      static const std::array<Point3f, NUM_CORNERS> CanonicalCorners;
      
      const Vision::KnownMarker* _leftMarker;
      const Vision::KnownMarker* _rightMarker;
      const Vision::KnownMarker* _frontMarker;
      const Vision::KnownMarker* _topMarker;
      
      std::array<VizManager::Handle_t,3> _vizHandle;
      
    }; // class Ramp
    
  } // namespace Cozmo
} // namespace Anki

#endif // ANKI_COZMO_BASESTATION_RAMP_H

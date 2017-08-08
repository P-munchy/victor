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

#include "anki/vision/MarkerCodeDefinitions.h"

#include "engine/actionableObject.h"
#include "engine/viz/vizManager.h"

namespace Anki {
  
  namespace Cozmo {
    
    // Note that a ramp's origin (o) is the bottom right vertex of this diagram:
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
    
    class Ramp : public ActionableObject
    {
    public:
      using Type = ObjectType;
      
      Ramp();
      
      virtual const Point3f& GetSize() const override { return _size; }
      
      f32     GetHeight() const { return Height; }
      Radians GetAngle()  const { return Angle;  }
      
      const Vision::KnownMarker* GetFrontMarker() const { return _frontMarker; }
      const Vision::KnownMarker* GetTopMarker()   const { return _topMarker;   }
      
      typedef enum : u8 {
        ASCENDING,
        DESCENDING,
        UNKNOWN
      } TraversalDirection;
      
      // Determine whether a robot will ascend or descend the ramp, based on its
      // relative pose. If it is above the ramp, it must be descending. If it
      // is on the same level as the ramp, it must be ascending. If it can't be
      // determined, UNKNOWN is returned.
      TraversalDirection WillAscendOrDescend(const Pose3d& robotPose) const;
      
      // Return final poses (at Ramp's current position) for a robot after it
      // has finished going up or down the ramp. Takes the robot's wheel base
      // as input since the assumption is that the robot will be level when its
      // back wheels have left the slope, meaning the robot's origin (between
      // its front two wheels) is wheel base away.
      Pose3d GetPostAscentPose(const float wheelBase)  const;
      Pose3d GetPostDescentPose(const float wheelBase) const;
      
      //
      // Inherited Virtual Methods
      //
      virtual ~Ramp();
      
      virtual Ramp*   CloneType() const override;
      virtual void    Visualize(const ColorRGBA& color) const override;
      virtual void    EraseVisualization() const override;
      
      virtual Point3f GetSameDistanceTolerance()  const override;
      
    protected:
      
      // Model dimensions in mm (perhaps these should come from a configuration
      // file instead?)
      constexpr static const f32 Width          = 74.5f;
      constexpr static const f32 Height         = 44.f;
      constexpr static const f32 SlopeLength    = 172.f;
      constexpr static const f32 PlatformLength = 50.f;
      constexpr static const f32 SideMarkerSize = 25.f;
      constexpr static const f32 TopMarkerSize  = 30.f;
      constexpr static const f32 FrontMarkerDistance = 40.f; // along sloped surface (at angle below)
      constexpr static const f32 PreDockDistance    = 90.f; // for picking up from sides
      constexpr static const f32 PreAscentDistance  = 50.f; // for ascending from bottom
      constexpr static const f32 PreDescentDistance = 30.f; // for descending from top
      constexpr static const f32 Angle = 0.31f; // of first part of ramp, using vertex 18
      
      virtual const std::vector<Point3f>& GetCanonicalCorners() const override;
      
      virtual void GeneratePreActionPoses(const PreActionPose::ActionType type,
                                          std::vector<PreActionPose>& preActionPoses) const override;
      
      Point3f _size;
      
      const Vision::KnownMarker* _leftMarker;
      const Vision::KnownMarker* _rightMarker;
      const Vision::KnownMarker* _frontMarker;
      const Vision::KnownMarker* _topMarker;
      
      mutable VizManager::Handle_t _vizHandle;
      //std::array<VizManager::Handle_t,3> _vizHandle;
      
      virtual bool IsPreActionPoseValid(const PreActionPose& preActionPose,
                                        const Pose3d* reachableFromPose,
                                        const std::vector<std::pair<Quad2f,ObjectID> >& obstacles) const override;
      
      
    }; // class Ramp
    
  } // namespace Cozmo
} // namespace Anki

#endif // ANKI_COZMO_BASESTATION_RAMP_H

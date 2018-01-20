/**
 * File: bridge.cpp
 *
 * Author: Andrew Stein
 * Date:   9/15/2014
 *
 * Description: Implements a Bridge object.
 *
 *
 * Copyright: Anki, Inc. 2014
 **/

#include "coretech/common/shared/types.h"

#include "coretech/common/engine/math/pose.h"
#include "coretech/common/engine/math/quad_impl.h"

#include "coretech/vision/shared/MarkerCodeDefinitions.h"

#include "anki/cozmo/shared/cozmoConfig.h"

#include "bridge.h"

namespace Anki {
  namespace Cozmo {
    
    static f32 GetLength(Bridge::Type type)
    {
      static const std::map<Bridge::Type, f32> Lengths = {
        {ObjectType::Bridge_LONG,  300.f},
        {ObjectType::Bridge_SHORT, 200.f}
      };
      
      auto iter = Lengths.find(type);
      if(iter == Lengths.end()) {
        PRINT_NAMED_ERROR("Bridge.GetLength.UnknownBridgeType",
                          "No length defined for bridge type %s (%d).",
                          ObjectTypeToString(type), type);
        return 0.f;
      } else {
        return iter->second;
      }
    } // GetLength()
    
    
    Bridge::Bridge(Type type)
    : ObservableObject(ObjectFamily::Mat, type)
    , MatPiece(type, {GetLength(type), 74.5f, 5.f})
    {
      Vision::MarkerType leftMarkerType = Vision::MARKER_UNKNOWN, rightMarkerType = Vision::MARKER_UNKNOWN, middleMarkerType = Vision::MARKER_UNKNOWN;
      f32 markerSize = 0.f;
      
      if(Type::Bridge_LONG == type) {
        markerSize = 30.f;
        
        //leftMarkerType   = Vision::MARKER_BRIDGESUNLEFT;
        //rightMarkerType  = Vision::MARKER_BRIDGESUNRIGHT;
        //middleMarkerType = Vision::MARKER_BRIDGESUNMIDDLE;
      }
      else if(Type::Bridge_SHORT == type) {
        markerSize = 30.f;
        
        //leftMarkerType   = Vision::MARKER_BRIDGEMOONLEFT;
        //rightMarkerType  = Vision::MARKER_BRIDGEMOONRIGHT;
        //middleMarkerType = Vision::MARKER_BRIDGEMOONMIDDLE;
      }
      else {
        PRINT_NAMED_ERROR("MatPiece.BridgeUnexpectedElse", "Should not get to else in if ladder constructing bridge-type mat.");
        return;
      }
      
      // Don't blindly call virtual GetSize() in the constructor because it may
      // not be the one we want. Explicitly ask for MatPiece's GetSize() implementation
      const Point3f& bridgeSize = MatPiece::GetSize();
      
      //Pose3d leftMarkerPose(-M_PI_2, Z_AXIS_3D(), {-_size.x()*.5f+markerSize, 0.f, _size.z()});
      //leftMarkerPose *= Pose3d(-M_PI_2, X_AXIS_3D(), {0.f, 0.f, 0.f});
      Pose3d leftMarkerPose(-M_PI_2_F, X_AXIS_3D(), {-bridgeSize.x()*.5f+markerSize, 0.f, 0.f});
      
      //Pose3d rightMarkerPose(M_PI_2, Z_AXIS_3D(), { _size.x()*.5f-markerSize, 0.f, _size.z()});
      //rightMarkerPose *= Pose3d(-M_PI_2, X_AXIS_3D(), {0.f, 0.f, 0.f});
      Pose3d rightMarkerPose(-M_PI_2_F, X_AXIS_3D(), { bridgeSize.x()*.5f-markerSize, 0.f, 0.f});
      
      _leftMarker  = &AddMarker(leftMarkerType,  leftMarkerPose,  markerSize);
      _rightMarker = &AddMarker(rightMarkerType, rightMarkerPose, markerSize);
      AddMarker(middleMarkerType, Pose3d(-M_PI_2_F, X_AXIS_3D(), {0.f, 0.f, 0.f}), markerSize);
      
      DEV_ASSERT(_leftMarker != nullptr, "Bridge.Constructor.InvalidLeftMarker");
      DEV_ASSERT(_rightMarker != nullptr, "Bridge.Constructor.InvalidRightMarker");
      
    } // Bridge()
    
    void Bridge::GeneratePreActionPoses(const PreActionPose::ActionType type,
                                        std::vector<PreActionPose>& preActionPoses) const
    {
      preActionPoses.clear();
      
      switch(type)
      {
        case PreActionPose::ActionType::ENTRY:
        {
          // Don't blindly call virtual GetSize() because it may
          // not be the one we want. Explicitly ask for MatPiece's GetSize() implementation
          const Point3f& bridgeSize = MatPiece::GetSize();
        
          Pose3d preCrossingPoseLeft  = Pose3d(0, Z_AXIS_3D(), {-bridgeSize.x()*.5f-30.f, 0.f, 0.f}, GetPose());
          Pose3d preCrossingPoseRight = Pose3d(M_PI, Z_AXIS_3D(), {bridgeSize.x()*.5f+30.f, 0.f, 0.f}, GetPose());
          
          if(preCrossingPoseLeft.GetWithRespectTo(_leftMarker->GetPose(), preCrossingPoseLeft) == false)
          {
            PRINT_NAMED_ERROR("MatPiece.PreCrossingPoseLeftError",
                              "Could not get preCrossingLeftPose w.r.t. left bridge marker.");
          }
          
          if(preCrossingPoseRight.GetWithRespectTo(_rightMarker->GetPose(), preCrossingPoseRight) == false)
          {
            PRINT_NAMED_ERROR("MatPiece.PreCrossingPoseRightError",
                              "Could not get preCrossingRightPose w.r.t. right bridge marker.");
          }
        
          preActionPoses.emplace_back(PreActionPose::ENTRY, _leftMarker,  preCrossingPoseLeft, 0);
          preActionPoses.emplace_back(PreActionPose::ENTRY, _rightMarker, preCrossingPoseRight, 0);
          break;
        }
        case PreActionPose::ActionType::DOCKING:
        case PreActionPose::ActionType::FLIPPING:
        case PreActionPose::ActionType::PLACE_ON_GROUND:
        case PreActionPose::ActionType::PLACE_RELATIVE:
        case PreActionPose::ActionType::ROLLING:
        case PreActionPose::ActionType::NONE:
        {
          break;
        }
      }
    }
    
    
    void Bridge::GetCanonicalUnsafeRegions(const f32 padding_mm,
                                           std::vector<Quad3f>& regions) const
    {
      // Canonical unsafe regions for bridges run up the sides of the bridge
      regions = {{
        Quad3f({-0.5f*GetSize().x(), 0.5f*GetSize().y() + padding_mm, 0.f},
               {-0.5f*GetSize().x(), 0.5f*GetSize().y() - padding_mm, 0.f},
               { 0.5f*GetSize().x(), 0.5f*GetSize().y() + padding_mm, 0.f},
               { 0.5f*GetSize().x(), 0.5f*GetSize().y() - padding_mm, 0.f}),
        Quad3f({-0.5f*GetSize().x(),-0.5f*GetSize().y() + padding_mm, 0.f},
               {-0.5f*GetSize().x(),-0.5f*GetSize().y() - padding_mm, 0.f},
               { 0.5f*GetSize().x(),-0.5f*GetSize().y() + padding_mm, 0.f},
               { 0.5f*GetSize().x(),-0.5f*GetSize().y() - padding_mm, 0.f})
      }};
    }
    
    

    
  } // namespace Cozmo
} // namespace Anki

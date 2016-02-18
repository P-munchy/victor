//
//  activeCube.cpp
//  Products_Cozmo
//
//  Created from block.cpp by Andrew Stein on 10/22/15.
//  Copyright (c) 2015 Anki, Inc. All rights reserved.
//

//#include "anki/common/basestation/math/linearAlgebra_impl.h"

#include "anki/cozmo/basestation/activeCube.h"

//#include "anki/common/basestation/math/quad_impl.h"
#include "anki/common/basestation/math/poseBase_impl.h"
#include "anki/common/basestation/math/point_impl.h"

//#include "anki/cozmo/shared/cozmoConfig.h"
#include "anki/cozmo/shared/cozmoEngineConfig.h"

#if ANKICORETECH_USE_OPENCV
#include "opencv2/imgproc/imgproc.hpp"
#endif

#define SAVE_SET_BLOCK_LIGHTS_MESSAGES_FOR_DEBUG 0

#if SAVE_SET_BLOCK_LIGHTS_MESSAGES_FOR_DEBUG
#  include <fstream>
#endif
#include <iomanip>

namespace Anki {
  namespace Cozmo {

  
    std::vector<RotationMatrix3d> const& ActiveCube::GetRotationAmbiguities() const
    {
      
      // TODO: Adjust if/when active blocks aren't fully ambiguous
      static const std::vector<RotationMatrix3d> RotationAmbiguities; /* = {
        RotationMatrix3d({1,0,0,  0,1,0,  0,0,1}),
        RotationMatrix3d({0,1,0,  1,0,0,  0,0,1}),
        RotationMatrix3d({0,1,0,  0,0,1,  1,0,0}),
        RotationMatrix3d({0,0,1,  0,1,0,  1,0,0}),
        RotationMatrix3d({0,0,1,  1,0,0,  0,1,0}),
        RotationMatrix3d({1,0,0,  0,0,1,  0,1,0})
      };
                                                                       */
      
      return RotationAmbiguities;
    }
    
    ActiveCube::ActiveCube(ObjectType type)
    : Block(ObjectFamily::LightCube, type)
    , _activeID(-1)
    , _factoryID(0)
    {
      // For now, assume 6 different markers, so we can avoid rotation ambiguities
      // Verify that here by making sure a set of markers has as many elements
      // as the original list:
      std::list<Vision::KnownMarker> const& markerList = GetMarkers();
      std::set<Vision::Marker::Code> uniqueCodes;
      for(auto & marker : markerList) {
        uniqueCodes.insert(marker.GetCode());
      }
      CORETECH_ASSERT(uniqueCodes.size() == markerList.size());
    }
    
    ActiveCube::ActiveCube(ActiveID activeID, FactoryID factoryID)
    : ActiveCube(GetTypeFromFactoryID(factoryID))
    {
      _activeID = activeID;
      _factoryID = factoryID;
    }
    
    // Changes to this mapping should also be reflected in ActiveBlock::activeIDToFactoryIDMap_
    ActiveCube::Type ActiveCube::GetTypeFromFactoryID(u32 id)
    {
      static constexpr ObjectType factoryIDToObjectType[4] = {
        ObjectType::Block_LIGHTCUBE3,
        ObjectType::Block_LIGHTCUBE2,
        ObjectType::Block_LIGHTCUBE1,
        ObjectType::Block_LIGHTCUBE4
      };
      
      u8 typeID = id & 0x3;
      return factoryIDToObjectType[typeID];
    }
  
    void ActiveCube::SetLEDs(const WhichCubeLEDs whichLEDs,
                             const ColorRGBA& onColor,
                             const ColorRGBA& offColor,
                             const u32 onPeriod_ms,
                             const u32 offPeriod_ms,
                             const u32 transitionOnPeriod_ms,
                             const u32 transitionOffPeriod_ms,
                             const bool turnOffUnspecifiedLEDs)
    {
      static const u8 FIRST_BIT = 0x01;
      u8 shiftedLEDs = static_cast<u8>(whichLEDs);
      for(u8 iLED=0; iLED<NUM_LEDS; ++iLED) {
        // If this LED is specified in whichLEDs (its bit is set), then
        // update
        if(shiftedLEDs & FIRST_BIT) {
          _ledState[iLED].onColor      = onColor;
          _ledState[iLED].offColor     = offColor;
          _ledState[iLED].onPeriod_ms  = onPeriod_ms;
          _ledState[iLED].offPeriod_ms = offPeriod_ms;
          _ledState[iLED].transitionOnPeriod_ms = transitionOnPeriod_ms;
          _ledState[iLED].transitionOffPeriod_ms = transitionOffPeriod_ms;
        } else if(turnOffUnspecifiedLEDs) {
          _ledState[iLED].onColor      = ::Anki::NamedColors::BLACK;
          _ledState[iLED].offColor     = ::Anki::NamedColors::BLACK;
          _ledState[iLED].onPeriod_ms  = 1000;
          _ledState[iLED].offPeriod_ms = 1000;
          _ledState[iLED].transitionOnPeriod_ms = 0;
          _ledState[iLED].transitionOffPeriod_ms = 0;
        }
        shiftedLEDs = shiftedLEDs >> 1;
      }
    }
    
    void ActiveCube::SetLEDs(const std::array<u32,NUM_LEDS>& onColors,
                             const std::array<u32,NUM_LEDS>& offColors,
                             const std::array<u32,NUM_LEDS>& onPeriods_ms,
                             const std::array<u32,NUM_LEDS>& offPeriods_ms,
                             const std::array<u32,NUM_LEDS>& transitionOnPeriods_ms,
                             const std::array<u32,NUM_LEDS>& transitionOffPeriods_ms)
    {
      for(u8 iLED=0; iLED<NUM_LEDS; ++iLED) {
        _ledState[iLED].onColor      = onColors[iLED];
        _ledState[iLED].offColor     = offColors[iLED];
        _ledState[iLED].onPeriod_ms  = onPeriods_ms[iLED];
        _ledState[iLED].offPeriod_ms = offPeriods_ms[iLED];

        // Handle some special cases (we want to avoid on/off times of 0 for the
        // sake of the real active blocks)
        if(onPeriods_ms[iLED] == 0 && offPeriods_ms[iLED] > 0) {
          // Looks like we mean for this LED to be solid "off" color
          _ledState[iLED].onColor = offColors[iLED];
          _ledState[iLED].onPeriod_ms = u32_MAX/2;
        }
        else if(offPeriods_ms[iLED] == 0 && onPeriods_ms[iLED] > 0) {
          // Looks like we mean for this LED to be solid "on" color
          _ledState[iLED].offColor = onColors[iLED];
          _ledState[iLED].offPeriod_ms = u32_MAX/2;
        }
        else if(onPeriods_ms[iLED]==0 && offPeriods_ms[iLED]==0) {
          // Looks like we mean for this LED to actually turn off
          _ledState[iLED].onColor = 0;
          _ledState[iLED].offColor = 0;
          _ledState[iLED].onPeriod_ms = u32_MAX/2;
          _ledState[iLED].offPeriod_ms = u32_MAX/2;
        }
        
        _ledState[iLED].transitionOnPeriod_ms = transitionOnPeriods_ms[iLED];
        _ledState[iLED].transitionOffPeriod_ms = transitionOffPeriods_ms[iLED];
      }
    }
    
    void ActiveCube::MakeStateRelativeToXY(const Point2f& xyPosition, MakeRelativeMode mode)
    {
      WhichCubeLEDs referenceLED = WhichCubeLEDs::NONE;
      switch(mode)
      {
        case MakeRelativeMode::RELATIVE_LED_MODE_OFF:
          // Nothing to do
          return;
          
        case MakeRelativeMode::RELATIVE_LED_MODE_BY_CORNER:
          referenceLED = GetCornerClosestToXY(xyPosition);
          break;
          
        case MakeRelativeMode::RELATIVE_LED_MODE_BY_SIDE:
          referenceLED = GetFaceClosestToXY(xyPosition);
          break;
          
        default:
          PRINT_NAMED_ERROR("ActiveCube.MakeStateRelativeToXY", "Unrecognized relateive LED mode %s.", MakeRelativeModeToString(mode));
          return;
      }
      
      switch(referenceLED)
      {
        //
        // When using upper left corner (of current top face) as reference corner:
        //
        //   OR
        //
        // When using upper side (of current top face) as reference side:
        // (Note this is the current "Left" face of the block.)
        //
          
        case WhichCubeLEDs::FRONT_RIGHT:
        case WhichCubeLEDs::FRONT:
          // Nothing to do
          return;
          
        case WhichCubeLEDs::FRONT_LEFT:
        case WhichCubeLEDs::LEFT:
          // Rotate clockwise one slot
          RotatePatternAroundTopFace(true);
          return;
          
        case WhichCubeLEDs::BACK_RIGHT:
        case WhichCubeLEDs::RIGHT:
          // Rotate counterclockwise one slot
          RotatePatternAroundTopFace(false);
          return;
          
        case WhichCubeLEDs::BACK_LEFT:
        case WhichCubeLEDs::BACK:
          // Rotate two slots (either direction)
          // TODO: Do this in one shot
          RotatePatternAroundTopFace(true);
          RotatePatternAroundTopFace(true);
          return;
          
        default:
          PRINT_STREAM_ERROR("ActiveCube.MakeStateRelativeToXY",
                            "Unexpected reference LED " << static_cast<int>(referenceLED) << ".");
          return;
      }
    } // MakeStateRelativeToXY()
    
    WhichCubeLEDs ActiveCube::MakeWhichLEDsRelativeToXY(const WhichCubeLEDs whichLEDs,
                                                         const Point2f& xyPosition,
                                                         MakeRelativeMode mode) const
    {
      WhichCubeLEDs referenceLED = WhichCubeLEDs::NONE;
      switch(mode)
      {
        case MakeRelativeMode::RELATIVE_LED_MODE_OFF:
          // Nothing to do
          return whichLEDs;
          
        case MakeRelativeMode::RELATIVE_LED_MODE_BY_CORNER:
          referenceLED = GetCornerClosestToXY(xyPosition);
          break;
          
        case MakeRelativeMode::RELATIVE_LED_MODE_BY_SIDE:
          referenceLED = GetFaceClosestToXY(xyPosition);
          break;
          
        default:
          PRINT_NAMED_ERROR("ActiveCube.MakeStateRelativeToXY", "Unrecognized relateive LED mode %s.", MakeRelativeModeToString(mode));
          return whichLEDs;
      }
      
      switch(referenceLED)
      {
        //
        // When using upper left corner (of current top face) as reference corner:
        //
        //  OR
        //
        // When using upper side (of current top face) as reference side:
        // (Note this is the current "Left" face of the block.)
        //
        
        case WhichCubeLEDs::FRONT_RIGHT:
        case WhichCubeLEDs::FRONT:
          // Nothing to do
          return whichLEDs;
          
        case WhichCubeLEDs::FRONT_LEFT:
        case WhichCubeLEDs::LEFT:
          // Rotate clockwise one slot
          return RotateWhichLEDsAroundTopFace(whichLEDs, true);
          
        case WhichCubeLEDs::BACK_RIGHT:
        case WhichCubeLEDs::RIGHT:
          // Rotate counterclockwise one slot
          return RotateWhichLEDsAroundTopFace(whichLEDs, false);

        case WhichCubeLEDs::BACK_LEFT:
        case WhichCubeLEDs::BACK:
          // Rotate two slots (either direction)
          // TODO: Do this in one shot
          return RotateWhichLEDsAroundTopFace(RotateWhichLEDsAroundTopFace(whichLEDs, true), true);
    
        default:
          PRINT_STREAM_ERROR("ActiveCube.MakeStateRelativeToXY",
                            "Unexpected reference LED " << static_cast<int>(referenceLED) << ".");
          return whichLEDs;
      }
    } // MakeWhichLEDsRelativeToXY()
    
    /*
    void ActiveCube::TurnOffAllLEDs()
    {
      SetLEDs(WhichCubeLEDs::ALL, NamedColors::BLACK, 0, 0);
    }
     */
    
    bool ActiveCube::CanBeUsedForLocalization() const
    {
      return (GetPoseState() == ObservableObject::PoseState::Known &&
              GetLastPoseUpdateDistance() >= 0.f &&
              GetLastPoseUpdateDistance() <= MAX_LOCALIZATION_AND_ID_DISTANCE_MM &&
              GetIdentityState() == ActiveIdentityState::Identified &&
              IsRestingFlat());
    }
    
    void ActiveCube::Identify()
    {
      if(_identificationTimer > 0) {
        _identificationTimer -= BS_TIME_STEP;
        _identityState = ActiveIdentityState::WaitingForIdentity;
        PRINT_NAMED_INFO("ActiveCube.Identify.Waiting",
                         "Faking identification time for object %d",
                         GetID().GetValue());
      } else if(GetLastPoseUpdateDistance() > MAX_LOCALIZATION_AND_ID_DISTANCE_MM) {
        PRINT_NAMED_INFO("ActiveCube.Identify.TooFar",
                         "Too far to identify object %d (%.1fmm > %.1fmm)",
                         GetID().GetValue(), GetLastPoseUpdateDistance(),
                         MAX_LOCALIZATION_AND_ID_DISTANCE_MM);
      } else {
        // TODO: Actually get activeID from flashing LEDs instead of using a single hard-coded value
        switch(_markers.front().GetCode())
        {
          case Vision::MARKER_1:
          case Vision::MARKER_LIGHTNINGBOLT_01:
            _identityState = ActiveIdentityState::Identified;
            break;
            
          case Vision::MARKER_INVERTED_1:
          case Vision::MARKER_LIGHTNINGBOLTHOLLOW_01:
            _identityState = ActiveIdentityState::Identified;
            break;
            
          case Vision::MARKER_INVERTED_LIGHTNINGBOLT_01:
            _identityState = ActiveIdentityState::Identified;
            break;
            
          case Vision::MARKER_INVERTED_LIGHTNINGBOLTHOLLOW_01:
            _identityState = ActiveIdentityState::Identified;
            break;
            
          default:
            _identityState = ActiveIdentityState::Unidentified;
            PRINT_NAMED_ERROR("ActiveCube.Identify.UnknownID",
                              "ActiveID not defined for block with front marker = %d\n",
                              _markers.front().GetCode());
        }
        
        _identificationTimer = ID_TIME_MS;
      }
    } // Identify()
    
    std::map<s32,bool>& ActiveCube::GetAvailableIDs()
    {
      static std::map<s32,bool> availableIDs;
      return availableIDs;
    }
    
    void ActiveCube::RegisterAvailableID(s32 activeID)
    {
      if(ActiveCube::GetAvailableIDs().count(activeID) > 0) {
        PRINT_NAMED_WARNING("ActiveCube.RegisterAvailableID",
                            "Ignoring duplicate registration of available ID %d.\n", activeID);
      } else {
        ActiveCube::GetAvailableIDs()[activeID] = false;
      }
      
    }
    
    void ActiveCube::ClearAvailableIDs()
    {
      ActiveCube::GetAvailableIDs().clear();
    }
    
    WhichCubeLEDs ActiveCube::GetCornerClosestToXY(const Point2f& xyPosition) const
    {
      // Get a vector from center of marker in its current pose to given xyPosition
      Pose3d topMarkerPose;
      const Vision::KnownMarker& topMarker = GetTopMarker(topMarkerPose);
      const Vec2f topMarkerCenter(topMarkerPose.GetTranslation());
      Vec2f v(xyPosition);
      v -= topMarkerCenter;
      
      if (topMarker.GetCode() != GetMarker(FaceName::TOP_FACE).GetCode()) {
        PRINT_NAMED_WARNING("ActiveCube.GetCornerClosestToXY.IgnoringBecauseBlockOnSide", "");
        return WhichCubeLEDs::FRONT_LEFT;
      }
      
      PRINT_STREAM_INFO("ActiveCube.GetCornerClosestToXY", "ActiveCube " << GetID().GetValue() << "'s TopMarker is = " << Vision::MarkerTypeStrings[topMarker.GetCode()] << ", angle = " << std::setprecision(3) << topMarkerPose.GetRotation().GetAngleAroundZaxis().getDegrees() << "deg");
      
      Radians angle = std::atan2(v.y(), v.x());
      angle -= topMarkerPose.GetRotationAngle<'Z'>();
      //assert(angle >= -M_PI && angle <= M_PI); // No longer needed: Radians class handles this
      
      WhichCubeLEDs whichLEDs = WhichCubeLEDs::NONE;
      if(angle > 0.f) {
        if(angle < M_PI_2) {
          // Between 0 and 90 degrees: Upper Right Corner
          PRINT_STREAM_INFO("ActiveCube.GetCornerClosestToXY", "Angle = " << std::setprecision(3) << angle.getDegrees() <<  "deg, Closest corner to (" << xyPosition.x() << "," << xyPosition.y() << "): Back Left");
          whichLEDs = WhichCubeLEDs::BACK_LEFT;
        } else {
          // Between 90 and 180: Upper Left Corner
          //assert(angle<=M_PI);
          PRINT_STREAM_INFO("ActiveCube.GetCornerClosestToXY", "Angle = " << std::setprecision(3) << angle.getDegrees() <<  "deg, Closest corner to (" << xyPosition.x() << "," << xyPosition.y() << "): Front Left");
          whichLEDs = WhichCubeLEDs::FRONT_LEFT;
        }
      } else {
        //assert(angle >= -M_PI);
        if(angle > -M_PI_2) {
          // Between -90 and 0: Lower Right Corner
          PRINT_STREAM_INFO("ActiveCube.GetCornerClosestToXY", "Angle = " << std::setprecision(3) << angle.getDegrees() <<  "deg, Closest corner to (" << xyPosition.x() << "," << xyPosition.y() << "): Back Right");
          whichLEDs = WhichCubeLEDs::BACK_RIGHT;
        } else {
          // Between -90 and -180: Lower Left Corner
          //assert(angle >= -M_PI);
          PRINT_STREAM_INFO("ActiveCube.GetCornerClosestToXY", "Angle = " << std::setprecision(3) << angle.getDegrees() <<  "deg, Closest corner to (" << xyPosition.x() << "," << xyPosition.y() << "): Front Right");
          whichLEDs = WhichCubeLEDs::FRONT_RIGHT;
        }
      }
      
      return whichLEDs;
    } // GetCornerClosestToXY()
    
    WhichCubeLEDs ActiveCube::GetFaceClosestToXY(const Point2f& xyPosition) const
    {
      // Get a vector from center of marker in its current pose to given xyPosition
      Pose3d topMarkerPose;
      const Vision::KnownMarker& topMarker = GetTopMarker(topMarkerPose);
      const Vec3f topMarkerCenter(topMarkerPose.GetTranslation());
      const Vec2f v(xyPosition.x()-topMarkerCenter.x(), xyPosition.y()-topMarkerCenter.y());
      
      if (topMarker.GetCode() != GetMarker(FaceName::TOP_FACE).GetCode()) {
        PRINT_NAMED_WARNING("ActiveCube.GetFaceClosestToXY.IgnoringBecauseBlockOnSide", "");
        return WhichCubeLEDs::FRONT;
      }
      
      
      PRINT_STREAM_INFO("ActiveCube.GetFaceClosestToXY", "ActiveCube " << GetID().GetValue() << "'s TopMarker is = " << Vision::MarkerTypeStrings[topMarker.GetCode()] << ", angle = " << std::setprecision(3) << topMarkerPose.GetRotation().GetAngleAroundZaxis().getDegrees() << "deg");
      
      Radians angle = std::atan2(v.y(), v.x());
      angle -= topMarkerPose.GetRotationAngle<'Z'>();
      
      WhichCubeLEDs whichLEDs = WhichCubeLEDs::NONE;
      if(angle < M_PI_4 && angle >= -M_PI_4) {
        // Between -45 and 45 degrees: Right Face
        whichLEDs = WhichCubeLEDs::BACK;
        PRINT_STREAM_INFO("ActiveCube.GetFaceClosestToXY", "Angle = " << std::setprecision(3) << angle.getDegrees() <<  "deg, Closest face to (" << xyPosition.x() << "," << xyPosition.y() << "): Back");
      }
      else if(angle < 3*M_PI_4 && angle >= M_PI_4) {
        // Between 45 and 135 degrees: Back Face
        whichLEDs = WhichCubeLEDs::LEFT;
        PRINT_STREAM_INFO("ActiveCube.GetFaceClosestToXY", "Angle = " << std::setprecision(3) << angle.getDegrees() <<  "deg, Closest face to (" << xyPosition.x() << "," << xyPosition.y() << "): Left");
      }
      else if(angle < -M_PI_4 && angle >= -3*M_PI_4) {
        // Between -45 and -135: Front Face
        whichLEDs = WhichCubeLEDs::RIGHT;
        PRINT_STREAM_INFO("ActiveCube.GetFaceClosestToXY", "Angle = " << std::setprecision(3) << angle.getDegrees() <<  "deg, Closest face to (" << xyPosition.x() << "," << xyPosition.y() << "): Right");
      }
      else {
        // Between -135 && +135: Left Face
        assert(angle < -3*M_PI_4 || angle > 3*M_PI_4);
        whichLEDs = WhichCubeLEDs::FRONT;
        PRINT_STREAM_INFO("ActiveCube.GetFaceClosestToXY", "Angle = " << std::setprecision(3) << angle.getDegrees() <<  "deg, Closest face to (" << xyPosition.x() << "," << xyPosition.y() << "): Front");
      }
      
      return whichLEDs;
    } // GetFaceClosestToXY()
    
/*
    WhichCubeLEDs ActiveCube::RotatePatternAroundTopFace(WhichCubeLEDs pattern, bool clockwise)
    {
      static const u8 MASK = 0x88; // 0b10001000
      const u8 oldPattern = static_cast<u8>(pattern);
      if(clockwise) {
        return static_cast<WhichCubeLEDs>( ((oldPattern << 1) & ~MASK) | ((oldPattern & MASK) >> 3) );
      } else {
        return static_cast<WhichCubeLEDs>( ((oldPattern >> 1) & ~MASK) | ((oldPattern & MASK) << 3) );
      }
    }
  */
    
    inline const u8* GetRotationLUT(bool clockwise)
    {
      static const u8 cwRotatedPosition[ActiveCube::NUM_LEDS] = {
        3, 0, 1, 2
      };
      static const u8 ccwRotatedPosition[ActiveCube::NUM_LEDS] = {
        1, 2, 3, 0
      };
      
      // Choose the appropriate LUT
      const u8* rotatedPosition = (clockwise ? cwRotatedPosition : ccwRotatedPosition);
      
      return rotatedPosition;
    }
    
    void ActiveCube::RotatePatternAroundTopFace(bool clockwise)
    {
      const u8* rotatedPosition = GetRotationLUT(clockwise);
      
      // Create the new state array
      std::array<LEDstate,NUM_LEDS> newState;
      for(u8 iLED=0; iLED<NUM_LEDS; ++iLED) {
        newState[rotatedPosition[iLED]] = _ledState[iLED];
      }
      
      // Swap new state into place
      std::swap(newState, _ledState);
    } // RotatePatternAroundTopFace()
    
    
    WhichCubeLEDs ActiveCube::RotateWhichLEDsAroundTopFace(WhichCubeLEDs whichLEDs, bool clockwise)
    {
      const u8* rotatedPosition = GetRotationLUT(clockwise);
      
      u8 rotatedWhichLEDs = 0;
      u8 currentBit = 1;
      for(u8 iLED=0; iLED<NUM_LEDS; ++iLED) {
        // Set the corresponding rotated bit if the current bit is set
        rotatedWhichLEDs |= ((currentBit & (u8)whichLEDs)>0) << rotatedPosition[iLED];
        currentBit = currentBit << 1;
      }

      return (WhichCubeLEDs)rotatedWhichLEDs;
    }
    
/*
    void ActiveCube::FillMessage(MessageSetBlockLights& m) const
    {
      m.blockID = _activeID;
      
      assert(m.onPeriod_ms.size() == NUM_LEDS);
      assert(m.offPeriod_ms.size() == NUM_LEDS);
      assert(m.onColor.size() == NUM_LEDS);
      assert(m.offColor.size() == NUM_LEDS);
      
      for(u8 iLED=0; iLED<NUM_LEDS; ++iLED) {
        m.onColor[iLED] = _ledState[iLED].onColor;
        m.offColor[iLED] = _ledState[iLED].offColor;
        m.onPeriod_ms[iLED]  = _ledState[iLED].onPeriod_ms;
        m.offPeriod_ms[iLED] = _ledState[iLED].offPeriod_ms;
        m.transitionOnPeriod_ms[iLED]  = _ledState[iLED].transitionOnPeriod_ms;
        m.transitionOffPeriod_ms[iLED] = _ledState[iLED].transitionOffPeriod_ms;
      }
      
#     if SAVE_SET_BLOCK_LIGHTS_MESSAGES_FOR_DEBUG
      {
        static int saveCtr=0;
        Json::Value jsonMsg = m.CreateJson();
        std::ofstream jsonFile("SetBlockLights_" + std::to_string(saveCtr++) + ".json", std::ofstream::out);
        fprintf(stdout, "Writing SetBlockLights message to JSON file.\n");
        jsonFile << jsonMsg.toStyledString();
        jsonFile.close();
      }
#     endif 
      
    }
*/

  } // namespace Cozmo
} // namespace Anki
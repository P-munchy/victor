//
//  activeCube.h
//  Products_Cozmo
//
//  Created from block.h by Andrew Stein on 10/22/15.
//  Copyright (c) 2015 Anki, Inc. All rights reserved.
//

#ifndef __Anki_Cozmo_ActiveCube_H__
#define __Anki_Cozmo_ActiveCube_H__

#include "anki/cozmo/basestation/block.h"

namespace Anki {
  
// Forward Declarations:
class Camera;

namespace Cozmo {
  
  class ActiveCube : public Block
  {
  public:
    static const s32 NUM_LEDS = 4;
    
    ActiveCube(Type type);
    ActiveCube(ActiveID activeID, FactoryID factoryID);
    
    static Type GetTypeFromFactoryID(FactoryID id);
    
    virtual std::vector<RotationMatrix3d> const& GetRotationAmbiguities() const override;
    
    virtual ActiveCube* CloneType() const override {
      return new ActiveCube(this->_type);
    }
    
    virtual bool IsActive() const override  { return true; }
    
    // This overrides ObservableObject::SetPose to mark this object as localized
    // anytime its pose is set
    void SetPose(const Pose3d& newPose);
    
    // Set the same color and flashing frequency of one or more LEDs on the block
    // If turnOffUnspecifiedLEDs is true, any LEDs that were not indicated by
    // whichLEDs will be turned off. Otherwise, they will be left in their current
    // state.
    // NOTE: Alpha is ignored.
    void SetLEDs(const WhichCubeLEDs whichLEDs,
                 const ColorRGBA& onColor,        const ColorRGBA& offColor,
                 const u32 onPeriod_ms,           const u32 offPeriod_ms,
                 const u32 transitionOnPeriod_ms, const u32 transitionOffPeriod_ms,
                 const bool turnOffUnspecifiedLEDs);
    
    // Specify individual colors and flash frequencies for all the LEDS of the block
    // The index of the arrays matches the diagram above.
    // NOTE: Alpha is ignored
    void SetLEDs(const std::array<u32,NUM_LEDS>& onColors,
                 const std::array<u32,NUM_LEDS>& offColors,
                 const std::array<u32,NUM_LEDS>& onPeriods_ms,
                 const std::array<u32,NUM_LEDS>& offPeriods_ms,
                 const std::array<u32,NUM_LEDS>& transitionOnPeriods_ms,
                 const std::array<u32,NUM_LEDS>& transitionOffPeriods_ms);
    
    // Make whatever state has been set on the block relative to a given (x,y)
    //  location.
    // When byUpperLeftCorner=true, "relative" means that the pattern is rotated
    //  so that whatever is currently specified for LED 0 is applied to the LED
    //  currently closest to the given position
    // When byUpperLeftCorner=false, "relative" means that the pattern is rotated
    //  so that whatever is specified for the side with LEDs 0 and 4 is applied
    //  to the face currently closest to the given position
    void MakeStateRelativeToXY(const Point2f& xyPosition, MakeRelativeMode mode);
    
    // Similar to above, but returns rotated WhichCubeLEDs rather than changing
    // the block's current state.
    WhichCubeLEDs MakeWhichLEDsRelativeToXY(const WhichCubeLEDs whichLEDs,
                                             const Point2f& xyPosition,
                                             MakeRelativeMode mode) const;   
    
    // If object is moving, returns true and the time that it started moving in t.
    // If not moving, returns false and the time that it stopped moving in t.
    virtual bool IsMoving(TimeStamp_t* t = nullptr) const override { if (t) *t=_movingTime; return _isMoving; }
    
    // Set the moving state of the object and when it either started or stopped moving.
    virtual void SetIsMoving(bool isMoving, TimeStamp_t t) override { _isMoving = isMoving; _movingTime = t;}
    
    virtual bool CanBeUsedForLocalization() const override;

    
    
    
    // Take the given top LED pattern and create a pattern that indicates
    // the corresponding bottom LEDs as well
    static WhichCubeLEDs MakeTopAndBottomPattern(WhichCubeLEDs topPattern);
    
    // Get the LED specification for the top (and bottom) LEDs on the corner closest
    // to the specified (x,y) position, using the ActiveCube's current pose.
    WhichCubeLEDs GetCornerClosestToXY(const Point2f& xyPosition) const;
    
    // Get the LED specification for the four LEDs on the face closest
    // to the specified (x,y) position, using the ActiveCube's current pose.
    WhichCubeLEDs GetFaceClosestToXY(const Point2f& xyPosition) const;
    
    // Rotate the currently specified pattern of colors/flashing once slot in
    // the specified direction (assuming you are looking down at the top face)
    void RotatePatternAroundTopFace(bool clockwise);
    
    // Helper for figuring out which LEDs will be selected after rotating
    // a given pattern of LEDs one slot in the specified direction
    static WhichCubeLEDs RotateWhichLEDsAroundTopFace(WhichCubeLEDs whichLEDs, bool clockwise);
    
    // Populate a message specifying the current state of the block, for sending
    // out to actually set the physical block to match
    //void FillMessage(SetBlockLights& msg) const;


    const LEDstate& GetLEDState(s32 whichLED) const;
    
  protected:
    
    bool        _isMoving = false;
    TimeStamp_t _movingTime = 0;
    
    // Keep track of flash rate and color of each LED
    std::array<LEDstate,NUM_LEDS> _ledState;
    
    // Temporary timer for faking duration of identification process
    // TODO: Remove once real identification is implemented
    static const s32 ID_TIME_MS = 300;
    s32 _identificationTimer = ID_TIME_MS;
    
  }; // class ActiveCube
  

#pragma mark --- Inline Accessors Implementations ---
  
  inline WhichCubeLEDs ActiveCube::MakeTopAndBottomPattern(WhichCubeLEDs topPattern) {
    u8 pattern = static_cast<u8>(topPattern);
    return static_cast<WhichCubeLEDs>((pattern << 4) + (pattern & 0x0F));
  }
  
  inline const ActiveCube::LEDstate& ActiveCube::GetLEDState(s32 whichLED) const
  {
    if(whichLED >= NUM_LEDS) {
      PRINT_NAMED_WARNING("ActiveCube.GetLEDState.IndexTooLarge",
                          "Requested LED index is too large (%d > %d). Returning %d.",
                          whichLED, NUM_LEDS-1, NUM_LEDS-1);
      whichLED = NUM_LEDS-1;
    } else if(whichLED < 0) {
      PRINT_NAMED_WARNING("ActiveCube.GetLEDState.NegativeIndex",
                          "LED index should be >= 0, not %d. Using 0.", whichLED);
      whichLED = 0;
    }
    return _ledState[whichLED];
  }
  
} // namespace Cozmo
} // namespace Anki

#endif // __Anki_Cozmo_ActiveCube_H__

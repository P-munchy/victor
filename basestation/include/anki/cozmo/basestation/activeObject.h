/**
 * File: ActiveObject.h
 *
 * Author: Kevin Yoon
 * Date:   5/19/2016
 *
 * Description: Defines an active object (i.e. one that has a radio connection and has lights that can be set)
 *
 *
 * Copyright: Anki, Inc. 2016
 **/

#include "anki/cozmo/basestation/cozmoObservableObject.h"
#include "clad/types/activeObjectTypes.h"

#ifndef __Anki_Cozmo_ActiveObject_H__
#define __Anki_Cozmo_ActiveObject_H__

namespace Anki {
  
namespace Cozmo {
  
  class ActiveObject : public virtual ObservableObject  // NOTE: Cozmo::ObservableObject, not Vision::
  {
  public:
    static const s32 NUM_LEDS = 4;
    
    ActiveObject(){};
    
    virtual bool IsActive() const override  { return true; }
    
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
    

    // If object is moving, returns true and the time that it started moving in t.
    // If not moving, returns false and the time that it stopped moving in t.
    virtual bool IsMoving(TimeStamp_t* t = nullptr) const override { if (t) *t=_movingTime; return _isMoving; }
    
    // Set the moving state of the object and when it either started or stopped moving.
    virtual void SetIsMoving(bool isMoving, TimeStamp_t t) override { _isMoving = isMoving; _movingTime = t;}
    
    
    // Mapping of ActiveObjectType to ObjectType
    static ObjectType GetTypeFromActiveObjectType(ActiveObjectType type) {
      ObjectType objType = ObjectType::Unknown;
      switch(type) {
        case ActiveObjectType::OBJECT_CHARGER:
          objType = ObjectType::Charger_Basic;
          break;
        case ActiveObjectType::OBJECT_CUBE1:
          objType = ObjectType::Block_LIGHTCUBE1;
          break;
        case ActiveObjectType::OBJECT_CUBE2:
          objType = ObjectType::Block_LIGHTCUBE2;
          break;
        case ActiveObjectType::OBJECT_CUBE3:
          objType = ObjectType::Block_LIGHTCUBE3;
          break;
        default:
          break;
      }
      
      return objType;
    }
    
    struct LEDstate {
      ColorRGBA onColor;
      ColorRGBA offColor;
      u32       onPeriod_ms;
      u32       offPeriod_ms;
      u32       transitionOnPeriod_ms;
      u32       transitionOffPeriod_ms;
      
      LEDstate()
      : onColor(0), offColor(0), onPeriod_ms(0), offPeriod_ms(0)
      , transitionOnPeriod_ms(0), transitionOffPeriod_ms(0)
      {
        
      }
    };
    const LEDstate& GetLEDState(s32 whichLED) const;
    
  protected:
    
    bool        _isMoving = false;
    TimeStamp_t _movingTime = 0;
    
    // Keep track of flash rate and color of each LED
    std::array<LEDstate,NUM_LEDS> _ledState;
    
    // Scaled version of _ledState so that it's playable by hardware without corrupting colors
    std::array<LEDstate,NUM_LEDS> _scaledLedState;
    
    void ScaleLEDValuesForHardware();
    
  }; // class ActiveObject
  

#pragma mark --- Inline Accessors Implementations ---
  
  inline const ActiveObject::LEDstate& ActiveObject::GetLEDState(s32 whichLED) const
  {
    if(whichLED >= NUM_LEDS) {
      PRINT_NAMED_WARNING("ActiveObject.GetLEDState.IndexTooLarge",
                          "Requested LED index is too large (%d > %d). Returning %d.",
                          whichLED, NUM_LEDS-1, NUM_LEDS-1);
      whichLED = NUM_LEDS-1;
    } else if(whichLED < 0) {
      PRINT_NAMED_WARNING("ActiveObject.GetLEDState.NegativeIndex",
                          "LED index should be >= 0, not %d. Using 0.", whichLED);
      whichLED = 0;
    }
    return _scaledLedState[whichLED];
  }
  
} // namespace Cozmo
} // namespace Anki

#endif // __Anki_Cozmo_ActiveObject_H__

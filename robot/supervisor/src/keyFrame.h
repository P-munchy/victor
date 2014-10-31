/**
 * File: keyFrame.h
 *
 * Author: Andrew Stein
 * Created: 10/16/2014
 *
 * Description:
 *
 *   Defines a KeyFrame for animations.
 *
 * Copyright: Anki, Inc. 2014
 *
 **/

#ifndef ANKI_COZMO_ROBOT_KEYFRAME_H
#define ANKI_COZMO_ROBOT_KEYFRAME_H

#include "anki/cozmo/robot/hal.h"
#include "anki/cozmo/shared/cozmoTypes.h"


namespace Anki {
namespace Cozmo {

struct KeyFrame
{
  // Add a new KeyFrame Type by adding it to this enumerated list and then
  // defining it as a new struct in the union below.
  enum Type
  {
    HEAD_ANGLE = 0,
    START_HEAD_NOD,
    STOP_HEAD_NOD,
    LIFT_HEIGHT,
    DRIVE_LINE_SEGMENT,
    DRIVE_ARC,
    BACK_AND_FORTH,
    START_WIGGLE,
    POINT_TURN,
    PLAY_SOUND,
    WAIT_FOR_SOUND, // basically a no-op to allow sound to finish if no other keyframes
    STOP_SOUND,
    BLINK_EYES,
    FLASH_EYES,
    SPIN_EYES,
    STOP_EYES, // end any eye animation
    SET_EYE,
    // SET_LED_COLORS,
    START_LIFT_NOD,
    STOP_LIFT_NOD,
    TRIGGER_ANIMATION,
    NUM_TYPES
  };

  
  // Common Keyframe elements:
  Type           type;
  u16            relTime_ms; // time relative to first keyframe
  
  KeyFrameTransitionType transitionIn;
  KeyFrameTransitionType transitionOut;
  
  // Define all the possible type-specific KeyFrame info as structs here, and
  // then create an "instance" of each of these in the union below. It would
  // be cleaner to do an anonymous definition inside the union directly, but
  // Keil does not allow that.
  
  // Directly set the head's angle and speed
  struct SetHeadAngle_t {
    s8 angle_deg;
  };
  
  // Command a canned head nodding action between two angles
  // Must be used in conjunction with a StopHeadNod_t keyframe after it.
  struct StartHeadNod_t {
    s8  lowAngle_deg;
    s8  highAngle_deg;
    u16 period_ms;
  };
  
  struct StopHeadNod_t {
    s8 finalAngle_deg;
  };
  
  // Directly set lift's height and speed
  struct SetLiftHeight_t {
    u16 targetHeight; // mm
    u16 targetSpeed;  // mm/s
  };
  
  // Command a canned lift nodding action between two heights
  // Must be used in conjunction with a StopLiftNod_t keyframe after it.
  struct StartLiftNod_t {
    u8  lowHeight;  // mm
    u8  highHeight; // mm
    u16 period_ms;
  };
  
  struct StopLiftNod_t {
    u8 finalHeight;
  };
  
  struct DriveLineSegment_t {
    s16 relativeDistance; // in mm, +ve for fwd, -ve for backward
  };
  
  struct DriveArc_t {
    u8  radius_mm;
    s16 sweepAngle_deg; // +ve arcs left, -ve arcs right
  };
  
  // Drive forward and backward primitive
  // (Can use different forward/backward distances to get a net "shimmy"
  //  forward or backward)
  struct BackAndForth_t {
    u16 period_ms;
    u8  forwardDist_mm;
    u8  backwardDist_mm;
  };
  
  // Side-to-side body wiggle primitive
  // (Can use different left/right angles to get a net "shimmy" left or right)
  struct StartWiggle_t {
    u16 period_ms;
    s8  leftAngle_deg;
    s8  rightAngle_deg;
  };
  
  struct StopWiggle_t {
    
  };
  
  // Turn in place primitive
  struct TurnInPlace_t {
    s16 relativeAngle_deg; // +ve turns left, -ve turns right
  };
  
  struct PlaySound_t {
    u16 soundID;
    u8  numLoops;
  };
  
  // Turn eye(s) off and back on in specified color, using a built-in blink animation
  struct BlinkEyes_t {
    u16      timeOn_ms;
    u16      timeOff_ms;
    u32      color;
    u8       variability_ms;
  };
  
  // Flash eyes
  struct FlashEyes_t {
    u16 timeOn_ms;
    u16 timeOff_ms;
    u32 color;
    EyeShape shape;
  };
  
  struct SetEye_t {
    WhichEye whichEye;
    EyeShape shape;
    u32      color;
  };
  
  struct SpinEyes_t {
    u16 period_ms;
    u32 color;
    u8  leftClockwise;
    u8  rightClockWise;
  };
  
  struct TriggerAnimation_t {
    AnimationID_t animID;
    u8            numLoops;
  };
  
  // Kinda large
  /*
  // Set the color for all LEDs individually
  struct SetLEDcolors_t {
    u32 led[NUM_LEDS];
  };
   */
  
  // Using a union of structs here (determined by a check of the Type above)
  // instead of inheritance.
  union {
    
    // NOTE: Keil does not like defining structs inside anonymous unions, so
    // we define them outside and "instantiate" them each here. We _could_
    // use anonymous structs and then do the definitions here as well, but
    // then things get really confusing. Seems better to group the type-specific
    // info by name this way.
    
    // Head
    SetHeadAngle_t     SetHeadAngle;
    StartHeadNod_t     StartHeadNod;
    StopHeadNod_t      StopHeadNod;
    
    // Lift
    SetLiftHeight_t    SetLiftHeight;
    StartLiftNod_t     StartLiftNod;
    StopLiftNod_t      StopLiftNod;
    
    // Pose
    DriveLineSegment_t DriveLineSegment;
    DriveArc_t         DriveArc;
    BackAndForth_t     BackAndForth;
    StartWiggle_t      StartWiggle;
    StopWiggle_t       StopWiggle;
    TurnInPlace_t      TurnInPlace;
    
    // Sound
    PlaySound_t        PlaySound;
    
    // Lights
    BlinkEyes_t        BlinkEyes;
    SetEye_t           SetEye;
    //SetLEDcolors_t     SetLEDcolors;
    FlashEyes_t        FlashEyes;
    SpinEyes_t         SpinEyes;
    
    // Special
    TriggerAnimation_t TriggerAnimation;
    
  }; // union of structs
    
  void TransitionOutOf(const u32 animStartTime_ms) const;
  void TransitionInto(const u32 animStartTime_ms)  const;

  // Returns true if lift is at the target height, or head is at the target
  // angle, etc.
  bool IsInPosition();
  
}; // struct KeyFrame

  /*
KeyFrame::KeyFrame(Type t)
: type(t)
{
  
  // Initialize type-specific members:
  switch(type)
  {
    case SET_LED_COLORS:
    {
      for(s32 iLED = 0; iLED < HAL::NUM_LEDS; ++iLED) {
        SetLEDcolors.led[iLED] = UNSPECIFIED_COLOR;
      }
    }
  }
}
  */
  
} // namespace Cozmo
} // namespace Anki

#endif // ANKI_COZMO_ROBOT_KEYFRAME_H


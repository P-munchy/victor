#include "anki/cozmo/robot/hal.h"
#include "animationController.h"
#include "anki/common/robot/errorHandling.h"
#include "anki/common/robot/utilities_c.h"
#include "anki/common/shared/radians.h"
#include "anki/common/shared/velocityProfileGenerator.h"

#include "headController.h"
#include "liftController.h"
#include "localization.h"
#include "wheelController.h"
#include "steeringController.h"
#include "speedController.h"
#include "timeProfiler.h"

#define DEBUG_ANIMATION_CONTROLLER 0

#define USE_HARDCODED_ANIMATIONS 0

namespace Anki {
namespace Cozmo {
namespace AnimationController {
  
  namespace {
    
    // Streamed animation will not play until we've got this many _audio_ keyframes
    // buffered.
    static const s32 ANIMATION_PREROLL_LENGTH = 7;
    
    // Circular byte buffer for keyframe messages
    ONCHIP u8 _keyFrameBuffer[KEYFRAME_BUFFER_SIZE];
    s32 _currentBufferPos;
    s32 _lastBufferPos;
    
    s32 _numAudioFramesBuffered; // NOTE: Also counts EndOfAnimationFrames...
    s32 _numBytesPlayed = 0;

    u8  _currentTag = 0;
    
    bool _isBufferStarved;
    bool _haveReceivedTerminationFrame;
    bool _isPlaying;
    bool _bufferFullMessagePrintedThisTick;
    
    s32 _tracksToPlay;
    
    int _tracksInUse = 0;
    
#   if DEBUG_ANIMATION_CONTROLLER
    TimeStamp_t _currentTime_ms;
#   endif

  } // "private" members
  
  static void DefineHardCodedAnimations()
  {
#if USE_HARDCODED_ANIMATIONS
    //
    // FAST HEAD NOD - 3 fast nods
    //
    {
      ClearCannedAnimation(ANIM_HEAD_NOD);
      KeyFrame kf;
      kf.transitionIn  = KF_TRANSITION_LINEAR;
      kf.transitionOut = KF_TRANSITION_LINEAR;
      
      
      // Start the nod
      kf.type = KeyFrame::START_HEAD_NOD;
      kf.relTime_ms = 0;
      kf.StartHeadNod.lowAngle  = DEG_TO_RAD(-10);
      kf.StartHeadNod.highAngle = DEG_TO_RAD( 10);
      kf.StartHeadNod.period_ms = 600;
      AddKeyFrameToCannedAnimation(kf, ANIM_HEAD_NOD);
      
      // Stop the nod
      kf.type = KeyFrame::STOP_HEAD_NOD;
      kf.relTime_ms = 1500;
      kf.StopHeadNod.finalAngle = 0.f;
      AddKeyFrameToCannedAnimation(kf, ANIM_HEAD_NOD);
      
    } // FAST HEAD NOD
    
    //
    // SLOW HEAD NOD - 2 slow nods
    //
    {
      ClearCannedAnimation(ANIM_HEAD_NOD_SLOW);
      KeyFrame kf;
      kf.transitionIn  = KF_TRANSITION_LINEAR;
      kf.transitionOut = KF_TRANSITION_LINEAR;
      
      // Start the nod
      kf.type = KeyFrame::START_HEAD_NOD;
      kf.relTime_ms = 0;
      kf.StartHeadNod.lowAngle  = DEG_TO_RAD(-25);
      kf.StartHeadNod.highAngle = DEG_TO_RAD( 25);
      kf.StartHeadNod.period_ms = 1200;
      AddKeyFrameToCannedAnimation(kf, ANIM_HEAD_NOD_SLOW);
      
      // Stop the nod
      kf.type = KeyFrame::STOP_HEAD_NOD;
      kf.relTime_ms = 2400;
      kf.StopHeadNod.finalAngle = 0.f;
      AddKeyFrameToCannedAnimation(kf, ANIM_HEAD_NOD_SLOW);
      
    } // SLOW HEAD NOD
    
    
    //
    // BLINK
    //
    {
      ClearCannedAnimation(ANIM_BLINK);
      KeyFrame kf;
      kf.type = KeyFrame::SET_LED_COLORS;
      kf.transitionIn  = KF_TRANSITION_INSTANT;
      kf.transitionOut = KF_TRANSITION_INSTANT;
      
      // Start with all eye segments on:
      kf.relTime_ms = 0;
      kf.SetLEDcolors.led[LED_LEFT_EYE_BOTTOM]  = LED_BLUE;
      kf.SetLEDcolors.led[LED_LEFT_EYE_LEFT]    = LED_BLUE;
      kf.SetLEDcolors.led[LED_LEFT_EYE_RIGHT]   = LED_BLUE;
      kf.SetLEDcolors.led[LED_LEFT_EYE_TOP]     = LED_BLUE;
      kf.SetLEDcolors.led[LED_RIGHT_EYE_BOTTOM] = LED_BLUE;
      kf.SetLEDcolors.led[LED_RIGHT_EYE_LEFT]   = LED_BLUE;
      kf.SetLEDcolors.led[LED_RIGHT_EYE_RIGHT]  = LED_BLUE;
      kf.SetLEDcolors.led[LED_RIGHT_EYE_TOP]    = LED_BLUE;
      AddKeyFrameToCannedAnimation(kf, ANIM_BLINK);
      
      // Turn off top/bottom segments first
      kf.relTime_ms = 1700;
      kf.SetLEDcolors.led[LED_LEFT_EYE_BOTTOM]  = LED_OFF;
      kf.SetLEDcolors.led[LED_LEFT_EYE_LEFT]    = LED_BLUE;
      kf.SetLEDcolors.led[LED_LEFT_EYE_RIGHT]   = LED_BLUE;
      kf.SetLEDcolors.led[LED_LEFT_EYE_TOP]     = LED_OFF;
      kf.SetLEDcolors.led[LED_RIGHT_EYE_BOTTOM] = LED_OFF;
      kf.SetLEDcolors.led[LED_RIGHT_EYE_LEFT]   = LED_BLUE;
      kf.SetLEDcolors.led[LED_RIGHT_EYE_RIGHT]  = LED_BLUE;
      kf.SetLEDcolors.led[LED_RIGHT_EYE_TOP]    = LED_OFF;
      AddKeyFrameToCannedAnimation(kf, ANIM_BLINK);
      
      // Turn off all segments shortly thereafter
      kf.relTime_ms = 1750;
      kf.SetLEDcolors.led[LED_LEFT_EYE_BOTTOM]  = LED_OFF;
      kf.SetLEDcolors.led[LED_LEFT_EYE_LEFT]    = LED_OFF;
      kf.SetLEDcolors.led[LED_LEFT_EYE_RIGHT]   = LED_OFF;
      kf.SetLEDcolors.led[LED_LEFT_EYE_TOP]     = LED_OFF;
      kf.SetLEDcolors.led[LED_RIGHT_EYE_BOTTOM] = LED_OFF;
      kf.SetLEDcolors.led[LED_RIGHT_EYE_LEFT]   = LED_OFF;
      kf.SetLEDcolors.led[LED_RIGHT_EYE_RIGHT]  = LED_OFF;
      kf.SetLEDcolors.led[LED_RIGHT_EYE_TOP]    = LED_OFF;
      AddKeyFrameToCannedAnimation(kf, ANIM_BLINK);
      
      // Turn on left/right segments first
      kf.relTime_ms = 1850;
      kf.SetLEDcolors.led[LED_LEFT_EYE_BOTTOM]  = LED_OFF;
      kf.SetLEDcolors.led[LED_LEFT_EYE_LEFT]    = LED_BLUE;
      kf.SetLEDcolors.led[LED_LEFT_EYE_RIGHT]   = LED_BLUE;
      kf.SetLEDcolors.led[LED_LEFT_EYE_TOP]     = LED_OFF;
      kf.SetLEDcolors.led[LED_RIGHT_EYE_BOTTOM] = LED_OFF;
      kf.SetLEDcolors.led[LED_RIGHT_EYE_LEFT]   = LED_BLUE;
      kf.SetLEDcolors.led[LED_RIGHT_EYE_RIGHT]  = LED_BLUE;
      kf.SetLEDcolors.led[LED_RIGHT_EYE_TOP]    = LED_OFF;
      AddKeyFrameToCannedAnimation(kf, ANIM_BLINK);
      
      // Turn on all segments shortly thereafter
      kf.relTime_ms = 1900;
      kf.SetLEDcolors.led[LED_LEFT_EYE_BOTTOM]  = LED_BLUE;
      kf.SetLEDcolors.led[LED_LEFT_EYE_LEFT]    = LED_BLUE;
      kf.SetLEDcolors.led[LED_LEFT_EYE_RIGHT]   = LED_BLUE;
      kf.SetLEDcolors.led[LED_LEFT_EYE_TOP]     = LED_BLUE;
      kf.SetLEDcolors.led[LED_RIGHT_EYE_BOTTOM] = LED_BLUE;
      kf.SetLEDcolors.led[LED_RIGHT_EYE_LEFT]   = LED_BLUE;
      kf.SetLEDcolors.led[LED_RIGHT_EYE_RIGHT]  = LED_BLUE;
      kf.SetLEDcolors.led[LED_RIGHT_EYE_TOP]    = LED_BLUE;
      AddKeyFrameToCannedAnimation(kf, ANIM_BLINK);
    } // BLINK
    
    //
    // Up/Down/Left/Right
    //  Move lift and head up and down (in opposite directions)
    //  Then turn to the left and the to the right
    {
      ClearCannedAnimation(ANIM_UPDOWNLEFTRIGHT);
      KeyFrame kf;
      
      // Move head up
      kf.type = KeyFrame::HEAD_ANGLE;
      kf.relTime_ms = 0;
      kf.SetHeadAngle.targetAngle = DEG_TO_RAD(25.f);
      kf.SetHeadAngle.targetSpeed = 5.f;
      AddKeyFrameToCannedAnimation(kf, ANIM_UPDOWNLEFTRIGHT);
      
      // Move lift down
      kf.type = KeyFrame::LIFT_HEIGHT;
      kf.relTime_ms = 0;
      kf.SetLiftHeight.targetHeight = 0.f;
      kf.SetLiftHeight.targetSpeed  = 50.f;
      AddKeyFrameToCannedAnimation(kf, ANIM_UPDOWNLEFTRIGHT);
      
      // Move head down
      kf.type = KeyFrame::HEAD_ANGLE;
      kf.relTime_ms = 750;
      kf.SetHeadAngle.targetAngle = DEG_TO_RAD(-25.f);
      kf.SetHeadAngle.targetSpeed = 5.f;
      AddKeyFrameToCannedAnimation(kf, ANIM_UPDOWNLEFTRIGHT);
      
      // Move lift up
      kf.type = KeyFrame::LIFT_HEIGHT;
      kf.relTime_ms = 750;
      kf.SetLiftHeight.targetHeight = 75.f;
      kf.SetLiftHeight.targetSpeed  = 50.f;
      AddKeyFrameToCannedAnimation(kf, ANIM_UPDOWNLEFTRIGHT);
      
      // Turn left
      kf.type = KeyFrame::POINT_TURN;
      kf.relTime_ms = 1250;
      kf.TurnInPlace.relativeAngle = DEG_TO_RAD(-45);
      kf.TurnInPlace.targetSpeed = 100.f;
      AddKeyFrameToCannedAnimation(kf, ANIM_UPDOWNLEFTRIGHT);

      // Turn right
      kf.type = KeyFrame::POINT_TURN;
      kf.relTime_ms = 2250;
      kf.TurnInPlace.relativeAngle = DEG_TO_RAD(90);
      kf.TurnInPlace.targetSpeed = 100.f;
      AddKeyFrameToCannedAnimation(kf, ANIM_UPDOWNLEFTRIGHT);
      
      // Turn back to center
      kf.type = KeyFrame::POINT_TURN;
      kf.relTime_ms = 2750;
      kf.TurnInPlace.relativeAngle = DEG_TO_RAD(-45);
      kf.TurnInPlace.targetSpeed = 100.f;
      AddKeyFrameToCannedAnimation(kf, ANIM_UPDOWNLEFTRIGHT);
      
    } // Up/Down/Left/Right
    
    //
    // BACK_AND_FORTH_EXCITED
    //
    {
      ClearCannedAnimation(ANIM_BACK_AND_FORTH_EXCITED);
      KeyFrame kf;
      
      kf.type = KeyFrame::DRIVE_LINE_SEGMENT;
      kf.relTime_ms = 300;
      kf.DriveLineSegment.relativeDistance = -9;
      kf.DriveLineSegment.targetSpeed = 30;
      AddKeyFrameToCannedAnimation(kf, ANIM_BACK_AND_FORTH_EXCITED);
      
      kf.type = KeyFrame::DRIVE_LINE_SEGMENT;
      kf.relTime_ms = 600;
      kf.DriveLineSegment.relativeDistance = 9;
      kf.DriveLineSegment.targetSpeed = 30;
      AddKeyFrameToCannedAnimation(kf, ANIM_BACK_AND_FORTH_EXCITED);
      
    } // BACK_AND_FORTH_EXCITED
    
#endif // USE_HARDCODED_ANIMATIONS
    
  } // DefineHardCodedAnimations()
  
  
  Result Init()
  {
#   if DEBUG_ANIMATION_CONTROLLER
    PRINT("Initializing AnimationController\n");
#   endif
    
    _tracksToPlay = ENABLE_ALL_TRACKS;
    
    _tracksInUse  = 0;
    
    Clear();
    
    DefineHardCodedAnimations();
    
    return RESULT_OK;
  }
  
  static s32 GetNumBytesAvailable()
  {
    if(_lastBufferPos >= _currentBufferPos) {
      return sizeof(_keyFrameBuffer) - (_lastBufferPos - _currentBufferPos);
    } else {
      return _currentBufferPos - _lastBufferPos;
    }
  }
  
  static s32 GetNumBytesInBuffer()
  {
    if(_lastBufferPos >= _currentBufferPos) {
      return (_lastBufferPos - _currentBufferPos);
    } else {
      return sizeof(_keyFrameBuffer) - (_currentBufferPos - _lastBufferPos);
    }
  }
  
  s32 GetTotalNumBytesPlayed() {
    return _numBytesPlayed;
  }
  
  void ClearNumBytesPlayed() {
    _numBytesPlayed = 0;
  }
  
  void Clear()
  {
#   if DEBUG_ANIMATION_CONTROLLER
    PRINT("Clearing AnimationController\n");
#   endif
    
    _numBytesPlayed += GetNumBytesInBuffer();
    //PRINT("CLEAR NumBytesPlayed %d (%d)\n", _numBytesPlayed, GetNumBytesInBuffer());
    
    _currentBufferPos = 0;
    _lastBufferPos = 0;
    _currentTag = 0;
    
    _numAudioFramesBuffered = 0;

    _haveReceivedTerminationFrame = false;
    _isPlaying = false;
    _isBufferStarved = false;
    _bufferFullMessagePrintedThisTick = false;
    
    if(_tracksInUse) {
      // In case we are aborting an animation, stop any tracks that were in use
      // (For now, this just means motor-based tracks.) Note that we don't
      // stop tracks we weren't using, in case we were, for example, playing
      // a head animation while driving a path.
      if(_tracksInUse & HEAD_TRACK) {
        HeadController::SetAngularVelocity(0);
      }
      if(_tracksInUse & LIFT_TRACK) {
        LiftController::SetAngularVelocity(0);
      }
      if(_tracksInUse & BODY_TRACK) {
        SteeringController::ExecuteDirectDrive(0, 0);
      }
    }
      
    _tracksInUse = 0;
    
#   if DEBUG_ANIMATION_CONTROLLER
    _currentTime_ms = 0;
#   endif
  }

  static inline RobotInterface::EngineToRobot::Tag PeekBufferTag()
  {
    return _keyFrameBuffer[_currentBufferPos];
  }
  
  static s32 GetFromBuffer(u8* data, s32 numBytes)
  {
    assert(numBytes < sizeof(_keyFrameBuffer));
    
    if(_currentBufferPos + numBytes < sizeof(_keyFrameBuffer)) {
      // There's enough room from current position to end of buffer to just
      // copy directly
      memcpy(data, _keyFrameBuffer + _currentBufferPos, numBytes);
      _currentBufferPos += numBytes;
    } else {
      // Copy the first chunk from whatever remains from current position to end of
      // the buffer
      const s32 firstChunk = sizeof(_keyFrameBuffer) - _currentBufferPos;
      memcpy(data, _keyFrameBuffer + _currentBufferPos, firstChunk);
      
      // Copy the remaining data starting at the beginning of the buffer
      memcpy(data+firstChunk, _keyFrameBuffer, numBytes - firstChunk);
      _currentBufferPos = numBytes-firstChunk;
    }
    
    // Increment total number of bytes played since startup
    _numBytesPlayed += numBytes;
    //PRINT("NumBytesPlayed %d (%d) (%d)\n", _numBytesPlayed, numBytes, *((u32*)data));
    
    assert(_currentBufferPos >= 0 && _currentBufferPos < sizeof(_keyFrameBuffer));
    
    return numBytes;
  }
  
  static s32 GetFromBuffer(RobotInterface::EngineToRobot* msg)
  {
    s32 readSoFar;
    memset(msg, 0, sizeof(RobotInterface::EngineToRobot)); // Memset 0, presumably sets all size fields to 0
    readSoFar  = GetFromBuffer(msg->GetBuffer(), RobotInterface::EngineToRobot::MIN_SIZE); // Read in enough to know what it is
    readSoFar += GetFromBuffer(msg->GetBuffer() + readSoFar, msg->Size() - readSoFar); // Read in the minimum size for the type to get length fields
    readSoFar += GetFromBuffer(msg->GetBuffer() + readSoFar, msg->Size() - readSoFar); // Read in anything left now that we know how big minimum fields are
    return readSoFar;
  }
  
  Result BufferKeyFrame(const RobotInterface::EngineToRobot& msg)
  {
    const s32 numBytesAvailable = GetNumBytesAvailable();
    const s32 numBytesNeeded = msg.Size();
    if(numBytesAvailable < numBytesNeeded) {
      // Only print the error message if we haven't already done so this tick,
      // to prevent spamming that could clog reliable UDP
      if(!_bufferFullMessagePrintedThisTick) {
        AnkiError("AnimationController.BufferKeyFrame.BufferFull",
                  "%d bytes available, %d needed.\n",
                  numBytesAvailable, numBytesNeeded);
        _bufferFullMessagePrintedThisTick = true;
      }
      return RESULT_FAIL;
    }
    
    s32 numBytes = msg.Size();
    
    assert(numBytes < sizeof(_keyFrameBuffer));
    
    if(_lastBufferPos + numBytes < sizeof(_keyFrameBuffer)) {
      // There's enough room from current end position to end of buffer to just
      // copy directly
      memcpy(_keyFrameBuffer + _lastBufferPos, msg.GetBuffer(), numBytes);
      _lastBufferPos += numBytes;
    } else {
      // Copy the first chunk into whatever fits from current position to end of
      // the buffer
      const s32 firstChunk = sizeof(_keyFrameBuffer) - _lastBufferPos;
      memcpy(_keyFrameBuffer + _lastBufferPos, msg.GetBuffer(), firstChunk);
      
      // Copy the remaining data starting at the beginning of the buffer
      memcpy(_keyFrameBuffer, msg.GetBuffer()+firstChunk, numBytes - firstChunk);
      _lastBufferPos = numBytes-firstChunk;
     }
    switch(msg.tag) {
      case RobotInterface::EngineToRobot::Tag_animEndOfAnimation:
        _haveReceivedTerminationFrame = true;
      case RobotInterface::EngineToRobot::Tag_animAudioSample:
      case RobotInterface::EngineToRobot::Tag_animAudioSilence:
        ++_numAudioFramesBuffered;
        break;
        default:
        break;
    }
    
    assert(_lastBufferPos >= 0 && _lastBufferPos < sizeof(_keyFrameBuffer));
    
    return RESULT_OK;
  }
    
  bool IsBufferFull()
  {
    return GetNumBytesAvailable() > 0;
  }
  
  bool IsPlaying()
  {
    return _isPlaying;
  }
  
  static inline bool IsReadyToPlay()
  {
    
    bool ready = false;
    
    if(_isPlaying) {
      // If we are already in progress playing something, we are "ready to play"
      // until we run out of keyframes in the buffer
      // Note that we need at least two "frames" in the buffer so we can always
      // read from the current one to the next one without reaching end of buffer.
      ready = _numAudioFramesBuffered > 1;

      // Report every time the buffer goes from having a sufficient number of audio frames to not.
      if (!ready) {
        if (!_isBufferStarved) {
          _isBufferStarved = true;
          PRINT("AnimationController.IsReadyToPlay.BufferStarved\n");
        }
      } else {
        _isBufferStarved = false;
      }
      
    } else {
      // Otherwise, wait until we get enough frames to start
      ready = (_numAudioFramesBuffered > ANIMATION_PREROLL_LENGTH || _haveReceivedTerminationFrame);
      if(ready) {
        _isPlaying = true;
        _isBufferStarved = false;
        
#       if DEBUG_ANIMATION_CONTROLLER
        _currentTime_ms = 0;
#       endif
      }
    }
    
    //assert(_currentFrame <= _lastFrame);
    
    return ready;
  } // IsReadyToPlay()
  

  Result Update()
  {
    if(IsReadyToPlay()) {
      
      // If AudioReady() returns true, we are ready to move to the next keyframe
      if(HAL::AudioReady())
      {
        START_TIME_PROFILE(Anim, AUDIOPLAY);        
        
        // Next thing in the buffer should be audio or silence:
        RobotInterface::EngineToRobot::Tag msgID = PeekBufferTag();
        RobotInterface::EngineToRobot msg;
        
        // If the next message is not audio, then delete it until it is.
        while(msgID != RobotInterface::EngineToRobot::Tag_animAudioSilence &&
              msgID != RobotInterface::EngineToRobot::Tag_animAudioSample) {
          PRINT("Expecting either audio sample or silence next in animation buffer. (Got 0x%02x instead). Dumping message. (FYI AudioSample_ID = 0x%02x)\n", msgID, RobotInterface::EngineToRobot::Tag_animAudioSample);
          GetFromBuffer(&msg);
          msgID = PeekBufferTag();
        }
        
        GetFromBuffer(&msg);
        
        switch(msg.tag)
        {
          case RobotInterface::EngineToRobot::Tag_animAudioSilence:
          {
            HAL::AudioPlaySilence();
            break;
          }
          case RobotInterface::EngineToRobot::Tag_animAudioSample:
          {
            if(_tracksToPlay & AUDIO_TRACK) {
              HAL::AudioPlayFrame(&msg.animAudioSample);
            } else {
              HAL::AudioPlaySilence();
            }
            break;
          }
          default:
            PRINT("Expecting either audio sample or silence next in animation buffer. (Got 0x%02x instead)\n", msgID);
            return RESULT_FAIL;
        }
      
#       if DEBUG_ANIMATION_CONTROLLER
        _currentTime_ms += 33;
#       endif
        
        MARK_NEXT_TIME_PROFILE(Anim, WHILE);
        
        // Keep reading until we hit another audio type
        bool nextAudioFrameFound = false;
        bool terminatorFound = false;
        while(!nextAudioFrameFound && !terminatorFound)
        {
          if(_currentBufferPos == _lastBufferPos) {
            // We should not be here if there isn't at least another audio sample,
            // silence, or end-of-animation keyframe in the buffer to find.
            // (Note that IsReadyToPlay() checks for there being at least _two_
            //  keyframes in the buffer, where a "keyframe" is considered an
            //  audio sample (or silence) or an end-of-animation indicator.)
            PRINT("Ran out of animation buffer after getting audio/silence.\n");
            return RESULT_FAIL;
          }
          
          msgID = PeekBufferTag();
          
          switch(msgID)
          {
            case RobotInterface::EngineToRobot::Tag_animAudioSample:
            {
              _tracksInUse |= BACKPACK_LIGHTS_TRACK;
              // Fall through to below...
            }
            case RobotInterface::EngineToRobot::Tag_animAudioSilence:
            {
              nextAudioFrameFound = true;
              break;
            }
            case RobotInterface::EngineToRobot::Tag_animStartOfAnimation:
            {
              GetFromBuffer(&msg);
              _currentTag = msg.animStartOfAnimation.tag;
#             if DEBUG_ANIMATION_CONTROLLER
              PRINT("AnimationController: StartOfAnimation w/ tag=%d\n", _currentTag);
#             endif
              break;
            }  
            case RobotInterface::EngineToRobot::Tag_animEndOfAnimation:
            {
#             if DEBUG_ANIMATION_CONTROLLER
              PRINT("AnimationController[t=%dms(%d)] hit EndOfAnimation\n",
                    _currentTime_ms, HAL::GetTimeStamp());
#             endif
              GetFromBuffer(&msg);
              terminatorFound = true;
              _tracksInUse = 0;
              break;
            }
              
            case RobotInterface::EngineToRobot::Tag_animHeadAngle:
            {
              GetFromBuffer(&msg);
              if(_tracksToPlay & HEAD_TRACK) {
#               if DEBUG_ANIMATION_CONTROLLER
                PRINT("AnimationController[t=%dms(%d)] requesting head angle of %ddeg over %.2fsec\n",
                      _currentTime_ms, HAL::GetTimeStamp(),
                      msg.animHeadAngle.angle_deg, static_cast<f32>(msg.animHeadAngle.time_ms)*.001f);
#               endif
                
                HeadController::SetDesiredAngle(DEG_TO_RAD(static_cast<f32>(msg.animHeadAngle.angle_deg)), 0.1f, 0.1f,
                                                static_cast<f32>(msg.animHeadAngle.time_ms)*.001f);
                _tracksInUse |= HEAD_TRACK;
              }
              break;
            }
              
            case RobotInterface::EngineToRobot::Tag_animLiftHeight:
            {
              GetFromBuffer(&msg);              
              if(_tracksToPlay & LIFT_TRACK) {
#               if DEBUG_ANIMATION_CONTROLLER
                PRINT("AnimationController[t=%dms(%d)] requesting lift height of %dmm over %.2fsec\n",
                      _currentTime_ms, HAL::GetTimeStamp(),
                      msg.animLiftHeight.height_mm, static_cast<f32>(msg.animLiftHeight.time_ms)*.001f);
#               endif
                
                LiftController::SetDesiredHeight(static_cast<f32>(msg.animLiftHeight.height_mm), 0.1f, 0.1f,
                                                 static_cast<f32>(msg.animLiftHeight.time_ms)*.001f);
                _tracksInUse |= LIFT_TRACK;
              }
              break;
            }
              
            case RobotInterface::EngineToRobot::Tag_animBackpackLights:
            {
              GetFromBuffer(&msg);
              
              if(_tracksToPlay & BACKPACK_LIGHTS_TRACK) {
#               if DEBUG_ANIMATION_CONTROLLER
                PRINT("AnimationController[t=%dms(%d)] setting backpack LEDs.\n",
                      _currentTime_ms, HAL::GetTimeStamp());
#               endif
                
                for(s32 iLED=0; iLED<NUM_BACKPACK_LEDS; ++iLED) {
                  HAL::SetLED(static_cast<LEDId>(iLED), msg.animBackpackLights.colors[iLED]);
                }
                _tracksInUse |= BACKPACK_LIGHTS_TRACK;
              }
              break;
            }
              
            case RobotInterface::EngineToRobot::Tag_animFaceImage:
            {
              GetFromBuffer(&msg);
              
              if(_tracksToPlay & FACE_IMAGE_TRACK) {
#               if DEBUG_ANIMATION_CONTROLLER
                PRINT("AnimationController[t=%dms(%d)] setting face frame.\n",
                      _currentTime_ms, HAL::GetTimeStamp());
#               endif
                
                HAL::FaceAnimate(msg.animFaceImage.image);
                
                _tracksInUse |= FACE_IMAGE_TRACK;
              }
              break;
            }
              
            case RobotInterface::EngineToRobot::Tag_animFacePosition:
            {
              GetFromBuffer(&msg);
              
              if(_tracksToPlay & FACE_POS_TRACK) {
#               if DEBUG_ANIMATION_CONTROLLER
                PRINT("AnimationController[t=%dms(%d)] setting face position to (%d,%d).\n",
                      _currentTime_ms, HAL::GetTimeStamp(), msg.animFacePosition.xCen, msg.animFacePosition.yCen);
#               endif
                
                HAL::FaceMove(msg.animFacePosition.xCen, msg.animFacePosition.yCen);
                
                _tracksInUse |= FACE_POS_TRACK;
              }
              break;
            }
              
            case RobotInterface::EngineToRobot::Tag_animBlink:
            {
              GetFromBuffer(&msg);
              
              if(_tracksToPlay & BLINK_TRACK) {
#               if DEBUG_ANIMATION_CONTROLLER
                PRINT("AnimationController[t=%dms(%d)] Blinking.\n",
                      _currentTime_ms, HAL::GetTimeStamp());
#               endif
                
                if(msg.animBlink.blinkNow) {
                  HAL::FaceBlink();
                } else {
                  if(msg.animBlink.enable) {
                    //EyeController::Enable();
                  } else {
                    //EyeController::Disable();
                  }
                }
                _tracksInUse |= BLINK_TRACK;
              }
              break;
            }
              
            case RobotInterface::EngineToRobot::Tag_animBodyMotion:
            {
              GetFromBuffer(&msg);
              
              if(_tracksToPlay & BODY_TRACK) {
#               if DEBUG_ANIMATION_CONTROLLER
                PRINT("AnimationController[t=%dms(%d)] setting body motion to radius=%d, speed=%d\n",
                      _currentTime_ms, HAL::GetTimeStamp(), msg.animBodyMotion.curvatureRadius_mm,
                      msg.animBodyMotion.speed);
#               endif

                _tracksInUse |= BODY_TRACK;
                
                f32 leftSpeed=0, rightSpeed=0;
                if(msg.animBodyMotion.speed == 0) {
                  // Stop
                  leftSpeed = 0.f;
                  rightSpeed = 0.f;
                } else if(msg.animBodyMotion.curvatureRadius_mm == s16_MAX || 
                          msg.animBodyMotion.curvatureRadius_mm == s16_MIN) {
                  // Drive straight
                  leftSpeed  = static_cast<f32>(msg.animBodyMotion.speed);
                  rightSpeed = static_cast<f32>(msg.animBodyMotion.speed);
                } else if(msg.animBodyMotion.curvatureRadius_mm == 0) {
                  SteeringController::ExecutePointTurn(DEG_TO_RAD_F32(msg.animBodyMotion.speed), 50);
                  break;
                  
                } else {
                  // Drive an arc
                  
                  //if speed is positive, the left wheel should turn slower, so
                  // it becomes the INNER wheel
                  leftSpeed = static_cast<f32>(msg.animBodyMotion.speed) * (1.0f - WHEEL_DIST_HALF_MM / static_cast<f32>(msg.animBodyMotion.curvatureRadius_mm));
                  
                  //if speed is positive, the right wheel should turn faster, so
                  // it becomes the OUTER wheel
                  rightSpeed = static_cast<f32>(msg.animBodyMotion.speed) * (1.0f + WHEEL_DIST_HALF_MM / static_cast<f32>(msg.animBodyMotion.curvatureRadius_mm));
                }
                
                SteeringController::ExecuteDirectDrive(leftSpeed, rightSpeed);
              }
              break;
            }
              
            default:
            {
              PRINT("Unexpected message type %d in animation buffer!\n", msgID);
              return RESULT_FAIL;
            }
              
          } // switch
        } // while(!nextAudioFrameFound && !terminatorFound)

        --_numAudioFramesBuffered;
        
        if(terminatorFound) {
          _isPlaying = false;
          _haveReceivedTerminationFrame = false;
          --_numAudioFramesBuffered;
#         if DEBUG_ANIMATION_CONTROLLER
          PRINT("Reached animation %d termination frame (%d frames still buffered, curPos/lastPos = %d/%d).\n",
                _currentTag, _numAudioFramesBuffered, _currentBufferPos, _lastBufferPos);
#         endif
          _currentTag = 0;
        }

        // Print time profile stats
        END_TIME_PROFILE(Anim);
        PERIODIC_PRINT_AND_RESET_TIME_PROFILE(Anim, 120);
        

      } // if(AudioReady())
    } // if(IsReadyToPlay())
    
    return RESULT_OK;
  } // Update()
  
  void EnableTracks(u8 whichTracks)
  {
    _tracksToPlay |= whichTracks;
  }
  
  void DisableTracks(u8 whichTracks)
  {
    _tracksToPlay &= ~whichTracks;
  }
  
  u8 GetCurrentTag()
  {
    return _currentTag;
  }
  
} // namespace AnimationController
} // namespace Cozmo
} // namespace Anki

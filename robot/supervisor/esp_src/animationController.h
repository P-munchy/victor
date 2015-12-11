/**
 * File: animationController.h
 *
 * Author: Kevin Yoon
 * Created: 6/23/2014
 *
 * Description:
 *
 *   Controller for playing animations that comprise coordinated motor, light, and sound actions.
 *
 * Update: Andrew Stein, 6/22/2015
 *   Updating to support streaming animations from Basestation instead of a set of static, 
 *   canned animations stored on the Robot.
 *
 * Update: Daniel Casner, 10/23/2015
 *   Refactoring to run buffer and spooling on the Espressif with most of the execution happening on the K02
 *
 * Copyright: Anki, Inc. 2015
 *
 **/


#ifndef COZMO_ANIMATION_CONTROLLER_H_
#define COZMO_ANIMATION_CONTROLLER_H_

#include "anki/types.h"
#include "messages.h"

/// Send animation state message every 5ms = 5,000 us
#define ANIM_STATE_INTERVAL 5000

namespace Anki {
  namespace Cozmo {
    namespace AnimationController {
      
      Result Init();

      // Buffer up a new KeyFrame for playing, using a KeyFrame message
      Result BufferKeyFrame(const RobotInterface::EngineToRobot& msg);

      // Plays any buffered keyframes available, if enough of a pre-roll is
      // buffered up or we've received all the keyframes for the animation
      // that's currently playing.
      void Update();
      
      /// Retrieves one drop worth of audio data to pass along to RTIP
      // When an audio frame is finished, a call to Update is posted
      // @param dest A buffer to copy data into
      // @boot True if data was provided false if none available
      extern "C" bool PumpAudioData(uint8_t* dest);
      
      // Clears any remaining buffered keyframes and thus immediately stops
      // animation from playing
      void Clear();
      
      // Sends the animation state message to the base station
      Result SendAnimStateMessage();
      
      // Returns true if there are buffered keyframes being played
      bool IsPlaying();
      
      // Returns true if there is no more room left in the buffer for new
      // frames to be streamed. (With some padding for what may already be
      // on the way)
      bool IsBufferFull();
      
      // Get total number of bytes played since startup or ClearNumBytesPlayed() was last called.
      s32 GetTotalNumBytesPlayed();
      
      void ClearNumBytesPlayed();
      
      // Enables/disables the given tracks without changing the others' states.
      // Keyframes for disabled tracks that are encountered in the buffer are
      // discarded (but the numBytesPlayed count is still incremented)
      void EnableTracks(u8 whichTracks);
      void DisableTracks(u8 whichTracks);
      
      // Return the "tag" from the most recent StartOfAnimation keyframe
      u8 GetCurrentTag();
      
    } // namespace AnimationController
  } // namespcae Cozmo
} // namespace Anki

#endif // COZMO_ANIMATION_CONTROLLER_H_

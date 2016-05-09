/**
* File: track
*
* Author: damjan stulic
* Created: 9/16/15
*
* Description: 
*
* Copyright: Anki, inc. 2015
*
*/


#include "anki/cozmo/basestation/animations/track.h"
#include "util/logging/logging.h"

namespace Anki {
namespace Cozmo {
namespace Animations {

  static void EnableStopMessageHelper(const BodyMotionKeyFrame& addedKeyFrame,
                                      BodyMotionKeyFrame* prevKeyFrame)
  {
    if(nullptr != prevKeyFrame)
    {
      // If the keyframe we just added starts within a single sample length
      // of the end of the previous keyframe, then there's no need to send
      // a stop message for the previous keyframe because the body motion
      // command for this new keyframe will handle it. This avoids delays
      // introduced by "extra" stop messages being inserted unnecessarily.
      const TimeStamp_t prevKeyFrameEndTime = (prevKeyFrame->GetTriggerTime() +
                                               prevKeyFrame->GetDurationTime());
      if(prevKeyFrameEndTime >= addedKeyFrame.GetTriggerTime() - IKeyFrame::SAMPLE_LENGTH_MS) {
        //PRINT_NAMED_DEBUG("Animations.EnableStopMessageHelper",
        //                  "Disabling stop message for body motion keyframe at t=%d "
        //                  "with duration=%d because of next keyframe at t=%d",
        //                  prevKeyFrame->GetTriggerTime(), prevKeyFrame->GetDurationTime(),
        //                  addedKeyFrame.GetTriggerTime());
        prevKeyFrame->EnableStopMessage(false);
      }
    }
  }
  
  
  // Specializations for body motion to decide if we need to send a stop message
  // between the last frame and the one being added
  template<>
  Result Track<BodyMotionKeyFrame>::AddKeyFrameToBack(const BodyMotionKeyFrame& keyFrame)
  {
    BodyMotionKeyFrame* prevKeyFrame = nullptr;
    Result result = AddKeyFrameToBackHelper(keyFrame, prevKeyFrame);
    
    if(result == RESULT_OK)
    {
      EnableStopMessageHelper(keyFrame, prevKeyFrame);
    }
    
    return result;
  }

  
  template<>
  Result Track<BodyMotionKeyFrame>::AddKeyFrameByTime(const BodyMotionKeyFrame& keyFrame)
  {
    BodyMotionKeyFrame* prevKeyFrame = nullptr;
    Result result = AddKeyFrameByTimeHelper(keyFrame, prevKeyFrame);
    
    if(result == RESULT_OK)
    {
      EnableStopMessageHelper(keyFrame, prevKeyFrame);
    }
    
    return result;
  }
  
} // end namespace Animations
} // end namespace Cozmo
} // end namespace Anki




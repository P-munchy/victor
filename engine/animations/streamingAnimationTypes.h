/*
 * File: streamingAnimationTypes.h
 *
 * Author: Jordan Rivas
 * Created: 12/12/16
 *
 * Description: Streaming Animation types
 *
 * #include "engine/animations/streamingAnimationTypes.h"
 *
 * Copyright: Anki, Inc. 2016
 */

#ifndef __Basestation_Animations_StreamingAnimationTypes_H__
#define __Basestation_Animations_StreamingAnimationTypes_H__


namespace Anki {
  
namespace AudioEngine {
  struct AudioFrameData;
}

namespace Cozmo {

class HeadAngleKeyFrame;
class LiftHeightKeyFrame;
class FaceAnimationKeyFrame;
class ProceduralFaceKeyFrame;
class EventKeyFrame;
class BackpackLightsKeyFrame;
class BodyMotionKeyFrame;
class DeviceAudioKeyFrame;
class RobotAudioKeyFrame;

namespace RobotAnimation {

//
// Collection of keyframes for each animation track.
// Keyframes may be nullptr if track is not active.
// Struct does not take ownership of members.
//
struct StreamingAnimationFrame {
  
  HeadAngleKeyFrame*                  headFrame;
  LiftHeightKeyFrame*                 liftFrame;
  FaceAnimationKeyFrame*              animFaceFrame;
  ProceduralFaceKeyFrame*             procFaceFrame;
  EventKeyFrame*                      eventFrame;
  BackpackLightsKeyFrame*             backpackFrame;
  BodyMotionKeyFrame*                 bodyFrame;
  RecordHeadingKeyFrame*              recordHeadingFrame;
  TurnToRecordedHeadingKeyFrame*      turnToRecordedHeadingFrame;
  DeviceAudioKeyFrame*                deviceAudioFrame;
  RobotAudioKeyFrame*                 robotAudioFrame;
  const AudioEngine::AudioFrameData*  audioFrameData;
  
  
  StreamingAnimationFrame()
  {
    ClearFrame();
  }
  
  void ClearFrame()
  {
    headFrame = nullptr;
    liftFrame = nullptr;
    animFaceFrame = nullptr;
    procFaceFrame = nullptr;
    eventFrame = nullptr;
    backpackFrame = nullptr;
    bodyFrame = nullptr;
    recordHeadingFrame = nullptr;
    turnToRecordedHeadingFrame = nullptr;
    deviceAudioFrame = nullptr;
    robotAudioFrame = nullptr;
    audioFrameData = nullptr;
  }
  
};

} // namespace RobotAnimation
} // namespace Cozmo
} // namespace Anki


#endif /* __Basestation_Animations_StreamingAnimationTypes_H__ */

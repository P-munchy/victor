/**
 * File: trackLayerComponent.h
 *
 * Authors: Al Chaussee
 * Created: 06/28/2017
 *
 * Description: Component which manages creating various procedural animations by
 *              using the trackLayerManagers to generate keyframes and add them to
 *              track layers
 *              Currently there are only three trackLayerManagers face, backpack, and audio
 *
 *
 * Copyright: Anki, Inc. 2017
 *
 **/

#ifndef __Anki_Cozmo_TrackLayerComponent_H__
#define __Anki_Cozmo_TrackLayerComponent_H__

#include "coretech/common/shared/types.h"

#include "cozmoAnim/animation/animation.h"

#include "clad/types/liveIdleAnimationParameters.h"

#include <memory>

namespace Anki {
namespace Cozmo {

class CozmoAnimContext;

class AudioLayerManager;
class BackpackLayerManager;
class FaceLayerManager;
 
class TrackLayerComponent
{
public:

  // Output struct that contains the final keyframes to
  // stream to the robot
  struct LayeredKeyFrames {
    bool haveAudioKeyFrame = false;
    RobotAudioKeyFrame audioKeyFrame;
    
    bool haveBackpackKeyFrame = false;
    BackpackLightsKeyFrame backpackKeyFrame;
    
    bool haveFaceKeyFrame = false;
    ProceduralFaceKeyFrame faceKeyFrame;
  };
  
  TrackLayerComponent(const CozmoAnimContext* context);
  ~TrackLayerComponent();
  
  void Init();
  
  void Update();
  
  // Pulls the current keyframe from various tracks of the anim
  // and combines it with any track layers that may exist
  // Outputs layeredKeyframes struct which contains the final combined
  // keyframes from the anim and the various track layers
  void ApplyLayersToAnim(Animation* anim,
                         TimeStamp_t startTime_ms,
                         TimeStamp_t streamTime_ms,
                         LayeredKeyFrames& layeredKeyFrames,
                         bool storeFace);
  
  // Keep Cozmo's face alive using the params specified
  // (call each tick while the face should be kept alive)
  void KeepFaceAlive(const std::map<LiveIdleAnimationParameter, f32>& params);
  
  // Removes the live face after duration_ms has passed
  // Note: Will not cancel/remove a blink that is in progress
  void RemoveKeepFaceAlive(s32 duration_ms);
  
  // Make Cozmo blink
  void AddBlink();
  
  // Make Cozmo squint (will continue to squint until removed)
  // Returns a tag to keep track of what squint was added
  AnimationTag AddSquint(const std::string& name, f32 squintScaleX, f32 squintScaleY, f32 upperLidAngle);

  // Removes specified squint after duration_ms has passed
  void RemoveSquint(AnimationTag tag, s32 duration_ms = 0);
  
  // Either start an eye shift or update an already existing eye shift with new params
  // If tag == NotAnimationTag then start a new shift, tag will be updated to reference
  // this new shift
  // If tag != NotAnimationTag then update the existing eye shift with new params
  // Note: Eye shift will continue until removed
  void AddOrUpdateEyeShift(AnimationTag& tag,
                           const std::string& name,
                           f32 xPix,
                           f32 yPix,
                           TimeStamp_t duration_ms,
                           f32 xMax = ProceduralFace::HEIGHT,
                           f32 yMax = ProceduralFace::WIDTH,
                           f32 lookUpMaxScale = 1.1f,
                           f32 lookDownMinScale = 0.85f,
                           f32 outerEyeScaleIncrease = 0.1f);
  
  // Removes the specified eye shift after duration_ms has passed
  void RemoveEyeShift(AnimationTag tag, s32 duration_ms = 0);
  
  // Make Cozmo glitch
  void AddGlitch(f32 glitchDegree);
  
  // Returns true if any of the layerManagers have layers to send
  bool HaveLayersToSend() const;
  
  u32 GetMaxBlinkSpacingTimeForScreenProtection_ms() const;
  
private:

  void ApplyAudioLayersToAnim(Animation* anim,
                              TimeStamp_t startTime_ms,
                              TimeStamp_t streamTime_ms,
                              LayeredKeyFrames& layeredKeyFrames);
  
  void ApplyBackpackLayersToAnim(Animation* anim,
                                 TimeStamp_t startTime_ms,
                                 TimeStamp_t streamTime_ms,
                                 LayeredKeyFrames& layeredKeyFrames);
  
  void ApplyFaceLayersToAnim(Animation* anim,
                             TimeStamp_t startTime_ms,
                             TimeStamp_t streamTime_ms,
                             LayeredKeyFrames& layeredKeyFrames,
                             bool storeFace);
  
  std::unique_ptr<AudioLayerManager>    _audioLayerManager;
  std::unique_ptr<BackpackLayerManager> _backpackLayerManager;
  std::unique_ptr<FaceLayerManager>     _faceLayerManager;
  
  std::unique_ptr<ProceduralFace> _lastProceduralFace;
};
  
}
}

#endif

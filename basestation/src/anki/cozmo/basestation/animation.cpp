/**
 * File: animation.cpp
 *
 * Authors: Andrew Stein
 * Created: 2015-06-25
 *
 * Description:
 *    Class for storing a single animation, which is made of
 *    tracks of keyframes. Also manages streaming those keyframes
 *    to a robot.
 *
 * Copyright: Anki, Inc. 2015
 *
 **/

#include "anki/cozmo/basestation/animation/animation.h"
#include "anki/cozmo/basestation/robot.h"
#include "anki/cozmo/shared/cozmoEngineConfig.h"
#include "anki/cozmo/shared/cozmoConfig.h"
#include "anki/cozmo/basestation/soundManager.h"
#include "util/logging/logging.h"
#include "clad/robotInterface/messageEngineToRobot.h"
//#include <cassert>

#define DEBUG_ANIMATIONS 0

namespace Anki {
namespace Cozmo {

  static const char* kNameKey = "Name";
  
  Animation::Animation(const std::string& name)
  : _name(name)
  , _isInitialized(false)
  {
    
  }
  
  Result Animation::DefineFromJson(const std::string& name, const Json::Value &jsonRoot)
  {
    _name = name;
    
    // Clear whatever is in the existing animation
    Clear();
    
    const s32 numFrames = jsonRoot.size();
    for(s32 iFrame = 0; iFrame < numFrames; ++iFrame)
    {
      const Json::Value& jsonFrame = jsonRoot[iFrame];
      
      if(!jsonFrame.isObject()) {
        PRINT_NAMED_ERROR("Animation.DefineFromJson.FrameMissing",
                          "frame %d of '%s' animation is missing or incorrect type.",
                          iFrame, _name.c_str());
        return RESULT_FAIL;
      }
      
      const Json::Value& jsonFrameName = jsonFrame[kNameKey];
      
      if(!jsonFrameName.isString()) {
        PRINT_NAMED_ERROR("Animation.DefineFromJson.FrameNameMissing",
                          "Missing '%s' field for frame %d of '%s' animation.",
                          kNameKey, iFrame, _name.c_str());
        return RESULT_FAIL;
      }
      
      const std::string& frameName = jsonFrameName.asString();
      
      
      Result addResult = RESULT_FAIL;
      
      // Map from string name of frame to which track we want to store it in:
      if(frameName == HeadAngleKeyFrame::GetClassName()) {
        addResult = _headTrack.AddKeyFrameToBack(jsonFrame, name);
      } else if(frameName == LiftHeightKeyFrame::GetClassName()) {
        addResult = _liftTrack.AddKeyFrameToBack(jsonFrame, name);
      } else if(frameName == FaceAnimationKeyFrame::GetClassName()) {
        addResult = _faceAnimTrack.AddKeyFrameToBack(jsonFrame, name);
      } else if(frameName == FacePositionKeyFrame::GetClassName()) {
        addResult = _facePosTrack.AddKeyFrameToBack(jsonFrame, name);
      } else if(frameName == DeviceAudioKeyFrame::GetClassName()) {
        addResult = _deviceAudioTrack.AddKeyFrameToBack(jsonFrame, name);
      } else if(frameName == BlinkKeyFrame::GetClassName()) {
        addResult = _blinkTrack.AddKeyFrameToBack(jsonFrame, name);
      } else if(frameName == RobotAudioKeyFrame::GetClassName()) {
        addResult = _robotAudioTrack.AddKeyFrameToBack(jsonFrame, name);
      } else if(frameName == BackpackLightsKeyFrame::GetClassName()) {
        addResult = _backpackLightsTrack.AddKeyFrameToBack(jsonFrame, name);
      } else if(frameName == BodyMotionKeyFrame::GetClassName()) {
        addResult = _bodyPosTrack.AddKeyFrameToBack(jsonFrame, name);
      } else if(frameName == ProceduralFaceKeyFrame::GetClassName()) {
        addResult = _proceduralFaceTrack.AddKeyFrameToBack(jsonFrame, name);
      } else {
        PRINT_NAMED_ERROR("Animation.DefineFromJson.UnrecognizedFrameName",
                          "Frame %d in '%s' animation has unrecognized name '%s'.",
                          iFrame, _name.c_str(), frameName.c_str());
        return RESULT_FAIL;
      }
      
      if(addResult != RESULT_OK) {
        PRINT_NAMED_ERROR("Animation.DefineFromJson.AddKeyFrameFailure",
                          "Adding %s frame %d failed.",
                          frameName.c_str(), iFrame);
        return addResult;
      }
      
    } // for each frame
    
    return RESULT_OK;
  }
  
  template<>
  Animations::Track<HeadAngleKeyFrame>& Animation::GetTrack() {
    return _headTrack;
  }
  
  template<>
  Animations::Track<LiftHeightKeyFrame>& Animation::GetTrack() {
    return _liftTrack;
  }
  
  template<>
  Animations::Track<FaceAnimationKeyFrame>& Animation::GetTrack() {
    return _faceAnimTrack;
  }
  
  template<>
  Animations::Track<FacePositionKeyFrame>& Animation::GetTrack() {
    return _facePosTrack;
  }
  
  template<>
  Animations::Track<DeviceAudioKeyFrame>& Animation::GetTrack() {
    return _deviceAudioTrack;
  }
  
  template<>
  Animations::Track<RobotAudioKeyFrame>& Animation::GetTrack() {
    return _robotAudioTrack;
  }
  
  template<>
  Animations::Track<BackpackLightsKeyFrame>& Animation::GetTrack() {
    return _backpackLightsTrack;
  }
  
  template<>
  Animations::Track<BodyMotionKeyFrame>& Animation::GetTrack() {
    return _bodyPosTrack;
  }
  
  template<>
  Animations::Track<BlinkKeyFrame>& Animation::GetTrack() {
    return _blinkTrack;
  }
  
  template<>
  Animations::Track<ProceduralFaceKeyFrame>& Animation::GetTrack() {
    return _proceduralFaceTrack;
  }
  
  /*
  Result Animation::AddKeyFrameToBack(const HeadAngleKeyFrame& kf)
  {
    return _headTrack.AddKeyFrameToBack(kf);
  }
   */
  
  // Helper macro for running a given method of all tracks and combining the result
  // in the specified way. To just call a method, use ";" for COMBINE_WITH, or
  // use "&&" or "||" to combine into a single result.
#define ALL_TRACKS(__METHOD__, __COMBINE_WITH__, ...) \
_headTrack.__METHOD__(__VA_ARGS__) __COMBINE_WITH__ \
_liftTrack.__METHOD__(__VA_ARGS__) __COMBINE_WITH__ \
_faceAnimTrack.__METHOD__(__VA_ARGS__) __COMBINE_WITH__ \
_proceduralFaceTrack.__METHOD__(__VA_ARGS__) __COMBINE_WITH__ \
_facePosTrack.__METHOD__(__VA_ARGS__) __COMBINE_WITH__ \
_deviceAudioTrack.__METHOD__(__VA_ARGS__) __COMBINE_WITH__ \
_robotAudioTrack.__METHOD__(__VA_ARGS__) __COMBINE_WITH__ \
_backpackLightsTrack.__METHOD__(__VA_ARGS__) __COMBINE_WITH__ \
_bodyPosTrack.__METHOD__(__VA_ARGS__) __COMBINE_WITH__ \
_blinkTrack.__METHOD__(__VA_ARGS__)
  
  //# define ALL_TRACKS(__METHOD__, __ARG__, __COMBINE_WITH__) ALL_TRACKS_WITH_ARG(__METHOD__, void, __COMBINE_WITH__)
  
  Result Animation::Init()
  {

#   if DEBUG_ANIMATIONS
    PRINT_NAMED_INFO("Animation.Init", "Initializing animation '%s'", GetName().c_str());
#   endif
    
    ALL_TRACKS(Init, ;);
    
    _isInitialized = true;
    
    return RESULT_OK;
  } // Animation::Init()
  
  void Animation::Clear()
  {
    ALL_TRACKS(Clear, ;);
    _isInitialized = false;
  }
  
  bool Animation::IsEmpty() const
  {
    return ALL_TRACKS(IsEmpty, &&);
  }
  
  bool Animation::HasFramesLeft() const
  {
    return ALL_TRACKS(HasFramesLeft, ||);
  }

  void Animation::SetIsLive(bool isLive)
  {
    _isLive = isLive;
    ALL_TRACKS(SetIsLive, ;, isLive);
  }
  
} // namespace Cozmo
} // namespace Anki

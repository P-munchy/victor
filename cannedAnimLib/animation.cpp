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

#include "cannedAnimLib/animation.h"
#include "cannedAnimLib/cozmo_anim_generated.h"
//#include "cozmoAnim/animation/proceduralFace.h"
//#include "anki/cozmo/shared/cozmoEngineConfig.h"
#include "anki/cozmo/shared/cozmoConfig.h"
#include "util/logging/logging.h"
#include "clad/robotInterface/messageEngineToRobot.h"

#define DEBUG_ANIMATIONS 0

namespace Anki {
namespace Cozmo {

static const char* kNameKey = "Name";

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Animation::Animation(const std::string& name)
: _name(name)
, _isInitialized(false)
{
  
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Result Animation::DefineFromFlatBuf(const std::string& name, const CozmoAnim::AnimClip* animClip)
{
  /*
  TODO: Does this method and the FlatBuffers schema file need to support
        "DeviceAudioKeyFrame" and/or "FaceImageKeyFrame" (COZMO-8766) ?

  TODO: Update the processing of BackpackLights keyframes to NOT use an
        intermediate JSON data structure (Json::Value) for those keyframes. The
        corresponding track AddKeyFrameToBack() method should be overloaded to take
        in a CozmoAnim::BackpackLights keyframe and process accordingly (COZMO-8766).

  TODO: Reduce some code duplication in this method (COZMO-8766). Can we
        add a template helper function that uses the class type of the
        FlatBuffers-generated class as the template argument?
  */

  _name = name;

  // Clear whatever is in the existing animation
  Clear();

  auto keyframes = animClip->keyframes();

  auto liftData = keyframes->LiftHeightKeyFrame();
  if (liftData != nullptr) {
    for (int lftIdx=0; lftIdx < liftData->size(); lftIdx++) {
      const CozmoAnim::LiftHeight* liftKeyframe = liftData->Get(lftIdx);
      Result addResult = _liftTrack.AddKeyFrameToBack(liftKeyframe, name);
      if(addResult != RESULT_OK) {
        PRINT_NAMED_ERROR("Animation.DefineFromFlatBuf.AddKeyFrameFailure",
                          "Adding LiftHeight frame %d failed.", lftIdx);
        return addResult;
      }
    }
  }

  auto procFaceData = keyframes->ProceduralFaceKeyFrame();
  if (procFaceData != nullptr) {
    for (int pfIdx=0; pfIdx < procFaceData->size(); pfIdx++) {
      const CozmoAnim::ProceduralFace* procFaceKeyframe = procFaceData->Get(pfIdx);
      Result addResult = _proceduralFaceTrack.AddKeyFrameToBack(procFaceKeyframe, name);
      if(addResult != RESULT_OK) {
        PRINT_NAMED_ERROR("Animation.DefineFromFlatBuf.AddKeyFrameFailure",
                          "Adding ProceduralFace frame %d failed.", pfIdx);
        return addResult;
      }
    }
  }

  auto headData = keyframes->HeadAngleKeyFrame();
  if (headData != nullptr) {
    for (int headIdx=0; headIdx < headData->size(); headIdx++) {
      const CozmoAnim::HeadAngle* headKeyframe = headData->Get(headIdx);
      Result addResult = _headTrack.AddKeyFrameToBack(headKeyframe, name);
      if(addResult != RESULT_OK) {
        PRINT_NAMED_ERROR("Animation.DefineFromFlatBuf.AddKeyFrameFailure",
                          "Adding HeadAngle frame %d failed.", headIdx);
        return addResult;
      }
    }
  }

  auto audioData = keyframes->RobotAudioKeyFrame();
  if (audioData != nullptr) {
    for (int audioIdx=0; audioIdx < audioData->size(); audioIdx++) {
      const CozmoAnim::RobotAudio* audioKeyframe = audioData->Get(audioIdx);
      Result addResult = _robotAudioTrack.AddKeyFrameToBack(audioKeyframe, name);
      if(addResult != RESULT_OK) {
        PRINT_NAMED_ERROR("Animation.DefineFromFlatBuf.AddKeyFrameFailure",
                          "Adding RobotAudio frame %d failed.", audioIdx);
        return addResult;
      }
    }
  }

  auto backpackData = keyframes->BackpackLightsKeyFrame();
  if (backpackData != nullptr) {
    for (int bpIdx=0; bpIdx < backpackData->size(); bpIdx++) {

      // TODO: Update the processing of these keyframes to NOT use an intermediate
      //       JSON data structure (Json::Value) for them. The corresponding track
      //       AddKeyFrameToBack() method should be overloaded to take in a
      //       CozmoAnim::BackpackLights keyframe and process accordingly (COZMO-8766).

      auto backpackKeyframe = backpackData->Get(bpIdx);
      Json::Value jsonFrame;
      jsonFrame[kNameKey] = std::string("BackpackLightsKeyFrame");
      jsonFrame["triggerTime_ms"] = backpackKeyframe->triggerTime_ms();
      jsonFrame["durationTime_ms"] = backpackKeyframe->durationTime_ms();
      
      jsonFrame["Front"] = Json::Value(Json::arrayValue);
      auto frontData = backpackKeyframe->Front();
      for (int idx=0; idx < frontData->size(); idx++) {
        auto frontVal = frontData->Get(idx);
        jsonFrame["Front"].append(frontVal);
      }
      jsonFrame["Middle"] = Json::Value(Json::arrayValue);
      auto middleData = backpackKeyframe->Middle();
      for (int idx=0; idx < middleData->size(); idx++) {
        auto middleVal = middleData->Get(idx);
        jsonFrame["Middle"].append(middleVal);
      }
      jsonFrame["Back"] = Json::Value(Json::arrayValue);
      auto backData = backpackKeyframe->Back();
      for (int idx=0; idx < backData->size(); idx++) {
        auto backVal = backData->Get(idx);
        jsonFrame["Back"].append(backVal);
      }
      Result addResult = _backpackLightsTrack.AddKeyFrameToBack(jsonFrame, name);
      if(addResult != RESULT_OK) {
        PRINT_NAMED_ERROR("Animation.DefineFromFlatBuf.AddKeyFrameFailure",
                          "Adding BackpackLights frame %d failed.", bpIdx);
        return addResult;
      }
    }
  }

  auto spriteSequenceData = keyframes->FaceAnimationKeyFrame();
  if (spriteSequenceData != nullptr) {
    for (int faIdx=0; faIdx < spriteSequenceData->size(); faIdx++) {
      const CozmoAnim::FaceAnimation* faceAnimKeyframe = spriteSequenceData->Get(faIdx);
      Result addResult = _spriteSequenceTrack.AddKeyFrameToBack(faceAnimKeyframe, name);
      if(addResult != RESULT_OK) {
        PRINT_NAMED_ERROR("Animation.DefineFromFlatBuf.AddKeyFrameFailure",
                          "Adding FaceAnimation frame %d failed.", faIdx);
        return addResult;
      }
    }
  }

  auto eventData = keyframes->EventKeyFrame();
  if (eventData != nullptr) {
    for (int eIdx=0; eIdx < eventData->size(); eIdx++) {
      const CozmoAnim::Event* eventKeyframe = eventData->Get(eIdx);
      Result addResult = _eventTrack.AddKeyFrameToBack(eventKeyframe, name);
      if(addResult != RESULT_OK) {
        PRINT_NAMED_ERROR("Animation.DefineFromFlatBuf.AddKeyFrameFailure",
                          "Adding Event frame %d failed.", eIdx);
        return addResult;
      }
    }
  }

  auto bodyData = keyframes->BodyMotionKeyFrame();
  if (bodyData != nullptr) {
    for (int bdyIdx=0; bdyIdx < bodyData->size(); bdyIdx++) {
      const CozmoAnim::BodyMotion* bodyKeyframe = bodyData->Get(bdyIdx);
      Result addResult = _bodyPosTrack.AddKeyFrameToBack(bodyKeyframe, name);
      if(addResult != RESULT_OK) {
        PRINT_NAMED_ERROR("Animation.DefineFromFlatBuf.AddKeyFrameFailure",
                          "Adding BodyMotion frame %d failed.", bdyIdx);
        return addResult;
      }
    }
  }
  
  auto recordHeadingData = keyframes->RecordHeadingKeyFrame();
  if (recordHeadingData != nullptr) {
    for (int rhIdx=0; rhIdx < recordHeadingData->size(); rhIdx++) {
      const CozmoAnim::RecordHeading* recordHeadingKeyframe = recordHeadingData->Get(rhIdx);
      Result addResult = _recordHeadingTrack.AddKeyFrameToBack(recordHeadingKeyframe, name);
      if(addResult != RESULT_OK) {
        PRINT_NAMED_ERROR("Animation.DefineFromFlatBuf.AddKeyFrameFailure",
                          "Adding RecordHeading frame %d failed.", rhIdx);
        return addResult;
      }
    }
  }
  
  auto turnToRecordedHeadingData = keyframes->TurnToRecordedHeadingKeyFrame();
  if (turnToRecordedHeadingData != nullptr) {
    for (int rhIdx=0; rhIdx < turnToRecordedHeadingData->size(); rhIdx++) {
      const CozmoAnim::TurnToRecordedHeading* turnToRecordedHeadingKeyframe = turnToRecordedHeadingData->Get(rhIdx);
      Result addResult = _turnToRecordedHeadingTrack.AddKeyFrameToBack(turnToRecordedHeadingKeyframe, name);
      if(addResult != RESULT_OK) {
        PRINT_NAMED_ERROR("Animation.DefineFromFlatBuf.AddKeyFrameFailure",
                          "Adding TurnToRecordedHeading frame %d failed.", rhIdx);
        return addResult;
      }
    }
  }

  return RESULT_OK;
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
    } else if(frameName == SpriteSequenceKeyFrame::GetClassName()) {
      addResult = _spriteSequenceTrack.AddKeyFrameToBack(jsonFrame, name);
    } else if(frameName == EventKeyFrame::GetClassName()) {
      addResult = _eventTrack.AddKeyFrameToBack(jsonFrame, name);
    } else if(frameName == "DeviceAudioKeyFrame") {
      // Deprecated V1 keyframe. Do nothing.
    } else if(frameName == RobotAudioKeyFrame::GetClassName()) {
      addResult = _robotAudioTrack.AddKeyFrameToBack(jsonFrame, name);
    } else if(frameName == BackpackLightsKeyFrame::GetClassName()) {
      addResult = _backpackLightsTrack.AddKeyFrameToBack(jsonFrame, name);
    } else if(frameName == BodyMotionKeyFrame::GetClassName()) {
      addResult = _bodyPosTrack.AddKeyFrameToBack(jsonFrame, name);
    } else if(frameName == RecordHeadingKeyFrame::GetClassName()) {
      addResult = _recordHeadingTrack.AddKeyFrameToBack(jsonFrame, name);
    } else if(frameName == TurnToRecordedHeadingKeyFrame::GetClassName()) {
      addResult = _turnToRecordedHeadingTrack.AddKeyFrameToBack(jsonFrame, name);
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

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
template<>
Animations::Track<HeadAngleKeyFrame>& Animation::GetTrack() {
  return _headTrack;
}

template<>
Animations::Track<LiftHeightKeyFrame>& Animation::GetTrack() {
  return _liftTrack;
}

template<>
Animations::Track<SpriteSequenceKeyFrame>& Animation::GetTrack() {
  return _spriteSequenceTrack;
}

template<>
Animations::Track<EventKeyFrame>& Animation::GetTrack() {
  return _eventTrack;
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
Animations::Track<RecordHeadingKeyFrame>& Animation::GetTrack() {
  return _recordHeadingTrack;
}

template<>
Animations::Track<TurnToRecordedHeadingKeyFrame>& Animation::GetTrack() {
  return _turnToRecordedHeadingTrack;
}

template<>
Animations::Track<ProceduralFaceKeyFrame>& Animation::GetTrack() {
  return _proceduralFaceTrack;
}
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

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
_spriteSequenceTrack.__METHOD__(__VA_ARGS__) __COMBINE_WITH__ \
_proceduralFaceTrack.__METHOD__(__VA_ARGS__) __COMBINE_WITH__ \
_eventTrack.__METHOD__(__VA_ARGS__) __COMBINE_WITH__ \
_robotAudioTrack.__METHOD__(__VA_ARGS__) __COMBINE_WITH__ \
_backpackLightsTrack.__METHOD__(__VA_ARGS__) __COMBINE_WITH__ \
_bodyPosTrack.__METHOD__(__VA_ARGS__)  __COMBINE_WITH__  \
_recordHeadingTrack.__METHOD__(__VA_ARGS__)  __COMBINE_WITH__  \
_turnToRecordedHeadingTrack.__METHOD__(__VA_ARGS__)
 

//# define ALL_TRACKS(__METHOD__, __ARG__, __COMBINE_WITH__) ALL_TRACKS_WITH_ARG(__METHOD__, void, __COMBINE_WITH__)

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Result Animation::Init()
{

#   if DEBUG_ANIMATIONS
  PRINT_NAMED_INFO("Animation.Init", "Initializing animation '%s'", GetName().c_str());
#   endif
  
  ALL_TRACKS(MoveToStart, ;);
  
  _isInitialized = true;
  
  return RESULT_OK;
} // Animation::Init()

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Animation::Clear()
{
  ALL_TRACKS(Clear, ;);
  _isInitialized = false;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool Animation::IsEmpty() const
{
  return ALL_TRACKS(IsEmpty, &&);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool Animation::HasFramesLeft() const
{
  return ALL_TRACKS(HasFramesLeft, ||);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Animation::SetIsLive(bool isLive)
{
  _isLive = isLive;
  ALL_TRACKS(SetIsLive, ;, isLive);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Animation::AppendAnimation(const Animation& appendAnim)
{
  // Append animation starting at the next keyframe
  const uint32_t animOffest_ms = GetLastKeyFrameTime_ms() + ANIM_TIME_STEP_MS;
  
  // Append animation tracks
  _headTrack.AppendTrack(appendAnim.GetTrack<HeadAngleKeyFrame>(), animOffest_ms);
  _liftTrack.AppendTrack(appendAnim.GetTrack<LiftHeightKeyFrame>(), animOffest_ms);
  _spriteSequenceTrack.AppendTrack(appendAnim.GetTrack<SpriteSequenceKeyFrame>(), animOffest_ms);
  _proceduralFaceTrack.AppendTrack(appendAnim.GetTrack<ProceduralFaceKeyFrame>(), animOffest_ms);
  _eventTrack.AppendTrack(appendAnim.GetTrack<EventKeyFrame>(), animOffest_ms);
  _backpackLightsTrack.AppendTrack(appendAnim.GetTrack<BackpackLightsKeyFrame>(), animOffest_ms);
  _bodyPosTrack.AppendTrack(appendAnim.GetTrack<BodyMotionKeyFrame>(), animOffest_ms);
  _recordHeadingTrack.AppendTrack(appendAnim.GetTrack<RecordHeadingKeyFrame>(), animOffest_ms);
  _turnToRecordedHeadingTrack.AppendTrack(appendAnim.GetTrack<TurnToRecordedHeadingKeyFrame>(), animOffest_ms);
  _robotAudioTrack.AppendTrack(appendAnim.GetTrack<RobotAudioKeyFrame>(), animOffest_ms);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uint32_t Animation::GetLastKeyFrameTime_ms()
{
  // Get Last keyframe of every track to find the last one in time_ms
  TimeStamp_t lastFrameTime_ms = 0;
  
  lastFrameTime_ms = CompareLastFrameTime<RobotAudioKeyFrame>(lastFrameTime_ms);
  lastFrameTime_ms = CompareLastFrameTime<HeadAngleKeyFrame>(lastFrameTime_ms);
  lastFrameTime_ms = CompareLastFrameTime<LiftHeightKeyFrame>(lastFrameTime_ms);
  lastFrameTime_ms = CompareLastFrameTime<BodyMotionKeyFrame>(lastFrameTime_ms);
  lastFrameTime_ms = CompareLastFrameTime<RecordHeadingKeyFrame>(lastFrameTime_ms);
  lastFrameTime_ms = CompareLastFrameTime<TurnToRecordedHeadingKeyFrame>(lastFrameTime_ms);
  lastFrameTime_ms = CompareLastFrameTime<EventKeyFrame>(lastFrameTime_ms);
  lastFrameTime_ms = CompareLastFrameTime<SpriteSequenceKeyFrame>(lastFrameTime_ms);
  lastFrameTime_ms = CompareLastFrameTime<BackpackLightsKeyFrame>(lastFrameTime_ms);
  lastFrameTime_ms = CompareLastFrameTime<ProceduralFaceKeyFrame>(lastFrameTime_ms);

  return lastFrameTime_ms;
}

  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uint32_t Animation::GetLastKeyFrameEndTime_ms()
{
  // Get Last keyframe of every track to find the last one in time_ms
  TimeStamp_t lastFrameTime_ms = 0;
  
  lastFrameTime_ms = CompareLastFrameEndTime<RobotAudioKeyFrame>(lastFrameTime_ms);
  lastFrameTime_ms = CompareLastFrameEndTime<HeadAngleKeyFrame>(lastFrameTime_ms);
  lastFrameTime_ms = CompareLastFrameEndTime<LiftHeightKeyFrame>(lastFrameTime_ms);
  lastFrameTime_ms = CompareLastFrameEndTime<BodyMotionKeyFrame>(lastFrameTime_ms);
  lastFrameTime_ms = CompareLastFrameEndTime<RecordHeadingKeyFrame>(lastFrameTime_ms);
  lastFrameTime_ms = CompareLastFrameEndTime<TurnToRecordedHeadingKeyFrame>(lastFrameTime_ms);
  lastFrameTime_ms = CompareLastFrameEndTime<EventKeyFrame>(lastFrameTime_ms);
  lastFrameTime_ms = CompareLastFrameEndTime<SpriteSequenceKeyFrame>(lastFrameTime_ms);
  lastFrameTime_ms = CompareLastFrameEndTime<BackpackLightsKeyFrame>(lastFrameTime_ms);
  lastFrameTime_ms = CompareLastFrameEndTime<ProceduralFaceKeyFrame>(lastFrameTime_ms);
  
  return lastFrameTime_ms;
}

  
template<class KeyFrameType>
TimeStamp_t Animation::CompareLastFrameTime(const TimeStamp_t lastFrameTime_ms)
{
  const auto& track = GetTrack<KeyFrameType>();
  if (!track.IsEmpty()) {
    // Compare track's last key frame time and lastFrameTime_ms
    return std::max(lastFrameTime_ms, track.GetLastKeyFrame()->GetTriggerTime());
  }
  // No key frames in track
  return lastFrameTime_ms;
}
  
  
template<class KeyFrameType>
TimeStamp_t Animation::CompareLastFrameEndTime(const TimeStamp_t lastFrameTime_ms)
{
  const auto& track = GetTrack<KeyFrameType>();
  if (!track.IsEmpty()) {
    // Compare track's last key frame time and lastFrameTime_ms
    return std::max(lastFrameTime_ms, track.GetLastKeyFrame()->GetKeyFrameFinalTimestamp_ms());
  }
  // No key frames in track
  return lastFrameTime_ms;
}

} // namespace Cozmo
} // namespace Anki

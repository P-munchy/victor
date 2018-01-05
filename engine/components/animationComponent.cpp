/**
 * File: animationComponent.cpp
 *
 * Author: Kevin Yoon
 * Created: 2017-08-01
 *
 * Description: Control interface for animation process to manage execution of 
 *              canned and idle animations
 *
 * Copyright: Anki, Inc. 2017
 *
 **/

#include "engine/ankiEventUtil.h"
#include "engine/animations/animationGroup/animationGroupContainer.h"
#include "engine/components/animationComponent.h"
#include "engine/cozmoContext.h"
#include "engine/robot.h"
#include "engine/robotManager.h"
#include "engine/robotInterface/messageHandler.h"

#include "clad/types/animationTypes.h"

#include "coretech/common/engine/array2d_impl.h"
#include "coretech/common/engine/utils/data/dataPlatform.h"
#include "coretech/common/engine/utils/timer.h"

#include "json/json.h"

namespace Anki {
namespace Cozmo {
 
namespace {
  static const char* kLogChannelName = "Animations";

  const u32 kMaxNumAvailableAnimsToReportPerTic = 50;

  static const u32 kNumImagePixels     = FACE_DISPLAY_HEIGHT * FACE_DISPLAY_WIDTH;
  static const u32 kNumHalfImagePixels = kNumImagePixels / 2;
}
  
  
AnimationComponent::AnimationComponent(Robot& robot, const CozmoContext* context)
: _isInitialized(false)
, _tagCtr(0)
, _robot(robot)
, _animationGroups(context->GetRobotManager()->GetAnimationGroups())
, _isDolingAnims(false)
, _nextAnimToDole("")
, _currPlayingAnim("")
, _lockedTracks(0)
, _isAnimating(false)
, _currAnimName("")
, _currAnimTag(0)
{
  if (context) {
    // Setup game message handlers
    IExternalInterface *extInterface = context->GetExternalInterface();
    if (extInterface != nullptr) {
      
      auto helper = MakeAnkiEventUtil(*extInterface, *this, GetSignalHandles());
  
      using namespace ExternalInterface;
      helper.SubscribeGameToEngine<MessageGameToEngineTag::RequestAvailableAnimations>();
      helper.SubscribeGameToEngine<MessageGameToEngineTag::DisplayProceduralFace>();
      helper.SubscribeGameToEngine<MessageGameToEngineTag::SetFaceHue>();
      helper.SubscribeGameToEngine<MessageGameToEngineTag::DisplayFaceImageBinaryChunk>();
    }
  }
  
  // Setup robot message handlers
  RobotInterface::MessageHandler *messageHandler = robot.GetContext()->GetRobotManager()->GetMsgHandler();
  RobotID_t robotId = robot.GetID();

  // Subscribe to RobotToEngine messages
  using localHandlerType = void(AnimationComponent::*)(const AnkiEvent<RobotInterface::RobotToEngine>&);
  // Create a helper lambda for subscribing to a tag with a local handler
  auto doRobotSubscribe = [this, robotId, messageHandler] (RobotInterface::RobotToEngineTag tagType, localHandlerType handler)
  {
    GetSignalHandles().push_back(messageHandler->Subscribe(robotId, tagType, std::bind(handler, this, std::placeholders::_1)));
  };
  
  // bind to specific handlers
  doRobotSubscribe(RobotInterface::RobotToEngineTag::animStarted,           &AnimationComponent::HandleAnimStarted);
  doRobotSubscribe(RobotInterface::RobotToEngineTag::animEnded,             &AnimationComponent::HandleAnimEnded);
  doRobotSubscribe(RobotInterface::RobotToEngineTag::animEvent,             &AnimationComponent::HandleAnimationEvent);
  doRobotSubscribe(RobotInterface::RobotToEngineTag::animState,             &AnimationComponent::HandleAnimState);
}

void AnimationComponent::Init()
{
  // Open manifest file
  static const std::string manifestFile = "assets/anim_manifest.json";
  Json::Value jsonManifest;
  const bool success = _robot.GetContext()->GetDataPlatform()->readAsJson(Util::Data::Scope::Resources, manifestFile, jsonManifest);
  if (!success) {
    PRINT_NAMED_ERROR("AnimationComponent.Init.ManifestNotFound", "");
    return;
  }

  // Process animations in manifest
  _availableAnims.clear();
  for (int i=0; i<jsonManifest.size(); ++i) {
    const Json::Value& jsonAnim = jsonManifest[i];
    
    static const char* kNameField = "name";
    static const char* kLengthField = "length_ms";

    if (!jsonAnim.isMember(kNameField)) {
      PRINT_NAMED_ERROR("AnimationComponent.Init.MissingJsonField", "%s", kNameField);
      continue;
    }

    if (!jsonAnim.isMember(kLengthField)) {
      PRINT_NAMED_ERROR("AnimationComponent.Init.MissingJsonField", "%s", kLengthField);
      continue;
    }

    _availableAnims[jsonAnim[kNameField].asCString()].length_ms = jsonAnim[kLengthField].asInt();
  }
  PRINT_CH_INFO(kLogChannelName, "AnimationComponent.Init.ManifestRead", "%zu animations loaded", _availableAnims.size());

  _isInitialized = true;
  
}
  
void AnimationComponent::Update()
{
  if (_isInitialized) {
    DoleAvailableAnimations();
  }
  
  // Check for entries that have stayed in _callbackMap for too long
  const float currTime_sec = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
  auto it = _callbackMap.begin();
  while(it != _callbackMap.end()) {
    if (it->second.abortTime_sec != 0 && currTime_sec >= it->second.abortTime_sec) {
      PRINT_NAMED_WARNING("AnimationComponent.Update.AnimTimedOut", "Anim: %s", it->second.animName.c_str());
      _robot.SendRobotMessage<RobotInterface::AbortAnimation>();
      it->second.ExecuteCallback(AnimResult::Timedout);
      it = _callbackMap.erase(it);
    }
    else {
      ++it;
    }
  }
}
 

Result AnimationComponent::GetAnimationMetaInfo(const std::string& animName, AnimationMetaInfo& metaInfo) const
{
  auto it = _availableAnims.find(animName);
  if (it != _availableAnims.end()) {
    metaInfo = it->second;
    return RESULT_OK;
  }
  
  return RESULT_FAIL;
}

// Doles animations (the max number that can be doled per tic) to game if requested
void AnimationComponent::DoleAvailableAnimations()
{
  if (_isDolingAnims) {
    u32 numAnimsDoledThisTic = 0;

    auto it = _nextAnimToDole.empty() ? _availableAnims.begin() : _availableAnims.find(_nextAnimToDole);
    for (; it != _availableAnims.end() && numAnimsDoledThisTic < kMaxNumAvailableAnimsToReportPerTic; ++it) {
      _robot.Broadcast(ExternalInterface::MessageEngineToGame(ExternalInterface::AnimationAvailable(it->first)));
      ++numAnimsDoledThisTic;
    }
    if (it == _availableAnims.end()) {
      PRINT_CH_INFO(kLogChannelName, "DoleAvailableAnimations.Done", "");
      _isDolingAnims = false;
      _nextAnimToDole = "";
      _robot.Broadcast(ExternalInterface::MessageEngineToGame(ExternalInterface::EndOfMessage(ExternalInterface::MessageType::AnimationAvailable)));
    } else {
      _nextAnimToDole = it->first;
    }
  }
}


const std::string& AnimationComponent::GetAnimationNameFromGroup(const std::string& name, bool strictCooldown) const
{
  const AnimationGroup* group = _animationGroups.GetAnimationGroup(name);
  if(group != nullptr && !group->IsEmpty()) {
    return group->GetAnimationName(_robot.GetMoodManager(), _animationGroups, _robot.GetHeadAngle(), strictCooldown);
  }
  static const std::string empty("");
  return empty;
}

  
Result AnimationComponent::PlayAnimByName(const std::string& animName,
                                          int numLoops,
                                          bool interruptRunning,
                                          AnimationCompleteCallback callback,
                                          const u32 actionTag,
                                          float timeout_sec)
{
  if (!_isInitialized) {
    PRINT_NAMED_WARNING("AnimationComponent.PlayAnimByName.Uninitialized", "");
    return RESULT_FAIL;
  }
  
  // Check that animName is valid
  auto it = _availableAnims.find(animName);
  if (it == _availableAnims.end()) {
    PRINT_NAMED_WARNING("AnimationComponent.PlayAnimByName.AnimNotFound", "%s", animName.c_str());
    return RESULT_FAIL;
  }
  
  PRINT_CH_DEBUG(kLogChannelName, "AnimationComponent.PlayAnimByName.PlayingAnim", "%s", it->first.c_str());

  // Check that a valid actionTag was specified if there is non-empty callback
  if (callback != nullptr && actionTag == 0) {
    PRINT_NAMED_WARNING("AnimationComponent.PlayAnimByName.MissingActionTag", "");
    return RESULT_FAIL;
  }
  
  // TODO: Is this what interruptRunning should mean?
  //       Or should it queue on anim process side and optionally interrupt currently executing anim?
  if (IsPlayingAnimation() && !interruptRunning) {
    PRINT_NAMED_WARNING("AnimationComponent.PlayAnimByName.WontInterruptCurrentAnim", "");
    return RESULT_FAIL;
  }
  
  const Tag currTag = GetNextTag();
  if (_robot.SendRobotMessage<RobotInterface::PlayAnim>(numLoops, currTag, animName) == RESULT_OK) {
    // Check if tag already exists in callback map.
    // If so, trigger callback with Stale
    {
      auto it = _callbackMap.find(currTag);
      if (it != _callbackMap.end()) {
        PRINT_NAMED_WARNING("AnimationComponent.PlayAnimByName.StaleTag", "%d", currTag);
        it->second.ExecuteCallback(AnimResult::Stale);
        _callbackMap.erase(it);
      }
    }
    const float abortTime_sec = (numLoops > 0 ? BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + timeout_sec : 0);
    _callbackMap.emplace(std::piecewise_construct,
                         std::forward_as_tuple(currTag),
                         std::forward_as_tuple(animName, callback, actionTag, abortTime_sec));
  }
  
  return RESULT_OK;
}
  
AnimationComponent::Tag AnimationComponent::IsAnimPlaying(const std::string& animName)
{
  for (auto it = _callbackMap.begin(); it != _callbackMap.end(); ++it) {
    if (it->second.animName == animName) {
      return it->first;
    }
  }
  return kNotAnimatingTag;
}
  
  
Result AnimationComponent::StopAnimByName(const std::string& animName)
{
  // Verify that the animation is currently playing
  const auto it = _availableAnims.find(animName);
  if (it != _availableAnims.end()) {
    const Tag tag = IsAnimPlaying(animName);
    if (tag != kNotAnimatingTag) {
      PRINT_CH_DEBUG(kLogChannelName, "AnimationComponent.StopAnimByName.AbortingAnim", "%s", animName.c_str());
      return _robot.SendRobotMessage<RobotInterface::AbortAnimation>();
    }
    else {
      PRINT_NAMED_WARNING("AnimationComponent.StopAnimByName.AnimNotPlaying",
                          "%s", animName.c_str());
      return RESULT_OK;
    }
  }
  else {
    PRINT_NAMED_WARNING("AnimationComponent.StopAnimByName.InvalidName",
                        "%s", animName.c_str());
    return RESULT_FAIL;
  }

}

  
// Enables only the specified tracks. 
// Status of other tracks remain unchanged.
void AnimationComponent::UnlockTracks(u8 tracks)
{
  _lockedTracks &= ~tracks;
  _robot.SendRobotMessage<RobotInterface::LockAnimTracks>(_lockedTracks);
}

void AnimationComponent::UnlockAllTracks()
{
  if (_lockedTracks != 0) {
    _lockedTracks = 0;
    _robot.SendRobotMessage<RobotInterface::LockAnimTracks>(_lockedTracks);
  }
}

// Disables only the specified tracks. 
// Status of other tracks remain unchanged.
void AnimationComponent::LockTracks(u8 tracks)
{
  _lockedTracks |= tracks;
  _robot.SendRobotMessage<RobotInterface::LockAnimTracks>(_lockedTracks);
}


Result AnimationComponent::DisplayFaceImageBinary(const Vision::Image& img, u32 duration_ms, bool interruptRunning)
{
  if (!_isInitialized) {
    PRINT_NAMED_WARNING("AnimationComponent.DisplayFaceImageBinary.Uninitialized", "");
    return RESULT_FAIL;
  }
  
  // TODO: Is this what interruptRunning should mean?
  //       Or should it queue on anim process side and optionally interrupt currently executing anim?
  if (IsPlayingAnimation() && !interruptRunning) {
    PRINT_NAMED_WARNING("AnimationComponent.DisplayFaceImageBinary.WontInterruptCurrentAnim", "");
    return RESULT_FAIL;
  }

  // Verify that image is expected size
  const bool imageIsValidSize = (img.GetNumRows() == FACE_DISPLAY_HEIGHT) && 
                                (img.GetNumCols() == FACE_DISPLAY_WIDTH) &&
                                (img.GetNumChannels() == 1) &&
                                img.IsContinuous();
  
  if (!ANKI_VERIFY(imageIsValidSize, 
                   "AnimationComponent.DisplayFaceImageBinary.InvalidImageSize", 
                   "%d x %d (continuous: %d), expected %d x %d", 
                   img.GetNumCols(), img.GetNumRows(), img.IsContinuous(), FACE_DISPLAY_WIDTH, FACE_DISPLAY_HEIGHT)) {
    return RESULT_FAIL;
  }

  // Convert image into bit images (top half and bottom half)
  const u8* imageData_i = img.GetDataPointer();

  for (int halfIdx = 0; halfIdx < 2; ++halfIdx) {

    RobotInterface::DisplayFaceImageBinaryChunk msg;
    static const u32 kFaceArraySize = sizeof(msg.faceData);
    static_assert(8 * kFaceArraySize == kNumHalfImagePixels, "AnimationComponent.DisplayFaceImageBinary.WrongFaceDataSize");
  
    msg.imageId = 0;
    msg.chunkIndex = halfIdx;
    msg.duration_ms = duration_ms;

    u8* byte = msg.faceData.data();
    for (int i=0; i < kFaceArraySize; ++i) {
      *byte = 0;
      for (int b = 7; b >= 0; --b){
        if (*imageData_i > 0) {
          *byte |= 1 << b;
        }
        ++imageData_i;
      }
      ++byte;
    }
    _robot.SendMessage(RobotInterface::EngineToRobot(std::move(msg)));
  }

  return RESULT_OK;
}

Result AnimationComponent::DisplayFaceImage(const Vision::ImageRGB565& imgRGB565, u32 duration_ms, bool interruptRunning)
{
  if (!_isInitialized) {
    PRINT_NAMED_WARNING("AnimationComponent.DisplayFaceImage.Uninitialized", "");
    return RESULT_FAIL;
  }
  
  // TODO: Is this what interruptRunning should mean?
  //       Or should it queue on anim process side and optionally interrupt currently executing anim?
  if (IsPlayingAnimation() && !interruptRunning) {
    PRINT_NAMED_WARNING("AnimationComponent.DisplayFaceImage.WontInterruptCurrentAnim", "");
    return RESULT_FAIL;
  }

  ASSERT_NAMED(imgRGB565.IsContinuous(), "AnimationComponent.DisplayFaceImage.NotContinuous");
  
  static const int kMaxPixelsPerMsg = RobotInterface::DisplayFaceImageRGBChunk().faceData.size();
  
  int chunkCount = 0;
  int pixelsLeftToSend = FACE_DISPLAY_NUM_PIXELS;
  const u16* startIt = imgRGB565.GetRawDataPointer();
  while (pixelsLeftToSend > 0) {
    RobotInterface::DisplayFaceImageRGBChunk msg;
    msg.duration_ms = duration_ms;
    msg.imageId = 0;
    msg.chunkIndex = chunkCount++;
    msg.numPixels = std::min(kMaxPixelsPerMsg, pixelsLeftToSend);

    std::copy_n(startIt, msg.numPixels, std::begin(msg.faceData));

    pixelsLeftToSend -= msg.numPixels;
    std::advance(startIt, msg.numPixels);

    _robot.SendMessage(RobotInterface::EngineToRobot(std::move(msg)));
  }

  static const int kExpectedNumChunks = static_cast<int>(std::ceilf( (f32)FACE_DISPLAY_NUM_PIXELS / kMaxPixelsPerMsg ));
  DEV_ASSERT_MSG(chunkCount == kExpectedNumChunks, "AnimationComponent.DisplayFaceImage.UnexpectedNumChunks", "%d", chunkCount);

  return RESULT_OK;
}

Result AnimationComponent::DisplayFaceImage(const Vision::ImageRGB& img, u32 duration_ms, bool interruptRunning)
{
  static Vision::ImageRGB565 img565; // static to avoid repeatedly allocating this once it's used
  img565.SetFromImageRGB(img);
  return DisplayFaceImage(img565, duration_ms, interruptRunning);
}



// ================ Game messsage handlers ======================
template<>
void AnimationComponent::HandleMessage(const ExternalInterface::RequestAvailableAnimations& msg)
{
  PRINT_CH_INFO("AnimationComponent", "RequestAvailableAnimations.Recvd", "");
  _isDolingAnims = true;
}
  
template<>
void AnimationComponent::HandleMessage(const ExternalInterface::DisplayProceduralFace& msg)
{
  if (!_isInitialized) {
    PRINT_NAMED_WARNING("AnimationComponent.DisplayProceduralFace.Uninitialized", "");
    return;
  }
  
  // TODO: Is this what interruptRunning should mean?
  //       Or should it queue on anim process side and optionally interrupt currently executing anim?
  if (IsPlayingAnimation() && !msg.interruptRunning) {
    PRINT_NAMED_WARNING("AnimationComponent.DisplayProceduralFace.WontInterruptCurrentAnim", "");
    return;
  }

  // Convert ExternalInterface version of DisplayProceduralFace to RobotInterface version and send
  _robot.SendRobotMessage<RobotInterface::DisplayProceduralFace>(msg.faceParams, msg.duration_ms);
}

template<>
void AnimationComponent::HandleMessage(const ExternalInterface::SetFaceHue& msg)
{
  _robot.SendRobotMessage<RobotInterface::SetFaceHue>(msg.hue);
}

template<>
void AnimationComponent::HandleMessage(const ExternalInterface::DisplayFaceImageBinaryChunk& msg)
{
  if (!_isInitialized) {
    PRINT_NAMED_WARNING("AnimationComponent.HandleDisplayFaceImageBinaryChunk.Uninitialized", "");
    return;
  }
  
  // TODO: Is this what interruptRunning should mean?
  //       Or should it queue on anim process side and optionally interrupt currently executing anim?
  if (IsPlayingAnimation() && !msg.interruptRunning) {
    PRINT_NAMED_WARNING("AnimationComponent.HandleDisplayFaceImage.WontInterruptCurrentAnim", "");
    return;
  }

  // Convert ExternalInterface version of DisplayFaceImage to RobotInterface version and send
  _robot.SendRobotMessage<RobotInterface::DisplayFaceImageBinaryChunk>(msg.duration_ms, msg.faceData, msg.imageId, msg.chunkIndex);
}

// ================ Robot message handlers ======================

void AnimationComponent::HandleAnimStarted(const AnkiEvent<RobotInterface::RobotToEngine>& message)
{
  const auto & payload = message.GetData().Get_animStarted();
  auto it = _callbackMap.find(payload.tag);
  if (it != _callbackMap.end()) {
    PRINT_CH_INFO("AnimationComponent", "AnimStarted.Tag", "name=%s, tag=%d", payload.animName.c_str(), payload.tag);
  } else if (payload.animName != EnumToString(AnimConstants::PROCEDURAL_ANIM) ) {
    //PRINT_NAMED_WARNING("AnimationComponent.AnimStarted.UnexpectedTag", "name=%s, tag=%d", payload.animName.c_str(), payload.tag);
    return;
  }

  _isAnimating = true;
  _currAnimName = payload.animName;
  _currAnimTag = payload.tag;

  _robot.GetContext()->GetVizManager()->SendCurrentAnimation(_currAnimName, _currAnimTag);
}

void AnimationComponent::HandleAnimEnded(const AnkiEvent<RobotInterface::RobotToEngine>& message)
{
  const auto & payload = message.GetData().Get_animEnded();
  
  // Verify that expected animation completed and execute callback
  auto it = _callbackMap.find(payload.tag);
  if (it != _callbackMap.end()) {
    PRINT_CH_INFO("AnimationComponent", "AnimEnded.Tag", "name=%s, tag=%d", payload.animName.c_str(), payload.tag);
    it->second.ExecuteCallback(payload.wasAborted ? AnimResult::Aborted : AnimResult::Completed);
    _callbackMap.erase(it);
  } else if (payload.animName != EnumToString(AnimConstants::PROCEDURAL_ANIM) ) {
    //PRINT_NAMED_WARNING("AnimationComponent.AnimEnded.UnexpectedTag", "name=%s, tag=%d", payload.animName.c_str(), payload.tag);
    return;
  }

  _isAnimating = false;
  DEV_ASSERT_MSG(_currAnimName == payload.animName, "AnimationComponent.AnimEnded.UnexpectedName", "Got %s, expected %s", payload.animName.c_str(), _currAnimName.c_str());
  DEV_ASSERT_MSG(_currAnimTag == payload.tag, "AnimationComponent.AnimEnded.UnexpectedTag", "Got %d, expected %d", payload.tag, _currAnimTag);

  _currAnimName = "";
  _currAnimTag = kNotAnimatingTag;

  _robot.GetContext()->GetVizManager()->SendCurrentAnimation(_currAnimName, _currAnimTag);
}
  
void AnimationComponent::HandleAnimationEvent(const AnkiEvent<RobotInterface::RobotToEngine>& message)
{
  const auto & payload = message.GetData().Get_animEvent();
  auto it = _callbackMap.find(payload.tag);
  if (it != _callbackMap.end()) {
    PRINT_CH_INFO("AnimationComponent", "HandleAnimationEvent", "%s", EnumToString(payload.event_id));
    ExternalInterface::AnimationEvent msg;
    msg.timestamp = payload.timestamp;
    msg.event_id = payload.event_id;
    _robot.GetExternalInterface()->BroadcastToGame<ExternalInterface::AnimationEvent>(std::move(msg));
  }
}
  
void AnimationComponent::HandleAnimState(const AnkiEvent<RobotInterface::RobotToEngine>& message)
{
  _animState = message.GetData().Get_animState();
}
  
  
} // namespace Cozmo
} // namespace Anki

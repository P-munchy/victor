/**
 * File: behaviorProceduralClock.cpp
 *
 * Author: Kevin M. Karol
 * Created: 1/31/18
 *
 * Description: Behavior which displays a procedural clock on the robot's face
 *
 * Copyright: Anki, Inc. 208
 *
 **/


#include "engine/aiComponent/behaviorComponent/behaviors/timer/behaviorProceduralClock.h"

#include "cannedAnimLib/proceduralFace/proceduralFace.h"
#include "engine/actions/animActions.h"
#include "engine/actions/basicActions.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/beiRobotInfo.h"
#include "engine/aiComponent/aiComponent.h"
#include "engine/aiComponent/faceSelectionComponent.h"
#include "engine/aiComponent/timerUtility.h"
#include "engine/components/animationComponent.h"
#include "engine/faceWorld.h"


#include "coretech/common/engine/jsonTools.h"
#include "coretech/common/engine/utils/timer.h"
#include "coretech/vision/shared/spritePathMap.h"

namespace Anki {
namespace Vector {

namespace{
const char* kGetInTriggerKey        = "getInAnimTrigger";
const char* kGetOutTriggerKey       = "getOutAnimTrigger";
const char* kDisplayClockSKey       = "displayClockFor_s";
const char* kShouldTurnToFaceKey    = "shouldTurnToFace";
const char* kShouldPlayAudioKey     = "shouldPlayAudioOnClockUpdates";

const Vision::SpritePathMap::AssetID kClockEmptyGridSpriteID = 
  Vision::SpritePathMap::GetAssetID("clock_empty_grid");

const std::vector<Vision::SpritePathMap::AssetID> kDigitMap = 
{
  Vision::SpritePathMap::GetAssetID("clock_00"),
  Vision::SpritePathMap::GetAssetID("clock_01"),
  Vision::SpritePathMap::GetAssetID("clock_02"),
  Vision::SpritePathMap::GetAssetID("clock_03"),
  Vision::SpritePathMap::GetAssetID("clock_04"),
  Vision::SpritePathMap::GetAssetID("clock_05"),
  Vision::SpritePathMap::GetAssetID("clock_06"),
  Vision::SpritePathMap::GetAssetID("clock_07"),
  Vision::SpritePathMap::GetAssetID("clock_08"),
  Vision::SpritePathMap::GetAssetID("clock_09")
};

// SpriteBoxKeyFrame Definitions
const Vision::SpriteBoxKeyFrame kTensLeftOfColonKeyFrame(
  0.0f,
  Vision::SpriteBox(
    100.0f,
    kClockEmptyGridSpriteID,
    27,
    26,
    29,
    43,
    Vision::SpriteBoxName::TensLeftOfColon,
    Anki::Vision::LayerName::Layer_6,
    Anki::Vision::SpriteRenderMethod::EyeColor,
    Anki::Vision::SpriteSeqEndType::Clear,
    {{0,0}}
  )
);

const Vision::SpriteBoxKeyFrame kOnesLeftOfColonKeyFrame(
  0.0f,
  Vision::SpriteBox(
    100.0f,
    kClockEmptyGridSpriteID,
    57,
    26,
    29,
    43,
    Vision::SpriteBoxName::OnesLeftOfColon,
    Anki::Vision::LayerName::Layer_6,
    Anki::Vision::SpriteRenderMethod::EyeColor,
    Anki::Vision::SpriteSeqEndType::Clear,
    {{0,0}}
  )
);

const Vision::SpriteBoxKeyFrame kColonKeyFrame(
  0.0f,
  Vision::SpriteBox(
    100.0f,
    Vision::SpritePathMap::GetAssetID("clock_colon"),
    87,
    27,
    10,
    43,
    Vision::SpriteBoxName::Colon,
    Anki::Vision::LayerName::Layer_6,
    Anki::Vision::SpriteRenderMethod::EyeColor,
    Anki::Vision::SpriteSeqEndType::Clear,
    {{0,0}}
  )
);

const Vision::SpriteBoxKeyFrame kTensRightOfColonKeyFrame(
  0.0f,
  Vision::SpriteBox(
    100.0f,
    kClockEmptyGridSpriteID,
    98,
    26,
    29,
    43,
    Vision::SpriteBoxName::TensRightOfColon,
    Anki::Vision::LayerName::Layer_6,
    Anki::Vision::SpriteRenderMethod::EyeColor,
    Anki::Vision::SpriteSeqEndType::Clear,
    {{0,0}}
  )
);

const Vision::SpriteBoxKeyFrame kOnesRightOfColonKeyFrame(
  0.0f,
  Vision::SpriteBox(
    100.0f,
    kClockEmptyGridSpriteID,
    128,
    26,
    29,
    43,
    Vision::SpriteBoxName::OnesRightOfColon,
    Anki::Vision::LayerName::Layer_6,
    Anki::Vision::SpriteRenderMethod::EyeColor,
    Anki::Vision::SpriteSeqEndType::Clear,
    {{0,0}}
  )
);

const std::map<Vision::SpriteBoxName, Vision::SpriteBoxKeyFrame> kKeyFrameMap =
{
  {Vision::SpriteBoxName::TensLeftOfColon,  kTensLeftOfColonKeyFrame},
  {Vision::SpriteBoxName::OnesLeftOfColon,  kOnesLeftOfColonKeyFrame},
  {Vision::SpriteBoxName::Colon,            kColonKeyFrame},
  {Vision::SpriteBoxName::TensRightOfColon, kTensRightOfColonKeyFrame},
  {Vision::SpriteBoxName::OnesRightOfColon, kOnesRightOfColonKeyFrame}
};

} // namespace

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorProceduralClock::BehaviorProceduralClock(const Json::Value& config)
: ICozmoBehavior(config)
{
  const std::string kDebugStr = "BehaviorProceduralClock.ParsingIssue";

  _instanceParams.getInAnim = AnimationTriggerFromString(JsonTools::ParseString(config, kGetInTriggerKey, kDebugStr));
  _instanceParams.getOutAnim = AnimationTriggerFromString(JsonTools::ParseString(config, kGetOutTriggerKey, kDebugStr));
  _instanceParams.totalTimeDisplayClock_sec = static_cast<float>(JsonTools::ParseUint8(config, kDisplayClockSKey, kDebugStr));
  JsonTools::GetValueOptional(config, kShouldTurnToFaceKey, _instanceParams.shouldTurnToFace);
  JsonTools::GetValueOptional(config, kShouldPlayAudioKey, _instanceParams.shouldPlayAudioOnClockUpdates);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorProceduralClock::GetBehaviorJsonKeys(std::set<const char*>& expectedKeys) const
{
  const char* list[] = {
    kGetInTriggerKey,
    kGetOutTriggerKey,
    kDisplayClockSKey,
    kShouldTurnToFaceKey,
    kShouldPlayAudioKey
  };
  expectedKeys.insert( std::begin(list), std::end(list) );
  GetBehaviorJsonKeysInternal(expectedKeys);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorProceduralClock::InitBehavior()
{
  auto& timerUtility = GetBEI().GetAIComponent().GetComponent<TimerUtility>();
  if(_instanceParams.getDigitFunction == nullptr){
    GetDigitsFunction countUpFunction = [&timerUtility](const int offset){
      std::map<Vision::SpriteBoxName, int> outMap;
      const int currentTime_s = timerUtility.GetSystemTime_s() + offset;
      // Ten Mins Digit
      {          
        outMap.emplace(std::make_pair(Vision::SpriteBoxName::TensLeftOfColon, 
                                      TimerHandle::SecondsToDisplayMinutes(currentTime_s)/10));
      }
      // One Mins Digit
      {
        outMap.emplace(std::make_pair(Vision::SpriteBoxName::OnesLeftOfColon, 
                                            TimerHandle::SecondsToDisplayMinutes(currentTime_s) % 10));
      }
      // Ten seconds digit
      {
        outMap.emplace(std::make_pair(Vision::SpriteBoxName::TensRightOfColon, 
                                      TimerHandle::SecondsToDisplaySeconds(currentTime_s)/10));
      }
      // One seconds digit
      {
        outMap.emplace(std::make_pair(Vision::SpriteBoxName::OnesRightOfColon, 
                       TimerHandle::SecondsToDisplaySeconds(currentTime_s) % 10));
      }
      return outMap;
    };

    SetGetDigitFunction(std::move(countUpFunction));
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorProceduralClock::OnBehaviorActivated() 
{
  _lifetimeParams = LifetimeParams();

  // Set up the colon keyframes and use them to set the animation duration
  Vision::SpriteBoxKeyFrame colonStartKeyFrame = kKeyFrameMap.at(Vision::SpriteBoxName::Colon);
  Vision::SpriteBoxKeyFrame colonEndKeyFrame = kKeyFrameMap.at(Vision::SpriteBoxName::Colon);

  int timeToDisplay_ms = Util::SecToMilliSec(_instanceParams.totalTimeDisplayClock_sec);
  colonEndKeyFrame.triggerTime_ms = timeToDisplay_ms - (timeToDisplay_ms % ANIM_TIME_STEP_MS);
  _lifetimeParams.keyFrames.push_back(colonStartKeyFrame);
  _lifetimeParams.keyFrames.push_back(colonEndKeyFrame);

  if(_instanceParams.shouldTurnToFace && UpdateTargetFace().IsValid()){
    TransitionToTurnToFace();
  }else{
    TransitionToGetIn();
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorProceduralClock::TransitionToTurnToFace()
{
  if(ANKI_VERIFY(_lifetimeParams.targetFaceID.IsValid(),
     "BehaviorProceduralClock.TransitionToTurnToFace.InvalidFace","")){
    DelegateIfInControl(new TurnTowardsFaceAction(_lifetimeParams.targetFaceID, M_PI_F, true),
                        &BehaviorProceduralClock::TransitionToGetIn);
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorProceduralClock::TransitionToGetIn()
{
  _lifetimeParams.currentState = BehaviorState::GetIn;
  DelegateIfInControl(new TriggerAnimationAction(_instanceParams.getInAnim), 
                      &BehaviorProceduralClock::TransitionToShowClock);
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorProceduralClock::TransitionToShowClock()
{  
  if(_instanceParams.showClockCallback != nullptr){
    _instanceParams.showClockCallback();
  }

  _lifetimeParams.currentState = BehaviorState::ShowClock;
  TransitionToShowClockInternal();
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorProceduralClock::TransitionToShowClockInternal()
{
  int numUpdates = std::round(_instanceParams.totalTimeDisplayClock_sec);
  for(int i = 0; i < numUpdates; i++){
    AddKeyFramesForOffset(i, i*1000);
  }
  DisplayClock();
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorProceduralClock::TransitionToGetOut()
{
  _lifetimeParams.currentState = BehaviorState::GetOut;
  DelegateNow(new TriggerAnimationAction(_instanceParams.getOutAnim), 
                     [this](){ CancelSelf(); });
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorProceduralClock::BehaviorUpdate()
{
  if(!IsActivated()){
    return;
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorProceduralClock::AddKeyFramesForOffset(const int clockOffset_s, const int displayTime_ms)
{
  // Truncate to the nearest valid frame time for the triggerTime
  const int triggerTime_ms = displayTime_ms - (displayTime_ms % ANIM_TIME_STEP_MS);

  // set digits
  bool isLeadingZero = ShouldDimLeadingZeros();

  const std::map<Vision::SpriteBoxName, int> digitMap = _instanceParams.getDigitFunction(clockOffset_s);

  for(auto& pair : digitMap){
    Vision::SpriteBoxKeyFrame newKeyFrame = kKeyFrameMap.at(pair.first);
    newKeyFrame.triggerTime_ms = triggerTime_ms;

    isLeadingZero &= (pair.second == 0);
    if(!isLeadingZero){
      newKeyFrame.spriteBox.assetID = kDigitMap.at(pair.second);
    }

    _lifetimeParams.keyFrames.push_back(newKeyFrame);
  }

  if(_instanceParams.shouldPlayAudioOnClockUpdates){
    _lifetimeParams.audioTickTimes.push_back(triggerTime_ms);
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorProceduralClock::DisplayClock()
{
  std::vector<Vision::SpriteBoxKeyFrame> residualKeyFrames;
  RobotInterface::PlayAnimWithSpriteBoxKeyFrames dummyPlayMsg;
  const size_t maxFramesForPlayAnimMsg = dummyPlayMsg.spriteBoxKeyFrames.size();

  if(_lifetimeParams.keyFrames.size() > maxFramesForPlayAnimMsg){
    auto firstKeyFrameIter = _lifetimeParams.keyFrames.begin() + maxFramesForPlayAnimMsg;
    auto lastKeyFrameIter = _lifetimeParams.keyFrames.end();
    std::move(firstKeyFrameIter, lastKeyFrameIter, std::back_inserter(residualKeyFrames));
    _lifetimeParams.keyFrames.erase(firstKeyFrameIter, lastKeyFrameIter);
  }

  auto animationCallback = [this](const AnimationComponent::AnimResult res, u32 streamTimeAnimEnded){
    TransitionToGetOut();
  };
  GetBEI().GetAnimationComponent().PlayAnimWithSpriteBoxKeyFrames("", 
                                                                  _lifetimeParams.keyFrames,
                                                                  true,
                                                                  animationCallback);

  if(!residualKeyFrames.empty()){
    RobotInterface::AddSpriteBoxKeyFrames dummyAddMsg;
    const size_t maxFramesForAddKeyFramesMsg = dummyAddMsg.spriteBoxKeyFrames.size(); 
    while(!residualKeyFrames.empty()){
      const size_t numToSend = std::min(residualKeyFrames.size(), maxFramesForAddKeyFramesMsg);
      auto firstKeyFrameIter = residualKeyFrames.begin();
      auto lastKeyFrameIter = firstKeyFrameIter + numToSend;
      std::vector<Vision::SpriteBoxKeyFrame> keyFramesToSend;
      std::move(firstKeyFrameIter, lastKeyFrameIter, std::back_inserter(keyFramesToSend));
      residualKeyFrames.erase(firstKeyFrameIter, lastKeyFrameIter);
      GetBEI().GetAnimationComponent().AddSpriteBoxKeyFramesToRunningAnim(keyFramesToSend);
    }
  }

  if(_instanceParams.shouldPlayAudioOnClockUpdates){
    // Have the animation process send ticks at the appropriate time stamp 
    for(const auto& triggerTime_ms : _lifetimeParams.audioTickTimes){
      AudioEngine::Multiplexer::PostAudioEvent audioMessage;
      audioMessage.gameObject = Anki::AudioMetaData::GameObjectType::Animation;
      audioMessage.audioEvent = AudioMetaData::GameEvent::GenericEvent::Play__Robot_Vic_Sfx__Timer_Countdown;

      RobotInterface::EngineToRobot wrapper(std::move(audioMessage));
      GetBEI().GetAnimationComponent().AlterStreamingAnimationAtTime(std::move(wrapper), triggerTime_ms);
    }
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorProceduralClock::SetGetDigitFunction(GetDigitsFunction&& function)
{
  _instanceParams.getDigitFunction = function;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
SmartFaceID BehaviorProceduralClock::UpdateTargetFace()
{
  auto smartFaces = GetBEI().GetFaceWorld().GetSmartFaceIDs(0);

  const auto& faceSelection = GetAIComp<FaceSelectionComponent>();
  FaceSelectionComponent::FaceSelectionFactorMap criteriaMap;
  criteriaMap.insert(std::make_pair(FaceSelectionPenaltyMultiplier::RelativeHeadAngleRadians, 1));
  criteriaMap.insert(std::make_pair(FaceSelectionPenaltyMultiplier::RelativeBodyAngleRadians, 3));
  _lifetimeParams.targetFaceID = faceSelection.GetBestFaceToUse(criteriaMap, smartFaces);

  return _lifetimeParams.targetFaceID;
}


} // namespace Vector
} // namespace Anki

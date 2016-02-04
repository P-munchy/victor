/**
 * File: moodManager
 *
 * Author: Mark Wesley
 * Created: 10/14/15
 *
 * Description: Manages the Mood (a selection of emotions) for a Cozmo Robot
 *
 * Copyright: Anki, Inc. 2015
 *
 **/


#include "anki/cozmo/basestation/moodSystem/moodManager.h"
#include "anki/cozmo/basestation/moodSystem/emotionEvent.h"
#include "anki/cozmo/basestation/moodSystem/staticMoodData.h"
#include "anki/cozmo/basestation/events/ankiEvent.h"
#include "anki/cozmo/basestation/externalInterface/externalInterface.h"
#include "anki/cozmo/basestation/robot.h"
#include "anki/cozmo/basestation/viz/vizManager.h"
#include "anki/common/basestation/utils/timer.h"
#include "clad/externalInterface/messageEngineToGame.h"
#include "clad/externalInterface/messageGameToEngine.h"
#include "clad/vizInterface/messageViz.h"
#include "util/graphEvaluator/graphEvaluator2d.h"
#include "util/logging/logging.h"
#include "util/math/math.h"
#include <assert.h>


namespace Anki {
namespace Cozmo {


// For now StaticMoodData is basically a singleton, but hidden behind an interface in mood manager incase we ever
// need it to be different per robot / moodManager
static StaticMoodData sStaticMoodData;


StaticMoodData& MoodManager::GetStaticMoodData()
{
  return sStaticMoodData;
}
  

double MoodManager::GetCurrentTimeInSeconds()
{
  const double currentTimeInSeconds = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
  return currentTimeInSeconds;
}
  
  
MoodManager::MoodManager(Robot* inRobot)
  : _robot(inRobot)
  , _lastUpdateTime(0.0)
{
}
  
  
void MoodManager::Init(const Json::Value& inJson)
{
  GetStaticMoodData().Init(inJson);
}

  
void MoodManager::Reset()
{
  for (size_t i = 0; i < (size_t)EmotionType::Count; ++i)
  {
    GetEmotionByIndex(i).Reset();
  }
  _lastUpdateTime = 0.0;
}


void MoodManager::Update(double currentTime)
{
  const float kMinTimeStep = 0.0001f; // minimal sensible timestep, should be at least > epsilon
  float timeDelta = (_lastUpdateTime != 0.0) ? float(currentTime - _lastUpdateTime) : kMinTimeStep;
  if (timeDelta < kMinTimeStep)
  {
    PRINT_NAMED_WARNING("MoodManager.BadTimeStep", "TimeStep %f (%f-%f) is < %f - clamping!", timeDelta, currentTime, _lastUpdateTime, kMinTimeStep);
    timeDelta = kMinTimeStep;
  }
  
  _lastUpdateTime = currentTime;

  SEND_MOOD_TO_VIZ_DEBUG_ONLY( VizInterface::RobotMood robotMood );
  SEND_MOOD_TO_VIZ_DEBUG_ONLY( robotMood.emotion.reserve((size_t)EmotionType::Count) );

  for (size_t i = 0; i < (size_t)EmotionType::Count; ++i)
  {
    const EmotionType emotionType = (EmotionType)i;
    Emotion& emotion = GetEmotionByIndex(i);
    
    emotion.Update(GetStaticMoodData().GetDecayGraph(emotionType), currentTime, timeDelta);

    SEND_MOOD_TO_VIZ_DEBUG_ONLY( robotMood.emotion.push_back(emotion.GetValue()) );
  }
  
  SendEmotionsToGame();

  #if SEND_MOOD_TO_VIZ_DEBUG
  robotMood.recentEvents = std::move(_eventNames);
  _eventNames.clear();
  _robot->GetContext()->GetVizManager()->SendRobotMood(std::move(robotMood));
  #endif //SEND_MOOD_TO_VIZ_DEBUG
}
  
  
void MoodManager::HandleEvent(const AnkiEvent<ExternalInterface::MessageGameToEngine>& event)
{
  const auto& eventData = event.GetData();
  
  switch (eventData.GetTag())
  {
    case ExternalInterface::MessageGameToEngineTag::MoodMessage:
      {
        const Anki::Cozmo::ExternalInterface::MoodMessageUnion& moodMessage = eventData.Get_MoodMessage().MoodMessageUnion;

        switch (moodMessage.GetTag())
        {
          case ExternalInterface::MoodMessageUnionTag::GetEmotions:
            SendEmotionsToGame();
            break;
          case ExternalInterface::MoodMessageUnionTag::SetEmotion:
          {
            const Anki::Cozmo::ExternalInterface::SetEmotion& msg = moodMessage.Get_SetEmotion();
            SetEmotion(msg.emotionType, msg.newVal);
            break;
          }
          case ExternalInterface::MoodMessageUnionTag::AddToEmotion:
          {
            const Anki::Cozmo::ExternalInterface::AddToEmotion& msg = moodMessage.Get_AddToEmotion();
            AddToEmotion(msg.emotionType, msg.deltaVal, msg.uniqueIdString.c_str(), GetCurrentTimeInSeconds());
            break;
          }
          case ExternalInterface::MoodMessageUnionTag::TriggerEmotionEvent:
          {
            const Anki::Cozmo::ExternalInterface::TriggerEmotionEvent& msg = moodMessage.Get_TriggerEmotionEvent();
            TriggerEmotionEvent(msg.emotionEventName, GetCurrentTimeInSeconds());
            break;
          }
          default:
            PRINT_NAMED_ERROR("MoodManager.HandleEvent.UnhandledMessageUnionTag", "Unexpected tag %u", (uint32_t)moodMessage.GetTag());
            assert(0);
        }
      }
      break;
    default:
      PRINT_NAMED_ERROR("MoodManager.HandleEvent.UnhandledMessageGameToEngineTag", "Unexpected tag %u", (uint32_t)eventData.GetTag());
      assert(0);
  }
}
  
  
void MoodManager::SendEmotionsToGame()
{
  if (_robot)
  {
    std::vector<float> emotionValues;
    emotionValues.reserve((size_t)EmotionType::Count);
    
    for (size_t i = 0; i < (size_t)EmotionType::Count; ++i)
    {
      const Emotion& emotion = GetEmotionByIndex(i);
      emotionValues.push_back(emotion.GetValue());
    }
    
    ExternalInterface::MoodState message(_robot->GetID(), std::move(emotionValues));
    _robot->Broadcast(ExternalInterface::MessageEngineToGame(std::move(message)));
  }
}

  
// updates the most recent time this event was triggered, and returns how long it's been since the event was last seen
// returns FLT_MAX if this is the first time the event has been seen
float MoodManager::UpdateLatestEventTimeAndGetTimeElapsedInSeconds(const std::string& eventName, double currentTimeInSeconds)
{
  auto newEntry = _moodEventTimes.insert( MoodEventTimes::value_type(eventName, currentTimeInSeconds) );
  
  if (newEntry.second)
  {
    // first time event has occured, map insert has successfully updated the time seen
    return FLT_MAX;
  }
  else
  {
    // event has happened before - calculate time since it last occured and the matching penalty, then update the time
    
    double& timeEventLastOccured = newEntry.first->second;
    const float timeSinceLastOccurence = Util::numeric_cast<float>(currentTimeInSeconds - timeEventLastOccured);
    
    timeEventLastOccured = currentTimeInSeconds;
    
    return timeSinceLastOccurence;
  }
}


float MoodManager::UpdateEventTimeAndCalculateRepetitionPenalty(const std::string& eventName, double currentTimeInSeconds)
{
  const float timeSinceLastOccurence = UpdateLatestEventTimeAndGetTimeElapsedInSeconds(eventName, currentTimeInSeconds);
  
  const EmotionEvent* emotionEvent = GetStaticMoodData().GetEmotionEventMapper().FindEvent(eventName);
  
  if (emotionEvent)
  {
    // Use the emotionEvent with the matching name for calculating the repetion penalty
    const float repetitionPenalty = emotionEvent->CalculateRepetitionPenalty(timeSinceLastOccurence);
    return repetitionPenalty;
  }
  else
  {
    // No matching event name - use the default repetition penalty
    const float repetitionPenalty = GetStaticMoodData().GetDefaultRepetitionPenalty().EvaluateY(timeSinceLastOccurence);
    return repetitionPenalty;
  }
}


void MoodManager::TriggerEmotionEvent(const std::string& eventName, double currentTimeInSeconds)
{
  const EmotionEvent* emotionEvent = GetStaticMoodData().GetEmotionEventMapper().FindEvent(eventName);
  if (emotionEvent)
  {
    const float timeSinceLastOccurence = UpdateLatestEventTimeAndGetTimeElapsedInSeconds(eventName, currentTimeInSeconds);
    const float repetitionPenalty = emotionEvent->CalculateRepetitionPenalty(timeSinceLastOccurence);

    const std::vector<EmotionAffector>& emotionAffectors = emotionEvent->GetAffectors();
    for (const EmotionAffector& emotionAffector : emotionAffectors)
    {
      const float penalizedDeltaValue = emotionAffector.GetValue() * repetitionPenalty;
      GetEmotion(emotionAffector.GetType()).Add(penalizedDeltaValue);
    }
    
    SEND_MOOD_TO_VIZ_DEBUG_ONLY( AddEvent(eventName.c_str()) );
  }
  else
  {
    PRINT_NAMED_WARNING("MoodManager.TriggerEmotionEvent.EventNotFound", "Failed to find event '%s'", eventName.c_str());
  }
}


void MoodManager::AddToEmotion(EmotionType emotionType, float baseValue, const char* uniqueIdString, double currentTimeInSeconds)
{
  const float repetitionPenalty = UpdateEventTimeAndCalculateRepetitionPenalty(uniqueIdString, currentTimeInSeconds);
  const float penalizedDeltaValue = baseValue * repetitionPenalty;
  GetEmotion(emotionType).Add(penalizedDeltaValue);
  SEND_MOOD_TO_VIZ_DEBUG_ONLY( AddEvent(uniqueIdString) );
}


void MoodManager::AddToEmotions(EmotionType emotionType1, float baseValue1,
                                EmotionType emotionType2, float baseValue2, const char* uniqueIdString, double currentTimeInSeconds)
{
  const float repetitionPenalty = UpdateEventTimeAndCalculateRepetitionPenalty(uniqueIdString, currentTimeInSeconds);
  const float penalizedDeltaValue1 = baseValue1 * repetitionPenalty;
  const float penalizedDeltaValue2 = baseValue2 * repetitionPenalty;
  
  GetEmotion(emotionType1).Add(penalizedDeltaValue1);
  GetEmotion(emotionType2).Add(penalizedDeltaValue2);
  
  SEND_MOOD_TO_VIZ_DEBUG_ONLY( AddEvent(uniqueIdString) );
}

  
void MoodManager::AddToEmotions(EmotionType emotionType1, float baseValue1,
                                EmotionType emotionType2, float baseValue2,
                                EmotionType emotionType3, float baseValue3, const char* uniqueIdString, double currentTimeInSeconds)
{
  const float repetitionPenalty = UpdateEventTimeAndCalculateRepetitionPenalty(uniqueIdString, currentTimeInSeconds);
  const float penalizedDeltaValue1 = baseValue1 * repetitionPenalty;
  const float penalizedDeltaValue2 = baseValue2 * repetitionPenalty;
  const float penalizedDeltaValue3 = baseValue3 * repetitionPenalty;
  
  GetEmotion(emotionType1).Add(penalizedDeltaValue1);
  GetEmotion(emotionType2).Add(penalizedDeltaValue2);
  GetEmotion(emotionType3).Add(penalizedDeltaValue3);
    
  SEND_MOOD_TO_VIZ_DEBUG_ONLY( AddEvent(uniqueIdString) );
}


void MoodManager::SetEmotion(EmotionType emotionType, float value)
{
  GetEmotion(emotionType).SetValue(value);
  SEND_MOOD_TO_VIZ_DEBUG_ONLY( AddEvent("SetEmotion") );
}

  
#if SEND_MOOD_TO_VIZ_DEBUG
void MoodManager::AddEvent(const char* eventName)
{
  if (_eventNames.empty() || (_eventNames.back() != eventName))
  {
    _eventNames.push_back(eventName);
  }
}
#endif // SEND_MOOD_TO_VIZ_DEBUG

  
} // namespace Cozmo
} // namespace Anki


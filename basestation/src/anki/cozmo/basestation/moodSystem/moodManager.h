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


#ifndef __Cozmo_Basestation_MoodSystem_MoodManager_H__
#define __Cozmo_Basestation_MoodSystem_MoodManager_H__


#include "anki/cozmo/basestation/moodSystem/emotion.h"
#include "anki/cozmo/basestation/moodSystem/moodDebug.h"
#include "clad/types/emotionTypes.h"
#include "clad/types/simpleMoodTypes.h"
#include "util/graphEvaluator/graphEvaluator2d.h"
#include "util/signals/simpleSignal.hpp"
#include <assert.h>
#include <map>
#include <string>
#include <vector>


namespace Anki {
namespace Cozmo {


constexpr float kEmotionChangeVerySmall = 0.06f;
constexpr float kEmotionChangeSmall     = 0.12f;
constexpr float kEmotionChangeMedium    = 0.25f;
constexpr float kEmotionChangeLarge     = 0.50f;
constexpr float kEmotionChangeVeryLarge = 1.00f;

  
template <typename Type>
class AnkiEvent;


namespace ExternalInterface {
  class MessageGameToEngine;
}
  
  
class Robot;
class StaticMoodData;

  
class MoodManager
{
public:
  
  using MoodEventTimes = std::map<std::string, double>;
  
  explicit MoodManager(Robot* inRobot = nullptr);
  
  void Init(const Json::Value& inJson);
  
  bool LoadEmotionEvents(const Json::Value& inJson);
  
  void Reset();
  
  void Update(double currentTime);
  
  // ==================== Modify Emotions ====================
  
  void TriggerEmotionEvent(const std::string& eventName, double currentTimeInSeconds);
  
  void AddToEmotion(EmotionType emotionType, float baseValue, const char* uniqueIdString, double currentTimeInSeconds);
  
  void AddToEmotions(EmotionType emotionType1, float baseValue1,
                     EmotionType emotionType2, float baseValue2,
                     const char* uniqueIdString, double currentTimeInSeconds);
  
  void AddToEmotions(EmotionType emotionType1, float baseValue1,
                     EmotionType emotionType2, float baseValue2,
                     EmotionType emotionType3, float baseValue3,
                     const char* uniqueIdString, double currentTimeInSeconds);
  
  void SetEmotion(EmotionType emotionType, float value); // directly set the value e.g. for debugging
  
  // ==================== GetEmotion... ====================

  float GetEmotionValue(EmotionType emotionType) const { return GetEmotion(emotionType).GetValue(); }

  float GetEmotionDeltaRecentTicks(EmotionType emotionType, uint32_t numTicksBackwards) const
  {
    return GetEmotion(emotionType).GetDeltaRecentTicks(numTicksBackwards);
  }
  
  float GetEmotionDeltaRecentSeconds(EmotionType emotionType, float secondsBackwards)   const
  {
    return GetEmotion(emotionType).GetDeltaRecentSeconds(secondsBackwards);
  }
  
  SimpleMoodType GetSimpleMood() const;
  
  double GetLastUpdateTime() const { return _lastUpdateTime; }
  
  // ==================== Event/Message Handling ====================
  // Handle various message types
  template<typename T>
  void HandleMessage(const T& msg);
  
  void SendEmotionsToGame();

  // ============================== Public Static Member Funcs ==============================
  
  static StaticMoodData& GetStaticMoodData();
  
  // Helper for anything calling functions that require currentTimeInSeconds, where they don't already have access to it
  static double GetCurrentTimeInSeconds();
  
private:
  
  // ============================== Private Member Funcs ==============================
  
  float UpdateLatestEventTimeAndGetTimeElapsedInSeconds(const std::string& eventName, double currentTimeInSeconds);
  
  float UpdateEventTimeAndCalculateRepetitionPenalty(const std::string& eventName, double currentTimeInSeconds);
  
  void  FadeEmotionsToDefault(float delta);

  const Emotion&  GetEmotion(EmotionType emotionType) const { return GetEmotionByIndex((size_t)emotionType); }
  Emotion&        GetEmotion(EmotionType emotionType)       { return GetEmotionByIndex((size_t)emotionType); }
  
  const Emotion&  GetEmotionByIndex(size_t index) const
  {
    assert((index >= 0) && (index < (size_t)EmotionType::Count));
    return _emotions[index];
  }
  
  Emotion&  GetEmotionByIndex(size_t index)
  {
    assert((index >= 0) && (index < (size_t)EmotionType::Count));
    return _emotions[index];
  }
  
  SEND_MOOD_TO_VIZ_DEBUG_ONLY( void AddEvent(const char* eventName) );
    
  // ============================== Private Member Vars ==============================
  
  Emotion         _emotions[(size_t)EmotionType::Count];
  MoodEventTimes  _moodEventTimes;
  SEND_MOOD_TO_VIZ_DEBUG_ONLY( std::vector<std::string> _eventNames; )
  Robot*          _robot;
  double          _lastUpdateTime;

  std::vector<Signal::SmartHandle> _signalHandles;
};
  

} // namespace Cozmo
} // namespace Anki


#endif // __Cozmo_Basestation_MoodSystem_MoodManager_H__


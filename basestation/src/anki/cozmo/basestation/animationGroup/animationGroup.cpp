/**
 * File: animationGroup.cpp
 *
 * Authors: Trevor Dasch
 * Created: 2016-01-11
 *
 * Description:
 *    Class for storing a group of animations,
 *    from which an animation can be selected
 *    for a given set of moods
 *
 * Copyright: Anki, Inc. 2016
 *
 **/

#include "anki/cozmo/basestation/animationGroup/animationGroup.h"
#include "anki/cozmo/basestation/animationGroup/animationGroupContainer.h"
#include "anki/cozmo/basestation/moodSystem/moodManager.h"
#include "util/logging/logging.h"
#include "util/random/randomGenerator.h"
#include "anki/common/basestation/jsonTools.h"

#define DEBUG_ANIMATION_GROUP_SELECTION 0

namespace Anki {
namespace Cozmo {
    
static const char* kAnimationsKeyName = "Animations";
    
AnimationGroup::AnimationGroup(const std::string& name)
  : _name(name)
{
      
}
    
Result AnimationGroup::DefineFromJson(const std::string& name, const Json::Value &jsonRoot)
{
  _name = name;
      
  const Json::Value& jsonAnimations = jsonRoot[kAnimationsKeyName];

  if(!jsonAnimations.isArray()) {
        
    PRINT_NAMED_ERROR("AnimationGroup.DefineFromJson.NoAnimations",
                      "Missing '%s' field for animation group.", kAnimationsKeyName);
    return RESULT_FAIL;
  }

  _animations.clear();
      
  const s32 numEntries = jsonAnimations.size();
      
  _animations.reserve(numEntries);
      
  for(s32 iEntry = 0; iEntry < numEntries; ++iEntry)
  {
    const Json::Value& jsonEntry = jsonAnimations[iEntry];
        
    _animations.emplace_back();
        
    auto addResult = _animations.back().DefineFromJson(jsonEntry);
        
    if(addResult != RESULT_OK) {
      PRINT_NAMED_ERROR("AnimationGroup.DefineFromJson.AddEntryFailure",
                        "Adding animation %d failed.",
                        iEntry);
      return addResult;
    }
        
        
  } // for each Entry
      
  return RESULT_OK;
}
    
bool AnimationGroup::IsEmpty() const
{
  return _animations.empty();
}
    
const std::string& AnimationGroup::GetAnimationName(const MoodManager& moodManager,
                                                    AnimationGroupContainer& animationGroupContainer, float headAngleRad) const
{
  return GetAnimationName(moodManager.GetSimpleMood(), moodManager.GetLastUpdateTime(), animationGroupContainer,headAngleRad);
}
    
const std::string& AnimationGroup::GetAnimationName(SimpleMoodType mood,
                                                    float currentTime_s,
                                                    AnimationGroupContainer& animationGroupContainer,
                                                    float headAngleRad) const
{
  PRINT_NAMED_DEBUG("AnimationGroup.GetAnimation", "getting animation from group '%s', simple mood = '%s'",
                    _name.c_str(),
                    SimpleMoodTypeToString(mood));
      
  Util::RandomGenerator rng; // [MarkW:TODO] We should share these (1 per robot or subsystem maybe?) for replay determinism
      
  float totalWeight = 0.0f;
  bool anyAnimationsMatchingMood = false;
      
  std::vector<const AnimationGroupEntry*> availableAnimations;
      
  for (auto entry = _animations.begin(); entry != _animations.end(); entry++)
  {
    if(entry->GetMood() == mood)
    {
      anyAnimationsMatchingMood = true;
      bool validHeadAngle = true;
      if( entry->GetUseHeadAngle())
      {
        if( !(headAngleRad > entry->GetHeadAngleMin() && headAngleRad < entry->GetHeadAngleMax()))
        {
          validHeadAngle = false;
        }
      }
      if( validHeadAngle )
      {
        if( !animationGroupContainer.IsAnimationOnCooldown(entry->GetName(),currentTime_s))
        {
          totalWeight += entry->GetWeight();
          availableAnimations.emplace_back(&(*entry));

          if( DEBUG_ANIMATION_GROUP_SELECTION )
          {
            PRINT_NAMED_INFO("AnimationGroup.GetAnimation.ConsiderAnimation",
                             "%s: considering animation '%s' with weight %f",
                             _name.c_str(),
                             entry->GetName().c_str(),
                             entry->GetWeight());
          }
        }
        else if( DEBUG_ANIMATION_GROUP_SELECTION )
        {
          PRINT_NAMED_INFO("AnimationGroup.GetAnimation.RejectAnimation.Cooldown",
                           "%s: rejecting animation %s with mood %s is on cooldown (timer=%f)",
                           _name.c_str(),
                           entry->GetName().c_str(),
                           SimpleMoodTypeToString(entry->GetMood()),
                           entry->GetCooldown());
        }
      }
      else if( DEBUG_ANIMATION_GROUP_SELECTION ) {
        PRINT_NAMED_INFO("AnimationGroup.GetAnimation.RejectAnimation.HeadAngle",
                         "%s: rejecting animation %s with head angle (%f) out of range (%f,%f)",
                         _name.c_str(),
                         entry->GetName().c_str(),
                         RAD_TO_DEG(headAngleRad),
                         entry->GetHeadAngleMin(),
                         entry->GetHeadAngleMax());
      }
    }
    else if( DEBUG_ANIMATION_GROUP_SELECTION )
    {
      PRINT_NAMED_INFO("AnimationGroup.GetAnimation.RejectAnimation.WrongMood",
                       "%s: rejecting animation %s with mood %s %son cooldown",
                       _name.c_str(),
                       entry->GetName().c_str(),
                       SimpleMoodTypeToString(entry->GetMood()),
                       animationGroupContainer.IsAnimationOnCooldown(entry->GetName(),currentTime_s) ?
                       "" :
                       "not ");
    }
  }
      
  float weightedSelection = rng.RandDbl(totalWeight);
      
  const AnimationGroupEntry* lastEntry = nullptr;
      
  for (auto entry : availableAnimations)
  {
    lastEntry = &(*entry);
    weightedSelection -= entry->GetWeight();

    if(weightedSelection < 0.0f) {
      break;
    }
  }
      
  // Possible that if weightedSelection == totalWeight, we wouldn't
  // select any, so return the last one if its not the end
  if(lastEntry != nullptr) {
    animationGroupContainer.SetAnimationCooldown(lastEntry->GetName(), currentTime_s + lastEntry->GetCooldown());
    return lastEntry->GetName();
  }

  // we couldn't find an animation. If we were in a non-default mood, try again with the default mood
  if( mood != SimpleMoodType::Default ) {
    PRINT_NAMED_INFO("AnimationGroup.GetAnimation.NoMoodMatch",
                     "No animations from group '%s' selected matching mood '%s', trying with default mood",
                     _name.c_str(),
                     SimpleMoodTypeToString(mood));
    
    return GetAnimationName(SimpleMoodType::Default, currentTime_s, animationGroupContainer,headAngleRad);
  }

  static const std::string empty = "";
  // Since this is the backup emergency case, also ignore head angle and just play something
  if( anyAnimationsMatchingMood ) {
    // choose the animation closest to being off cooldown
    const AnimationGroupEntry* bestEntry = nullptr;
    float minCooldown = std::numeric_limits<float>::max();

    // TODO:(bn) this should be a warning, but we are doing this all over the place in the vertical slice, so
    // keeping info for now
    PRINT_NAMED_INFO("AnimationGroup.GetAnimation.AllOnCooldown",
                     "All animations are on cooldown. Selecting the one closest to being finished");

    for (auto entry = _animations.begin(); entry != _animations.end(); entry++)
    {
      if(entry->GetMood() == mood) {
        float timeLeft = animationGroupContainer.TimeUntilCooldownOver(entry->GetName(), currentTime_s);

        if( DEBUG_ANIMATION_GROUP_SELECTION ) {
          PRINT_NAMED_INFO("AnimationGroup.GetAnimation.ConsiderIgnoringCooldown",
                           "%s: animation %s has %f left on it's cooldown",
                           _name.c_str(),
                           entry->GetName().c_str(),
                           timeLeft);
        }

        if(timeLeft < minCooldown) {
          minCooldown = timeLeft;
          bestEntry = &(*entry);
        }
      }
    }

    if( bestEntry != nullptr ) {
      return bestEntry->GetName();
    }
  }

  PRINT_NAMED_ERROR("AnimationGroup.GetAnimation.NoAnimation",
                    "Could not find a single animation from group '%s' to run. Returning empty",
                    _name.c_str());
  return empty;
}
    
} // namespace Cozmo
} // namespace Anki

/**
 * File: animationGroupEntry.cpp
 *
 * Authors: Trevor Dasch
 * Created: 2016-01-11
 *
 * Description:
 *    Class for storing an animation selection
 *    Which defines a set of mood score graphs
 *    by which to evaluate the suitability of this animation
 *
 * Copyright: Anki, Inc. 2016
 *
 **/

#include "anki/cozmo/basestation/animationGroup/animationGroupEntry.h"
#include "anki/cozmo/basestation/cannedAnimationContainer.h"

#include "util/logging/logging.h"
#include "anki/common/basestation/jsonTools.h"
#include "anki/cozmo/basestation/moodSystem/simpleMoodTypesHelpers.h"

namespace Anki {
  namespace Cozmo {
    
    static const char* kNameKey = "Name";
    static const char* kWeightKey = "Weight";
    static const char* kMoodKey = "Mood";
    static const char* kCooldownKey = "CooldownTime_Sec";
    static const char* kUseHeadAngleKey = "UseHeadAngle";
    static const char* kHeadAngleMinKey = "HeadAngleMin_Deg";
    static const char* kHeadAngleMaxKey = "HeadAngleMax_Deg";
    
    AnimationGroupEntry::AnimationGroupEntry()
    {
    }
    
    Result AnimationGroupEntry::DefineFromJson(const Json::Value &jsonRoot, const CannedAnimationContainer* cannedAnimations)
    {
      const Json::Value& jsonName = jsonRoot[kNameKey];
      
      if(!jsonName.isString()) {
        PRINT_NAMED_ERROR("AnimationGroupEntry.DefineFromJson.NoName",
                          "Missing '%s' field for animation.", kNameKey);
        return RESULT_FAIL;
      }
      
      _name = jsonName.asString();
      if(nullptr != cannedAnimations && nullptr == cannedAnimations->GetAnimation(_name)) {
        PRINT_NAMED_ERROR("AnimationGroupEntry.DefineFromJson.InvalidName",
                          "No canned animation exists named '%s'",
                          _name.c_str());
        return RESULT_FAIL;
      }
      
      const Json::Value& jsonWeight = jsonRoot[kWeightKey];
      
      if(!jsonWeight.isDouble()) {
        PRINT_NAMED_ERROR("AnimationGroupEntry.DefineFromJson.NoWeight",
                          "Missing '%s' field for animation.", kWeightKey);
        
        return RESULT_FAIL;
      }
      
      _weight = jsonWeight.asFloat();
      
      const Json::Value& jsonMood = jsonRoot[kMoodKey];
      
      if(!jsonMood.isString()) {
        PRINT_NAMED_ERROR("AnimationGroupEntry.DefineFromJson.NoMood",
                          "Missing '%s' field for animation.", kMoodKey);
        
        return RESULT_FAIL;
      }
      
      const char* moodTypeString = jsonMood.asCString();
      _mood  = SimpleMoodTypeFromString( moodTypeString );
      
      if (_mood == SimpleMoodType::Count)
      {
        PRINT_NAMED_WARNING("SimpleMoodScorer.ReadFromJson.BadType", "Bad '%s' = '%s'", kMoodKey, moodTypeString);
        return RESULT_FAIL;
      }
      
      const Json::Value& jsonCooldown = jsonRoot[kCooldownKey];
      
      // Cooldown is optional
      if(jsonWeight.isDouble()) {
        _cooldownTime_s = jsonCooldown.asDouble();
      }
      else {
        _cooldownTime_s = 0;
      }
      // Use Head Angle is optional
      _useHeadAngle = false;
      if(JsonTools::GetValueOptional(jsonRoot, kUseHeadAngleKey, _useHeadAngle) && _useHeadAngle)
      {
        const Json::Value& minHeadAngle = jsonRoot[kHeadAngleMinKey];
        const Json::Value& maxHeadAngle = jsonRoot[kHeadAngleMaxKey];
        if(!minHeadAngle.isDouble() || !maxHeadAngle.isDouble()) {
          PRINT_NAMED_ERROR("AnimationGroupEntry.DefineFromJson.NoHeadAngleWhenUsingHeadAngles",
                            "Missing '%s' or '%s' field for animation.", kHeadAngleMinKey,kHeadAngleMaxKey);
          
          return RESULT_FAIL;
        }
        _headAngleMin = DEG_TO_RAD_F32(minHeadAngle.asFloat());
        _headAngleMax = DEG_TO_RAD_F32(maxHeadAngle.asFloat());
      }
      
      return RESULT_OK;
    }
    
  } // namespace Cozmo
} // namespace Anki

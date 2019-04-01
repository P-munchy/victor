/**
 * File: userIntentMap.h
 *
 * Author: Brad Neuman
 * Created: 2018-01-30
 *
 * Description: Mapping from other intents (e.g. cloud) to user intents
 *
 * Copyright: Anki, Inc. 2018
 *
 **/

#ifndef __Engine_AiComponent_BehaviorComponent_UserIntentMap_H__
#define __Engine_AiComponent_BehaviorComponent_UserIntentMap_H__

#include "engine/aiComponent/behaviorComponent/userIntents.h"

#include "util/helpers/noncopyable.h"

#include "json/json-forwards.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace Anki {
namespace Vector {

class CozmoContext;
class AnimationComponent;
class MoodManager;

// struct defined in cpp to hide UserIntent from needing a header dependency
struct SimpleVoiceResponseMap;

class UserIntentMap : private Util::noncopyable
{
public:

  explicit UserIntentMap(const Json::Value& config, const CozmoContext* ctx);

  ~UserIntentMap();

  // returns a user intent that matches cloud intent. If none is found, it will return an intent signaling
  // that there was no match
  UserIntentTag GetUserIntentFromCloudIntent(const std::string& cloudIntent) const;

  // returns true if the specified intent exists in the map
  bool IsValidCloudIntent(const std::string& cloudIntent) const;

  // looks up the given cloud intent and returns a bool that indicates if the engine's user
  // intent parsing test should look for that cloud intent in the Dialogflow sample file
  bool GetTestParsingBoolFromCloudIntent(const std::string& cloudIntent) const;
  
  UserIntentTag GetUserIntentFromAppIntent(const std::string& appIntent) const;
  
  bool IsValidAppIntent(const std::string& appIntent) const;
  
  // modify paramsList, replacing the cloud variable names with the user intent variable names, and
  // turning quoted numeric types like "123" into actual json numeric types (no quotes), based on
  // the config passed in the constructor
  void SanitizeCloudIntentVariables(const std::string& cloudIntent, Json::Value& paramsList) const;
  void SanitizeAppIntentVariables(const std::string& appIntent, Json::Value& paramsList) const;

  // if a given cloud intent is a "simple voice response" (i.e. GetUserIntentFromCloudIntent returns
  // simple_voice_response), then the user intent map will actually contain a full UserIntent, not just the
  // tag. This function can be used to return it
  const UserIntent& GetSimpleVoiceResponse(const std::string& cloudIntent) const;

  // get list of cloud/app intents from json
  std::vector<std::string> DevGetCloudIntentsList() const;
  std::vector<std::string> DevGetAppIntentsList() const;

  // iterate through the simple response map and validate that the parameters within are valid (e.g. that
  // animation groups and emotion event names are valid). This should be called after load time and will
  // assert / print errors internally and return false if there is an issue. NOTE: if anim groups are invalid,
  // the map entries will be removed so that they go to "unmatched intent" instead, but if emotion events are
  // invalid, only an error will be printed
  bool VerifySimpleVoiceResponses(const AnimationComponent& animComponent, const MoodManager& moodManager);

private:

  struct SanitationActions {
    std::string from;
    std::string to; // if non-empty, the "from" variable name is replaced with "to"
    bool isNumeric=false; // if true, we replace variable values "90" with 90
  };

  using VarSubstitutionList = std::vector< SanitationActions >;
  
  struct IntentInfo {
    UserIntentTag userIntent;
    VarSubstitutionList varSubstitutions;
    bool testParsing;
  };
  
  using MapType = std::map<std::string, IntentInfo>;
  
  MapType _cloudToUserMap;
  MapType _appToUserMap;

  std::unique_ptr<SimpleVoiceResponseMap> _simpleCloudResponseMap;

  UserIntentTag _unmatchedUserIntent;
  
  void SanitizeVariables(const std::string& intent,
                         const MapType& container,
                         const char* debugName,
                         Json::Value& paramsList) const;
};

}
}

#endif

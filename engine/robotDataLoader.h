/**
 * File: robotDataLoader
 *
 * Author: baustin
 * Created: 6/10/16
 *
 * Description: Loads and holds static data robots use for initialization
 *
 * Copyright: Anki, Inc. 2016
 *
 **/

#ifndef ANKI_COZMO_BASESTATION_ROBOT_DATA_LOADER_H
#define ANKI_COZMO_BASESTATION_ROBOT_DATA_LOADER_H

#include "clad/types/animationTrigger.h"
#include "engine/aiComponent/behaviorComponent/behaviorTypesWrapper.h"

#include "util/helpers/noncopyable.h"
#include <json/json.h>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <set>
#include <unordered_map>
#include <vector>

namespace Anki {

namespace Util {
namespace Data {
class DataPlatform;
}
}

namespace Cozmo {

class AnimationGroupContainer;
class BackpackLightAnimationContainer;
class CannedAnimationContainer;
class CubeLightAnimationContainer;
class CozmoContext;
class AnimationTriggerResponsesContainer;

class RobotDataLoader : private Util::noncopyable
{
public:
  RobotDataLoader(const CozmoContext* context);
  ~RobotDataLoader();

  // loads all data excluding configs, using DispatchWorker to parallelize.
  // Blocks until the data is loaded.
  void LoadNonConfigData();
  
  // Starts a thread to handle loading non-config data if it hasn't been done yet.
  // Can be repeatedly called to get an updated loading complete ratio. Returns
  // false if loading is ongoing, otherwise returns true
  bool DoNonConfigDataLoading(float& loadingCompleteRatio_out);

  // refresh individual data pieces after initial load
  void LoadRobotConfigs();
  void LoadVoiceCommandConfigs();

  using FileJsonMap       = std::unordered_map<std::string, const Json::Value>;
  using ImagePathMap      = std::unordered_map<std::string, std::string>;
  using BehaviorIDJsonMap = std::unordered_map<BehaviorID,  const Json::Value>;

  const FileJsonMap& GetEmotionEventJsons()   const { return _emotionEvents; }
  const BehaviorIDJsonMap& GetBehaviorJsons() const { return _behaviors; }
  
  CannedAnimationContainer* GetCannedAnimationContainer() const { return _cannedAnimations.get(); }
  CubeLightAnimationContainer* GetCubeLightAnimations() const { return _cubeLightAnimations.get(); }
  AnimationGroupContainer* GetAnimationGroups() const { return _animationGroups.get(); }
  AnimationTriggerResponsesContainer* GetAnimationTriggerResponses() const { return _animationTriggerResponses.get(); }
  AnimationTriggerResponsesContainer* GetCubeAnimationTriggerResponses() const { return _cubeAnimationTriggerResponses.get(); }
  BackpackLightAnimationContainer* GetBackpackLightAnimations() const { return _backpackLightAnimations.get(); }

  bool HasAnimationForTrigger( AnimationTrigger ev );
  std::string GetAnimationForTrigger( AnimationTrigger ev );
  std::string GetCubeAnimationForTrigger( CubeAnimationTrigger ev );
  
  const std::set<AnimationTrigger>& GetDasBlacklistedAnimationTriggers() const { return _dasBlacklistedAnimationTriggers; }

  // robot configuration json files
  const Json::Value& GetRobotMoodConfig() const              { return _robotMoodConfig; }
  const Json::Value& GetVictorFreeplayBehaviorConfig() const { return _victorFreeplayBehaviorConfig; }
  const Json::Value& GetRobotVisionConfig() const            { return _robotVisionConfig; }
  const Json::Value& GetVisionScheduleMediatorConfig() const { return _visionScheduleMediatorConfig; }
  const Json::Value& GetVoiceCommandConfig() const           { return _voiceCommandConfig; }
  const Json::Value& GetRobotNeedsConfig() const             { return _needsSystemConfig; }
  const Json::Value& GetStarRewardsConfig() const            { return _starRewardsConfig; }
  const Json::Value& GetRobotNeedsActionsConfig() const      { return _needsActionConfig; }
  const Json::Value& GetRobotNeedsDecayConfig() const        { return _needsDecayConfig; }
  const Json::Value& GetRobotNeedsHandlersConfig() const     { return _needsHandlersConfig; }
  const Json::Value& GetLocalNotificationConfig() const      { return _localNotificationConfig; }
  const Json::Value& GetInventoryConfig() const              { return _inventoryConfig; }
  const Json::Value& GetWebServerEngineConfig() const        { return _webServerEngineConfig; }
  const Json::Value& GetDasEventConfig() const               { return _dasEventConfig; }

  // images are stored as a map of stripped file name (no file extension) to full path
  const ImagePathMap& GetFacePNGPaths()       const { return _facePNGPaths; }

  bool IsCustomAnimLoadEnabled() const;
  
private:
  void CollectAnimFiles();
  
  void LoadCubeLightAnimations();
  void LoadCubeLightAnimationFile(const std::string& path);
  
  void LoadBackpackLightAnimations();
  void LoadBackpackLightAnimationFile(const std::string& path);
  
  void LoadAnimationGroups();
  void LoadAnimationGroupFile(const std::string& path);
  
  void LoadAnimationTriggerResponses();
  void LoadCubeAnimationTriggerResponses();
  
  void AddToLoadingRatio(float delta);

  using TimestampMap = std::unordered_map<std::string, time_t>;
  void WalkAnimationDir(const std::string& animationDir, TimestampMap& timestamps,
                        const std::function<void(const std::string& filePath)>& walkFunc);

  void LoadEmotionEvents();
  void LoadBehaviors();

  void LoadDasBlacklistedAnimationTriggers();
  
  void LoadFacePNGPaths();


  const CozmoContext* const _context;
  const Util::Data::DataPlatform* _platform;

  FileJsonMap _emotionEvents;
  BehaviorIDJsonMap _behaviors;

  enum FileType {
      Animation,
      AnimationGroup,
      CubeLightAnimation,
      BackpackLightAnimation
  };
  std::unordered_map<int, std::vector<std::string>> _jsonFiles;

  // animation data
  std::unique_ptr<CannedAnimationContainer>           _cannedAnimations;
  std::unique_ptr<CubeLightAnimationContainer>        _cubeLightAnimations;
  std::unique_ptr<AnimationGroupContainer>            _animationGroups;
  std::unique_ptr<AnimationTriggerResponsesContainer> _animationTriggerResponses;
  std::unique_ptr<AnimationTriggerResponsesContainer> _cubeAnimationTriggerResponses;
  std::unique_ptr<BackpackLightAnimationContainer>    _backpackLightAnimations;
  TimestampMap _animFileTimestamps;
  TimestampMap _groupAnimFileTimestamps;
  TimestampMap _cubeLightAnimFileTimestamps;
  TimestampMap _backpackLightAnimFileTimestamps;

  std::string _test_anim;

  // robot configs
  Json::Value _robotMoodConfig;
  Json::Value _victorFreeplayBehaviorConfig;
  Json::Value _robotVisionConfig;
  Json::Value _visionScheduleMediatorConfig;
  Json::Value _voiceCommandConfig;
  Json::Value _needsSystemConfig;
  Json::Value _starRewardsConfig;
  Json::Value _needsActionConfig;
  Json::Value _needsDecayConfig;
  Json::Value _needsHandlersConfig;
  Json::Value _localNotificationConfig;
  Json::Value _textToSpeechConfig;
  Json::Value _inventoryConfig;
  Json::Value _webServerEngineConfig;
  Json::Value _dasEventConfig;
  
  ImagePathMap _facePNGPaths;

  bool                  _isNonConfigDataLoaded = false;
  std::mutex            _parallelLoadingMutex;
  std::atomic<float>    _loadingCompleteRatio{0};
  std::thread           _dataLoadingThread;
  std::atomic<bool>     _abortLoad{false};
  
  std::set<AnimationTrigger> _dasBlacklistedAnimationTriggers;
};

}
}

#endif

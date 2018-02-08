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

#ifndef ANKI_COZMO_ANIM_DATA_LOADER_H
#define ANKI_COZMO_ANIM_DATA_LOADER_H

#include "util/helpers/noncopyable.h"
#include "assert.h"
#include <json/json.h>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace Anki {

namespace Util {
namespace Data {
class DataPlatform;
}
}

namespace Cozmo {

class CannedAnimationContainer;
class Animation;
class AnimContext;

class RobotDataLoader : private Util::noncopyable
{
public:
  RobotDataLoader(const AnimContext* context);
  ~RobotDataLoader();

  // Loads all static configuration data.
  // Blocks until data is loaded.
  void LoadConfigData();
  
  // Loads all data excluding configs, using DispatchWorker to parallelize.
  // Blocks until the data is loaded.
  void LoadNonConfigData();
  void LoadAnimationFile(const std::string& path);
  
  // Starts a thread to handle loading non-config data if it hasn't been done yet.
  // Can be repeatedly called to get an updated loading complete ratio. Returns
  // false if loading is ongoing, otherwise returns true
  bool DoNonConfigDataLoading(float& loadingCompleteRatio_out);

  const Json::Value & GetTextToSpeechConfig() const { return _tts_config; }
  Animation* GetCannedAnimation(const std::string& name);
  
private:
  void NotifyAnimAdded(const std::string& animName, uint32_t animLength);

  const AnimContext* const _context;
  const Util::Data::DataPlatform* _platform;

  // animation data
  std::unique_ptr<CannedAnimationContainer>           _cannedAnimations;
  // loading properties shared with the animiation loader
  std::atomic<float>    _loadingCompleteRatio{0};
  std::atomic<bool>     _abortLoad{false};

  bool                  _isNonConfigDataLoaded = false;
  std::thread           _dataLoadingThread;

  
  Json::Value _tts_config;
  
};

}
}

#endif

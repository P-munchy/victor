/**
 * File: cannedAnimationLoader.h
 *
 * Authors: Kevin M. Karol
 * Created: 1/18/18
 *
 * Description:
 *    Class that loads animations from data on worker threads and
 *    returns the final animation container
 *
 * Copyright: Anki, Inc. 2015
 *
 **/


#ifndef ANKI_COZMO_CANNED_ANIMATION_LOADER_H
#define ANKI_COZMO_CANNED_ANIMATION_LOADER_H

#include "util/helpers/noncopyable.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Anki {

namespace Util {
namespace Data {
class DataPlatform;
}
}

namespace Cozmo {

// forward declaration
class CannedAnimationContainer;
class CozmoAnimContext;

class CannedAnimationLoader : private Util::noncopyable
{
public:
  CannedAnimationLoader(const Util::Data::DataPlatform* platform,
                        std::atomic<float>& loadingCompleteRatio,
                        std::atomic<bool>&  abortLoad)
  : _platform(platform)
  , _loadingCompleteRatio(loadingCompleteRatio)
  , _abortLoad(abortLoad){}

  CannedAnimationContainer* LoadAnimations(); 
private:
  using TimestampMap = std::unordered_map<std::string, time_t>;
  
  // params passed in by data loader class
  const Util::Data::DataPlatform* _platform;
  std::atomic<float>& _loadingCompleteRatio;
  std::atomic<bool>&  _abortLoad;


  // Animation paths/timestamps
  std::vector<std::string> _jsonFiles;
  TimestampMap _animFileTimestamps;
  
  
  // This gets set when we start loading animations and know the total number
  float _perAnimationLoadingRatio = 0.0f;
  std::mutex _parallelLoadingMutex;
  std::unique_ptr<CannedAnimationContainer> _cannedAnimations;

  void LoadAnimationsInternal();
  
  void AddToLoadingRatio(float delta);


  void WalkAnimationDir(const std::string& animationDir, TimestampMap& timestamps,
                        const std::function<void(const std::string& filePath)>& walkFunc);

  void LoadFaceAnimations();


  void CollectAnimFiles();

  void LoadAnimationFile(const std::string& path);

};

} // namespace Cozmo
} // namespace Anki

#endif // ANKI_COZMO_CANNED_ANIMATION_LOADER_H

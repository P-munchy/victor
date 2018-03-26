/**
 * File: templatedImageCache.h
 *
 * Author: Kevin M. Karol
 * Created: 2/2/18
 *
 * Description: Caches the images it builds based off of a template and then
 * only re-draws the quadrants that have changed for the next request.
 *
 * Copyright: Anki, Inc. 2018
 *
 **/

#ifndef __Cozmo_Basestation_AIComponent_CompositeImageCache_H__
#define __Cozmo_Basestation_AIComponent_CompositeImageCache_H__

#include "clad/types/compositeImageTypes.h"
#include "coretech/vision/engine/colorPixelTypes.h"
#include "coretech/vision/engine/compositeImage/compositeImage.h"
#include "engine/robotDataLoader.h"
#include "engine/aiComponent/aiComponents_fwd.h"
#include "util/entityComponent/iDependencyManagedComponent.h"
#include "util/helpers/noncopyable.h"

namespace Anki {
namespace Cozmo {

  
class CompositeImageCache : public IDependencyManagedComponent<AIComponentID>, 
                            private Util::noncopyable
{
public:
  CompositeImageCache(const RobotDataLoader::ImagePathMap& imageMap)
  : IDependencyManagedComponent<AIComponentID>(this, AIComponentID::CompositeImageCache)
  , _imagePathMap(imageMap){}
  // Pass in the template and a map of quadrant name to image name
  // If an image with the same name has been built and cached, only the quadrants
  // different from the cached image will be re-drawn 
  const Vision::ImageRGBA& BuildImage(const std::string& imageName,
                                      s32 imageWidth, s32 imageHeight,
                                      const Vision::CompositeImage& image);

  // Give behaviors a way to access the imagePathMap
  const RobotDataLoader::ImagePathMap& GetImagePathMap(){ return _imagePathMap;}
private:
  struct CacheEntry{
    CacheEntry(s32 imageWidth, s32 imageHeight)
    : preAllocatedImage(imageHeight, imageWidth){
      preAllocatedImage.FillWith(Vision::PixelRGBA());
    }
    
    Vision::CompositeImage compositeImage;
    Vision::ImageRGBA preAllocatedImage;
  };

  const RobotDataLoader::ImagePathMap& _imagePathMap;
  std::map<std::string, CacheEntry> _imageCache;

  void UpdateCacheEntry(CacheEntry& cacheEntry,
                        const Vision::CompositeImage& image);

}; // class CompositeImageCache

} // namespace Cozmo
} // namespace Anki

#endif // __Cozmo_Basestation_AIComponent_CompositeImageCache_H__

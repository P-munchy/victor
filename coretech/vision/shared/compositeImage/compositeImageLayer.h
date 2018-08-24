/**
* File: compositeImageLayer.h
*
* Author: Kevin M. Karol
* Created: 3/19/2018
*
* Description: Defines the sprite box layout for a composite layer and the
* image map for each sprite box
*
* Copyright: Anki, Inc. 2018
*
**/

#ifndef __Vision_CompositeImageLayer_fwd_H__
#define __Vision_CompositeImageLayer_fwd_H__

#include "clad/types/compositeImageTypes.h"
#include "clad/types/spriteNames.h"

#include "coretech/common/engine/math/point.h"
#include "coretech/vision/shared/compositeImage/compositeImageLayoutModifier.h"
#include "coretech/vision/shared/spriteCache/spriteCache.h"
#include "coretech/vision/shared/spriteSequence/spriteSequence.h"
#include <unordered_map>
#include "util/helpers/templateHelpers.h"

// forward declaration
namespace Json {
class Value;
}

namespace Anki {
namespace Vision {

namespace CompositeImageConfigKeys{
static const char* kLayerNameKey       = "layerName";
static const char* kImagesListKey      = "images";
static const char* kSpriteBoxLayoutKey = "spriteBoxLayout";
static const char* kSpriteBoxNameKey   = "spriteBoxName";
static const char* kRenderMethodKey    = "spriteRenderMethod";
static const char* kHueKey             = "renderHue";
static const char* kSaturationKey      = "renderSaturation";
static const char* kSpriteNameKey      = "spriteName";
static const char* kCornerXKey         = "x";
static const char* kCornerYKey         = "y";
static const char* kWidthKey           = "width";
static const char* kHeightKey          = "height";
}

class SpriteSequenceContainer;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
class CompositeImageLayer{
public:
  struct SpriteBox;
  struct SpriteEntry;
  using LayoutMap = std::map<SpriteBoxName, SpriteBox>;
  using ImageMap  = std::unordered_map<SpriteBoxName, SpriteEntry, Anki::Util::EnumHasher>;

  CompositeImageLayer(LayerName layerName = LayerName::StaticBackground)
  : _layerName(layerName){};
  CompositeImageLayer(const Json::Value& layoutSpec);
  CompositeImageLayer(LayerName layerName,
                      LayoutMap&& layoutSpec)
  : _layerName(layerName)
  , _layoutMap(std::move(layoutSpec)){}
  virtual ~CompositeImageLayer();
  
  bool operator ==(const CompositeImageLayer& other) const;


  LayerName        GetLayerName() const { return _layerName;}
  const LayoutMap& GetLayoutMap() const { return _layoutMap;}
  LayoutMap& GetLayoutMap()             { return _layoutMap;}
  const ImageMap&  GetImageMap()  const { return _imageMap;}
  ImageMap&  GetImageMap()              { return _imageMap;}

  // Returns true if the spritebox maps to a sprite sequence and the name of that sequence has been set
  bool GetSpriteSequenceName(SpriteBoxName sbName, Vision::SpriteName& sequenceName)  const;

  // Merges all sprite boxes/image maps from other image into this image
  void MergeInLayer(const CompositeImageLayer& otherLayer);

  // Functions which add on to the current layout
  void AddToLayout(SpriteBoxName sbName, const SpriteBox& spriteBox);
  void AddToImageMap(SpriteCache* cache, SpriteSequenceContainer* seqContainer,
                     SpriteBoxName sbName, const Vision::SpriteName& spriteName);
  void AddToImageMap(SpriteBoxName sbName, const SpriteEntry& spriteEntry);
  
  // Returns true if composite image has an image map entry that matches the sprite box
  // Returns false if image is not set
  bool GetSpriteEntry(const SpriteBox& sb, SpriteEntry& outEntry) const;
  
  bool GetFrame(SpriteBoxName sbName, const u32 index, 
                Vision::SpriteHandle& handle) const;

  // Functions which replace existing map with a new map
  void SetImageMap(const Json::Value& imageMapSpec,
                   SpriteCache* cache, SpriteSequenceContainer* seqContainer);
  void SetImageMap(ImageMap&& imageMap){_imageMap = imageMap;}
  // Checks image map against the Layer's layout to ensure spritebox names match up
  bool IsValidImageMap(const ImageMap& imageMap, bool requireAllSpriteBoxes = false) const;

private:
  LayerName _layerName;
  LayoutMap _layoutMap;
  ImageMap  _imageMap;
};


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
struct CompositeImageLayer::SpriteBox{
  SpriteBox(SpriteBoxName spriteBoxName,
            SpriteRenderConfig renderConfig,
            const Point2i& topLeftCorner, 
            int width, int height)
  : spriteBoxName(spriteBoxName)
  , renderConfig(renderConfig)
  , topLeftCorner(topLeftCorner)
  , width(width)
  , height(height){
    ValidateRenderConfig();
  }

  SpriteBox(const SpriteBox& spriteBox);

  SpriteBox(const SerializedSpriteBox& spriteBox);
  
  SpriteBox& operator=(SpriteBox other);
  bool operator ==(const SpriteBox& other) const;

  SerializedSpriteBox Serialize() const;
  bool ValidateRenderConfig() const;

  void GetPositionForFrame(const u32 frameIdx, Point2i& outTopLeftCorner, 
                           int& outWidth, int& outHeight) const;

  void SetLayoutModifier(CompositeImageLayoutModifier*& modifier);

  SpriteBoxName          spriteBoxName;
  // When the render method is custom hue a hue/saturation value of 0,0 
  // indicates that the sprite box should be rendered the color of the robot's eyes
  SpriteRenderConfig     renderConfig;

  private:
    std::unique_ptr<CompositeImageLayoutModifier> layoutModifier;
    Point2i       topLeftCorner;
    int           width;
    int           height;
};

// TODO: VIC-2414 - currently composite images can only be sent
// via sprite names in the image map. Should be able to have  a serialized
// sprite handle fallback that sends file paths or image chunks when appropriate
struct CompositeImageLayer::SpriteEntry{
  SpriteEntry(){};
  SpriteEntry(SpriteCache* cache,
              SpriteSequenceContainer* seqContainer,
              Vision::SpriteName spriteName,
              uint frameStartOffset = 0);

  SpriteEntry(Vision::SpriteSequence&& sequence);
  SpriteEntry(Vision::SpriteHandle spriteHandle);

          
  bool operator == (const SpriteEntry& other) const;
  bool operator != (const SpriteEntry& other) const{ return !(*this == other);}

  Vision::SpriteName GetSpriteName() const { return _spriteName;}
  bool GetFrame(const u32 index, Vision::SpriteHandle& handle) const;
  uint GetNumFrames() const { return _frameStartOffset + _spriteSequence.GetNumFrames(); }
  bool ContentIsValid() const { return _spriteSequence.GetNumFrames() > 0;}

private:
  // Allow sprite entries to offset 
  uint _frameStartOffset = 0;
  Vision::SpriteSequence _spriteSequence;
  // For serialization only
  Vision::SpriteName _spriteName = Vision::SpriteName::Count;
  
};

}; // namespace Vision
}; // namespace Anki

#endif // __Vision_CompositeImageLayer_fwd_H__

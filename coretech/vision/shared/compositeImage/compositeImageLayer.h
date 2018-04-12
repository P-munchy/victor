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
static const char* kSpriteBoxLayoutKey = "spriteBoxLayout";
static const char* kSpriteBoxNameKey   = "spriteBoxName";
static const char* kSpriteNameKey      = "spriteName";
static const char* kCornerXKey         = "x";
static const char* kCornerYKey         = "y";
static const char* kWidthKey           = "width";
static const char* kHeightKey          = "height";
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
class CompositeImageLayer{
public:
  struct SpriteBox;
  using LayoutMap = std::map<SpriteBoxName, SpriteBox>;
  using ImageMap  = std::unordered_map<SpriteBoxName, Vision::SpriteName, Anki::Util::EnumHasher>;

  CompositeImageLayer() {};
  CompositeImageLayer(const Json::Value& layoutSpec);
  CompositeImageLayer(LayerName layerName,
                      LayoutMap&& layoutSpec)
  : _layerName(layerName)
  , _layoutMap(std::move(layoutSpec)){}
  virtual ~CompositeImageLayer();

  LayerName        GetLayerName() const { return _layerName;}
  const LayoutMap& GetLayoutMap() const { return _layoutMap;}
  const ImageMap&  GetImageMap()  const { return _imageMap;}

  // Returns true if image has been set for sprite box name and outName has been set
  bool GetSpriteName(SpriteBoxName sbName, Vision::SpriteName& outName)  const;

  // Functions which add on to the current layout
  void AddToLayout(SpriteBoxName sbName, const SpriteBox& spriteBox);
  void AddToImageMap(SpriteBoxName sbName, const Vision::SpriteName& spriteName);

  // Functions which replace existing map with a new map
  void SetImageMap(const Json::Value& imageMapSpec);
  void SetImageMap(ImageMap&& imageMap){_imageMap = imageMap;}
  // Checks image map against the Layer's layout to ensure spritebox names match up
  bool IsValidImageMap(const ImageMap& imageMap, bool requireAllSpriteBoxes = false) const;

private:
  LayerName _layerName = LayerName::StaticBackground;
  LayoutMap _layoutMap;
  ImageMap  _imageMap;
};


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
struct CompositeImageLayer::SpriteBox{
  SpriteBox(SpriteBoxName spriteBoxName,
            const Point2i& topLeftCorner, 
            int width, int height)
  : spriteBoxName(spriteBoxName)
  , topLeftCorner(topLeftCorner)
  , width(width)
  , height(height){}

  SpriteBox(const SerializedSpriteBox& spriteBox)
  : spriteBoxName(spriteBox.name)
  , topLeftCorner(spriteBox.topLeftX, spriteBox.topLeftY)
  , width(spriteBox.width)
  , height(spriteBox.height){}

  SerializedSpriteBox Serialize() const;

  SpriteBoxName spriteBoxName;
  Point2i       topLeftCorner;
  int           width;
  int           height;
};

}; // namespace Vision
}; // namespace Anki

#endif // __Vision_CompositeImageLayer_fwd_H__

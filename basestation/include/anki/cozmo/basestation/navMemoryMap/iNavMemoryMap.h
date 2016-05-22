/**
 * File: iNavMemoryMap.h
 *
 * Author: Raul
 * Date:   12/09/2015
 *
 * Description: Public interface for a map of the space navigated by the robot with some memory 
 * features (like decay = forget).
 *
 * Copyright: Anki, Inc. 2015
 **/

#ifndef ANKI_COZMO_INAV_MEMORY_MAP_H
#define ANKI_COZMO_INAV_MEMORY_MAP_H

#include "navMemoryMapTypes.h"
#include "quadData/iNavMemoryMapQuadData.h"

#include "anki/common/basestation/math/point.h"
#include "anki/common/basestation/math/quad.h"
#include "anki/common/basestation/math/pose.h"
#include "util/logging/logging.h"

#include <set>

namespace Anki {
namespace Cozmo {
  
class VizManager;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Class INavMemoryMap
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
class INavMemoryMap
{
public:

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Types
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

  // import types from NavMemoryMapTypes
  using BorderVector = NavMemoryMapTypes::BorderVector;
  using EContentType = NavMemoryMapTypes::EContentType;

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Construction/Destruction
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  virtual ~INavMemoryMap() {}

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Modification
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

  // add a quad with the specified content type and empty additional content
  void AddQuad(const Quad2f& quad, EContentType type) {
    ASSERT_NAMED(!NavMemoryMapTypes::ExpectsAdditionalData(type), "INavMemoryMap.AddQuad.ExpectedAdditionalData");
    AddQuadInternal(quad, type);
  }
  // add a quad with the specified additional content. Such content specifies the associated EContentType
  void AddQuad(const Quad2f& quad, const INavMemoryMapQuadData& content) {
    ASSERT_NAMED(NavMemoryMapTypes::ExpectsAdditionalData(content.type), "INavMemoryMap.AddQuad.NotEpectedAdditionalData");
    AddQuadInternal(quad, content);
  }
  
  // merge the given map into this map by applying to the other's information the given transform
  // although this methods allows merging any INavMemoryMap into any INavMemoryMap, subclasses are not
  // expected to provide support for merging other subclasses, but only other instances from the same
  // subclass
  virtual void Merge(const INavMemoryMap* other, const Pose3d& transform) = 0;
  
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Query
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

  // check whether the given content types would have any borders at the moment. This method is expected to
  // be faster than CalculateBorders for the same innerType/outerType combination, since it only queries
  // whether a border exists, without requiring calculating all of them
  virtual bool HasBorders(EContentType innerType, EContentType outerType) const = 0;
  virtual bool HasBorders(EContentType innerType, const std::set<EContentType>& outerTypes) const = 0;
  
  // retrieve the borders currently found in the map between the given types. This query is not const
  // so that the memory map can calculate and cache values upon being requested, rather than when
  // the map is modified. Function is expected to clear the vector before returning the new borders
  virtual void CalculateBorders(EContentType innerType, EContentType outerType, BorderVector& outBorders) = 0;
  virtual void CalculateBorders(EContentType innerType, const std::set<EContentType>& outerTypes, BorderVector& outBorders) = 0;

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Debug
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  
  // Render/stop rendering memory map
  virtual void Draw(size_t mapIdxHint) const = 0;
  virtual void ClearDraw() const = 0;
  
protected:

  // add a quad with the specified content type and empty additional content
  virtual void AddQuadInternal(const Quad2f& quad, EContentType type) = 0;
  // add a quad with the specified additional content. Such content specifies the associated EContentType
  virtual void AddQuadInternal(const Quad2f& quad, const INavMemoryMapQuadData& content) = 0;
  
}; // class
  
} // namespace
} // namespace

#endif // 

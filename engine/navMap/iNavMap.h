/**
 * File: iNavMap.h
 *
 * Author: Raul
 * Date:   12/09/2015
 *
 * Description: Public interface for a map of the space navigated by the robot with some memory 
 * features (like decay = forget).
 *
 * Copyright: Anki, Inc. 2015
 **/

#ifndef ANKI_COZMO_INAV_MAP_H
#define ANKI_COZMO_INAV_MAP_H

#include "memoryMap/memoryMapTypes.h"
#include "memoryMap/data/memoryMapData.h"

#include "anki/common/basestation/math/triangle.h"
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
class INavMap
{
public:
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Types		
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -		
		
  // import types from MemoryMapTypes		
  using BorderRegionVector    = MemoryMapTypes::BorderRegionVector;
  using EContentType          = MemoryMapTypes::EContentType;
  using FullContentArray      = MemoryMapTypes::FullContentArray;
  using NodeTransformFunction = MemoryMapTypes::NodeTransformFunction;
    
  
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Construction/Destruction
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  virtual ~INavMap() {}

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Modification
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

  // add a quad with the specified content type and empty additional content
  inline void AddQuad(const Quad2f& quad, EContentType type, TimeStamp_t timeMeasured) {
    DEV_ASSERT(!ExpectsAdditionalData(type), "INavMap.AddQuad.ExpectedAdditionalData");
    AddQuadInternal(quad, type, timeMeasured);
  }
  // add a quad with the specified additional content. Such content specifies the associated EContentType
  inline void AddQuad(const Quad2f& quad, const MemoryMapData& content) {
    DEV_ASSERT(ExpectsAdditionalData(content.type), "INavMap.AddQuad.NotExpectedAdditionalData");
    AddQuadInternal(quad, content);
  }
  
  // add a line with the specified content type and empty additional content
  inline void AddLine(const Point2f& from, const Point2f& to, EContentType type, TimeStamp_t timeMeasured) {
    DEV_ASSERT(!ExpectsAdditionalData(type), "INavMap.AddLine.ExpectedAdditionalData");
    AddLineInternal(from, to, type, timeMeasured);
  }
  // add a line with the specified additional content. Such content specifies the associated EContentType
  inline void AddLine(const Point2f& from, const Point2f& to, const MemoryMapData& content) {
    DEV_ASSERT(ExpectsAdditionalData(content.type), "INavMap.AddLine.NotExpectedAdditionalData");
    AddLineInternal(from, to, content);
  }

  // add a triangle with the specified content type and empty additional content
  inline void AddTriangle(const Triangle2f& tri, EContentType type, TimeStamp_t timeMeasured) {
    DEV_ASSERT(!ExpectsAdditionalData(type), "INavMap.AddTriangle.ExpectedAdditionalData");
    AddTriangleInternal(tri, type, timeMeasured);
  }
  // add a triangle with the specified additional content. Such content specifies the associated EContentType
  inline void AddTriangle(const Triangle2f& tri, const MemoryMapData& content) {
    DEV_ASSERT(ExpectsAdditionalData(content.type), "INavMap.AddTriangle.NotExpectedAdditionalData");
    AddTriangleInternal(tri, content);
  }

  // add a point with the specified content type and empty additional content
  inline void AddPoint(const Point2f& point, EContentType type, TimeStamp_t timeMeasured) {
    DEV_ASSERT(!ExpectsAdditionalData(type), "INavMap.AddPoint.ExpectedAdditionalData");
    AddPointInternal(point, type, timeMeasured);
  }
  // add a point with the specified additional content. Such content specifies the associated EContentType
  inline void AddPoint(const Point2f& point, const MemoryMapData& content) {
    DEV_ASSERT(ExpectsAdditionalData(content.type), "INavMap.AddPoint.NotExpectedAdditionalData");
    AddPointInternal(point, content);
  }
  
  // merge the given map into this map by applying to the other's information the given transform
  // although this methods allows merging any INavMap into any INavMap, subclasses are not
  // expected to provide support for merging other subclasses, but only other instances from the same
  // subclass
  virtual void Merge(const INavMap* other, const Pose3d& transform) = 0;

  // TODO: FillBorder should be local (need to specify a max quad that can perform the operation, otherwise the
  // bounds keeps growing as Cozmo explores). Profiling+Performance required
  // fills content regions of filledType that have borders with fillingType(s), converting the filledType region
  // into withThisType content
  void FillBorder(EContentType typeToReplace, const FullContentArray& neighborsToFillFrom, EContentType newTypeSet, TimeStamp_t timeMeasured) {
    DEV_ASSERT(!ExpectsAdditionalData(newTypeSet), "INavMap.FillBorder.CantFillExtraInfo");
    FillBorderInternal(typeToReplace, neighborsToFillFrom, newTypeSet, timeMeasured);
  }

  // replaces the given content type with the given new type, within the given quad
  void ReplaceContent(const Quad2f& quad, EContentType typeToReplace, EContentType newTypeSet, TimeStamp_t timeMeasured) {
    DEV_ASSERT(!ExpectsAdditionalData(newTypeSet), "INavMemoryMap.ReplaceContent.CantFillExtraInfo");
    ReplaceContentInternal(quad, typeToReplace, newTypeSet, timeMeasured);
  }
  
  // replaces the given content type with the given new type
  void ReplaceContent(EContentType typeToReplace, EContentType newTypeSet, TimeStamp_t timeMeasured) {
    DEV_ASSERT(!ExpectsAdditionalData(newTypeSet), "INavMemoryMap.ReplaceContent.CantFillExtraInfo");
    ReplaceContentInternal(typeToReplace, newTypeSet, timeMeasured);
  }
  
  // attempt to apply a transformation function to all nodes in the tree
  virtual void TransformContent(NodeTransformFunction transform) = 0;
  
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Query
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  
  // return the size of the area currently explored
  virtual double GetExploredRegionAreaM2() const = 0;
  // return the size of the area currently flagged as interesting edges
  virtual double GetInterestingEdgeAreaM2() const = 0;
  
  // returns the precision of content data in the memory map. For example, if you add a point, and later query for it,
  // the region that the point generated to store the point could have an error of up to this length.
  virtual float GetContentPrecisionMM() const = 0;

  // check whether the given content types would have any borders at the moment. This method is expected to
  // be faster than CalculateBorders for the same innerType/outerType combination, since it only queries
  // whether a border exists, without requiring calculating all of them
  virtual bool HasBorders(EContentType innerType, const FullContentArray& outerTypes) const = 0;
  
  // retrieve the borders currently found in the map between the given types. This query is not const
  // so that the memory map can calculate and cache values upon being requested, rather than when
  // the map is modified. Function is expected to clear the vector before returning the new borders
  virtual void CalculateBorders(EContentType innerType, const FullContentArray& outerTypes, BorderRegionVector& outBorders) = 0;
  
  // checks if the given ray collides with the given types (any quad with that types)
  virtual bool HasCollisionRayWithTypes(const Point2f& rayFrom, const Point2f& rayTo, const FullContentArray& types) const = 0;
  
  // returns true if there are any nodes of the given type, false otherwise
  virtual bool HasContentType(EContentType type) const = 0;
  
  // returns the time navMap was last changed
  virtual TimeStamp_t GetLastChangedTimeStamp() const = 0;
  
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Debug
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  
  // Render/stop rendering memory map
  virtual void DrawDebugProcessorInfo(size_t mapIdxHint) const = 0;
  virtual void ClearDraw() const = 0;
  
  // Broadcast memory map
  virtual void Broadcast(uint32_t originID) const = 0;
  virtual void BroadcastMemoryMapDraw(uint32_t originID, size_t mapIdxHint) const = 0;
  
protected:

  // add a quad with the specified content type and empty additional content
  virtual void AddQuadInternal(const Quad2f& quad, EContentType type, TimeStamp_t timeMeasured) = 0;
  // add a quad with the specified additional content. Such content specifies the associated EContentType
  virtual void AddQuadInternal(const Quad2f& quad, const MemoryMapData& content) = 0;

  // add a line with the specified content type and empty additional content
  virtual void AddLineInternal(const Point2f& from, const Point2f& to, EContentType type, TimeStamp_t timeMeasured) = 0;
  // add a line with the specified additional content. Such content specifies the associated EContentType
  virtual void AddLineInternal(const Point2f& from, const Point2f& to, const MemoryMapData& content) = 0;

  // add a triangle with the specified content type and empty additional content
  virtual void AddTriangleInternal(const Triangle2f& tri, EContentType type, TimeStamp_t timeMeasured) = 0;
  // add a triangle with the specified additional content. Such content specifies the associated EContentType
  virtual void AddTriangleInternal(const Triangle2f& tri, const MemoryMapData& content) = 0;

  // add a point with the specified content type and empty additional content
  virtual void AddPointInternal(const Point2f& point, EContentType type, TimeStamp_t timeMeasured) = 0;
  // add a point with the specified additional content. Such content specifies the associated EContentType
  virtual void AddPointInternal(const Point2f& point, const MemoryMapData& content) = 0;
  
  // change the content type from typeToReplace into newTypeSet if there's a border from any of the typesToFillFrom towards typeToReplace
  virtual void FillBorderInternal(EContentType typeToReplace, const FullContentArray& neighborsToFillFrom, EContentType newTypeSet, TimeStamp_t timeMeasured) = 0;

  // change the content type from typeToReplace into newTypeSet within the given quad
  virtual void ReplaceContentInternal(const Quad2f& inQuad, EContentType typeToReplace, EContentType newTypeSet, TimeStamp_t timeMeasured) = 0;

  // change the content type from typeToReplace into newTypeSet in all known space
  virtual void ReplaceContentInternal(EContentType typeToReplace, EContentType newTypeSet, TimeStamp_t timeMeasured) = 0;
  
}; // class

} // namespace
} // namespace

#endif // 

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

#include "coretech/common/engine/math/polygon.h"
#include "coretech/common/engine/math/pose.h"
#include "util/logging/logging.h"

#include <unordered_set>

namespace Anki {
namespace Cozmo {
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Class INavMemoryMap
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
class INavMap
{
friend class MapComponent;

public:
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Types		
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -		
		
  // import types from MemoryMapTypes		
  using BorderRegionVector    = MemoryMapTypes::BorderRegionVector;
  using EContentType          = MemoryMapTypes::EContentType;
  using FullContentArray      = MemoryMapTypes::FullContentArray;
  using NodeTransformFunction = MemoryMapTypes::NodeTransformFunction;
  using NodePredicate         = MemoryMapTypes::NodePredicate;
    
  
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Construction/Destruction
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  virtual ~INavMap() {}
  
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
  
  // Pack map data to broadcast
  virtual void GetBroadcastInfo(MemoryMapTypes::MapBroadcastData& info) const = 0;

  // populate a list of all data that matches the predicate
  virtual void FindContentIf(NodePredicate pred, MemoryMapTypes::MemoryMapDataConstList& output) const = 0;
  
protected:
  
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Modification
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  
  // NOTE: Leave modifying calls as protected methods, and access them via the friend classes (at the moment only
  //       MapComponent). The classes manage publication of map data, and need to monitor if the map state has changed

  // add a poly with the specified content. 
  virtual bool Insert(const Poly2f& poly, const MemoryMapData& data) = 0;
  
  // merge the given map into this map by applying to the other's information the given transform
  // although this methods allows merging any INavMap into any INavMap, subclasses are not
  // expected to provide support for merging other subclasses, but only other instances from the same
  // subclass
  virtual bool Merge(const INavMap* other, const Pose3d& transform) = 0;
 
  // attempt to apply a transformation function to all nodes in the tree
  virtual bool TransformContent(NodeTransformFunction transform) = 0;
  
  // attempt to apply a transformation function to all nodes in the tree constrained by poly
  virtual bool TransformContent(const Poly2f& poly, NodeTransformFunction transform) = 0;

  // TODO: FillBorder should be local (need to specify a max quad that can perform the operation, otherwise the
  // bounds keeps growing as Cozmo explores). Profiling+Performance required.
  // change the content type from typeToReplace into newTypeSet if there's a border from any of the typesToFillFrom towards typeToReplace
  virtual bool FillBorder(EContentType typeToReplace, const FullContentArray& neighborsToFillFrom, EContentType newTypeSet, TimeStamp_t timeMeasured) = 0;

}; // class

} // namespace
} // namespace

#endif // 

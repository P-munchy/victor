/**
 * File: quadTreeProcessor.cpp
 *
 * Author: Raul
 * Date:   01/13/2016
 *
 * Description: Class for processing a quadTree. It relies on the quadTree and quadTreeNodes to
 * share the proper information for the Processor.
 *
 * Copyright: Anki, Inc. 2016
 **/
#include "quadTreeProcessor.h"
#include "quadTree.h"

#include "coretech/common/engine/math/quad_impl.h"
#include "coretech/common/engine/math/polygon_impl.h"
#include "coretech/vision/engine/profiler.h"

#include "util/console/consoleInterface.h"
#include "util/cpuProfiler/cpuProfiler.h"
#include "util/logging/logging.h"

#include <limits>
#include <memory>
#include <typeinfo>
#include <type_traits>
#include <unordered_map>

namespace Anki {
namespace Vector {

CONSOLE_VAR(bool , kRenderSeeds        , "QuadTreeProcessor", false); // renders seeds differently for debugging purposes
CONSOLE_VAR(bool , kRenderBordersFrom  , "QuadTreeProcessor", false); // renders detected borders (origin quad)
CONSOLE_VAR(bool , kRenderBordersToDot , "QuadTreeProcessor", false); // renders detected borders (border center) as dots
CONSOLE_VAR(bool , kRenderBordersToQuad, "QuadTreeProcessor", false); // renders detected borders (destination quad)
CONSOLE_VAR(bool , kRenderBorder3DLines, "QuadTreeProcessor", false); // renders borders returned as 3D lines (instead of quads)
CONSOLE_VAR(float, kRenderZOffset      , "QuadTreeProcessor", 20.0f); // adds Z offset to all quads
CONSOLE_VAR(bool , kDebugFindBorders   , "QuadTreeProcessor", false); // prints debug information in console

#define DEBUG_FIND_BORDER(format, ...)                                                                          \
if ( kDebugFindBorders ) {                                                                                      \
  do{::Anki::Util::sChanneledInfoF(DEFAULT_CHANNEL_NAME, "NMQTProcessor", {}, format, ##__VA_ARGS__);}while(0); \
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
QuadTreeProcessor::QuadTreeProcessor()
: _quadTree(nullptr)
, _totalExploredArea_m2(0.0)
, _totalInterestingEdgeArea_m2(0.0)
{

}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void QuadTreeProcessor::OnNodeContentTypeChanged(const QuadTreeNode* node, const EContentType& oldType, const bool wasEmpty)
{
  
  using namespace MemoryMapTypes;
  EContentType newType = node->GetContent().data->type;

  DEV_ASSERT(oldType != newType, "QuadTreeProcessor.OnNodeContentTypeChanged.ContentNotChanged");

  // update exploration area based on the content type
  {
    const bool needsToRemove = !wasEmpty &&  node->IsEmptyType();
    const bool needsToAdd    =  wasEmpty && !node->IsEmptyType();
    if ( needsToRemove )
    {
      const float side_m = MM_TO_M(node->GetSideLen());
      const float area_m2 = side_m*side_m;
      _totalExploredArea_m2 -= area_m2;
    }
    else if ( needsToAdd )
    {
      const float side_m = MM_TO_M(node->GetSideLen());
      const float area_m2 = side_m*side_m;
      _totalExploredArea_m2 += area_m2;
    }
  }

  // update interesting edge
  {
    const bool shouldBeCountedOld = (oldType == EContentType::InterestingEdge);
    const bool shouldBeCountedNew = (newType == EContentType::InterestingEdge);
    const bool needsToRemove =  shouldBeCountedOld && !shouldBeCountedNew;
    const bool needsToAdd    = !shouldBeCountedOld &&  shouldBeCountedNew;
    if ( needsToRemove )
    {
      const float side_m = MM_TO_M(node->GetSideLen());
      const float area_m2 = side_m*side_m;
      _totalInterestingEdgeArea_m2 -= area_m2;
    }
    else if ( needsToAdd )
    {
      const float side_m = MM_TO_M(node->GetSideLen());
      const float area_m2 = side_m*side_m;
      _totalInterestingEdgeArea_m2 += area_m2;
    }
  }

  // if old content type is cached
  if ( IsCached(oldType) )
  {
    // remove the node from that cache
    DEV_ASSERT(_nodeSets[oldType].find(node) != _nodeSets[oldType].end(),
               "QuadTreeProcessor.OnNodeContentTypeChanged.InvalidRemove");
    _nodeSets[oldType].erase( node );
  }

  if ( IsCached(newType) )
  {
    // add node to that cache
    DEV_ASSERT(_nodeSets[newType].find(node) == _nodeSets[newType].end(),
               "QuadTreeProcessor.OnNodeContentTypeChanged.InvalidInsert");
    _nodeSets[newType].insert(node);
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void QuadTreeProcessor::OnNodeDestroyed(const QuadTreeNode* node)
{
  // if old content type is cached
  const EContentType oldContent = node->GetData()->type;
  if ( IsCached(oldContent) )
  {
    // remove the node from that cache
    DEV_ASSERT(_nodeSets[oldContent].find(node) != _nodeSets[oldContent].end(),
               "QuadTreeProcessor.OnNodeDestroyed.InvalidNode");
    _nodeSets[oldContent].erase(node);
  }

  // remove the area for this node if it was counted before
  {
    const bool wasOutOld = node->IsEmptyType();
    const bool needsToRemove = !wasOutOld;
    if ( needsToRemove )
    {
      const float side_m = MM_TO_M(node->GetSideLen());
      const float area_m2 = side_m*side_m;
      _totalExploredArea_m2 -= area_m2;
    }
  }
  
  // remove interesting edge area if it was counted before
  {
    const bool shouldBeCountedOld = (node->GetData()->type == EContentType::InterestingEdge);
    const bool needsToRemove =  shouldBeCountedOld;
    if ( needsToRemove )
    {
      const float side_m = MM_TO_M(node->GetSideLen());
      const float area_m2 = side_m*side_m;
      _totalInterestingEdgeArea_m2 -= area_m2;
    }
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void QuadTreeProcessor::GetNodesToFill(const NodePredicate& innerPred, const NodePredicate& outerPred, NodeSet& output)
{
  // search direction constants
  static const EClockDirection clockDir = EClockDirection::CW;
  static const std::array<EDirection, 4> cwDirs{{ EDirection::North, EDirection::East, EDirection::South, EDirection::West }};

  // find any node of typeToFill that satisfies pred(node, neighbor)
  std::deque<const QuadTreeNode*> unexpandedNodes;
  FoldFunctorConst fillTypeFilter = [&] (const QuadTreeNode& node) {
    // first check if node is typeToFill
    if ( innerPred( node.GetData() ) ) {
      QuadTreeNode::NodeCPtrVector neighbors;
      neighbors.reserve( 1 << node.GetLevel() );

      // check if this nodes has a neighbor of any typesToFillFrom
      for(const EDirection candidateDir : cwDirs) {
        neighbors.clear(); // AddSmallestNeighbors does not clear the output list itself
        node.AddSmallestNeighbors(candidateDir, clockDir, neighbors);

        for(const auto neighbor : neighbors) {
          if( outerPred( neighbor->GetData() ) ) {
            unexpandedNodes.push_back( &node );
            return;
          }
        }
      }
    }
  };
  ((const QuadTree*) _quadTree)->Fold(fillTypeFilter);

  // expand all nodes for fill
  while(!unexpandedNodes.empty()) {
    // get the next node and add it to the output list
    const QuadTreeNode* node = unexpandedNodes.front();
    unexpandedNodes.pop_front();
    output.insert(node);

    // get all of this nodes neighbors of the same type
    QuadTreeNode::NodeCPtrVector neighbors;
    neighbors.reserve( 1 << node->GetLevel() );

    for(const EDirection candidateDir : cwDirs) {
      neighbors.clear();
      node->AddSmallestNeighbors(candidateDir, clockDir, neighbors);

      // for any neighbor of the same type, if it has not already been expanded, add it the unexpanded list
      for(const auto neighbor : neighbors) {
        MemoryMapDataConstPtr neighborData = neighbor->GetData();
        if ( innerPred( neighbor->GetData() ) && (output.find(neighbor) == output.end()) ) {
          unexpandedNodes.push_back( neighbor );
        }
      } // done adding neighbors for this side
    } // finished all sides
  } // all nodes expanded
}
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool QuadTreeProcessor::FillBorder(EContentType filledType, EContentTypePackedType fillingTypeFlags, const MemoryMapDataPtr& data)
{
  DEV_ASSERT_MSG(IsCached(filledType),
                 "QuadTreeProcessor.FillBorder.FilledTypeNotCached",
                 "%s is not cached, which is needed for fast processing operations",
                 EContentTypeToString(filledType));
  NodePredicate innerCheckFunc = [&filledType](MemoryMapDataConstPtr inside) {
    return (filledType == inside->type);
  };
  NodePredicate outerCheckFunc = [&fillingTypeFlags](MemoryMapDataConstPtr outside) {
    return IsInEContentTypePackedType(outside->type, fillingTypeFlags);
  };
  return FillBorder( innerCheckFunc, outerCheckFunc, data );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool QuadTreeProcessor::FillBorder(const NodePredicate& innerPred, const NodePredicate& outerPred, const MemoryMapDataPtr& data)
{
  ANKI_CPU_PROFILE("QuadTreeProcessor.FillBorder");

  // should this timer be a member variable? It's normally desired to time all processors
  // together, but beware when merging stats from different maps (always current is the only one processing)
  static Vision::Profiler timer;
  // timer.SetPrintChannelName("Unfiltered");
  timer.SetPrintFrequency(5000);
  timer.Tic("QuadTreeProcessor.FillBorder");

  // calculate nodes being flooded directly. Note that we are not going to cause filled nodes to flood forward
  // into others. A second call to FillBorder would be required for that (consider for local fills when we have them,
  // since they'll be significally faster).
  
  // The reason why we cache points instead of nodes is because adding a point can cause change and destruction of nodes,
  // for example through automerges in parents whose children all become the new content. To prevent having to
  // update an iterator from this::OnNodeX events, cache centers and apply. The result algorithm should be
  // slightly slower, but much simpler to understand, debug and profile
  std::vector<Point2f> floodedQuadCenters;

  // grab nodes to fill and find their centers
  NodeSet nodesToFill;
  GetNodesToFill(innerPred, outerPred, nodesToFill);
  for( const auto& node : nodesToFill ) {
    floodedQuadCenters.emplace_back( node->GetCenter() );
  }
  
  // add flooded centers to the tree (note this does not cause flood filling)
  bool changed = false;
  MemoryMapDataPtr dataPtr = data->Clone();
  for( const auto& center : floodedQuadCenters ) {
    changed |= _quadTree->Insert(FastPolygon({center}), [&dataPtr] (auto _) { return dataPtr; });
  }
  
  timer.Toc("QuadTreeProcessor.FillBorder");
  return changed;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool QuadTreeProcessor::HasContentType(EContentType nodeType) const
{
  DEV_ASSERT_MSG(IsCached(nodeType), "QuadTreeProcessor.HasContentType", "%s is not cached",
                 EContentTypeToString(nodeType));
  
  // check if any nodes for that type are cached currently
  auto nodeSetMatch = _nodeSets.find(nodeType);
  const bool hasAny = (nodeSetMatch != _nodeSets.end()) && !nodeSetMatch->second.empty();
  return hasAny;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool QuadTreeProcessor::IsCached(EContentType contentType)
{
  switch( contentType ) {
    case EContentType::ObstacleObservable:
    case EContentType::ObstacleProx:
    case EContentType::ObstacleUnrecognized:
    case EContentType::InterestingEdge:
    case EContentType::NotInterestingEdge:
    case EContentType::Cliff:
    {
      return true;
    }
    case EContentType::Unknown:
    case EContentType::ClearOfObstacle:
    case EContentType::ClearOfCliff:
    case EContentType::_Count:
    {
      return false;
    }
  }
}
} // namespace Vector
} // namespace Anki

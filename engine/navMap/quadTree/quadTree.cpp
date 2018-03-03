/**
 * File: quadTree.cpp
 *
 * Author: Raul
 * Date:   12/09/2015
 *
 * Description: Mesh representation of known geometry and obstacles for/from navigation with quad trees.
 *
 * Copyright: Anki, Inc. 2015
 **/
#include "quadTree.h"

#include "engine/viz/vizManager.h"
#include "engine/robot.h"

#include "coretech/common/engine/math/point_impl.h"
#include "coretech/common/engine/math/quad_impl.h"
#include "coretech/common/engine/math/polygon_impl.h"

#include "coretech/messaging/engine/IComms.h"

#include "clad/externalInterface/messageEngineToGame.h"

#include "util/console/consoleInterface.h"
#include "util/cpuProfiler/cpuProfiler.h"
#include "util/logging/callstack.h"
#include "util/logging/logging.h"
#include "util/math/math.h"

#include <sstream>

namespace Anki {
namespace Cozmo {
  
class Robot;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
namespace {
// rsam note: I tweaked this to Initial=160mm, maxDepth=8 to get 256cm max area. With old the 200 I had to choose
// between 160cm (too small) or 320cm (too big). Incidentally we have gained 2mm per leaf node. I think performance-wise
// it will barely impact even slowest devices, but we need to keep an eye an all these numbers as we get data from
// real users
constexpr float kQuadTreeInitialRootSideLength = 160.0f;
constexpr uint8_t kQuadTreeInitialMaxDepth = 4;
constexpr uint8_t kQuadTreeMaxRootDepth = 8;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
QuadTree::QuadTree()
: QuadTreeNode({0,0,1}, kQuadTreeInitialRootSideLength, kQuadTreeInitialMaxDepth, QuadTreeTypes::EQuadrant::Root, nullptr)  // Note the root is created at z=1
{
  _processor.SetRoot( this );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
QuadTree::~QuadTree()
{
  // we are destroyed, stop our rendering
  _processor.SetRoot(nullptr);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
float QuadTree::GetContentPrecisionMM() const
{
  // return the length of the smallest quad allowed
  const float minSide_mm = kQuadTreeInitialRootSideLength / (1 << kQuadTreeInitialMaxDepth); // 1 << x = pow(2,x)
  return minSide_mm;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool QuadTree::Insert(const FastPolygon& poly, MemoryMapDataPtr data)
{
  ANKI_CPU_PROFILE("QuadTree::Insert");
  
  // if the root does not contain the poly, expand
  if ( !Contains( poly ) )
  {
    ExpandToFit( poly );
  }
  
  // run the insert on the expanded QT
  bool contentChanged = false;
  FoldFunctor accumulator = [&] (QuadTreeNode& node)
  {
    if ( node.GetData() == data ) { return; }

    node.GetData()->SetLastObservedTime(data->GetLastObservedTime());

    // split node if we can unsure if the incoming poly will fill the entire area
    if ( !node.IsContainedBy(poly) && !node.IsSubdivided() && node.CanSubdivide())
    {
      node.Subdivide( _processor );
    }
    
    if ( !node.IsSubdivided() )
    {
      if ( node.GetData()->CanOverrideSelfWithContent(data->type) ) {
        node.ForceSetDetectedContentType( data, _processor );
        contentChanged = true;
      }
    } 
  };
  Fold(accumulator, poly);

  // try to cleanup tree
  FoldFunctor merge = [this] (QuadTreeNode& node) { node.TryAutoMerge(_processor); };
  Fold(merge, poly, FoldDirection::DepthFirst);

  return contentChanged;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool QuadTree::Transform(const Poly2f& poly, NodeTransformFunction transform)
{
  // run the transform
  bool contentChanged = false;
  FoldFunctor trfm = [&] (QuadTreeNode& node)
    {
      MemoryMapDataPtr newData = transform(node.GetData());
      if ((node.GetData() != newData) && !node.IsSubdivided()) 
      {
        node.ForceSetDetectedContentType(newData, _processor);
        contentChanged = true;
      }
    };

  Fold(trfm, FastPolygon(poly));

  // try to cleanup tree
  FoldFunctor merge = [this] (QuadTreeNode& node) { node.TryAutoMerge(_processor); };
  Fold(merge, FastPolygon(poly), FoldDirection::DepthFirst);
  
  return contentChanged;
}
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool QuadTree::Transform(NodeTransformFunction transform)
{
  // run the transform
  bool contentChanged = false;
  FoldFunctor trfm = [&] (QuadTreeNode& node)
    {
      MemoryMapDataPtr newData = transform(node.GetData());
      if ((node.GetData() != newData) && !node.IsSubdivided()) 
      {
        node.ForceSetDetectedContentType(newData, _processor);
        contentChanged = true;
      }
    };

  Fold(trfm);

  // try to cleanup tree
  FoldFunctor merge = [this] (QuadTreeNode& node) { node.TryAutoMerge(_processor); };
  Fold(merge, FoldDirection::DepthFirst);
  
  return contentChanged;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool QuadTree::Merge(const QuadTree& other, const Pose3d& transform)
{
  // TODO rsam for the future, when we merge with transform, poses or directions stored as extra info are invalid
  // since they were wrt a previous origin!
  Pose2d transform2d(transform);

  // obtain all leaf nodes from the map we are merging from
  NodeCPtrVector leafNodes;
  other.Fold(
    [&leafNodes](const QuadTreeNode& node) {
      if (!node.IsSubdivided()) { leafNodes.push_back(&node); }
    });
  
  // note regarding quad size limit: when we merge one map into another, this map can expand or shift the root
  // to accomodate the information that we are receiving from 'other'. 'other' is considered to have more up to
  // date information than 'this', so it should be ok to let it destroy as much info as it needs by shifting the root
  // towards them. In an ideal world, it would probably come to a compromise to include as much information as possible.
  // This I expect to happen naturally, since it's likely that 'other' won't be fully expanded in the opposite direction.
  // It can however happen in Cozmo during explorer mode, and it's debatable which information is more relevant.
  // A simple idea would be to limit leafNodes that we add back to 'this' by some distance, for example, half the max
  // root length. That would allow 'this' to keep at least half a root worth of information with respect the new one
  // we are bringing in.
  
  // iterate all those leaf nodes, adding them to this tree
  bool changed = false;
  for( const auto& nodeInOther : leafNodes ) {
  
    // if the leaf node is unkown then we don't need to add it
    const bool isUnknown = ( nodeInOther->GetData()->type == EContentType::Unknown );
    if ( !isUnknown ) {
      // get transformed quad
      Quad2f transformedQuad2d;
      transform2d.ApplyTo(nodeInOther->MakeQuadXY(), transformedQuad2d);
      
      // NOTE: there's a precision problem when we add back the quads; when we add a non-axis aligned quad to the map,
      // we modify (if applicable) all quads that intersect with that non-aa quad. When we merge this information into
      // a different map, we have lost precision on how big the original non-aa quad was, since we have stored it
      // with the resolution of the memory map quad size. In general, when merging information from the past, we should
      // not rely on precision, but there ar things that we could do to mitigate this issue, for example:
      // a) reducing the size of the aaQuad being merged by half the size of the leaf nodes
      // or
      // b) scaling down aaQuad to account for this error
      // eg: transformedQuad2d.Scale(0.9f);
      // At this moment is just a known issue
      
      // add to this
      Poly2f transformedPoly;
      transformedPoly.ImportQuad2d(transformedQuad2d);
      
      changed |= Insert(transformedPoly, nodeInOther->GetData());
    }
  }
  return changed;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool QuadTree::ExpandToFit(const Poly2f& polyToCover)
{
  ANKI_CPU_PROFILE("QuadTree::ExpandToFit");
  
  // allow expanding several times until the poly fits in the tree, as long as we can expand, we keep trying,
  // relying on the root to tell us if we reached a limit
  bool expanded = false;
  bool fitsInMap = false;
  
  do
  {
    // find in which direction we are expanding, upgrade root level in that direction (center moves)
    const Vec2f& direction = polyToCover.ComputeCentroid() - Point2f{GetCenter().x(), GetCenter().y()};
    expanded = UpgradeRootLevel(direction, kQuadTreeMaxRootDepth, _processor);

    // check if the poly now fits in the expanded root
    fitsInMap = Contains(polyToCover);
    
  } while( !fitsInMap && expanded );

  // if the poly still doesn't fit, see if we can shift once
  if ( !fitsInMap )
  {
    // shift the root to try to cover the poly, by removing opposite nodes in the map
    ShiftRoot(polyToCover, _processor);

    // check if the poly now fits in the expanded root
    fitsInMap = Contains(polyToCover);
  }
  
  // the poly should be contained, if it's not, we have reached the limit of expansions and shifts, and the poly does not
  // fit, which will cause information loss
  if ( !fitsInMap ) {
    PRINT_NAMED_WARNING("QuadTree.Expand.InsufficientExpansion",
      "Quad caused expansion, but expansion was not enough PolyCenter(%.2f, %.2f), Root(%.2f,%.2f) with sideLen(%.2f).",
      polyToCover.ComputeCentroid().x(), polyToCover.ComputeCentroid().y(),
      GetCenter().x(), GetCenter().y(),
      GetSideLen() );
  }
  
  // always flag as dirty since we have modified the root (potentially)
  return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool QuadTree::ShiftRoot(const Poly2f& requiredPoints, QuadTreeProcessor& processor)
{
  bool xPlusAxisReq  = false;
  bool xMinusAxisReq = false;
  bool yPlusAxisReq  = false;
  bool yMinusAxisReq = false;
  const float rootHalfLen = _sideLen * 0.5f;

  // iterate every point and see what direction they need the root to shift towards
  for( const Point2f& p : requiredPoints )
  {
    xPlusAxisReq  = xPlusAxisReq  || FLT_GE(p.x(), _center.x()+rootHalfLen);
    xMinusAxisReq = xMinusAxisReq || FLT_LE(p.x(), _center.x()-rootHalfLen);
    yPlusAxisReq  = yPlusAxisReq  || FLT_GE(p.y(), _center.y()+rootHalfLen);
    yMinusAxisReq = yMinusAxisReq || FLT_LE(p.y(), _center.y()-rootHalfLen);
  }
  
  // can't shift +x and -x at the same time
  if ( xPlusAxisReq && xMinusAxisReq ) {
    PRINT_NAMED_WARNING("QuadTreeNode.ShiftRoot.CantShiftPMx", "Current root size can't accomodate given points");
    return false;
  }

  // can't shift +y and -y at the same time
  if ( yPlusAxisReq && yMinusAxisReq ) {
    PRINT_NAMED_WARNING("QuadTreeNode.ShiftRoot.CantShiftPMy", "Current root size can't accomodate given points");
    return false;
  }

  // cache which axes we shift in
  const bool xShift = xPlusAxisReq || xMinusAxisReq;
  const bool yShift = yPlusAxisReq || yMinusAxisReq;
  if ( !xShift && !yShift ) {
    // this means all points are contained in this node, we shouldn't be here
    PRINT_NAMED_ERROR("QuadTreeNode.ShiftRoot.AllPointsIn", "We don't need to shift");
    return false;
  }

  // the new center will be shifted in one or both axes, depending on xyIncrease
  // for example, if we left the root through the right, only the right side will expand, and the left will collapse,
  // but top and bottom will remain the same
  _center.x() = _center.x() + (xShift ? (xPlusAxisReq ? rootHalfLen : -rootHalfLen) : 0.0f);
  _center.y() = _center.y() + (yShift ? (yPlusAxisReq ? rootHalfLen : -rootHalfLen) : 0.0f);
  ResetBoundingBox();
  
  // if the root has children, update them, otherwise no further changes are necessary
  if ( !_childrenPtr.empty() )
  {
    // save my old children so that we can swap them with the new ones
    std::vector< std::unique_ptr<QuadTreeNode> > oldChildren;
    std::swap(oldChildren, _childrenPtr);
    
    // create new children
    const float chHalfLen = rootHalfLen*0.5f;
      
    _childrenPtr.emplace_back( new QuadTreeNode(Point3f{_center.x()+chHalfLen, _center.y()+chHalfLen, _center.z()}, rootHalfLen, _level-1, EQuadrant::TopLeft , this) ); // up L
    _childrenPtr.emplace_back( new QuadTreeNode(Point3f{_center.x()+chHalfLen, _center.y()-chHalfLen, _center.z()}, rootHalfLen, _level-1, EQuadrant::TopRight, this) ); // up R
    _childrenPtr.emplace_back( new QuadTreeNode(Point3f{_center.x()-chHalfLen, _center.y()+chHalfLen, _center.z()}, rootHalfLen, _level-1, EQuadrant::BotLeft , this) ); // lo L
    _childrenPtr.emplace_back( new QuadTreeNode(Point3f{_center.x()-chHalfLen, _center.y()-chHalfLen, _center.z()}, rootHalfLen, _level-1, EQuadrant::BotRight, this) ); // lo R

    // typedef to cast quadrant enum to the underlaying type (that can be assigned to size_t)
    using Q2N = std::underlying_type<EQuadrant>::type; // Q2N stands for "Quadrant To Number", it makes code below easier to read
    static_assert( sizeof(Q2N) < sizeof(size_t), "UnderlyingTypeIsBiggerThanSizeType");
    
    /* 
      Example of shift along both axes +x,+y
    
                      ^                                           ^ +y
                      | +y                                        |---- ----
                                                                  |    | TL |
                  ---- ----                                        ---- ----
        -x       | BL | TL |     +x               -x              | BR |    |  +x
       < ---      ---- ----      --->              < ---           ---- ----  --->
                 | BR | TR |
                  ---- ----
     
                      | -y                                        | -y
                      v                                           v
     
       Since the root can't expand anymore, we move it in the direction we would want to expand. Note in the example
       how TopLeft becomes BottomRight in the new root. We want to preserve the children of that direct child (old TL), but
       we need to hook them to a different child (new BR). That's essentially what the rest of this method does.
     
    */
    
    // this content is set to the children that don't inherit old children
    
    // calculate which children are brought over from the old ones
    if ( xShift && yShift )
    {
      // double move, only one child is preserved, which is the one in the same direction top the expansion one
      if ( xPlusAxisReq ) {
        if ( yPlusAxisReq ) {
          // we are moving along +x +y axes, top left becomes bottom right of the new root
          _childrenPtr[(Q2N)EQuadrant::BotRight]->SwapChildrenAndContent(oldChildren[(Q2N)EQuadrant::TopLeft].get(), processor);
        } else {
          // we are moving along +x -y axes, top right becomes bottom left of the new root
          _childrenPtr[(Q2N)EQuadrant::BotLeft ]->SwapChildrenAndContent(oldChildren[(Q2N)EQuadrant::TopRight].get(), processor);
        }
      }
      else
      {
        if ( yPlusAxisReq ) {
          // we are moving along -x +y axes, bottom left becomes top right of the new root
          _childrenPtr[(Q2N)EQuadrant::TopRight]->SwapChildrenAndContent(oldChildren[(Q2N)EQuadrant::BotLeft].get(), processor);
        } else {
          // we are moving along -x -y axes, bottom right becomes top left of the new root
          _childrenPtr[(Q2N)EQuadrant::TopLeft ]->SwapChildrenAndContent(oldChildren[(Q2N)EQuadrant::BotRight].get(), processor);
        }
      }
    }
    else if ( xShift )
    {
      // move only in one axis, two children are preserved, top or bottom
      if ( xPlusAxisReq )
      {
        // we are moving along +x axis, top children are preserved, but they become the bottom ones
        _childrenPtr[(Q2N)EQuadrant::BotLeft ]->SwapChildrenAndContent(oldChildren[(Q2N)EQuadrant::TopLeft].get(), processor );
        _childrenPtr[(Q2N)EQuadrant::BotRight]->SwapChildrenAndContent(oldChildren[(Q2N)EQuadrant::TopRight].get(), processor);
      }
      else
      {
        // we are moving along -x axis, bottom children are preserved, but they become the top ones
        _childrenPtr[(Q2N)EQuadrant::TopLeft ]->SwapChildrenAndContent(oldChildren[(Q2N)EQuadrant::BotLeft].get(), processor);
        _childrenPtr[(Q2N)EQuadrant::TopRight]->SwapChildrenAndContent(oldChildren[(Q2N)EQuadrant::BotRight].get(), processor);
      }
    }
    else if ( yShift )
    {
      // move only in one axis, two children are preserved, left or right
      if ( yPlusAxisReq )
      {
        // we are moving along +y axis, left children are preserved, but they become the right ones
        _childrenPtr[(Q2N)EQuadrant::TopRight]->SwapChildrenAndContent(oldChildren[(Q2N)EQuadrant::TopLeft].get(), processor);
        _childrenPtr[(Q2N)EQuadrant::BotRight]->SwapChildrenAndContent(oldChildren[(Q2N)EQuadrant::BotLeft].get(), processor);
      }
      else
      {
        // we are moving along -y axis, right children are preserved, but they become the left ones
        _childrenPtr[(Q2N)EQuadrant::TopLeft ]->SwapChildrenAndContent(oldChildren[(Q2N)EQuadrant::TopRight].get(), processor);
        _childrenPtr[(Q2N)EQuadrant::BotLeft ]->SwapChildrenAndContent(oldChildren[(Q2N)EQuadrant::BotRight].get(), processor);
      }
    }
    
    // destroy the nodes that are going away because we shifted away from them
    DestroyNodes(oldChildren, processor);
  }
  
  // log
  PRINT_CH_INFO("QuadTree", "QuadTree.ShiftRoot", "Root level is still %u, root shifted. Allowing %.2fm", _level, MM_TO_M(_sideLen));
  
  // successful shift
  return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool QuadTree::UpgradeRootLevel(const Point2f& direction, uint8_t maxRootLevel, QuadTreeProcessor& processor)
{
  DEV_ASSERT(!NEAR_ZERO(direction.x()) || !NEAR_ZERO(direction.y()),
             "QuadTreeNode.UpgradeRootLevel.InvalidDirection");
  
  // reached expansion limit
  if ( _level == std::numeric_limits<uint8_t>::max() || _level >= maxRootLevel) {
    return false;
  }

  // save my old children to store in the child that is taking my spot
  std::vector< std::unique_ptr<QuadTreeNode> > oldChildren;
  std::swap(oldChildren, _childrenPtr);

  const bool xPlus = FLT_GE_ZERO(direction.x());
  const bool yPlus = FLT_GE_ZERO(direction.y());
  
  // move to its new center
  const float oldHalfLen = _sideLen * 0.50f;
  _center.x() = _center.x() + (xPlus ? oldHalfLen : -oldHalfLen);
  _center.y() = _center.y() + (yPlus ? oldHalfLen : -oldHalfLen);

  // create new children
  _childrenPtr.emplace_back( new QuadTreeNode(Point3f{_center.x()+oldHalfLen, _center.y()+oldHalfLen, _center.z()}, _sideLen, _level, EQuadrant::TopLeft , this) ); // up L
  _childrenPtr.emplace_back( new QuadTreeNode(Point3f{_center.x()+oldHalfLen, _center.y()-oldHalfLen, _center.z()}, _sideLen, _level, EQuadrant::TopRight, this) ); // up R
  _childrenPtr.emplace_back( new QuadTreeNode(Point3f{_center.x()-oldHalfLen, _center.y()+oldHalfLen, _center.z()}, _sideLen, _level, EQuadrant::BotLeft , this) ); // lo L
  _childrenPtr.emplace_back( new QuadTreeNode(Point3f{_center.x()-oldHalfLen, _center.y()-oldHalfLen, _center.z()}, _sideLen, _level, EQuadrant::BotRight, this) ); // lo R

  // calculate the child that takes my place by using the opposite direction to expansion
  size_t childIdx = 0;
  if      ( !xPlus &&  yPlus ) { childIdx = 1; }
  else if (  xPlus && !yPlus ) { childIdx = 2; }
  else if (  xPlus &&  yPlus ) { childIdx = 3; }
  QuadTreeNode& childTakingMyPlace = *_childrenPtr[childIdx];
  
  
  // set the new parent in my old children
  for ( auto& childPtr : oldChildren ) {
    childPtr->ChangeParent( &childTakingMyPlace );
  }
  
  // swap children with the temp
  std::swap(childTakingMyPlace._childrenPtr, oldChildren);

  // set the content type I had in the child that takes my place, then reset my content
  childTakingMyPlace.ForceSetDetectedContentType( _content.data, processor );
  ForceSetDetectedContentType(MemoryMapDataPtr(), processor);
  
  // upgrade my remaining stats
  _sideLen = _sideLen * 2.0f;
  ++_level;
  ResetBoundingBox();

  // log
  PRINT_CH_INFO("QuadTree", "QuadTree.UpdgradeRootLevel", "Root expanded to level %u. Allowing %.2fm", _level, MM_TO_M(_sideLen));
  
  return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
inline void QuadTree::ResetBoundingBox()
{
  Point3f offset(_sideLen/2, _sideLen/2, 0);
  _boundingBox = AxisAlignedQuad(_center - offset, _center + offset );
}
  

} // namespace Cozmo
} // namespace Anki

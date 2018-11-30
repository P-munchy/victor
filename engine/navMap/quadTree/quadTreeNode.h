/**
 * File: quadTreeNode.h
 *
 * Author: Raul
 * Date:   12/09/2015
 *
 * Description: Nodes in the nav mesh, represented as quad tree nodes.
 * Note nodes can work with a processor to speed up algorithms and searches, however this implementation supports
 * working with one processor only for any given node. Do not use more than one processor instance for nodes, or
 * otherwise leaks and bad pointer references will happen.
 *
 * Copyright: Anki, Inc. 2015
 **/

#ifndef ANKI_COZMO_QUAD_TREE_NODE_H
#define ANKI_COZMO_QUAD_TREE_NODE_H

#include "quadTreeTypes.h"

#include "engine/navMap/memoryMap/data/memoryMapData.h"
#include "coretech/common/engine/math/axisAlignedHyperCube.h"

#include "util/helpers/noncopyable.h"

#include <memory>
#include <vector>

namespace Anki {
namespace Vector {

class QuadTreeProcessor;
using namespace QuadTreeTypes;
using namespace MemoryMapTypes;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
class QuadTreeNode : private Util::noncopyable
{
  friend class QuadTree;
public:
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Types
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  
  using NodeCPtrVector = std::vector<const QuadTreeNode*>;  
  using NodeContent    = QuadTreeTypes::NodeContent;
  using FoldFunctor    = QuadTreeTypes::FoldFunctor;
  using FoldDirection  = QuadTreeTypes::FoldDirection;
  
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Accessors
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

  bool                   IsRootNode()     const { return _parent == nullptr; }
  bool                   IsSubdivided()   const { return !_childrenPtr.empty(); }
  bool                   IsEmptyType()    const { return IsSubdivided() || (GetData()->type == EContentType::Unknown); }
  uint8_t                GetLevel()       const { return _level; }
  float                  GetSideLen()     const { return _sideLen; }
  const Point2f&         GetCenter()      const { return _center; }
  MemoryMapDataPtr       GetData()        const { return _content.data; }
  const NodeContent&     GetContent()     const { return _content; }
  const NodeAddress&     GetAddress()     const { return _address; }
  const AxisAlignedQuad& GetBoundingBox() const { return _boundingBox; }



  // run the provided accumulator function recursively over the tree for all nodes intersecting with region (if provided).
  // NOTE: any recursive call through the QTN should be implemented by fold so all collision checks happen in a consistant manner
  void Fold(const FoldFunctorConst& accumulator, FoldDirection dir = FoldDirection::BreadthFirst) const;
  void Fold(const FoldFunctorConst& accumulator, const FoldableRegion& region, FoldDirection dir = FoldDirection::BreadthFirst) const;
  
  // finds all the leaf nodes that are neighbors with this node
  NodeCPtrVector GetNeighbors() const;

  // find a node at a particular address
  const QuadTreeNode* GetNodeAtAddress(const NodeAddress& addr) const;

protected:

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Initialization
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  
  // Leave the constructor as a protected member so only the root node or other Quad tree nodes can create new nodes
  // it will allow subdivision as long as level is greater than 0
  QuadTreeNode(const QuadTreeNode* parent = nullptr, EQuadrant quadrant = EQuadrant::Root);
   
  // with noncopyable this is not needed, but xcode insist on showing static_asserts in cpp as errors for a while,
  // which is annoying
  QuadTreeNode(const QuadTreeNode&&) = delete;
  QuadTreeNode& operator=(const QuadTreeNode&&) = delete;
  
  // updates the address incase tree structure changes (expands and shifts)
  void ResetAddress();

  // find a node at a particular address
  QuadTreeNode* GetNodeAtAddress(const NodeAddress& addr);
  
  // subdivide/join children
  bool Subdivide();
  bool Join(QuadTreeProcessor& processor);

  // copys the data of this node to its children, and resets its own data
  void MoveDataToChildren(QuadTreeProcessor& processor);

private:

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Types
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  
  // info about moving towards a neighbor
  struct MoveInfo {
    EQuadrant neighborQuadrant;  // destination quadrant
    bool sharesParent;           // whether destination quadrant is in the same parent
  };
    
  // container for each node's children
  using ChildrenVector = std::vector< std::unique_ptr<QuadTreeNode> >;
  
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Query
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -                 
  
  // return true if this quad can subdivide
  bool CanSubdivide() const { return (_level > 0) && !IsSubdivided(); }
  
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Modification
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -


  // checks if all children are the same type, if so it removes the children and merges back to a single parent
  void TryAutoMerge(QuadTreeProcessor& processor);
  
  // force sets the type and updates shared container
  void ForceSetDetectedContentType(const MemoryMapDataPtr detectedContent, QuadTreeProcessor& processor);
  
  // sets a new parent to this node. Used on expansions
  void ChangeParent(const QuadTreeNode* newParent) { _parent = newParent; }
  
  // swaps children and content with 'otherNode', updating the children's parent pointer
  void SwapChildrenAndContent(QuadTreeNode* otherNode, QuadTreeProcessor& processor);
  
  // read the note in destructor on why we manually destroy nodes when they are removed
  static void DestroyNodes(ChildrenVector& nodes, QuadTreeProcessor& processor);

  // run the provided accumulator function recursively over the tree for all nodes intersecting with region (if provided).
  // NOTE: mutable recursive calls should remain private to ensure tree invariants are held
  void Fold(FoldFunctor& accumulator, FoldDirection dir = FoldDirection::BreadthFirst);
  void Fold(FoldFunctor& accumulator, const FoldableRegion& region, FoldDirection dir = FoldDirection::BreadthFirst);
  
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Exploration
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  
  // get the child in the given quadrant, or null if this node is not subdivided
  const QuadTreeNode* GetChild(EQuadrant quadrant) const;
  QuadTreeNode* GetChild(EQuadrant quadrant);

  // iterate until we reach the nodes that have a border in the given direction, and add them to the vector
  // NOTE: this method is expected to NOT clear the vector before adding descendants
  void AddSmallestDescendants(EDirection direction, NodeCPtrVector& descendants) const;
   
  // find the neighbor of the same or higher level in the given direction
  const QuadTreeNode* FindSingleNeighbor(EDirection direction) const;

  
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Attributes
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  
  // NOTE: try to minimize padding in these attributes

  // children when subdivided. Can be empty or have 4 nodes
  ChildrenVector _childrenPtr;

  // coordinates of this quad
  Point2f _center;
  float   _sideLen;

  AxisAlignedQuad _boundingBox;

  // parent node
  const QuadTreeNode* _parent;

  // our level
  uint8_t _level;

  // quadrant within the parent
  EQuadrant _quadrant;
  NodeAddress _address;
  
  // information about what's in this quad
  NodeContent _content;
    
}; // class
  

} // namespace
} // namespace

#endif //

/**
 * File: navMeshQuadTree.h
 *
 * Author: Raul
 * Date:   12/09/2015
 *
 * Description: Mesh representation of known geometry and obstacles for/from navigation with quad trees.
 *
 * Copyright: Anki, Inc. 2015
 **/

#ifndef ANKI_COZMO_NAV_MESH_QUAD_TREE_H
#define ANKI_COZMO_NAV_MESH_QUAD_TREE_H

#include "navMeshQuadTreeNode.h"
#include "navMeshQuadTreeProcessor.h"

#include "anki/cozmo/basestation/navMemoryMap/navMemoryMapTypes.h"
#include "anki/common/basestation/math/point.h"

namespace Anki {
namespace Cozmo {

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
class NavMeshQuadTree
{
public:

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Initialization
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  
  // constructor/destructor
  NavMeshQuadTree(VizManager* vizManager);
  ~NavMeshQuadTree();
  
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Render
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

  // Render navmesh
  void Draw(size_t mapIdxHint) const;

  // Stop rendering navmesh
  void ClearDraw() const;
  
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Accessors
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

  // notify the navmesh that the given quad has the specified content
  void AddQuad(const Quad2f& quad, const NodeContent& nodeContent);
  
  // merge the given quadtree into this quad tree, applying to the quads from other the given transform
  void Merge(const NavMeshQuadTree& other, const Pose3d& transform);
  
  // return the Processor associated to this QuadTree for queries
  NavMeshQuadTreeProcessor& GetProcessor() { return _processor; }
  const NavMeshQuadTreeProcessor& GetProcessor() const { return _processor; }
  
private:

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Node operations
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  
  // Expand the root node so that the given quad is included in the navMesh
  void Expand(const Quad2f& quadToCover);

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Attributes
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

  // set to true if data has changed since last time we send to gfx
  mutable bool _gfxDirty;

  // processor for this quadtree
  NavMeshQuadTreeProcessor _processor;

  // current root of the tree. It expands as needed
  NavMeshQuadTreeNode _root;
  
  VizManager* _vizManager;
  
}; // class
  
} // namespace
} // namespace

#endif //

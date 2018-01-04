/**
* File: physVizController
*
* Author: damjan stulic
* Created: 9/15/15
*
* Description: 
*
* Copyright: Anki, inc. 2015
*
*/
#ifndef __CozmoPhysics_PhysVizControllerImpl_H__
#define __CozmoPhysics_PhysVizControllerImpl_H__

#include "coretech/messaging/shared/UdpServer.h"
#include "anki/common/basestation/math/point.h"
#include "anki/common/basestation/math/point_impl.h"
#include "engine/events/ankiEventMgr.h"
#include "clad/externalInterface/messageEngineToGame.h"
#include "clad/vizInterface/messageViz.h"
#include <vector>
#include <unordered_map>
#include <map>
#define DEBUG_COZMO_PHYSICS 0


namespace Anki {
namespace Cozmo {
  
using SimpleQuadVector = std::vector<VizInterface::SimpleQuad>;

  
class MemoryMapNode
{
public:
  MemoryMapNode(int depth, float size_m, const Point3f& center);
  
  bool AddChild(SimpleQuadVector& destSimpleQuads, const ExternalInterface::ENodeContentTypeDebugVizEnum content, const int depth);
  
private:
  int     _depth;
  float   _size_m;
  Point3f _center;
  int     _nextChild;
  std::vector<MemoryMapNode> _children;
};
  
class PhysVizController
{

public:
  PhysVizController() {};

  void Init();
  void Update();
  void Draw(int pass, const char *view);
  void Cleanup();
  
private:

  void ProcessMessage(VizInterface::MessageViz&& message);

  void Subscribe(const VizInterface::MessageVizTag& tagType, std::function<void(const AnkiEvent<VizInterface::MessageViz>&)> messageHandler) {
    _eventMgr.SubscribeForever(static_cast<uint32_t>(tagType), messageHandler);
  }

  void ProcessVizObjectMessage(const AnkiEvent<VizInterface::MessageViz>& msg);
  void ProcessVizSegmentPrimitiveMessage(const AnkiEvent<VizInterface::MessageViz>& msg);
  void ProcessVizQuadMessage(const AnkiEvent<VizInterface::MessageViz>& msg);
  void ProcessVizSimpleQuadVectorMessageBegin(const AnkiEvent<VizInterface::MessageViz>& msg);
  void ProcessVizSimpleQuadVectorMessage(const AnkiEvent<VizInterface::MessageViz>& msg);
  void ProcessVizSimpleQuadVectorMessageEnd(const AnkiEvent<VizInterface::MessageViz>& msg);
  void ProcessVizMemoryMapMessageDebugVizBegin(const AnkiEvent<VizInterface::MessageViz>& msg);
  void ProcessVizMemoryMapMessageDebugViz(const AnkiEvent<VizInterface::MessageViz>& msg);
  void ProcessVizMemoryMapMessageDebugVizEnd(const AnkiEvent<VizInterface::MessageViz>& msg);
  void ProcessVizEraseObjectMessage(const AnkiEvent<VizInterface::MessageViz>& msg);
  void ProcessVizEraseSegmentPrimitivesMessage(const AnkiEvent<VizInterface::MessageViz>& msg);
  void ProcessVizEraseQuadMessage(const AnkiEvent<VizInterface::MessageViz>& msg);
  void ProcessVizAppendPathSegmentLineMessage(const AnkiEvent<VizInterface::MessageViz>& msg);
  void ProcessVizAppendPathSegmentArcMessage(const AnkiEvent<VizInterface::MessageViz>& msg);
  void ProcessVizSetPathColorMessage(const AnkiEvent<VizInterface::MessageViz>& msg);
  void ProcessVizErasePathMessage(const AnkiEvent<VizInterface::MessageViz>& msg);
  void ProcessVizDefineColorMessage(const AnkiEvent<VizInterface::MessageViz>& msg);
  void ProcessVizShowObjectsMessage(const AnkiEvent<VizInterface::MessageViz>& msg);
  void ProcessVizSetOriginMessage(const AnkiEvent<VizInterface::MessageViz>& msg);

  void DrawTextAtOffset(std::string s, float x_off, float y_off, float z_off);
  void DrawCuboid(float x_dim, float y_dim, float z_dim);
  void DrawRamp(float platformLength, float slopeLength, float width, float height);
  void DrawHead(float width, float height, float depth);
  void DrawTetrahedronMarker(const float x, const float y, const float z,
    const float length_x, const float length_y, const float length_z);
  void DrawRobot(Anki::Cozmo::VizRobotMarkerType type);
  void DrawPredockPose();
  void DrawQuad(const float xUpperLeft,  const float yUpperLeft, const float zUpperLeft,
    const float xLowerLeft,  const float yLowerLeft, const float zLowerLeft,
    const float xUpperRight, const float yUpperRight, const float zUpperRight,
    const float xLowerRight, const float yLowerRight, const float zLowerRight);
  void DrawQuadFill(const float xUpperLeft,  const float yUpperLeft, const float zUpperLeft,
    const float xLowerLeft,  const float yLowerLeft, const float zLowerLeft,
    const float xUpperRight, const float yUpperRight, const float zUpperRight,
    const float xLowerRight, const float yLowerRight, const float zLowerRight);


  AnkiEventMgr<VizInterface::MessageViz> _eventMgr;

  struct PathPoint {
    float x;
    float y;
    float z;
    bool isStartOfSegment;
    
    PathPoint(float x, float y, float z, bool isStartOfSegment = false){
      this->x = x;
      this->y = y;
      this->z = z;
      this->isStartOfSegment = isStartOfSegment;
    }
  };
  

  // Types for paths
  //using PathVertex_t = std::vector<float>;
  //using Path_t = std::vector<PathVertex_t>;
  //using PathMap_t = std::unordered_map<uint32_t, Path_t >;
  //
  //// Map of all paths indexed by robotID and pathID
  //PathMap_t pathMap_;
  std::unordered_map<uint32_t, std::vector<PathPoint> > _pathMap;

  // Map of pathID to colorID
  std::unordered_map<uint32_t, uint32_t> _pathColorMap;

  // objects
  //using VizObject_t = std::unordered_map<uint32_t, VizInterface::Object>;
  //VizObject_t objectMap_;
  std::map<uint32_t, VizInterface::Object> _objectMap;

  // quads
  //using VizQuadMap_t = std::unordered_map<uint32_t, VizInterface::Quad>;
  //using VizQuadTypeMap_t = std::unordered_map<uint32_t, VizQuadMap_t>;
  //VizQuadTypeMap_t quadMap_;
  std::unordered_map<uint32_t, std::unordered_map<uint32_t, VizInterface::Quad> > _quadMap;
  
  // quad arrays injected by name instead of requiring one ID per quad
  std::unordered_map<std::string, SimpleQuadVector> _simpleQuadVectorMapReady;    // ready to draw
  std::unordered_map<std::string, SimpleQuadVector> _simpleQuadVectorMapIncoming; // incoming from the socket
  
  // memory map quad info data
  // Maps message sequence number to message. This allows use to check that all messages were received and deals with
  // out of order messages
  using MemoryMapQuadInfoDebugVizVector = std::map<u32, std::vector<ExternalInterface::MemoryMapQuadInfoDebugViz>>;
  std::unordered_map<uint32_t, MemoryMapQuadInfoDebugVizVector> _memoryMapQuadInfoDebugVizVectorMapIncoming;  // incoming from the socket
  std::unordered_map<uint32_t, ExternalInterface::MemoryMapInfo> _memoryMapInfo;
  
  struct Segment {
    Segment() : color(0) {}
    Segment(uint32_t c, const std::array<float, 3>& o, const std::array<float, 3>& d) :
      color(c), origin(o), dest(d) {}
    uint32_t color;
    std::array<float, 3> origin;
    std::array<float, 3> dest;
  };
  
  // segment primitives
  using SegmentVector = std::vector<Segment>;
  std::map<std::string, SegmentVector> _segmentPrimitives;

  // Color map
  //using VizColorDef_t = std::unordered_map<uint32_t, VizInterface::DefineColor>;
  //VizColorDef_t colorMap_;
  std::unordered_map<uint32_t, VizInterface::DefineColor> _colorMap;

  // Server that listens for visualization messages from basestation's VizManger
  UdpServer _server;

  // Whether or not to draw anything
  bool _drawEnabled = true;

  // Default height offset of paths (m)
  float _heightOffset = 0.045f;

  // Default angular resolution of arc path segments (radians)
  float _arcRes_rad = 0.2f;

  // Global offset
  float _globalRotation[4] = {0,0,0,0};    // angle, axis_x, axis_y, axis_z
  float _globalTranslation[3] = {0, 0, 0}; // x,y,z

};


} // end namespace Cozmo
} // end namespace Anki



#endif //__CozmoPhysics_PhysVizControllerImpl_H__

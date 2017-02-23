/**
 * File: vizManager.h
 *
 * Author: Kevin Yoon
 * Date:   2/5/2014
 *
 * Description: Implements the VizManager class for vizualizing objects such as 
 *              blocks and robot paths in a Webots simulated world. The Webots 
 *              world needs to invoke the cozmo_physics plugin in order for this to work.
 *
 * Copyright: Anki, Inc. 2014
 **/

#ifndef VIZ_MANAGER_H
#define VIZ_MANAGER_H

#include "anki/common/basestation/math/fastPolygon2d.h"
#include "anki/common/basestation/math/pose.h"
#include "anki/common/basestation/math/polygon.h"
#include "anki/common/basestation/colorRGBA.h"
#include "anki/common/types.h"
#include "util/helpers/ankiDefines.h"
#include "anki/vision/CameraSettings.h"
#include "anki/planning/shared/path.h"
#include "anki/messaging/shared/UdpClient.h"
#include "clad/types/imageTypes.h"
#include "clad/types/vizTypes.h"
#include "clad/types/objectTypes.h"
#include "clad/types/robotStatusAndActions.h"
#include "clad/vizInterface/messageViz.h"
#include "util/signals/simpleSignal_fwd.h"
#include "util/math/numericCast.h"

#include <vector>
#include <map>

#if defined(ANKI_PLATFORM_ANDROID) || defined(ANKI_PLATFORM_IOS)
#define VIZ_ON_DEVICE 1
#else
#define VIZ_ON_DEVICE 0
#endif

namespace Anki {
  
  // Forward declaration
  namespace Vision {
    class TrackedFace;
  }
  
  namespace Cozmo {

  namespace VizInterface {
  class MessageViz;
  enum class MessageVizTag : uint8_t;
  struct RobotMood;
  } // end namespace VizInterface
    
    class GameMessagePort;
    class IExternalInterface;

    class VizManager
    {
    public:
      
      typedef enum : u8 {
        ACTION,
        LOCALIZED_TO,
        WORLD_ORIGIN,
        VISION_MODE,
        BEHAVIOR_STATE,
        ANIMATION_NAME,
        DEBUG_STRING
      } TextLabelType;
      
      using Handle_t = u32;
      static const Handle_t INVALID_HANDLE;
      
      VizManager();
      
      // NOTE: Connect() will call Disconnect() first if already connected.
      Result Connect(const char *udp_host_address, const unsigned short port, const char* unity_host_address, const unsigned short unity_port);
      Result Disconnect();

      #if VIZ_ON_DEVICE
      void SetMessagePort(GameMessagePort* port) { _unityVizPort = port; }
      #endif
      
      // Whether or not to display the viz objects
      void ShowObjects(bool show);
      
      
      // NOTE: This DrawRobot is completely different from the convenience
      // function below which is just a wrapper around DrawObject. This one
      // actually sets the pose of a CozmoBot model in the world providing
      // more detailed visualization capabilities.
      void DrawRobot(const u32 robotID,
                     const Pose3d &pose,
                     const f32 headAngle,
                     const f32 liftAngle);
      
      
      // ===== Convenience object draw functions for specific object types ====
      
      // These convenience functions basically call DrawObject() with the
      // appropriate objectTypeID as well as by offseting the objectID by
      // some base amount so that the caller need not be concerned with
      // making robot and block object IDs that don't collide with each other.
      // A "handle" (unique, internal ID) will be returned that can be
      // used later to reference the visualization, e.g. for a call
      // to EraseVizObject.
      
      Handle_t DrawRobot(const u32 robotID,
                         const Pose3d &pose,
                         const ColorRGBA& color = ::Anki::NamedColors::DEFAULT);
      
      Handle_t DrawCuboid(const u32 blockID,
                          const Point3f &size,
                          const Pose3d &pose,
                          const ColorRGBA& color = ::Anki::NamedColors::DEFAULT);
      
      Handle_t DrawPreDockPose(const u32 preDockPoseID,
                               const Pose3d &pose,
                               const ColorRGBA& color = ::Anki::NamedColors::DEFAULT);
      
      Handle_t DrawRamp(const u32 rampID,
                        const f32 platformLength,
                        const f32 slopeLength,
                        const f32 width,
                        const f32 height,
                        const Pose3d& pose,
                        const ColorRGBA& color = ::Anki::NamedColors::DEFAULT);
      
      Handle_t DrawCharger(const u32 chargerID,
                           const f32 platformLength,
                           const f32 slopeLength,
                           const f32 width,
                           const f32 height,
                           const Pose3d& pose,
                           const ColorRGBA& color = ::Anki::NamedColors::DEFAULT);
      
      Handle_t DrawHumanHead(const s32 headID,
                             const Point3f& size,
                             const Pose3d& pose,
                             const ColorRGBA& color = ::Anki::NamedColors::DEFAULT);
      
      void DrawCameraFace(const Vision::TrackedFace& face,
                          const ColorRGBA& color);
      
      //void DrawRamp();
      
      
      void EraseRobot(const u32 robotID);
      void EraseCuboid(const u32 blockID);
      void EraseAllCuboids();
      void ErasePreDockPose(const u32 preDockPoseID);
      
      
      // ===== Static object draw functions ====
      
      // Sets the id objectID to correspond to a drawable object of
      // type objectTypeID (See VizObjectTypes) located at the specified pose.
      // For parameterized types, like VIZ_CUBOID, size determines the dimensions
      // of the object. For other types, like VIZ_ROBOT, size is ignored.
      // Up to 4 other parameters can be specified in an array pointed to
      // by params.
      void DrawObject(const u32 objectID, VizObjectType objectTypeID,
        const Point3f &size,
        const Pose3d &pose,
        const ColorRGBA& color = ::Anki::NamedColors::DEFAULT,
        const f32* params = nullptr);
      
      // Erases the object corresponding to the objectID
      void EraseVizObject(const Handle_t objectID);
      
      // Erases all objects. (Not paths)
      void EraseAllVizObjects();
      
      // Erase all objects of a certain type
      void EraseVizObjectType(const VizObjectType type);
      
      
      // ===== Path draw functions ====
      
      void DrawPath(const u32 pathID,
                    const Planning::Path& p,
                    const ColorRGBA& color = ::Anki::NamedColors::DEFAULT);
      
      // Appends the specified line segment to the path with id pathID
      void AppendPathSegmentLine(const u32 pathID,
                                 const f32 x_start_mm, const f32 y_start_mm,
                                 const f32 x_end_mm, const f32 y_end_m);

      // Appends the specified arc segment to the path with id pathID
      void AppendPathSegmentArc(const u32 pathID,
                                const f32 x_center_mm, const f32 y_center_mm,
                                const f32 radius_mm, const f32 startRad, const f32 sweepRad);
      
      // Sets the color of the path to the one corresponding to colorID
      void SetPathColor(const u32 pathID, const ColorRGBA& color);
      
      //void ShowPath(u32 pathID, bool show);
      
      //void SetPathHeightOffset(f32 m);

      // Erases the path corresponding to pathID
      void ErasePath(const u32 pathID);
      
      // Erases all paths
      void EraseAllPaths();
      
      // ==== Quad functions =====
    
      
      // Draws a generic 3D quadrilateral
      template<typename T>
      void DrawGenericQuad(const u32 quadID,
                           const Quadrilateral<3,T>& quad,
                           const ColorRGBA& color);

      // Draws a generic 2D quadrilateral in the XY plane at the specified Z height
      template<typename T>
      void DrawGenericQuad(const u32 quadID,
                           const Quadrilateral<2,T>& quad,
                           const T zHeight,
                           const ColorRGBA& color);
      
      // Draw a generic 2D quad in the camera display
      // TopColor is the color of the line connecting the upper left and upper right corners.
      template<typename T>
      void DrawCameraQuad(const Quadrilateral<2,T>& quad,
                          const ColorRGBA& color);
      template<typename T>
      void DrawCameraQuad(const Quadrilateral<2,T>& quad,
                          const ColorRGBA& color,
                          const ColorRGBA& topColor);
      
      // Draw a rectangle in the camera display
      template<typename T>
      void DrawCameraRect(const Rectangle<T>& rect, const ColorRGBA& color, bool filled = false);
      
      // Draw a line segment in the camera display
      void DrawCameraLine(const Point2f& start,
                          const Point2f& end,
                          const ColorRGBA& color);
      
      // Draw an oval in the camera display
      void DrawCameraOval(const Point2f& center,
                          float xRadius, float yRadius,
                          const ColorRGBA& color);
      
      // Draw text in the camera display
      void DrawCameraText(const Point2f& position,
                          const std::string& text,
                          const ColorRGBA& color);
      
      template<typename T>
      void DrawMatMarker(const u32 quadID,
                         const Quadrilateral<3,T>& quad,
                         const ColorRGBA& color);
      
      template<typename T>
      void DrawRobotBoundingBox(const u32 quadID,
                                const Quadrilateral<3,T>& quad,
                                const ColorRGBA& color);
      
      template<typename T>
      void DrawPlannerObstacle(const bool isReplan,
                               const u32 quadID,
                               const Polygon<2,T>& poly,
                               const ColorRGBA& color);

      void DrawPlannerObstacle(const bool isReplan,
                               const u32 quadID,
                               const FastPolygon& poly,
                               const ColorRGBA& color);

      template<typename T>
      void DrawPoseMarker(const u32 quadID,
                          const Quadrilateral<2,T>& quad,
                          const ColorRGBA& color);
      
      // Draw quads of a specified type (usually called as a helper by the
      // above methods for specific types)
      template<typename T>
      void DrawQuad(const VizQuadType quadType, const u32 quadID, const Quadrilateral<2,T>& quad,
        const T zHeight_mm, const ColorRGBA& color);

      template<typename T>
      void DrawQuad(const VizQuadType quadType, const u32 quadID, const Quadrilateral<3,T>& quad,
        const ColorRGBA& color);

      template<typename T>
      void DrawPoly(const u32 polyID,
                    const Polygon<2,T>& poly,
                    const ColorRGBA& color);

      void DrawPoly(const u32 polyID,
                    const FastPolygon& poly,
                    const ColorRGBA& color);

      // ==== Erase functions =====
      
      void ErasePoly(u32 polyID);
      
      // Erases the quad with the specified type and ID
      void EraseQuad(const uint32_t quadType, const u32 quadID);
      
      // Erases all the quads fo the specified type
      void EraseAllQuadsWithType(const uint32_t quadType);
      
      // Erases all quads
      void EraseAllQuads();
      
      void EraseAllPlannerObstacles(const bool isReplan);
      
      void EraseAllMatMarkers();

      // ==== Draw functions without identifier =====
      // This supports sending requests to draw primitives without requiring to assign a single ID to every
      // one of them, but a group. Used for debugging purposes where the underlaying geometry is not directly
      // related to a given object
      
      template <typename T>
      void DrawSegment(const std::string& identifier,
        const Point<3,T>& from, const Point<3,T>& to, const ColorRGBA& color, bool clearPrevious, float zOffset=0.0f);
      void EraseSegments(const std::string& identifier);
      
      // vector of simple quads (note a simple quad is an axis aligned quad with a color)
      using SimpleQuadVector = std::vector<VizInterface::SimpleQuad>;
      void DrawQuadVector(const std::string& identifier, const SimpleQuadVector& quads);
      void EraseQuadVector(const std::string& identifier);
      
      // circle as segments
      template <typename T>
      void DrawXYCircleAsSegments(const std::string& identifier, const Point<3, T>& center, const T radius, const ColorRGBA& color,
        bool clearPrevious, u32 numSegments=8, const T startAngle=T());
      
      // non-axis aligned quads as 4 segments
      template <typename T>
      void DrawQuadAsSegments(const std::string& identifier, const Quadrilateral<2, T>& quad, T z, const ColorRGBA& color, bool clearPrevious);
      template <typename T>
      void DrawQuadAsSegments(const std::string& identifier, const Quadrilateral<3, T>& quad, const ColorRGBA& color, bool clearPrevious);
      
      // helper to create SimpleQuads from Color and coordinates/size in millimeters. Note SimpleQuad uses floats
      template <typename T>
      static VizInterface::SimpleQuad MakeSimpleQuad(const ColorRGBA& color, const Point<3, T>& centerMM, T sideSizeMM);
      
      // ==== Circle functions =====
      template<typename T>
      void DrawXYCircle(u32 polyID,
                      const ColorRGBA& color,
                      const Point<2, T>& center,
                      const T radius,
                      u32 numSegments = 20);
      
      void EraseCircle(u32 polyID);
    
      // ==== Text functions =====
      void SetText(const TextLabelType& labelType, const ColorRGBA& color, const char* format, ...);
      
      
      // ==== Color functions =====
      /*
      // Sets the index colorID to correspond to the specified color vector
      void DefineColor(const u32 colorID,
                       const f32 red, const f32 green, const f32 blue,
                       const f32 alpha);
      */
      //void ClearAllColors();

        
      // ==== Misc. Debug functions =====
      void SetDockingError(const f32 x_dist, const f32 y_dist, const f32 z_dist, const f32 angle);
      
      void SendCameraInfo(const u16 exposure_ms, const f32 gain);

      void EnableImageSend(bool tf) { _sendImages = tf; }
      /*
      void SendGreyImage(const RobotID_t robotID, const u8* data, const Vision::CameraResolution res, const TimeStamp_t timestamp);
      void SendColorImage(const RobotID_t robotID, const u8* data, const Vision::CameraResolution res, const TimeStamp_t timestamp);

      void SendImage(const RobotID_t robotID, const u8* data, const u32 dataLength,
                     const Vision::CameraResolution res,
                     const TimeStamp_t timestamp,
                     const Vision::ImageEncoding_t encoding);
      */

      void SendImageChunk(const RobotID_t robotID, const ImageChunk& robotImageChunk);
      
      void SendTrackerQuad(const u16 topLeft_x, const u16 topLeft_y,
                           const u16 topRight_x, const u16 topRight_y,
                           const u16 bottomRight_x, const u16 bottomRight_y,
                           const u16 bottomLeft_x, const u16 bottomLeft_y);
      
      void SendRobotState(const RobotState &msg,
                          const s32 numAnimBytesFree,
                          const s32 numAnimAudioFramesFree,
                          const u8 videoFrameRateHz,
                          const u8 imageProcFrameRateHz,
                          const u8 enabledAnimTracks,
                          const u8 animTag);
      
      void SetOrigin(const SetVizOrigin& msg);
      
      void SubscribeToEngineEvents(IExternalInterface& externalInterface);
      
      // Declaration for message handling specializations. See AnkiEventUtil.h
      template <typename T>
      void HandleMessage(const T& msg);
      
      void SendRobotMood(VizInterface::RobotMood&& robotMood);
      void SendRobotBehaviorSelectData(VizInterface::RobotBehaviorSelectData&& robotBehaviorSelectData);
      void SendNewBehaviorSelected(VizInterface::NewBehaviorSelected&& newBehaviorSelected);
      void SendStartRobotUpdate();
      void SendEndRobotUpdate();
      void SendSaveImages(ImageSendMode mode, std::string path = "");
      void SendSaveState(bool enabled, std::string path = "");

      
      // ============= ActiveObjectInfo ===========
      void SendObjectConnectionState(u32 activeID, ObjectType type, bool connected);
      void SendObjectMovingState(u32 activeID, bool moving);
      void SendObjectUpAxisState(u32 activeID, UpAxis upAxis);
      void SendObjectAccelState(u32 activeID, const ActiveAccel& accel);
      
    protected:
      
      void SendMessage(const VizInterface::MessageViz& message);

      bool               _isInitialized;
      UdpClient          _vizClient;
      #if VIZ_ON_DEVICE
      GameMessagePort*   _unityVizPort = nullptr;
      #else
      UdpClient          _unityVizClient;
      #endif
      

      /*
      // Image sending
      std::map<RobotID_t, u8> _imgID;
      */

      bool               _sendImages;
      
      // Stores the maximum ID permitted for a given VizObject type
      u32 _VizObjectMaxID[(int)VizObjectType::NUM_VIZ_OBJECT_TYPES];
      
      // TODO: Won't need this offest once Polygon is implmeneted correctly (not drawing with path)
      const u32 _polyIDOffset = 2200;
      
      // For handling messages:
      std::vector<Signal::SmartHandle> _eventHandlers;
    }; // class VizManager
    
    
    template<typename T>
    void VizManager::DrawQuad(const VizQuadType quadType, const u32 quadID, const Quadrilateral<2,T>& quad,
      const T zHeight_mm, const ColorRGBA& color)
    {
      using namespace Quad;
      VizInterface::Quad v;
      v.quadType = quadType;
      v.quadID = quadID;
      
      const f32 zHeight_m = (float)MM_TO_M(static_cast<float>(zHeight_mm));
      
      v.xUpperLeft  = (float)MM_TO_M(static_cast<float>(quad[TopLeft].x()));
      v.yUpperLeft  = (float)MM_TO_M(static_cast<float>(quad[TopLeft].y()));
      v.zUpperLeft  = zHeight_m;
      
      v.xLowerLeft  = (float)MM_TO_M(static_cast<float>(quad[BottomLeft].x()));
      v.yLowerLeft  = (float)MM_TO_M(static_cast<float>(quad[BottomLeft].y()));
      v.zLowerLeft  = zHeight_m;
      
      v.xUpperRight = (float)MM_TO_M(static_cast<float>(quad[TopRight].x()));
      v.yUpperRight = (float)MM_TO_M(static_cast<float>(quad[TopRight].y()));
      v.zUpperRight = zHeight_m;
      
      v.xLowerRight = (float)MM_TO_M(static_cast<float>(quad[BottomRight].x()));
      v.yLowerRight = (float)MM_TO_M(static_cast<float>(quad[BottomRight].y()));
      v.zLowerRight = zHeight_m;
      v.color = (uint32_t)color;
      SendMessage(VizInterface::MessageViz(std::move(v)));
    }

    template<typename T>
    void VizManager::DrawQuad(const VizQuadType quadType, const u32 quadID, const Quadrilateral<3,T>& quad,
      const ColorRGBA& color)
    {
      using namespace Quad;
      VizInterface::Quad v;
      v.quadType = quadType;
      v.quadID = quadID;
      
      v.xUpperLeft  = (float)MM_TO_M(static_cast<float>(quad[TopLeft].x()));
      v.yUpperLeft  = (float)MM_TO_M(static_cast<float>(quad[TopLeft].y()));
      v.zUpperLeft  = (float)MM_TO_M(static_cast<float>(quad[TopLeft].z()));
      
      v.xLowerLeft  = (float)MM_TO_M(static_cast<float>(quad[BottomLeft].x()));
      v.yLowerLeft  = (float)MM_TO_M(static_cast<float>(quad[BottomLeft].y()));
      v.zLowerLeft  = (float)MM_TO_M(static_cast<float>(quad[BottomLeft].z()));
      
      v.xUpperRight = (float)MM_TO_M(static_cast<float>(quad[TopRight].x()));
      v.yUpperRight = (float)MM_TO_M(static_cast<float>(quad[TopRight].y()));
      v.zUpperRight = (float)MM_TO_M(static_cast<float>(quad[TopRight].z()));
      
      v.xLowerRight = (float)MM_TO_M(static_cast<float>(quad[BottomRight].x()));
      v.yLowerRight = (float)MM_TO_M(static_cast<float>(quad[BottomRight].y()));
      v.zLowerRight = (float)MM_TO_M(static_cast<float>(quad[BottomRight].z()));
      v.color = (uint32_t)color;
      SendMessage(VizInterface::MessageViz(std::move(v)));
    }
    
    template<typename T>
    void VizManager::DrawPoly(const u32 __polyID,
                              const Polygon<2,T>& poly,
                              const ColorRGBA& color)
    {
      // we don't have a poly viz message (yet...) so construct a path
      // from the poly, and use the viz path stuff instead

      Planning::Path polyPath;

      // hack! don't want to collide with path ids
      u32 pathId = __polyID + _polyIDOffset;

      size_t numPts = poly.size();

      for(size_t i=0; i<numPts; ++i) {
        size_t j = (i + 1) % numPts;
        polyPath.AppendLine(0,
                            poly[i].x(), poly[i].y(),
                            poly[j].x(), poly[j].y(),
                            1.0, 1.0, 1.0);
      }

      DrawPath(pathId, polyPath, color);
    }
    
    template<typename T>
    void VizManager::DrawGenericQuad(const u32 quadID,
                                     const Quadrilateral<2,T>& quad,
                                     const T zHeight_mm,
                                     const ColorRGBA& color)
    {
      DrawQuad(VizQuadType::VIZ_QUAD_GENERIC_2D, quadID, quad, zHeight_mm, color);
    }
    
    template<typename T>
    void VizManager::DrawGenericQuad(const u32 quadID,
                                     const Quadrilateral<3,T>& quad,
                                     const ColorRGBA& color)
    {
      DrawQuad(VizQuadType::VIZ_QUAD_GENERIC_3D, quadID, quad, color);
    }
    
    template<typename T>
    inline void VizManager::DrawCameraQuad(const Quadrilateral<2,T>& quad,
                                           const ColorRGBA& color)
    {
      DrawCameraQuad(quad, color, color);
    }
    
    template<typename T>
    void VizManager::DrawCameraQuad(const Quadrilateral<2,T>& quad,
                                    const ColorRGBA& color,
                                    const ColorRGBA& topColor)
    {
      using namespace Quad;
      VizInterface::CameraQuad v;
      
      v.xUpperLeft  = static_cast<float>(quad[TopLeft].x());
      v.yUpperLeft  = static_cast<float>(quad[TopLeft].y());
      
      v.xLowerLeft  = static_cast<float>(quad[BottomLeft].x());
      v.yLowerLeft  = static_cast<float>(quad[BottomLeft].y());
      
      v.xUpperRight = static_cast<float>(quad[TopRight].x());
      v.yUpperRight = static_cast<float>(quad[TopRight].y());
      
      v.xLowerRight = static_cast<float>(quad[BottomRight].x());
      v.yLowerRight = static_cast<float>(quad[BottomRight].y());
      v.color = (uint32_t)color;
      v.topColor = (uint32_t)topColor;
      SendMessage(VizInterface::MessageViz(std::move(v)));
    }
    
    template<typename T>
    void VizManager::DrawCameraRect(const Rectangle<T>& rect, const ColorRGBA& color, bool filled)
    {
      VizInterface::CameraRect msg{
        (uint32_t)color,
        static_cast<float>(rect.GetX()),
        static_cast<float>(rect.GetY()),
        static_cast<float>(rect.GetWidth()),
        static_cast<float>(rect.GetHeight()),
        filled,
      };
      SendMessage(VizInterface::MessageViz(std::move(msg)));
    }
    
    template<typename T>
    void VizManager::DrawMatMarker(const u32 quadID,
                                   const Quadrilateral<3,T>& quad,
                                   const ColorRGBA& color)
    {
      DrawQuad(VizQuadType::VIZ_QUAD_MAT_MARKER, quadID, quad, color);
    }
    
    template<typename T>
    void VizManager::DrawPlannerObstacle(const bool isReplan,
                                         const u32 polyID,
                                         const Polygon<2,T>& poly,
                                         const ColorRGBA& color)
    {
      // const u32 polyType = (isReplan ? VIZ_QUAD_PLANNER_OBSTACLE_REPLAN : VIZ_QUAD_PLANNER_OBSTACLE);
      
      DrawPoly(polyID, poly, color);
    }

    
    template<typename T>
    void VizManager::DrawRobotBoundingBox(const u32 quadID,
                                          const Quadrilateral<3,T>& quad,
                                          const ColorRGBA& color)
    {
      DrawQuad(VizQuadType::VIZ_QUAD_ROBOT_BOUNDING_BOX, quadID, quad, color);
    }

    template<typename T>
    void VizManager::DrawPoseMarker(const u32 quadID,
                                    const Quadrilateral<2,T>& quad,
                                    const ColorRGBA& color)
    {
      DrawQuad(VizQuadType::VIZ_QUAD_POSE_MARKER, quadID, quad, 0.5f, color);
    }
    
    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 
    template <typename T>
    void VizManager::DrawSegment(const std::string& identifier,
      const Point<3,T>& from, const Point<3,T>& to, const ColorRGBA& color, bool clearPrevious, float zOffset)
    {
      SendMessage(VizInterface::MessageViz(VizInterface::SegmentPrimitive
        {identifier,
         color.AsRGBA(),
         { {Anki::Util::numeric_cast<float>(MM_TO_M(from.x())),
            Anki::Util::numeric_cast<float>(MM_TO_M(from.y())),
            Anki::Util::numeric_cast<float>(MM_TO_M(from.z()+zOffset))}
         },
         { {Anki::Util::numeric_cast<float>(MM_TO_M(to.x())),
            Anki::Util::numeric_cast<float>(MM_TO_M(to.y())),
            Anki::Util::numeric_cast<float>(MM_TO_M(to.z()+zOffset))}
         },
         clearPrevious
        })
      );
    }
    
    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    template <typename T>
    void VizManager::DrawXYCircleAsSegments(const std::string& identifier, const Point<3, T>& center, const T radius,
                                const ColorRGBA& color, bool clearPrevious, u32 numSegments, const T startAngle)
    {
      // Note we create the polygon clockwise intentionally
      T anglePerSegment = static_cast<T>(-2) * static_cast<T>(M_PI) / static_cast<T>(numSegments);
      
      // Use the tangential and radial factors to draw the segments without recalculating every time.
      // Algorithm found here: http://slabode.exofire.net/circle_draw.shtml
      T tangentialFactor = std::tan(anglePerSegment);
      T radialFactor = std::cos(anglePerSegment);
      
      // Start at angle specified
      T newX = radius;
      T newY = startAngle;

      for (u32 i=0; i<numSegments; ++i)
      {
        T prevX = newX;
        T prevY = newY;
        
        T tx = -newY;
        T ty = newX;
        
        newX += tx * tangentialFactor;
        newY += ty * tangentialFactor;
        
        newX *= radialFactor;
        newY *= radialFactor;
        
        Point<3,T> prevPoint(prevX + center.x(), prevY + center.y(), center.z());
        Point<3,T> newPoint(newX + center.x(), newY + center.y(), center.z());
        DrawSegment(identifier, prevPoint, newPoint, color, (i==0)&&(clearPrevious));
      }
    }
    
    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    template <typename T>
    void VizManager::DrawQuadAsSegments(const std::string& identifier, const Quadrilateral<2, T>& quad, T z, const ColorRGBA& color, bool clearPrevious)
    {
      const Point<3,T> topLeft = {quad[Quad::CornerName::TopLeft].x(), quad[Quad::CornerName::TopLeft].y(), z};
      const Point<3,T> topRight = {quad[Quad::CornerName::TopRight].x(), quad[Quad::CornerName::TopRight].y(), z};
      const Point<3,T> bottomLeft = {quad[Quad::CornerName::BottomLeft].x(), quad[Quad::CornerName::BottomLeft].y(), z};
      const Point<3,T> bottomRight = {quad[Quad::CornerName::BottomRight].x(), quad[Quad::CornerName::BottomRight].y(), z};
      DrawSegment(identifier, topLeft, topRight, color, clearPrevious);
      DrawSegment(identifier, topRight, bottomRight, color, false);
      DrawSegment(identifier, bottomRight, bottomLeft, color, false);
      DrawSegment(identifier, bottomLeft, topLeft, color, false);
    }
    
    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    template <typename T>
    void VizManager::DrawQuadAsSegments(const std::string& identifier, const Quadrilateral<3, T>& quad, const ColorRGBA& color, bool clearPrevious)
    {
      const Point<3,T>& topLeft = quad[Quad::CornerName::TopLeft];
      const Point<3,T>& topRight = quad[Quad::CornerName::TopRight];
      const Point<3,T>& bottomLeft = quad[Quad::CornerName::BottomLeft];
      const Point<3,T>& bottomRight = quad[Quad::CornerName::BottomRight];
      DrawSegment(identifier, topLeft, topRight, color, clearPrevious);
      DrawSegment(identifier, topRight, bottomRight, color, false);
      DrawSegment(identifier, bottomRight, bottomLeft, color, false);
      DrawSegment(identifier, bottomLeft, topLeft, color, false);
    }

    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    template <typename T>
    VizInterface::SimpleQuad VizManager::MakeSimpleQuad(const ColorRGBA& color, const Point<3, T>& centerMM, T sideSizeMM)
    {
      VizInterface::SimpleQuad ret;
      ret.color = color.AsRGBA();
      ret.sideSize = Anki::Util::numeric_cast<float>(MM_TO_M(sideSizeMM));;
      ret.center[0] = Anki::Util::numeric_cast<float>(MM_TO_M(centerMM[0]));
      ret.center[1] = Anki::Util::numeric_cast<float>(MM_TO_M(centerMM[1]));
      ret.center[2] = Anki::Util::numeric_cast<float>(MM_TO_M(centerMM[2]));
      return ret;
    }
    
    template <typename T>
    void VizManager::DrawXYCircle(u32 polyID,
                                const ColorRGBA& color,
                                const Point<2, T>& center,
                                const T radius,
                                u32 numSegments)
    {
      // Note we create the polygon clockwise intentionally
      T anglePerSegment = static_cast<T>(-2) * static_cast<T>(M_PI) / static_cast<T>(numSegments);
      
      // Use the tangential and radial factors to draw the segments without recalculating every time.
      // Algorithm found here: http://slabode.exofire.net/circle_draw.shtml
      T tangentialFactor = std::tan(anglePerSegment);
      T radialFactor = std::cos(anglePerSegment);
      
      // Start at angle 0
      T newX = radius;
      T newY = 0;
      
      Polygon<2, T> newCircle;
      for (u32 i=0; i<numSegments; ++i)
      {
        newCircle.push_back(Point<2,T>(newX + center.x(), newY + center.y()));
        
        T tx = -newY;
        T ty = newX;
        
        newX += tx * tangentialFactor;
        newY += ty * tangentialFactor;
        
        newX *= radialFactor;
        newY *= radialFactor;
      }
      DrawPoly(polyID, newCircle, color);
    }
  } // namespace Cozmo
} // namespace Anki


#endif // VIZ_MANAGER_H

/**
 * File: vizManager.cpp
 *
 * Author: Kevin Yoon
 * Date:   2/5/2014
 *
 * Description: Implements the singleton VizManager object. See
 *              corresponding header for more detail.
 *
 * Copyright: Anki, Inc. 2014
 **/

#include "vizManager.h"
#include "anki/cozmo/robot/cozmoConfig.h"
#include "anki/common/basestation/general.h"
#include "anki/common/basestation/exceptions.h"
#include "anki/common/basestation/math/point_impl.h"

namespace Anki {
  namespace Cozmo {
    
    VizManager* VizManager::singletonInstance_ = 0;
    
    Result VizManager::Init()
    {
      
      if (!vizClient_.Connect(ROBOT_SIM_WORLD_HOST, VIZ_SERVER_PORT)) {
        PRINT_INFO("Failed to init VizManager client (%s:%d)\n", ROBOT_SIM_WORLD_HOST, VIZ_SERVER_PORT);
        isInitialized_ = false;
      }
    
      // Define colors
      DefineColor(VIZ_COLOR_EXECUTED_PATH,              1.0, 0.0, 0.0, 1.0);
      DefineColor(VIZ_COLOR_PREDOCKPOSE,                1.0, 0.0, 0.0, 0.75);
      DefineColor(VIZ_COLOR_SELECTED_OBJECT,            0.0, 1.0, 0.0, 0.0);
      DefineColor(VIZ_COLOR_BLOCK_BOUNDING_QUAD,        0.0, 0.0, 1.0, 0.75);
      DefineColor(VIZ_COLOR_OBSERVED_QUAD,              1.0, 0.0, 0.0, 0.75);
      DefineColor(VIZ_COLOR_ROBOT_BOUNDING_QUAD,        0.0, 0.8, 0.0, 0.75);
      DefineColor(VIZ_COLOR_REPLAN_BLOCK_BOUNDING_QUAD, 1.0, 0.1, 1.0, 0.75);
      
      DefineColor(VIZ_COLOR_RED,       1.0, 0.0, 0.0, 1.0);
      DefineColor(VIZ_COLOR_GREEN,     0.0, 1.0, 0.0, 1.0);
      DefineColor(VIZ_COLOR_BLUE,      0.0, 0.0, 1.0, 1.0);
      DefineColor(VIZ_COLOR_YELLOW,    1.0, 1.0, 0.0, 1.0);
      DefineColor(VIZ_COLOR_DARKGRAY,  0.3, 0.3, 0.3, 1.0);
      DefineColor(VIZ_COLOR_DARKGREEN, 0.0, 0.5, 0.0, 1.0);
      DefineColor(VIZ_COLOR_ORANGE,    1.0, 0.5, 0.0, 1.0);
      DefineColor(VIZ_COLOR_OFFWHITE,  0.9, 0.8, 0.8, 1.0);
      
      isInitialized_ = true;
      
      return isInitialized_ ? RESULT_OK : RESULT_FAIL;
    }
    
    VizManager::VizManager()
    {
      // Compute the max IDs permitted by VizObject type
      for (u32 i=0; i<NUM_VIZ_OBJECT_TYPES; ++i) {
        VizObjectMaxID[i] = VizObjectBaseID[i+1] - VizObjectBaseID[i];
      }
      
      isInitialized_ = false;
      imgID = 0;
    }

    void VizManager::SendMessage(u8 vizMsgID, void* msg)
    {
      //printf("Sending viz msg %d with %d bytes\n", vizMsgID, msgSize + 1);
      
      // TODO: Does this work for poorly packed structs?  Just use Andrew's message class creator?
      u32 msgSize = Anki::Cozmo::VizMsgLookupTable_[vizMsgID].size;
      
      sendBuf[0] = vizMsgID;
      memcpy(sendBuf + 1, msg, msgSize);
      if (vizClient_.Send(sendBuf, msgSize+1) <= 0) {
        printf("Send msg %d of size %d failed\n", vizMsgID, msgSize+1);
      }
    }

    
    void VizManager::ShowObjects(bool show)
    {
      VizShowObjects v;
      v.show = show ? 1 : 0;
      
      SendMessage(GET_MESSAGE_ID(VizShowObjects), &v);
    }
    
    
    // ===== Robot drawing function =======
    
    void VizManager::DrawRobot(const u32 robotID,
                               const Pose3d &pose,
                               const f32 headAngle,
                               const f32 liftAngle)
    {
      VizSetRobot v;
      
      v.robotID = robotID;
      
      v.x_trans_m = MM_TO_M(pose.GetTranslation().x());
      v.y_trans_m = MM_TO_M(pose.GetTranslation().y());
      v.z_trans_m = MM_TO_M(pose.GetTranslation().z());
      
      v.rot_rad = pose.GetRotationAngle().ToFloat();
      v.rot_axis_x = pose.GetRotationAxis().x();
      v.rot_axis_y = pose.GetRotationAxis().y();
      v.rot_axis_z = pose.GetRotationAxis().z();

      v.head_angle = headAngle;
      v.lift_angle = liftAngle;
      
      SendMessage(GET_MESSAGE_ID(VizSetRobot), &v);
    }
    
    
    // ===== Convenience object draw functions for specific object types ====
    
    void VizManager::DrawRobot(const u32 robotID,
                               const Pose3d &pose,
                               const u32 colorID)
    {
      CORETECH_ASSERT(robotID < VizObjectMaxID[VIZ_OBJECT_ROBOT]);
      
      Anki::Point3f dims; // junk
      DrawObject(VizObjectBaseID[VIZ_OBJECT_ROBOT] + robotID,
                 VIZ_OBJECT_ROBOT,
                 dims,
                 pose,
                 colorID);
    }
    
    void VizManager::DrawCuboid(const u32 blockID,
                                const Point3f &size,
                                const Pose3d &pose,
                                const u32 colorID)
    {
      CORETECH_ASSERT(blockID < VizObjectMaxID[VIZ_OBJECT_CUBOID]);
      
      DrawObject(VizObjectMaxID[VIZ_OBJECT_CUBOID] + blockID,
                 VIZ_OBJECT_CUBOID,
                 size,
                 pose,
                 colorID);
    }
    
    void VizManager::DrawPreDockPose(const u32 preDockPoseID,
                                     const Pose3d &pose,
                                     const u32 colorID)
    {
      CORETECH_ASSERT(preDockPoseID < VizObjectMaxID[VIZ_OBJECT_PREDOCKPOSE]);
      
      Anki::Point3f dims; // junk
      DrawObject(VizObjectMaxID[VIZ_OBJECT_PREDOCKPOSE] + preDockPoseID,
                 VIZ_OBJECT_PREDOCKPOSE,
                 dims,
                 pose,
                 colorID);
    }
    

    
    void VizManager::EraseRobot(const u32 robotID)
    {
      CORETECH_ASSERT(robotID < VizObjectMaxID[VIZ_OBJECT_ROBOT]);
      EraseVizObject(VizObjectBaseID[VIZ_OBJECT_ROBOT] + robotID);
    }
    
    void VizManager::EraseCuboid(const u32 blockID)
    {
      CORETECH_ASSERT(blockID < VizObjectMaxID[VIZ_OBJECT_CUBOID]);
      EraseVizObject(VizObjectMaxID[VIZ_OBJECT_CUBOID] + blockID);
    }

    void VizManager::EraseAllCuboids()
    {
      EraseVizObjectType(VIZ_OBJECT_CUBOID);
    }
    
    void VizManager::ErasePreDockPose(const u32 preDockPoseID)
    {
      CORETECH_ASSERT(preDockPoseID < VizObjectMaxID[VIZ_OBJECT_PREDOCKPOSE]);
      EraseVizObject(VizObjectBaseID[VIZ_OBJECT_PREDOCKPOSE] + preDockPoseID);
    }
    
    
    
    // ================== Object drawing methods ====================
    
    void VizManager::DrawObject(const u32 objectID,
                                const u32 objectTypeID,
                                const Anki::Point3f &size_mm,
                                const Anki::Pose3d &pose,
                                const u32 colorID)
    {
      VizObject v;
      v.objectID = objectID;
      v.objectTypeID = objectTypeID;
      
      v.x_size_m = MM_TO_M(size_mm.x());
      v.y_size_m = MM_TO_M(size_mm.y());
      v.z_size_m = MM_TO_M(size_mm.z());
      
      v.x_trans_m = MM_TO_M(pose.GetTranslation().x());
      v.y_trans_m = MM_TO_M(pose.GetTranslation().y());
      v.z_trans_m = MM_TO_M(pose.GetTranslation().z());
      
      
      // TODO: rotation...
      v.rot_deg = RAD_TO_DEG( pose.GetRotationAngle().ToFloat() );
      v.rot_axis_x = pose.GetRotationAxis().x();
      v.rot_axis_y = pose.GetRotationAxis().y();
      v.rot_axis_z = pose.GetRotationAxis().z();
      
      v.color = colorID;
      
      SendMessage( GET_MESSAGE_ID(VizObject), &v );
    }
    
    
    void VizManager::EraseVizObject(const u32 objectID)
    {
      VizEraseObject v;
      v.objectID = objectID;
      
      SendMessage( GET_MESSAGE_ID(VizEraseObject), &v );
    }
    

    void VizManager::EraseAllVizObjects()
    {
      VizEraseObject v;
      v.objectID = ALL_OBJECT_IDs;
      
      SendMessage( GET_MESSAGE_ID(VizEraseObject), &v );
    }
    
    void VizManager::EraseVizObjectType(const VizObjectType type)
    {
      VizEraseObject v;
      v.objectID = OBJECT_ID_RANGE;
      v.lower_bound_id = VizObjectBaseID[type];
      v.upper_bound_id = VizObjectBaseID[type+1]-1;
      
      SendMessage( GET_MESSAGE_ID(VizEraseObject), &v );
    }

    
    // ================== Path drawing methods ====================
    
    void VizManager::DrawPath(const u32 pathID,
                              const Planning::Path& p,
                              const u32 colorID)
    {
      ErasePath(pathID);
      printf("drawing path %u of length %hhu\n", pathID, p.GetNumSegments());
      
      for (int s=0; s < p.GetNumSegments(); ++s) {
        const Planning::PathSegmentDef& seg = p.GetSegmentConstRef(s).GetDef();
        switch(p.GetSegmentConstRef(s).GetType()) {
          case Planning::PST_LINE:
            AppendPathSegmentLine(pathID,
                                  seg.line.startPt_x, seg.line.startPt_y,
                                  seg.line.endPt_x, seg.line.endPt_y);
            break;
          case Planning::PST_ARC:
            AppendPathSegmentArc(pathID,
                                 seg.arc.centerPt_x, seg.arc.centerPt_y,
                                 seg.arc.radius,
                                 seg.arc.startRad,
                                 seg.arc.sweepRad);
            break;
          default:
            break;
        }
      }
      
      SetPathColor(pathID, colorID);
    }
    
    
    void VizManager::AppendPathSegmentLine(const u32 pathID,
                                           const f32 x_start_mm, const f32 y_start_mm,
                                           const f32 x_end_mm, const f32 y_end_mm)
    {
      VizAppendPathSegmentLine v;
      v.pathID = pathID;
      v.x_start_m = MM_TO_M(x_start_mm);
      v.y_start_m = MM_TO_M(y_start_mm);
      v.z_start_m = 0;
      v.x_end_m = MM_TO_M(x_end_mm);
      v.y_end_m = MM_TO_M(y_end_mm);
      v.z_end_m = 0;
      
      SendMessage( GET_MESSAGE_ID(VizAppendPathSegmentLine), &v );
    }
    
    void VizManager::AppendPathSegmentArc(const u32 pathID,
                                          const f32 x_center_mm, const f32 y_center_mm,
                                          const f32 radius_mm, const f32 startRad, const f32 sweepRad)
    {
      VizAppendPathSegmentArc v;
      v.pathID = pathID;
      v.x_center_m = MM_TO_M(x_center_mm);
      v.y_center_m = MM_TO_M(y_center_mm);
      v.radius_m = MM_TO_M(radius_mm);
      v. start_rad = startRad;
      v. sweep_rad = sweepRad;
      
      
      SendMessage( GET_MESSAGE_ID(VizAppendPathSegmentArc), &v );
    }
    

    void VizManager::ErasePath(const u32 pathID)
    {
      VizErasePath v;
      v.pathID = pathID;

      printf("viz: erasing path %u\n", pathID);
      
      SendMessage( GET_MESSAGE_ID(VizErasePath), &v );
    }

    void VizManager::EraseAllPaths()
    {
      VizErasePath v;
      v.pathID = ALL_PATH_IDs;

      printf("viz: erasing all paths\n");
      
      SendMessage( GET_MESSAGE_ID(VizErasePath), &v );
    }
    
    void VizManager::SetPathColor(const u32 pathID, const u32 colorID)
    {
      VizSetPathColor v;
      v.pathID = pathID;
      v.colorID = colorID;
      
      SendMessage( GET_MESSAGE_ID(VizSetPathColor), &v );
    }
    
    
    // =============== Quad methods ==================
    
    void VizManager::EraseQuad(const u32 quadType, const u32 quadID)
    {
      VizEraseQuad v;
      v.quadType = quadType;
      v.quadID = quadID;
      
      SendMessage( GET_MESSAGE_ID(VizEraseQuad), &v );
    }
    
    void VizManager::EraseAllQuadsWithType(const u32 quadType)
    {
      EraseQuad(quadType, ALL_QUAD_IDs);
    }
    
    void VizManager::EraseAllQuads()
    {
      EraseQuad(ALL_QUAD_TYPEs, ALL_QUAD_IDs);
    }
    
    void VizManager::EraseAllPlannerObstacles(const bool isReplan)
    {
      if(isReplan) {
        EraseAllQuadsWithType(VIZ_QUAD_PLANNER_OBSTACLE_REPLAN);
      } else {
        EraseAllQuadsWithType(VIZ_QUAD_PLANNER_OBSTACLE);
      }
    }
    
    void VizManager::EraseAllMatMarkers()
    {
      EraseAllQuadsWithType(VIZ_QUAD_MAT_MARKER);
    }

    
    // =============== Text methods ==================

    void VizManager::SetText(const u32 labelID, const u32 colorID, const char* format, ...)
    {
      VizSetLabel v;
      v.labelID = labelID;
      v.colorID = colorID;
      
      va_list argptr;
      va_start(argptr, format);
      vsnprintf((char*)v.text, sizeof(v.text), format, argptr);
      va_end(argptr);
      
      SendMessage( GET_MESSAGE_ID(VizSetLabel), &v );
    }
    
    
    // ================== Color methods ====================
    
    // Sets the index colorID to correspond to the specified color vector
    void VizManager::DefineColor(const u32 colorID,
                                 const f32 red, const f32 green, const f32 blue,
                                 const f32 alpha)
    {
      VizDefineColor v;
      v.colorID = colorID;
      v.r = red;
      v.g = green;
      v.b = blue;
      v.alpha = alpha;
      
      SendMessage( GET_MESSAGE_ID(VizDefineColor), &v );
    }
    
    
    // ============== Misc. Debug methods =================
    void VizManager::SetDockingError(const f32 x_dist, const f32 y_dist, const f32 angle)
    {
      VizDockingErrorSignal v;
      v.x_dist = x_dist;
      v.y_dist = y_dist;
      v.angle = angle;
      
      SendMessage( GET_MESSAGE_ID(VizDockingErrorSignal), &v );
    }
    

    void VizManager::SendGreyImage(const u8* data, const Vision::CameraResolution res)
    {
      VizImageChunk v;
      v.resolution = res;
      v.imgId = ++imgID;
      v.chunkId = 0;
      v.chunkSize = MAX_VIZ_IMAGE_CHUNK_SIZE;
      
      s32 bytesToSend = Vision::CameraResInfo[res].width * Vision::CameraResInfo[res].height;
      

      while (bytesToSend > 0) {
        if (bytesToSend < MAX_VIZ_IMAGE_CHUNK_SIZE) {
          v.chunkSize = bytesToSend;
        }
        bytesToSend -= v.chunkSize;

        //printf("Sending CAM image %d chunk %d (size: %d), bytesLeftToSend %d\n", v.imgId, v.chunkId, v.chunkSize, bytesToSend);
        memcpy(v.data, &data[v.chunkId * MAX_VIZ_IMAGE_CHUNK_SIZE], v.chunkSize);
        SendMessage( GET_MESSAGE_ID(VizImageChunk), &v );
        
        ++v.chunkId;
      }
    }

    void VizManager::SendTrackerQuad(const u16 topLeft_x, const u16 topLeft_y,
                                     const u16 topRight_x, const u16 topRight_y,
                                     const u16 bottomRight_x, const u16 bottomRight_y,
                                     const u16 bottomLeft_x, const u16 bottomLeft_y)
    {
      VizTrackerQuad v;
      v.topLeft_x = topLeft_x;
      v.topLeft_y = topLeft_y;
      v.topRight_x = topRight_x;
      v.topRight_y = topRight_y;
      v.bottomRight_x = bottomRight_x;
      v.bottomRight_y = bottomRight_y;
      v.bottomLeft_x = bottomLeft_x;
      v.bottomLeft_y = bottomLeft_y;
      
      SendMessage(GET_MESSAGE_ID(VizTrackerQuad), &v);
    }
    
    
  } // namespace Cozmo
} // namespace Anki

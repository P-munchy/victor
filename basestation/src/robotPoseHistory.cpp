//
//  robotPoseHistory.cpp
//  Products_Cozmo
//
//  Created by Kevin Yoon 2014-05-13
//  Copyright (c) 2014 Anki, Inc. All rights reserved.
//

#include "anki/cozmo/basestation/robotPoseHistory.h"
#include "anki/common/basestation/general.h"

#include "anki/common/basestation/math/point_impl.h"

#define DEBUG_ROBOT_POSE_HISTORY 0

namespace Anki {
  namespace Cozmo {

    //////////////////////// RobotPoseStamp /////////////////////////
    
    RobotPoseStamp::RobotPoseStamp()
    { }
    
    RobotPoseStamp::RobotPoseStamp(const PoseFrameID_t frameID,
                                   const f32 pose_x, const f32 pose_y, const f32 pose_z,
                                   const f32 pose_angle,
                                   const f32 head_angle)
    {
      SetPose(frameID, pose_x, pose_y, pose_z, pose_angle, head_angle);
    }

    RobotPoseStamp::RobotPoseStamp(const PoseFrameID_t frameID,
                                   const Pose3d& pose,
                                   const f32 head_angle)
    {
      SetPose(frameID, pose, head_angle);
    }
    

    void RobotPoseStamp::SetPose(const PoseFrameID_t frameID,
                                 const f32 pose_x, const f32 pose_y, const f32 pose_z,
                                 const f32 pose_angle,
                                 const f32 head_angle)
    {
      frame_ = frameID;
      
      pose_.set_rotation(pose_angle, Z_AXIS_3D);
      pose_.set_translation(Vec3f(pose_x, pose_y, pose_z));
      headAngle_ = head_angle;
    }
    
    void RobotPoseStamp::SetPose(const PoseFrameID_t frameID,
                                 const Pose3d& pose,
                                 const f32 head_angle)
    {
      frame_ = frameID;
      pose_ = pose;
      headAngle_ = head_angle;
    }
    
    void RobotPoseStamp::Print() const
    {
      printf("Frame %d, headAng %f, ", frame_, headAngle_);
      pose_.Print();
    }
    
    /////////////////////// RobotPoseHistory /////////////////////////////
    
    HistPoseKey RobotPoseHistory::currHistPoseKey_ = 0;
    
    RobotPoseHistory::RobotPoseHistory()
    : windowSize_(3000)
    {}

    void RobotPoseHistory::Clear()
    {
      poses_.clear();
      visPoses_.clear();
      computedPoses_.clear();
      
      tsByKeyMap_.clear();
      keyByTsMap_.clear();
    }
    
    void RobotPoseHistory::SetTimeWindow(const u32 windowSize_ms)
    {
      windowSize_ = windowSize_ms;
      CullToWindowSize();
    }
    
    
    Result RobotPoseHistory::AddRawOdomPose(const TimeStamp_t t,
                                     const RobotPoseStamp& p)
    {
      return AddRawOdomPose(t,
                            p.GetFrameId(),
                            p.GetPose().get_translation().x(),
                            p.GetPose().get_translation().y(),
                            p.GetPose().get_translation().z(),
                            p.GetPose().get_rotationMatrix().GetAngleAroundZaxis().ToFloat(),
                            p.GetHeadAngle());
    }


    // Adds a pose to the history
    Result RobotPoseHistory::AddRawOdomPose(const TimeStamp_t t,
                                            const PoseFrameID_t frameID,
                                            const f32 pose_x, const f32 pose_y, const f32 pose_z,
                                            const f32 pose_angle,
                                            const f32 head_angle)
    {
      // Should the pose be added?
      TimeStamp_t newestTime = poses_.rbegin()->first;
      if (newestTime > windowSize_ && t < newestTime - windowSize_) {
        return RESULT_FAIL;
      }
      
      std::pair<PoseMapIter_t, bool> res;
      res = poses_.emplace(std::piecewise_construct,
                           std::make_tuple(t),
                           std::make_tuple(frameID, pose_x, pose_y, pose_z, pose_angle, head_angle));
      
      if (!res.second) {
        PRINT_NAMED_WARNING("RobotPoseHistory.AddRawOdomPose.AddFailed", "Time: %d\n", t);
        return RESULT_FAIL;
      }

      CullToWindowSize();
      
      return RESULT_OK;
    }

    
    Result RobotPoseHistory::AddVisionOnlyPose(const TimeStamp_t t,
                                               const PoseFrameID_t frameID,
                                               const f32 pose_x, const f32 pose_y, const f32 pose_z,
                                               const f32 pose_angle,
                                               const f32 head_angle)
    {
      RobotPoseStamp p(frameID, pose_x, pose_y, pose_z, pose_angle, head_angle);
      return AddVisionOnlyPose(t, p);
    }
    
    Result RobotPoseHistory::AddVisionOnlyPose(const TimeStamp_t t,
                                               const RobotPoseStamp& p)
    {
      // Should the pose be added?
      // Check if the pose's timestamp is too old.
      if (!poses_.empty()) {
        TimeStamp_t newestTime = poses_.rbegin()->first;
        if (newestTime > windowSize_ && t < newestTime - windowSize_) {
          return RESULT_FAIL;
        }
      }
      
      // If visPose entry exist at t, then overwrite it
      PoseMapIter_t it = visPoses_.find(t);
      if (it != visPoses_.end()) {
        it->second.SetPose(p.GetFrameId(), p.GetPose(), p.GetHeadAngle());
      } else {
      
        std::pair<PoseMapIter_t, bool> res;
        res = visPoses_.emplace(std::piecewise_construct,
                                std::make_tuple(t),
                                std::make_tuple(p.GetFrameId(), p.GetPose(), p.GetHeadAngle()));
      
        if (!res.second) {
          return RESULT_FAIL;
        }
        
        CullToWindowSize();
      }
      
      return RESULT_OK;
    }

    
    
    
    // Sets p to the pose nearest the given timestamp t.
    // Interpolates pose if withInterpolation == true.
    // Returns OK if t is between the oldest and most recent timestamps stored.
    Result RobotPoseHistory::GetRawPoseAt(const TimeStamp_t t_request,
                                          TimeStamp_t& t, RobotPoseStamp& p,
                                          bool withInterpolation) const
    {
      // This pose occurs at or immediately after t_request
      const_PoseMapIter_t it = poses_.lower_bound(t_request);
      
      // Check if in range
      if (it == poses_.end() || t_request < poses_.begin()->first) {
        return RESULT_FAIL;
      }
      
      if (t_request == it->first) {
        // If the exact timestamp was found, return the corresponding pose.
        t = it->first;
        p = it->second;
      } else {

        // Get iterator to the pose just before t_request
        const_PoseMapIter_t prev_it = it;
        prev_it--;
        
        if (withInterpolation) {
          
          // Get the pose transform between the two poses.
          Pose3d pTransform = it->second.GetPose().getWithRespectTo(&(prev_it->second.GetPose()));
          
          // Compute scale factor between time to previous pose and time between previous pose and next pose.
          f32 timeScale = (f32)(t_request - prev_it->first) / (it->first - prev_it->first);
          
          // Compute scaled transform
          Vec3f interpTrans(prev_it->second.GetPose().get_translation());
          interpTrans += pTransform.get_translation() * timeScale;
          
          // NOTE: Assuming there is only z-axis rotation!
          // TODO: Make generic?
          Radians interpRotation = prev_it->second.GetPose().get_rotationAngle() + Radians(pTransform.get_rotationAngle() * timeScale);
          
          // Interp head angle
          f32 interpHeadAngle = prev_it->second.GetHeadAngle() + timeScale * (it->second.GetHeadAngle() - prev_it->second.GetHeadAngle());
        
          t = t_request;
          p.SetPose(prev_it->second.GetFrameId(), interpTrans.x(), interpTrans.y(), interpTrans.z(), interpRotation.ToFloat(), interpHeadAngle);
          
        } else {
          
          // Return the pose closest to the requested time
          if (it->first - t_request < t_request - prev_it->first) {
            t = it->first;
            p = it->second;
          } else {
            t = prev_it->first;
            p = prev_it->second;
          }
        }
      }
      
      return RESULT_OK;
    }

    Result RobotPoseHistory::GetVisionOnlyPoseAt(const TimeStamp_t t_request, RobotPoseStamp** p)
    {
      PoseMapIter_t it = visPoses_.find(t_request);
      if (it != visPoses_.end()) {
        *p = &(it->second);
        return RESULT_OK;
      }
      return RESULT_FAIL;
    }
    
    Result RobotPoseHistory::ComputePoseAt(const TimeStamp_t t_request,
                                           TimeStamp_t& t, RobotPoseStamp& p,
                                           bool withInterpolation) const
    {
      // If the vision-based version of the pose exists, return it.
      const_PoseMapIter_t it = visPoses_.find(t_request);
      if (it != visPoses_.end()) {
        t = t_request;
        p = it->second;
        return RESULT_OK;
      }
      
      // Get the raw pose at the requested timestamp
      RobotPoseStamp p1;
      if (GetRawPoseAt(t_request, t, p1, withInterpolation) == RESULT_FAIL) {
        return RESULT_FAIL;
      }
      
      // Now get the previous vision-based pose
      const_PoseMapIter_t git = visPoses_.lower_bound(t);
      
      // If there are no vision-based poses then return the raw pose that we just got
      if (git == visPoses_.end()) {
        if (visPoses_.empty()) {
          p = p1;
          return RESULT_OK;
        } else {
          --git;
        }
      } else if (git->first != t) {
        // As long as the vision-based pose is not from time t,
        // decrement the pointer to get the previous vision-based
        --git;
      }
      
      // Check frame ID
      // If the vision pose frame id <= requested frame id
      // then just return the raw pose of the requested frame id since it
      // is already based on the vision-based pose.
      if (git->second.GetFrameId() <= p1.GetFrameId()) {
        //printf("FRAME %d <= %d\n", git->second.GetFrameId(), p1.GetFrameId());
        p = p1;
        return RESULT_OK;
      }
      
      #if(DEBUG_ROBOT_POSE_HISTORY)
      static bool printDbg = false;
      if(printDbg) {
        printf("gt: %d\n", git->first);
        git->second.GetPose().Print();
      }
      #endif
      
      // git now points to the latest vision-based pose that exists before time t.
      // Now get the pose in poses_ that immediately follows the vision-based pose's time.
      const_PoseMapIter_t p0_it = poses_.lower_bound(git->first);

      #if(DEBUG_ROBOT_POSE_HISTORY)
      if (printDbg) {
        printf("p0_it: t: %d  frame: %d\n", p0_it->first, p0_it->second.GetFrameId());
        p0_it->second.GetPose().Print();
      
        printf("p1: t: %d  frame: %d\n", t, p1.GetFrameId());
        p1.GetPose().Print();
      }
      #endif
      
      CORETECH_ASSERT((p1.GetPose().get_parent() == Pose3d::World) &&
                      (p0_it->second.GetPose().get_parent() == Pose3d::World));
      
      // Compute relative pose between p0_it and p1 and append to the vision-based pose.
      // Need to account for intermediate frames between p0 and p1 if any.
      // pMid0 and pMid1 are used to denote the start and end poses of
      // every intermediate frame.
      Pose3d pTransform = p0_it->second.GetPose().getInverse();
      const_PoseMapIter_t pMid0 = p0_it;
      const_PoseMapIter_t pMid1 = p0_it;
      for (pMid1 = p0_it; pMid1->first != t; ++pMid1) {
        if (pMid1->second.GetFrameId() > pMid0->second.GetFrameId()) {
          
          #if(DEBUG_ROBOT_POSE_HISTORY)
          if (printDbg) {
            printf(" ComputePoseAt: frame %d (t=%d) to frame %d (t=%d)\n", pMid0->second.GetFrameId(), pMid0->first, pMid1->second.GetFrameId(), pMid1->first);
          }
          #endif
          
          // Point pMid1 to the last pose of the same frame as pMid0
          // and multiply with pTransform to get the transform for pMid0's frame.
          --pMid1;
          pTransform *= pMid1->second.GetPose();
          
          // Now point pMid0 and pMid1 to the first pose of the next frame
          // and multiply the inverse with pTransform to get the first part of the transform
          // for the next frame.
          ++pMid1;
          pTransform *= pMid1->second.GetPose().getInverse();
          
          pMid0 = pMid1;
        }
        if (pMid1->second.GetFrameId() == p1.GetFrameId()) {
          break;
        }
      }
      pTransform *= p1.GetPose();
      

      #if(DEBUG_ROBOT_POSE_HISTORY)
      if (printDbg) {
        printf("pTrans: %d\n", t);
        pTransform.Print();
      }
      #endif
      
      pTransform.preComposeWith(git->second.GetPose());
      p.SetPose(git->second.GetFrameId(), pTransform, p1.GetHeadAngle());
      
      return RESULT_OK;
    }
    
    Result RobotPoseHistory::ComputeAndInsertPoseAt(const TimeStamp_t t_request,
                                                    TimeStamp_t& t, RobotPoseStamp** p,
                                                    HistPoseKey* key,
                                                    bool withInterpolation)
    {
      RobotPoseStamp ps;
      //printf("COMPUTE+INSERT\n");
      if (ComputePoseAt(t_request, t, ps, withInterpolation) == RESULT_FAIL) {
        *p = nullptr;
        return RESULT_FAIL;
      }
      
      // If computedPose entry exist at t, then overwrite it
      PoseMapIter_t it = computedPoses_.find(t);
      if (it != computedPoses_.end()) {
        it->second.SetPose(ps.GetFrameId(), ps.GetPose(), ps.GetHeadAngle());
        *p = &(it->second);
        
        if (key) {
          *key = keyByTsMap_[t];
        }
      } else {
        
        std::pair<PoseMapIter_t, bool> res;
        res = computedPoses_.emplace(std::piecewise_construct,
                                     std::forward_as_tuple(t),
                                     std::forward_as_tuple(ps.GetFrameId(), ps.GetPose(), ps.GetHeadAngle()));
        
        if (!res.second) {
          return RESULT_FAIL;
        }
        
        *p = &(res.first->second);
        
        
        // Create key associated with computed pose
        ++currHistPoseKey_;
        tsByKeyMap_.emplace(std::piecewise_construct,
                            std::forward_as_tuple(currHistPoseKey_),
                            std::forward_as_tuple(t));
        keyByTsMap_.emplace(std::piecewise_construct,
                                  std::forward_as_tuple(t),
                                  std::forward_as_tuple(currHistPoseKey_));
        
        if (key) {
          *key = currHistPoseKey_;
        }

      }
      
      return RESULT_OK;
    }
    
    Result RobotPoseHistory::GetComputedPoseAt(const TimeStamp_t t_request, RobotPoseStamp** p, HistPoseKey* key)
    {
      PoseMapIter_t it = computedPoses_.find(t_request);
      if (it != computedPoses_.end()) {
        *p = &(it->second);
        
        // Get key for the computed pose
        if (key){
          KeyByTimestampMapIter_t kIt = keyByTsMap_.find(it->first);
          if (kIt == keyByTsMap_.end()) {
            PRINT_NAMED_WARNING("RobotPoseHistory.GetComputedPoseAt.KeyNotFound","");
            return RESULT_FAIL;
          }
          *key = kIt->second;
        }
        
        return RESULT_OK;
      }
      
      // TODO: Compute the pose if it doesn't exist already?
      // ...
      
      return RESULT_FAIL;
    }
    
    Result RobotPoseHistory::GetLatestVisionOnlyPose(TimeStamp_t& t, RobotPoseStamp& p) const
    {
      if (!visPoses_.empty()) {
        t = visPoses_.rbegin()->first;
        p = visPoses_.rbegin()->second;
        return RESULT_OK;
      }
      
      return RESULT_FAIL;
    }
    
    
    void RobotPoseHistory::CullToWindowSize()
    {
      if (poses_.size() > 1) {
        
        // Get the most recent timestamp
        TimeStamp_t mostRecentTime = poses_.rbegin()->first;
        
        // If most recent time is less than window size, we're done.
        if (mostRecentTime < windowSize_) {
          return;
        }
        
        // Get pointer to the oldest timestamp that may remain in the map
        TimeStamp_t oldestAllowedTime = mostRecentTime - windowSize_;
        const_PoseMapIter_t it = poses_.upper_bound(oldestAllowedTime);
        const_PoseMapIter_t git = visPoses_.upper_bound(oldestAllowedTime);
        const_PoseMapIter_t cit = computedPoses_.upper_bound(oldestAllowedTime);
        
        // Delete everything before the oldest allowed timestamp
        if (oldestAllowedTime > poses_.begin()->first) {
          poses_.erase(poses_.begin(), it);
        }
        if (oldestAllowedTime > visPoses_.begin()->first) {
          visPoses_.erase(visPoses_.begin(), git);
        }
        if (oldestAllowedTime > computedPoses_.begin()->first) {

          // For all computedPoses up until cit, remove their associated keys.
          for(PoseMapIter_t delIt = computedPoses_.begin(); delIt != cit; ++delIt) {
            KeyByTimestampMapIter_t kbtIt = keyByTsMap_.find(delIt->first);
            if (kbtIt == keyByTsMap_.end()) {
              PRINT_NAMED_WARNING("RobotPoseHistory.CullToWindowSize.KeyNotFound", "");
              break;
            }
            tsByKeyMap_.erase(kbtIt->second);
            keyByTsMap_.erase(kbtIt);
          }

          computedPoses_.erase(computedPoses_.begin(), cit);
        }
      }
    }
    
    bool RobotPoseHistory::IsValidPoseKey(const HistPoseKey key) const
    {
      return (tsByKeyMap_.find(key) != tsByKeyMap_.end());
    }
    
    TimeStamp_t RobotPoseHistory::GetOldestTimeStamp() const
    {
      return (poses_.empty() ? 0 : poses_.begin()->first);
    }
    
    TimeStamp_t RobotPoseHistory::GetNewestTimeStamp() const
    {
      return (poses_.empty() ? 0 : poses_.rbegin()->first);
    }
    
    void RobotPoseHistory::Print() const
    {
      // Create merged map of all poses
      std::multimap<TimeStamp_t, std::pair<std::string, const_PoseMapIter_t> > mergedPoses;
      std::multimap<TimeStamp_t, std::pair<std::string, const_PoseMapIter_t> >::iterator mergedIt;
      const_PoseMapIter_t pit;
      
      for(pit = poses_.begin(); pit != poses_.end(); ++pit) {
        mergedPoses.emplace(std::piecewise_construct,
                            std::forward_as_tuple(pit->first),
                            std::forward_as_tuple("  ", pit));
      }

      for(pit = visPoses_.begin(); pit != visPoses_.end(); ++pit) {
        mergedPoses.emplace(std::piecewise_construct,
                            std::forward_as_tuple(pit->first),
                            std::forward_as_tuple("v ", pit));
      }

      for(pit = computedPoses_.begin(); pit != computedPoses_.end(); ++pit) {
        mergedPoses.emplace(std::piecewise_construct,
                            std::forward_as_tuple(pit->first),
                            std::forward_as_tuple("c ", pit));
      }
      
      
      printf("\nRobotPoseHistory\n");
      printf("================\n");
      for(mergedIt = mergedPoses.begin(); mergedIt != mergedPoses.end(); ++mergedIt) {
        printf("%s%d: ", mergedIt->second.first.c_str(), mergedIt->first);
        mergedIt->second.second->second.Print();
      }
    }
    
  } // namespace Cozmo
} // namespace Anki

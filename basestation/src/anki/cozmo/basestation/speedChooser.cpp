/**
 * File: speedChooser.cpp
 *
 * Author: Al Chaussee
 * Date:   4/20/2016
 *
 * Description: Creates a path motion profile based on robot state
 *
 *
 * Copyright: Anki, Inc. 2016
 **/

#include "anki/cozmo/basestation/ankiEventUtil.h"
#include "anki/cozmo/basestation/robot.h"
#include "anki/cozmo/shared/cozmoEngineConfig.h"
#include "speedChooser.h"
#include "util/logging/logging.h"


namespace Anki {
  namespace Cozmo{
      
    SpeedChooser::SpeedChooser(Robot& robot)
    : _robot(robot)
    {

      if (_robot.HasExternalInterface() )
      {
        auto helper = MakeAnkiEventUtil(*_robot.GetExternalInterface(), *this, _signalHandles);
        using namespace ExternalInterface;
        helper.SubscribeGameToEngine<MessageGameToEngineTag::SetEnableSpeedChooser>();
      }
    }
    
    PathMotionProfile SpeedChooser::GetPathMotionProfile(const Pose3d& goal)
    {
      PathMotionProfile motionProfile = DEFAULT_PATH_MOTION_PROFILE;

      if( !_enabled ) {
        return motionProfile;
      }
      
      // Random acceleration
      motionProfile.accel_mmps2 = rng.RandDblInRange(minAccel_mmps2, maxAccel_mmps2);
      
      // Deceleration is opposite of acceleration
      motionProfile.decel_mmps2 = maxAccel_mmps2 - motionProfile.accel_mmps2 + minAccel_mmps2;
      
      // Speed based on distance to goal
      Pose3d pose;
      goal.GetWithRespectTo(_robot.GetPose(), pose);
      f32 distToObject = pose.GetTranslation().Length();
      f32 speed = (distToObject) * (maxSpeed_mmps - minSpeed_mmps) / (distToObjectForMaxSpeed_mm) + minSpeed_mmps;
      speed = CLIP(speed, minSpeed_mmps, maxSpeed_mmps);
      motionProfile.speed_mmps = speed;
      
      // Reverse speed 75% of forward speed
      motionProfile.reverseSpeed_mmps = motionProfile.speed_mmps * 0.75;
      
      PRINT_NAMED_INFO("SpeedChooser.GetPathMotionProfile", "distToGoal:%f using speed:%f revSpeed:%f accel:%f",
                       distToObject,
                       motionProfile.speed_mmps,
                       motionProfile.reverseSpeed_mmps,
                       motionProfile.accel_mmps2);
      
      return motionProfile;
    }
    
    PathMotionProfile SpeedChooser::GetPathMotionProfile(const std::vector<Pose3d>& goals)
    {
      if(goals.size() == 0)
      {
        PRINT_NAMED_WARNING("SpeedChooser.GetPathMotionProfile",
                            "Number of goal poses is 0 returning default motion profile");
        return DEFAULT_PATH_MOTION_PROFILE;
      }
      
      // Pick the goal pose that is closest to the robot
      Pose3d closestPoseToRobot = goals.front();
      f32 closestDist = MAXFLOAT;
      for(auto & pose : goals)
      {
        Pose3d p;
        pose.GetWithRespectTo(_robot.GetPose(), p);
        f32 dist = p.GetTranslation().Length();
        if(dist < closestDist)
        {
          closestPoseToRobot = pose;
          closestDist = dist;
        }
      }
      return GetPathMotionProfile(closestPoseToRobot);
    }

    template<>
    void SpeedChooser::HandleMessage(const ExternalInterface::SetEnableSpeedChooser& msg)
    {
      _enabled = msg.enabled;
    }
  
  }
}

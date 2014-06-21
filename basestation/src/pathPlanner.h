/**
 * File: pathPlanner.h
 *
 * Author: Kevin Yoon
 * Date:   2/24/2014
 *
 * Description: Class for planning collision-free paths from one pose to another.
 *              (Currently, uses Dubins path planning without any collision checking - Kevin 2/24/2014)
 *
 * Copyright: Anki, Inc. 2014
 **/

#ifndef PATH_PLANNER_H
#define PATH_PLANNER_H

#include "anki/common/basestation/math/pose.h"
#include "anki/common/types.h"
#include "anki/planning/shared/path.h"
#include "json/json-forwards.h"

#include <set>

namespace Anki {
  namespace Cozmo {

    class BlockWorld;
    
    class IPathPlanner
    {
    public:

      virtual ~IPathPlanner() {}

      // Replan if needed because the environment changed. Returns
      // DID_REPLAN if there is a new path and REPLAN_NOT_NEEDED if no
      // replan was necessary and the path has not changed.  If a new
      // path is needed but could not be computed a corresponding enum
      // value is returned.  Assumes the goal pose didn't change. If
      // forceReplanFromScratch = true, then definitely do a new plan, from
      // scratch. 
      // 
      // If the goal hasn't changed, it is better to call the version
      // that doesn't specify a goal
      // 
      // NOTE: Some planners may never attempt to replan unless you
      // set forceReplanFromScratch
      enum EPlanStatus {
        PLAN_NOT_NEEDED,
        DID_PLAN,
        PLAN_NEEDED_BUT_START_FAILURE,
        PLAN_NEEDED_BUT_GOAL_FAILURE,
        PLAN_NEEDED_BUT_PLAN_FAILURE
      };
      virtual EPlanStatus GetPlan(Planning::Path &path,
                                    const Pose3d& startPose,
                                    bool forceReplanFromScratch = false) {return PLAN_NOT_NEEDED;};

      // A simple planner that doesn't really support replanning can
      // just implement this function. forceReplanFromScratch is
      // implied to be true because we are changing both the start and
      // the goal
      virtual EPlanStatus GetPlan(Planning::Path &path,
                                  const Pose3d& startPose,
                                  const Pose3d& targetPose) = 0;

      // This version gets a plan to any of the goals you supply. It
      // is up to the planner implementation to decide. The last
      // argument will be set to the index into targetPoses that was
      // selected, if the planner succeeded
      virtual EPlanStatus GetPlan(Planning::Path &path,
                                  const Pose3d& startPose,
                                  const std::vector<Pose3d>& targetPoses,
                                  size_t& selectedIndex) {
        // TEMP!
        selectedIndex = 0;
        return GetPlan(path, startPose, targetPoses[0]);
      }
      
      void AddIgnoreType(const ObjectType_t objType)    { _ignoreTypes.insert(objType); }
      void RemoveIgnoreType(const ObjectType_t objType) { _ignoreTypes.erase(objType); }
      void ClearIgnoreTypes()                           { _ignoreTypes.clear(); }
      
      void AddIgnoreID(const ObjectID_t objID)          { _ignoreIDs.insert(objID); }
      void RemoveIgnoreID(const ObjectID_t objID)       { _ignoreIDs.erase(objID); }
      void ClearIgnoreIDs()                             { _ignoreIDs.clear(); }
      
    protected:
      
      std::set<ObjectType_t> _ignoreTypes;
      std::set<ObjectID_t>   _ignoreIDs;
      
    }; // Interface IPathPlanner

    // This is the Dubbins planner
    class PathPlanner : public IPathPlanner
    {
    public:
      PathPlanner();
      
      virtual EPlanStatus GetPlan(Planning::Path &path,
                                    const Pose3d& startPose,
                                    const Pose3d& targetPose) override;
      
    protected:
      
      
    }; // class PathPlanner

    // This make a plan that turns and drives towards the goal you give it
    class FaceAndApproachPlanner : public IPathPlanner
    {
    public:

      virtual EPlanStatus GetPlan(Planning::Path &path,
                                  const Pose3d& startPose,
                                  bool forceReplanFromScratch = false) override;

      virtual EPlanStatus GetPlan(Planning::Path &path,
                                  const Pose3d& startPose,
                                  const Pose3d& targetPose) override;
    protected:
      Vec3f _targetVec;
      float _finalTargetAngle;
    };

    class LatticePlannerImpl;

    class LatticePlanner : public IPathPlanner
    {
      friend LatticePlannerImpl;
    public:
      LatticePlanner(const BlockWorld* blockWorld, const Json::Value& mprims);
      virtual ~LatticePlanner();
      
      virtual EPlanStatus GetPlan(Planning::Path &path,
                                    const Pose3d& startPose,
                                    bool forceReplanFromScratch = false) override;

      virtual EPlanStatus GetPlan(Planning::Path &path,
                                    const Pose3d& startPose,
                                    const Pose3d& targetPose) override;

      // This version gets a plan to any of the goals you supply. It
      // is up to the planner implementation to decide 
      virtual EPlanStatus GetPlan(Planning::Path &path,
                                  const Pose3d& startPose,
                                  const std::vector<Pose3d>& targetPoses,
                                  size_t& selectedIndex) override;

    protected:
      LatticePlannerImpl* impl_;
    };
    
    class PathPlannerStub : public IPathPlanner
    {
    public:
      PathPlannerStub() { }

      virtual EPlanStatus GetPlan(Planning::Path &path,
                                    const Pose3d& startPose,
                                    const Pose3d& targetPose) override {
        return PLAN_NOT_NEEDED;
      }
    }; // class PathPlannerStub
    
    
  } // namespace Cozmo
} // namespace Anki


#endif // PATH_PLANNER_H

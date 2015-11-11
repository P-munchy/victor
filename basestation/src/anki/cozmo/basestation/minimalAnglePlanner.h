/**
 * File: minimalAnglePlanner.h
 *
 * Author: Brad Neuman
 * Created: 2015-11-09
 *
 * Description: A simple "planner" which tries to minimize the amount it turns away from the angle it is
 * currently facing. It will back straight up some distance, then turn in place to face the goal, drive to the
 * goal, then turn in place again. Very similar to FaceAndApproachPlanner, but will look better in some cases,
 * e.g. when docking
 *
 * Copyright: Anki, Inc. 2015
 *
 **/

#ifndef __BASESTATION_MINIMALANGLEPLANNER_H__
#define __BASESTATION_MINIMALANGLEPLANNER_H__

#include "anki/common/basestation/math/point.h"
#include "anki/common/shared/radians.h"
#include "pathPlanner.h"

namespace Anki {
namespace Cozmo {

class MinimalAnglePlanner : public IPathPlanner
{
public:

  virtual EComputePathStatus ComputePath(const Pose3d& startPose,
                                         const Pose3d& targetPose) override;

  virtual EComputePathStatus ComputeNewPathIfNeeded(const Pose3d& startPose,
                                                    bool forceReplanFromScratch = false) override;
protected:
  Vec3f _targetVec;
  Radians _finalTargetAngle;
};

}
}

#endif

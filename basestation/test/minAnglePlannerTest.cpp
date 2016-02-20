/**
 * File: minAnglePlannerTest.cpp
 *
 * Author: Brad Neuman
 * Created: 2016-01-22
 *
 * Description: Tests for the minimal angle planner
 *
 * Copyright: Anki, Inc. 2016
 *
 **/

#include "json/json.h"
#include "util/helpers/includeGTest.h"

#define private public
#define protected public

#include "anki/common/types.h"
#include "anki/cozmo/basestation/minimalAnglePlanner.h"
#include "anki/cozmo/basestation/robot.h"
#include "anki/cozmo/basestation/cozmoContext.h"
#include "anki/cozmo/basestation/robotInterface/messageHandler.h"
#include "anki/cozmo/basestation/robotInterface/messageHandlerStub.h"

using namespace Anki;
using namespace Cozmo;

extern Anki::Cozmo::CozmoContext* cozmoContext;

TEST(MinAnglePlanner, Create)
{
  Robot robot(1, cozmoContext);

  MinimalAnglePlanner* planner = dynamic_cast<MinimalAnglePlanner*>(robot._shortMinAnglePathPlanner);
  ASSERT_TRUE( planner != nullptr );
}


TEST(MinAnglePlanner, Straight)
{
  Robot robot(1, cozmoContext);

  MinimalAnglePlanner* planner = dynamic_cast<MinimalAnglePlanner*>(robot._shortMinAnglePathPlanner);
  ASSERT_TRUE( planner != nullptr );

  Pose3d start(0, Z_AXIS_3D(), Vec3f(0,0,0) );
  Pose3d goal(0, Z_AXIS_3D(), Vec3f(20,0,0) );

  EComputePathStatus ret = planner->ComputePath(start, goal);
  EXPECT_EQ(ret, EComputePathStatus::Running);

  EPlannerStatus status = planner->CheckPlanningStatus();
  EXPECT_EQ( status, EPlannerStatus::CompleteWithPlan );

  size_t selectedTargetIdx = 100;
  Planning::Path path;

  bool hasPath = planner->GetCompletePath(start, path, selectedTargetIdx);
  ASSERT_TRUE(hasPath);
  EXPECT_EQ(selectedTargetIdx, 0) << "only one target, should have selected it";

  ASSERT_EQ(path.GetNumSegments(), 1) << "should have one path segment";

  EXPECT_EQ(path[0].GetType(), Planning::PathSegmentType::PST_LINE);
}


TEST(MinAnglePlanner, Simple)
{
  Robot robot(1, cozmoContext);

  MinimalAnglePlanner* planner = dynamic_cast<MinimalAnglePlanner*>(robot._shortMinAnglePathPlanner);
  ASSERT_TRUE( planner != nullptr );

  Pose3d start(0, Z_AXIS_3D(), Vec3f(0,0,0) );
  Pose3d goal(0, Z_AXIS_3D(), Vec3f(5,7,0) );

  EComputePathStatus ret = planner->ComputePath(start, goal);
  EXPECT_EQ(ret, EComputePathStatus::Running);

  EPlannerStatus status = planner->CheckPlanningStatus();
  EXPECT_EQ( status, EPlannerStatus::CompleteWithPlan );

  size_t selectedTargetIdx = 100;
  Planning::Path path;

  bool hasPath = planner->GetCompletePath(start, path, selectedTargetIdx);
  ASSERT_TRUE(hasPath);
  EXPECT_EQ(selectedTargetIdx, 0) << "only one target, should have selected it";

  ASSERT_EQ(path.GetNumSegments(), 4) << "should have one path segment";

  EXPECT_EQ(path[0].GetType(), Planning::PathSegmentType::PST_LINE);
  EXPECT_EQ(path[1].GetType(), Planning::PathSegmentType::PST_POINT_TURN);
  EXPECT_EQ(path[2].GetType(), Planning::PathSegmentType::PST_LINE);
  EXPECT_EQ(path[3].GetType(), Planning::PathSegmentType::PST_POINT_TURN);
}

TEST(MinAnglePlanner, NoFinalTurn)
{
  Robot robot(1, cozmoContext);

  MinimalAnglePlanner* planner = dynamic_cast<MinimalAnglePlanner*>(robot._shortMinAnglePathPlanner);
  ASSERT_TRUE( planner != nullptr );

  Pose3d start(0, Z_AXIS_3D(), Vec3f(0,0,0) );
  Pose3d goal(0.392, Z_AXIS_3D(), Vec3f(5,7,0) );

  EComputePathStatus ret = planner->ComputePath(start, goal);
  EXPECT_EQ(ret, EComputePathStatus::Running);

  EPlannerStatus status = planner->CheckPlanningStatus();
  EXPECT_EQ( status, EPlannerStatus::CompleteWithPlan );

  size_t selectedTargetIdx = 100;
  Planning::Path path;

  bool hasPath = planner->GetCompletePath(start, path, selectedTargetIdx);
  ASSERT_TRUE(hasPath);
  EXPECT_EQ(selectedTargetIdx, 0) << "only one target, should have selected it";

  ASSERT_EQ(path.GetNumSegments(), 3) << "should have one path segment";

  EXPECT_EQ(path[0].GetType(), Planning::PathSegmentType::PST_LINE);
  EXPECT_EQ(path[1].GetType(), Planning::PathSegmentType::PST_POINT_TURN);
  EXPECT_EQ(path[2].GetType(), Planning::PathSegmentType::PST_LINE);
}

TEST(MinAnglePlanner, StraightAndTurn)
{
  Robot robot(1, cozmoContext);

  MinimalAnglePlanner* planner = dynamic_cast<MinimalAnglePlanner*>(robot._shortMinAnglePathPlanner);
  ASSERT_TRUE( planner != nullptr );

  Pose3d start(0, Z_AXIS_3D(), Vec3f(0,0,0) );
  Pose3d goal(0.392, Z_AXIS_3D(), Vec3f(12.0,0.04,0) );

  EComputePathStatus ret = planner->ComputePath(start, goal);
  EXPECT_EQ(ret, EComputePathStatus::Running);

  EPlannerStatus status = planner->CheckPlanningStatus();
  EXPECT_EQ( status, EPlannerStatus::CompleteWithPlan );

  size_t selectedTargetIdx = 100;
  Planning::Path path;

  bool hasPath = planner->GetCompletePath(start, path, selectedTargetIdx);
  ASSERT_TRUE(hasPath);
  EXPECT_EQ(selectedTargetIdx, 0) << "only one target, should have selected it";

  ASSERT_EQ(path.GetNumSegments(), 2) << "should have one path segment";

  EXPECT_EQ(path[0].GetType(), Planning::PathSegmentType::PST_LINE);
  EXPECT_EQ(path[1].GetType(), Planning::PathSegmentType::PST_POINT_TURN);
}

TEST(MinAnglePlanner, NoBackup)
{
  Robot robot(1, cozmoContext);

  MinimalAnglePlanner* planner = dynamic_cast<MinimalAnglePlanner*>(robot._shortMinAnglePathPlanner);
  ASSERT_TRUE( planner != nullptr );

  Pose3d start(0, Z_AXIS_3D(), Vec3f(0,0,0) );
  Pose3d goal(0, Z_AXIS_3D(), Vec3f(20,1.3,0) );

  EComputePathStatus ret = planner->ComputePath(start, goal);
  EXPECT_EQ(ret, EComputePathStatus::Running);

  EPlannerStatus status = planner->CheckPlanningStatus();
  EXPECT_EQ( status, EPlannerStatus::CompleteWithPlan );

  size_t selectedTargetIdx = 100;
  Planning::Path path;

  bool hasPath = planner->GetCompletePath(start, path, selectedTargetIdx);
  ASSERT_TRUE(hasPath);
  EXPECT_EQ(selectedTargetIdx, 0) << "only one target, should have selected it";

  ASSERT_EQ(path.GetNumSegments(), 3) << "should have one path segment";

  EXPECT_EQ(path[0].GetType(), Planning::PathSegmentType::PST_POINT_TURN);
  EXPECT_EQ(path[1].GetType(), Planning::PathSegmentType::PST_LINE);
  EXPECT_EQ(path[2].GetType(), Planning::PathSegmentType::PST_POINT_TURN);
}

TEST(MinAnglePlanner, TurnOnly)
{
  Robot robot(1, cozmoContext);

  MinimalAnglePlanner* planner = dynamic_cast<MinimalAnglePlanner*>(robot._shortMinAnglePathPlanner);
  ASSERT_TRUE( planner != nullptr );

  Pose3d start(0, Z_AXIS_3D(), Vec3f(0,0,0) );
  Pose3d goal(DEG_TO_RAD(45.0), Z_AXIS_3D(), Vec3f(1.4,-0.54,0) );

  EComputePathStatus ret = planner->ComputePath(start, goal);
  EXPECT_EQ(ret, EComputePathStatus::Running);

  EPlannerStatus status = planner->CheckPlanningStatus();
  EXPECT_EQ( status, EPlannerStatus::CompleteWithPlan );

  size_t selectedTargetIdx = 100;
  Planning::Path path;

  bool hasPath = planner->GetCompletePath(start, path, selectedTargetIdx);
  ASSERT_TRUE(hasPath);
  EXPECT_EQ(selectedTargetIdx, 0) << "only one target, should have selected it";

  ASSERT_EQ(path.GetNumSegments(), 1) << "should have one path segment";

  EXPECT_EQ(path[0].GetType(), Planning::PathSegmentType::PST_POINT_TURN);
}

TEST(MinAnglePlanner, OldBug)
{
  Robot robot(1, cozmoContext);

  MinimalAnglePlanner* planner = dynamic_cast<MinimalAnglePlanner*>(robot._shortMinAnglePathPlanner);
  ASSERT_TRUE( planner != nullptr );

  Pose3d start(0, Z_AXIS_3D(), Vec3f(166.914886, 153.714859, 0));
  Pose3d goal( DEG_TO_RAD(-7.68), Z_AXIS_3D(), Vec3f(149.33, 153.33, 0));
  
  EComputePathStatus ret = planner->ComputePath(start, goal);
  EXPECT_EQ(ret, EComputePathStatus::Running);

  EPlannerStatus status = planner->CheckPlanningStatus();
  EXPECT_EQ( status, EPlannerStatus::CompleteWithPlan );

  size_t selectedTargetIdx = 100;
  Planning::Path path;

  bool hasPath = planner->GetCompletePath(start, path, selectedTargetIdx);
  ASSERT_TRUE(hasPath);
  EXPECT_EQ(selectedTargetIdx, 0) << "only one target, should have selected it";

  ASSERT_EQ(path.GetNumSegments(), 2) << "should have one path segment";

  EXPECT_EQ(path[0].GetType(), Planning::PathSegmentType::PST_LINE);
  EXPECT_EQ(path[1].GetType(), Planning::PathSegmentType::PST_POINT_TURN);
}

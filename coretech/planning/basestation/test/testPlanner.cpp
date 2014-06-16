#include "gtest/gtest.h"
#include <set>
#include <vector>

#define private public
#define protected public

#include "anki/common/basestation/general.h"
#include "anki/common/basestation/math/rotatedRect.h"
#include "anki/common/basestation/platformPathManager.h"
#include "anki/planning/basestation/xythetaEnvironment.h"
#include "anki/planning/basestation/xythetaPlanner.h"

// hack just for unit testing, don't do this outside of tests
#include "xythetaPlanner_internal.h"


using namespace std;
using namespace Anki::Planning;


GTEST_TEST(TestPlanner, PlanOnceEmptyEnv)
{
  // Assuming this is running from root/build......
  xythetaEnvironment env;

  // TODO:(bn) open something saved in the test dir isntead, so we
  // know not to change or remove it
  EXPECT_TRUE(env.ReadMotionPrimitives(
                PREPEND_SCOPED_PATH(Test, "coretech/planning/matlab/test_mprim.json").c_str()));


  xythetaPlanner planner(env);

  State_c start(0.0, 1.0, 0.57);
  State_c goal(-10.0, 3.0, -1.5);

  ASSERT_TRUE(planner.SetStart(start));
  ASSERT_TRUE(planner.SetGoal(goal));

  EXPECT_TRUE(planner.Replan());
  EXPECT_TRUE(planner.PlanIsSafe(0));
}

GTEST_TEST(TestPlanner, PlanAroundBox)
{
  // Assuming this is running from root/build......
  xythetaEnvironment env;

  // TODO:(bn) open something saved in the test dir isntead, so we
  // know not to change or remove it
  EXPECT_TRUE(env.ReadMotionPrimitives(
                PREPEND_SCOPED_PATH(Test, "coretech/planning/matlab/test_mprim.json").c_str()));


  env.AddObstacle(Anki::RotatedRectangle(50.0, -10.0, 80.0, -10.0, 20.0));

  xythetaPlanner planner(env);

  State_c start(0, 0, 0);
  State_c goal(200, 0, 0);

  ASSERT_TRUE(planner.SetStart(start));
  ASSERT_TRUE(planner.SetGoal(goal));

  ASSERT_TRUE(planner.Replan());
  EXPECT_TRUE(planner.PlanIsSafe(0));
}

GTEST_TEST(TestPlanner, ReplanEasy)
{
  // Assuming this is running from root/build......
  xythetaEnvironment env;

  // TODO:(bn) open something saved in the test dir isntead, so we
  // know not to change or remove it
  EXPECT_TRUE(env.ReadMotionPrimitives(
                PREPEND_SCOPED_PATH(Test, "coretech/planning/matlab/test_mprim.json").c_str()));

  xythetaPlanner planner(env);

  State_c start(0, 0, 0);
  State_c goal(200, 0, 0);

  ASSERT_TRUE(planner.SetStart(start));
  ASSERT_TRUE(planner.SetGoal(goal));

  EXPECT_TRUE(planner.Replan());
  EXPECT_TRUE(planner.PlanIsSafe(0));

  env.AddObstacle(Anki::RotatedRectangle(50.0, -100.0, 80.0, -100.0, 20.0));

  EXPECT_TRUE(planner.PlanIsSafe(0)) << "new obstacle should not interfere with plan";

  EXPECT_TRUE(planner.Replan());
  EXPECT_TRUE(planner.PlanIsSafe(0));
}


GTEST_TEST(TestPlanner, ReplanHard)
{
  // Assuming this is running from root/build......
  xythetaEnvironment env;

  // TODO:(bn) open something saved in the test dir isntead, so we
  // know not to change or remove it
  EXPECT_TRUE(env.ReadMotionPrimitives(
                PREPEND_SCOPED_PATH(Test, "coretech/planning/matlab/test_mprim.json").c_str()));

  xythetaPlanner planner(env);

  State_c start(0, 0, 0);
  State_c goal(800, 0, 0);

  ASSERT_TRUE(planner.SetStart(start));
  ASSERT_TRUE(planner.SetGoal(goal));

  EXPECT_TRUE(planner.Replan());
  EXPECT_TRUE(planner.PlanIsSafe(0));

  // env.PrintPlan(planner.GetPlan());

  env.AddObstacle(Anki::RotatedRectangle(200.0, -10.0, 230.0, -10.0, 20.0));

  EXPECT_FALSE(planner.PlanIsSafe(0)) << "new obstacle should block plan!";

  State_c newRobotPos(31.7*5, -1.35, 0.0736);
  ASSERT_FALSE(env.IsInCollision(newRobotPos)) << "position "<<newRobotPos<<" should be safe";
  ASSERT_FALSE(env.IsInCollision(env.State_c2State(newRobotPos)));

  State_c lastSafeState;
  xythetaPlan oldPlan;

  int currentPlanIdx = planner.FindClosestPlanSegmentToPose(newRobotPos);
  ASSERT_EQ(currentPlanIdx, 3) << "should be at action #3 in the plan (plan should have 1 short, then 3 long straights in a row)";

  ASSERT_FALSE(planner.PlanIsSafe(1000.0, currentPlanIdx, lastSafeState, oldPlan));

  ASSERT_GE(oldPlan.Size(), 1) << "should re-use at least one action from the old plan";

  StateID currID = oldPlan.start_.GetStateID();
  for(const auto& action : oldPlan.actions_) {
    ASSERT_TRUE(env.ApplyAction(action, currID, false)) << "couldn't apply action!";
  }

  ASSERT_EQ(currID, env.State_c2State(lastSafeState).GetStateID()) << "end of validOldPlan should match lastSafeState!";
  
  // replan from last safe state
  ASSERT_TRUE(planner.SetStart(lastSafeState));
  ASSERT_TRUE(planner.GoalIsValid()) << "goal should still be valid";

  EXPECT_TRUE(planner.Replan());
  EXPECT_TRUE(planner.PlanIsSafe(0));
}


GTEST_TEST(TestPlanner, ClosestSegmentToPose_straight)
{
  xythetaEnvironment env;

  // TODO:(bn) open something saved in the test dir isntead, so we
  // know not to change or remove it
  EXPECT_TRUE(env.ReadMotionPrimitives(
                PREPEND_SCOPED_PATH(Test, "coretech/planning/matlab/test_mprim.json").c_str()));

  xythetaPlanner planner(env);

  planner._impl->plan_.start_ = State(0, 0, 0);

  ASSERT_EQ(env.GetRawMotionPrimitive(0, 0).endStateOffset.x, 1) << "invalid action";
  ASSERT_EQ(env.GetRawMotionPrimitive(0, 0).endStateOffset.y, 0) << "invalid action";

  for(int i=0; i<10; ++i) {
    planner._impl->plan_.Push(0);
  }

  // plan now goes form (0,0) to (10,0), any point in between should work

  for(float distAlong = 0.0f; distAlong < 12.0 * env.GetResolution_mm(); distAlong += 0.7356 * env.GetResolution_mm()) {
    size_t expected = (size_t)floor(distAlong / env.GetResolution_mm());
    if(expected >= 10)
      expected = 9;

    State_c pose(distAlong, 0.0, 0.0);
    ASSERT_EQ(planner.FindClosestPlanSegmentToPose(pose), expected) 
      <<"closest path segment doesn't match expectation for state "<<pose;

    pose.y_mm = 7.36;
    ASSERT_EQ(planner.FindClosestPlanSegmentToPose(pose), expected) 
      <<"closest path segment doesn't match expectation for state "<<pose;

    pose.y_mm = -0.3;
    ASSERT_EQ(planner.FindClosestPlanSegmentToPose(pose), expected) 
      <<"closest path segment doesn't match expectation for state "<<pose;
  }
}



GTEST_TEST(TestPlanner, ClosestSegmentToPose_wiggle)
{
  xythetaEnvironment env;

  // TODO:(bn) open something saved in the test dir isntead, so we
  // know not to change or remove it
  EXPECT_TRUE(env.ReadMotionPrimitives(
                PREPEND_SCOPED_PATH(Test, "coretech/planning/matlab/test_mprim.json").c_str()));

  xythetaPlanner planner(env);

  // bunch of random actions, no turn in place
  planner._impl->plan_.start_ = State(0, 0, 6);
  planner._impl->plan_.Push(0);
  planner._impl->plan_.Push(2);
  planner._impl->plan_.Push(2);
  planner._impl->plan_.Push(0);
  planner._impl->plan_.Push(1);
  planner._impl->plan_.Push(2);
  planner._impl->plan_.Push(2);
  planner._impl->plan_.Push(0);
  planner._impl->plan_.Push(1);
  planner._impl->plan_.Push(3);
  planner._impl->plan_.Push(3);
  planner._impl->plan_.Push(0);

  // go through each intermediate point, perturb it a bit, and make sure it returns correctly
  State curr = State(0,0,6);

  size_t planSize = planner._impl->plan_.Size();
  for(size_t planIdx = 0; planIdx < planSize; ++planIdx) {
    const MotionPrimitive& prim(env.GetRawMotionPrimitive(curr.theta, planner._impl->plan_.actions_[planIdx]));

    ASSERT_EQ(planIdx, planner.FindClosestPlanSegmentToPose(env.State2State_c(curr))) << "initial state wrong";

    ASSERT_FALSE(prim.intermediatePositions.empty());

    // check everything except the last one (which overlaps with the next segment)
    for(size_t intermediateIdx = 0; intermediateIdx < prim.intermediatePositions.size() - 1; intermediateIdx++) {
      State_c pose(prim.intermediatePositions[intermediateIdx].x_mm + env.GetX_mm(curr.x),
                   prim.intermediatePositions[intermediateIdx].y_mm + env.GetX_mm(curr.y),
                   prim.intermediatePositions[intermediateIdx].theta);

      ASSERT_EQ(planIdx, planner.FindClosestPlanSegmentToPose(pose)) << "exact intermediate state "<<intermediateIdx<<" wrong";

      pose.x_mm += 0.003;
      pose.y_mm -= 0.006;
      ASSERT_EQ(planIdx, planner.FindClosestPlanSegmentToPose(pose)) << "offset intermediate state "<<intermediateIdx<<" wrong";
    }

    StateID currID(curr);
    env.ApplyAction(planner._impl->plan_.actions_[planIdx], currID, false);
    curr = State(currID);
  }
}

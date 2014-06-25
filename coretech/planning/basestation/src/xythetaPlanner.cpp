#include "anki/planning/basestation/xythetaPlanner.h"

#include <iostream>
#include <assert.h>
#include "xythetaPlanner_internal.h"
#include "anki/common/basestation/general.h"

namespace Anki {
namespace Planning {

xythetaPlanner::xythetaPlanner(const xythetaEnvironment& env)
{
  _impl = new xythetaPlannerImpl(env);
}

xythetaPlanner::~xythetaPlanner()
{
  delete _impl;
  _impl = NULL;
}

bool xythetaPlanner::SetGoal(const State_c& goal)
{
  return _impl->SetGoal(goal);
}

State_c xythetaPlanner::GetGoal() const
{
  return _impl->goal_c_;
}

bool xythetaPlanner::GoalIsValid() const
{
  return _impl->GoalIsValid();
}

bool xythetaPlanner::SetStart(const State_c& start)
{
  return _impl->SetStart(start);
}

void xythetaPlanner::AllowFreeTurnInPlaceAtGoal(bool allow)
{
  _impl->freeTurnInPlaceAtGoal_ = allow;
}

bool xythetaPlanner::Replan(unsigned int maxExpansions)
{
  return _impl->ComputePath(maxExpansions);
}

void xythetaPlanner::SetReplanFromScratch()
{
  _impl->fromScratch_ = true;
}

const xythetaPlan& xythetaPlanner::GetPlan() const
{
  return _impl->plan_;
}

xythetaPlan& xythetaPlanner::GetPlan()
{
  return _impl->plan_;
}

Cost xythetaPlanner::GetFinalCost() const
{
  return _impl->finalCost_;
}

////////////////////////////////////////////////////////////////////////////////
// implementation functions
////////////////////////////////////////////////////////////////////////////////

#define PLANNER_DEBUG_PLOT_STATES_CONSIDERED 0

xythetaPlannerImpl::xythetaPlannerImpl(const xythetaEnvironment& env)
  : start_(0,0,0)
  , env_(env)
  , goalChanged_(false)
  , freeTurnInPlaceAtGoal_(false)
  , searchNum_(0)
{
  startID_ = start_.GetStateID();
  Reset();
}

bool xythetaPlannerImpl::SetGoal(const State_c& goal_c)
{
  if(env_.IsInCollision(goal_c)) {
    printf("goal is in collision!\n");
    return false;
  }

  State goal = env_.State_c2State(goal_c);

  if(env_.IsInCollision(goal)) {
    if(!env_.RoundSafe(goal_c, goal)) {
      printf("all possible discrete states of goal are in collision!\n");
      return false;
    }

    assert(!env_.IsInCollision(goal));
  }

  goalID_ = goal.GetStateID();

  std::cout<<goal<<std::endl;

  // convert back so this is still lined up perfectly with goalID_
  goal_c_ = env_.State2State_c(goal);

  // TEMP: for now, replan if the goal changes, but this isn't
  // necessary. Instead, we could just re-order the open list and
  // continue as usual
  fromScratch_ = true;

  goalChanged_ = true;
  return true;
}

bool xythetaPlannerImpl::GoalIsValid() const
{
  return !env_.IsInCollision(goal_c_);
}


bool xythetaPlannerImpl::SetStart(const State_c& start_c)
{
  if(env_.IsInCollision(start_c)) {
    printf("start is in collision!\n");
    return false;
  }

  start_ = env_.State_c2State(start_c);

  if(env_.IsInCollision(start_)) {
    if(!env_.RoundSafe(start_c, start_)) {
      printf("all possible discrete states of start are in collision!\n");
      return false;
    }

    assert(!env_.IsInCollision(start_));
  }

  startID_ = start_.GetStateID();

  // if the start changes, can't re-use anything
  fromScratch_ = true;

  return true;
}


void xythetaPlannerImpl::Reset()
{
  plan_.Clear();

  // TODO:(bn) shouln't need to clear these if I use searchNum_
  // properly
  table_.Clear();
  open_.clear();

  expansions_ = 0;
  considerations_ = 0;
  collisionChecks_ = 0;

  goalChanged_ = false;
  fromScratch_ = false;

  finalCost_ = 0.0f;
}

bool xythetaPlannerImpl::NeedsReplan() const
{
  State_c waste1;
  xythetaPlan waste2;
  // TODO:(bn) parameter or at least a comment.
  const float default_maxDistanceToReUse_mm = 60.0;
  return !env_.PlanIsSafe(plan_, default_maxDistanceToReUse_mm, 0, waste1, waste2);
}

bool xythetaPlannerImpl::ComputePath(unsigned int maxExpansions)
{
  if(fromScratch_ || NeedsReplan()) {
    Reset();
  }
  else {
    printf("No replan needed!\n");
    return true;
  }

  if(PLANNER_DEBUG_PLOT_STATES_CONSIDERED) {
    debugExpPlotFile_ = fopen("expanded.txt", "w");
  }

  StateID startID = start_.GetStateID();

  // push starting state
  table_.emplace(startID, 
                 open_.insert(startID, 0.0),
                 startID,
                 0, // action doesn't matter
                 0.0,
                 0.0);

  bool foundGoal = false;
  while(!open_.empty()) {
    StateID sid = open_.pop();
    if(sid == goalID_) {
      foundGoal = true;
      finalCost_ = table_[sid].g_;
      printf("expanded goal! cost = %f\n", finalCost_);
      break;
    }

    ExpandState(sid);
    expansions_++;
    if(expansions_ > maxExpansions) {
      printf("exceeded max expansions of %u!\n", maxExpansions);
      printf("topF =  %8.5f (%8.5f + %8.5f)\n",
                 open_.topF(),
                 table_[open_.top()].g_,
                 open_.topF() -
                 table_[open_.top()].g_);
      return false;
    }

    if(PLANNER_DEBUG_PLOT_STATES_CONSIDERED) {
      State_c c = env_.State2State_c(State(sid));
      fprintf(debugExpPlotFile_, "%f %f %f %d\n",
                  c.x_mm,
                  c.y_mm,
                  c.theta,
                  sid.theta);
    }


    // TEMP: 
    if(expansions_ % 10000 == 0) {
      printf("%8d %8.5f = %8.5f + %8.5f\n",
                 expansions_,
                 open_.topF(),
                 table_[open_.top()].g_,
                 open_.topF() -
                 table_[open_.top()].g_);
    }
  }

  if(foundGoal) {
    BuildPlan();
  }
  else {
    printf("xythetaPlanner: no path found!\n");
  }

  if(PLANNER_DEBUG_PLOT_STATES_CONSIDERED) {
    fclose(debugExpPlotFile_);
  }

  printf("finished after %d expansions. foundGoal = %d\n", expansions_, foundGoal);

  return foundGoal;
}

void xythetaPlannerImpl::ExpandState(StateID currID)
{
  Cost currG = table_[currID].g_;
  
  SuccessorIterator it = env_.GetSuccessors(currID, currG);

  if(!it.Done(env_))
    it.Next(env_);

  while(!it.Done(env_)) {
    considerations_++;

    StateID nextID = it.Front().stateID;
    float newG = it.Front().g;

    if(freeTurnInPlaceAtGoal_ && currID.x == goalID_.x && currID.y == goalID_.y)
      newG = currG;

    auto oldEntry = table_.find(nextID);

    if(oldEntry == table_.end()) {    
      Cost h = heur(nextID);
      Cost f = newG + h;
      table_.emplace(nextID,
                     open_.insert(nextID, f),
                     currID,
                     it.Front().actionID,
                     it.Front().penalty,
                     newG);
    }
    else if(!oldEntry->second.IsClosed(searchNum_)) {
      // only update if g value is lower
      if(newG < oldEntry->second.g_) {
        Cost h = heur(nextID);
        Cost f = newG + h;
        oldEntry->second.openIt_ = open_.insert(nextID, f);
        oldEntry->second.closedIter_ = -1;
        oldEntry->second.backpointer_ = currID;
        oldEntry->second.backpointerAction_ = it.Front().actionID;
        oldEntry->second.g_ = newG;
      }
    }

    it.Next(env_);    
  }

  table_[currID].closedIter_ = searchNum_;
}

Cost xythetaPlannerImpl::heur(StateID sid)
{
  State s(sid);
  // return euclidean distance in mm

  return env_.GetDistanceBetween(goal_c_, s) * env_.GetOneOverMaxVelocity();
}

void xythetaPlannerImpl::BuildPlan()
{
  // start at the goal and go backwards, pushing actions into the plan
  // until we get to the start id

  StateID curr = goalID_;
  BOUNDED_WHILE(1000, !(curr == startID_)) {
    auto it = table_.find(curr);

    assert(it != table_.end());

    plan_.Push(it->second.backpointerAction_, it->second.penaltyIntoState_);
    curr = it->second.backpointer_;
  }

  std::reverse(plan_.actions_.begin(), plan_.actions_.end());
  std::reverse(plan_.penalties_.begin(), plan_.penalties_.end());

  plan_.start_ = start_;

  printf("Created plan of length %lu\n", plan_.actions_.size());
}


}
}

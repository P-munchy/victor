/**
 * File: compoundActions.cpp
 *
 * Author: Andrew Stein
 * Date:   7/9/2014
 *
 * Description: Implements compound actions, which are groups of IActions to be
 *              run together in series or in parallel.
 *
 * Copyright: Anki, Inc. 2014
 **/

#include "anki/cozmo/basestation/compoundActions.h"

#include "anki/common/basestation/utils/timer.h"


namespace Anki {
  namespace Cozmo {
    
#pragma mark ---- ICompoundAction ----
    
    ICompoundAction::ICompoundAction(std::initializer_list<IActionRunner*> actions)
    {
      for(IActionRunner* action : actions) {
        if(action == nullptr) {
          PRINT_NAMED_WARNING("ICompoundAction.NullActionPointer",
                              "Refusing to add a null action pointer to group.\n");
        } else {
          AddAction(action);
        }
      }
    }
    
    ICompoundAction::~ICompoundAction()
    {
      for(auto & actionPair : _actions) {
        assert(actionPair.second != nullptr);
        // TODO: issue a warning when a group is deleted without all its actions completed?
        delete actionPair.second;
      }
    }
    
    void ICompoundAction::Reset()
    {
      for(auto & actionPair : _actions) {
        actionPair.first = false;
        actionPair.second->Reset();
      }
    }
    
    void ICompoundAction::AddAction(IActionRunner* action)
    {
      if(_actions.empty()) {
        _name = "["; // initialize with opening bracket for first action
      } else {
        _name.pop_back(); // remove last char ']'
        _name += "+";
      }
      
      _actions.emplace_back(false, action);
      _actions.back().second->SetIsPartOfCompoundAction(true);
      if(_actions.size()==1) {
        _name += action->GetName();
        _name += "]";
      }
    }
    
    bool ICompoundAction::ShouldLockHead() const
    {
      auto actionIter = _actions.begin();
      while(actionIter != _actions.end()) {
        if(actionIter->second->ShouldLockHead()) {
          return true;
        }
      }
      return false;
    }
    
    bool ICompoundAction::ShouldLockLift() const
    {
      auto actionIter = _actions.begin();
      while(actionIter != _actions.end()) {
        if(actionIter->second->ShouldLockLift()) {
          return true;
        }
      }
      return false;
    }
    
    bool ICompoundAction::ShouldLockWheels() const
    {
      auto actionIter = _actions.begin();
      while(actionIter != _actions.end()) {
        if(actionIter->second->ShouldLockWheels()) {
          return true;
        }
      }
      return false;
    }

#pragma mark ---- CompoundActionSequential ----
    
    CompoundActionSequential::CompoundActionSequential(std::initializer_list<IActionRunner*> actions)
    : ICompoundAction(actions)
    , _delayBetweenActionsInSeconds(0)
    , _waitUntilTime(-1.f)
    , _currentActionPair(_actions.begin())
    {
      
    }
    
    void CompoundActionSequential::Reset()
    {
      ICompoundAction::Reset();
      _waitUntilTime = -1.f;
      _currentActionPair = _actions.begin();
    }
    
    IAction::ActionResult CompoundActionSequential::UpdateInternal(Robot& robot)
    {
      SetStatus(GetName());
      
      if(_currentActionPair != _actions.end())
      {
        bool& isDone = _currentActionPair->first;
        assert(isDone == false);
        
        IActionRunner* currentAction = _currentActionPair->second;
        assert(currentAction != nullptr); // should not have been allowed in by constructor
        
        if(_waitUntilTime < 0.f ||
           BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() > _waitUntilTime)
        {
          const ActionResult subResult = currentAction->Update(robot);
          SetStatus(currentAction->GetStatus());
          switch(subResult)
          {
            case SUCCESS:
            {
              // Finished the current action, move ahead to the next
              isDone = true; // mark as done (not strictly necessary)
              
              if(_delayBetweenActionsInSeconds > 0.f) {
                // If there's a delay specified, figure out how long we need to
                // wait from now to start next action
                _waitUntilTime = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() + _delayBetweenActionsInSeconds;
              }
              
              ++_currentActionPair;
              
              // if that was the last action, we're done
              if(_currentActionPair == _actions.end()) {
#             if USE_ACTION_CALLBACKS
                RunCallbacks(SUCCESS);
#             endif
                return SUCCESS;
              }
              
              // Otherwise, we are still running
              return RUNNING;
            }
              
            case FAILURE_RETRY:
              // A constituent action failed . Reset all the constituent actions
              // and try again as long as there are retries remaining
              if(RetriesRemain()) {
                PRINT_NAMED_INFO("CompoundActionSequential.Update.Retrying",
                                 "%s triggered retry.\n", currentAction->GetName().c_str());
                Reset();
                return RUNNING;
              }
              // No retries remaining. Fall through:
              
            case RUNNING:
            case FAILURE_ABORT:
            case FAILURE_TIMEOUT:
            case FAILURE_PROCEED:
#           if USE_ACTION_CALLBACKS
              RunCallbacks(subResult);
#           endif
              return subResult;
              
          } // switch(result)
        } else {
          return RUNNING;
        } // if/else waitUntilTime
      } // if currentAction != actions.end()
      
      // Shouldn't normally get here, but this means we've completed everything
      // and are done
      return SUCCESS;
      
    } // CompoundActionSequential::Update()
    
    
#pragma mark ---- CompoundActionParallel ----
    
    CompoundActionParallel::CompoundActionParallel(std::initializer_list<IActionRunner*> actions)
    : ICompoundAction(actions)
    {
      
    }
    
    IAction::ActionResult CompoundActionParallel::UpdateInternal(Robot& robot)
    {
      // Return success unless we encounter anything still running or failed in loop below.
      // Note that we will return SUCCESS on the call following the one where the
      // last action actually finishes.
      ActionResult result = SUCCESS;
      
      SetStatus(GetName());
      
      for(auto & currentActionPair : _actions)
      {
        bool& isDone = currentActionPair.first;
        if(!isDone) {
          IActionRunner* currentAction = currentActionPair.second;
          assert(currentAction != nullptr); // should not have been allowed in by constructor
          
          const ActionResult subResult = currentAction->Update(robot);
          SetStatus(currentAction->GetStatus());
          switch(subResult)
          {
            case SUCCESS:
              // Just finished this action, mark it as done
              isDone = true;
              break;
              
            case RUNNING:
              // If any action is still running the group is still running
              result = RUNNING;
              break;
              
            case FAILURE_RETRY:
              // If any retries are left, reset the group and try again.
              if(RetriesRemain()) {
                PRINT_NAMED_INFO("CompoundActionParallel.Update.Retrying",
                                 "%s triggered retry.\n", currentAction->GetName().c_str());
                Reset();
                return RUNNING;
              }
              
              // If not, just fall through to other failure handlers:
            case FAILURE_ABORT:
            case FAILURE_PROCEED:
            case FAILURE_TIMEOUT:
              // Return failure, aborting updating remaining actions the group
#             if USE_ACTION_CALLBACKS
              RunCallbacks(subResult);
#             endif
              return subResult;
              
            default:
              PRINT_NAMED_ERROR("CompoundActionParallel.Update.UnknownResultCase", "\n");
              assert(false);
              return FAILURE_ABORT;
              
          } // switch(subResult)
          
        } // if(!isDone)
      } // for each action in the group
      
#     if USE_ACTION_CALLBACKS
      if(result != RUNNING) {
        RunCallbacks(result);
      }
#     endif
      
      return result;
    } // CompoundActionParallel::Update()
    
  } // namespace Cozmo
} // namespace Anki

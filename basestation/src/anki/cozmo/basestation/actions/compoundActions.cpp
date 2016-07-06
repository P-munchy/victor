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

#include "anki/cozmo/basestation/actions/compoundActions.h"

#include "anki/common/basestation/utils/timer.h"
#include "util/helpers/templateHelpers.h"


namespace Anki {
  namespace Cozmo {
    
#pragma mark ---- ICompoundAction ----
    
    ICompoundAction::ICompoundAction(Robot& robot, std::list<IActionRunner*> actions)
    : IActionRunner(robot,
                    "ICompoundAction",
                    RobotActionType::COMPOUND,
                    (u8)AnimTrackFlag::NO_TRACKS)
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
      DeleteActions();
    }
    
    void ICompoundAction::Reset(bool shouldUnlockTracks)
    {
      ResetState();
      for(auto & action : _actions) {
        action->Reset(shouldUnlockTracks);
      }
    }
    
    void ICompoundAction::AddAction(IActionRunner* action, bool ignoreFailure)
    {
      std::string name = GetName();
      if(_actions.empty()) {
        name = "["; // initialize with opening bracket for first action
      } else {
        name.pop_back(); // remove last char ']'
        name += "+";
      }
      
      // All added actions have the same message display setting as the parent
      // compound action in which they are included
      action->EnableMessageDisplay(IsMessageDisplayEnabled());
      
      // As part of a compound action this should not emit completion
      action->ShouldEmitCompletionSignal(false);
      
      _actions.emplace_back(action);
      name += action->GetName();
      name += "]";
      
      SetName(name);
      
      if(ignoreFailure) {
        _ignoreFailure.insert(action);
      }
    }
    
    void ICompoundAction::ClearActions()
    {
      DeleteActions();
      _actions.clear();
      _ignoreFailure.clear();
      Reset();
    }
    
    void ICompoundAction::DeleteActions()
    {
      for(auto iter = _actions.begin(); iter != _actions.end();)
      {
        assert((*iter) != nullptr);
        // TODO: issue a warning when a group is deleted without all its actions completed?
        (*iter)->PrepForCompletion();
        Util::SafeDelete(*iter);
        iter = _actions.erase(iter);
      }
    }
    
    void ICompoundAction::StoreUnionAndDelete(std::list<IActionRunner*>::iterator& currentAction)
    {
      // Store this actions completion union before deleting it
      ActionCompletedUnion actionUnion;
      (*currentAction)->GetCompletionUnion(actionUnion);
      _completedActionInfoStack[(*currentAction)->GetTag()] = CompletionData{
        .completionUnion = actionUnion,
        .type            = (*currentAction)->GetType(),
      };
      
      // Delete completed action
      (*currentAction)->PrepForCompletion(); // Possible overkill
      Util::SafeDelete(*currentAction);
      currentAction = _actions.erase(currentAction);
    }

    
    bool ICompoundAction::ShouldIgnoreFailure(IActionRunner* action) const
    {
      // We should ignore this action's failure if it's in our ignore set
      return _ignoreFailure.find(action) != _ignoreFailure.end();
    }
    
    void ICompoundAction::SetProxyTag(u32 tag)
    {
      _proxyTag = tag;
      _proxySet = true;
    
      for(auto action : _actions) {
        if(action->GetTag() == _proxyTag) {
          SetType(action->GetType());
        }
      }
      
      auto iter = _completedActionInfoStack.find(_proxyTag);
      if(iter != _completedActionInfoStack.end()) {
        SetType(iter->second.type);
      }
    }
    
    void ICompoundAction::GetCompletionUnion(ActionCompletedUnion& completionUnion) const
    {
      if(_proxySet)
      {
        for(auto action : _actions) {
          if(action->GetTag() == _proxyTag) {
            return action->GetCompletionUnion(completionUnion);
          }
        }

        auto iter = _completedActionInfoStack.find(_proxyTag);
        if(iter != _completedActionInfoStack.end()) {
          completionUnion = iter->second.completionUnion;
          return;
        }
        
        PRINT_NAMED_WARNING("ICompoundAction.GetCompletionUnion.InvalidProxyTag",
                            "CompletionData with proxy tag=%d not found", _proxyTag);
      }
      
      IActionRunner::GetCompletionUnion(completionUnion);
    }
    
#pragma mark ---- CompoundActionSequential ----
    
    CompoundActionSequential::CompoundActionSequential(Robot& robot)
    : CompoundActionSequential(robot, {})
    {
      
    }
    
    CompoundActionSequential::CompoundActionSequential(Robot& robot, std::list<IActionRunner*> actions)
    : ICompoundAction(robot, actions)
    , _delayBetweenActionsInSeconds(0)
    {
      Reset();
    }
    
    void CompoundActionSequential::Reset(bool shouldUnlockTracks)
    {
      ICompoundAction::Reset(shouldUnlockTracks);
      _waitUntilTime = -1.f;
      _currentAction = _actions.begin();
      _wasJustReset = true;
    }
    
    ActionResult CompoundActionSequential::MoveToNextAction(double currentTime)
    {
      ActionResult subResult = ActionResult::SUCCESS;
      
      if(_delayBetweenActionsInSeconds > 0.f) {
        // If there's a delay specified, figure out how long we need to
        // wait from now to start next action
        _waitUntilTime = currentTime + _delayBetweenActionsInSeconds;
      }
      
      // Store this actions completion union and delete _currentActionPair
      StoreUnionAndDelete(_currentAction);
      
      // if that was the last action, we're done
      if(_currentAction == _actions.end()) {
        if(USE_ACTION_CALLBACKS) {
          RunCallbacks(ActionResult::SUCCESS);
        }
        return ActionResult::SUCCESS;
      } else if(currentTime >= _waitUntilTime) {
        PRINT_NAMED_INFO("CompoundActionSequential.Update.NextAction",
                         "Moving to action %s [%d]",
                         (*_currentAction)->GetName().c_str(),
                         (*_currentAction)->GetTag());
        
        // If the compound action is suppressing track locking then the constituent actions should too
        (*_currentAction)->ShouldSuppressTrackLocking(IsSuppressingTrackLocking());
        
        // Otherwise, we are still running. Go ahead and immediately do an
        // update on the next action now to get its initialization and
        // precondition checking going, to reduce lag between actions.
        subResult = (*_currentAction)->Update();
        
        // In the special case that the sub-action sucessfully completed
        // immediately, don't return SUCCESS if there are more actions left!
        if(ActionResult::SUCCESS == subResult) {
          
          StoreUnionAndDelete(_currentAction);
          
          if(_currentAction == _actions.end()) {
            // no more actions, safe to return success for the compound action
            if(USE_ACTION_CALLBACKS) {
              RunCallbacks(ActionResult::SUCCESS);
            }
            return ActionResult::SUCCESS;
          } else {
            // more actions, just say we're still running
            subResult = ActionResult::RUNNING;
          }
        }
      }
      else {
        // this sub-action finished, but we still have others that we are waiting to run, probably due
        // to delay between actions, so return running
        return ActionResult::RUNNING;
      }
      
      return subResult;
    }
    
    
    ActionResult CompoundActionSequential::UpdateInternal()
    {
      SetStatus(GetName());
      
      Result derivedUpdateResult = UpdateDerived();
      if(RESULT_OK != derivedUpdateResult) {
        PRINT_NAMED_INFO("CompoundActionSequential.UpdateInternal.UpdateDerivedFailed", "");
        return ActionResult::FAILURE_ABORT;
      }
      
      if(_wasJustReset) {
        // In case actions were added after construction/reset
        _currentAction = _actions.begin();
        _wasJustReset = false;
      }
      
      if(_currentAction != _actions.end())
      {
        assert((*_currentAction) != nullptr); // should not have been allowed in by constructor
        
        // If the compound action is suppressing track locking then the constituent actions should too
        (*_currentAction)->ShouldSuppressTrackLocking(IsSuppressingTrackLocking());
        
        double currentTime = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
        if(_waitUntilTime < 0.f || currentTime >= _waitUntilTime)
        {
          ActionResult subResult = (*_currentAction)->Update();
          SetStatus((*_currentAction)->GetStatus());
          
          switch(subResult)
          {
            case ActionResult::RUNNING:
            {
              return ActionResult::RUNNING;
            }
              
            case ActionResult::SUCCESS:
            {
              return MoveToNextAction(currentTime);
            }
              
            case ActionResult::FAILURE_RETRY:
              // A constituent action failed . Reset all the constituent actions
              // and try again as long as there are retries remaining
              if(RetriesRemain()) {
                PRINT_NAMED_INFO("CompoundActionSequential.Update.Retrying",
                                 "%s triggered retry.\n", (*_currentAction)->GetName().c_str());
                Reset();
                return ActionResult::RUNNING;
              }
              // No retries remaining. Fall through:

            case ActionResult::FAILURE_ABORT:
            case ActionResult::FAILURE_TIMEOUT:
            case ActionResult::FAILURE_PROCEED:
            case ActionResult::FAILURE_TRACKS_LOCKED:
            case ActionResult::FAILURE_BAD_TAG:
            case ActionResult::FAILURE_NOT_STARTED:
            case ActionResult::CANCELLED:
            case ActionResult::INTERRUPTED:
              if(USE_ACTION_CALLBACKS) {
                RunCallbacks(subResult);
              }
              if(ShouldIgnoreFailure(*_currentAction)) {
                // We are ignoring this action's failures, so just move to next action
                return MoveToNextAction(currentTime);
              } else {
                return subResult;
              }
              
          } // switch(result)
        } else {
          return ActionResult::RUNNING;
        } // if/else waitUntilTime
      } // if currentAction != actions.end()
      
      // Shouldn't normally get here, but this means we've completed everything
      // and are done
      return ActionResult::SUCCESS;
      
    } // CompoundActionSequential::Update()
    

    
#pragma mark ---- CompoundActionParallel ----
    
    CompoundActionParallel::CompoundActionParallel(Robot& robot)
    : CompoundActionParallel(robot, {})
    {
      
    }
    
    CompoundActionParallel::CompoundActionParallel(Robot& robot, std::list<IActionRunner*> actions)
    : ICompoundAction(robot, actions)
    {
      
    }
    
    ActionResult CompoundActionParallel::UpdateInternal()
    {
      // Return success unless we encounter anything still running or failed in loop below.
      // Note that we will return SUCCESS on the call following the one where the
      // last action actually finishes.
      ActionResult result = ActionResult::SUCCESS;
      
      SetStatus(GetName());
      
      for(auto currentAction = _actions.begin(); currentAction != _actions.end();)
      {
        assert((*currentAction) != nullptr); // should not have been allowed in by constructor
        
        // If the compound action is suppressing track locking then the constituent actions should too
        (*currentAction)->ShouldSuppressTrackLocking(IsSuppressingTrackLocking());

        const ActionResult subResult = (*currentAction)->Update();
        SetStatus((*currentAction)->GetStatus());
        switch(subResult)
        {
          case ActionResult::SUCCESS:
            // Just finished this action, delete it
            StoreUnionAndDelete(currentAction);
            break;
            
          case ActionResult::RUNNING:
            // If any action is still running the group is still running
            result = ActionResult::RUNNING;
            ++currentAction;
            break;
            
          case ActionResult::FAILURE_RETRY:
            // If any retries are left, reset the group and try again.
            if(RetriesRemain()) {
              PRINT_NAMED_INFO("CompoundActionParallel.Update.Retrying",
                               "%s triggered retry.\n", (*currentAction)->GetName().c_str());
              Reset();
              return ActionResult::RUNNING;
            }
            
            // If not, just fall through to other failure handlers:
          case ActionResult::FAILURE_ABORT:
          case ActionResult::FAILURE_PROCEED:
          case ActionResult::FAILURE_TIMEOUT:
          case ActionResult::FAILURE_TRACKS_LOCKED:
          case ActionResult::FAILURE_BAD_TAG:
          case ActionResult::FAILURE_NOT_STARTED:
          case ActionResult::CANCELLED:
          case ActionResult::INTERRUPTED:
          {
            // Return failure, aborting updating remaining actions the group
            if(USE_ACTION_CALLBACKS) {
              RunCallbacks(subResult);
            }
            if(ShouldIgnoreFailure(*currentAction)) {
              // Ignore the fact that this action failed and just delete it
              (*currentAction)->PrepForCompletion(); // Just in case we were cancelled
              StoreUnionAndDelete(currentAction);
              break;
            } else {
              return subResult;
            }
          }
        } // switch(subResult)
      } // for each action in the group
      
      if(USE_ACTION_CALLBACKS) {
        if(result != ActionResult::RUNNING) {
          RunCallbacks(result);
        }
      }
      
      return result;
    } // CompoundActionParallel::Update()
    
  } // namespace Cozmo
} // namespace Anki

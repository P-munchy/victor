/**
 * File: compoundActions.h
 *
 * Author: Andrew Stein
 * Date:   7/9/2014
 *
 * Description: Defines compound actions, which are groups of IActions to be
 *              run together in series or in parallel.
 *
 * Copyright: Anki, Inc. 2014
 **/

#ifndef ANKI_COZMO_COMPOUND_ACTIONS_H
#define ANKI_COZMO_COMPOUND_ACTIONS_H

#include "anki/cozmo/basestation/actions/actionInterface.h"

namespace Anki {
  namespace Cozmo {
    
    // Interface for compound actions, which are fixed sets of actions to be
    // run together or in order (determined by derived type)
    class ICompoundAction : public IActionRunner
    {
    public:
      ICompoundAction(Robot& robot, std::list<IActionRunner*> actions);
      
      virtual void AddAction(IActionRunner* action);
      
      // First calls cleanup on any constituent actions and then removes them
      // from this compound action completely.
      void ClearActions();
      
      const std::list<IActionRunner*>& GetActionList() const { return _actions; }
      
      // Constituent actions will be deleted upon destruction of the group
      virtual ~ICompoundAction();
      
      virtual const std::string& GetName() const override { return _name; }
      
      virtual RobotActionType GetType() const override { return RobotActionType::COMPOUND; }
      
      virtual u8 GetTracksToLock() const override { return (u8)AnimTrackFlag::NO_TRACKS; }

    protected:
      
      // Call the constituent actions' Reset() methods and mark them each not done.
      virtual void Reset(bool shouldUnlockTracks = true) override;
      
      std::list<IActionRunner*> _actions;
      std::string _name;
      
    private:
      void DeleteActions();
    };
    
    
    // Executes a fixed set of actions sequentially
    class CompoundActionSequential : public ICompoundAction
    {
    public:
      CompoundActionSequential(Robot& robot);
      CompoundActionSequential(Robot& robot, std::list<IActionRunner*> actions);
      
      // Add a delay, in seconds, between running each action in the group.
      // Default is 0 (no delay).
      void SetDelayBetweenActions(f32 seconds);
      
      // We want to override and not ignore any movement tracks ourselves; our constituent actions will
      // ignore what they want to when running
      virtual u8 GetTracksToLock() const override { return (u8)AnimTrackFlag::NO_TRACKS; }
      
    protected:
      // Stack of pairs of actionCompletionUnions and actionTypes of the already completed actions
      std::list<std::pair<ActionCompletedUnion, RobotActionType>> _completedActionInfoStack;
      
    private:
      virtual void Reset(bool shouldUnlockTracks = true) override final;
      
      virtual ActionResult UpdateInternal() override final;
      
      // Stores the _currentActionPair's completion union and then deletes _currentActionPair
      void StoreUnionAndDelete();
      
      f32 _delayBetweenActionsInSeconds;
      f32 _waitUntilTime;
      std::list<IActionRunner*>::iterator _currentAction;
      bool _wasJustReset;
      
    }; // class CompoundActionSequential
    
    inline void CompoundActionSequential::SetDelayBetweenActions(f32 seconds) {
      _delayBetweenActionsInSeconds = seconds;
    }
    
    // Executes a fixed set of actions in parallel
    class CompoundActionParallel : public ICompoundAction
    {
    public:
      CompoundActionParallel(Robot& robot);
      CompoundActionParallel(Robot& robot, std::list<IActionRunner*> actions);
      
    protected:
      
      virtual ActionResult UpdateInternal() override final;
      
    }; // class CompoundActionParallel
    
  } // namespace Cozmo
} // namespace Anki

#endif // ANKI_COZMO_COMPOUND_ACTIONS_H



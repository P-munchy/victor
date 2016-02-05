/**
 * File: actionContainers.h
 *
 * Author: Andrew Stein
 * Date:   8/29/2014
 *
 * Description: Defines containers for running actions, both as a queue and a 
 *              concurrent list.
 *
 *
 * Copyright: Anki, Inc. 2014
 **/

#ifndef ANKI_COZMO_ACTION_CONTAINERS_H
#define ANKI_COZMO_ACTION_CONTAINERS_H

#include "anki/common/types.h"

#include "clad/types/actionTypes.h"

#include <list>
#include <map>

// TODO: Is this Cozmo-specific or can it be moved to coretech?
// (Note it does require a Robot, which is currently only present in Cozmo)

namespace Anki {
  
  namespace Cozmo {

    // Forward declarations:
    class Robot;
    class IActionRunner;
    
    // This is an ordered list of actions to be run. It is similar to an
    // CompoundActionSequential, but actions can be added to it dynamically,
    // either "next" or at the end of the queue. As actions are completed,
    // they are popped off the queue. Thus, when it is empty, it is "done".
    class ActionQueue
    {
    public:
      ActionQueue();
      
      ~ActionQueue();
      
      Result   Update();
      
      // Queue action to run right after the current action, before anything else in the queue
      Result   QueueNext(IActionRunner    *action, u8 numRetries = 0);
      
      // Queue action to run after everything else currently in the queue
      Result   QueueAtEnd(IActionRunner   *action, u8 numRetires = 0);
      
      // Cancel the current action and immediately run the new action, preserving rest of queue
      Result   QueueNow(IActionRunner     *action, u8 numRetries = 0);
      
      // Stop current action and reset it, insert new action at the front, leaving
      // current action in the queue to run fresh next (after this newly-inserted action)
      Result   QueueAtFront(IActionRunner *action, u8 numRetries = 0);
      
      // Blindly clear the queue
      void     Clear();
      
      bool     Cancel(RobotActionType withType = RobotActionType::UNKNOWN);
      bool     Cancel(u32 idTag);
      
      bool     IsEmpty() const { return _queue.empty() && nullptr == _currentAction; }
      
      bool     IsDuplicate(IActionRunner* action);
      
      size_t   Length() const { return _queue.size(); }
      
      IActionRunner* GetNextActionToRun();
      const IActionRunner* GetCurrentAction() const;
      const IActionRunner* GetCurrentRunningAction() const { return _currentAction; }
      
      void Print() const;
      
    private:
      // Deletes the current action only if it isn't in the process of being deleted
      // Safeguards against the action being deleted twice due to handling action
      // completion signals
      void DeleteCurrentAction();
    
      IActionRunner*            _currentAction           = nullptr;
      bool                      _currentActionIsDeleting = false;
      std::list<IActionRunner*> _queue;
      
    }; // class ActionQueue
    
    
    // This is a list of concurrent actions to be run, addressable by ID handle.
    // Each slot in the list is really a queue, to which new actions can be added
    // using that slot's ID handle. When a slot finishes, it is popped.
    class ActionList
    {
    public:
      using SlotHandle = s32;
      
      static const SlotHandle UnknownSlot = -1;
      
      ActionList(Robot& robot);
      ~ActionList();
      
      // Updates the current action of each queue in each slot
      Result     Update();
      
      // Add a new action to be run concurrently, generating a new slot, whose
      // handle is returned. If there is no desire to queue anything to run after
      // this action, the returned SlotHandle can be ignored.
      SlotHandle AddConcurrentAction(IActionRunner* action, u8 numRetries = 0);
      
      // Queue an action
      // These wrap correspondong QueueFoo() methods in ActionQueue.
      Result     QueueActionNext(IActionRunner* action, u8 numRetries = 0);
      Result     QueueActionAtEnd(IActionRunner* action, u8 numRetries = 0);
      Result     QueueActionNow(IActionRunner* action, u8 numRetries = 0);
      Result     QueueActionAtFront(IActionRunner* action, u8 numRetries = 0);
      
      Result     QueueAction(QueueActionPosition inPosition,
                             IActionRunner* action, u8 numRetries = 0);
      
      bool       IsEmpty() const;
      
      size_t     GetQueueLength(SlotHandle atSlot);
      
      size_t     GetNumQueues();

      // Only cancels with the specified type. All slots are searched.
      // Returns true if any actions were cancelled.
      bool       Cancel(RobotActionType withType = RobotActionType::UNKNOWN);
      
      // Find and cancel the action with the specified ID Tag. All slots are searched.
      // Returns true if the action was found and cancelled.
      bool       Cancel(u32 idTag);
      
      void       Print() const;
      
      // Returns true if actionName is the name of one of the actions that are currently
      // being executed.
      bool       IsCurrAction(const std::string& actionName) const;

      // Returns true if the passed in action tag matches the action currently playing in the given slot
      bool       IsCurrAction(u32 idTag, SlotHandle fromSlot = 0) const;
      
      // Returns true if this is a duplicate action
      bool       IsDuplicate(IActionRunner* action);
      
    protected:
      // Blindly clears out the contents of the action list
      void       Clear();
      
      std::map<SlotHandle, ActionQueue> _queues;
      
    private:
      Robot* _robot = nullptr;
      
    }; // class ActionList
    
    // The current running action should be considered part of the queue
    inline size_t ActionList::GetQueueLength(SlotHandle atSlot)
    {
      return _queues[atSlot].Length() + (nullptr == _queues[atSlot].GetCurrentRunningAction() ? 0 : 1);
    }
    
    inline size_t ActionList::GetNumQueues()
    {
      return _queues.size();
    }
    
  } // namespace Cozmo
} // namespace Anki

#endif // ANKI_COZMO_ACTION_CONTAINERS_H

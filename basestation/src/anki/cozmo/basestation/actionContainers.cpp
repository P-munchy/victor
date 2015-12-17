/**
 * File: actionContainers.cpp
 *
 * Author: Andrew Stein
 * Date:   8/29/2014
 *
 * Description: Implements containers for running actions, both as a queue and a
 *              concurrent list.
 *
 *
 * Copyright: Anki, Inc. 2014
 **/

#include "anki/cozmo/basestation/actionContainers.h"
#include "anki/cozmo/basestation/actionInterface.h"

#include "util/logging/logging.h"

namespace Anki {
  namespace Cozmo {
    
#pragma mark ---- ActionList ----
    
    ActionList::ActionList()
    {
      
    }
    
    ActionList::~ActionList()
    {
      Clear();
    }
    
    bool ActionList::Cancel(SlotHandle fromSlot, RobotActionType withType)
    {
      bool found = false;
      
      // Clear specified slot / type
      for(auto & q : _queues) {
        if(fromSlot == -1 || q.first == fromSlot) {
          found |= q.second.Cancel(withType);
        }
      }
      return found;
    }
    
    bool ActionList::Cancel(u32 idTag, SlotHandle fromSlot)
    {
      bool found = false;
      
      if(fromSlot == -1) {
        for(auto & q : _queues) {
          if(q.second.Cancel(idTag) == true) {
            if(found) {
              PRINT_NAMED_WARNING("ActionList.Cancel.DuplicateTags",
                                  "Multiple actions from multiple slots cancelled with idTag=%d.\n", idTag);
            }
            found = true;
          }
        }
        return found;
      } else {
        auto q = _queues.find(fromSlot);
        if(q != _queues.end()) {
          found = q->second.Cancel(idTag);
        } else {
          PRINT_NAMED_WARNING("ActionList.Cancel.NoSlot", "No slot with handle %d.\n", fromSlot);
        }
      }
      
      return found;
    }
    
    void ActionList::Clear()
    {
      _queues.clear();
    }
    
    bool ActionList::IsEmpty() const
    {
      return _queues.empty();
    }
    
    void ActionList::Print() const
    {
      if(IsEmpty()) {
        PRINT_STREAM_INFO("ActionList.Print", "ActionList is empty.\n");
      } else {
        PRINT_STREAM_INFO("ActionList.Print", "ActionList contains " << _queues.size() << " queues:\n");
        for(auto const& queuePair : _queues) {
          queuePair.second.Print();
        }
      }
      
    } // Print()
    
    Result ActionList::Update(Robot& robot)
    {
      Result lastResult = RESULT_OK;
      
      for(auto queueIter = _queues.begin(); queueIter != _queues.end(); )
      {
        lastResult = queueIter->second.Update(robot);
        
        // If the queue is complete, remove it
        if(queueIter->second.IsEmpty()) {
          queueIter = _queues.erase(queueIter);
        } else {
          ++queueIter;
        }
      } // for each actionMemberPair
      
      return lastResult;
    } // Update()
    
    
    ActionList::SlotHandle ActionList::AddConcurrentAction(IActionRunner* action, u8 numRetries)
    {
      if(action == nullptr) {
        PRINT_NAMED_WARNING("ActionList.AddAction.NullActionPointer", "Refusing to add null action.\n");
        return -1;
      }
      
      // Find an empty slot
      SlotHandle currentSlot = 0;
      while(_queues.find(currentSlot) != _queues.end()) {
        ++currentSlot;
      }
      
      if(_queues[currentSlot].QueueAtEnd(action, numRetries) != RESULT_OK) {
        PRINT_NAMED_ERROR("ActionList.AddAction.FailedToAdd", "Failed to add action to new queue.\n");
      }
      
      return currentSlot;
    }
    
    bool ActionList::IsCurrAction(const std::string& actionName) const
    {
      for(auto queueIter = _queues.begin(); queueIter != _queues.end();  ++queueIter)
      {
        if (nullptr == queueIter->second.GetCurrentAction()) {
          return false;
        }
        if (queueIter->second.GetCurrentAction()->GetName() == actionName) {
          return true;
        }
      }
      return false;
    }

    bool ActionList::IsCurrAction(u32 idTag, SlotHandle fromSlot) const
    {
      const auto qIter = _queues.find(fromSlot);
      if( qIter == _queues.end() ) {
        // can't be playing if the slot doesn't exist
        return false;
      }

      if( nullptr == qIter->second.GetCurrentAction() ) {
        return false;
      }

      return qIter->second.GetCurrentAction()->GetTag() == idTag;
    }

#pragma mark ---- ActionQueue ----
    
    ActionQueue::ActionQueue()
    {
      
    }
    
    ActionQueue::~ActionQueue()
    {
      Clear();
    }
    
    void ActionQueue::Clear()
    {
      while(!_queue.empty()) {
        IActionRunner* action = _queue.front();
        CORETECH_ASSERT(action != nullptr);
        delete action;
        _queue.pop_front();
      }
    }

    bool ActionQueue::Cancel(RobotActionType withType)
    {
      bool found = false;
      for(auto action : _queue)
      {
        CORETECH_ASSERT(action != nullptr);
        
        if(withType == RobotActionType::UNKNOWN || action->GetType() == withType) {
          action->Cancel();
          found = true;
        }
      }
      return found;
    }
    
    bool ActionQueue::Cancel(u32 idTag)
    {
      bool found = false;
      for(auto action : _queue)
      {
        if(action->GetTag() == idTag) {
          if(found == true) {
            PRINT_NAMED_WARNING("ActionQueue.Cancel.DuplicateIdTags",
                                "Multiple actions with tag=%d found in queue.\n",
                                idTag);
          }
          action->Cancel();
          found = true;
        }
      }
      
      return found;
    }

    Result ActionQueue::QueueNow(IActionRunner *action, u8 numRetries)
    {
      if(action == nullptr) {
        PRINT_NAMED_ERROR("ActionQueue.QueueNow.NullActionPointer",
                          "Refusing to queue a null action pointer.\n");
        return RESULT_FAIL;
      }
      
      if(_queue.empty()) {
        
        // Nothing in the queue, so this is the same as QueueAtEnd
        return QueueAtEnd(action, numRetries);
        
      } else {
        // Cancel whatever is running now and then queue this to happen next
        // (right after any cleanup due to the cancellation completes)
        _queue.front()->Cancel();
        return QueueNext(action, numRetries);
      }
    }
    
    Result ActionQueue::QueueAtFront(IActionRunner* action, u8 numRetries)
    {
      if(action == nullptr) {
        PRINT_NAMED_ERROR("ActionQueue.QueueAFront.NullActionPointer",
                          "Refusing to queue a null action pointer.\n");
        return RESULT_FAIL;
      }
      
      Result result = RESULT_OK;
      
      if(_queue.empty()) {
        // Nothing in the queue, so this is the same as QueueAtEnd
        result = QueueAtEnd(action, numRetries);
      } else {
        // Try to interrupt whatever is running and put this new action in front of it
        if(_queue.front()->Interrupt()) {
          // Current front action is interruptible. Reset it so it's ready to be
          // re-run and put the new action in front of it in the queue.
          PRINT_NAMED_INFO("ActionQueue.QueueAtFront.Interrupt",
                           "Interrupting %s to put %s in front of it.",
                           _queue.front()->GetName().c_str(),
                           action->GetName().c_str());
          _queue.front()->Reset();
          action->SetNumRetries(numRetries);
          _queue.push_front(action);
        } else {
          // Current front action is not interruptible, so just use QueueNow and
          // cancel it
          result = QueueNow(action, numRetries);
        }
      }
      
      return result;
    }
    
    Result ActionQueue::QueueAtEnd(IActionRunner *action, u8 numRetries)
    {
      if(action == nullptr) {
        PRINT_NAMED_ERROR("ActionQueue.QueueAtEnd.NullActionPointer",
                          "Refusing to queue a null action pointer.\n");
        return RESULT_FAIL;
      }
      
      action->SetNumRetries(numRetries);
      _queue.push_back(action);
      return RESULT_OK;
    }
    
    Result ActionQueue::QueueNext(IActionRunner *action, u8 numRetries)
    {
      if(action == nullptr) {
        PRINT_NAMED_ERROR("ActionQueue.QueueNext.NullActionPointer",
                          "Refusing to queue a null action pointer.\n");
        return RESULT_FAIL;
      }
      
      action->SetNumRetries(numRetries);
      
      if(_queue.empty()) {
        return QueueAtEnd(action, numRetries);
      }
      
      std::list<IActionRunner*>::iterator queueIter = _queue.begin();
      ++queueIter;
      _queue.insert(queueIter, action);
      
      return RESULT_OK;
    }
    
    Result ActionQueue::Update(Robot& robot)
    {
      Result lastResult = RESULT_OK;
      
      if(!_queue.empty())
      {
        IActionRunner* currentAction = GetCurrentAction();
        assert(currentAction != nullptr);
        
        VizManager::getInstance()->SetText(VizManager::ACTION, NamedColors::GREEN,
                                           "Action: %s", currentAction->GetName().c_str());
        
        const ActionResult actionResult = currentAction->Update(robot);
        
        if(actionResult != ActionResult::RUNNING) {
          // Current action just finished, pop it
          PopCurrentAction();
          
          if(actionResult != ActionResult::SUCCESS && actionResult != ActionResult::CANCELLED) {
            lastResult = RESULT_FAIL;
          }
          
          VizManager::getInstance()->SetText(VizManager::ACTION, NamedColors::GREEN, "");
        }
      } // if queue not empty
      
      return lastResult;
    }
    
    IActionRunner* ActionQueue::GetCurrentAction()
    {
      if(_queue.empty()) {
        return nullptr;
      }
      
      return _queue.front();
    }

    const IActionRunner* ActionQueue::GetCurrentAction() const
    {
      if(_queue.empty()) {
        return nullptr;
      }
      
      return _queue.front();
    }

    void ActionQueue::PopCurrentAction()
    {
      if(!IsEmpty()) {
        if(_queue.front() == nullptr) {
          PRINT_NAMED_ERROR("ActionQueue.PopCurrentAction.NullActionPointer",
                            "About to delete and pop action pointer from queue, found it to be nullptr!\n");
        } else {
          delete _queue.front();
        }
        _queue.pop_front();
      }
    }
    
    void ActionQueue::Print() const
    {
      
      if(IsEmpty()) {
        PRINT_STREAM_INFO("ActionQueue.Print", "ActionQueue is empty.\n");
      } else {
        std::stringstream ss;
        ss << "ActionQueue with " << _queue.size() << " actions: ";
        for(auto action : _queue) {
          ss << action->GetName() << ", ";
        }
        PRINT_STREAM_INFO("ActionQueue.Print", ss.str());
      }
      
    } // Print()
    
  } // namespace Cozmo
} // namespace Anki


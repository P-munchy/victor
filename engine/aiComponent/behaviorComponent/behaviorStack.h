/**
* File: behaviorSystemManager.h
*
* Author: Kevin Karol
* Date:   8/17/2017
*
* Description: Manages and enforces the lifecycle and transitions
* of parts of the behavior system
*
* Copyright: Anki, Inc. 2017
**/

#ifndef AI_COMPONENT_BEHAVIOR_COMPONENT_BEHAVIOR_STACK
#define AI_COMPONENT_BEHAVIOR_COMPONENT_BEHAVIOR_STACK

#include "util/helpers/noncopyable.h"

#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

namespace Anki {
namespace Cozmo {

// forward declarations
class AsyncMessageGateComponent;
class BehaviorExternalInterface;
class IBehavior;
  
namespace ExternalInterface{
struct RobotCompletedAction;
}
  
class BehaviorStack : private Util::noncopyable {
public:
  BehaviorStack(BehaviorExternalInterface* behaviorExternalInterface)
  :_behaviorExternalInterface(behaviorExternalInterface){};
  virtual ~BehaviorStack();
  
  void InitBehaviorStack(BehaviorExternalInterface& behaviorExternalInterface,
                         IBehavior* baseOfStack);
  // Clear the stack if it needs to be re-initialized
  void ClearStack();
  
  void UpdateBehaviorStack(BehaviorExternalInterface& behaviorExternalInterface,
                          std::vector<ExternalInterface::RobotCompletedAction>& actionsCompletedThisTick,
                           AsyncMessageGateComponent& asyncMessageGateComp,
                           std::set<IBehavior*>& tickedInStack);
  
  inline IBehavior* GetTopOfStack(){ return _behaviorStack.empty() ? nullptr : _behaviorStack.back();}
  inline bool IsInStack(const IBehavior* behavior) { return _behaviorToIndexMap.find(behavior) != _behaviorToIndexMap.end();}
  
  // if the passed in behavior is in the stack, return a pointer to the behavior which is above it in the
  // stack, or null if it is at the top or not in the stack
  const IBehavior* GetBehaviorInStackAbove(const IBehavior* behavior) const;

  void PushOntoStack(IBehavior* behavior);
  void PopStack();
  
  using DelegatesMap = std::map<IBehavior*,std::set<IBehavior*>>;
  const DelegatesMap& GetDelegatesMap(){ return _delegatesMap;}
  
  // for debug only, prints stack info
  void DebugPrintStack(const std::string& debugStr) const;

  // in debug builds, send viz messages to webots
  void SendDebugVizMessages(BehaviorExternalInterface& behaviorExternalInterface) const;
  
private:
  BehaviorExternalInterface* _behaviorExternalInterface;
  std::vector<IBehavior*> _behaviorStack;
  std::unordered_map<const IBehavior*, int> _behaviorToIndexMap;
  std::map<IBehavior*,std::set<IBehavior*>> _delegatesMap;
  
  
  
  // calls all appropriate functions to prep the delegates of something about to be added to the stack
  void PrepareDelegatesToEnterScope(IBehavior* delegated);
  
  // calls all appropriate functions to prepare a delegate to be removed from the stack
  void PrepareDelegatesForRemovalFromStack(IBehavior* delegated);
};


} // namespace Cozmo
} // namespace Anki


#endif // AI_COMPONENT_BEHAVIOR_COMPONENT_BEHAVIOR_STACK

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

#ifndef COZMO_BEHAVIOR_SYSTEM_MANAGER_H
#define COZMO_BEHAVIOR_SYSTEM_MANAGER_H

#include "anki/common/types.h"
#include "clad/types/behaviorComponent/behaviorTypes.h"


#include "engine/aiComponent/behaviorComponent/asyncMessageGateComponent.h"
#include "engine/aiComponent/behaviorComponent/behaviors/iCozmoBehavior_fwd.h"
#include "engine/aiComponent/behaviorComponent/iBehaviorRunner.h"
#include "engine/aiComponent/behaviorComponent/behaviorStack.h"
#include "json/json-forwards.h"
#include "util/signals/simpleSignal_fwd.h"

#include <set>
#include <unordered_map>

namespace Anki {
namespace Cozmo {

// Forward declarations
class BehaviorExternalInterface;
class IBehavior;

struct BehaviorRunningInfo;

class BehaviorSystemManager : public IBehaviorRunner, private Util::noncopyable
{
public:
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Initialization/Destruction
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  
  BehaviorSystemManager();
  virtual ~BehaviorSystemManager();
  
  // initialize this behavior manager from the given Json config
  Result InitConfiguration(IBehavior* baseBehavior,
                           BehaviorExternalInterface& behaviorExternalInterface,
                           AsyncMessageGateComponent* asyncMessageComponent);
  
  // destroy the current behavior stack and setup a new one - provides
  // no gaurentees that other aspects of behavior system or component state are reset
  void ResetBehaviorStack(IBehavior* baseBehavior);
  
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  //
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  
  // Calls the current behavior's Update() method until it returns COMPLETE or FAILURE.
  void Update(BehaviorExternalInterface& behaviorExternalInterface) override;
  
  bool IsControlDelegated(const IBehavior* delegator) override;
  bool CanDelegate(IBehavior* delegator) override;
  bool Delegate(IBehavior* delegator, IBehavior* delegated) override;
  void CancelDelegates(IBehavior* delegator) override;
  void CancelSelf(IBehavior* delegator) override;

  // If control of the passed in behavior is delegated (to another behavior), return the pointer of the
  // behavior that it was delegated to. Otherwise, return nullptr (including if control was delegated to an
  // action or helper)
  const IBehavior* GetBehaviorDelegatedTo(const IBehavior* delegatingBehavior) const;

private:
  enum class InitializationStage{
    SystemNotInitialized,
    StackNotInitialized,
    Initialized
  };
  
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Attributes
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  InitializationStage _initializationStage;
  // Store the base behavior until the stack is initialized
  IBehavior* _baseBehaviorTmp;
  
  // - - - - - - - - - - - - - - -
  // others/shared
  // - - - - - - - - - - - - - - -
  BehaviorExternalInterface* _behaviorExternalInterface;
  
  AsyncMessageGateComponent* _asyncMessageComponent;
  std::vector<ExternalInterface::RobotCompletedAction> _actionsCompletedThisTick;
  std::vector<Signal::SmartHandle> _eventHandles;

  std::unique_ptr<BehaviorStack> _behaviorStack;

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Methods
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  
  void UpdateInActivatableScope(BehaviorExternalInterface& behaviorExternalInterface, const std::set<IBehavior*>& tickedInStack);
  
}; // class BehaviorSystemManager

} // namespace Cozmo
} // namespace Anki


#endif // COZMO_BEHAVIOR_SYSTEM_MANAGER_H

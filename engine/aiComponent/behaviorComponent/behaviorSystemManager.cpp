/**
* File: behaviorSystemManager.cpp
*
* Author: Kevin Karol
* Date:   8/17/2017
*
* Description: Manages and enforces the lifecycle and transitions
* of parts of the behavior system
*
* Copyright: Anki, Inc. 2017
**/

#include "engine/aiComponent/behaviorComponent/behaviorSystemManager.h"

#include "engine/actions/actionContainers.h"
#include "engine/aiComponent/behaviorComponent/asyncMessageGateComponent.h"
#include "engine/aiComponent/behaviorComponent/behaviorContainer.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/behaviorExternalInterface.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/delegationComponent.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/behaviorEventComponent.h"
#include "engine/aiComponent/behaviorComponent/behaviors/iCozmoBehavior.h"
#include "engine/aiComponent/behaviorComponent/behaviorTypesWrapper.h"
#include "engine/aiComponent/behaviorComponent/iBehavior.h"
#include "engine/externalInterface/externalInterface.h"
#include "engine/robot.h"
#include "engine/robotDataLoader.h"
#include "engine/viz/vizManager.h"

#include "util/cpuProfiler/cpuProfiler.h"
#include "util/helpers/boundedWhile.h"
#include "util/logging/logging.h"

namespace Anki {
namespace Cozmo {

// Forward declaration
class IReactionTriggerStrategy;

namespace{
const int kArbitrarilyLargeCancelBound = 1000000;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorSystemManager::BehaviorSystemManager()
: IDependencyManagedComponent(this, BCComponentID::BehaviorSystemManager)
, _initializationStage(InitializationStage::SystemNotInitialized)
{
  _behaviorStack.reset();
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorSystemManager::~BehaviorSystemManager()
{

}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorSystemManager::InitDependent(Robot* robot, const BCCompMap& dependentComponents)
{
  auto& baseBehaviorWrapper = dependentComponents.GetValue<BaseBehaviorWrapper>();
  auto& bei = dependentComponents.GetValue<BehaviorExternalInterface>();
  auto& async = dependentComponents.GetValue<AsyncMessageGateComponent>();

  InitConfiguration(*robot,
                    baseBehaviorWrapper._baseBehavior,
                    bei,
                    &async);
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Result BehaviorSystemManager::InitConfiguration(Robot& robot,
                                                IBehavior* baseBehavior,
                                                BehaviorExternalInterface& behaviorExternalInterface,
                                                AsyncMessageGateComponent* asyncMessageComponent)
{
  // do not support multiple initialization. A) we don't need it, B) it's easy to forget to clean up everything properly
  // when adding new stuff. During my refactoring I found several variables that were not properly reset, so
  // potentially double Init was never supported
  DEV_ASSERT(_initializationStage == InitializationStage::SystemNotInitialized &&
             baseBehavior != nullptr,
             "BehaviorSystemManager.InitConfiguration.AlreadyInitialized");

  // If this is the factory test forcibly set baseBehavior as playpen
  if(FACTORY_TEST)
  {
    baseBehavior = behaviorExternalInterface.GetBehaviorContainer().FindBehaviorByID(BEHAVIOR_ID(PlaypenTest)).get();
    DEV_ASSERT(baseBehavior != nullptr, "BehaviorSystemManager.InitConfiguration.ForcingPlaypen.Null");
  }

  // Assumes there's only one instance of the behavior external Intarfec
  _behaviorExternalInterface = &behaviorExternalInterface;
  _asyncMessageComponent = asyncMessageComponent;
  ResetBehaviorStack(baseBehavior);
  
  if(robot.HasExternalInterface()){
    _eventHandles.push_back(robot.GetExternalInterface()->Subscribe(EngineToGameTag::RobotCompletedAction,
                                            [this](const EngineToGameEvent& event) {
                                              DEV_ASSERT(event.GetData().GetTag() == EngineToGameTag::RobotCompletedAction,
                                                         "ICozmoBehavior.RobotCompletedAction.WrongEventTypeFromCallback");
                                              _actionsCompletedThisTick.push_back(event.GetData().Get_RobotCompletedAction());
                                            }));
  }
  
  return RESULT_OK;
}

  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorSystemManager::ResetBehaviorStack(IBehavior* baseBehavior)
{
  _initializationStage = InitializationStage::StackNotInitialized;
  _baseBehaviorTmp = baseBehavior;
  if(_behaviorStack != nullptr){
    _behaviorStack->ClearStack();
  }
  _behaviorStack.reset(new BehaviorStack());
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorSystemManager::UpdateDependent(const BCCompMap& dependentComponents)
{
  auto& bei = dependentComponents.GetValue<BehaviorExternalInterface>();
  ANKI_CPU_PROFILE("BehaviorSystemManager::Update");
  
  if(_initializationStage == InitializationStage::SystemNotInitialized) {
    PRINT_NAMED_ERROR("BehaviorSystemManager.Update.NotInitialized", "");
    return;
  }
  
  // There's a delay between init and first robot update tick - this messes with
  // time checks in IBehavior, so Activate the base here instead of in init
  if(_initializationStage == InitializationStage::StackNotInitialized){
    _initializationStage = InitializationStage::Initialized;


    IBehavior* baseBehavior = _baseBehaviorTmp;
    
    _behaviorStack->InitBehaviorStack(baseBehavior);
    _baseBehaviorTmp = nullptr;
  }

  for( const auto& completionMsg : _actionsCompletedThisTick ) {
    bei.GetDelegationComponent().HandleActionComplete( completionMsg.idTag );
  }
  
  _asyncMessageComponent->PrepareCache();
  
  std::set<IBehavior*> behaviorsUpdatesTickedInStack;
  // First update the behavior stack and allow it to make any delegation/canceling
  // decisions that it needs to make
  _behaviorStack->UpdateBehaviorStack(bei,
                                      _actionsCompletedThisTick,
                                      *_asyncMessageComponent,
                                      behaviorsUpdatesTickedInStack);
  _actionsCompletedThisTick.clear();
  // Then once all of that's done, update anything that's in activatable scope
  // but isn't currently on the behavior stack
  UpdateInActivatableScope(bei, behaviorsUpdatesTickedInStack);
  
  _asyncMessageComponent->ClearCache();
} // Update()


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorSystemManager::UpdateInActivatableScope(BehaviorExternalInterface& behaviorExternalInterface, const std::set<IBehavior*>& tickedInStack)
{
  // This is innefficient and should be replaced, but not overengineering right now
  const auto& allInActivatableScope = _behaviorStack->GetBehaviorsInActivatableScope();;

  for(auto& entry: allInActivatableScope){
    if(tickedInStack.find(entry) != tickedInStack.end()){
      continue;
    }
    
    behaviorExternalInterface.GetBehaviorEventComponent()._gameToEngineEvents.clear();
    behaviorExternalInterface.GetBehaviorEventComponent()._engineToGameEvents.clear();
    behaviorExternalInterface.GetBehaviorEventComponent()._robotToEngineEvents.clear();

    _asyncMessageComponent->GetEventsForBehavior(
       entry,
       behaviorExternalInterface.GetBehaviorEventComponent()._gameToEngineEvents);
    _asyncMessageComponent->GetEventsForBehavior(
       entry,
       behaviorExternalInterface.GetBehaviorEventComponent()._engineToGameEvents);
    _asyncMessageComponent->GetEventsForBehavior(
       entry,
       behaviorExternalInterface.GetBehaviorEventComponent()._robotToEngineEvents);

    entry->Update();
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorSystemManager::IsControlDelegated(const IBehavior* delegator)
{
  return (_behaviorStack->IsInStack(delegator)) &&
         (_behaviorStack->GetTopOfStack() != delegator);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
const IBehavior* BehaviorSystemManager::GetBehaviorDelegatedTo(const IBehavior* delegatingBehavior) const
{
  return _behaviorStack->GetBehaviorInStackAbove(delegatingBehavior);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Json::Value BehaviorSystemManager::BuildDebugBehaviorTree(BehaviorExternalInterface& bei) const
{
  if( _behaviorStack != nullptr ) {
    return _behaviorStack->BuildDebugBehaviorTree( bei );
  } else {
    return {};
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorSystemManager::CanDelegate(IBehavior* delegator)
{
  return _behaviorStack->GetTopOfStack() == delegator;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorSystemManager::Delegate(IBehavior* delegator, IBehavior* delegated)
{
  // Ensure that the delegator is on top of the stack
  if(!ANKI_VERIFY(delegator == _behaviorStack->GetTopOfStack(),
                  "BehaviorSystemManager.Delegate.DelegatorNotOnTopOfStack",
                  "")){
    return false;
  }
  
  if(!ANKI_VERIFY(delegated != nullptr,
                  "BehaviorSystemManager.Delegate.DelegatingToNullptr", "")){
    return false;
  }
  
  
  {
    // Ensure that the delegated behavior is in the delegates map
    if(!ANKI_VERIFY(_behaviorStack->IsValidDelegation(delegator, delegated),
                   "BehaviorSystemManager.Delegate.DelegateNotInAvailableDelegateMap",
                   "Delegator %s asked to delegate to %s which is not in available delegates map",
                   delegator->GetDebugLabel().c_str(),
                   delegated->GetDebugLabel().c_str())){
      return false;
    }
  }
  
  PRINT_CH_INFO("BehaviorSystem", "BehaviorSystemManager.Delegate.ToBehavior",
                "'%s' will delegate to '%s'",
                delegator != nullptr ? delegator->GetDebugLabel().c_str() : "Empty Stack",
                delegated->GetDebugLabel().c_str());

  // Activate the new behavior and add it to the top of the stack
  _behaviorStack->PushOntoStack(delegated);
  
  _behaviorStack->DebugPrintStack("AfterDelegation");
  
  return true;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorSystemManager::CancelDelegates(IBehavior* delegator)
{
  if(_behaviorStack->IsInStack(delegator)){
    BOUNDED_WHILE(kArbitrarilyLargeCancelBound,
                  _behaviorStack->GetTopOfStack() != delegator){
      _behaviorStack->PopStack();
    }
  }

  PRINT_CH_INFO("BehaviorSystem", "BehaviorSystemManager.CancelDelegates",
                "'%s' canceled its delegates",
                delegator->GetDebugLabel().c_str());

  _behaviorStack->DebugPrintStack("AfterCancelDelgates");
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// TODO:(bn) kevink: consider rename to "stop" rather than cancel
void BehaviorSystemManager::CancelSelf(IBehavior* delegator)
{
  if(!ANKI_VERIFY(_behaviorStack->IsInStack(delegator),
                  "BehaviorSystemManager.CancelSelf.NotINStack",
                  "%s is not in stack",
                  delegator->GetDebugLabel().c_str())){
    return;
  }
  
  CancelDelegates(delegator);
  
  if(ANKI_VERIFY(!IsControlDelegated(delegator),
                 "BehaviorSystemManager.CancelSelf.ControlStillDelegated",
                 "CancelDelegates was called, but the delegator is not on the top of the stack")){
    _behaviorStack->PopStack();
  }

  PRINT_CH_INFO("BehaviorSystem", "BehaviorSystemManager.CancelSelf",
                "'%s' canceled itself",
                delegator->GetDebugLabel().c_str());

  _behaviorStack->DebugPrintStack("AfterCancelSelf");
}

} // namespace Cozmo
} // namespace Anki

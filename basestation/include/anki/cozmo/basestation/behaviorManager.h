/**
 * File: behaviorManager.h
 *
 * Author: Kevin Yoon
 * Date:   2/27/2014
 *
 * Overhaul: 7/30/15 Andrew Stein, 2016-04-18 Brad Neuman
 *
 * Description: High-level module that is a container for available behaviors,
 *              determines what behavior a robot should be executing at a given
 *              time and handles ticking the running behavior forward.
 *
 * Copyright: Anki, Inc. 2014-2016
 **/

#ifndef COZMO_BEHAVIOR_MANAGER_H
#define COZMO_BEHAVIOR_MANAGER_H

#include "anki/common/types.h"

#include "clad/types/behaviorTypes.h"
#include "clad/types/unlockTypes.h"


#include "util/helpers/templateHelpers.h"
#include "util/random/randomGenerator.h"
#include "util/signals/simpleSignal_fwd.h"

#include "json/json-forwards.h"

#include <assert.h>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace Anki {
namespace Cozmo {
  
// Forward declaration
class BehaviorFactory;
class AIWhiteboard;
class AIGoalEvaluator;
class IBehavior;
class IBehaviorChooser;
class IReactionaryBehavior;
class Robot;
template<typename TYPE> class AnkiEvent;

  
namespace ExternalInterface {
class BehaviorManagerMessageUnion;
}

class BehaviorManager
{
public:

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Initialization/Destruction
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  
  BehaviorManager(Robot& robot);
  ~BehaviorManager();

  // initialize this behavior manager from the given Json config
  Result InitConfiguration(const Json::Value& config);
  
  // create a behavior from the given config and add to the factory. Can fail if the configuration is not valid
  Result CreateBehaviorFromConfiguration(const Json::Value& behaviorConfig);
  
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Games
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  
  // get if the given flags are available, you can check for all or if any is set
  inline bool AreAllGameFlagsAvailable(BehaviorGameFlag gameFlag) const;
  inline bool IsAnyGameFlagAvailable(BehaviorGameFlag gameFlag) const;
  UnlockId GetActiveSpark() const { return _activeSpark; };
  
  // sets which games are available by setting the mask/flag combination
  void SetAvailableGame(BehaviorGameFlag availableGames) { _availableGames = Util::EnumToUnderlying(availableGames); }
  
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  //
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

  // Calls the current behavior's Update() method until it returns COMPLETE or FAILURE.
  Result Update();
        
  // Set the current IBehaviorChooser. Note this results in the destruction of the current IBehaviorChooser
  // (if there is one) and the IBehaviors it contains
  void SetBehaviorChooser(IBehaviorChooser* newChooser);
  
  // returns last time we changerd behavior chooser
  float GetLastBehaviorChooserSwitchTime() const { return _lastChooserSwitchTime; }

  // Stops the current behavior and switches to null. The next time Update is called, the behavior chooser
  // will have a chance to select a new behavior. This is mostly useful as a hack to force a behavior switch
  // when it is needed (e.g. demo). `stoppedByWhom` is a string for debugging so we know why this was stopped
  void ForceStopCurrentBehavior(const std::string& stoppedByWhom);
  
  // Returns nullptr if there is no current behavior
  const IBehavior* GetCurrentBehavior() const { return _currentBehavior; }

  const IBehaviorChooser* GetBehaviorChooser() const { return _currentChooserPtr; }
    
  const BehaviorFactory& GetBehaviorFactory() const { assert(_behaviorFactory); return *_behaviorFactory; }
        BehaviorFactory& GetBehaviorFactory()       { assert(_behaviorFactory); return *_behaviorFactory; }
  
  // Enable and disable reactionary behaviors
  void RequestEnableReactionaryBehavior(std::string requesterID, BehaviorType behavior, bool enable);
  
  // accessors: whiteboard
  const AIWhiteboard& GetWhiteboard() const { assert(_whiteboard); return *_whiteboard; }
        AIWhiteboard& GetWhiteboard()       { assert(_whiteboard); return *_whiteboard; }
     
  void HandleMessage(const Anki::Cozmo::ExternalInterface::BehaviorManagerMessageUnion& message);

private:

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Types
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Methods
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

  // switches to a given behavior (stopping the current behavior if necessary). Returns true if it switched
  bool SwitchToBehavior(IBehavior* nextBehavior);

  // same as SwitchToBehavior but also handles special reactionary logic
  void SwitchToReactionaryBehavior(IBehavior* nextBehavior);
  

  
  // checks the chooser and switches to a new behavior if neccesary
  void SwitchToNextBehavior();

  // If there is a behavior to resume, switch to it and return true. Otherwise return false
  bool ResumeBehaviorIfNeeded();

  // stop the current behavior if it is non-null and running (i.e. Init was called)
  void StopCurrentBehavior();

  void SendDasTransitionMessage(IBehavior* oldBehavior, IBehavior* newBehavior);
  
  void AddReactionaryBehavior(IReactionaryBehavior* behavior);

  // check if there is a matching reactionary behavior which wants to run for the given event, and switch to
  // it if so
  template<typename EventType>
  void ConsiderReactionaryBehaviorForEvent(const AnkiEvent<EventType>& event);

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Sparks
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  
  // sets the given spark as currently active
  void SetActiveSpark(UnlockId spark);
  
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Attributes
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

  bool _isInitialized = false;
    
  Robot& _robot;
  
  // - - - - - - - - - - - - - - -
  // current running behavior
  // - - - - - - - - - - - - - - -
  
  bool _runningReactionaryBehavior = false;
  
  // behavior currently running and responsible for controlling the robot
  IBehavior* _currentBehavior  = nullptr;
  
  // this is the behavior to go back to after a reactionary behavior is completed
  IBehavior* _behaviorToResume = nullptr;
  
  // current behavior chooser (weak_ptr pointing to one of the others)
  IBehaviorChooser* _currentChooserPtr = nullptr;
  
  // - - - - - - - - - - - - - - -
  // factory & choosers
  // - - - - - - - - - - - - - - -
  
  // Factory creates and tracks data-driven behaviors etc
  BehaviorFactory* _behaviorFactory = nullptr;
    
  // This is a chooser which manually runs specific behaviors. The manager starts out using this chooser to
  // avoid immediately executing a behavior when the engine starts
  IBehaviorChooser* _selectionChooser = nullptr;

  // This is a special chooser for the demo. In addition to choosing behaviors, it is also the central place
  // in engine for any demo-specific code. This is in reference to the announce / PR demo
  IBehaviorChooser* _demoChooser = nullptr;

  // Behavior chooser for freeplay mode, it also includes games and sparks if needed
  IBehaviorChooser* _freeplayChooser = nullptr;
  
  // list of behaviors that fire automatically as reactions to events
  std::vector<IReactionaryBehavior*> _reactionaryBehaviors;
  bool                               _reactionsEnabled = true;
  
  // time at which last chooser was selected
  float _lastChooserSwitchTime;

  // - - - - - - - - - - - - - - -
  // others/shared
  // - - - - - - - - - - - - - - -
  
  // games that are available currently for Cozmo to request
  using BehaviorGameFlagMask = std::underlying_type<BehaviorGameFlag>::type;
  BehaviorGameFlagMask _availableGames = Util::EnumToUnderlying( BehaviorGameFlag::NoGame );
  
  // current active spark (this does guarantee that behaviors will kick in, only that Cozmo is in a Sparked state)
  UnlockId _activeSpark = UnlockId::Count;

  // whiteboard for behaviors to share information, or to store information only useful to behaviors
  std::unique_ptr<AIWhiteboard> _whiteboard;
    
  // For storing event handlers
  std::vector<Signal::SmartHandle> _eventHandlers;
    
}; // class BehaviorManager

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Inline
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorManager::AreAllGameFlagsAvailable(BehaviorGameFlag gameFlag) const
{
  const BehaviorGameFlagMask flags = Util::EnumToUnderlying(gameFlag);
  const bool allSet = ((_availableGames & flags) == flags);
  return allSet;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorManager::IsAnyGameFlagAvailable(BehaviorGameFlag gameFlag) const
{
  const BehaviorGameFlagMask flags = Util::EnumToUnderlying(gameFlag);
  const bool anySet = ((_availableGames & flags) != 0);
  return anySet;
}

} // namespace Cozmo
} // namespace Anki


#endif // COZMO_BEHAVIOR_MANAGER_H

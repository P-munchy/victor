/**
 * File: behaviorInterface.h
 *
 * Author: Andrew Stein
 * Date:   7/30/15
 *
 * Description: Defines interface for a Cozmo "Behavior".
 *
 * Copyright: Anki, Inc. 2015
 **/

#ifndef __Cozmo_Basestation_Behaviors_BehaviorInterface_H__
#define __Cozmo_Basestation_Behaviors_BehaviorInterface_H__


#include "anki/cozmo/basestation/actions/actionContainers.h"
#include "anki/cozmo/basestation/behaviorSystem/behaviorGroupFlags.h"
#include "anki/cozmo/basestation/moodSystem/emotionScorer.h"
#include "anki/cozmo/basestation/moodSystem/moodScorer.h"
#include "json/json-forwards.h"
#include <set>

#include "clad/externalInterface/messageEngineToGameTag.h"
#include "clad/externalInterface/messageGameToEngineTag.h"
#include "clad/types/behaviorGroup.h"
#include "util/bitFlags/bitFlags.h"
#include "util/logging/logging.h"
#include "util/random/randomGenerator.h"
#include "util/signals/simpleSignal_fwd.h"

// This macro uses PRINT_NAMED_INFO if the supplied define (first arg) evaluates to true, and PRINT_NAMED_DEBUG otherwise
// All args following the first are passed directly to the chosen print macro
#define BEHAVIOR_VERBOSE_PRINT(_BEHAVIORDEF, ...) do { \
  if ((_BEHAVIORDEF)) { PRINT_NAMED_INFO( __VA_ARGS__ ); } \
  else { PRINT_NAMED_DEBUG( __VA_ARGS__ ); } \
  } while(0) \

namespace Anki {
namespace Cozmo {
  
// Forward declarations
class IReactionaryBehavior;
class MoodManager;
class Robot;
class Reward;
namespace ExternalInterface {
class MessageEngineToGame;
class MessageGameToEngine;
struct RobotCompletedAction;
}
template<typename TYPE> class AnkiEvent;
  
// Base Behavior Interface specification
class IBehavior
{
protected:
    
  // Enforce creation through BehaviorFactory
  friend class BehaviorFactory;

  // Can't create a public IBehavior, but derived classes must pass a robot
  // reference into this protected constructor.
  IBehavior(Robot& robot, const Json::Value& config);
    
  virtual ~IBehavior();
    
public:
    
  enum class Status {
    Failure,
    Running,
    Complete
  };
    
  bool IsRunning() const { return _isRunning; }
    
  double GetRunningDuration() const;
    
  // Will be called upon first switching to a behavior before calling update.
  // Calls protected virtual InitInternal() method, which each derived class
  // should implement.
  Result Init();

  // If this behavior is resuming after a short interruption (e.g. a cliff reaction), this Resume function
  // will be called instead. It calls ResumeInternal(), which defaults to simply calling InitInternal, but can
  // be implemented by children to do specific resume behavior (e.g. start at a specific state). If this
  // function returns anything other than RESULT_OK, the behavior will not be resumed (but may still be Init'd
  // later)
  Result Resume();  

  // Step through the behavior and deliver rewards to the robot along the way
  // This calls the protected virtual UpdateInternal() method, which each
  // derived class should implement.
  Status Update() ;
    
  // This behavior was the currently running behavior, but is now stopping (to make way for a new current
  // behavior). Any behaviors from StartActing will be canceled.
  void Stop();

  //
  // Abstract methods to be overloaded:
  //
    
  // Returns true iff the state of the world/robot is sufficient for this
  // behavior to be executed
  virtual bool IsRunnable(const Robot& robot) const = 0;
  
  const std::string& GetName() const { return _name; }
  const std::string& GetStateName() const { return _stateName; }

  double GetTimeStartedRunning_s() const { return _startedRunningTime_s; }

  // returns true if any action from StartAction is currently running, indicating that the behavior is
  // likely waiting for something to complete
  inline bool IsActing() const;
    
  // EvaluateEmotionScore is a score directly based on the given emotion rules
  float EvaluateEmotionScore(const MoodManager& moodManager) const;
  
  float EvaluateScore(const Robot& robot) const;

  const MoodScorer& GetMoodScorer() const { return _moodScorer; }
    
  void ClearEmotionScorers()                         { _moodScorer.ClearEmotionScorers(); }
  void AddEmotionScorer(const EmotionScorer& scorer) { _moodScorer.AddEmotionScorer(scorer); }
  size_t GetEmotionScorerCount() const { return _moodScorer.GetEmotionScorerCount(); }
  const EmotionScorer& GetEmotionScorer(size_t index) const { return _moodScorer.GetEmotionScorer(index); }
    
    
  void SetOverrideScore(float newVal) { _overrideScore = newVal; }
    
  float EvaluateRepetitionPenalty() const;
  const Util::GraphEvaluator2d& GetRepetionalPenalty() const { return _repetitionPenalty; }
    
  bool IsOwnedByFactory() const { return _isOwnedByFactory; }
    
  bool IsChoosable() const { return _isChoosable; }
  void SetIsChoosable(bool newVal) { _isChoosable = newVal; }
    
  virtual IReactionaryBehavior* AsReactionaryBehavior() { assert(0); return nullptr; }
    
  bool IsBehaviorGroup(BehaviorGroup behaviorGroup) const { return _behaviorGroups.IsBitFlagSet(behaviorGroup); }
  bool MatchesAnyBehaviorGroups(const BehaviorGroupFlags::StorageType& flags) const {
    return _behaviorGroups.AreAnyBitsInMaskSet(flags);
  }
  bool MatchesAnyBehaviorGroups(const BehaviorGroupFlags& groupFlags) const {
    return MatchesAnyBehaviorGroups(groupFlags.GetFlags());
  }

  // if true, the previously running behavior will be resumed (if possible) after this behavior is
  // complete. Otherwise, a new behavior will be selected by the chooser after this one runs. This should
  // generally only be true for reactionary behaviors
  virtual bool ShouldResumeLastBehavior() const { return false; }

    
protected:
  
  void ClearBehaviorGroups() { _behaviorGroups.ClearFlags(); }
  void SetBehaviorGroup(BehaviorGroup behaviorGroup, bool newVal = true) {
    _behaviorGroups.SetBitFlag(behaviorGroup, newVal);
  }
    
  // Going forward we don't want names being set arbitrarily (they can come from data etc.)
  void DEMO_HACK_SetName(const char* inName) { _name = inName; }
  // Only sets the name if it's currenty the base default name
  void SetDefaultName(const char* inName);
  inline void SetStateName(const std::string& inName) { _stateName = inName; }
    
  virtual Result InitInternal(Robot& robot) = 0;
  virtual Result ResumeInternal(Robot& robot) { return InitInternal(robot); }

  // EvaluateScoreInternal is used to score each behavior for behavior selection - by default it just uses
  // EvaluateEmotionScore. If the behavior is running, it uses the Running score to decide if it should keep
  // running
  virtual float EvaluateRunningScoreInternal(const Robot& robot) const;
  virtual float EvaluateScoreInternal(const Robot& robot) const;

  // This function can be implemented by behaviors. It should return Running while it is running, and Complete
  // or Failure as needed. If it returns Complete, Stop will be called. Default implementation is to
  // return Running while IsActing, and Complete otherwise
  virtual Status UpdateInternal(Robot& robot);
  virtual void   StopInternal(Robot& robot) = 0;
    
  bool ReadFromJson(const Json::Value& config);

  Util::RandomGenerator& GetRNG() const;
    
  // Convenience aliases
  using GameToEngineEvent = AnkiEvent<ExternalInterface::MessageGameToEngine>;
  using EngineToGameEvent = AnkiEvent<ExternalInterface::MessageEngineToGame>;
  using EngineToGameTag   = ExternalInterface::MessageEngineToGameTag;
  using GameToEngineTag   = ExternalInterface::MessageGameToEngineTag;
    
  // Derived classes should use these methods to subscribe to any tags they
  // are interested in handling.
  void SubscribeToTags(std::set<GameToEngineTag>&& tags);
  void SubscribeToTags(std::set<EngineToGameTag>&& tags);
    
  // Derived classes must override this method to handle events that come in
  // irrespective of whether the behavior is running or not. Note that the Robot
  // reference is const to prevent the behavior from modifying the robot when it
  // is not running. If the behavior is subscribed to multiple tags, the presumption
  // is that this will handle switching based on tag internally.
  // NOTE: AlwaysHandle is called before HandleWhileRunning and HandleWhielNotRunning!
  virtual void AlwaysHandle(const GameToEngineEvent& event, const Robot& robot) { }
  virtual void AlwaysHandle(const EngineToGameEvent& event, const Robot& robot) { }
    
  // Derived classes must override this method to handle events that come in
  // while the behavior is running. In this case, the behavior is allowed to
  // modify the robot and thus receives a non-const reference to it.
  // If the behavior is subscribed to multiple tags, the presumption is that it
  // will handle switching based on tag internally.
  // NOTE: AlwaysHandle is called first!
  virtual void HandleWhileRunning(const GameToEngineEvent& event, Robot& robot) { }
  virtual void HandleWhileRunning(const EngineToGameEvent& event, Robot& robot) { }
    
  // Derived classes must override this method to handle events that come in
  // only while the behavior is NOT running. If it doesn't matter whether the
  // the behavior is running or not, consider using AlwaysHandle above instead.
  // If the behavior is subscribed to multiple tags, the presumption is that it
  // will handle switching based on tag internally.
  // NOTE: AlwaysHandle is called first!
  virtual void HandleWhileNotRunning(const GameToEngineEvent& event, const Robot& robot) { }
  virtual void HandleWhileNotRunning(const EngineToGameEvent& event, const Robot& robot) { }

  // Many behaviors use a pattern of executing an action, then waiting for it to finish before selecting the
  // next action. Instead of directly starting actions and handling ActionCompleted callbacks, derived
  // classes can use the functions below. Note: none of the StartActing functions can be called when the
  // behavior is not running (will result in an error and no action). Also, if the behavior was running when
  // you called StartActing, but is no longer running when the action completed, the callback will NOT be
  // called (this is necessary to prevent non-running behaviors from doing things with the robot). If the
  // behavior is stopped, within the Stop function, any actions which are still running will be canceled
  // (and you will not get any callback for it).

  // Each StartActing function returns true if the action was started, false otherwise. Reasons actions
  // might not be started include:
  // 1. Another action (from StartActing) is currently running or queued
  // 2. You are not running ( IsRunning() returns false )

  // Start an action now, and optionally provide a callback which will be called with the
  // RobotCompletedAction that corresponds to the action
  using RobotCompletedActionCallback = std::function<void(const ExternalInterface::RobotCompletedAction&)>;
  bool StartActing(IActionRunner* action, RobotCompletedActionCallback callback = {});

  // helper that just looks at the result (simpler, but you can't get things like the completion union)
  using ActionResultCallback = std::function<void(ActionResult)>;
  bool StartActing(IActionRunner* action, ActionResultCallback callback);

  // If you want to do something when the action finishes, regardless of it's result, you can use the
  // following callbacks, with either a callback function taking no arguments or taking a single Robot&
  // argument. You can also use member functions. The callback will be called when action completes for any
  // reason (as long as the behavior is running)

  using SimpleCallback = std::function<void(void)>;
  bool StartActing(IActionRunner* action, SimpleCallback callback);

  using SimpleCallbackWithRobot = std::function<void(Robot& robot)>;
  bool StartActing(IActionRunner* action, SimpleCallbackWithRobot callback);

  template<typename T>
  bool StartActing(IActionRunner* action, void(T::*callback)(Robot&));

  template<typename T>
  bool StartActing(IActionRunner* action, void(T::*callback)(void));

  // Called after StartActing, will add extraScore to the result of EvaluateRunningScoreInternal. This makes
  // it easy to encourage the system to keep a behavior running while it is acting. NOTE: multiple calls to
  // this function (for the same action) will be cumulative. The bonus will be reset as soon as the action is
  // complete, or the behavior is no longer running
  void IncreaseScoreWhileActing(float extraScore);
  
  // This function cancels the action started by StartActing (if there is one). Returns true if an action
  // was canceled, false otherwise. Note that if you are running, this will trigger a callback for the
  // cancellation unless you set allowCallback to false
  bool StopActing(bool allowCallback = true);

private:
            
  template<class EventType>
  void HandleEvent(const EventType& event);

  // this is an internal handler just form StartActing
  void HandleActionComplete(const ExternalInterface::RobotCompletedAction& msg);
    
  // ==================== Static Member Vars ====================
            
  static const char* kBaseDefaultName;
                
  // ==================== Member Vars ====================
    
  std::string _name;
  std::string _stateName = "";
        
  MoodScorer                 _moodScorer;
  Util::GraphEvaluator2d     _repetitionPenalty;
    
  Robot& _robot;
    
  std::vector<::Signal::SmartHandle> _eventHandles;
    
  double _startedRunningTime_s;
  double _lastRunTime_s;

  float _overrideScore; // any value >= 0 implies it should be used

  // for Start/StopActing if invalid, no action
  u32 _lastActionTag = ActionConstants::INVALID_TAG;
  RobotCompletedActionCallback  _actingCallback;
  float _extraRunningScore;
    
  BehaviorGroupFlags  _behaviorGroups;

  bool _isRunning;
  bool _isOwnedByFactory;
  bool _isChoosable;
    
  bool _enableRepetitionPenalty;
    
}; // class IBehavior
  
  
template<typename T>
bool IBehavior::StartActing(IActionRunner* action, void(T::*callback)(Robot&))
{
  return StartActing(action, std::bind(callback, static_cast<T*>(this), std::placeholders::_1));
}

template<typename T>
bool IBehavior::StartActing(IActionRunner* action, void(T::*callback)(void))
{
  std::function<void(void)> boundCallback = std::bind(callback, static_cast<T*>(this));
  return StartActing(action, boundCallback);
}

inline bool IBehavior::IsActing() const {
  return _lastActionTag != ActionConstants::INVALID_TAG;
}
  
template<class EventType>
void IBehavior::HandleEvent(const EventType& event)
{
  AlwaysHandle(event, _robot);
    
  if(IsRunning()) {
    HandleWhileRunning(event, _robot);
  } else {
    HandleWhileNotRunning(event, _robot);
  }
}
  
  
class IReactionaryBehavior : public IBehavior
{
protected:
    
  // Enforce creation through BehaviorFactory
  IReactionaryBehavior(Robot& robot, const Json::Value& config);
  virtual ~IReactionaryBehavior() {}
    
public:

  virtual const std::set<EngineToGameTag>& GetEngineToGameTags() const { return _engineToGameTags; }
  virtual const std::set<GameToEngineTag>& GetGameToEngineTags() const { return _gameToEngineTags; }
    
  // Subscribe to tags that should immediately trigger this behavior
  void SubscribeToTriggerTags(std::set<EngineToGameTag>&& tags);
  void SubscribeToTriggerTags(std::set<GameToEngineTag>&& tags);

  // if a trigger tag is received, this function will be called. If it returns true, this behavior will run
  // immediately
  virtual bool ShouldRunForEvent(const ExternalInterface::MessageEngineToGame& event) const { return true; }
  virtual bool ShouldRunForEvent(const ExternalInterface::MessageGameToEngine& event) const { return true; }
    
  virtual IReactionaryBehavior* AsReactionaryBehavior() override { return this; }

  // if true, the previously running behavior will be resumed (if possible) after this behavior is
  // complete. Otherwise, a new behavior will be selected by the chooser after this one runs
  virtual bool ShouldResumeLastBehavior() const override = 0;
    
protected:
  
  std::set<EngineToGameTag> _engineToGameTags;
  std::set<GameToEngineTag> _gameToEngineTags;
}; // class IReactionaryBehavior

template<typename EventTag>
inline const std::set<EventTag>& GetReactionaryBehaviorTags(const IReactionaryBehavior* behavior);

////////////////////////////////////////////////////////////////////////////////

template<>
inline const std::set<ExternalInterface::MessageEngineToGameTag>&
GetReactionaryBehaviorTags<ExternalInterface::MessageEngineToGameTag>(const IReactionaryBehavior* behavior)
{
  return behavior->GetEngineToGameTags();
}

template<>
inline const std::set<ExternalInterface::MessageGameToEngineTag>&
GetReactionaryBehaviorTags<ExternalInterface::MessageGameToEngineTag>(const IReactionaryBehavior* behavior)
{
  return behavior->GetGameToEngineTags();
}


} // namespace Cozmo
} // namespace Anki

#endif // __Cozmo_Basestation_Behaviors_BehaviorInterface_H__

/**
 * File: behaviorChooser.h
 *
 * Author: Lee
 * Created: 08/20/15
 *
 * Description: Class for handling picking of behaviors.
 *
 * Copyright: Anki, Inc. 2015
 *
 **/

#ifndef __Cozmo_Basestation_BehaviorChooser_H__
#define __Cozmo_Basestation_BehaviorChooser_H__

#include "anki/types.h"
#include "anki/cozmo/basestation/behaviorSystem/behaviorGroupFlags.h"
#include "clad/externalInterface/messageEngineToGame.h"
#include "clad/externalInterface/messageGameToEngine.h"
#include "util/graphEvaluator/graphEvaluator2d.h"
#include "util/helpers/noncopyable.h"
#include <map>
#include <string>
#include <set>
#include <vector>
#include <functional>


namespace Json {
  class Value;
}


namespace Anki {
namespace Cozmo {
  
//Forward declarations
class IBehavior;
class IReactionaryBehavior;
class MoodManager;
class Robot;
template <typename Type> class AnkiEvent;

// Interface for the container and logic associated with holding and choosing behaviors
class IBehaviorChooser : private Util::noncopyable
{
public:
  virtual Result AddBehavior(IBehavior *newBehavior) = 0;
  virtual IBehavior* ChooseNextBehavior(const Robot& robot, double currentTime_sec) const = 0;
  virtual IBehavior* GetBehaviorByName(const std::string& name) const = 0;
  
  virtual void AddReactionaryBehavior(IReactionaryBehavior* behavior) = 0;
  virtual IBehavior* GetReactionaryBehavior(const Robot& robot,
                                            const AnkiEvent<ExternalInterface::MessageEngineToGame>& event) const = 0;
  virtual IBehavior* GetReactionaryBehavior(const Robot& robot,
                                            const AnkiEvent<ExternalInterface::MessageGameToEngine>& event) const = 0;
  
  virtual Result Update(double currentTime_sec) { return Result::RESULT_OK; }

  virtual ~IBehaviorChooser() { }
  
  virtual const char* GetName() const = 0;
  
  virtual void EnableAllBehaviors(bool newVal = true) = 0;
  virtual void EnableBehaviorGroup(BehaviorGroup behaviorGroup, bool newVal = true) = 0;
  virtual bool EnableBehavior(const std::string& behaviorName, bool newVal = true) = 0;
  
  virtual void InitEnabledBehaviors(const Json::Value& inJson) = 0;

}; // class IBehaviorChooser
  
  
// A simple implementation for choosing behaviors based on priority only
// Behaviors are checked for runnability in the order they were added
class SimpleBehaviorChooser : public IBehaviorChooser
{
public:
  
  SimpleBehaviorChooser();
  
  // For IBehaviorChooser
  virtual Result AddBehavior(IBehavior *newBehavior) override;
  virtual IBehavior* ChooseNextBehavior(const Robot& robot, double currentTime_sec) const override;
  virtual IBehavior* GetBehaviorByName(const std::string& name) const override;
  
  virtual void AddReactionaryBehavior(IReactionaryBehavior* behavior) override { }
  virtual IBehavior* GetReactionaryBehavior(
    const Robot& robot,
    const AnkiEvent<ExternalInterface::MessageEngineToGame>& event) const override { return nullptr; }
  virtual IBehavior* GetReactionaryBehavior(
    const Robot& robot,
    const AnkiEvent<ExternalInterface::MessageGameToEngine>& event) const override { return nullptr; }
  
  virtual const char* GetName() const override { return "Simple"; }
  
  virtual void EnableAllBehaviors(bool newVal = true) override;
  virtual void EnableBehaviorGroup(BehaviorGroup behaviorGroup, bool newVal = true) override;
  virtual bool EnableBehavior(const std::string& behaviorName, bool newVal = true) override;
  
  void InitEnabledBehaviors(const Json::Value& inJson) override;
  
  // We need to clean up the behaviors we've been given to hold onto
  virtual ~SimpleBehaviorChooser();
  
protected:
  
  float MinMarginToSwapRunningBehavior(float runningDuration) const;

  using NameToBehaviorMap = std::map<std::string, IBehavior*>;
  NameToBehaviorMap _nameToBehaviorMap;
  
  Util::GraphEvaluator2d  _minMarginToSwapRunningBehavior;
};
  
// Builds upon the SimpleBehaviorChooser to also directly trigger a specific behavior on certain events
class ReactionaryBehaviorChooser : public SimpleBehaviorChooser
{
public:
  virtual void AddReactionaryBehavior(IReactionaryBehavior* behavior) override
  {
    _reactionaryBehaviorList.push_back(behavior);
  }
  virtual IBehavior* GetReactionaryBehavior(
    const Robot& robot,
    const AnkiEvent<ExternalInterface::MessageEngineToGame>& event) const override;
  virtual IBehavior* GetReactionaryBehavior(
    const Robot& robot,
    const AnkiEvent<ExternalInterface::MessageGameToEngine>& event) const override;
  
  virtual const char* GetName() const override { return "Reactionary"; }
  
  // We need to clean up the behaviors we've been given to hold onto
  virtual ~ReactionaryBehaviorChooser();
  
protected:
  std::vector<IReactionaryBehavior*> _reactionaryBehaviorList;
  
private:
  // Helper function to do the common functionality of the GetReactionaryBehavior calls
  template <typename EventType>
  IReactionaryBehavior* _GetReactionaryBehavior(
    const Robot& robot,
    const AnkiEvent<EventType>& event,
    std::function<const std::set<typename EventType::Tag>&(const IReactionaryBehavior&)> getTagSet) const;
};
  
} // namespace Cozmo
} // namespace Anki

#endif // __Cozmo_Basestation_BehaviorChooser_H__

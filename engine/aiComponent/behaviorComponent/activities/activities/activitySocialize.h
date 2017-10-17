/**
* File: ActivitySocialize.h
*
* Author: Kevin M. Karol
* Created: 04/27/17
*
* Description: Activity for cozmo to interact with the user's face
*
* Copyright: Anki, Inc. 2017
*
**/

#ifndef __Cozmo_Basestation_BehaviorSystem_Activities_Activities_ActivitySocialize_H__
#define __Cozmo_Basestation_BehaviorSystem_Activities_Activities_ActivitySocialize_H__

#include "engine/aiComponent/behaviorComponent/activities/activities/iActivity.h"

#include "clad/types/behaviorSystem/behaviorTypes.h"
#include "clad/types/behaviorSystem/behaviorObjectives.h"
#include "util/signals/simpleSignal_fwd.h"
#include <map>
#include <vector>

namespace Json {
class Value;
}


namespace Anki {
namespace Cozmo {
  
class BehaviorExploreLookAroundInPlace;

// A helper class to handle objective requirements
struct PotentialObjectives {
  PotentialObjectives(const Json::Value& config);
  
  BehaviorObjective objective = BehaviorObjective::Count;
  BehaviorID behaviorID = BehaviorID::PounceOnMotion_Socialize;
  UnlockId requiredUnlock = UnlockId::Count;
  float probabilityToRequire = 1.0f;
  unsigned int randCompletionsMin = 1;
  unsigned int randCompletionsMax = 1;
};

class ActivitySocialize : public IActivity
{
public:
  ActivitySocialize(const Json::Value& config);
  ~ActivitySocialize() {};
  
  
  
protected:
  // chooses the next behavior to run (could be the same we are currently running or null if none are desired)
  virtual ICozmoBehaviorPtr GetDesiredActiveBehaviorInternal(BehaviorExternalInterface& behaviorExternalInterface,
                                                        const ICozmoBehaviorPtr currentRunningBehavior) override;

  // reset the state and populate the objective which we will require for this run (they are randomized each
  // time the activity is selected)
  virtual void OnActivatedActivity(BehaviorExternalInterface& behaviorExternalInterface) override;
  virtual Result Update_Legacy(BehaviorExternalInterface& behaviorExternalInterface) override;

  virtual void InitActivity(BehaviorExternalInterface& behaviorExternalInterface) override;
  
private:
  // use the objective requirements to populate _objectivesLeft, taking into account unlocks and random
  // probabilities.
  void PopulatePotentialObjectives(BehaviorExternalInterface& behaviorExternalInterface);
  
  void PrintDebugObjectivesLeft(const std::string& eventName) const;
  
  enum class State {
    Initial,
    FindingFaces,
    Interacting,
    FinishedInteraction,
    Playing, // Either peekaboo or pouncing
    FinishedPlaying,
    None
  };
  
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Parameters set during init / construction
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  std::shared_ptr<BehaviorExploreLookAroundInPlace> _findFacesBehavior = nullptr;
  ICozmoBehaviorPtr _interactWithFacesBehavior = nullptr;
  
  ICozmoBehaviorPtr _playingBehavior = nullptr;
  
  unsigned int _maxNumIterationsToAllowForSearch = 0; // 0 means infinite
  
  // requirements defined from json
  using PotentialObjectivesList = std::vector< std::unique_ptr<PotentialObjectives> >;
  const PotentialObjectivesList _potentialObjectives;
  
  // function to read requirements from json
  static PotentialObjectivesList ReadPotentialObjectives(const Json::Value& config);
  
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Variables
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  
  State _state;
  
  // keep track of the number of iterations FindFaces does, so we can stop it manually when we want to
  unsigned int _lastNumSearchIterations = 0;
  
  // keep track of the number of times pounce has started, so we can advance states as needed (to detect when
  // the pounce behavior has started and stopped)
  unsigned int _lastNumTimesPlayStarted = 0;
  
  // contains an entry for each objective we need to complete, mapping to the number of times we need to complete it
  std::map< BehaviorObjective, unsigned int > _objectivesLeft;
};


} // namespace Cozmo
} // namespace Anki

#endif // __Cozmo_Basestation_BehaviorSystem_Activities_Activities_ActivitySocialize_H__

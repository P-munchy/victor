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

#include "anki/cozmo/basestation/behaviorSystem/activities/activities/activitySocialize.h"

#include "anki/common/basestation/jsonTools.h"
#include "anki/cozmo/basestation/ankiEventUtil.h"
#include "anki/cozmo/basestation/behaviorSystem/behaviorManager.h"
#include "anki/cozmo/basestation/behaviorSystem/behaviorPreReqs/behaviorPreReqRobot.h"
#include "anki/cozmo/basestation/behaviorSystem/behaviors/freeplay/exploration/behaviorExploreLookAroundInPlace.h"
#include "anki/cozmo/basestation/components/progressionUnlockComponent.h"
#include "anki/cozmo/basestation/robot.h"
#include "util/math/math.h"

namespace Anki {
namespace Cozmo {


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
PotentialObjectives::PotentialObjectives(const Json::Value& config)
{
  const std::string& objectiveStr = JsonTools::ParseString(config, "objective",
                                                           "FPSocialize.ObjectiveRequirement.InvalidConfig.NoObjective");
  objective = BehaviorObjectiveFromString(objectiveStr.c_str());
  
  std::string unlockStr;
  if( JsonTools::GetValueOptional(config, "ignoreIfLocked", unlockStr) ) {
    requiredUnlock = UnlockIdFromString(unlockStr);
  }

  probabilityToRequire = config.get("probabilityToRequireObjective", 1.0f).asFloat();
  randCompletionsMin = config.get("randomCompletionsNeededMin", 1).asUInt();
  randCompletionsMax = config.get("randomCompletionsNeededMax", 1).asUInt();
  behaviorID = IBehavior::ExtractBehaviorIDFromConfig(config);
  
  DEV_ASSERT(randCompletionsMax >= randCompletionsMin, "FPSocialize.ObjectiveRequirement.InvalidConfig.MaxLTMin");
  DEV_ASSERT(FLT_GE_ZERO( probabilityToRequire ), "FPSocialize.ObjectiveRequirement.InvalidConfig.NegativeProb");
}
  
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -`
ActivitySocialize::PotentialObjectivesList
          ActivitySocialize::ReadPotentialObjectives(const Json::Value& config)
{
  PotentialObjectivesList requirementList;
  
  const Json::Value& requirements = config["requiredObjectives"];
  if( !requirements.isNull() ) {
    for( const auto& objectiveConfig : requirements ) {
      requirementList.emplace_back( new PotentialObjectives(objectiveConfig) );
    }
  }
  
  return requirementList;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
ActivitySocialize::ActivitySocialize(Robot& robot, const Json::Value& config)
: IActivity(robot, config)
, _robot(robot)
, _potentialObjectives( ReadPotentialObjectives( config ) )
{
  // choosers and activities are created after the behaviors are added to the factory, so grab those now
  
  IBehaviorPtr facesBehavior = robot.GetBehaviorManager().FindBehaviorByID(BehaviorID::FindFaces_socialize);
  assert(std::static_pointer_cast<BehaviorExploreLookAroundInPlace>(facesBehavior));
  _findFacesBehavior = std::static_pointer_cast<BehaviorExploreLookAroundInPlace>(facesBehavior);
  DEV_ASSERT(nullptr != _findFacesBehavior, "FPSocializeBehaviorChooser.MissingBehavior.FindFaces");
  
  _interactWithFacesBehavior = robot.GetBehaviorManager().FindBehaviorByID(BehaviorID::InteractWithFaces);
  DEV_ASSERT(nullptr != _interactWithFacesBehavior, "FPSocializeBehaviorChooser.MissingBehavior.InteractWithFaces");
  
  // defaults to 0 to mean allow infinite iterations
  _maxNumIterationsToAllowForSearch = config.get("maxNumFindFacesSearchIterations", 0).asUInt();
  
  if(robot.HasExternalInterface()) {
    auto helper = MakeAnkiEventUtil(*robot.GetExternalInterface(), *this, _signalHandles);
    using namespace ExternalInterface;
    helper.SubscribeEngineToGame<MessageEngineToGameTag::BehaviorObjectiveAchieved>();
  }
}

  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ActivitySocialize::OnSelectedInternal(Robot& robot)
{
  // we always want to do the search first, if possible
  _state = State::Initial;
  PopulatePotentialObjectives();
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
IBehaviorPtr ActivitySocialize::ChooseNextBehaviorInternal(Robot& robot, const IBehaviorPtr currentRunningBehavior)
{
  IBehaviorPtr bestBehavior;
  
  bestBehavior = IActivity::ChooseNextBehaviorInternal(robot, currentRunningBehavior);
  
  if(bestBehavior != nullptr) {
    if( bestBehavior != currentRunningBehavior ) {
      PRINT_CH_INFO("Behaviors", "SocializeBehaviorChooser.ChooseNext.UseSimple",
                    "Simple behavior chooser chose behavior '%s', so use it",
                    BehaviorIDToString(bestBehavior->GetID()));
    }
    return bestBehavior;
  }
  BehaviorPreReqRobot preReqData(robot);
  // otherwise, check if it's time to change behaviors
  switch( _state ) {
    case State::Initial: {
      if( _interactWithFacesBehavior->IsRunnable(preReqData) ) {
        // if we can just right to interact, do that
        bestBehavior = _interactWithFacesBehavior;
        _state = State::Interacting;
      }
      else if( _findFacesBehavior->IsRunnable(preReqData) ) {
        // otherwise, search for a face
        bestBehavior = _findFacesBehavior;
        _state = State::FindingFaces;
        _lastNumSearchIterations = _findFacesBehavior->GetNumIterationsCompleted();
      }
      break;
    }
    
    case State::FindingFaces: {
      if( _interactWithFacesBehavior->IsRunnable(preReqData) ) {
        bestBehavior = _interactWithFacesBehavior;
        _state = State::Interacting;
      }
      else if( _maxNumIterationsToAllowForSearch > 0 &&
              _findFacesBehavior->GetNumIterationsCompleted() - _lastNumSearchIterations >=
              _maxNumIterationsToAllowForSearch ) {
        
        // NOTE: this is different from setting "behavior_NumberOfScansBeforeStop" in the find faces behavior,
        // because this will only transition out of the FindingFaces state if we _actually_ complete the
        // scans, whereas if we just set behavior_NumberOfScansBeforeStop = 2, the behavior may end for any of
        // a number of reasons (e.g. interruption)
        
        // we ran out of time searching, give up on this activity
        PRINT_CH_INFO("Behaviors", "SocializeBehaviorChooser.CompletedSearchIterations",
                      "Finished %d search iterations, giving up on activity",
                      _findFacesBehavior->GetNumIterationsCompleted() - _lastNumSearchIterations);
        // TODO:(bn) ideally this wouldn't put socialize on cooldown, but that's hard to implement in the
        // current system
        bestBehavior = nullptr;
        _state = State::None;
      }
      else {
        bestBehavior = _findFacesBehavior;
      }
      break;
    }
    
    case State::Interacting: {
      // keep interacting until the behavior ends. If we can't interact (e.g. we lost the face) then go back
      // to searching
      if( _interactWithFacesBehavior->IsRunning() || _interactWithFacesBehavior->IsRunnable(preReqData) ) {
        bestBehavior = _interactWithFacesBehavior;
      }
      else {
        // go back to find, but don't reset search count
        bestBehavior = _findFacesBehavior;
        _state = State::FindingFaces;
      }
      break;
    }
    
    case State::FinishedInteraction: {
      bool needsToPlay = false;
      // Has objectives we might want to do
      if( !_objectivesLeft.empty() )
      {
        std::vector<IBehaviorPtr> wantsRunnableBehaviors;
        // fill in _playingBehavior with one of the valid objectives
        for( const auto& reqPtr : _potentialObjectives )
        {
          // Check if behavior objective found
          if( _objectivesLeft.find(reqPtr->objective) != _objectivesLeft.end() )
          {
            IBehaviorPtr beh = robot.GetBehaviorManager().FindBehaviorByID(reqPtr->behaviorID);
            BehaviorPreReqRobot preReqData(robot);
            if( beh != nullptr )
            {
              // Check if runnable and valid.
              if( beh->IsRunnable(preReqData) )
              {
                wantsRunnableBehaviors.push_back(beh);
                PRINT_CH_INFO("Behaviors", "SocializeBehaviorChooser.FinishedInteraction",
                              "%s is runnable", beh->GetIDStr().c_str());
              }
              else
              {
                PRINT_CH_INFO("Behaviors", "SocializeBehaviorChooser.FinishedInteraction",
                              "%s is NOT runnable", beh->GetIDStr().c_str());
              }
            }
          }
        }
        
        if(!wantsRunnableBehaviors.empty())
        {
          int index = robot.GetRNG().RandIntInRange( 0, Util::numeric_cast<int>(wantsRunnableBehaviors.size()) - 1);
          _playingBehavior = wantsRunnableBehaviors[index];
          needsToPlay = true;
        }
      }
      
      if( nullptr != _playingBehavior && needsToPlay ) {
        bestBehavior = _playingBehavior;
        _lastNumTimesPlayStarted = _playingBehavior->GetNumTimesBehaviorStarted();
        _state = State::Playing;
      }
      else {
        _state = State::None;
      }
      break;
    }
    
    case State::Playing: {
      
      if( currentRunningBehavior == nullptr ) {
        
        // current being null means the playing behavior may have stopped, or maybe a reactionary behavior
        // ran, so check how many times play started. If it has actually started since we entered this
        // state, the assume we are done playing once it stops
        
        const bool hasPlayBehaviorStarted =
        _playingBehavior->GetNumTimesBehaviorStarted() > _lastNumTimesPlayStarted;
        if( hasPlayBehaviorStarted ) {
          // play behavior stopped for some reason.. finished
          _state = State::None;
          break;
        }
      }
      
      if( _playingBehavior->IsRunning() || _playingBehavior->IsRunnable(preReqData) ) {
        bestBehavior = _playingBehavior;
      }
      break;
    }
    
    case State::FinishedPlaying: {
      // At this point, we've told the pouncing behavior to stop after it's current action, so let it run
      // until that happens to avoid a harsh cut
      if( currentRunningBehavior == _playingBehavior && _playingBehavior->IsRunning() ) {
        // keep it going while it's running, let it stop itself
        bestBehavior = _playingBehavior;
      }
      break;
    }        
    
    case State::None:
    break;
  }

  return bestBehavior;      
}

  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
template<>
void ActivitySocialize::HandleMessage(const ExternalInterface::BehaviorObjectiveAchieved& msg)
{
  // transition out of the interacting state if needed
  
  if( _state == State::Interacting && msg.behaviorObjective == BehaviorObjective::InteractedWithFace ) {
    PRINT_CH_INFO("Behaviors", "SocializeBehaviorChooser.GotInteraction",
                  "Got interacted objective, advancing to next behavior");
    _state = State::FinishedInteraction;
    return;
  }
  
  // update objective counts needed
  int numObjectivesRemaining = Util::numeric_cast<int>(_objectivesLeft.size());
  auto objectiveIt = _objectivesLeft.find(msg.behaviorObjective);
  if( objectiveIt != _objectivesLeft.end() ) {
    DEV_ASSERT(objectiveIt->second > 0, "FPSocializeStrategy.HandleMessage.CorruptObjectiveData");
    
    objectiveIt->second--;
    numObjectivesRemaining = objectiveIt->second;
    if( objectiveIt->second == 0 ) {
      _objectivesLeft.erase( objectiveIt );
    }
  }
  
  PrintDebugObjectivesLeft("FPSocialize.HandleObjectiveAchieved.StillLeft");
  // _objectivesLeft might contain other objectives we decided not to do.
  if( numObjectivesRemaining == 0 && _state == State::Playing ) {
    PRINT_CH_INFO("Behaviors", "SocializeBehaviorChooser.FinishedPlaying",
                  "Got enough objectives to be done with pouncing, will transition out");
    if( _playingBehavior != nullptr && _playingBehavior->IsRunning() )
    {
      // tell the behavior to end nicely (when it's not acting)
      _playingBehavior->StopOnNextActionComplete();
    }
    
    _state = State::FinishedPlaying;
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ActivitySocialize::PopulatePotentialObjectives()
{
  _objectivesLeft.clear();
  
  for( const auto& reqPtr : _potentialObjectives ) {
    // first, check if the requirement is valid (based on unlock)
    if( reqPtr->requiredUnlock != UnlockId::Count &&
       ! _robot.GetProgressionUnlockComponent().IsUnlocked( reqPtr->requiredUnlock, true ) ) {
      
      PRINT_CH_INFO("Behaviors", "FPSocialize.Start.RequiredObjectiveLocked",
                    "objective %s requires %s, ignoring",
                    BehaviorObjectiveToString( reqPtr->objective ),
                    UnlockIdToString( reqPtr->requiredUnlock ));
      continue;
    }
    
    if( reqPtr->probabilityToRequire < 1.0f &&
       _robot.GetRNG().RandDbl() >= reqPtr->probabilityToRequire ) {
      
      PRINT_CH_INFO("Behaviors", "FPSocialize.Start.CoinFlipFailed", "%s (p=%f)",
                    BehaviorObjectiveToString( reqPtr->objective ),
                    reqPtr->probabilityToRequire);
      continue;
    }
    
    int numRequired = _robot.GetRNG().RandIntInRange( reqPtr->randCompletionsMin, reqPtr->randCompletionsMax );
    PRINT_CH_INFO("Behaviors", "FPSocialize.Start.RequiredObjective",
                  "must complete '%s' %d times ( range was [%d, %d] )",
                  BehaviorObjectiveToString( reqPtr->objective ),
                  numRequired,
                  reqPtr->randCompletionsMin,
                  reqPtr->randCompletionsMax);
    
    auto& objectiveRef = _objectivesLeft[ reqPtr->objective ];
    objectiveRef += numRequired;
  }
  
  PrintDebugObjectivesLeft("FPSocialize.Start.InitialObjectives");
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ActivitySocialize::PrintDebugObjectivesLeft(const std::string& eventName) const
{
#if ANKI_DEV_CHEATS
  std::stringstream ss;
  
  ss << "{";
  for(const auto& objectivePair : _objectivesLeft) {
    if(objectivePair.second <= 0) {
      PRINT_NAMED_ERROR("FPSocialize.CorruptObjectiveData",
                        "Objective '%s' has count %u, should not be possible",
                        BehaviorObjectiveToString(objectivePair.first),
                        objectivePair.second);
    }
    
    ss << ' ' << BehaviorObjectiveToString(objectivePair.first) << ":" << objectivePair.second;
  }
  ss << " }";
  
  PRINT_CH_INFO("Behaviors", eventName.c_str(), "Objectives left: %s", ss.str().c_str());
#endif // ANKI_DEV_CHEATS
}
  
} // namespace Cozmo
} // namespace Anki

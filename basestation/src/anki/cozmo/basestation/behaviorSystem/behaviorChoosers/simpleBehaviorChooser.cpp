/**
 * File: behaviorChooser.cpp
 *
 * Author: Lee
 * Created: 08/20/15
 *
 * Description: Class for handling picking of behaviors.
 *
 * Copyright: Anki, Inc. 2015
 *
 **/
#include "simpleBehaviorChooser.h"

#include "anki/common/basestation/utils/timer.h"
#include "anki/cozmo/basestation/behaviorSystem/behaviorFactory.h"
#include "anki/cozmo/basestation/behaviorSystem/behaviorGroupHelpers.h"
#include "anki/cozmo/basestation/behaviors/behaviorInterface.h"
#include "anki/cozmo/basestation/events/ankiEvent.h"
#include "anki/cozmo/basestation/messageHelpers.h"
#include "anki/cozmo/basestation/robot.h"
#include "anki/cozmo/basestation/viz/vizManager.h"
#include "util/global/globalDefinitions.h"
#include "util/helpers/templateHelpers.h"
#include "util/logging/logging.h"
#include "util/math/math.h"

#if ANKI_DEV_CHEATS
  #define VIZ_BEHAVIOR_SELECTION  1
#else
  #define VIZ_BEHAVIOR_SELECTION  0
#endif // ANKI_DEV_CHEATS

#if VIZ_BEHAVIOR_SELECTION
  #define VIZ_BEHAVIOR_SELECTION_ONLY(exp)  exp
#else
  #define VIZ_BEHAVIOR_SELECTION_ONLY(exp)
#endif // VIZ_BEHAVIOR_SELECTION

#define DEBUG_SHOW_ALL_SCORES 0

namespace Anki {
namespace Cozmo {

static const char* kScoreBonusForCurrentBehaviorKey = "scoreBonusForCurrentBehavior";
static const char* kBehaviorsInChooserKey = "behaviorGroups";
static const char* kDisabledGroupsKey     = "disabledGroups";
static const char* kEnabledGroupsKey      = "enabledGroups";
static const char* kDisabledBehaviorsKey  = "disabledBehaviors";
static const char* kEnabledBehaviorsKey   = "enabledBehaviors";

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
SimpleBehaviorChooser::SimpleBehaviorChooser(Robot& robot, const Json::Value& config)
{
  ReloadFromConfig(robot, config);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
SimpleBehaviorChooser::~SimpleBehaviorChooser()
{
  ClearBehaviors();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Result SimpleBehaviorChooser::ReloadFromConfig(Robot& robot, const Json::Value& config)
{
  // clear previous
  ClearBehaviors();

  // grab none behavior
  _behaviorNone = robot.GetBehaviorFactory().CreateBehavior(BehaviorType::NoneBehavior, robot, Json::Value());

  // add behaviors to this chooser
  AddFactoryBehaviorsFromGroupConfig(robot, config[kBehaviorsInChooserKey]);

  // add the proper behaviors and enable/disable appropriately
  ReadEnabledBehaviorsConfiguration(config);

  // - score bonus
  _scoreBonusForCurrentBehavior.Clear();

  const Json::Value& scoreBonusJson = config[kScoreBonusForCurrentBehaviorKey];
  if (scoreBonusJson.isNull() || !_scoreBonusForCurrentBehavior.ReadFromJson(scoreBonusJson))
  {
    PRINT_NAMED_WARNING("SimpleBehaviorChooser.ReadFromJson.BadScoreBonus",
      "'%s' failed to read (%s)", kScoreBonusForCurrentBehaviorKey, scoreBonusJson.isNull() ? "Missing" : "Bad");
  }
  
  if (_scoreBonusForCurrentBehavior.GetNumNodes() == 0)
  {
    PRINT_NAMED_WARNING("SimpleBehaviorChooser.ReadFromJson.EmptyScoreBonus", "Forcing to default (no bonuses)");
    _scoreBonusForCurrentBehavior.AddNode(0.0f, 0.0f); // no bonus for any X
  }

  return RESULT_OK;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void SimpleBehaviorChooser::SetAllBehaviorsEnabled(bool newVal)
{
  for (auto& kv : _nameToBehaviorInfoMap)
  {
    BehaviorInfo& behaviorInfo = kv.second;
    behaviorInfo._enabled = newVal;
  }
}
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void SimpleBehaviorChooser::SetBehaviorGroupEnabled(BehaviorGroup behaviorGroup, bool newVal)
{
  BehaviorGroupFlags behaviorGroupFlags;
  behaviorGroupFlags.SetBitFlag(behaviorGroup, true);
  
  // PRINT_NAMED_DEBUG("SimpleBehaviorChooser.EnableBehaviorGroup",
  //                   "%s: %d",
  //                   BehaviorGroupToString(behaviorGroup),
  //                   newVal);

  for (auto& kv : _nameToBehaviorInfoMap)
  {
    BehaviorInfo& behaviorInfo = kv.second;
    const bool affected = behaviorInfo._behaviorPtr->MatchesAnyBehaviorGroups(behaviorGroupFlags);
    if ( affected ) {
      behaviorInfo._enabled = newVal;
    }
  }
}
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool SimpleBehaviorChooser::SetBehaviorEnabled(const std::string& behaviorName, bool newVal)
{
  const auto& it = _nameToBehaviorInfoMap.find(behaviorName);
  if (it != _nameToBehaviorInfoMap.end())
  {
    BehaviorInfo& behaviorInfo = it->second;
    behaviorInfo._enabled = newVal;
    return true;
  }
  else
  {
    PRINT_NAMED_WARNING("EnableBehavior.NotFound", "No Behavior named '%s' (newVal = %d)", behaviorName.c_str(), (int)newVal);
    return false;
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void SimpleBehaviorChooser::ReadEnabledBehaviorsConfiguration(const Json::Value& inJson)
{
  // enabled everything by default
  SetAllBehaviorsEnabled();

  // Disable groups, then enable groups
  SetBehaviorEnabledFromGroupConfig( inJson[kDisabledGroupsKey], false );
  SetBehaviorEnabledFromGroupConfig( inJson[kEnabledGroupsKey], true );

  // Disable specific behaviors, then enable specific behaviors
  SetBehaviorEnabledFromBehaviorConfig( inJson[kDisabledBehaviorsKey], false );
  SetBehaviorEnabledFromBehaviorConfig( inJson[kEnabledBehaviorsKey], true );
}
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
float SimpleBehaviorChooser::ScoreBonusForCurrentBehavior(float runningDuration) const
{
  const float minMargin = _scoreBonusForCurrentBehavior.EvaluateY(runningDuration);
  return minMargin;
}
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
IBehavior* SimpleBehaviorChooser::ChooseNextBehavior(const Robot& robot) const
{
  const float kRandomFactor = 0.1f;
  
  Util::RandomGenerator rng; // [MarkW:TODO] We should share these (1 per robot or subsystem maybe?) for replay determinism
  
  VIZ_BEHAVIOR_SELECTION_ONLY( VizInterface::RobotBehaviorSelectData robotBehaviorSelectData );
  
  IBehavior* bestBehavior = nullptr;
  float bestScore = 0.0f;
  for (const auto& kv : _nameToBehaviorInfoMap)
  {
    const BehaviorInfo& behaviorInfo = kv.second;
    if (!behaviorInfo._enabled)
    {
      continue;
    }

    IBehavior* behavior = kv.second._behaviorPtr;
    
    VizInterface::BehaviorScoreData scoreData;

    scoreData.behaviorScore = behavior->EvaluateScore(robot);
    scoreData.totalScore    = scoreData.behaviorScore;
    VIZ_BEHAVIOR_SELECTION_ONLY( scoreData.name = behavior->GetName() );
    
    if (scoreData.totalScore > 0.0f)
    {
      if (behavior->IsRunning())
      {
        const float runningDuration = Util::numeric_cast<float>(behavior->GetRunningDuration());
        const float runningBonus = ScoreBonusForCurrentBehavior(runningDuration);
        
        scoreData.totalScore += runningBonus;

        // running behavior gets max possible random score
        scoreData.totalScore += kRandomFactor;

        // don't allow margin and rand to push score out of >0 range
        scoreData.totalScore = Util::Max(scoreData.totalScore, 0.01f);

        if( DEBUG_SHOW_ALL_SCORES ) {
          PRINT_NAMED_DEBUG("BehaviorChooser.Score.Running",
                            "behavior '%s' total=%f (raw=%f + running=%f + random=%f)",
                            behavior->GetName().c_str(),
                            scoreData.totalScore,
                            scoreData.behaviorScore,
                            runningBonus,
                            kRandomFactor);
        }        
      }
      else
      {
        // randomization only for non-running behaviors
        scoreData.totalScore += rng.RandDbl(kRandomFactor);

        if( DEBUG_SHOW_ALL_SCORES ) {
          PRINT_NAMED_DEBUG("BehaviorChooser.Score.NotRunning",
                            "behavior '%s' total=%f (raw=%f + random)",
                            behavior->GetName().c_str(),
                            scoreData.totalScore,
                            scoreData.behaviorScore);
        }
      }

      // allow sub-classes to modify this score
      ModifyScore(behavior, scoreData.totalScore);
      
      if (scoreData.totalScore > bestScore)
      {
        bestBehavior = behavior;
        bestScore    = scoreData.totalScore;
      }
    }
    else if( DEBUG_SHOW_ALL_SCORES ) {
      PRINT_NAMED_DEBUG("BehaviorChooser.Score.Zero",
                        "behavior '%s' choosable but has 0 score",
                        behavior->GetName().c_str());
    }
    
    VIZ_BEHAVIOR_SELECTION_ONLY( robotBehaviorSelectData.scoreData.push_back(scoreData) );
  }
  
  VIZ_BEHAVIOR_SELECTION_ONLY( robot.GetContext()->GetVizManager()->SendRobotBehaviorSelectData(std::move(robotBehaviorSelectData)) );
  
  if (bestBehavior == nullptr)
  {
    bestBehavior = _behaviorNone;
  }

  return bestBehavior;
}
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void SimpleBehaviorChooser::ClearBehaviors()
{
  // behaviors should actually be smart pointers, since the factory can be destroyed before choosers

  // clear behavior none
  ASSERT_NAMED((nullptr == _behaviorNone) || (_behaviorNone->IsOwnedByFactory()),
    "SimpleBehaviorChooser.ClearBehaviors.BadNoneBehavior");
  _behaviorNone = nullptr;
  
  // clear all others
  #if ANKI_DEVELOPER_CODE
  {
    for( const auto& infoPair : _nameToBehaviorInfoMap )
    {
      ASSERT_NAMED((nullptr != infoPair.second._behaviorPtr) && (infoPair.second._behaviorPtr->IsOwnedByFactory()),
        "SimpleBehaviorChooser.ClearBehaviors.BehaviorNotOwnedByFactory");
    }
  }
  #endif
  _nameToBehaviorInfoMap.clear();
}
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void SimpleBehaviorChooser::AddFactoryBehaviorsFromGroupConfig(Robot& robot, const Json::Value& groupList)
{
  // set group flags for all groups specified in the list
  BehaviorGroupFlags behaviorGroupFlags;
  Json::Value::const_iterator groupNameIt = groupList.begin();
  const Json::Value::const_iterator groupNameEnd = groupList.end();
  for(; groupNameIt != groupNameEnd; ++groupNameIt)
  {
    const char* groupName = groupNameIt->asCString();
    const BehaviorGroup behaviorGroup = BehaviorGroupFromString(groupName);
    ASSERT_NAMED(behaviorGroup != BehaviorGroup::Count,
      "SimpleBehaviorChooser.AddFactoryBehaviorsFromGroupConfig.BadGroupInConfig");
    behaviorGroupFlags.SetBitFlag(behaviorGroup, true);
    
    // log
    PRINT_NAMED_DEBUG("SimpleBehaviorChooser.AddFactoryBehaviorsFromGroupConfig",
      "BehaviorGroup '%s' included", groupName);
  }
  
  // if we have groups defined, add behaviors
  const bool hasGroupsToInclude = behaviorGroupFlags.AreAnyFlagsSet();
  if ( hasGroupsToInclude )
  {
    // iterate all behaviors in the factory, and grab those that match any of the allowed groups
    const BehaviorFactory& behaviorFactory = robot.GetBehaviorFactory();
    for( const auto& factoryMapPair : behaviorFactory.GetBehaviorMap() )
    {
      IBehavior* const behaviorToAdd = factoryMapPair.second;
      ASSERT_NAMED(nullptr != behaviorToAdd,
        "SimpleBehaviorChooser.AddFactoryBehaviorsFromGroupConfig.NullBehavior");
      ASSERT_NAMED(behaviorToAdd->GetName() == factoryMapPair.first,
        "SimpleBehaviorChooser.AddFactoryBehaviorsFromGroupConfig.NameInFactoryAndBehaviorNameMismatch");
      
      // check if this behavior has any of the groups defined for this chooser
      const bool shouldAddBehavior = behaviorToAdd->MatchesAnyBehaviorGroups(behaviorGroupFlags);
      if ( shouldAddBehavior )
      {
        TryAddBehavior( behaviorToAdd );
      }
    }
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Result SimpleBehaviorChooser::TryAddBehavior(IBehavior* behavior)
{
  // try to add by behavior name
  const std::string& behaviorName = behavior->GetName();
  const auto insertResult = _nameToBehaviorInfoMap.insert( std::make_pair(behaviorName, BehaviorInfo(behavior, true)) );
  const bool addedNewEntry = insertResult.second;
  if (!addedNewEntry)
  {
    // if we have an entry in our map under this name, it has to match the pointer in the factory, otherwise
    // who the hell are we pointing to?
    ASSERT_NAMED( insertResult.first->second._behaviorPtr == behavior,
      "SimpleBehaviorChooser.TryAddBehavior.DuplicateNameDifferentPointer" );
  }
  else
  {
    // added to the map as expected
    PRINT_NAMED_DEBUG("SimpleBehaviorChooser.TryAddBehavior.Addition",
      "Added behavior '%s' from factory", behavior->GetName().c_str());
  }
  
  // return code
  const Result ret = addedNewEntry ? RESULT_OK : RESULT_FAIL;
  return ret;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool SimpleBehaviorChooser::IsBehaviorEnabled(const std::string& name) const
{
  bool enabled = false;

  // check in table
  const auto match = _nameToBehaviorInfoMap.find(name);
  if ( match != _nameToBehaviorInfoMap.end() )
  {
    enabled = match->second._enabled;
  }
  else
  {
    PRINT_NAMED_ERROR("SimpleBehaviorChooser.IsBehaviorEnabled", "Behavior not found in this chooser '%s'", name.c_str());
  }
  
  return enabled;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void SimpleBehaviorChooser::SetBehaviorEnabledFromGroupConfig(const Json::Value& groupList, bool enable)
{
  // set group flags for all groups specified in the list
  BehaviorGroupFlags behaviorGroupFlags;
  Json::Value::const_iterator groupNameIt = groupList.begin();
  const Json::Value::const_iterator groupNameEnd = groupList.end();
  for(; groupNameIt != groupNameEnd; ++groupNameIt)
  {
    const char* groupName = groupNameIt->asCString();
    const BehaviorGroup behaviorGroup = BehaviorGroupFromString(groupName);
    ASSERT_NAMED(behaviorGroup != BehaviorGroup::Count,
      "SimpleBehaviorChooser.SetBehaviorEnabledFromGroupConfig.BadGroupInConfig");
    behaviorGroupFlags.SetBitFlag(behaviorGroup, true);
    
    // log
    PRINT_NAMED_DEBUG("SimpleBehaviorChooser.SetBehaviorEnabledFromGroupConfig",
      "BehaviorGroup '%s' %sabled", groupName, enable ? "en" : "dis");
  }

  // check we have flags
  if ( behaviorGroupFlags.AreAnyFlagsSet() )
  {
    // iterate our behaviors and set enabled/disabled if they match the group
    for( auto& mapPair : _nameToBehaviorInfoMap )
    {
      BehaviorInfo& behaviorInfo = mapPair.second;
      const bool affected = behaviorInfo._behaviorPtr->MatchesAnyBehaviorGroups( behaviorGroupFlags );
      if ( affected )
      {
        behaviorInfo._enabled = enable;
        // log
        PRINT_NAMED_DEBUG("SimpleBehaviorChooser.SetBehaviorEnabledFromGroupConfig",
          "Behavior '%s' %sabled due to group setting", mapPair.first.c_str(), enable ? "en" : "dis");
      }
    }
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void SimpleBehaviorChooser::SetBehaviorEnabledFromBehaviorConfig(const Json::Value& behaviorList, bool enable)
{
  // set group flags for all groups specified in the list
  Json::Value::const_iterator behaviorNameIt = behaviorList.begin();
  const Json::Value::const_iterator behaviorNameEnd = behaviorList.end();
  for(; behaviorNameIt != behaviorNameEnd; ++behaviorNameIt)
  {
    const char* behaviorName = behaviorNameIt->asCString();
    
    // if we have it, change
    const auto match = _nameToBehaviorInfoMap.find(behaviorName);
    if ( match != _nameToBehaviorInfoMap.end() )
    {
      BehaviorInfo& behaviorInfo = match->second;
      behaviorInfo._enabled = enable;
      
      // log
      PRINT_NAMED_DEBUG("SimpleBehaviorChooser.SetBehaviorEnabledFromBehaviorConfig",
        "Behavior '%s' %sabled", behaviorName, enable ? "en" : "dis");
    }
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
IBehavior* SimpleBehaviorChooser::FindBehaviorInTableByName(const std::string& name)
{
  IBehavior* ret = nullptr;
  const auto& matchIt = _nameToBehaviorInfoMap.find(name);
  if ( matchIt != _nameToBehaviorInfoMap.end() ) {
    ret = matchIt->second._behaviorPtr;
  }
  return ret;
}
  
} // namespace Cozmo
} // namespace Anki

/**
* File: stateConceptStrategyFactory.cpp
*
* Author: Kevin M. Karol
* Created: 6/03/17
*
* Description: Factory for creating wantsToRunStrategy
*
* Copyright: Anki, Inc. 2017
*
**/

#include "engine/aiComponent/beiConditions/beiConditionFactory.h"

#include "engine/aiComponent/beiConditions/conditions/conditionBatteryLevel.h"
#include "engine/aiComponent/beiConditions/conditions/conditionBehaviorTimer.h"
#include "engine/aiComponent/beiConditions/conditions/conditionCliffDetected.h"
#include "engine/aiComponent/beiConditions/conditions/conditionCompound.h"
#include "engine/aiComponent/beiConditions/conditions/conditionConsoleVar.h"
#include "engine/aiComponent/beiConditions/conditions/conditionCubeTapped.h"
#include "engine/aiComponent/beiConditions/conditions/conditionEmotion.h"
#include "engine/aiComponent/beiConditions/conditions/conditionEyeContact.h"
#include "engine/aiComponent/beiConditions/conditions/conditionFacePositionUpdated.h"
#include "engine/aiComponent/beiConditions/conditions/conditionFeatureGate.h"
#include "engine/aiComponent/beiConditions/conditions/conditionMotionDetected.h"
#include "engine/aiComponent/beiConditions/conditions/conditionObjectInitialDetection.h"
#include "engine/aiComponent/beiConditions/conditions/conditionObjectKnown.h"
#include "engine/aiComponent/beiConditions/conditions/conditionObjectMoved.h"
#include "engine/aiComponent/beiConditions/conditions/conditionObjectPositionUpdated.h"
#include "engine/aiComponent/beiConditions/conditions/conditionObstacleDetected.h"
#include "engine/aiComponent/beiConditions/conditions/conditionOffTreadsState.h"
#include "engine/aiComponent/beiConditions/conditions/conditionOnCharger.h"
#include "engine/aiComponent/beiConditions/conditions/conditionOnChargerPlatform.h"
#include "engine/aiComponent/beiConditions/conditions/conditionPetInitialDetection.h"
#include "engine/aiComponent/beiConditions/conditions/conditionProxInRange.h"
#include "engine/aiComponent/beiConditions/conditions/conditionRobotPlacedOnSlope.h"
#include "engine/aiComponent/beiConditions/conditions/conditionRobotShaken.h"
#include "engine/aiComponent/beiConditions/conditions/conditionRobotTouched.h"
#include "engine/aiComponent/beiConditions/conditions/conditionSimpleMood.h"
#include "engine/aiComponent/beiConditions/conditions/conditionTimedDedup.h"
#include "engine/aiComponent/beiConditions/conditions/conditionTimerInRange.h"
#include "engine/aiComponent/beiConditions/conditions/conditionTriggerWordPending.h"
#include "engine/aiComponent/beiConditions/conditions/conditionUnexpectedMovement.h"
#include "engine/aiComponent/beiConditions/conditions/conditionUnitTest.h"
#include "engine/aiComponent/beiConditions/conditions/conditionUserIntentPending.h"
#include "engine/aiComponent/beiConditions/conditions/conditionTrue.h"

#include "clad/types/behaviorComponent/beiConditionTypes.h"

#include "util/logging/logging.h"


namespace Anki {
namespace Cozmo {

  
namespace {
static const char* kCustomConditionKey = "customCondition";
}

std::map< std::string, IBEIConditionPtr > BEIConditionFactory::_customConditionMap;


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
CustomBEIConditionHandleInternal::CustomBEIConditionHandleInternal(const std::string& conditionName)
  : _conditionName(conditionName)
{
  DEV_ASSERT(!_conditionName.empty(), "CustomBEIConditionHandle.NoConditionName");
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
CustomBEIConditionHandleInternal::~CustomBEIConditionHandleInternal()
{
  if( !_conditionName.empty() ) {
    BEIConditionFactory::RemoveCustomCondition(_conditionName);
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
CustomBEIConditionHandle BEIConditionFactory::InjectCustomBEICondition(const std::string& name,
                                                                       IBEIConditionPtr condition)
{
  DEV_ASSERT_MSG(_customConditionMap.find(name) == _customConditionMap.end(),
                 "BEIConditionFactory.InjectCustomBEICondition.DuplicateName",
                 "already have a condition with name '%s'",
                 name.c_str());
  
  _customConditionMap[name] = condition;

  PRINT_CH_DEBUG("Behaviors", "BEIConditionFactory.InjectCustomBEICondition",
                 "Added custom condition '%s'",
                 name.c_str());

  if( condition->GetOwnerDebugLabel().empty() ) {
    // set debug label to include name for easier debugging
    condition->SetOwnerDebugLabel( "@" + name );
  }
  
  // note: can't use make_shared because constructor is private
  CustomBEIConditionHandle ret( new CustomBEIConditionHandleInternal( name ) );
  return ret;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BEIConditionFactory::RemoveCustomCondition(const std::string& name)
{
  auto it = _customConditionMap.find(name);
  if( ANKI_VERIFY( it != _customConditionMap.end(),
                   "BEIConditionFactory.RemoveCustomCondition.NotFound",
                   "condition name '%s' not found among our %zu custom conditions",
                   name.c_str(),
                   _customConditionMap.size() ) ) {
    _customConditionMap.erase(it);

    PRINT_CH_DEBUG("Behaviors", "BEIConditionFactory.RemoveCustomCondition",
                   "Removed custom condition '%s'",
                   name.c_str());
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BEIConditionFactory::IsValidCondition(const Json::Value& config)
{
  if( !config[kCustomConditionKey].isNull() ) {
    auto it = _customConditionMap.find(config[kCustomConditionKey].asString());
    const bool found = it != _customConditionMap.end();
    return found;
  }

  if( !config[IBEICondition::kConditionTypeKey].isNull() ) {
    const std::string& typeStr = config[IBEICondition::kConditionTypeKey].asString();
    BEIConditionType waste;
    const bool convertedOK =  BEIConditionTypeFromString(typeStr, waste);
    return convertedOK;
  }

  // neither key is specified
  return false;    
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
IBEIConditionPtr BEIConditionFactory::GetCustomCondition(const Json::Value& config, const std::string& ownerDebugLabel)
{
  DEV_ASSERT( config[IBEICondition::kConditionTypeKey].isNull(), "BEIConditionFactory.SpecifiedCustomConditionAndType" );
  
  auto it = _customConditionMap.find(config[kCustomConditionKey].asString());
  if( ANKI_VERIFY( it != _customConditionMap.end(),
                   "BEIConditionFactory.GetCustomCondition.NotFound",
                   "No custom condition with name '%s' found. Have %zu custom conditions",
                   config[kCustomConditionKey].asString().c_str(),
                   _customConditionMap.size() ) ) {
    return it->second;
  }
  else {
    return IBEIConditionPtr{};
  }
  
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
IBEIConditionPtr BEIConditionFactory::CreateBEICondition(const Json::Value& config, const std::string& ownerDebugLabel)
{
  
  if( !config[kCustomConditionKey].isNull() ) {
    return GetCustomCondition(config, ownerDebugLabel);
  }
  
  BEIConditionType strategyType = IBEICondition::ExtractConditionType(config);
  
  IBEIConditionPtr strategy = nullptr;

  switch (strategyType) {
    case BEIConditionType::BatteryLevel:
    {
      strategy = std::make_shared<ConditionBatteryLevel>(config);
      break;
    }
    case BEIConditionType::BehaviorTimer:
    {
      strategy = std::make_shared<ConditionBehaviorTimer>(config);
      break;
    }
    case BEIConditionType::Compound:
    {
      strategy = std::make_shared<ConditionCompound>(config);
      break;
    }
    case BEIConditionType::ConsoleVar:
    {
      strategy = std::make_shared<ConditionConsoleVar>(config);
      break;
    }
    case BEIConditionType::Emotion:
    {
      strategy = std::make_shared<ConditionEmotion>(config);
      break;
    }
    case BEIConditionType::EyeContact:
    {
      strategy = std::make_shared<ConditionEyeContact>(config);
      break;
    }
    case BEIConditionType::FacePositionUpdated:
    {
      strategy = std::make_shared<ConditionFacePositionUpdated>(config);
      break;
    }
    case BEIConditionType::FeatureGate:
    {
      strategy = std::make_shared<ConditionFeatureGate>(config);
      break;
    }
    case BEIConditionType::MotionDetected:
    {
      strategy = std::make_shared<ConditionMotionDetected>(config);
      break;
    }
    case BEIConditionType::ObjectInitialDetection:
    {
      strategy = std::make_shared<ConditionObjectInitialDetection>(config);
      break;
    }
    case BEIConditionType::ObjectKnown:
    {
      strategy = std::make_shared<ConditionObjectKnown>(config);
      break;
    }
    case BEIConditionType::ObjectMoved:
    {
      strategy = std::make_shared<ConditionObjectMoved>(config);
      break;
    }
    case BEIConditionType::ObjectPositionUpdated:
    {
      strategy = std::make_shared<ConditionObjectPositionUpdated>(config);
      break;
    }
    case BEIConditionType::ObstacleDetected:
    {
      strategy = std::make_shared<ConditionObstacleDetected>(config);
      break;
    }
    case BEIConditionType::PetInitialDetection:
    {
      strategy = std::make_shared<ConditionPetInitialDetection>(config);
      break;
    }
    case BEIConditionType::ProxInRange:
    {
      strategy = std::make_shared<ConditionProxInRange>(config);
      break;
    }
    case BEIConditionType::RobotPlacedOnSlope:
    {
      strategy = std::make_shared<ConditionRobotPlacedOnSlope>(config);
      break;
    }
    case BEIConditionType::RobotShaken:
    {
      strategy = std::make_shared<ConditionRobotShaken>(config);
      break;
    }
    case BEIConditionType::RobotTouched:
    {
      strategy = std::make_shared<ConditionRobotTouched>(config);
      break;
    }
    case BEIConditionType::SimpleMood:
    {
      strategy = std::make_shared<ConditionSimpleMood>(config);
      break;
    }
    case BEIConditionType::TimerInRange:
    {
      strategy = std::make_shared<ConditionTimerInRange>(config);
      break;
    }
    case BEIConditionType::TimedDedup:
    {
      strategy = std::make_shared<ConditionTimedDedup>(config);
      break;
    }
    case BEIConditionType::TrueCondition:
    {
      strategy = std::make_shared<ConditionTrue>(config);
      break;
    }
    case BEIConditionType::TriggerWordPending:
    {
      strategy = std::make_shared<ConditionTriggerWordPending>(config);
      break;
    }
    case BEIConditionType::UnexpectedMovement:
    {
      strategy = std::make_shared<ConditionUnexpectedMovement>(config);
      break;
    }
    case BEIConditionType::UserIntentPending:
    {
      strategy = std::make_shared<ConditionUserIntentPending>(config);
      break;
    }
    case BEIConditionType::OnCharger:
    {
      strategy = std::make_shared<ConditionOnCharger>(config);
      break;
    }
    case BEIConditionType::OnChargerPlatform:
    {
      strategy = std::make_shared<ConditionOnChargerPlatform>(config);
      break;
    }
    case BEIConditionType::OffTreadsState:
    {
      strategy = std::make_shared<ConditionOffTreadsState>(config);
      break;
    }
    case BEIConditionType::CliffDetected:
    {
      strategy = std::make_shared<ConditionCliffDetected>(config);
      break;
    }
    case BEIConditionType::CubeTapped:
    {
      strategy = std::make_shared<ConditionCubeTapped>(config);
      break;
    }
    case BEIConditionType::UnitTestCondition:
    {
      strategy = std::make_shared<ConditionUnitTest>(config);
      break;
    }
    
    case BEIConditionType::Lambda:
    {
      DEV_ASSERT(false, "BEIConditionFactory.CreateWantsToRunStrategy.CantCreateLambdaFromConfig");
      break;
    }
    case BEIConditionType::Invalid:
    {
      DEV_ASSERT(false, "BEIConditionFactory.CreateWantsToRunStrategy.InvalidType");
      break;
    }
    
  }
  
  if( (strategy != nullptr) && !ownerDebugLabel.empty() ) {
    strategy->SetOwnerDebugLabel( ownerDebugLabel );
  }
  
  return strategy;
}
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
IBEIConditionPtr BEIConditionFactory::CreateBEICondition(BEIConditionType type, const std::string& ownerDebugLabel)
{
  Json::Value config = IBEICondition::GenerateBaseConditionConfig( type );
  return CreateBEICondition( config, ownerDebugLabel );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BEIConditionFactory::CheckConditionsAreUsed(const CustomBEIConditionHandleList& handles,
                                                 const std::string& debugStr)
{
  bool ret = true;
  
  for( const auto& handle : handles ) {
    if( handle == nullptr ) {
      ret = false;
      PRINT_NAMED_WARNING("BEIConditionFactory.AreConditionsUsed.NullHandle",
                          "One of the handles in the container was empty");
      continue;
    }

    auto it = _customConditionMap.find( handle->_conditionName );
    if( it == _customConditionMap.end() ) {
      ret = false;
      PRINT_NAMED_ERROR("BEIConditionFactory.AreConditionsUsed.HandleNotContained",
                        "The handle with name '%s' was not found in the map. This is a bug",
                        handle->_conditionName.c_str());
      continue;
    }

    const long numUses = it->second.use_count();

    if( numUses <= 1 ) {
      PRINT_NAMED_WARNING("BEIConditionFactory.AreConditionsUsed.NotUsed",
                          "%s: BEI condition '%s' only has a use count of %lu, may not have been used",
                          debugStr.c_str(),
                          handle != nullptr ? handle->_conditionName.c_str() : "<NULL>",
                          numUses);
      ret = false;
      // continue looping to print all relevant warnings
    }
  }

  return ret;
}
  

} // namespace Cozmo
} // namespace Anki

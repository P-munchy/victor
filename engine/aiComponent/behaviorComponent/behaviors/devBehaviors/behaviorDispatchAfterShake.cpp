/**
 * File: behaviorDispatchAfterShake.cpp
 *
 * Author: Brad Neuman
 * Created: 2018-01-18
 *
 * Description: Simple behavior to wait for the robot to be shaken and placed down before delegating to
 *              another data-defined behavior
 *
 * Copyright: Anki, Inc. 2018
 *
 **/

#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/behaviorDispatchAfterShake.h"

#include "coretech/common/engine/utils/timer.h"

#include "engine/aiComponent/behaviorComponent/behaviorContainer.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/beiRobotInfo.h"
#include "engine/aiComponent/behaviorComponent/behaviorTypesWrapper.h"
#include "engine/components/bodyLightComponent.h"

#include "util/console/consoleInterface.h"

#include <set>

namespace Anki {
namespace Cozmo {

namespace{
  
const float kAccelMagnitudeShakingStartedThreshold = 16000.f;

// set to > 0 from console to fake the repeated "shakes" (shakes are hard to do in webots sim)
CONSOLE_VAR(unsigned int, kDevDispatchAfterShake, "DevBaseBehavior", 0);
// how long you have to shake/pause
CONSOLE_VAR_RANGED(float, kShakeTime, "DevBaseBehavior", 0.1f, 0.01f, 2.0f);
  
static const BackpackLights kLightsSteady =
{
  .onColors               = {{NamedColors::BLACK,NamedColors::BLACK,NamedColors::BLACK}},
  .offColors              = {{NamedColors::BLACK,NamedColors::BLACK,NamedColors::BLACK}},
  .onPeriod_ms            = {{100,0,0}},
  .offPeriod_ms           = {{100,0,0}},
  .transitionOnPeriod_ms  = {{0,0,0}},
  .transitionOffPeriod_ms = {{0,0,0}},
  .offset                 = {{0,0,0}}
};

static const BackpackLights kLightsShake =
{
  .onColors               = {{NamedColors::RED,NamedColors::BLACK,NamedColors::BLACK}},
  .offColors              = {{NamedColors::RED,NamedColors::BLACK,NamedColors::BLACK}},
  .onPeriod_ms            = {{100,0,0}},
  .offPeriod_ms           = {{100,0,0}},
  .transitionOnPeriod_ms  = {{0,0,0}},
  .transitionOffPeriod_ms = {{0,0,0}},
  .offset                 = {{0,0,0}}
};


}

BehaviorDispatchAfterShake::BehaviorDispatchAfterShake(const Json::Value& config)
  : ICozmoBehavior(config)
{
  for( const auto& behavior : config["behaviors"] ) {
    _delegateIDs.push_back( BehaviorTypesWrapper::BehaviorIDFromString( behavior.asString() ) );
  }
  
  ANKI_VERIFY( !_delegateIDs.empty(),
               "BehaviorDispatchAfterShake.Ctor.Empty",
               "No behavior delegates were found" );
}

void BehaviorDispatchAfterShake::OnBehaviorActivated()
{
  _countShaken = 0;
}

void BehaviorDispatchAfterShake::InitBehavior()
{
  for( const auto& delegateID : _delegateIDs ) {
    _delegates.push_back( GetBEI().GetBehaviorContainer().FindBehaviorByID(delegateID) );
    
    ANKI_VERIFY( _delegates.back() != nullptr,
                 "BehaviorDispatchAfterShake.Delegate.InvalidBehavior",
                 "could not get pointer for behavior '%s'",
                 BehaviorTypesWrapper::BehaviorIDToString(delegateID) );
  }
}

void BehaviorDispatchAfterShake::GetAllDelegates(std::set<IBehavior*>& delegates) const
{
  // copy _delegates into delegates, taking the raw ptrs
  std::transform( _delegates.begin(),
                  _delegates.end(),
                  std::inserter(delegates, delegates.end()),
                  [](const auto& x){ return x.get(); });
}

void BehaviorDispatchAfterShake::BehaviorUpdate()
{
  if(!IsActivated() || IsControlDelegated()){
    return;
  }

  const auto& robotInfo = GetBEI().GetRobotInfo();
  
  if( kDevDispatchAfterShake > 0 ) {
    
    _countShaken = kDevDispatchAfterShake;
    kDevDispatchAfterShake = 0;
    _shakingSession = false;
    
  } else {
    
    const bool isBeingShaken = (robotInfo.GetHeadAccelMagnitudeFiltered() > kAccelMagnitudeShakingStartedThreshold);
    const float currentTime = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
    if( (_shakingSession && isBeingShaken)
        || (!_shakingSession && !isBeingShaken) )
    {
      // last shake time / last steady time
      _lastChangeTime_s = currentTime;
    }
    
    const bool timeElapsed = (currentTime - _lastChangeTime_s >= kShakeTime);
    if( _shakingSession && (!isBeingShaken) && timeElapsed ) {
      // shaking stopped for a while
      _shakingSession = false;
      
      GetBEI().GetBodyLightComponent().SetBackpackLights(kLightsSteady);
    }
    if( !_shakingSession && isBeingShaken && timeElapsed ) {
      // shaking started for a while
      _shakingSession = true;
      ++_countShaken;
      
      GetBEI().GetBodyLightComponent().SetBackpackLights(kLightsShake);
    }
    // shaking is one of those words where if you write it enough times it starts to look wrong.
    
  }
  
  const bool isOnTreads = (robotInfo.GetOffTreadsState() == OffTreadsState::OnTreads);
  if( (_countShaken>0) && (!_shakingSession) && isOnTreads ) {
    // time to delegate to the data defined delegate
    size_t idx = _countShaken - 1;
    if( _countShaken > _delegates.size() ) {
      PRINT_NAMED_WARNING("BehaviorDispatchAfterShake.BehaviorUpdate.TooManyShakes",
                          "You shook the robot (%zu) times but there were only (%zu) behaviors",
                          _countShaken,
                          _delegates.size());
      idx = std::min(idx, _delegates.size()-1);
    }
    
    DEV_ASSERT( idx<_delegates.size(), "BehaviorDispatchAfterShake.Update.OutOfRange" );
    auto& delegate = _delegates[idx];
    if( ANKI_VERIFY( delegate != nullptr,
                     "BehaviorDispatchAfterShake.Update.NullDelegate",
                     "Behavior idx (%zu) is null",
                     idx ) )
    {
      if( delegate->WantsToBeActivated() ) {
        DelegateIfInControl( delegate.get() );
        
        // clear shaken count so you have to shake again to run the behavior again if it completes
        _countShaken = 0;
        return;
      }
    }
  }
}

}
}

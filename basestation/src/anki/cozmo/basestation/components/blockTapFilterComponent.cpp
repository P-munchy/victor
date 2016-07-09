/**
 * File: blockTapFilterComponent.cpp
 *
 * Author: Molly Jameson
 * Created: 2016-07-07
 *
 * Description: A component to manage time delays to only send the most intense taps
 *               from blocks sent close together, since the other taps were likely noise
 *
 * Copyright: Anki, Inc. 2016
 *
 **/
#include "anki/cozmo/basestation/components/blockTapFilterComponent.h"
#include "anki/cozmo/basestation/externalInterface/externalInterface.h"
#include "anki/cozmo/basestation/robot.h"
#include "anki/common/basestation/utils/timer.h"
#include "anki/cozmo/basestation/robotManager.h"
#include "util/console/consoleInterface.h"
#include "util/transport/connectionStats.h"
#include "util/math/math.h"

CONSOLE_VAR(int16_t, kTapIntensityMin, "TapFilter.IntesityMin", 55);
CONSOLE_VAR(Anki::TimeStamp_t, kTapWaitOffset_ms, "TapFilter.WaitOffsetTime", 75);

namespace Anki {
namespace Cozmo {


BlockTapFilterComponent::BlockTapFilterComponent(Robot& robot)
  : _robot(robot)
  ,_enabled(true)
  ,_waitToTime(0)
{
  if( _robot.GetContext()->GetRobotManager()->GetMsgHandler() )
  {
    _robotToEngineSignalHandle = (_robot.GetContext()->GetRobotManager()->GetMsgHandler()->Subscribe(_robot.GetID(),
                                                                                                     RobotInterface::RobotToEngineTag::activeObjectTapped,
                                                                                                     std::bind(&BlockTapFilterComponent::HandleActiveObjectTapped, this, std::placeholders::_1)));
  }
  // Null for unit tests
  if( _robot.GetContext()->GetExternalInterface() )
  {
    _gameToEngineSignalHandle = (_robot.GetContext()->GetExternalInterface()->Subscribe(
                                                                                        ExternalInterface::MessageGameToEngineTag::EnableBlockTapFilter,
                                                                                        std::bind(&BlockTapFilterComponent::HandleEnableTapFilter, this, std::placeholders::_1)));
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BlockTapFilterComponent::Update()
{
  if( !_tapInfo.empty() )
  {
    TimeStamp_t currTime = BaseStationTimer::getInstance()->GetCurrentTimeStamp();
    if( currTime > _waitToTime )
    {
      std::list<ObjectTapped>::iterator highIter = _tapInfo.begin();
      int16_t highIntensity = ((*highIter).tapPos - (*highIter).tapNeg);
      // grab with highest intensity from recent taps after set amount of time from first tap in group.
      for (std::list<ObjectTapped>::iterator it=_tapInfo.begin(); it != _tapInfo.end(); ++it)
      {
        int16_t currIntensity = ((*it).tapPos - (*it).tapNeg);
        if( highIntensity < currIntensity)
        {
          highIter = it;
          highIntensity = currIntensity;
        }
      }
      PRINT_CH_INFO("blocks","BlockTapFilterComponent.Update","intensity %d time: %d",highIntensity,currTime);
      _robot.Broadcast(ExternalInterface::MessageEngineToGame(ObjectTapped(*highIter)));
      _tapInfo.clear();
    }
  }
}
  
void BlockTapFilterComponent::HandleEnableTapFilter(const AnkiEvent<ExternalInterface::MessageGameToEngine>& message)
{
  if( message.GetData().GetTag() == ExternalInterface::MessageGameToEngineTag::EnableBlockTapFilter)
  {
    const Anki::Cozmo::ExternalInterface::EnableBlockTapFilter& msg = message.GetData().Get_EnableBlockTapFilter();
    _enabled = msg.enable;
    PRINT_CH_INFO("blocks","BlockTapFilterComponent.HandleEnableTapFilter","on %d",_enabled);
  }
}

  
void BlockTapFilterComponent::HandleActiveObjectTapped(const AnkiEvent<RobotInterface::RobotToEngine>& message)
{
  // We make a copy of this message so we can update the object ID before broadcasting
  ObjectTapped payload = message.GetData().Get_activeObjectTapped();
  ActiveObject* object = _robot.GetBlockWorld().GetActiveObjectByActiveID(payload.objectID);
  
  if(nullptr == object )
  {
    PRINT_NAMED_WARNING("BlockTapFilterComponent.HandleActiveObjectTapped.UnknownActiveID",
                        "Could not find match for active object ID %d", payload.objectID);
  }
  else if( object->IsActive() )
  {
    int16_t intensity = payload.tapPos - payload.tapNeg;
    Anki::TimeStamp_t engineTime = BaseStationTimer::getInstance()->GetCurrentTimeStamp();
    PRINT_CH_INFO("blocks","BlockTapFilterComponent.HandleEnableTapFilter.HandleActiveObjectTapped.MessageActiveObjectTapped",
                      "Received message that %s %d (Active ID %d) was tapped %d times (robotTime %d, tapTime %d, intensity: %d, engineTime: %d).",
                      EnumToString(object->GetType()),
                      object->GetID().GetValue(), payload.objectID, payload.numTaps,
                      payload.timestamp, payload.tapTime, intensity, engineTime);
    if( intensity > kTapIntensityMin )
    {
      // Update the ID to be the blockworld ID before broadcasting
      payload.objectID = object->GetID();
      payload.robotID = _robot.GetID();
      
      // not enabled means send immediately with no filtering.
      if( !_enabled )
      {
        _robot.Broadcast(ExternalInterface::MessageEngineToGame(ObjectTapped(payload)));
      }
      else
      {
        // A new "group" of taps is coming in, evaluate after a certain amount of time after the first one
        if( _tapInfo.empty() )
        {
          // Potentially we could add more time based on LatencyAvg if we wanted to track that in the shipping app.
          // Latency should be higher on lower end devices
          _waitToTime = engineTime + kTapWaitOffset_ms;
        }
        _tapInfo.emplace_back(std::move(payload));
      }
    }
    else
    {
      PRINT_CH_INFO("blocks", "BlockTapFilterComponent.HandleEnableTapFilter.Ignored", "Tap ignored %d < %d",intensity,kTapIntensityMin);
    }
  }
}

}
}

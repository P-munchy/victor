//
//  robotManager.cpp
//  Products_Cozmo
//
//  Created by Andrew Stein on 8/23/13.
//  Copyright (c) 2013 Anki, Inc. All rights reserved.
//

#include "engine/animations/animationContainers/cannedAnimationContainer.h"
#include "engine/animations/animationContainers/cubeLightAnimationContainer.h"
#include "engine/animations/animationGroup/animationGroupContainer.h"
#include "engine/cozmoContext.h"
#include "engine/events/animationTriggerResponsesContainer.h"
#include "engine/externalInterface/externalInterface.h"
#include "engine/firmwareUpdater/firmwareUpdater.h"
#include "engine/needsSystem/needsManager.h"
#include "engine/robot.h"
#include "engine/robotDataLoader.h"
#include "engine/robotInitialConnection.h"
#include "engine/robotInterface/messageHandler.h"
#include "engine/robotManager.h"
#include "engine/robotToEngineImplMessaging.h"
#include "anki/common/basestation/utils/timer.h"
#include "anki/common/robot/config.h"
#include "clad/externalInterface/messageEngineToGame.h"
#include "clad/types/animationTrigger.h"
#include "json/json.h"
#include "util/cpuProfiler/cpuProfiler.h"
#include "util/fileUtils/fileUtils.h"
#include "util/logging/logging.h"
#include "util/signals/simpleSignal_fwd.h"
#include "util/time/stepTimers.h"
#include <vector>
#include <sys/stat.h>

#include "anki/common/robot/config.h"
#include "util/global/globalDefinitions.h"

namespace Anki {
namespace Cozmo {

RobotManager::RobotManager(const CozmoContext* context)
: _context(context)
, _robotEventHandler(context)
, _backpackLightAnimations(context->GetDataLoader()->GetBackpackLightAnimations())
, _cannedAnimations(context->GetDataLoader()->GetCannedAnimations())
, _cubeLightAnimations(context->GetDataLoader()->GetCubeLightAnimations())
, _animationGroups(context->GetDataLoader()->GetAnimationGroups())
, _animationTriggerResponses(context->GetDataLoader()->GetAnimationTriggerResponses())
, _cubeAnimationTriggerResponses(context->GetDataLoader()->GetCubeAnimationTriggerResponses())
, _firmwareUpdater(new FirmwareUpdater(context))
, _robotMessageHandler(new RobotInterface::MessageHandler())
, _fwVersion(0)
, _fwTime(0)
{
  using namespace ExternalInterface;
  
  auto broadcastAvailableAnimationGroupsCallback = [this](const AnkiEvent<MessageGameToEngine>& event)
  {
    this->BroadcastAvailableAnimationGroups();
  };
    
  IExternalInterface* externalInterface = context->GetExternalInterface();

  MessageGameToEngineTag tagGroups = MessageGameToEngineTag::RequestAvailableAnimationGroups;
    
  if (externalInterface != nullptr){
    _signalHandles.push_back( externalInterface->Subscribe(tagGroups, broadcastAvailableAnimationGroupsCallback) );
  }
}

RobotManager::~RobotManager()
{
  DEV_ASSERT_MSG(_robots.empty(),
                 "robotmanager_robot_leak",
                 "RobotManager::~RobotManager. Not all the robots have been destroyed. This is a memory leak");
}

void RobotManager::Init(const Json::Value& config)
{
  auto startTime = std::chrono::steady_clock::now();

  Anki::Util::Time::PushTimedStep("RobotManager::Init");
  _robotMessageHandler->Init(config, this, _context);
  Anki::Util::Time::PopTimedStep(); // RobotManager::Init
  
  Anki::Util::Time::PrintTimedSteps();
  Anki::Util::Time::ClearSteps();

  auto endTime = std::chrono::steady_clock::now();
  auto timeSpent_millis = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
  
  if (ANKI_DEBUG_LEVEL >= ANKI_DEBUG_ERRORS_AND_WARNS)
  {
    constexpr auto maxInitTime_millis = 3000;
    if (timeSpent_millis > maxInitTime_millis)
    {
      PRINT_NAMED_WARNING("RobotManager.Init.TimeSpent",
                          "%lld milliseconds spent initializing, expected %d",
                          timeSpent_millis,
                          maxInitTime_millis);
    }
  }
  
  LOG_EVENT("robot.init.time_spent_ms", "%lld", timeSpent_millis);

  _firmwareUpdater->LoadHeader(FirmwareType::Current, std::bind(&RobotManager::ParseFirmwareHeader, this, std::placeholders::_1));
}

void RobotManager::AddRobot(const RobotID_t withID)
{
  if (_robots.find(withID) == _robots.end()) {
    PRINT_STREAM_INFO("RobotManager.AddRobot", "Adding robot with ID=" << withID);
    _robots[withID] = new Robot(withID, _context);
    _IDs.push_back(withID);
    _initialConnections.emplace(std::piecewise_construct,
      std::forward_as_tuple(withID),
      std::forward_as_tuple(withID, _robotMessageHandler.get(), _context->GetExternalInterface(), _fwVersion, _fwTime));
  } else {
    PRINT_STREAM_WARNING("RobotManager.AddRobot.AlreadyAdded", "Robot with ID " << withID << " already exists. Ignoring.");
  }  
}

void RobotManager::RemoveRobot(const RobotID_t withID, bool robotRejectedConnection)
{
  auto iter = _robots.find(withID);
  if(iter != _robots.end()) {
    PRINT_NAMED_INFO("RobotManager.RemoveRobot", "Removing robot with ID=%d", withID);
    
    // ask initial connection tracker if it's handling this
    bool handledDisconnect = false;
    auto initialIter = _initialConnections.find(withID);
    if (initialIter != _initialConnections.end()) {
      const auto result = robotRejectedConnection ? RobotConnectionResult::ConnectionRejected : RobotConnectionResult::ConnectionFailure;
      handledDisconnect = initialIter->second.HandleDisconnect(result);
    }
    if (!handledDisconnect) {
      _context->GetExternalInterface()->OnRobotDisconnected(withID);
      _context->GetExternalInterface()->Broadcast(ExternalInterface::MessageEngineToGame(ExternalInterface::RobotDisconnected(withID, 0.0f)));
    }

    _context->GetNeedsManager()->OnRobotDisconnected();

    delete(iter->second);
    iter = _robots.erase(iter);
    
    // Find the ID. This is inefficient, but this isn't a long list
    for(auto idIter = _IDs.begin(); idIter != _IDs.end(); ++idIter) {
      if(*idIter == withID) {
        _IDs.erase(idIter);
        break;
      }
    }
    _initialConnections.erase(withID);
    
    // Clear out the global DAS values that contain the robot hardware IDs.
    Anki::Util::sSetGlobal(DPHYS, nullptr);
    Anki::Util::sSetGlobal(DGROUP, nullptr);
  } else {
    PRINT_NAMED_WARNING("RobotManager.RemoveRobot", "Robot %d does not exist. Ignoring.", withID);
  }
}

void RobotManager::RemoveRobots()
{
  for (auto &kvp : _robots) {
    delete(kvp.second);
  }
  _robots.clear();
}

std::vector<RobotID_t> const& RobotManager::GetRobotIDList() const
{
  return _IDs;
}

// for when you don't care and you just want a damn robot
Robot* RobotManager::GetFirstRobot()
{
  if (_IDs.empty()) {
    return nullptr;
  }
  return GetRobotByID(_IDs.front());
}

// Get a pointer to a robot by ID
Robot* RobotManager::GetRobotByID(const RobotID_t robotID)
{
  auto it = _robots.find(robotID);
  if (it != _robots.end()) {
    return it->second;
  }
  
  PRINT_NAMED_WARNING("RobotManager.GetRobotByID.InvalidID", "No robot with ID=%d", robotID);
  
  return nullptr;
}

size_t RobotManager::GetNumRobots() const
{
  return _robots.size();
}

bool RobotManager::DoesRobotExist(const RobotID_t withID) const
{
  return _robots.count(withID) > 0;
}


bool RobotManager::InitUpdateFirmware(FirmwareType type, int version)
{
  bool success = _firmwareUpdater->InitUpdate(_robots, type, version);
  
  if (success)
  {
    for (const auto& kv : _robots)
    {
      const auto robotID = kv.second->GetID();
      if (!ANKI_VERIFY(MakeRobotFirmwareUntrusted(robotID),
                       "RobotManager.InitUpdateFirmware",
                       "Error making firmware untrusted for robotID: %d", robotID))
      {
        success = false;
      }
    }
  }
  
  return success;
}


bool RobotManager::UpdateFirmware()
{
  return _firmwareUpdater->Update(_robots);
}


void RobotManager::UpdateAllRobots()
{
  ANKI_CPU_PROFILE("RobotManager::UpdateAllRobots");
  
  //for (auto &r : _robots) {
  for(auto r = _robots.begin(); r != _robots.end(); ) {
    // Call update
    const RobotID_t robotId = r->first; // have to cache this prior to any ++r calls...
    Robot* robot = r->second;
    Result result = robot->Update();
    
    switch(result)
    {
      case RESULT_FAIL_IO_TIMEOUT:
      {
        PRINT_NAMED_WARNING("RobotManager.UpdateAllRobots.FailIOTimeout", "Signaling robot disconnect");
        const RobotID_t robotIdToRemove = r->first;
        ++r;
        const bool robotRejectedConnection = false;
        RemoveRobot(robotIdToRemove, robotRejectedConnection);
        
        break;
      }
        
        // TODO: Handle other return results here
        
      default:
        // No problems, simply move to next robot
        ++r;
        break;
    }

    if(robot->HasReceivedRobotState()) {
      _context->GetExternalInterface()->Broadcast(ExternalInterface::MessageEngineToGame(robot->GetRobotState()));
    } else {
      PRINT_NAMED_WARNING("RobotManager.UpdateAllRobots",
                          "Not sending robot %d state (none available).",
                          robotId);
    }
  } // End loop on _robots
  
}

void RobotManager::UpdateRobotConnection()
{
  ANKI_CPU_PROFILE("RobotManager::UpdateRobotConnection");
  _robotMessageHandler->ProcessMessages();
}

void RobotManager::ReadAnimationDir()
{
  _context->GetDataLoader()->LoadAnimations();
}

void RobotManager::ReadFaceAnimationDir()
{
  _context->GetDataLoader()->LoadFaceAnimations();
}

void RobotManager::BroadcastAvailableAnimationGroups()
{
  Anki::Util::Time::ScopedStep scopeTimer("BroadcastAvailableAnimationGroups");
  if (nullptr != _context->GetExternalInterface()) {
    std::vector<std::string> animNames(_animationGroups->GetAnimationGroupNames());
    for (std::vector<std::string>::iterator i=animNames.begin(); i != animNames.end(); ++i) {
      _context->GetExternalInterface()->BroadcastToGame<ExternalInterface::AnimationGroupAvailable>(*i);
    }
  }
}

bool RobotManager::HasCannedAnimation(const std::string& animName)
{
  return _cannedAnimations->HasAnimation(animName);
}
bool RobotManager::HasAnimationGroup(const std::string& groupName)
{
  return _animationGroups->HasGroup(groupName);
}
bool RobotManager::HasAnimationForTrigger( AnimationTrigger ev )
{
  return _animationTriggerResponses->HasResponse(ev);
}
std::string RobotManager::GetAnimationForTrigger( AnimationTrigger ev )
{
  return _animationTriggerResponses->GetResponse(ev);
}
bool RobotManager::HasCubeAnimationForTrigger( CubeAnimationTrigger ev )
{
  return _cubeAnimationTriggerResponses->HasResponse(ev);
}
std::string RobotManager::GetCubeAnimationForTrigger( CubeAnimationTrigger ev )
{
  return _cubeAnimationTriggerResponses->GetResponse(ev);
}

void RobotManager::ParseFirmwareHeader(const Json::Value& header)
{
  JsonTools::GetValueOptional(header, FirmwareUpdater::kFirmwareVersionKey, _fwVersion);
  JsonTools::GetValueOptional(header, FirmwareUpdater::kFirmwareTimeKey, _fwTime);
  if (_fwVersion == 0 || _fwTime == 0) {
    PRINT_NAMED_WARNING("RobotManager.ParseFirmwareHeader", "got version %d, time %d", _fwVersion, _fwTime);
  }
}

bool RobotManager::ShouldFilterMessage(const RobotID_t robotId, const RobotInterface::RobotToEngineTag msgType) const
{
  auto iter = _initialConnections.find(robotId);
  if (iter == _initialConnections.end()) {
    return false;
  }
  return iter->second.ShouldFilterMessage(msgType);
}

bool RobotManager::ShouldFilterMessage(const RobotID_t robotId, const RobotInterface::EngineToRobotTag msgType) const
{
  auto iter = _initialConnections.find(robotId);
  if (iter == _initialConnections.end()) {
    return false;
  }
  return iter->second.ShouldFilterMessage(msgType);
}

void RobotManager::ConnectRobotToNeedsManager(u32 serialNumber) const
{
  _context->GetNeedsManager()->InitAfterSerialNumberAcquired(serialNumber);
}

bool RobotManager::MakeRobotFirmwareUntrusted(RobotID_t robotId)
{
  auto iter = _initialConnections.find(robotId);
  if (iter == _initialConnections.end()) {
    return false;
  }
  iter->second.MakeFirmwareUntrusted();
  return true;
}

} // namespace Cozmo
} // namespace Anki

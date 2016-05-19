//
//  robot.h
//  Products_Cozmo
//
//     RobotManager class for keeping up with available robots, by their ID.
//
//  Created by Andrew Stein on 8/23/13.
//  Copyright (c) 2013 Anki, Inc. All rights reserved.
//

#ifndef ANKI_COZMO_BASESTATION_ROBOTMANAGER_H
#define ANKI_COZMO_BASESTATION_ROBOTMANAGER_H

#include "anki/cozmo/basestation/robotEventHandler.h"
#include "util/signals/simpleSignal.hpp"
#include "util/helpers/noncopyable.h"
#include "clad/types/gameEvent.h"
#include <map>
#include <vector>
#include <memory>
#include <unordered_map>
#include <string>


namespace Json {
  class Value;
}

namespace Anki {
  
namespace Util {
namespace Data {
  class DataPlatform;
}
}

namespace Cozmo {
  
// Forward declarations:
namespace RobotInterface {
class MessageHandler;
}
class Robot;
class IExternalInterface;
class CozmoContext;
class CannedAnimationContainer;
class AnimationGroupContainer;
class FirmwareUpdater;
class GameEventResponsesContainer;

class RobotManager : Util::noncopyable
{
public:

  RobotManager(const CozmoContext* context);
  
  ~RobotManager();
  
  void Init(const Json::Value& config);
  
  // Get the list of known robot ID's
  std::vector<RobotID_t> const& GetRobotIDList() const;

  // for when you don't care and you just want a damn robot
  Robot* GetFirstRobot();

  // Get a pointer to a robot by ID
  Robot* GetRobotByID(const RobotID_t robotID);
  
  // Check whether a robot exists
  bool DoesRobotExist(const RobotID_t withID) const;
  
  // Add / remove robots
  void AddRobot(const RobotID_t withID);
  void RemoveRobot(const RobotID_t withID);
  
  // Call each Robot's Update() function
  void UpdateAllRobots();
  
  // Update robot connection state
  void UpdateRobotConnection();
  
  // Attempt to begin updating firmware to specified version (return false if it cannot begin)
  bool InitUpdateFirmware(int version);
  
  // Update firmware (if appropriate) on every connected robot
  bool UpdateFirmware();
  
  // Return a
  // Return the number of availale robots
  size_t GetNumRobots() const;

  // Events
  using RobotDisconnectedSignal = Signal::Signal<void (RobotID_t)>;
  RobotDisconnectedSignal& OnRobotDisconnected() { return _robotDisconnectedSignal; }

  CannedAnimationContainer& GetCannedAnimations() { return *_cannedAnimations; }
  AnimationGroupContainer& GetAnimationGroups() { return *_animationGroups; }
  
  bool HasCannedAnimation(const std::string& animName);
  bool HasAnimationGroup(const std::string& groupName);
  bool HasAnimationResponseForEvent( GameEvent ev );
  std::string GetAnimationResponseForEvent( GameEvent ev );
  
  // Read the animations in a dir
  void ReadAnimationDir();

  // Iterate through the loaded animations and broadcast their names
  void BroadcastAvailableAnimations();
  
  using RobotMap = std::map<RobotID_t,Robot*>;
  const RobotMap& GetRobotMap() const { return _robots; }
  RobotInterface::MessageHandler* GetMsgHandler() const { return _robotMessageHandler.get(); }

protected:
  RobotDisconnectedSignal _robotDisconnectedSignal;
  RobotMap _robots;
  std::vector<RobotID_t>     _IDs;
  const CozmoContext* _context;
  RobotEventHandler _robotEventHandler;
  std::unique_ptr<CannedAnimationContainer>   _cannedAnimations;
  std::unique_ptr<AnimationGroupContainer>    _animationGroups;
  std::unique_ptr<FirmwareUpdater>            _firmwareUpdater;
  std::unordered_map<std::string, time_t> _loadedAnimationFiles;
  std::unordered_map<std::string, time_t> _loadedAnimationGroupFiles;
  std::unique_ptr<GameEventResponsesContainer> _gameEventResponses;
  std::unique_ptr<RobotInterface::MessageHandler> _robotMessageHandler;
  
  void ReadAnimationDirImpl(const std::string& animationDir);
  void ReadAnimationFile(const char* filename);

  void ReadAnimationGroupDir();
  void ReadAnimationGroupFile(const char* filename);
  
}; // class RobotManager
  
} // namespace Cozmo
} // namespace Anki


#endif // ANKI_COZMO_BASESTATION_ROBOTMANAGER_H

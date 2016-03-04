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
#include <map>
#include <vector>
#include <memory>

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

    class RobotManager
    {
    public:
    
      RobotManager(const CozmoContext* context);
      
      ~RobotManager();
      
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
      
      // Return a
      // Return the number of availale robots
      size_t GetNumRobots() const;

      // Events
      using RobotDisconnectedSignal = Signal::Signal<void (RobotID_t)>;
      RobotDisconnectedSignal& OnRobotDisconnected() { return _robotDisconnectedSignal; }

      CannedAnimationContainer& GetCannedAnimationContainer() { return *_cannedAnimationContainer; }

    protected:
      RobotDisconnectedSignal _robotDisconnectedSignal;
      std::map<RobotID_t,Robot*> _robots;
      std::vector<RobotID_t>     _IDs;
      const CozmoContext* _context;
      RobotEventHandler _robotEventHandler;
      std::unique_ptr<CannedAnimationContainer> _cannedAnimationContainer;
      
    }; // class RobotManager
    
    
  } // namespace Cozmo
} // namespace Anki

#endif // ANKI_COZMO_BASESTATION_ROBOTMANAGER_H

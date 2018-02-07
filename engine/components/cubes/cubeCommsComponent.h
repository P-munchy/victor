/**
 * File: cubeCommsComponent.h
 *
 * Author: Matt Michini
 * Created: 11/29/2017
 *
 * Description: Component for managing communications with light cubes
 *
 * Copyright: Anki, Inc. 2017
 *
 **/

#ifndef __Engine_Components_CubeCommsComponent_H__
#define __Engine_Components_CubeCommsComponent_H__

#include "engine/cozmoObservableObject.h" // alias ActiveID

#include "cubeBleClient/cubeBleClient.h" // alias BleFactoryId (should be moved to CLAD)
#include "util/entityComponent/entity.h"

#include "util/helpers/noncopyable.h"
#include "util/signals/simpleSignal_fwd.h" // Signal::SmartHandle

#include <unordered_map>

namespace Anki {
namespace Cozmo {
  
// forware decl:
class Robot;
template <typename Type>
class AnkiEvent;
struct ObjectAvailable;
struct ObjectAccel;
namespace BlockMessages {
  class LightCubeMessage;
}
namespace ExternalInterface {
  class MessageGameToEngine;
  class MessageEngineToGame;
}
  
class CubeCommsComponent : public IDependencyManagedComponent<RobotComponentID>, private Util::noncopyable
{
public:
  CubeCommsComponent();
  ~CubeCommsComponent() = default;

  //////
  // IDependencyManagedComponent functions
  //////
  virtual void InitDependent(Cozmo::Robot* robot, const RobotCompMap& dependentComponents) override;
  // Maintain the chain of initializations currently in robot - it might be possible to
  // change the order of initialization down the line, but be sure to check for ripple effects
  // when changing this function
  virtual void GetInitDependencies(RobotCompIDSet& dependencies) const override {
    dependencies.insert(RobotComponentID::CubeAccel);
  };
  virtual void GetUpdateDependencies(RobotCompIDSet& dependencies) const override {};
  //////
  // end IDependencyManagedComponent functions
  //////

  void Update();
  
  // Enter or leave discovery mode for the requested duration (uses default if none specified).
  // In discover mode, component will listen for advertising cubes and connect to the 'best' ones.
  // If any cubes are connected, they will be disconnected.
  void EnableDiscovery(const bool enable = true, const float discoveryTime_sec = 0.f);
  
  // Interface for other components to send messages to light cubes by ActiveId
  bool SendLightCubeMessage(const ActiveID&, const BlockMessages::LightCubeMessage&);

  // Start/stop ObjectAccel message streaming from the specified cube
  void SetStreamObjectAccel(const ActiveID&, const bool enable=true);
  
  // Sends current available cube list to game
  void SendBlockPoolData() const;
  
  // Game to engine event handlers
  void HandleGameEvents(const AnkiEvent<ExternalInterface::MessageGameToEngine>&);
  
private:
  
  Robot* _robot = nullptr;
  
  CubeBleClient* _cubeBleClient;
  
  // Handles for grabbing GameToEngine messages
  std::vector<Signal::SmartHandle> _signalHandles;
    
  // Handlers for messages from CubeBleClient:
  
  // Handler for ObjectAvailable advertisement message
  void HandleObjectAvailable(const ObjectAvailable&);
  
  // Handler for messages from light cubes
  void HandleLightCubeMessage(const BleFactoryId&, const BlockMessages::LightCubeMessage&);
  
  // Handler for when light cube BLE connection is established/unestablished
  void HandleConnectionStateChange(const BleFactoryId&, const bool connected);
  
  // If discovering, then we are listening for any advertising cubes and
  // selecting the best ones to connect to.
  bool _discovering = false;
  float _discoveringEndTime_sec = 0.f;
  
  // Next time we're supposed to check for disconnections
  float _nextDisconnectCheckTime_sec = 0.f;
  
  // Whether or not to broadcast incoming ObjectAvailable messages to game
  bool _broadcastObjectAvailableMsg = false;
  
  struct CubeInfo
  {
    BleFactoryId factoryId;
    ObjectType objectType;
    float lastHeardTime_sec;
    int lastRssi;
    bool connected;
  };
  
  // The main list of cubes we know about:
  std::map<ActiveID, CubeInfo> _availableCubes;
  
  // Convenience map of factoryID to ActiveID for quicker lookup based on factoryID:
  std::unordered_map<BleFactoryId, ActiveID> _factoryIdToActiveIdMap;
  
  // AddCubeToList() generates a new activeId and adds the cube to the _availableCubes list if it's not in there already.
  // Returns true if it was added, false if already there.
  bool AddCubeToList(const CubeInfo&);
  
  // Remove cube from the list based on BleFactoryId. Returns true if cube was successfully removed.
  bool RemoveCubeFromList(const BleFactoryId&);
  
  // Clear the list of cubes
  void ClearList();
  
  // Find a cube in the list by ActiveID and return a pointer to it. Returns nullptr if not found.
  CubeInfo* GetCubeByActiveId(const ActiveID&);
  
  // Find a cube in the list by factoryId and return a pointer to it. Returns nullptr if not found.
  CubeInfo* GetCubeByFactoryId(const BleFactoryId&);
  
};


} // Cozmo namespace
} // Anki namespace

#endif // __Engine_Components_CubeCommsComponent_H__

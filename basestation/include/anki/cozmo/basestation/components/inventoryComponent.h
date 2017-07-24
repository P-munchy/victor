/**
 * File: inventoryComponent.h
 *
 * Author: Molly Jameson
 * Created: 2017-05-24
 *
 * Description: A component to manage inventory
 *
 * Copyright: Anki, Inc. 2017
 *
 **/

#ifndef __Anki_Cozmo_Basestation_Components_InventoryComponent_H__
#define __Anki_Cozmo_Basestation_Components_InventoryComponent_H__

#include "clad/types/inventoryTypes.h"
#include "json/json-forwards.h"
#include "util/helpers/noncopyable.h"
#include "util/signals/simpleSignal_fwd.h"

#include <chrono>
#include <map>
#include <vector>

namespace Anki {
namespace Cozmo {

class Robot;
class CozmoContext;

class InventoryComponent : private Util::noncopyable
{
public:
  static const int kInfinity = -1;

  explicit InventoryComponent(Robot& robot);

  void Init(const Json::Value& config);
  void Update(const float currentTime_s);
  
  void SetInventoryAmount(InventoryType inventoryID, int total);
  void AddInventoryAmount(InventoryType inventoryID, int delta);
  int  GetInventoryAmount(InventoryType inventoryID) const;
  int  GetInventorySpaceRemaining(InventoryType inventoryID) const;
  
  // Handle various message types
  template<typename T>
  void HandleMessage(const T& msg);
  
  void SendInventoryAllToGame();

private:

  Robot& _robot;

  void TryWriteCurrentInventoryToRobot();
  void WriteCurrentInventoryToRobot();
  void ReadCurrentInventoryFromRobot();
  
  int GetInventoryCap(InventoryType inventoryID) const;
  
  // clad array for easy unpacking
  InventoryList _currentInventory;
  bool          _readFromRobot;
  
  std::chrono::time_point<std::chrono::system_clock>  _timeLastWrittenToRobot;
  bool          _robotWritePending;
  bool          _isWritingToRobot;
  
  std::vector<Signal::SmartHandle> _signalHandles;

  std::map<InventoryType, int>  _inventoryTypeCaps;
};

}
}

#endif

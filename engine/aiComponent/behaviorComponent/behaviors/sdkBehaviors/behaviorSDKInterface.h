/**
 * File: BehaviorSDKInterface.h
 *
 * Author: Michelle Sintov
 * Created: 2018-05-21
 *
 * Description: Interface for SDKs including C# and Python
 *
 * Copyright: Anki, Inc. 2018
 *
 **/

#ifndef __Engine_AiComponent_BehaviorComponent_Behaviors_BehaviorSDKInterface__
#define __Engine_AiComponent_BehaviorComponent_Behaviors_BehaviorSDKInterface__

#include "engine/aiComponent/behaviorComponent/behaviors/iCozmoBehavior.h"

namespace Anki {
namespace Cozmo {

class BehaviorDriveOffCharger;
class BehaviorGoHome;
class IGatewayInterface;
namespace external_interface {
  class DriveOffChargerRequest;
  class DriveOnChargerRequest;
}
  
class BehaviorSDKInterface : public ICozmoBehavior
{
protected:
  // Enforce creation through BehaviorFactory
  friend class BehaviorFactory;
  explicit BehaviorSDKInterface(const Json::Value& config);  

  virtual void GetBehaviorOperationModifiers(BehaviorOperationModifiers& modifiers) const override;
  virtual void GetAllDelegates(std::set<IBehavior*>& delegates) const override;
  virtual void GetBehaviorJsonKeys(std::set<const char*>& expectedKeys) const override;
  
  virtual void InitBehavior() override;
  virtual bool WantsToBeActivatedBehavior() const override;
  virtual void OnBehaviorActivated() override;
  virtual void BehaviorUpdate() override;
  virtual void OnBehaviorDeactivated() override;
  
  virtual void HandleWhileActivated(const AppToEngineEvent& event) override;

private:
  void DriveOffChargerRequest(const external_interface::DriveOffChargerRequest& driveOffChargerRequest);
  void DriveOnChargerRequest(const external_interface::DriveOnChargerRequest& driveOnChargerRequest);

  void HandleDriveOffChargerComplete();
  void HandleDriveOnChargerComplete();

  struct InstanceConfig {
    InstanceConfig();

    std::string driveOffChargerBehaviorStr;
    ICozmoBehaviorPtr driveOffChargerBehavior;

    std::string goHomeBehaviorStr;
    ICozmoBehaviorPtr goHomeBehavior;
  };

  struct DynamicVariables {
    DynamicVariables();
    // TODO: put member variables here
  };

  InstanceConfig _iConfig;
  DynamicVariables _dVars;
  
  std::vector<Signal::SmartHandle> _signalHandles;
  AnkiEventMgr<external_interface::GatewayWrapper> _eventMgr;
};
} // namespace Cozmo
} // namespace Anki

#endif // __Engine_AiComponent_BehaviorComponent_Behaviors_BehaviorSDKInterface__

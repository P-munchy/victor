/**
* File: settingsCommManager.h
*
* Author: Paul Terry
* Created: 6/15/18
*
* Description: Communicates settings with App and Cloud; calls into SettingsManager
*
* Copyright: Anki, Inc. 2018
*
**/

#ifndef __Cozmo_Basestation_Components_settingsCommManager_H__
#define __Cozmo_Basestation_Components_settingsCommManager_H__

#include "engine/cozmoContext.h"
#include "engine/robotComponents_fwd.h"

#include "util/entityComponent/iDependencyManagedComponent.h"
#include "util/helpers/noncopyable.h"
#include "util/signals/simpleSignal_fwd.h"

#include "clad/types/robotSettingsTypes.h"

namespace Anki {
namespace Vector {

template <typename T>
class AnkiEvent;
class IGatewayInterface;
class SettingsManager;
class JdocsManager;
namespace external_interface {
  class GatewayWrapper;
  class PullJdocsRequest;
  class PushJdocsRequest;
  class UpdateSettingsRequest;
}

class SettingsCommManager : public IDependencyManagedComponent<RobotComponentID>,
                            private Anki::Util::noncopyable
{
public:
  SettingsCommManager();

  //////
  // IDependencyManagedComponent functions
  //////
  virtual void InitDependent(Robot* robot, const RobotCompMap& dependentComponents) override;
  virtual void GetInitDependencies(RobotCompIDSet& dependencies) const override {
    dependencies.insert(RobotComponentID::CozmoContextWrapper);
    dependencies.insert(RobotComponentID::SettingsManager);
    dependencies.insert(RobotComponentID::JdocsManager);
  };
  virtual void GetUpdateDependencies(RobotCompIDSet& dependencies) const override {
  };
  virtual void UpdateDependent(const RobotCompMap& dependentComps) override;
  //////
  // end IDependencyManagedComponent functions
  //////

  bool HandleRobotSettingChangeRequest(const RobotSetting robotSetting,
                                       const Json::Value& settingJson,
                                       const bool updateSettingsJdoc = false);
  bool ToggleRobotSettingHelper(const RobotSetting robotSetting);

  void RefreshConsoleVars();

private:

  void HandleEvents(const AnkiEvent<external_interface::GatewayWrapper>& event);
  void OnRequestPullJdocs     (const external_interface::PullJdocsRequest& pullJdocsRequest);
  void OnRequestPushJdocs     (const external_interface::PushJdocsRequest& pushJdocsRequest);
  void OnRequestUpdateSettings(const external_interface::UpdateSettingsRequest& updateSettingsRequest);

  SettingsManager*    _settingsManager = nullptr;
  JdocsManager*       _jdocsManager = nullptr;
  IGatewayInterface*  _gatewayInterface = nullptr;

  std::vector<Signal::SmartHandle> _signalHandles;
};


} // namespace Vector
} // namespace Anki

#endif // __Cozmo_Basestation_Components_settingsCommManager_H__

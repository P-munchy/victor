/**
 * File: powerStateManager.cpp
 *
 * Author: Brad Neuman
 * Created: 2018-06-27
 *
 * Description: Central engine component to manage power states (i.e. "power save mode")
 *
 * Copyright: Anki, Inc. 2018
 *
 **/

#include "engine/components/powerStateManager.h"

#include "clad/types/imageTypes.h"
#include "engine/components/visionComponent.h"
#include "engine/cozmoContext.h"
#include "engine/robotInterface/messageHandler.h"
#include "engine/robotManager.h"
#include "platform/camera/cameraService.h"
#include "util/console/consoleInterface.h"
#include "util/entityComponent/dependencyManagedEntity.h"
#include "util/helpers/boundedWhile.h"
#include "util/logging/logging.h"

namespace Anki {
namespace Cozmo {

namespace {

#define CONSOLE_GROUP "PowerSave"

// TODO:(bn) re-enable. trigger word not working with this yet until Lee's changes go in
CONSOLE_VAR( bool, kPowerSave_CalmMode, CONSOLE_GROUP, false);

CONSOLE_VAR( bool, kPowerSave_Camera, CONSOLE_GROUP, true);

}

PowerStateManager::PowerStateManager()
  : IDependencyManagedComponent<RobotComponentID>(this, RobotComponentID::PowerStateManager)
  , UnreliableComponent<BCComponentID>(this, BCComponentID::PowerStateManager)
{
}

void PowerStateManager::InitDependent(Cozmo::Robot* robot, const RobotCompMap& dependentComps)
{
  _context = dependentComps.GetComponent<ContextWrapper>().context;
}
  
void PowerStateManager::UpdateDependent(const RobotCompMap& dependentComps)
{
  if( !ANKI_VERIFY( _context != nullptr, "PowerStateManager.Update.NoContext", "" ) ) {
    return;
  }

  const bool shouldBeInPowerSave = !_powerSaveRequests.empty();
  if( shouldBeInPowerSave != _inPowerSaveMode ) {
    if( shouldBeInPowerSave ) {
      EnterPowerSave(dependentComps);
    }
    else {
      ExitPowerSave(dependentComps);
    }
  }

  if( _cameraState == CameraState::ShouldDelete ) {
    auto& visionComponent = dependentComps.GetComponent<VisionComponent>();
    if( visionComponent.TryReleaseInternalImages() ) {
      CameraService::getInstance()->DeleteCamera();
      _cameraState = CameraState::Deleted;
    }
  }
}

void PowerStateManager::RequestPowerSaveMode(const std::string& requester)
{
  PRINT_CH_DEBUG("PowerStates", "PowerStateManager.Update.AddRequest",
                 "Adding power save request from '%s'",
                 requester.c_str());
  _powerSaveRequests.insert(requester);
}

bool PowerStateManager::RemovePowerSaveModeRequest(const std::string& requester)
{
  const size_t numRemoved = _powerSaveRequests.erase(requester);

  PRINT_CH_DEBUG("PowerStates", "PowerStateManager.Update.RemoveRequest",
                 "Removed %zu requests for '%s'",
                 numRemoved,
                 requester.c_str());
  
  return (numRemoved > 0);
}

  
void PowerStateManager::TogglePowerSaveSetting( const RobotCompMap& components,
                                                PowerSaveSetting setting,
                                                bool savePower )
{
  const bool currentlyEnabled = _enabledSettings.find(setting) != _enabledSettings.end();

  if( savePower && currentlyEnabled ) {
    PRINT_NAMED_WARNING("PowerStateManager.Toggle.DoubleEnable",
                        "Attempting to enable power save mode twice");
    // TODO:(bn) enum to string
    return;
  }
  if( !savePower && !currentlyEnabled ) {
    PRINT_NAMED_WARNING("PowerStateManager.Toggle.DoubleDisable",
                        "Attempting to disable power save mode twice");
    return;
  }

  bool result = true;
  
  switch( setting ) {
    case PowerSaveSetting::CalmMode: {
      const bool calibOnDisable = true;
      Result sendResult = _context->GetRobotManager()->GetMsgHandler()->SendMessage(
        RobotInterface::EngineToRobot(RobotInterface::CalmPowerMode(savePower, calibOnDisable)));
      result = (sendResult == RESULT_OK);
      break;
    }

    case PowerSaveSetting::Camera: {
      if( !CameraService::hasInstance() ) {
        PRINT_NAMED_WARNING("PowerStateManager.Toggle.CameraService.NoInstance",
                            "Trying to interact with camera service, but it doesn't exist");        
        result = false;
      }
      else {
        auto& visionComponent = components.GetComponent<VisionComponent>();
        if( savePower ) {
          visionComponent.Pause(true);

          if( _cameraState != CameraState::Deleted ) {
            _cameraState = CameraState::ShouldDelete;
          }
        }
        else {
          if( _cameraState == CameraState::Deleted ) {
            if( CameraService::getInstance()->InitCamera() == RESULT_OK ) {
              _cameraState = CameraState::Running;
            }
            else {
              PRINT_NAMED_ERROR("PowerStateManager.Toggle.FailedToInitCamera",
                                "Camera service init failed! Camera may be in a bad state");
            }
          }
          else {
            _cameraState = CameraState::Running;
          }

          visionComponent.Pause(false);
        }
      }

      break;
    }
  }

  if( result && savePower ) {
    _enabledSettings.insert(setting);
  }
  else if( result && !savePower ) {
    _enabledSettings.erase(setting);
  }  
}


void PowerStateManager::EnterPowerSave(const RobotCompMap& components)
{
  PRINT_CH_INFO("PowerStates", "PowerStateManager.Enter",
                "Entering power save mode");

  if( kPowerSave_CalmMode ) {
    TogglePowerSaveSetting( components, PowerSaveSetting::CalmMode, true );
  }

  if( kPowerSave_Camera ) {
    TogglePowerSaveSetting( components, PowerSaveSetting::Camera, true );
  }

  _inPowerSaveMode = true;
}

void PowerStateManager::ExitPowerSave(const RobotCompMap& components)
{
  PRINT_CH_INFO("PowerStates", "PowerStateManager.Exit",
                "Exiting power save mode");

  BOUNDED_WHILE( 100, !_enabledSettings.empty() ) {
    const auto setting = *_enabledSettings.begin();
    TogglePowerSaveSetting( components, setting, false );
  }
  
  _inPowerSaveMode = false;
}

}
}

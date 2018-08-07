/**
* File: appCubeConnectionSubscriber.cpp
*
* Author: Matt Michini
* Created: 7/11/18
*
* Description: Class which handles cube connection requests from the app/sdk layer
*              and subscribes to the CubeConnectionCoordinator as necessary.
*
* Copyright: Anki, Inc. 2018
*
**/

#include "engine/components/cubes/appCubeConnectionSubscriber.h"

#include "engine/activeObject.h"
#include "engine/blockWorld/blockWorld.h"
#include "engine/components/cubes/cubeCommsComponent.h"
#include "engine/components/cubes/cubeConnectionCoordinator.h"
#include "engine/components/cubes/cubeLights/cubeLightComponent.h"
#include "engine/externalInterface/externalMessageRouter.h"
#include "engine/externalInterface/gatewayInterface.h"
#include "engine/robot.h"

#include "proto/external_interface/messages.pb.h"
#include "proto/external_interface/shared.pb.h"

#include "util/logging/logging.h"

namespace Anki{
namespace Vector{

AppCubeConnectionSubscriber::AppCubeConnectionSubscriber()
: IDependencyManagedComponent(this, RobotComponentID::AppCubeConnectionSubscriber)
{
  _appToEngineTags = {{
    AppToEngineTag::kConnectCubeRequest,
    AppToEngineTag::kDisconnectCubeRequest,
    AppToEngineTag::kFlashCubeLightsRequest,
    AppToEngineTag::kForgetPreferredCubeRequest,
    AppToEngineTag::kSetPreferredCubeRequest
  }};
}


AppCubeConnectionSubscriber::~AppCubeConnectionSubscriber()
{
}


void AppCubeConnectionSubscriber::GetInitDependencies(RobotCompIDSet& dependencies) const
{
  dependencies.insert(RobotComponentID::CozmoContextWrapper);
}


void AppCubeConnectionSubscriber::InitDependent(Robot* robot, const RobotCompMap& dependentComps)
{
  DEV_ASSERT(robot != nullptr && robot->HasGatewayInterface(),
             "AppCubeConnectionSubscriber.InitDependent.NoGatewayInterface");

  _robot = robot;
  _gi = robot->GetGatewayInterface();

  // Subscribe to app->engine tags
  for (const auto& tag : _appToEngineTags) {
    _eventHandles.push_back(_gi->Subscribe(tag, std::bind(&AppCubeConnectionSubscriber::HandleAppRequest,
                                                          this, std::placeholders::_1)));
  }

}


std::string AppCubeConnectionSubscriber::GetCubeConnectionDebugName() const {
  static const std::string debugName = "AppCubeConnectionSubscriber";
  return debugName;
}

void AppCubeConnectionSubscriber::ConnectedCallback(ECubeConnectionType connectionType)
{
  switch(connectionType){
    case ECubeConnectionType::Interactable:
    {
      PRINT_NAMED_INFO("AppCubeConnectionSubscriber.ConnectedCallback.ConnectionAttemptSuccess",
                      "Connection attempt succeeded. Sending message to gateway");

      auto* connectResultMsg = new external_interface::ConnectCubeResponse;
      connectResultMsg->set_success(true);

      const auto& activeId = _robot->GetCubeCommsComponent().GetConnectedCubeActiveId();
      const auto* object = _robot->GetBlockWorld().GetConnectedActiveObjectByActiveID(activeId);
      connectResultMsg->set_object_id(object->GetID());
      connectResultMsg->set_factory_id(object->GetFactoryID().c_str());
      _gi->Broadcast(ExternalMessageRouter::WrapResponse(connectResultMsg));

      break;
    }
    case ECubeConnectionType::Background: 
    {
      PRINT_NAMED_INFO("AppCubeConnectionSubscriber.ConnectedCallback.ConnectedBackground",
                       "Cube was already connected in background. Waiting for transition to Interactable connection.");
      break;
    }
  }
}

void AppCubeConnectionSubscriber::ConnectionFailedCallback()
{
  PRINT_NAMED_INFO("AppCubeConnectionSubscriber.ConnectionFailedCallback.ConnectionAttemptFailed",
                   "Connection attempt failed. Sending message to gateway");

  auto* connectResultMsg = new external_interface::ConnectCubeResponse;
  connectResultMsg->set_success(false);
  _gi->Broadcast(ExternalMessageRouter::WrapResponse(connectResultMsg));
}


void AppCubeConnectionSubscriber::ConnectionLostCallback()
{
  PRINT_NAMED_INFO("AppCubeConnectionSubscriber.ConnectionLostCallback.LostConnection",
                   "Lost connection to cube. Sending message to gateway");

  auto* connectResultMsg = new external_interface::CubeConnectionLost;
  _gi->Broadcast(ExternalMessageRouter::Wrap(connectResultMsg));
}


void AppCubeConnectionSubscriber::HandleAppRequest(const AppToEngineEvent& event)
{
  using namespace external_interface;

  auto& ccc = _robot->GetCubeConnectionCoordinator();

  switch(event.GetData().GetTag()) {
    case GatewayWrapperTag::kConnectCubeRequest:
    {
      PRINT_NAMED_INFO("AppCubeConnectionSubscriber.HandleAppRequest.SubscribeRequest",
                       "Received a request for cube connection from gateway. Subscribing to interactable connection");
      const bool background = false;
      ccc.SubscribeToCubeConnection(this, background);
      break;
    }
    case GatewayWrapperTag::kDisconnectCubeRequest:
    {
      PRINT_NAMED_INFO("AppCubeConnectionSubscriber.HandleAppRequest.UnsubscribeRequest",
                       "App is done with the cube connection. Requesting to unsubscribe");
      if (!ccc.UnsubscribeFromCubeConnection(this)) {
        PRINT_NAMED_WARNING("AppCubeConnectionSubscriber.HandleAppRequest.UnsubscribeFailed",
                            "Failed to unsubscribe from our cube connection. Did we have one in the first place?");
      }
      break;
    }
    case GatewayWrapperTag::kFlashCubeLightsRequest:
    {
      PRINT_NAMED_INFO("AppCubeConnectionSubscriber.HandleAppRequest.FlashCubeLightsRequest",
                       "Received a request from gateway to flash cube lights.");
      if (ccc.IsConnectedToCube()) {
        const auto& activeId = _robot->GetCubeCommsComponent().GetConnectedCubeActiveId();
        const auto* object = _robot->GetBlockWorld().GetConnectedActiveObjectByActiveID(activeId);
        if (object != nullptr) {
          _robot->GetCubeLightComponent().PlayLightAnimByTrigger(object->GetID(), CubeAnimationTrigger::Flash);
        }
      } else {
        PRINT_NAMED_WARNING("AppCubeConnectionSubscriber.HandleAppRequest.FlashCubeLightsRequest.NotConnected",
                            "Cannot flash cube lights - not connected to any cube!");
      }
      break;
    }
    case GatewayWrapperTag::kForgetPreferredCubeRequest:
    {
      PRINT_NAMED_INFO("AppCubeConnectionSubscriber.HandleAppRequest.ForgetPreferredCubeRequest",
                       "Received a request from gateway to forget our preferred cube.");
      _robot->GetCubeCommsComponent().ForgetPreferredCube();
      break;
    }
    case GatewayWrapperTag::kSetPreferredCubeRequest:
    {
      PRINT_NAMED_INFO("AppCubeConnectionSubscriber.HandleAppRequest.SetPreferredCubeRequest",
                       "Received a request from gateway to set our preferred cube.");
      const auto& factoryId = event.GetData().set_preferred_cube_request().factory_id();
      _robot->GetCubeCommsComponent().SetPreferredCube(factoryId);
      break;
    }
    default:
      DEV_ASSERT(false, "AppCubeConnectionSubscriber.HandleAppRequest.UnhandledTag");
      break;
  }
}


}
}


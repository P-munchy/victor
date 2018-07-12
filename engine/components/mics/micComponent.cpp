/***********************************************************************************************************************
 *
 *  MicComponent
 *  Victor / Engine
 *
 *  Created by Jarrod Hatfield on 4/4/2018
 *
 *  Description
 *  + Component to access mic data and to interface with playback and recording
 *
 **********************************************************************************************************************/

#include "engine/components/mics/micComponent.h"
#include "engine/components/mics/micDirectionHistory.h"
#include "engine/components/mics/voiceMessageSystem.h"
#include "engine/cozmoContext.h"
#include "engine/robot.h"

#include "clad/robotInterface/messageEngineToRobot.h"

#include "util/fileUtils/fileUtils.h"
#include "util/helpers/templateHelpers.h"
#include "util/logging/logging.h"


namespace Anki {
namespace Cozmo {

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
MicComponent::MicComponent() :
  IDependencyManagedComponent<RobotComponentID>( this, RobotComponentID::MicComponent ),
  _micHistory( new MicDirectionHistory() ),
  _messageSystem( new VoiceMessageSystem() )
{

}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
MicComponent::~MicComponent()
{
  Util::SafeDelete( _messageSystem );
  Util::SafeDelete( _micHistory );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MicComponent::GetInitDependencies( RobotCompIDSet& dependencies ) const
{
  // we could allow our sub-systems to add to this, but it's simple enough at this point
  dependencies.insert( RobotComponentID::CozmoContextWrapper );
  dependencies.insert( RobotComponentID::Vision );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MicComponent::InitDependent( Cozmo::Robot* robot, const RobotCompMap& dependentComps )
{
  _micHistory->Initialize( robot->GetContext()->GetWebService() );
  _messageSystem->Initialize( robot );
  _robot = robot;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MicComponent::StartWakeWordlessStreaming( CloudMic::StreamType streamType )
{
  RobotInterface::StartWakeWordlessStreaming message{ static_cast<uint8_t>(streamType) };
  _robot->SendMessage( RobotInterface::EngineToRobot( std::move(message) ) );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MicComponent::SetShouldStreamAfterWakeWord( bool shouldStream )
{
  RobotInterface::SetShouldStreamAfterWakeWord message{shouldStream};
  _robot->SendMessage(RobotInterface::EngineToRobot( std::move(message)) );
  _streamAfterWakeWord = shouldStream;
}
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MicComponent::SetTriggerWordDetectionEnabled( bool enabled )
{
  RobotInterface::SetTriggerWordDetectionEnabled message{ enabled };
  _robot->SendMessage( RobotInterface::EngineToRobot( std::move(message) ) );
  _triggerDetectionEnabled = enabled;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MicComponent::SetBufferFullness(float val)
{
  if( !Util::InRange( val, 0.0f, 1.0f ) ) {
    PRINT_NAMED_WARNING("MicComponent.SetBufferFullness.InvalidValue", "Fullness value %f invalid, must be [0, 1]",
                        val);
    _fullness = 0.0f;
  }
  else {
    _fullness = val;
  }
}

} // namespace Cozmo
} // namespace Anki

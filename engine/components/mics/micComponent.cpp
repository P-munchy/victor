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
#include "engine/robot.h"

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
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void MicComponent::InitDependent( Cozmo::Robot* robot, const RobotCompMap& dependentComponents )
{
  _messageSystem->Initialize( robot );
}

} // namespace Cozmo
} // namespace Anki

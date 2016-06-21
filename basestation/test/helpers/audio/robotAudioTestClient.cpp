/**
* File: RobotAudioTestClient
*
* Author: damjan stulic
* Created: 6/9/16
*
* Description: This Client handles the Robot’s specific audio needs. It is a sub-class of AudioEngineClient.
*
* Copyright: Anki, inc. 2016
*
*/

#include "helpers/audio/robotAudioTestClient.h"


namespace Anki {
namespace Cozmo {
namespace Audio {

RobotAudioTestClient::RobotAudioTestClient( Robot* robot )
  : RobotAudioClient( robot )
{
}


} // Audio
} // Cozmo
} // Anki

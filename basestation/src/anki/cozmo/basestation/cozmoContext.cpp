#include "anki/cozmo/basestation/cozmoContext.h"
#include "anki/cozmo/basestation/robotDataLoader.h"
#include "anki/cozmo/basestation/robotManager.h"
#include "anki/cozmo/basestation/audio/audioController.h"
#include "anki/cozmo/basestation/audio/audioServer.h"
#include "anki/cozmo/basestation/viz/vizManager.h"
#include "anki/cozmo/basestation/util/transferQueue/transferQueueMgr.h"
#include "anki/cozmo/basestation/utils/cozmoFeatureGate.h"
#include "anki/cozmo/shared/cozmoConfig_common.h"
//#include "anki/cozmo/game/comms/uiMessageHandler.h"
#include "anki/common/basestation/utils/data/dataPlatform.h"
#include "util/random/randomGenerator.h"

namespace Anki {
namespace Cozmo {
  
CozmoContext::CozmoContext(Util::Data::DataPlatform* dataPlatform, IExternalInterface* externalInterface)
  : _externalInterface(externalInterface)
  , _dataPlatform(dataPlatform)
  , _featureGate(new CozmoFeatureGate())
  , _random(new Anki::Util::RandomGenerator())
  , _dataLoader(new RobotDataLoader(this))
  , _robotMgr(new RobotManager(this))
  , _vizManager(new VizManager())
  , _transferQueueMgr(new Anki::Util::TransferQueueMgr())
{
  // Only set up the audio server if we have a real dataPlatform
  if (nullptr != dataPlatform)
  {
    _audioServer.reset(new Audio::AudioServer(new Audio::AudioController(dataPlatform)));
  }
}


CozmoContext::CozmoContext() : CozmoContext(nullptr, nullptr)
{

}

CozmoContext::~CozmoContext()
{

}
  
} // namespace Cozmo
} // namespace Anki

/**
 * File: cozmoAPI.h
 *
 * Author: Lee Crippen
 * Created: 08/19/15
 *
 * Description: Point of entry for anything needing to interact with Cozmo.
 *
 * Copyright: Anki, Inc. 2015
 *
 * COZMO_PUBLIC_HEADER
 **/

#ifndef __Anki_Cozmo_CozmoAPI_h__
#define __Anki_Cozmo_CozmoAPI_h__
#include "anki/common/types.h"
#include "util/export/export.h"
#include "util/helpers/noncopyable.h"
#include "json/json.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace Anki {
  
// Forward declarations
namespace Util {
namespace Data {
  class DataPlatform;
}
}
  
namespace Cozmo {

class CozmoEngine;
class GameMessagePort;

class CozmoAPI : private Util::noncopyable
{
public:
  // When the engine should run in a separate thread
  ANKI_VISIBLE bool StartRun(Util::Data::DataPlatform* dataPlatform, const Json::Value& config);
  
  // When manual control over updating the engine is desired:
  ANKI_VISIBLE bool Start(Util::Data::DataPlatform* dataPlatform, const Json::Value& config);
  ANKI_VISIBLE bool Update(const BaseStationTime_t currentTime_nanosec);

  // Send messages to game, receive messages from game
  ANKI_VISIBLE size_t SendMessages(uint8_t* buffer, size_t bufferSize);
  ANKI_VISIBLE void ReceiveMessages(const uint8_t* buffer, size_t size);
  ANKI_VISIBLE void ExecuteBackgroundTransfers();

  // Activate A/B experiment
  ANKI_VISIBLE uint32_t ActivateExperiment(const uint8_t* requestBuffer, size_t requestLen,
                                           uint8_t* responseBuffer, size_t responseLen);
  
  // Destroys any running thread and game instance
  ANKI_VISIBLE void Clear();
  
  ANKI_VISIBLE ~CozmoAPI();
  
private:
  class CozmoInstanceRunner
  {
  public:
    CozmoInstanceRunner(Util::Data::DataPlatform* dataPlatform,
                        const Json::Value& config, bool& initResult);
    
    virtual ~CozmoInstanceRunner();
    void Run();
    void Stop() { _isRunning.store(false); }
    
    // For manually ticking the game
    bool Update(const BaseStationTime_t currentTime_nanosec);
    GameMessagePort* GetGameMessagePort() const { return _gameMessagePort.get(); }
    CozmoEngine* GetEngine() const { return _cozmoInstance.get(); }
    void SyncWithEngineUpdate(const std::function<void()>& func) const;
    
  private:
    std::unique_ptr<GameMessagePort> _gameMessagePort;
    std::unique_ptr<CozmoEngine> _cozmoInstance;
    std::atomic<bool> _isRunning;
    mutable std::mutex _updateMutex;

  }; // class CozmoInstanceRunner
  
  // Our running instance, if we have one
  std::unique_ptr<CozmoInstanceRunner> _cozmoRunner;
  std::thread _cozmoRunnerThread;
}; // class CozmoAPI
  
} // namespace Cozmo
} // namespace Anki

#endif // __Anki_Cozmo_CozmoAPI_h__

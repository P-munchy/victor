/**
 * File: behaviorComponentCloudServer
 *
 * Author: baustin
 * Created: 10/31/17
 *
 * Description: Provides a server endpoint for cloud process to connect to
 *              and send messages
 *
 * Copyright: Anki, Inc. 2017
 *
 **/

#include "engine/aiComponent/behaviorComponent/behaviorComponentCloudServer.h"

#include "anki/cozmo/shared/cozmoConfig.h"
#include "coretech/messaging/shared/socketConstants.h"
#include "util/threading/threadPriority.h"

namespace Anki {
namespace Cozmo {

BehaviorComponentCloudServer::BehaviorComponentCloudServer(CallbackFunc callback, const std::string& name, const int sleepMs)
: _callback(std::move(callback))
, _shutdown(false)
, _sleepMs(sleepMs)
{
  _listenThread = std::thread([this, name] { RunThread(std::move(name)); });
}

BehaviorComponentCloudServer::~BehaviorComponentCloudServer()
{
  _shutdown = true;
  _listenThread.join();
}

void BehaviorComponentCloudServer::RunThread(std::string sockName)
{
  Anki::Util::SetThreadName(pthread_self(), "BehaviorServer");
  // Start UDP server
  _server.StartListening(Victor::AI_SERVER_BASE_PATH + sockName);
  char buf[512];
  while (!_shutdown) {
    const ssize_t received = _server.Recv(buf, sizeof(buf));
    if (received > 0) {
      std::string msg{buf, buf+received};
      _callback(std::move(msg));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(_sleepMs));
  }
}

}
}

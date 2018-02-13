/**
 * File: webService.h
 *
 * Author: richard; adapted for Victor by Paul Terry 01/08/2018
 * Created: 4/17/2017
 *
 * Description: Provides interface to civetweb, an embedded web server
 *
 *
 * Copyright: Anki, Inc. 2017-2018
 *
 **/
#ifndef WEB_SERVICE_H
#define WEB_SERVICE_H

#include "util/export/export.h"

#include "json/json.h"

#include <string>
#include <vector>
#include <mutex>
#include <unordered_set>
#include <unordered_map>

#include "util/signals/simpleSignal.hpp"

struct mg_context; // copied from civetweb.h
struct mg_connection;

namespace Json {
  class Value;
}

namespace Anki {

namespace Util {
namespace Data {
  class DataPlatform;
} // namespace Data
} // namespace Util

namespace Cozmo {

namespace WebService {

class WebService
{
public:
  
  WebService();
  ~WebService();

  void Start(Anki::Util::Data::DataPlatform* platform, const Json::Value& config);
  void Update();
  void Stop();
  
  // send data to any client subscribed to moduleName
  void SendToWebSockets(const std::string& moduleName, const Json::Value& data) const;
  
  inline void SendToWebViz(const std::string& moduleName, const Json::Value& data) const { SendToWebSockets(moduleName, data); }
  
  // subscribe to when a client connects and notifies the webservice that they want data for moduleName
  using SendToClientFunc = std::function<void(const Json::Value&)>;
  using OnWebVizSubscribedType = Signal::Signal<void(const SendToClientFunc&)>;
  OnWebVizSubscribedType& OnWebVizSubscribed(const std::string& moduleName) { return _webVizSubscribedSignals[moduleName]; }
  
  // subscribe to when a client (who is listening to moduleName) sends data back to the webservice
  using OnWebVizDataType = Signal::Signal<void(const Json::Value&,const SendToClientFunc&)>;
  OnWebVizDataType& OnWebVizData(const std::string& moduleName) { return _webVizDataSignals[moduleName]; }
  
  const std::string& getConsoleVarsTemplate();

  enum RequestType
  {
    RT_ConsoleVarsUI,
    RT_ConsoleVarGet,
    RT_ConsoleVarSet,
    RT_ConsoleVarList,
    RT_ConsoleFuncCall,
    
    RT_WebsocketOnSubscribe,
    RT_WebsocketOnData,
  };

  struct Request
  {
    Request(RequestType rt, const std::string& param1, const std::string& param2);
    Request(RequestType rt, const std::string& param1, const std::string& param2, const std::string& param3);
    RequestType _requestType;
    std::string _param1;
    std::string _param2;
    std::string _param3;
    std::string _result;
    bool        _resultReady; // Result is ready for use by the webservice thread
    bool        _done;        // Result has been used and now it's OK for main thread to delete this item
  };

  void AddRequest(Request* requestPtr);
  std::mutex _requestMutex;

  const Json::Value& GetConfig() { return _config; }

private:

  void GenerateConsoleVarsUI(std::string& page, const std::string& category);

  struct WebSocketConnectionData {
    struct mg_connection* conn = nullptr;
    std::unordered_set<std::string> subscribedModules;
  };
  
  // called by civetweb
  static void HandleWebSocketsReady(struct mg_connection* conn, void* cbparams);
  static int HandleWebSocketsConnect(const struct mg_connection* conn, void* cbparams);
  static int HandleWebSocketsData(struct mg_connection* conn, int bits, char* data, size_t dataLen, void* cbparams);
  static void HandleWebSocketsClose(const struct mg_connection* conn, void* cbparams);
  
  // called by the above handlers
  void OnOpenWebSocket(struct mg_connection* conn);
  void OnReceiveWebSocket(struct mg_connection* conn, const Json::Value& data);
  void OnCloseWebSocket(const struct mg_connection* conn);
  
  static void SendToWebSocket(struct mg_connection* conn, const Json::Value& data);
  
  // todo: OTA update somehow?

  struct mg_context* _ctx;
  
  std::vector<WebSocketConnectionData> _webSocketConnections;

  std::string _consoleVarsUIHTMLTemplate;

  std::vector<Request*> _requests;

  Json::Value _config;
 
  std::unordered_map<std::string, OnWebVizSubscribedType> _webVizSubscribedSignals;
  std::unordered_map<std::string, OnWebVizDataType> _webVizDataSignals;
};

} // namespace WebService
  
} // namespace Cozmo
} // namespace Anki

#endif // defined(WEB_SERVICE_H)

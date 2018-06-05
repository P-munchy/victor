/*
 * File:          webotsCtrlGameEngine2.cpp
 * Date:
 * Description:   Cozmo 2.0 engine for Webots simulation
 * Author:
 * Modifications:
 */

#include "../shared/ctrlCommonInitialization.h"
#include "anki/cozmo/shared/cozmoEngineConfig.h"
#include "anki/cozmo/shared/cozmoConfig.h"
#include "coretech/common/engine/utils/data/dataPlatform.h"
#include "coretech/common/engine/jsonTools.h"

#include "camera/cameraService.h"
#include "cubeBleClient/cubeBleClient.h"
#include "osState/osState.h"

#include "json/json.h"

#include "engine/cozmoAPI/cozmoAPI.h"
#include "engine/utils/parsingConstants/parsingConstants.h"

#include "util/console/consoleInterface.h"
#include "util/console/consoleSystem.h"
#include "util/logging/channelFilter.h"
#include "util/logging/logging.h"
#include "util/logging/printfLoggerProvider.h"
#include "util/logging/multiFormattedLoggerProvider.h"
#include "util/global/globalDefinitions.h"

#if ANKI_DEV_CHEATS
#include "engine/debug/cladLoggerProvider.h"
#include "engine/debug/devLoggerProvider.h"
#include "engine/debug/devLoggingSystem.h"
#include "util/fileUtils/fileUtils.h"
#include "util/logging/rollingFileLogger.h"
#endif

#include <fstream>
#include <webots/Supervisor.hpp>

#define ROBOT_ADVERTISING_HOST_IP "127.0.0.1"
#define SDK_ADVERTISING_HOST_IP   "127.0.0.1"

namespace Anki {
  namespace Cozmo {
    CONSOLE_VAR_EXTERN(bool, kEnableCladLogger);
  }
}

using namespace Anki;
using namespace Anki::Cozmo;


int main(int argc, char **argv)
{
  // parse commands
  WebotsCtrlShared::ParsedCommandLine params = WebotsCtrlShared::ParseCommandLine(argc, argv);

  // create platform.
  // Unfortunately, CozmoAPI does not properly receive a const DataPlatform, and that change
  // is too big of a change, since it involves changing down to the context, so create a non-const platform
  //const Anki::Util::Data::DataPlatform& dataPlatform = WebotsCtrlShared::CreateDataPlatformBS(argv[0]);
  Anki::Util::Data::DataPlatform dataPlatform = WebotsCtrlShared::CreateDataPlatformBS(argv[0], "webotsCtrlGameEngine2");

  // Instantiate supervisor and pass to AndroidHAL and cubeBleClient
  webots::Supervisor engineSupervisor;
  CameraService::SetSupervisor(&engineSupervisor);
  OSState::SetSupervisor(&engineSupervisor);
  CubeBleClient::SetSupervisor(&engineSupervisor);

  // Get robotID to determine if devlogger should be created
  // Only create devLogs for robot with DEFAULT_ROBOT_ID.
  // The only time it shouldn't create a log is for sim robots
  // with non-DEFAULT_ROBOT_ID so as to avoid multiple devLogs
  // recording to the same folder.
  RobotID_t robotID = OSState::getInstance()->GetRobotID();
  const bool createDevLoggers = robotID == DEFAULT_ROBOT_ID;

#if ANKI_DEV_CHEATS
  if (createDevLoggers) {
    DevLoggingSystem::CreateInstance(dataPlatform.pathToResource(Util::Data::Scope::CurrentGameLog, "devLogger"), "mac");
  } else {
    PRINT_NAMED_WARNING("webotsCtrlGameEngine.main.SkippingDevLogger",
                        "RobotID: %d - Only DEFAULT_ROBOT_ID may create loggers", robotID);
  }
#endif

  // - create and set logger
  Util::IFormattedLoggerProvider* printfLoggerProvider = new Util::PrintfLoggerProvider(Anki::Util::LOG_LEVEL_WARN,
                                                                                        params.colorizeStderrOutput);
  std::vector<Util::IFormattedLoggerProvider*> loggerVec;
  loggerVec.push_back(printfLoggerProvider);

#if ANKI_DEV_CHEATS
  if (createDevLoggers) {
    loggerVec.push_back(new DevLoggerProvider(DevLoggingSystem::GetInstance()->GetQueue(),
            Util::FileUtils::FullFilePath( {DevLoggingSystem::GetInstance()->GetDevLoggingBaseDirectory(), DevLoggingSystem::kPrintName} )));
  }
#endif

  Anki::Util::MultiFormattedLoggerProvider loggerProvider(loggerVec);

  loggerProvider.SetMinLogLevel(Anki::Util::LOG_LEVEL_DEBUG);
  Anki::Util::gLoggerProvider = &loggerProvider;
  Anki::Util::sSetGlobal(DPHYS, "0xdeadffff00000001");

  // - console filter for logs
  if ( params.filterLog )
  {
    using namespace Anki::Util;
    ChannelFilter* consoleFilter = new ChannelFilter();

    // load file config
    Json::Value consoleFilterConfig;
    const std::string& consoleFilterConfigPath = "config/engine/console_filter_config.json";
    if (!dataPlatform.readAsJson(Util::Data::Scope::Resources, consoleFilterConfigPath, consoleFilterConfig))
    {
      PRINT_NAMED_ERROR("webotsCtrlGameEngine.main.loadConsoleConfig", "Failed to parse Json file '%s'", consoleFilterConfigPath.c_str());
    }

    // initialize console filter for this platform
    const std::string& platformOS = dataPlatform.GetOSPlatformString();
    const Json::Value& consoleFilterConfigOnPlatform = consoleFilterConfig[platformOS];
    consoleFilter->Initialize(consoleFilterConfigOnPlatform);

    // set filter in the loggers
    std::shared_ptr<const IChannelFilter> filterPtr( consoleFilter );
    printfLoggerProvider->SetFilter(filterPtr);

    // also parse additional info for providers
    printfLoggerProvider->ParseLogLevelSettings(consoleFilterConfigOnPlatform);

    #if ANKI_DEV_CHEATS
    {
      // Disable the Clad logger by default - prevents it sending the log messages,
      // otherwise anyone with a config set to spam every message could run into issues
      // with the amount of messages overwhelming the socket on engine startup/load.
      // Anyone who needs the log messages can enable it afterwards via Unity, SDK or Webots
      Anki::Cozmo::kEnableCladLogger = false;
    }
    #endif // ANKI_DEV_CHEATS
  }
  else
  {
    PRINT_NAMED_INFO("webotsCtrlGameEngine.main.noFilter", "Console will not be filtered due to program args");
  }

  // Start with a step so that we can attach to the process here for debugging
  engineSupervisor.step(BS_TIME_STEP_MS);

  // Get configuration JSON
  Json::Value config;

  if (!dataPlatform.readAsJson(Util::Data::Scope::Resources,
                               "config/engine/configuration.json", config)) {
    PRINT_NAMED_ERROR("webotsCtrlGameEngine.main.loadConfig", "Failed to parse Json file config/engine/configuration.json");
  }

  if(!config.isMember(AnkiUtil::kP_ADVERTISING_HOST_IP)) {
    config[AnkiUtil::kP_ADVERTISING_HOST_IP] = ROBOT_ADVERTISING_HOST_IP;
  }

  if(!config.isMember(AnkiUtil::kP_UI_ADVERTISING_PORT)) {
    config[AnkiUtil::kP_UI_ADVERTISING_PORT] = UI_ADVERTISING_PORT;
  }

  if(!config.isMember(AnkiUtil::kP_SDK_ADVERTISING_HOST_IP)) {
    config[AnkiUtil::kP_SDK_ADVERTISING_HOST_IP] = SDK_ADVERTISING_HOST_IP;
  }

  if(!config.isMember(AnkiUtil::kP_SDK_ADVERTISING_PORT)) {
    config[AnkiUtil::kP_SDK_ADVERTISING_PORT] = SDK_ADVERTISING_PORT;
  }

  if(!config.isMember(AnkiUtil::kP_SDK_ON_DEVICE_TCP_PORT)) {
    config[AnkiUtil::kP_SDK_ON_DEVICE_TCP_PORT] = SDK_ON_DEVICE_TCP_PORT;
  }

  int numUIDevicesToWaitFor = 1;
  webots::Field* numUIsField = engineSupervisor.getSelf()->getField("numUIDevicesToWaitFor");
  if (numUIsField) {
    numUIDevicesToWaitFor = numUIsField->getSFInt32();
  } else {
    PRINT_NAMED_WARNING("webotsCtrlGameEngine.main.MissingField", "numUIDevicesToWaitFor not found in BlockworldComms");
  }

  config[AnkiUtil::kP_NUM_ROBOTS_TO_WAIT_FOR] = 0;
  config[AnkiUtil::kP_NUM_UI_DEVICES_TO_WAIT_FOR] = 1;
  config[AnkiUtil::kP_NUM_SDK_DEVICES_TO_WAIT_FOR] = 1;

  // Set up the console vars to load from file, if it exists
  ANKI_CONSOLE_SYSTEM_INIT("consoleVarsEngine.ini");

  // Initialize the API
  CozmoAPI myCozmo;
  myCozmo.Start(&dataPlatform, config);

  PRINT_NAMED_INFO("webotsCtrlGameEngine.main", "CozmoGame created and initialized.");

  //
  // Main Execution loop: step the world forward forever
  //
  while (engineSupervisor.step(BS_TIME_STEP_MS) != -1)
  {
    double currTimeNanoseconds = Util::SecToNanoSec(engineSupervisor.getTime());
    myCozmo.Update(Util::numeric_cast<BaseStationTime_t>(currTimeNanoseconds));
  } // while still stepping
#if ANKI_DEV_CHEATS
  DevLoggingSystem::DestroyInstance();
#endif

  Anki::Util::gLoggerProvider = nullptr;
  return 0;
}

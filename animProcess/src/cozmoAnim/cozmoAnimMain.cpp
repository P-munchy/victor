/**
* File: cozmoAnimMain.cpp
*
* Author: Kevin Yoon
* Created: 6/26/17
*
* Description: Cozmo Anim Process on Android
*
* Copyright: Anki, inc. 2017
*
*/
#include "cozmoAnim/animEngine.h"

#include "coretech/common/engine/jsonTools.h"
#include "coretech/common/engine/utils/data/dataPlatform.h"

#include "anki/cozmo/shared/cozmoConfig.h"

#include "util/logging/logging.h"
#include "util/logging/androidLogPrintLogger_android.h"
#include "util/fileUtils/fileUtils.h"


#include <stdio.h>
#include <chrono>
#include <fstream>
#include <thread>

using namespace Anki;
using namespace Anki::Cozmo;

namespace {
AnimEngine* animEngine = nullptr;
}

void Cleanup(int signum)
{
  if (animEngine != nullptr)
  {
    delete animEngine;
    animEngine = nullptr;
  }
  
  exit(signum);
}

Anki::Util::Data::DataPlatform* createPlatform(const std::string& filesPath,
                                         const std::string& cachePath,
                                         const std::string& externalPath,
                                         const std::string& resourcesPath)
{
    Anki::Util::FileUtils::CreateDirectory(filesPath);
    Anki::Util::FileUtils::CreateDirectory(cachePath);
    Anki::Util::FileUtils::CreateDirectory(externalPath);
    Anki::Util::FileUtils::CreateDirectory(resourcesPath);

    return new Anki::Util::Data::DataPlatform(filesPath, cachePath, externalPath, resourcesPath);
}

std::string createResourcesPath(const std::string& resourcesBasePath)
{
  std::string resourcesRefPath = resourcesBasePath + "/current";
  std::string resourcesRef = Anki::Util::FileUtils::ReadFile(resourcesRefPath);
  {
    auto it = std::find_if(resourcesRef.rbegin(), resourcesRef.rend(),
          [](char ch){ return !std::iswspace(ch); });
    resourcesRef.erase(it.base() , resourcesRef.end());
  }
  return resourcesBasePath + "/" + resourcesRef + "/cozmo_resources";
}

void getAndroidPlatformPaths(std::string& filesPath,
                             std::string& cachePath,
                             std::string& externalPath,
                             std::string& resourcesPath,
                             std::string& resourcesBasePath)
{
  filesPath = "/data/data/com.anki.cozmoengine/files";
  cachePath = "/data/data/com.anki.cozmoengine/cache";
  externalPath = "/sdcard/Android/data/com.anki.cozmoengine/files";
  resourcesBasePath = externalPath + "/assets";
  resourcesPath = createResourcesPath(resourcesBasePath);
}

Anki::Util::Data::DataPlatform* createPlatform()
{
  char config_file_path[PATH_MAX] = { 0 };
  const char* env_config = getenv("VIC_ANIM_CONFIG");
  if (env_config != NULL) {
    strncpy(config_file_path, env_config, sizeof(config_file_path));
  }

  Json::Value config;

  printf("config_file: %s\n", config_file_path);
  if (strlen(config_file_path)) {
    std::string config_file{config_file_path};
    if (!Anki::Util::FileUtils::FileExists(config_file)) {
      fprintf(stderr, "config file not found: %s\n", config_file_path);
    }

    std::string jsonContents = Anki::Util::FileUtils::ReadFile(config_file);
    printf("jsonContents: %s\n", jsonContents.c_str());
    Json::Reader reader;
    if (!reader.parse(jsonContents, config)) {
      PRINT_STREAM_ERROR("cozmo_startup",
        "json configuration parsing error: " << reader.getFormattedErrorMessages());
    }
  }

  std::string filesPath;
  std::string cachePath;
  std::string externalPath;
  std::string resourcesPath;
  std::string resourcesBasePath;
  
  getAndroidPlatformPaths(filesPath, cachePath, externalPath, resourcesPath, resourcesBasePath);


  if (config.isMember("DataPlatformFilesPath")) {
    filesPath = config["DataPlatformFilesPath"].asCString();
  } else {
    config["DataPlatformFilesPath"] = filesPath;
  }

  if (config.isMember("DataPlatformCachePath")) {
    cachePath = config["DataPlatformCachePath"].asCString();
  } else {
    config["DataPlatformCachePath"] = cachePath;
  }

  if (config.isMember("DataPlatformExternalPath")) {
    externalPath = config["DataPlatformExternalPath"].asCString();
  } else {
    config["DataPlatformExternalPath"] = externalPath;
  }

  if (config.isMember("DataPlatformResourcesBasePath")) {
    resourcesBasePath = config["DataPlatformResourcesBasePath"].asCString();
  } else {
    resourcesBasePath = externalPath;
    config["DataPlatformResourcesBasePath"] = resourcesBasePath;
  }

  if (config.isMember("DataPlatformResourcesPath")) {
    resourcesPath = config["DataPlatformResourcesPath"].asCString();
  } else {
    resourcesPath = createResourcesPath(resourcesBasePath);
    config["DataPlatformResourcesPath"] = resourcesPath;
  }

  Util::Data::DataPlatform* dataPlatform =
    createPlatform(filesPath, cachePath, externalPath, resourcesPath);

  return dataPlatform;
}


int main(void)
{
  signal(SIGTERM, Cleanup);
  
  // - create and set logger
  Util::AndroidLogPrintLogger logPrintLogger("anim");
  Util::gLoggerProvider = &logPrintLogger;

  Util::Data::DataPlatform* dataPlatform = createPlatform();
    
  // Create and init AnimEngine
  animEngine = new AnimEngine(dataPlatform);
  
  animEngine->Init();
  
  using namespace std::chrono;
  using TimeClock = steady_clock;

  const auto runStart = TimeClock::now();
  
  // Set the target time for the end of the first frame
  auto targetEndFrameTime = runStart + (microseconds)(ANIM_TIME_STEP_US);
  

  while (1) {

    const auto tickStart = TimeClock::now();
    const duration<double> curTime_s = tickStart - runStart;
    const BaseStationTime_t curTime_ns = Util::numeric_cast<BaseStationTime_t>(Util::SecToNanoSec(curTime_s.count()));

    if (animEngine->Update(curTime_ns) != RESULT_OK) {
      PRINT_NAMED_WARNING("CozmoAnimMain.Update.Failed", "Exiting...");
      break;
    }
    
    const auto tickNow = TimeClock::now();
    const auto remaining_us = duration_cast<microseconds>(targetEndFrameTime - tickNow);

    // Complain if we're going overtime
    if (remaining_us < microseconds(-ANIM_OVERTIME_WARNING_THRESH_US))
    {
      //const auto tickDuration_us = duration_cast<microseconds>(tickNow - tickStart);
      //PRINT_NAMED_INFO("CozmoAPI.CozmoInstanceRunner", "targetEndFrameTime:%8lld, tickDuration_us:%8lld, remaining_us:%8lld",
      //                 TimeClock::time_point(targetEndFrameTime).time_since_epoch().count(), tickDuration_us.count(), remaining_us.count());

      PRINT_NAMED_WARNING("CozmoAnimMain.overtime", "Update() (%dms max) is behind by %.3fms",
                          ANIM_TIME_STEP_MS, (float)(-remaining_us).count() * 0.001f);
    }
    
    // Now we ALWAYS sleep, but if we're overtime, we 'sleep zero' which still
    // allows other threads to run
    static const auto minimumSleepTime_us = microseconds((long)0);
    std::this_thread::sleep_for(std::max(minimumSleepTime_us, remaining_us));

    // Set the target end time for the next frame
    targetEndFrameTime += (microseconds)(ANIM_TIME_STEP_US);
    
    // See if we've fallen very far behind (this happens e.g. after a 5-second blocking
    // load operation); if so, compensate by catching the target frame end time up somewhat.
    // This is so that we don't spend the next SEVERAL frames catching up.
    const auto timeBehind_us = -remaining_us;
    static const auto kusPerFrame = ((microseconds)(ANIM_TIME_STEP_US)).count();
    static const int kTooFarBehindFramesThreshold = 2;
    static const auto kTooFarBehindThreshold = (microseconds)(kTooFarBehindFramesThreshold * kusPerFrame);
    if (timeBehind_us >= kTooFarBehindThreshold)
    {
      const int framesBehind = (int)(timeBehind_us.count() / kusPerFrame);
      const auto forwardJumpDuration = kusPerFrame * framesBehind;
      targetEndFrameTime += (microseconds)forwardJumpDuration;
      PRINT_NAMED_WARNING("CozmoAnimMain.catchup",
                          "Update was too far behind so moving target end frame time forward by an additional %.3fms",
                          (float)(forwardJumpDuration * 0.001f));
    }

  }
}

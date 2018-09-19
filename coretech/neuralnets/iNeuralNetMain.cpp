/**
 * File: iNeuralNetMain.cpp
 *
 * Author: Andrew Stein
 * Date:   9/14/2018
 *
 * Description: Interface class to create a standalone process to run forward inference using a neural network.
 *
 *              Currently uses the file system as a poor man's IPC to communicate with the "messenger"
 *              NeuralNetRunner::Model implementation in vic-engine's Vision System. See also VIC-2686.
 *
 * Copyright: Anki, Inc. 2018
 **/


#if defined(ANKI_NEURALNETS_USE_TENSORFLOW)
#  include "coretech/neuralnets/neuralNetModel_tensorflow.h"
#elif defined(ANKI_NEURALNETS_USE_CAFFE2)
#  include "coretech/neuralnets/objectDetector_caffe2.h"
#elif defined(ANKI_NEURALNETS_USE_OPENCV_DNN)
#  include "coretech/neuralnets/objectDetector_opencvdnn.h"
#elif defined(ANKI_NEURALNETS_USE_TFLITE)
#  include "coretech/neuralnets/neuralNetModel_tflite.h"
#else
#  error One of ANKI_NEURALNETS_USE_{TENSORFLOW | CAFFE2 | OPENCVDNN | TFLITE} must be defined
#endif

#include "clad/types/salientPointTypes.h"
#include "coretech/common/engine/scopedTicToc.h"
#include "coretech/neuralnets/iNeuralNetMain.h"
#include "coretech/vision/engine/image_impl.h"
#include "json/json.h"
#include "util/fileUtils/fileUtils.h"
#include "util/logging/logging.h"

#include "opencv2/imgcodecs/imgcodecs.hpp"
#include "opencv2/imgproc/imgproc.hpp"

#include <fstream>
#include <iostream>
#include <thread>
#include <unistd.h>

#define LOG_CHANNEL "NeuralNets"

namespace Anki {
namespace NeuralNets {
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Must be implemented here (in .cpp) due to use of unique_ptr with a forward declaration
INeuralNetMain::INeuralNetMain() = default;
INeuralNetMain::~INeuralNetMain() = default;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void INeuralNetMain::CleanupAndExit(Result result)
{
  LOG_INFO("INeuralNetMain.CleanupAndExit", "result:%d", result);
  Util::gLoggerProvider = nullptr;
  
  DerivedCleanup();
  
  sync();
  
  exit(result);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Result INeuralNetMain::Init(const std::string& configFilename,
                            const std::string& modelPath,
                            const std::string& cachePath,
                            const std::string& imageFileToProcess)
{
  _isInitialized = false;
  
  Util::gLoggerProvider = GetLoggerProvider();
  
  if(nullptr == Util::gLoggerProvider)
  {
    // Having no logger shouldn't kill us but probably isn't what we intended, so issue a warning
    LOG_WARNING("INeuralNetMain.Init.NullLogger", "");
  }
  
  Result result = RESULT_OK;
  
  // Make sure the config file and model path are valid
  // NOTE: cachePath need not exist yet as it will be created by the NeuralNetRunner
  if(!Util::FileUtils::FileExists(configFilename))
  {
    LOG_ERROR("INeuralNetMain.Init.BadConfigFile", "ConfigFile:%s", configFilename.c_str());
    result = RESULT_FAIL;
  }
  if(!Util::FileUtils::DirectoryExists(modelPath))
  {
    LOG_ERROR("INeuralNetMain.Init.BadModelPath", "ModelPath:%s", modelPath.c_str());
    result = RESULT_FAIL;
  }
  if(RESULT_OK != result)
  {
    CleanupAndExit(result);
  }
  LOG_INFO("INeuralNetMain.Init.Starting", "Config:%s ModelPath:%s CachePath:%s",
           configFilename.c_str(), modelPath.c_str(), cachePath.c_str());
  
  // Read config file
  Json::Value config;
  {
    Json::Reader reader;
    std::ifstream file(configFilename);
    const bool success = reader.parse(file, config);
    if(!success)
    {
      LOG_ERROR("INeuralNetMain.Init.ReadConfigFailed",
                        "Could not read config file: %s", configFilename.c_str());
      CleanupAndExit(RESULT_FAIL);
    }
    
    const char *  const kNeuralNetsKey = "NeuralNets";
    if(!config.isMember(kNeuralNetsKey))
    {
      LOG_ERROR("INeuralNetMain.Init.MissingConfigKey", "Config file missing '%s' field", kNeuralNetsKey);
      CleanupAndExit(RESULT_FAIL);
    }
    
    config = config[kNeuralNetsKey];
    
    if(!config.isMember("pollPeriod_ms"))
    {
      LOG_ERROR("INeuralNetMain.Init.MissingPollPeriodField", "No 'pollPeriod_ms' specified in config file");
      CleanupAndExit(RESULT_FAIL);
    }
  }
  
  _pollPeriod_ms = GetPollPeriod_ms(config);
  
  _imageFileprovided = !imageFileToProcess.empty();
  
  _imageFilename = (_imageFileprovided ?
                    imageFileToProcess :
                    Util::FileUtils::FullFilePath({cachePath, "neuralNetImage.png"}));
  
  _timestampFilename = Util::FileUtils::FullFilePath({cachePath, "timestamp.txt"});
  
  _jsonFilename = Util::FileUtils::FullFilePath({cachePath, "neuralNetResults.json"});
  
  // Initialize the detector
  _neuralNet.reset( new NeuralNets::NeuralNetModel(cachePath) );
  {
    ScopedTicToc ticToc("LoadModel", LOG_CHANNEL);
    result = _neuralNet->LoadModel(modelPath, config);
    
    if(RESULT_OK != result)
    {
      LOG_ERROR("INeuralNetMain.Init.LoadModelFail", "Failed to load model from path: %s", modelPath.c_str());
      CleanupAndExit(result);
    }
    
    ScopedTicToc::Enable(_neuralNet->IsVerbose());
  }
  
  LOG_INFO("INeuralNetMain.Init.ImageLoadMode", "%s: %s",
           (_imageFileprovided ? "Loading given image" : "Polling for images at"),
           _imageFilename.c_str());
  
  LOG_INFO("INeuralNetMain.Init.DetectorInitialized", "Waiting for images");
  
  _isInitialized = true;
  
  return RESULT_OK;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Result INeuralNetMain::Run()
{
  if(!_isInitialized)
  {
    LOG_ERROR("INeuralNetMain.Run.NotInitialized", "");
    return RESULT_FAIL;
  }
  
  Result result = RESULT_OK;
  
  while(!ShouldShutdown())
  {
    // Is there an image file available in the cache?
    const bool isImageAvailable = Util::FileUtils::FileExists(_imageFilename);
    
    if(isImageAvailable)
    {
      if(_neuralNet->IsVerbose())
      {
        LOG_INFO("INeuralNetMain.Run.FoundImage", "%s", _imageFilename.c_str());
      }
      
      // Get the image
      Vision::ImageRGB img;
      {
        ScopedTicToc ticToc("GetImage", LOG_CHANNEL);
        GetImage(_imageFilename, _timestampFilename, img);
        
        if(img.IsEmpty())
        {
          LOG_ERROR("INeuralNetMain.Run.ImageReadFailed", "Error while loading image %s", _imageFilename.c_str());
          if(_imageFileprovided)
          {
            // If we loaded in image file specified on the command line, we are done
            result = RESULT_FAIL;
            break;
          }
          else
          {
            // Remove the image file we were working with to signal that we're done with it
            // and ready for a new image, even if this one was corrupted
            if(_neuralNet->IsVerbose())
            {
              LOG_INFO("INeuralNetMain.Run.DeletingImageFile", "%s", _imageFilename.c_str());
            }
            remove(_imageFilename.c_str());
            continue; // no need to stop the process, it was just a bad image, won't happen again
          }
        }
      }
      
      // Detect what's in it
      std::list<Vision::SalientPoint> salientPoints;
      {
        ScopedTicToc ticToc("Detect", LOG_CHANNEL);
        result = _neuralNet->Detect(img, salientPoints);
      
        if(RESULT_OK != result)
        {
          LOG_ERROR("INeuralNetMain.Run.DetectFailed", "");
        }
      }
      
      // Convert the results to JSON
      Json::Value detectionResults;
      ConvertSalientPointsToJson(salientPoints, _neuralNet->IsVerbose(), detectionResults);
      
      // Write out the Json
      {
        ScopedTicToc ticToc("WriteJSON", LOG_CHANNEL);
        if(_neuralNet->IsVerbose())
        {
          LOG_INFO("INeuralNetMain.Run.WritingResults", "%s", _jsonFilename.c_str());
        }
        
        const bool success = WriteResults(_jsonFilename, detectionResults);
        if(!success)
        {
          result = RESULT_FAIL;
          break;
        }
      }
      
      if(_imageFileprovided)
      {
        // If we loaded in image file specified on the command line, we are done
        result = RESULT_OK;
        break;
      }
      else
      {
        // Remove the image file we were working with to signal that we're done with it
        // and ready for a new image
        if(_neuralNet->IsVerbose())
        {
          LOG_INFO("INeuralNetMain.Run.DeletingImageFile", "%s", _imageFilename.c_str());
        }
        remove(_imageFilename.c_str());
      }
    }
    else if(_imageFileprovided)
    {
      LOG_ERROR("INeuralNetMain.Run.ImageFileDoesNotExist", "%s", _imageFilename.c_str());
      break;
    }
    else
    {
      if(_neuralNet->IsVerbose())
      {
        const int kVerbosePrintFreq_ms = 1000;
        static int count = 0;
        if(count++ * _pollPeriod_ms >= kVerbosePrintFreq_ms)
        {
          LOG_INFO("INeuralNetMain.Run.WaitingForImage", "%s", _imageFilename.c_str());
          count = 0;
        }
      }
      
      Step(_pollPeriod_ms);
    }
  }
  
  CleanupAndExit(result);
  
  return result;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void INeuralNetMain::GetImage(const std::string& imageFilename, const std::string& timestampFilename, Vision::ImageRGB& img)
{
  using namespace Anki;
  const Result loadResult = img.Load(imageFilename);

  if(RESULT_OK != loadResult)
  {
    PRINT_NAMED_ERROR("INeuralNetMain.GetImage.EmptyImageRead", "%s", imageFilename.c_str());
    return;
  }
  
  if(Util::FileUtils::FileExists(timestampFilename))
  {
    std::ifstream file(timestampFilename);
    std::string line;
    std::getline(file, line);
    const TimeStamp_t timestamp = uint(std::stol(line));
    img.SetTimestamp(timestamp);
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void INeuralNetMain::ConvertSalientPointsToJson(const std::list<Vision::SalientPoint>& salientPoints,
                                                const bool isVerbose,
                                                Json::Value& detectionResults)
{
  std::string salientPointsStr;
  
  Json::Value& salientPointsJson = detectionResults["salientPoints"];
  for(auto const& salientPoint : salientPoints)
  {
    salientPointsStr += salientPoint.description + "[" + std::to_string((int)round(100.f*salientPoint.score)) + "] ";
    const Json::Value json = salientPoint.GetJSON();
    salientPointsJson.append(json);
  }
  
  if(isVerbose && !salientPoints.empty())
  {
    LOG_INFO("INeuralNetMain.ConvertSalientPointsToJson",
             "Detected %zu objects: %s", salientPoints.size(), salientPointsStr.c_str());
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool INeuralNetMain::WriteResults(const std::string jsonFilename, const Json::Value& detectionResults)
{
  // Write to a temporary file, then move into place once the write is complete (poor man's "lock")
  const std::string tempFilename = jsonFilename + ".lock";
  Json::StyledStreamWriter writer;
  std::fstream fs;
  fs.open(tempFilename, std::ios::out);
  if (!fs.is_open()) {
    LOG_ERROR("INeuralNetMain.WriteResults.OutputFileOpenFailed", "%s", jsonFilename.c_str());
    return false;
  }
  writer.write(fs, detectionResults);
  fs.close();
  
  const bool success = (rename(tempFilename.c_str(), jsonFilename.c_str()) == 0);
  if (!success)
  {
    LOG_ERROR("INeuralNetMain.WriteResults.RenameFail",
                      "%s -> %s", tempFilename.c_str(), jsonFilename.c_str());
    return false;
  }
  
  return true;
}
  
} // namespace NeuralNets
} // namespace Anki


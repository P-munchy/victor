/**
 * File: objectDetector.cpp
 *
 * Author: Andrew Stein
 * Date:   6/29/2017
 *
 * Description: 
 *
 * Copyright: Anki, Inc. 2017
 **/

#include "coretech/vision/engine/neuralNetRunner.h"
#include "coretech/vision/engine/image.h"
#include "coretech/vision/engine/imageCache.h"
#include "coretech/vision/engine/profiler.h"

#include "coretech/common/engine/array2d_impl.h"
#include "coretech/common/engine/jsonTools.h"
#include "coretech/common/engine/utils/timer.h"

#include "util/console/consoleInterface.h"
#include "util/fileUtils/fileUtils.h"
#include "util/helpers/quoteMacro.h"

#include <cstdio>
#include <list>
#include <fstream>

// TODO: put this back if/when we start supporting other NeuralNetRunnerModels
//#if USE_TENSORFLOW
//#  ifndef TENSORFLOW_USE_AOT
//#    error Expecting TENSORFLOW_USE_AOT to be defined by cmake!
//#  elif TENSORFLOW_USE_AOT==1
//#    include "objectDetectorModel_tensorflow_AOT.cpp"
//#  else
//#    include "objectDetectorModel_tensorflow.cpp"
//#  endif
//#elif USE_TENSORFLOW_LITE
//#  include "objectDetectorModel_tensorflow_lite.cpp"
//#elif USE_OPENCV_DNN
//#  include "neuralNetRunner_opencvdnnModel.cpp"
//#else
#include "neuralNetRunner_messengerModel.cpp"
//#endif

namespace Anki {
namespace Vision {
 
// Log channel name currently expected to be defined by one of the objectDetectorModel cpp files...
// static const char * const kLogChannelName = "VisionSystem";
 
namespace {
  CONSOLE_VAR(f32,   kObjectDetection_Gamma,  "Vision.NeuralNetRunner", 1.0f); // set to 1.0 to disable
}
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
NeuralNetRunner::NeuralNetRunner()
: _profiler("NeuralNetRunner")
, _model(new Model(_profiler))
{
  
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
NeuralNetRunner::~NeuralNetRunner()
{
  
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Result NeuralNetRunner::Init(const std::string& modelPath, const std::string& cachePath, const Json::Value& config)
{
  Result result = RESULT_OK;
  
  _profiler.Tic("LoadModel");
  result = _model->LoadModel(modelPath, cachePath, config);
  _profiler.Toc("LoadModel");
  
  if(RESULT_OK != result)
  {
    PRINT_NAMED_ERROR("NeuralNetRunner.Init.LoadModelFailed", "");
    return result;
  }
  
  // Get the input height/width so we can do the resize and only need to share/copy/write as
  // small an image as possible for the standalone CNN process to pick up
  if(false == JsonTools::GetValueOptional(config, "inputHeight", _processingHeight))
  {
    PRINT_NAMED_ERROR("NeuralNetRunner.Init.MissingConfig", "inputHeight");
    return RESULT_FAIL;
  }
  
  if(false == JsonTools::GetValueOptional(config, "inputWidth", _processingWidth))
  {
    PRINT_NAMED_ERROR("NeuralNetRunner.Init.MissingConfig", "inputWidth");
    return RESULT_FAIL;
  }

  PRINT_NAMED_INFO("NeuralNetRunner.Init.LoadModelTime", "Loading model from '%s' took %.1fsec",
                   modelPath.c_str(), Util::MilliSecToSec(_profiler.AverageToc("LoadModel")));

  _profiler.SetPrintFrequency(config.get("ProfilingPrintFrequency_ms", 10000).asUInt());
  _profiler.SetDasLogFrequency(config.get("ProfilingEventLogFrequency_ms", 10000).asUInt());

  _isInitialized = true;
  return result;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void NeuralNetRunner::ApplyGamma(ImageRGB& img)
{
  if(Util::IsFltNear(kObjectDetection_Gamma, 1.f))
  {
    return;
  }
  
  auto ticToc = _profiler.TicToc("Gamma");
  
  if(!Util::IsFltNear(kObjectDetection_Gamma, _currentGamma))
  {
    _currentGamma = kObjectDetection_Gamma;
    const f32 gamma = 1.f / _currentGamma;
    const f32 divisor = 1.f / 255.f;
    for(s32 value=0; value<256; ++value)
    {
      _gammaLUT[value] = std::round(255.f * std::powf((f32)value * divisor, gamma));
    }
  }
  
  s32 nrows = img.GetNumRows();
  s32 ncols = img.GetNumCols();
  if(img.IsContinuous()) {
    ncols *= nrows;
    nrows = 1;
  }
  for(s32 i=0; i<nrows; ++i)
  {
    Vision::PixelRGB* img_i = img.GetRow(i);
    for(s32 j=0; j<ncols; ++j)
    {
      Vision::PixelRGB& pixel = img_i[j];
      pixel.r() = _gammaLUT[pixel.r()];
      pixel.g() = _gammaLUT[pixel.g()];
      pixel.b() = _gammaLUT[pixel.b()];
    }
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool NeuralNetRunner::StartProcessingIfIdle(ImageCache& imageCache)
{
  if(!_isInitialized)
  {
    PRINT_NAMED_ERROR("NeuralNetRunner.StartProcessingIfIdle.NotInitialized", "");
    return false;
  }
  
  // If we're not already processing an image with a "future", create one to process this image asynchronously.
  if(!_future.valid())
  {
    // Require color data
    if(!imageCache.HasColor())
    {
      PRINT_PERIODIC_CH_DEBUG(30, kLogChannelName, "NeuralNetRunner.StartProcessingIfIdle.NeedColorData", "");
      return false;
    }
  
    // Resize to processing size
    _imgBeingProcessed.Allocate(_processingHeight, _processingWidth);
    const ImageCache::Size kImageSize = ImageCache::Size::Full;
    const Vision::ResizeMethod kResizeMethod = Vision::ResizeMethod::Linear;
    imageCache.GetRGB(kImageSize).Resize(_imgBeingProcessed, kResizeMethod);
    
    // Apply gamma (no-op if gamma is set to 1.0)
    ApplyGamma(_imgBeingProcessed);
    
    // Store its size relative to original size so we can rescale object detections later
    _heightScale = (f32)imageCache.GetOrigNumRows();
    _widthScale  = (f32)imageCache.GetOrigNumCols();
    
    PRINT_CH_INFO(kLogChannelName, "NeuralNetRunner.StartProcessingIfIdle.ProcessingImage",
                  "Detecting salient points in %dx%d image t=%u",
                  _imgBeingProcessed.GetNumCols(), _imgBeingProcessed.GetNumRows(), _imgBeingProcessed.GetTimestamp());
    
    _future = std::async(std::launch::async, [this]() {
      std::list<SalientPoint> salientPoints;
  
      _profiler.Tic("Model.Run");
      Result result = _model->Run(_imgBeingProcessed, salientPoints);
      _profiler.Toc("Model.Run");
      if(RESULT_OK != result)
      {
        PRINT_NAMED_WARNING("NeuralNetRunner.StartProcessingIfIdle.AsyncLambda.ModelRunFailed", "");
      }
    
      return salientPoints;
    });
    
    // We did start processing the given image
    return true;
  }
  
  // We were not idle, so did not start processing the new image
  return false;
}
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool NeuralNetRunner::GetDetections(std::list<SalientPoint>& salientPoints)
{
  // Always clear the output list, so it's definitely only populated once the
  // future is ready below
  salientPoints.clear();

  if(_future.valid())
  {
    // Check the future's status and keep waiting until it's ready.
    // Once it's ready, return the result.
    const auto kWaitForTime = std::chrono::microseconds(500);
    const std::future_status futureStatus = _future.wait_for(kWaitForTime);
    if(std::future_status::ready == futureStatus)
    {
      salientPoints = _future.get();
      DEV_ASSERT(!_future.valid(), "NeuralNetRunner.GetDetections.FutureStillValid");
      
      // The detection will be in normalized coordinates. Need to convert it to original resolution.
      std::for_each(salientPoints.begin(), salientPoints.end(), [this](SalientPoint& salientPoint)
                    {
                      salientPoint.x_img *= _widthScale;
                      salientPoint.y_img *= _heightScale;
                      
                      for(auto &pt : salientPoint.shape)
                      {
                        pt.x *= _widthScale;
                        pt.y *= _heightScale;
                      }
                    });
      
      if(ANKI_DEV_CHEATS)
      {
        if(salientPoints.empty())
        {
          PRINT_CH_INFO(kLogChannelName, "NeuralNetRunner.GetDetections.NoSalientPoints",
                        "t=%ums", _imgBeingProcessed.GetTimestamp());
        }
        for(auto const& salientPoint : salientPoints)
        {
          PRINT_CH_INFO(kLogChannelName, "NeuralNetRunner.GetDetections.FoundSalientPoint",
                        "t=%ums Name:%s Score:%.3f",
                        _imgBeingProcessed.GetTimestamp(), salientPoint.description.c_str(), salientPoint.score);
        }
      }
      
      return true;
    }
  }
  
  return false;
}
  
} // namespace Vision
} // namespace Anki

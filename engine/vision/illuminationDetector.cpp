/**
 * File: illuminationDetector.cpp
 * 
 * Author: Humphrey Hu
 * Date:   2018-05-25
 * 
 * Description: Vision system component for detecting scene illumination state/changes.
 * 
 * Copyright: Anki, Inc. 2018
 **/

#include "coretech/common/engine/jsonTools.h"
#include "coretech/common/engine/math/linearClassifier_impl.h"
#include "coretech/common/engine/utils/data/dataPlatform.h"
#include "coretech/vision/engine/imageBrightnessHistogram.h"
#include "coretech/vision/engine/imageCache.h"

#include "engine/cozmoContext.h"
#include "engine/vision/illuminationDetector.h"
#include "engine/vision/illuminationState.h"
#include "engine/vision/visionPoseData.h"

#include "util/math/math.h"

#include <fstream>

namespace Anki {
namespace Cozmo {

IlluminationDetector::IlluminationDetector() 
: _classifier( new LinearClassifier() ) {}

Result IlluminationDetector::Init( const Json::Value& config, const CozmoContext* context )
{
  // Macro to help parse all these parameters and return error codes
  #define PARSE_PARAM(conf, key, var, vtype, func) \
    if( !JsonTools::func<vtype>( conf, key, var ) ) \
    { \
      PRINT_NAMED_ERROR( "IlluminationDetector.Init.MissingParameter", "Could not parse parameter: %s", key ); \
      return RESULT_FAIL; \
    }

  // Read classifier parameters from separate file
  std::string classifierConfigPath;
  PARSE_PARAM(config, "ClassifierConfigPath", classifierConfigPath, std::string, GetValueOptional);
  const std::string fullPath = context->GetDataPlatform()->pathToResource(Anki::Util::Data::Scope::Resources,
                                                                          classifierConfigPath);
  std::ifstream configStream( fullPath );
  if( !configStream.is_open() )
  {
    PRINT_NAMED_ERROR( "IlluminationDetector.Init.ConfigLoadFailure",
                       "Could not load config from %s", fullPath.c_str() );
    return RESULT_FAIL;
  }
  Json::Value classifierConfig;
  configStream >> classifierConfig;

  // Initialize linear model from classifier config
  Result classifierResult = _classifier->Init( classifierConfig["LinearClassifier"] );
  if( classifierResult != RESULT_OK )
  {
    PRINT_NAMED_ERROR( "IlluminationDetector.Init.ClassifierInitFailure",
                       "Failed to initialize linear classifier" );
    return RESULT_FAIL;
  }

  // Parse non-tunable parameters from classifier config
  std::vector<f32> percs;
  PARSE_PARAM(classifierConfig, "FeatureWindowLength", _featWindowLength, u32, GetValueOptional);
  PARSE_PARAM(classifierConfig, "FeaturePercentiles", percs, f32, GetVectorOptional);
  for( unsigned int i = 0; i < percs.size(); ++i )
  {
    if( Util::IsFltLT(percs[i], 0.0f) || Util::IsFltGT(percs[i], 100.0f ) )
    {
      PRINT_NAMED_ERROR( "IlluminationDetector.Init.InvalidPercentile",
                         "Percentile %f out of bounds [0, 100]", percs[i] );
      return RESULT_FAIL;
    }
    // Percentiles must increase monotonically due to behavior of histogram
    if( i > 0 && Util::IsFltLE(percs[i], percs[i-1]) )
    {
      PRINT_NAMED_ERROR ("IlluminationDetector.Init.InvalidPercentile",
                         "Percentile %f not greater than previous %f", percs[i], percs[i-1]);
      return RESULT_FAIL;
    }
    _featPercentiles.insert( percs[i] );
  }

  // Parse tunable parameters
  PARSE_PARAM(config, "FeaturePercentileSubsample", _featPercSubsample, s32, GetValueOptional);
  PARSE_PARAM(config, "IlluminatedMinProbability", _illumMinProb, f32, GetValueOptional);
  PARSE_PARAM(config, "DarkenedMaxProbability", _darkMaxProb, f32, GetValueOptional);
  
  return RESULT_OK;
}

Result IlluminationDetector::Detect( Vision::ImageCache& cache,
                                     const VisionPoseData& poseData,
                                     Vision::IlluminationState& illumination )
{
  // If the robot moved, clear buffer and bail
  illumination = Vision::IlluminationState::Unknown;

  if( !CanRunDetection( poseData ) )
  {
    _featureBuffer.clear();
    return RESULT_OK;
  }
  
  GenerateFeatures( cache );

  // If not enough buffered timepoints, bail
  if( _featureBuffer.size() < _classifier->GetInputDim() )
  {
    PRINT_NAMED_DEBUG("IlluminationDetector.Detect.Buffering", "Buffer has %u/%u", 
                      (u32) _featureBuffer.size(), (u32) _classifier->GetInputDim());
    return RESULT_OK;
  }
  while( _featureBuffer.size() > _classifier->GetInputDim() )
  {
    _featureBuffer.pop_back(); // Front is newest, back is oldest
  }

  const f32 prob = _classifier->ClassifyProbability( _featureBuffer );

  if( prob > _illumMinProb )
  {
    illumination = Vision::IlluminationState::Illuminated;
  }
  else if( prob < _darkMaxProb )
  {
    illumination = Vision::IlluminationState::Darkened;
  }

  #ifndef NDEBUG
  std::stringstream ss;
  ss << "[";
  for( unsigned int i = 0; i < _featureBuffer.size() - 1; ++i )
  {
    ss << _featureBuffer[i] << ", ";
  }
  ss << _featureBuffer[_featureBuffer.size() - 1] << "]";
  PRINT_NAMED_DEBUG("IlluminationDetector.Detect.Features",
                    "Features: %s", ss.str().c_str());
  #endif

  PRINT_CH_INFO("VisionSystem", "IlluminationDetector.Detect.Result",
                "Probability: %.3f", prob);

  return RESULT_OK;
}

bool IlluminationDetector::CanRunDetection( const VisionPoseData& poseData )
{
  const HistRobotState& state = poseData.histState;
  return !state.WasCarryingObject() && !state.WasMoving() &&
         !state.WasHeadMoving() && !state.WasLiftMoving() &&
         !state.WereWheelsMoving() && !state.WasPickedUp();
}

void IlluminationDetector::GenerateFeatures( Vision::ImageCache& cache )
{
  Vision::ImageBrightnessHistogram hist;
  hist.FillFromImage( cache.GetGray(), _featPercSubsample );
  std::vector<u8> percentiles = hist.ComputePercentiles( _featPercentiles );
  
  #ifndef NDEBUG
  std::string f;
  for( auto iter = percentiles.begin(); iter != percentiles.end(); ++iter )
  {
    f += std::to_string(*iter) + ", ";
  }
  PRINT_NAMED_DEBUG("IlluminationDetector.GenerateFeatures.Features",
                    "Percentiles: %s", f.c_str());
  #endif

  // NOTE Have to push percentiles in reverse order
  for( auto iter = percentiles.rbegin(); iter != percentiles.rend(); ++iter )
  {
    _featureBuffer.push_front(static_cast<f32>(*iter) / 255.0f);
  }
}

}
}
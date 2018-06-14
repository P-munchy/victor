/**
 * File: nightVisionFilter.cpp
 *
 * Author: Humphrey Hu
 * Date:   2018-06-08
 *
 * Description: Helper class for averaging images together and contrast adjusting them.
 *
 * Copyright: Anki, Inc. 2018
 **/

#include "engine/vision/nightVisionFilter.h"
#include "coretech/common/engine/array2d_impl.h"
#include "coretech/common/engine/jsonTools.h"
#include "coretech/vision/engine/imageBrightnessHistogram.h"

#include "util/console/consoleInterface.h"

namespace Anki {
namespace Cozmo {

const char* kMinAccImagesKey     = "MinNumImages";
const char* kHistSubsampleKey    = "HistSubsample";
const char* kTargetPercentileKey = "TargetPercentile";
const char* kTargetValueKey      = "TargetValue";
const char* kBodyAngleThreshKey  = "BodyAngleThreshold";
const char* kBodyPoseThreshKey   = "BodyPoseThreshold";
const char* kHeadAngleThreshKey  = "HeadAngleThreshold";

CONSOLE_VAR(f32, _contrastTargetPercentile, "Vision.NightVision", 50.0f);
CONSOLE_VAR(u8, _contrastTargetValue, "Vision.NightVision", 240);

u16 CastPixel(const u8& p) { return (u16) p; }

u8 DividePixel(const u16& p, const u16& count)
{
  u16 out = p / count;
  if( out > 255 ) { out = 255; }
  return static_cast<u8>( out );
}

u8 ScalePixel(u8 p, const f32& k)
{
  u16 pK = static_cast<u16>( p * k );
  if( pK > 255 ) { pK = 255; }
  return static_cast<u8>( pK );
}

NightVisionFilter::NightVisionFilter()
: _contrastHist( new Vision::ImageBrightnessHistogram )
{
  Reset();
}

Result NightVisionFilter::Init( const Json::Value& config )
{
  #define PARSE_PARAM(conf, key, var) \
  if( !JsonTools::GetValueOptional( conf, key, var ) ) \
  { \
    PRINT_NAMED_ERROR( "NightVisionFilter.Init.MissingParameter", "Could not parse parameter: %s", key ); \
    return RESULT_FAIL; \
  }
  #define PARSE_PARAM_ANGLE(conf, key, var) \
  if( !JsonTools::GetAngleOptional( conf, key, var, true ) ) \
  { \
    PRINT_NAMED_ERROR( "NightVisionFilter.Init.MissingParameter", "Could not parse parameter: %s", key ); \
    return RESULT_FAIL; \
  }
  
  PARSE_PARAM( config, kMinAccImagesKey, _minNumImages );
  PARSE_PARAM( config, kHistSubsampleKey, _histSubsample );
  // PARSE_PARAM( config, kTargetPercentileKey, _contrastTargetPercentile );
  // PARSE_PARAM( config, kTargetValueKey, _contrastTargetValue );
  PARSE_PARAM_ANGLE( config, kBodyAngleThreshKey, _bodyAngleThresh );
  PARSE_PARAM( config, kBodyPoseThreshKey, _bodyPoseThresh );
  PARSE_PARAM_ANGLE( config, kHeadAngleThreshKey, _headAngleThresh );
  Reset();
  return RESULT_OK;
}

void NightVisionFilter::Reset()
{
  _numAccImages = 0;
}

void NightVisionFilter::AddImage( const Vision::Image& img, 
                                  const VisionPoseData& poseData )
{
  // If first image, allocate accumulator and reset
  if( _numAccImages == 0 )
  {
    _accumulator.Allocate( img.GetNumRows(), img.GetNumCols() );
    _accumulator.FillWith( 0 );
  }
  // Otherwise see if robot moved, since filter can only run when stationary
  else if( HasMoved( poseData ) )
  {
    Reset();
    return;
  }
  _lastPoseData = poseData;

  // Sanity check
  if( img.GetNumRows() != _accumulator.GetNumRows() ||
      img.GetNumCols() != _accumulator.GetNumCols() )
  {
    PRINT_NAMED_ERROR("NightVisionFilter.AddImage.SizeError", "");
    Reset();
    return;
  }

  // Cast img to u16 type to add
  _castImage.Allocate( img.GetNumRows(), img.GetNumCols() );
  std::function<u16(const u8&)> castOp = std::bind(&CastPixel, std::placeholders::_1);
  img.ApplyScalarFunction( castOp, _castImage );
  _accumulator += _castImage;
  _lastTimestamp = img.GetTimestamp();
  _numAccImages++;
}

bool NightVisionFilter::HasMoved( const VisionPoseData& poseData )
{
  // Some of these are not set to true if robot moved by human
  bool robotMoved = poseData.histState.WasCameraMoving() || 
                    poseData.histState.WasPickedUp() ||
                    poseData.histState.WasLiftMoving();
  // Should always catch case when robot moved by human
  bool isStill = _lastPoseData.IsBodyPoseSame( poseData, _bodyAngleThresh, _bodyPoseThresh ) &&
                 _lastPoseData.IsHeadAngleSame( poseData, _headAngleThresh );
  return robotMoved || !isStill;
}

bool NightVisionFilter::GetOutput( Vision::Image& out ) const
{
  if( _numAccImages < _minNumImages )
  {
    return false;
  }

  // Divide by the number of images
  out.Allocate( _accumulator.GetNumRows(), _accumulator.GetNumCols() );
  std::function<u8(const u16&)> divOp = std::bind(&DividePixel, std::placeholders::_1, _numAccImages);
  _accumulator.ApplyScalarFunction(divOp, out);

  // Compute image histogram and scale contrast
  _contrastHist->Reset();
  _contrastHist->FillFromImage( out, _histSubsample );
  u8 val = _contrastHist->ComputePercentile( _contrastTargetPercentile );
  f32 scale = static_cast<f32>(_contrastTargetValue) / val;
  PRINT_NAMED_INFO( "NightVisionFilter.GetOutput.Info",
                    "Percentile value: %d scale: %f", val, scale );
  std::function<u8(const u8)> scaleOp = std::bind(&ScalePixel, std::placeholders::_1, scale);
  out.ApplyScalarFunction(scaleOp);
  out.SetTimestamp( _lastTimestamp );
  return true;
}

} // end namespace Cozmo
} // end namespace Anki
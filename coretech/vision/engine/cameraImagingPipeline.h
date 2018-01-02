/**
 * File: cameraImagingPipeline.h
 *
 * Author: Andrew Stein
 * Date:   10/11/2016
 *
 * NOTE: Adapted from Pete Barnum's original Embedded implementation
 *
 * Description: Settings and methods for image capture pipeline, including things
 *              like exposure control, gamma lookup tables, vignetting, etc.
 *
 * Copyright: Anki, Inc. 2016
 **/

#ifndef __Anki_Coretech_Vision_CameraImagingPipeline_H__
#define __Anki_Coretech_Vision_CameraImagingPipeline_H__

#include "anki/common/types.h"
#include "coretech/vision/engine/imageBrightnessHistogram.h"

#include <array>
#include <vector>

namespace Anki {
namespace Vision {

  class Image;
  
  class ImagingPipeline
  {
  public:
    
    ImagingPipeline();
    
    Result SetExposureParameters(u8  targetMidValue,
                                 f32 midPercentile,
                                 f32 maxChangeFraction,
                                 s32 subSample);
    
    Result SetGammaTable(const std::vector<u8>& inputs, const std::vector<u8>& gamma);
    
    Result ComputeExposureAdjustment(const Vision::Image& image, f32& adjustmentFraction);
    Result ComputeExposureAdjustment(const Vision::Image& image, const Vision::Image& weightMask, f32& adjustmentFraction);
    
    Result ComputeExposureAdjustment(const std::vector<Vision::Image>& imageROIs, f32& adjustmentFraction);
    
    Result Linearize(const Vision::Image& image, Vision::Image& linearizedImage) const;
    
    // Get histogram computed in last ComputeExposureAdjustment() call
    const ImageBrightnessHistogram& GetHistogram() const { return _hist; }
    
  private:
    
    u8        _targetMidValue           = 128;
    f32       _midPercentile            = 0.5f;
    f32       _maxChangeFraction        = 0.5f;
    s32       _subSample                = 1;
    
    ImageBrightnessHistogram _hist;
    
    std::array<u8,256> _gammaLUT;
    std::array<u8,256> _linearizationLUT;
    
  };
  
} // namespace Vision
} // namespace Anki

#endif //__Anki_Coretech_Vision_CameraImagingPipeline_H__

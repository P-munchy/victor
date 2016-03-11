

#include "anki/vision/robot/nearestNeighborLibrary.h"
#include "anki/vision/robot/fiducialMarkers.h"

#include "anki/common/robot/array2d.h"
#include "anki/common/robot/fixedLengthList.h"
#include "anki/common/robot/errorHandling.h"

#include <array>

#define USE_EARLY_EXIT_DISTANCE_COMPUTATION 1
#define USE_WEIGHTS 0
#define USE_ILLUMINATION_NORMALIZATION 0
#define BINARIZE_PROBES 0
#define DRAW_DEBUG_IMAGES 1  // Requires VisionComponent to be in Synchronous mode

namespace Anki {
namespace Embedded {
  
# if USE_ILLUMINATION_NORMALIZATION
  //static const f32 kIllumFilterSize = 2*std::ceil((f32)VisionMarker::GRIDSIZE / 15.f)-1;
  static const f32 kIllumFilterSize = VisionMarker::GRIDSIZE / 2 - 1;
# endif
  
  NearestNeighborLibrary::NearestNeighborLibrary()
  : _isInitialized(false)
  , _probeXOffsets(NULL)
  , _probeYOffsets(NULL)
  , _useHoG(false)
  {
    
  }
  
  NearestNeighborLibrary::NearestNeighborLibrary(const std::string& dataPath,
                                                 const s32 numDataPoints, const s32 dataDim,
                                                 const s16* probeCenters_X, const s16* probeCenters_Y,
                                                 const s16* probePoints_X, const s16* probePoints_Y,
                                                 const s32 numProbePoints, const s32 numFractionalBits)
  : _isInitialized(false)
  , _probeValues(1, dataDim)
  , _data(numDataPoints, dataDim)
# if USE_WEIGHTS
  , _weights(numDataPoints, dataDim)
  , _totalWeight(numDataPoints, 1)
# endif
  , _numDataPoints(numDataPoints)
  , _dataDimension(dataDim)
  , _labels(1, numDataPoints)
  , _probeXCenters(probeCenters_X)
  , _probeYCenters(probeCenters_Y)
  , _probeXOffsets(probePoints_X)
  , _probeYOffsets(probePoints_Y)
  , _numProbeOffsets(numProbePoints)
  , _numFractionalBits(numFractionalBits)
  , _useHoG(false)
  {
    const std::string nnLibPath(dataPath + "/nnLibrary");
   
    AnkiConditionalErrorAndReturn(dataPath.empty() == false,
                                  "NearestNeighborLibrary.Constructor.EmptyDataPath", "");
    
    std::string dataFile = nnLibPath + "/nnLibrary.bin";
    
    FILE* fp = fopen(dataFile.c_str(), "rb");
    AnkiConditionalErrorAndReturn(fp, "NearestNeighborLibrary.Constructor.MissingFile",
                                  "Unable to find NN library data file '%s'.", dataFile.c_str());
    fread(_data.data, numDataPoints*dataDim, sizeof(u8), fp);
    fclose(fp);
    
#   if USE_WEIGHTS
    dataFile = nnLibPath + "/nnLibrary_weights.bin";
    AnkiConditionalErrorAndReturn(fp, "NearestNeighborLibrary.Constructor.MissingFile",
                                  "Unable to find NN library weights file '%s'.", dataFile.c_str());
    fp = fopen(dataFile.c_str(), "rb");
    fread(_weights.data, numDataPoints*dataDim, sizeof(u8), fp);
    fclose(fp);
#   endif
    
    dataFile = nnLibPath + "/nnLibrary_labels.bin";
    AnkiConditionalErrorAndReturn(fp, "NearestNeighborLibrary.Constructor.MissingFile",
                                  "Unable to find NN library labels file '%s'.", dataFile.c_str());
    fp = fopen(dataFile.c_str(), "rb");
    fread(_labels.data, numDataPoints, sizeof(u16), fp);
    fclose(fp);
    
    Init();
    
    _isInitialized = true;
  }
  
  NearestNeighborLibrary::NearestNeighborLibrary(const u8* data,
                                                 const u8* weights,
                                                 const u16* labels,
                                                 const s32 numDataPoints, const s32 dataDim,
                                                 const s16* probeCenters_X, const s16* probeCenters_Y,
                                                 const s16* probePoints_X, const s16* probePoints_Y,
                                                 const s32 numProbePoints, const s32 numFractionalBits)
  : _isInitialized(true)
  , _probeValues(1, dataDim)
  , _data(numDataPoints, dataDim)
# if USE_WEIGHTS
  , _weights(numDataPoints, dataDim, const_cast<u8*>(weights))
  , _totalWeight(numDataPoints, 1)
# endif
  , _numDataPoints(numDataPoints)
  , _dataDimension(dataDim)
  , _labels(1, numDataPoints, const_cast<u16*>(labels))
  , _probeXCenters(probeCenters_X)
  , _probeYCenters(probeCenters_Y)
  , _probeXOffsets(probePoints_X)
  , _probeYOffsets(probePoints_Y)
  , _numProbeOffsets(numProbePoints)
  , _numFractionalBits(numFractionalBits)
  , _useHoG(false)
  //, _probeValues(1, _dataDimension)
  {
    const cv::Mat_<u8> temp(numDataPoints, dataDim, const_cast<u8*>(data));
    temp.copyTo(_data);
    
    Init();
  }
  
  void NearestNeighborLibrary::Init()
  {
#   if USE_WEIGHTS
    // Sum all the weights for each example in the library along the columns:
    cv::reduce(_weights, _totalWeight, 1, CV_REDUCE_SUM);
#   endif
    
#   if USE_ILLUMINATION_NORMALIZATION
    // Normalize all the stored data
    assert(_data.isContinuous());
    for(s32 iPoint=0; iPoint<_data.rows; ++iPoint)
    {
      u8* data_i = _data.ptr(iPoint);
      NormalizeIllumination(data_i, VisionMarker::GRIDSIZE, kIllumFilterSize);
    }
#   endif
  }
  
  NearestNeighborLibrary::NearestNeighborLibrary(const u8* HoGdata,
                                                 const u16* labels,
                                                 const s32 numDataPoints, const s32 dataDim,
                                                 const s16* probeCenters_X, const s16* probeCenters_Y,
                                                 const s16* probePoints_X, const s16* probePoints_Y,
                                                 const s32 numProbePoints, const s32 numFractionalBits,
                                                 const s32 numHogScales, const s32 numHogOrientations)
  : _isInitialized(true)
  , _probeValues(1, dataDim)
  , _data(numDataPoints, dataDim, const_cast<u8*>(HoGdata))
  , _numDataPoints(numDataPoints)
  , _dataDimension(dataDim)
  , _labels(1, numDataPoints, const_cast<u16*>(labels))
  , _probeXCenters(probeCenters_X)
  , _probeYCenters(probeCenters_Y)
  , _probeXOffsets(probePoints_X)
  , _probeYOffsets(probePoints_Y)
  , _numProbeOffsets(numProbePoints)
  , _numFractionalBits(numFractionalBits)
  , _useHoG(true)
  , _numHogScales(numHogScales)
  , _numHogOrientations(numHogOrientations)
  //  , _probeValues(1, _dataDimension)
  , _probeHoG(16, _numHogScales*_numHogOrientations)
  , _probeHoG_F32(16, _numHogScales*_numHogOrientations)
  {

  }
  
  Result NearestNeighborLibrary::GetNearestNeighbor(const Array<u8> &image, const Array<f32> &homography, const s32 distThreshold, s32 &label, s32 &closestDistance)
  {
    const s32 kMedianBlurSize = 0;
    const f32 kGaussianBlurSigma = 0;
    cv::Mat_<u8> morphKernel; // = cv::Mat_<u8>::ones(3, 3);
    
    label = -1;
    s32 closestIndex = -1;
    s32 secondClosestIndex = -1;
  
    Result lastResult = VisionMarker::GetProbeValues(image, homography,
                                                     false, //USE_ILLUMINATION_NORMALIZATION,
                                                     _probeValues);
    
    closestDistance = s32_MAX;
    s32 secondClosestDistance = s32_MAX;
    
    cv::Mat_<u8> probeImage(VisionMarker::GRIDSIZE, VisionMarker::GRIDSIZE,
                            _probeValues.data);
    cv::normalize(probeImage, probeImage, 255, 0, CV_MINMAX);
    
    cv::Mat_<u8> diffImage, bestDiffImage, secondBestDiffImage;
    for(s32 iExample=0; iExample<_numDataPoints; ++iExample)
    {
      cv::Mat_<u8> currentExample = _data.row(iExample).reshape(0, VisionMarker::GRIDSIZE);
     
      cv::absdiff(probeImage, currentExample, diffImage);
      
      if(kMedianBlurSize > 0) {
        cv::medianBlur(diffImage > distThreshold, diffImage, kMedianBlurSize);
      }
      if(kGaussianBlurSigma > 0) {
        const s32 kernelSize = 2*std::ceil(kGaussianBlurSigma)+1;
        cv::GaussianBlur(diffImage, diffImage,
                         cv::Size(kernelSize, kernelSize),
                         kGaussianBlurSigma);
      }
      if(!morphKernel.empty()) {
        cv::morphologyEx(diffImage>32, diffImage, cv::MORPH_OPEN, morphKernel);
        cv::morphologyEx(diffImage, diffImage, cv::MORPH_CLOSE, morphKernel);
      }
      
#     if USE_WEIGHTS
      cv::Mat_<u8> currentWeights = _weights.row(iExample).reshape(0, VisionMarker::GRIDSIZE);
      diffImage.mul(_weights, 1./255.);
      const s32 currentDistance = (f32)(255 * cv::sum(diffImage)[0]) / (f32)_totalWeight(iExample,0);// * _totalWeight(iExample,0));
#     else
      const s32 currentDistance = cv::sum(diffImage)[0] / _dataDimension;
#     endif
      
      if(currentDistance < closestDistance)
      {
        secondClosestIndex = closestIndex;
        secondClosestDistance = closestDistance;
        closestDistance = currentDistance;
        closestIndex = iExample;
        if(DRAW_DEBUG_IMAGES)
        {
          std::swap(bestDiffImage, secondBestDiffImage);
          diffImage.copyTo(bestDiffImage);
        }
      }
      else if(currentDistance < secondClosestDistance)
      {
        secondClosestIndex = iExample;
        secondClosestDistance = currentDistance;
        if(DRAW_DEBUG_IMAGES)
        {
          diffImage.copyTo(secondBestDiffImage);
        }
      }
    }
  
    if(closestIndex != -1)
    {
      s32 closestLabel = _labels.at<s16>(closestIndex);
      s32 secondLabel = _labels.at<s16>(secondClosestIndex);
      
      s32 maskedDist = -1;
      if(secondClosestIndex == -1 || closestLabel == secondLabel) {
        // Top two labels the same, nothing more to check
        label = closestLabel;
      } else {
        // Find where the two top examples differ and check only those probes for
        // change
        const u8* restrict example1 = _data[closestIndex];
        const u8* restrict example2 = _data[secondClosestIndex];
        const u8* restrict probeData = _probeValues[0];
        s32 count = 0;
        maskedDist = 0;
        for(s32 i=0; i<_dataDimension; ++i)
        {
          if(std::abs((s32)(example1[i]) - (s32)(example2[i])) > distThreshold)
          {
            maskedDist += std::abs((s32)(probeData[i]) - (s32)(example1[i]));
            ++count;
          }
        }

        // Allow a bit more average variation since we're looking at a smaller
        // number of probes
        const f32 distThreshLeniency = 1.25f;
        
        maskedDist /= count;
        if(maskedDist < distThreshLeniency*distThreshold) {
          label = closestLabel;
        }
      }
      
      if(DRAW_DEBUG_IMAGES)
      {
        const s32 dispSize = 256;
 
        cv::Mat probeDisp;
        cv::resize(probeImage, probeDisp, cv::Size(dispSize,dispSize));
        cv::imshow("Probes", probeDisp);
        
        cv::Mat closestExampleDisp;
        cv::Mat_<u8> tempClosestExample(VisionMarker::GRIDSIZE, VisionMarker::GRIDSIZE,
                                        _data[closestIndex]);
        cv::resize(tempClosestExample, closestExampleDisp, cv::Size(dispSize,dispSize));
        cv::cvtColor(closestExampleDisp, closestExampleDisp, CV_GRAY2BGR);
      
        cv::resize(bestDiffImage, bestDiffImage, cv::Size(dispSize,dispSize));
        cv::imshow("ClosestDiff", bestDiffImage);
        
        char closestStr[128];
        snprintf(closestStr, 127, "Dist: %d(%d), Index: %d, Label: %d",
                 closestDistance, maskedDist, closestIndex, closestLabel);
        cv::putText(closestExampleDisp, closestStr, cv::Point(0,closestExampleDisp.rows-2), CV_FONT_NORMAL, 0.4, cv::Scalar(0,0,255));
        cv::imshow("ClosestExample", closestExampleDisp);
        
        if(secondClosestIndex != -1)
        {
          cv::resize(secondBestDiffImage, secondBestDiffImage, cv::Size(dispSize,dispSize));
          cv::imshow("SecondDiff", secondBestDiffImage);
          
          cv::Mat secondClosestDisp;
          cv::Mat_<u8> tempSecondExample(VisionMarker::GRIDSIZE, VisionMarker::GRIDSIZE,
                                          _data[secondClosestIndex]);
          cv::resize(tempSecondExample, secondClosestDisp, cv::Size(dispSize,dispSize));
          cv::cvtColor(secondClosestDisp, secondClosestDisp, CV_GRAY2BGR);
          
          cv::Mat maskDisp;
          cv::absdiff(tempClosestExample, tempSecondExample, maskDisp);
          cv::resize(maskDisp>distThreshold, maskDisp, cv::Size(dispSize, dispSize), CV_INTER_NN);
          cv::imshow("ExampleDiffMask", maskDisp);
          
          char closestStr[128];
          snprintf(closestStr, 127, "Dist: %d, Index: %d, Label: %d",
                   secondClosestDistance, secondClosestIndex, secondLabel);
          cv::putText(secondClosestDisp, closestStr, cv::Point(0,secondClosestDisp.rows-2), CV_FONT_NORMAL, 0.4, cv::Scalar(0,0,255));
          cv::imshow("SecondExample", secondClosestDisp);
        }
        
        cv::waitKey(1);
      }
    }
    return lastResult;
  }
  
#if 0

  Result NearestNeighborLibrary::GetNearestNeighbor(const Array<u8> &image,
                                                    const Array<f32> &homography,
                                                    const s32 distThreshold,
                                                    s32 &label, s32 &closestDistance)

  {
    AnkiConditionalErrorAndReturnValue(_isInitialized, RESULT_FAIL,
                                       "NearestNeighborLibrary.GetNearestNeighbor.NotInitialized", "");
    
    // Set these return values up front, in case of failure
#   if USE_EARLY_EXIT_DISTANCE_COMPUTATION
    closestDistance = distThreshold;
#   else
    closestDistance = std::numeric_limits<s32>::max();
#   endif
    
    label = -1;
    
    //closestDistance *= _dataDimension;
    
    //const f32 minDistDiff = 0.02f * closestDistance;
    
    s32 secondClosestDistance = closestDistance;
    s32 secondIndex = -1;
    
    Result lastResult = VisionMarker::GetProbeValues(image, homography,
                                                     false, //USE_ILLUMINATION_NORMALIZATION,
                                                     _probeValues);
    
    AnkiConditionalErrorAndReturnValue(lastResult == RESULT_OK, lastResult,
                                       "NearestNeighborLibrary.GetNearestNeighbor",
                                       "GetProbeValues() failed.");
    
    if(_useHoG) {
      GetProbeHoG();
      assert(_probeHoG.isContinuous());
      
      // Visualize HoG values
      //cv::Mat_<u8> temp(16,32,_probeHoG.data);
      //cv::imshow("ProbeHoG", _probeHoG);
      //cv::waitKey();
      
    }
    u8* restrict pProbeData = (_useHoG ? _probeHoG[0] : _probeValues[0]);

#   if USE_ILLUMINATION_NORMALIZATION
    lastResult = NormalizeIllumination(_probeValues[0], VisionMarker::GRIDSIZE, kIllumFilterSize);
    AnkiConditionalErrorAndReturnValue(lastResult == RESULT_OK, lastResult,
                                       "NearestNeighborLibrary.GetNearestNeighbor",
                                       "NormalizeIllumination() failed.");
#   else
    u8 minProbeValue = u8_MAX;
    u8 maxProbeValue = 0;
    for(s32 iProbe=0; iProbe < _dataDimension; ++iProbe)
    {
      u8 currentProbeVal = pProbeData[iProbe];
      if(currentProbeVal > maxProbeValue) {
        maxProbeValue = currentProbeVal;
      }
      if(currentProbeVal < minProbeValue) {
        minProbeValue = currentProbeVal;
      }
    }
    
    AnkiConditionalWarnAndReturnValue(maxProbeValue > minProbeValue, RESULT_OK,
                                      "NearestNeighborLibrary.GetNearestNeighbor",
                                      "MaxProbeValue (%d) <= MinProbeValue (%d)",
                                      maxProbeValue, minProbeValue);

#   if BINARIZE_PROBES
    const u8 binarizeThreshold = (u8)(((s32)maxProbeValue + (s32)minProbeValue)/2);
#   endif

    // Stretch / binarize the probe values to be [0,255]
    const s32 probeValRange = maxProbeValue - minProbeValue;
    for(s32 iProbe=0; iProbe < _dataDimension; ++iProbe)
    {
#     if BINARIZE_PROBES
      pProbeData[iProbe] = (pProbeData[iProbe] >= binarizeThreshold ? 255 : 0);
#     else
      pProbeData[iProbe] = (s32)(pProbeData[iProbe]-minProbeValue)*255 / probeValRange;
#     endif
    }
    
#   endif // USE_ILLUMINATION_NORMALIZATION
    
    /*
    // Visualize probe values
    cv::Mat_<u8> temp(VisionMarker::GRIDSIZE,VisionMarker::GRIDSIZE,_probeValues.data);
    cv::Mat_<u8> probeDisp;
    cv::resize(temp, probeDisp, cv::Size(256,256));
    cv::imshow("ProbeValues", probeDisp);
    cv::Mat_<u8> tempBinary(VisionMarker::GRIDSIZE,VisionMarker::GRIDSIZE,binarizedProbes);
    cv::Mat_<u8> binaryDisp;
    cv::resize(tempBinary, binaryDisp, cv::Size(256,256));
    cv::imshow("BinarizedProbes", binaryDisp);
    cv::waitKey(10);
    */
    
    s32 closestIndex = -1;
    
    if(_useHoG) {
      closestDistance *= _probeHoG.rows*_probeHoG.cols;
    }
    
    for(s32 iExample=0; iExample<_numDataPoints; ++iExample)
    {
      const u8* currentExample = _data[iExample];

      s32 currentDistance = 0;
      s32 iProbe = 0;

      if(_useHoG) {
        
        // Visualize current example
        //cv::Mat_<u8> temp(16, _numHogScales*_numHogOrientations,
        //                  const_cast<u8*>(currentExample));
        //cv::imshow("CurrentExample", temp);
        //cv::waitKey();
        
        while(iProbe < _dataDimension && currentDistance < closestDistance) {
          const s32 diff = static_cast<s32>(pProbeData[iProbe]) - static_cast<s32>(currentExample[iProbe]);
          currentDistance += std::abs(diff);
          ++iProbe;
        }
        
        if(currentDistance < closestDistance) {
          closestIndex = iExample;
          closestDistance = currentDistance;
        }
        
      } else {
        
#       if USE_WEIGHTS
        const u8* currentWeight  = _weights[iExample];
        const s32 currentTotalWeight = _totalWeight(iExample, 0);
#       endif
        
        // Visualize current example
        //cv::Mat_<u8> temp(32, 32, const_cast<u8*>(currentExample));
        //cv::namedWindow("CurrentExample", CV_WINDOW_KEEPRATIO);
        //cv::imshow("CurrentExample", temp);
        //cv::waitKey();
        
#       if USE_EARLY_EXIT_DISTANCE_COMPUTATION
        
#         if USE_WEIGHTS
          {
            // The distance threshold for this example depends on the total weight
            const s32 currentDistThreshold = currentTotalWeight * closestDistance;
            
            while(iProbe < _dataDimension && currentDistance < currentDistThreshold)
            {
              const s32 diff = static_cast<s32>(pProbeData[iProbe]) - static_cast<s32>(currentExample[iProbe]);
              currentDistance += static_cast<s32>(currentWeight[iProbe]) * std::abs(diff);
              ++iProbe;
            }
            
            if(currentDistance < currentDistThreshold) {
              currentDistance /= currentTotalWeight;
              if(currentDistance < closestDistance) {
                secondIndex = closestIndex;
                secondClosestDistance = closestDistance;
                closestIndex = iExample;
                closestDistance = currentDistance;
              }
            }
          }
#         else // don't use weights
          {
            const s32 currentDistThreshold = _dataDimension * closestDistance;
            while(iProbe < _dataDimension && currentDistance < currentDistThreshold)
            {
              const s32 diff = static_cast<s32>(pProbeData[iProbe]) - static_cast<s32>(currentExample[iProbe]);
              currentDistance += std::abs(diff);
              ++iProbe;
            }
            
            if(currentDistance < currentDistThreshold) {
              currentDistance /= _dataDimension;
              if(currentDistance < closestDistance) {
                secondIndex = closestIndex;
                secondClosestDistance = closestDistance;
                closestIndex = iExample;
                closestDistance = currentDistance;
              }
            }
          }
#         endif // USE_WEIGHTS
        
        
#       else // don't use early exit distance computation
        
#         if USE_WEIGHTS
          {
            for(s32 iProbe=0; iProbe < _dataDimension; ++iProbe) {
              const s32 diff = static_cast<s32>(pProbeData[iProbe]) - static_cast<s32>(currentExample[iProbe]);
              currentDistance += static_cast<s32>(currentWeight[iProbe]) * std::abs(diff);
            }
            
            currentDistance /= currentTotalWeight;
            
            if(currentDistance < closestDistance) {
              closestDistance = currentDistance;
              if(currentDistance < distThreshold) {
                secondIndex = closestIndex;
                secondClosestDistance = closestDistance;
                closestDistance = currentDistance;
                closestIndex = iExample;
              }
            }
          }
#         else // don't use weights
          {
            for(s32 iProbe=0; iProbe < _dataDimension; ++iProbe) {
              const s32 diff = static_cast<s32>(pProbeData[iProbe]) - static_cast<s32>(currentExample[iProbe]);
              currentDistance += std::abs(diff);
            }
            
            currentDistance /= _dataDimension;
            
            //printf("dist[%d] = %d\n", iExample, currentDistance);
            
            if(currentDistance < closestDistance) {
              closestDistance = currentDistance;
              if(currentDistance < distThreshold) {
                secondIndex = closestIndex;
                secondClosestDistance = closestDistance;
                closestDistance = currentDistance;
                closestIndex = iExample;
              }
            }
          }
#         endif // USE_WEIGHTS
        
#       endif // USE_EARLY_EXIT_DISTANCE_COMPUTATION
        
      } // if(_useHoG)
      
    } // for each example
    
    // Make sure the second best match is either the same label or sufficiently different
    // looking than the second best
    assert(secondClosestDistance >= closestDistance);
    const s32 distDiff = secondClosestDistance - closestDistance;
    
    const u16 closestLabel = _labels.at<u16>(closestIndex);
    const u16 secondLabel  = (secondIndex == -1 ? closestLabel : _labels.at<u16>(secondIndex));
    
    if(DRAW_DEBUG_IMAGES && closestIndex != -1)
    {
      const s32 dispSize = 256;
      
      cv::Mat_<u8> tempBinary(VisionMarker::GRIDSIZE,VisionMarker::GRIDSIZE,
                              const_cast<u8*>(pProbeData));
      cv::Mat binaryDisp;
      cv::resize(tempBinary, binaryDisp, cv::Size(dispSize,dispSize));
      cv::imshow("Probes", binaryDisp);
      
      cv::Mat closestExampleDisp, secondExampleDisp;
      cv::Mat_<u8> tempClosestExample(VisionMarker::GRIDSIZE, VisionMarker::GRIDSIZE,
                                      _data[closestIndex]);
      cv::resize(tempClosestExample, closestExampleDisp, cv::Size(dispSize,dispSize));
      cv::cvtColor(closestExampleDisp, closestExampleDisp, CV_GRAY2BGR);
      
      cv::Mat closestDiff;
      cv::absdiff(tempBinary, tempClosestExample, closestDiff);
#     if USE_WEIGHTS
      closestDiff.mul(cv::Mat_<u8>(VisionMarker::GRIDSIZE, VisionMarker::GRIDSIZE,
                                   _weights[closestIndex]), 1/255);
#     endif
      cv::resize(closestDiff, closestDiff, cv::Size(dispSize,dispSize));
      cv::imshow("ClosestDiff", closestDiff);

      if(secondIndex != -1) {
        cv::Mat_<u8> tempSecondExample(VisionMarker::GRIDSIZE, VisionMarker::GRIDSIZE,
                                       _data[secondIndex]);
        cv::resize(tempSecondExample,  secondExampleDisp, cv::Size(dispSize,dispSize));
        cv::cvtColor(secondExampleDisp, secondExampleDisp, CV_GRAY2BGR);
        
        cv::Mat secondDiff;
        cv::absdiff(tempBinary, tempSecondExample, secondDiff);
        cv::resize(secondDiff, secondDiff, cv::Size(dispSize,dispSize));
        cv::imshow("SecondDiff", secondDiff);
      } else {
        secondExampleDisp = cv::Mat(dispSize,dispSize,CV_8UC3);
        secondExampleDisp.setTo(0);
      }

      char closestStr[128], secondStr[128];
      snprintf(closestStr, 127, "Dist: %d, Index: %d, Label: %d",
               closestDistance, closestIndex, closestLabel);
      cv::putText(closestExampleDisp, closestStr, cv::Point(0,closestExampleDisp.rows-2), CV_FONT_NORMAL, 0.4, cv::Scalar(0,0,255));
      snprintf(secondStr, 127, "+Dist: %d, Index: %d, Label: %d",
               distDiff, secondIndex, secondLabel);
      cv::putText(secondExampleDisp, secondStr, cv::Point(0,secondExampleDisp.rows-2), CV_FONT_NORMAL, 0.4, cv::Scalar(0,0,255));
      cv::imshow("ClosestExample", closestExampleDisp);
      cv::imshow("SecondExample",  secondExampleDisp);
      cv::waitKey(1);
    }
    
    if(closestIndex != -1 && closestLabel == secondLabel) // || distDiff > minDistDiff))
    {
      label = closestLabel;
      /* DEBUG: Save out matches
      {
        static s32 matchCount = 0;
        const std::string savePrefix("/Users/andrew/temp/matches/match");
        cv::imwrite(savePrefix + std::to_string(matchCount) + "_library" +
                    std::to_string(closestIndex) + ".png", _data.row(closestIndex).reshape(1, 32));
        cv::imwrite(savePrefix + std::to_string(matchCount)  + "_observed.png",
                    _probeValues.reshape(1,32));
        ++matchCount;
      }
       */
      
      if(_useHoG) {
        closestDistance /= _probeHoG.rows * _probeHoG.cols;
      }
    }
    
    return RESULT_OK;
  } // GetNearestNeighbor()
  
#endif
  
  Result NearestNeighborLibrary::NormalizeIllumination(u8* data, s32 gridSize, s32 filterSize)
  {
    if(filterSize == 0) {
      assert(gridSize % 2 == 0);
      filterSize = gridSize/2 - 1;
    }
    assert(filterSize % 2 == 1);
    
    cv::Mat_<u8> temp(gridSize, gridSize, data);
    //cv::imshow("Original ProbeValues", temp);
    
    // TODO: Would it be faster to box filter and subtract it from the original?
    static cv::Mat_<s16> kernel;
    if(kernel.empty()) {
      //const s32 halfSize = filterSize/2;
      /*
       // Even sized kernel
       kernel = cv::Mat_<s16>(halfSize,halfSize);
       kernel = -1;
       kernel(kernel.rows/2,kernel.cols/2) = kernel.rows*kernel.cols - 1;
       */
      // Odd sized kernel
      kernel = cv::Mat_<s16>(filterSize,filterSize);
      kernel = -1;
      kernel((kernel.rows-1)/2,(kernel.cols-1)/2) = kernel.rows*kernel.cols - 1;
    }
    
    cv::filter2D(temp, _probeFiltering, _probeFiltering.depth(), kernel,
                 cv::Point(-1,-1), 0, cv::BORDER_REPLICATE);
    //cv::imshow("ProbeFiltering", _probeFiltering);
    cv::normalize(_probeFiltering, temp, 255.f, 0.f, CV_MINMAX);

    return RESULT_OK;
  }

  Result NearestNeighborLibrary::GetProbeHoG()
  {
    static const s32 gridSize = static_cast<s32>(sqrt(static_cast<f64>(_dataDimension)));
    static cv::Mat_<u8> whichHist(gridSize, gridSize);
    static bool whichHistInitialized = false;
    if(!whichHistInitialized) {
      AnkiAssert(gridSize*gridSize == _dataDimension);
      
      for(s32 y=1; y<=gridSize; ++y) {
        u8* whichHist_y = whichHist[y-1];
        const s32 yi = std::ceil(4*static_cast<f32>(y)/static_cast<f32>(gridSize));
        for(s32 x=1; x<=gridSize; ++x) {
          const s32 xi = std::ceil(4*static_cast<f32>(x)/static_cast<f32>(gridSize));
          const s32 bin = yi + (xi-1)*4;
          AnkiAssert(bin > 0 && bin <= 16);
          whichHist_y[x-1] = bin - 1;
        }
      }
      
      whichHistInitialized = true;
      
      // Visualize whichHist
      //cv::imshow("WhichHist", whichHist*16);
      //cv::waitKey();
    }
    
    const f32 oneOver255 = 1.f / 255.f;
    
    _probeHoG_F32 = 0.f;
    
    for(s32 iScale=0; iScale<_numHogScales; ++iScale) {
      const s32 scale = 1 << iScale;
      
      std::array<f32,16> histSums;
      histSums.fill(0);
      
      for(s32 i=0; i<_probeValues.rows; ++i)
      {
        const u8* probeValues_i = _probeValues[i];
        const u8* probeValues_iUp = _probeValues[std::max(0, i-scale)];
        const u8* probeValues_iDown = _probeValues[std::min(_probeValues.rows-1, i+scale)];
        const u8* whichHist_i = whichHist[i];
        
        for(s32 j=0; j<_probeValues.cols; ++j)
        {
          const s32 jLeft  = std::max(0, j-scale);
          const s32 jRight = std::min(_probeValues.cols-1, j+scale);
          
          const f32 Ix = static_cast<f32>(probeValues_i[jRight] - probeValues_i[jLeft]) * oneOver255;
          const f32 Iy = static_cast<f32>(probeValues_iDown[j] - probeValues_iUp[j]) * oneOver255;
          
          const f32 mag = sqrtf(Ix*Ix + Iy*Iy);
          f32 orient = std::atan2(Iy, Ix);
          
          if(std::abs(-M_PI - orient) < 1e-6f) {
            orient = M_PI;
          }
          
          // From (-pi, pi] to (0, 2pi] and then to (0, 1] and finally to (0, numBins] and 1:numBins
          const f32 scaledOrient = (orient + M_PI)/(2*M_PI) * _numHogOrientations;
          const s32 binnedOrient_R = std::ceil(scaledOrient);
          const f32 weight_L = binnedOrient_R - scaledOrient;
          AnkiAssert(weight_L >= 0.f && weight_L <= 1.f);
          s32 binnedOrient_L = binnedOrient_R - 1;
          if(binnedOrient_L == 0) {
            binnedOrient_L = _numHogOrientations;
          }
          const f32 weight_R = 1.f - weight_L;
          
          const s32 row   = whichHist_i[j];
          const s32 col_L = binnedOrient_L + iScale*_numHogOrientations - 1;
          const s32 col_R = binnedOrient_R + iScale*_numHogOrientations - 1;
          
          AnkiAssert(col_L >= 0 && col_L < _probeHoG.cols);
          AnkiAssert(col_R >= 0 && col_R < _probeHoG.cols);
          
          const f32 leftVal  = mag*weight_L;
          const f32 rightVal = mag*weight_R;
          _probeHoG_F32(row,col_L) += leftVal;
          _probeHoG_F32(row,col_R) += rightVal;
          
          histSums[row] += leftVal + rightVal;
        } // for j
      } // for i
      
      // Normalize each histogram at this scale to sum to one
      for(s32 row=0; row<_probeHoG_F32.rows; ++row) {
        cv::Mat_<f32> H = _probeHoG_F32(cv::Range(row,row+1),
                                        cv::Range(iScale*_numHogOrientations,
                                                  (iScale+1)*_numHogOrientations));
        H /= histSums[row];
      }
      
    } // for scale
    
    _probeHoG_F32.convertTo(_probeHoG, CV_8UC1, 255);
    
    return RESULT_OK;
    
  } // GetProbeHoG()
  
} // namespace Embedded
} // namesapce Anki
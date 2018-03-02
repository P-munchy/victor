/**
 * File: faceTrackerImpl_okao.cpp
 *
 * Author: Andrew Stein
 * Date:   11/30/2015
 *
 * Description: Wrapper for OKAO Vision face detection library.
 *
 * NOTE: This file should only be included by faceTracker.cpp
 *
 * Copyright: Anki, Inc. 2015
 **/

#if FACE_TRACKER_PROVIDER == FACE_TRACKER_OKAO

#include "faceTrackerImpl_okao.h"

#include "coretech/common/engine/math/rect_impl.h"
#include "coretech/common/engine/math/rotation.h"
#include "coretech/common/engine/jsonTools.h"

#include "util/console/consoleInterface.h"
#include "util/helpers/boundedWhile.h"
#include "util/logging/logging.h"
#include "util/random/randomGenerator.h"

namespace Anki {
namespace Vision {
  
  namespace FaceEnrollParams {
    // Faces are not enrollable unless the tracker is above this confidence
    // NOTE: It appears the returned track confidence is set to the fixed value of whatever
    //   the OKAO detection threshold is set to when in default tracking accuracy mode,
    //   so this parameter will have no effect unless the high-accuracy tracker is used
    CONSOLE_VAR(s32, kMinDetectionConfidence,       "Vision.FaceTracker",  500);
    
    CONSOLE_VAR(f32, kCloseDistanceBetweenEyesMin,  "Vision.FaceTracker",  64.f);
    CONSOLE_VAR(f32, kCloseDistanceBetweenEyesMax,  "Vision.FaceTracker",  128.f);
    CONSOLE_VAR(f32, kFarDistanceBetweenEyesMin,    "Vision.FaceTracker",  16.f);
    CONSOLE_VAR(f32, kFarDistanceBetweenEyesMax,    "Vision.FaceTracker",  32.f);
    CONSOLE_VAR(f32, kLookingStraightMaxAngle_deg,  "Vision.FaceTracker",  25.f);
    //CONSOLE_VAR(f32, kLookingLeftRightMinAngle_deg,  "Vision.FaceTracker",  10.f);
    //CONSOLE_VAR(f32, kLookingLeftRightMaxAngle_deg,  "Vision.FaceTracker",  20.f);
    CONSOLE_VAR(f32, kLookingUpMinAngle_deg,        "Vision.FaceTracker",  25.f);
    CONSOLE_VAR(f32, kLookingUpMaxAngle_deg,        "Vision.FaceTracker",  45.f);
    CONSOLE_VAR(f32, kLookingDownMinAngle_deg,      "Vision.FaceTracker", -10.f);
    CONSOLE_VAR(f32, kLookingDownMaxAngle_deg,      "Vision.FaceTracker", -25.f);
    
    // No harm in using fixed seed here (just for shuffling order of processing
    // multiple faces in the same image). It's hard to use CozmoContext's RNG here
    // because this runs on a different thread and has no robot/context.
    static const uint32_t kRandomSeed = 1;
  }
  
  FaceTracker::Impl::Impl(const Camera& camera,
                          const std::string& modelPath,
                          const Json::Value& config)
  : _camera(camera)
  , _recognizer(config)
  , _rng(new Util::RandomGenerator(FaceEnrollParams::kRandomSeed))
  {
    if(config.isMember("FaceDetection")) {
      // TODO: Use string constants
      _config = config["FaceDetection"];
    } else {
      PRINT_NAMED_WARNING("FaceTrackerImpl.Constructor.NoFaceDetectConfig",
                          "Did not find 'FaceDetection' field in config");
    }
    
    Profiler::SetProfileGroupName("FaceTracker.Profiler");
    
    Result initResult = Init();
    if(initResult != RESULT_OK) {
      PRINT_NAMED_ERROR("FaceTrackerImpl.Constructor.InitFailed", "");
    }
    
  } // Impl Constructor()
  
  template<class T>
  static inline bool SetParamHelper(const Json::Value& config, const std::string& keyName, T& value)
  {
    // TODO: Use string constants
    if(JsonTools::GetValueOptional(config, keyName, value)) {
      // TODO: Print value too...
      PRINT_NAMED_INFO("FaceTrackerImpl.SetParamHelper", "%s", keyName.c_str());
      return true;
    } else {
      return false;
    }
  }
  
  Result FaceTracker::Impl::Init()
  {
    _isInitialized = false;
    
    // Get and print Okao library version as a sanity check that we can even
    // talk to the library
    UINT8 okaoVersionMajor=0, okaoVersionMinor = 0;
    INT32 okaoResult = OKAO_CO_GetVersion(&okaoVersionMajor, &okaoVersionMinor);
    if(okaoResult != OKAO_NORMAL) {
      PRINT_NAMED_ERROR("FaceTrackerImpl.Init.FaceLibVersionFail", "");
      return RESULT_FAIL;
    }
    PRINT_NAMED_INFO("FaceTrackerImpl.Init.FaceLibVersion",
                     "Initializing with FaceLibVision version %d.%d",
                     okaoVersionMajor, okaoVersionMinor);
    
    _okaoCommonHandle = OKAO_CO_CreateHandle();
    if(NULL == _okaoCommonHandle) {
      PRINT_NAMED_ERROR("FaceTrackerImpl.Init.FaceLibCommonHandleNull", "");
      return RESULT_FAIL_MEMORY;
    }

    std::string detectionMode = "video";
    if(SetParamHelper(_config, "DetectionMode", detectionMode))
    {
      if(detectionMode == "video")
      {
        _okaoDetectorHandle = OKAO_DT_CreateHandle(_okaoCommonHandle, DETECTION_MODE_MOVIE, MaxFaces);
        if(NULL == _okaoDetectorHandle) {
          PRINT_NAMED_ERROR("FaceTrackerImpl.Init.FaceLibDetectionHandleAllocFail.VideoMode", "");
          return RESULT_FAIL_MEMORY;
        }
        
        // Adjust some detection parameters
        // TODO: Expose these for setting at runtime
        okaoResult = OKAO_DT_MV_SetDelayCount(_okaoDetectorHandle, 1); // have to see faces for more than one frame
        if(OKAO_NORMAL != okaoResult) {
          PRINT_NAMED_ERROR("FaceTrackerImpl.Init.FaceLibSetDelayCountFailed", "");
          return RESULT_FAIL_INVALID_PARAMETER;
        }
        
        okaoResult = OKAO_DT_MV_SetSearchCycle(_okaoDetectorHandle, 2, 2, 5);
        if(OKAO_NORMAL != okaoResult) {
          PRINT_NAMED_ERROR("FaceTrackerImpl.Init.FaceLibSetSearchCycleFailed", "");
          return RESULT_FAIL_INVALID_PARAMETER;
        }

        okaoResult = OKAO_DT_MV_SetDirectionMask(_okaoDetectorHandle, false);
        if(OKAO_NORMAL != okaoResult) {
          PRINT_NAMED_ERROR("FaceTrackerImpl.Init.FaceLibSetDirectionMaskFailed", "");
          return RESULT_FAIL_INVALID_PARAMETER;
        }
        
        okaoResult = OKAO_DT_MV_SetPoseExtension(_okaoDetectorHandle, true, true);
        if(OKAO_NORMAL != okaoResult) {
          PRINT_NAMED_ERROR("FaceTrackerImpl.Init.FaceLibSetPoseExtensionFailed", "");
          return RESULT_FAIL_INVALID_PARAMETER;
        }
        
        okaoResult = OKAO_DT_MV_SetAccuracy(_okaoDetectorHandle, TRACKING_ACCURACY_HIGH);
        if(OKAO_NORMAL != okaoResult) {
          PRINT_NAMED_ERROR("FaceTrackerImpl.Init.FaceLibSetAccuracyFailed", "");
          return RESULT_FAIL_INVALID_PARAMETER;
        }
      }
        
      else if(detectionMode == "singleImage")
      {
        _okaoDetectorHandle = OKAO_DT_CreateHandle(_okaoCommonHandle, DETECTION_MODE_STILL, MaxFaces);
        if(NULL == _okaoDetectorHandle) {
          PRINT_NAMED_ERROR("FaceTrackerImpl.Init.FaceLibDetectionHandleAllocFail.StillMode", "");
          return RESULT_FAIL_MEMORY;
        }
      }
        
      else {
        PRINT_NAMED_ERROR("FaceTrackerImpl.Init.UnknownDetectionMode",
                          "Requested mode = %s", detectionMode.c_str());
        return RESULT_FAIL;
      }
    }
    
    //okaoResult = OKAO_DT_SetAngle(_okaoDetectorHandle, POSE_ANGLE_HALF_PROFILE,
    //                              ROLL_ANGLE_U45 | ROLL_ANGLE_2 | ROLL_ANGLE_10);
    okaoResult = OKAO_DT_SetAngle(_okaoDetectorHandle, POSE_ANGLE_FRONT, ROLL_ANGLE_U45);
    if(OKAO_NORMAL != okaoResult) {
      PRINT_NAMED_ERROR("FaceTrackerImpl.Init.FaceLibSetAngleFailed", "");
      return RESULT_FAIL_INVALID_PARAMETER;
    }
    
    s32 minFaceSize = 48;
    s32 maxFaceSize = 640;
    SetParamHelper(_config, "minFaceSize", minFaceSize);
    SetParamHelper(_config, "maxFaceSize", maxFaceSize);

    okaoResult = OKAO_DT_SetSizeRange(_okaoDetectorHandle, minFaceSize, maxFaceSize);
    if(OKAO_NORMAL != okaoResult) {
      PRINT_NAMED_ERROR("FaceTrackerImpl.Init.FaceLibSetSizeRangeFailed", "");
      return RESULT_FAIL_INVALID_PARAMETER;
    }
    
    s32 detectionThreshold = 500;
    SetParamHelper(_config, "detectionThreshold", detectionThreshold);
    okaoResult = OKAO_DT_SetThreshold(_okaoDetectorHandle, detectionThreshold);
    if(OKAO_NORMAL != okaoResult) {
      PRINT_NAMED_ERROR("FaceTrackerImpl.Init.FaceLibSetThresholdFailed",
                        "FaceLib Result Code=%d", okaoResult);
      return RESULT_FAIL_INVALID_PARAMETER;
    }
    
    _okaoDetectionResultHandle = OKAO_DT_CreateResultHandle(_okaoCommonHandle);
    if(NULL == _okaoDetectionResultHandle) {
      PRINT_NAMED_ERROR("FacetrackerImpl.Init.FaceLibDetectionResultHandleAllocFail", "");
      return RESULT_FAIL_MEMORY;
    }
    
    _okaoPartDetectorHandle = OKAO_PT_CreateHandle(_okaoCommonHandle);
    if(NULL == _okaoPartDetectorHandle) {
      PRINT_NAMED_ERROR("FacetrackerImpl.Init.FaceLibPartDetectorHandleAllocFail", "");
      return RESULT_FAIL_MEMORY;
    }
    
    okaoResult = OKAO_PT_SetConfMode(_okaoPartDetectorHandle, PT_CONF_NOUSE);
    if(OKAO_NORMAL != okaoResult) {
      PRINT_NAMED_ERROR("FacetrakerImpl.Init.FaceLibPartDetectorConfModeFail",
                        "FaceLib Result Code=%d", okaoResult);
      return RESULT_FAIL_INVALID_PARAMETER;
    }
    
    _okaoPartDetectionResultHandle = OKAO_PT_CreateResultHandle(_okaoCommonHandle);
    if(NULL == _okaoPartDetectionResultHandle) {
      PRINT_NAMED_ERROR("FacetrackerImpl.Init.FaceLibPartDetectionResultHandleAllocFail", "");
      return RESULT_FAIL_MEMORY;
    }

    _okaoPartDetectionResultHandle2 = OKAO_PT_CreateResultHandle(_okaoCommonHandle);
    if(NULL == _okaoPartDetectionResultHandle2) {
      PRINT_NAMED_ERROR("FacetrackerImpl.Init.FaceLibPartDetectionResultHandle2AllocFail", "");
      return RESULT_FAIL_MEMORY;
    }
    
    _okaoEstimateExpressionHandle = OKAO_EX_CreateHandle(_okaoCommonHandle);
    if(NULL == _okaoEstimateExpressionHandle) {
      PRINT_NAMED_ERROR("FaceTrackerImpl.Init.FaceLibEstimateExpressionHandleAllocFail", "");
      return RESULT_FAIL_MEMORY;
    }
    
    _okaoExpressionResultHandle = OKAO_EX_CreateResultHandle(_okaoCommonHandle);
    if(NULL == _okaoExpressionResultHandle) {
      PRINT_NAMED_ERROR("FaceTrackerImpl.Init.FaceLibExpressionResultHandleAllocFail", "");
      return RESULT_FAIL_MEMORY;
    }

    _okaoSmileDetectHandle = OKAO_SM_CreateHandle();
    if(NULL == _okaoSmileDetectHandle) {
      PRINT_NAMED_ERROR("FaceTrackerImpl.Init.FaceLibSmileDetectionHandleAllocFail", "");
      return RESULT_FAIL_MEMORY;
    }
    
    _okaoSmileResultHandle = OKAO_SM_CreateResultHandle();
    if(NULL == _okaoSmileResultHandle) {
      PRINT_NAMED_ERROR("FaceTrackerImpl.Init.FaceLibSmileResultHandleAllocFail", "");
      return RESULT_FAIL_MEMORY;
    }
    
    _okaoGazeBlinkDetectHandle = OKAO_GB_CreateHandle();
    if(NULL == _okaoGazeBlinkDetectHandle) {
      PRINT_NAMED_ERROR("FaceTrackerImpl.Init.FaceLibGazeBlinkDetectionHandleAllocFail", "");
      return RESULT_FAIL_MEMORY;
    }
    
    _okaoGazeBlinkResultHandle = OKAO_GB_CreateResultHandle();
    if(NULL == _okaoGazeBlinkResultHandle) {
      PRINT_NAMED_ERROR("FaceTrackerImpl.Init.FaceLibGazeBlinkResultHandleAllocFail", "");
      return RESULT_FAIL_MEMORY;
    }
    
    Result recognizerInitResult = _recognizer.Init(_okaoCommonHandle);
    
    if(RESULT_OK == recognizerInitResult) {
      
      _isInitialized = true;
      
      PRINT_NAMED_INFO("FaceTrackerImpl.Init.Success",
                       "FaceLib Vision handles created successfully.");
    }
    
    return recognizerInitResult;
        
  } // Init()
  
  
  FaceTracker::Impl::~Impl()
  {
    //Util::SafeDeleteArray(_workingMemory);
    //Util::SafeDeleteArray(_backupMemory);

    // Must release album handles before common handle
    _recognizer.Shutdown();

    if(NULL != _okaoSmileDetectHandle) {
      if(OKAO_NORMAL != OKAO_SM_DeleteHandle(_okaoSmileDetectHandle)) {
        PRINT_NAMED_ERROR("FaceTrackerImpl.Destructor.FaceLibSmileDetectHandleDeleteFail", "");
      }
    }
    
    if(NULL != _okaoSmileResultHandle) {
      if(OKAO_NORMAL != OKAO_SM_DeleteResultHandle(_okaoSmileResultHandle)) {
        PRINT_NAMED_ERROR("FaceTrackerImpl.Destructor.FaceLibSmileResultHandleDeleteFail", "");
      }
    }
    
    if(NULL != _okaoGazeBlinkDetectHandle) {
      if(OKAO_NORMAL != OKAO_GB_DeleteHandle(_okaoGazeBlinkDetectHandle)) {
        PRINT_NAMED_ERROR("FaceTrackerImpl.Destructor.FaceLibGazeBlinkDetectHandleDeleteFail", "");
      }
    }
    
    if(NULL != _okaoGazeBlinkResultHandle) {
      if(OKAO_NORMAL != OKAO_GB_DeleteResultHandle(_okaoGazeBlinkResultHandle)) {
        PRINT_NAMED_ERROR("FaceTrackerImpl.Destructor.FaceLibGazeBlinkResulttHandleDeleteFail", "");
      }
    }
    
    if(NULL != _okaoExpressionResultHandle) {
      if(OKAO_NORMAL != OKAO_EX_DeleteResultHandle(_okaoExpressionResultHandle)) {
        PRINT_NAMED_ERROR("FaceTrackerImpl.Destructor.FaceLibExpressionResultHandleDeleteFail", "");
      }
    }
    
    if(NULL != _okaoEstimateExpressionHandle) {
      if(OKAO_NORMAL != OKAO_EX_DeleteHandle(_okaoEstimateExpressionHandle)) {
        PRINT_NAMED_ERROR("FaceTrackerImpl.Destructor.FaceLibEstimateExpressionHandleDeleteFail", "");
      }
    }
    
    if(NULL != _okaoPartDetectionResultHandle) {
      if(OKAO_NORMAL != OKAO_PT_DeleteResultHandle(_okaoPartDetectionResultHandle)) {
        PRINT_NAMED_ERROR("FaceTrackerImpl.Destructor.FaceLibPartDetectionResultHandle1DeleteFail", "");
      }
    }
    
    if(NULL != _okaoPartDetectionResultHandle2) {
      if(OKAO_NORMAL != OKAO_PT_DeleteResultHandle(_okaoPartDetectionResultHandle2)) {
        PRINT_NAMED_ERROR("FaceTrackerImpl.Destructor.FaceLibPartDetectionResultHandle2DeleteFail", "");
      }
    }
    
    if(NULL != _okaoPartDetectorHandle) {
      if(OKAO_NORMAL != OKAO_PT_DeleteHandle(_okaoPartDetectorHandle)) {
        PRINT_NAMED_ERROR("FaceTrackerImpl.Destructor.FaceLibPartDetectorHandleDeleteFail", "");
      }
    }

    if(NULL != _okaoDetectionResultHandle) {
      if(OKAO_NORMAL != OKAO_DT_DeleteResultHandle(_okaoDetectionResultHandle)) {
        PRINT_NAMED_ERROR("FaceTrackerImpl.Destructor.FaceLibDetectionResultHandleDeleteFail", "");
      }
    }
    
    if(NULL != _okaoDetectorHandle) {
      if(OKAO_NORMAL != OKAO_DT_DeleteHandle(_okaoDetectorHandle)) {
        PRINT_NAMED_ERROR("FaceTrackerImpl.Destructor.FaceLibDetectorHandleDeleteFail", "");
      }
      _okaoDetectorHandle = NULL;
    }
    
    if(NULL != _okaoCommonHandle) {
      if(OKAO_NORMAL != OKAO_CO_DeleteHandle(_okaoCommonHandle)) {
        PRINT_NAMED_ERROR("FaceTrackerImpl.Destructor.FaceLibCommonHandleDeleteFail", "");
      }
      _okaoCommonHandle = NULL;
    }
    
    _isInitialized = false;
  } // ~Impl()
  
  void FaceTracker::Impl::Reset()
  {
    INT32 result = OKAO_DT_MV_ResetTracking(_okaoDetectorHandle);
    if(OKAO_NORMAL != result)
    {
      PRINT_NAMED_WARNING("FaceTrackerImpl.Reset.FaceLibResetFailure",
                          "FaceLib result=%d", result);
    }
    
    _recognizer.ClearAllTrackingData();
  }
  
  void FaceTracker::Impl::SetRecognitionIsSynchronous(bool isSynchronous)
  {
    _recognizer.SetIsSynchronous(isSynchronous);
  }
  
  static inline void SetFeatureHelper(const POINT* faceParts, std::vector<s32>&& indices,
                                      TrackedFace::FeatureName whichFeature,
                                      TrackedFace& face)
  {
    TrackedFace::Feature feature;
    bool allPointsPresent = true;
    for(auto index : indices) {
      if(faceParts[index].x == FEATURE_NO_POINT ||
         faceParts[index].y == FEATURE_NO_POINT)
      {
        allPointsPresent = false;
        break;
      }
      feature.emplace_back(faceParts[index].x, faceParts[index].y);
    }
    
    if(allPointsPresent) {
      face.SetFeature(whichFeature, std::move(feature));
    }
  } // SetFeatureHelper()
  
  
  bool FaceTracker::Impl::DetectFaceParts(INT32 nWidth, INT32 nHeight, RAWIMAGE* dataPtr,
                                          INT32 detectionIndex,
                                          Vision::TrackedFace& face)
  {
    INT32 okaoResult = OKAO_PT_SetPositionFromHandle(_okaoPartDetectorHandle, _okaoDetectionResultHandle, detectionIndex);
    
    if(OKAO_NORMAL != okaoResult) {
      PRINT_NAMED_WARNING("FaceTrackerImpl.Update.FaceLibSetPositionFail",
                          "FaceLib Result Code=%d", okaoResult);
      return false;
    }
    okaoResult = OKAO_PT_DetectPoint_GRAY(_okaoPartDetectorHandle, dataPtr,
                                          nWidth, nHeight, GRAY_ORDER_Y0Y1Y2Y3, _okaoPartDetectionResultHandle);
    
    if(OKAO_NORMAL != okaoResult) {
      if(OKAO_ERR_PROCESSCONDITION != okaoResult) {
        PRINT_NAMED_WARNING("FaceTrackerImpl.Update.FaceLibPartDetectionFail",
                            "FaceLib Result Code=%d", okaoResult);
      }
      return false;
    }
    
    okaoResult = OKAO_PT_GetResult(_okaoPartDetectionResultHandle, PT_POINT_KIND_MAX,
                                   _facialParts, _facialPartConfs);
    if(OKAO_NORMAL != okaoResult) {
      PRINT_NAMED_WARNING("FaceTrackerImpl.Update.FaceLibGetFacePartResultFail",
                          "FaceLib Result Code=%d", okaoResult);
      return false;
    }
    
    // Set eye centers
    face.SetEyeCenters(Point2f(_facialParts[PT_POINT_LEFT_EYE].x,
                               _facialParts[PT_POINT_LEFT_EYE].y),
                       Point2f(_facialParts[PT_POINT_RIGHT_EYE].x,
                               _facialParts[PT_POINT_RIGHT_EYE].y));
    
    // Set other facial features
    SetFeatureHelper(_facialParts, {
      PT_POINT_LEFT_EYE_OUT, PT_POINT_LEFT_EYE, PT_POINT_LEFT_EYE_IN
    }, TrackedFace::FeatureName::LeftEye, face);
    
    SetFeatureHelper(_facialParts, {
      PT_POINT_RIGHT_EYE_IN, PT_POINT_RIGHT_EYE, PT_POINT_RIGHT_EYE_OUT
    }, TrackedFace::FeatureName::RightEye, face);
    
    SetFeatureHelper(_facialParts, {
      PT_POINT_NOSE_LEFT, PT_POINT_NOSE_RIGHT
    }, TrackedFace::FeatureName::Nose, face);
    
    SetFeatureHelper(_facialParts, {
      PT_POINT_MOUTH_LEFT, PT_POINT_MOUTH_UP, PT_POINT_MOUTH_RIGHT,
      PT_POINT_MOUTH, PT_POINT_MOUTH_LEFT,
    }, TrackedFace::FeatureName::UpperLip, face);
    
    
    // Fill in head orientation
    INT32 roll_deg=0, pitch_deg=0, yaw_deg=0;
    okaoResult = OKAO_PT_GetFaceDirection(_okaoPartDetectionResultHandle, &pitch_deg, &yaw_deg, &roll_deg);
    if(OKAO_NORMAL != okaoResult) {
      PRINT_NAMED_WARNING("FaceTrackerImpl.Update.FaceLibGetFaceDirectionFail",
                          "FaceLib Result Code=%d", okaoResult);
      return RESULT_FAIL;
    }
    
    //PRINT_NAMED_INFO("FaceTrackerImpl.Update.HeadOrientation",
    //                 "Roll=%ddeg, Pitch=%ddeg, Yaw=%ddeg",
    //                 roll_deg, pitch_deg, yaw_deg);
    
    face.SetHeadOrientation(DEG_TO_RAD(roll_deg),
                            DEG_TO_RAD(pitch_deg),
                            DEG_TO_RAD(yaw_deg));
    
    if(std::abs(roll_deg)  <= FaceEnrollParams::kLookingStraightMaxAngle_deg &&
       std::abs(pitch_deg) <= FaceEnrollParams::kLookingStraightMaxAngle_deg &&
       std::abs(yaw_deg)   <= FaceEnrollParams::kLookingStraightMaxAngle_deg)
    {
      face.SetIsFacingCamera(true);
    }
    else
    {
      face.SetIsFacingCamera(false);
    }
    
    return true;
  }
  
  Result FaceTracker::Impl::EstimateExpression(INT32 nWidth, INT32 nHeight, RAWIMAGE* dataPtr,
                                               Vision::TrackedFace& face)
  {
    INT32 okaoResult = OKAO_EX_SetPointFromHandle(_okaoEstimateExpressionHandle, _okaoPartDetectionResultHandle);
    if(OKAO_NORMAL != okaoResult) {
      PRINT_NAMED_WARNING("FaceTrackerImpl.Update.FaceLibSetExpressionPointFail",
                          "FaceLib Result Code=%d", okaoResult);
      return RESULT_FAIL;
    }
    
    okaoResult = OKAO_EX_Estimate_GRAY(_okaoEstimateExpressionHandle, dataPtr, nWidth, nHeight,
                                       GRAY_ORDER_Y0Y1Y2Y3, _okaoExpressionResultHandle);
    if(OKAO_NORMAL != okaoResult) {
      if(OKAO_ERR_PROCESSCONDITION == okaoResult) {
        // This might happen, depending on face parts
        PRINT_NAMED_INFO("FaceTrackerImpl.Update.FaceLibEstimateExpressionNotPossible", "");
      } else {
        // This should not happen
        PRINT_NAMED_WARNING("FaceTrackerImpl.Update.FaceLibEstimateExpressionFail",
                            "FaceLib Result Code=%d", okaoResult);
        return RESULT_FAIL;
      }
    } else {
      
      okaoResult = OKAO_EX_GetResult(_okaoExpressionResultHandle, EX_EXPRESSION_KIND_MAX, _expressionValues);
      if(OKAO_NORMAL != okaoResult) {
        PRINT_NAMED_WARNING("FaceTrackerImpl.Update.FaceLibGetExpressionResultFail",
                            "FaceLib Result Code=%d", okaoResult);
        return RESULT_FAIL;
      }
      
      static const FacialExpression TrackedFaceExpressionLUT[EX_EXPRESSION_KIND_MAX] = {
        FacialExpression::Neutral,
        FacialExpression::Happiness,
        FacialExpression::Surprise,
        FacialExpression::Anger,
        FacialExpression::Sadness
      };
      
      for(INT32 okaoExpressionVal = 0; okaoExpressionVal < EX_EXPRESSION_KIND_MAX; ++okaoExpressionVal) {
        face.SetExpressionValue(TrackedFaceExpressionLUT[okaoExpressionVal],
                                _expressionValues[okaoExpressionVal]);
      }
      
    }

    return RESULT_OK;
  } // EstimateExpression()
  
  Result FaceTracker::Impl::DetectSmile(INT32 nWidth, INT32 nHeight, RAWIMAGE* dataPtr,
                                        Vision::TrackedFace& face)
  {
    INT32 okaoResult = OKAO_SM_SetPointFromHandle(_okaoSmileDetectHandle, _okaoPartDetectionResultHandle);
    if(OKAO_NORMAL != okaoResult) {
      PRINT_NAMED_WARNING("FaceTrackerImpl.DetectSmile.SetPointFromHandleFailed",
                          "FaceLib Result=%d", okaoResult);
      return RESULT_FAIL;
    }
    
    okaoResult = OKAO_SM_Estimate(_okaoSmileDetectHandle, dataPtr, nWidth, nHeight, _okaoSmileResultHandle);
    if(OKAO_NORMAL != okaoResult) {
      PRINT_NAMED_WARNING("FaceTrackerImpl.DetectSmile.EstimateFailed",
                          "FaceLib Result=%d", okaoResult);
      return RESULT_FAIL;
    }
    
    INT32 smileDegree=0;
    INT32 confidence=0;
    okaoResult = OKAO_SM_GetResult(_okaoSmileResultHandle, &smileDegree, &confidence);
    if(OKAO_NORMAL != okaoResult) {
      PRINT_NAMED_WARNING("FaceTrackerImpl.DetectSmile.GetResultFailed",
                          "FaceLib Result=%d", okaoResult);
      return RESULT_FAIL;
    }
    
    // NOTE: smileDegree from OKAO is [0,100]. Convert to [0.0, 1.0].
    // Confidence from OKAO is [0,1000]. Also convert to [0.0, 1.0]
    face.SetSmileAmount(static_cast<f32>(smileDegree) * 0.01f, static_cast<f32>(confidence) * 0.001f);
    
    return RESULT_OK;
  }
  
  Result FaceTracker::Impl::DetectGazeAndBlink(INT32 nWidth, INT32 nHeight, RAWIMAGE* dataPtr,
                                               Vision::TrackedFace& face)
  {
    INT32 okaoResult = OKAO_GB_SetPointFromHandle(_okaoGazeBlinkDetectHandle, _okaoPartDetectionResultHandle);
    if(OKAO_NORMAL != okaoResult) {
      PRINT_NAMED_WARNING("FaceTrackerImpl.DetectGazeAndBlink.SetPointFromHandleFailed",
                          "FaceLib Result=%d", okaoResult);
      return RESULT_FAIL;
    }
    
    okaoResult = OKAO_GB_Estimate(_okaoGazeBlinkDetectHandle, dataPtr, nWidth, nHeight, _okaoGazeBlinkResultHandle);
    if(OKAO_NORMAL != okaoResult) {
      PRINT_NAMED_WARNING("FaceTrackerImpl.DetectGazeAndBlink.EstimateFailed",
                          "FaceLib Result=%d", okaoResult);
      return RESULT_FAIL;
    }
    
    if(_detectGaze)
    {
      INT32 gazeLeftRight_deg = 0;
      INT32 gazeUpDown_deg    = 0;
      okaoResult = OKAO_GB_GetGazeDirection(_okaoGazeBlinkResultHandle, &gazeLeftRight_deg, &gazeUpDown_deg);
      if(OKAO_NORMAL != okaoResult) {
        PRINT_NAMED_WARNING("FaceTrackerImpl.DetectGazeAndBlink.GetGazeDirectionFailed",
                            "FaceLib Result=%d", okaoResult);
        return RESULT_FAIL;
      }
    
      face.SetGaze(gazeLeftRight_deg, gazeUpDown_deg);
    }
    
    if(_detectBlinks)
    {
      INT32 blinkDegreeLeft  = 0;
      INT32 blinkDegreeRight = 0;
      okaoResult = OKAO_GB_GetEyeCloseRatio(_okaoGazeBlinkResultHandle, &blinkDegreeLeft, &blinkDegreeRight);
      if(OKAO_NORMAL != okaoResult) {
        PRINT_NAMED_WARNING("FaceTrackerImpl.DetectGazeAndBlink.GetEyeCloseRatioFailed",
                            "FaceLib Result=%d", okaoResult);
        return RESULT_FAIL;
      }
      
      // NOTE: blinkDegree from OKAO is [0,1000]. Convert to [0.0, 1.0]
      face.SetBlinkAmount(static_cast<f32>(blinkDegreeLeft) * 0.001f, static_cast<f32>(blinkDegreeRight) * 0.001f);
    }
    
    return RESULT_OK;
  }

  bool FaceTracker::Impl::DetectEyeContact(const TrackedFace& face,
                                           const TimeStamp_t& timeStamp)
  {
    DEV_ASSERT(face.IsTranslationSet(), "FaceTrackerImpl.DetectEyeContact.FaceTranslationNotSet");
    auto& entry = _facesEyeContact[face.GetID()];
    entry.Update(face, timeStamp);

    // Check if the face is stale
    bool eyeContact = false;
    if (entry.GetExpired(timeStamp))
    {
      _facesEyeContact.erase(face.GetID());
    }
    else
    {
      eyeContact = entry.IsMakingEyeContact();
    }
    return eyeContact;
  }
  
  Result FaceTracker::Impl::Update(const Vision::Image& frameOrig,
                                   std::list<TrackedFace>& faces,
                                   std::list<UpdatedFaceID>& updatedIDs)
  {
    if(!_isInitialized) {
      PRINT_NAMED_ERROR("FaceTrackerImpl.Update.NotInitialized", "");
      return RESULT_FAIL;
    }
    
    DEV_ASSERT(frameOrig.IsContinuous(), "FaceTrackerImpl.Update.NonContinuousImage");
    
    INT32 okaoResult = OKAO_NORMAL;
    //TIC;
    Tic("FaceDetect");
    const INT32 nWidth  = frameOrig.GetNumCols();
    const INT32 nHeight = frameOrig.GetNumRows();
    RAWIMAGE* dataPtr = const_cast<UINT8*>(frameOrig.GetDataPointer());
    okaoResult = OKAO_DT_Detect_GRAY(_okaoDetectorHandle, dataPtr, nWidth, nHeight,
                                     GRAY_ORDER_Y0Y1Y2Y3, _okaoDetectionResultHandle);
    if(OKAO_NORMAL != okaoResult) {
      PRINT_NAMED_WARNING("FaceTrackerImpl.Update.FaceLibDetectFail",
                          "FaceLib Result Code=%d", okaoResult);
      return RESULT_FAIL;
    }
    
    INT32 numDetections = 0;
    okaoResult = OKAO_DT_GetResultCount(_okaoDetectionResultHandle, &numDetections);
    if(OKAO_NORMAL != okaoResult) {
      PRINT_NAMED_WARNING("FaceTrackerImpl.Update.FaceLibGetResultCountFail",
                          "FaceLib Result Code=%d", okaoResult);
      return RESULT_FAIL;
    }
    Toc("FaceDetect");
    
    // If there are multiple faces, figure out which detected faces we already recognize
    // so that we can choose to run recognition more selectively in the loop below,
    // effectively prioritizing those we don't already recognize
    std::vector<INT32> detectionIndices(numDetections);
    std::set<INT32> skipRecognition;
    if(numDetections == 1)
    {
      detectionIndices[0] = 0;
    }
    else if(numDetections > 1)
    {
      for(INT32 detectionIndex=0; detectionIndex<numDetections; ++detectionIndex)
      {
        detectionIndices[detectionIndex] = detectionIndex;
        
        DETECTION_INFO detectionInfo;
        okaoResult = OKAO_DT_GetRawResultInfo(_okaoDetectionResultHandle, detectionIndex,
                                              &detectionInfo);
        
        if(OKAO_NORMAL != okaoResult) {
          PRINT_NAMED_WARNING("FaceTrackerImpl.Update.FaceLibGetResultInfoFail1",
                              "Detection index %d of %d. FaceLib Result Code=%d",
                              detectionIndex, numDetections, okaoResult);
          return RESULT_FAIL;
        }
        
        // Note that we don't consider the face currently being enrolled to be
        // "known" because we're in the process of updating it and want to run
        // recognition on it
        const bool isKnown = _recognizer.HasRecognitionData(detectionInfo.nID);
        if(isKnown && _recognizer.GetEnrollmentTrackID() != detectionInfo.nID)
        {
          skipRecognition.insert(detectionInfo.nID);
        }
      }
      
      // If we know everyone, no need to prioritize anyone, so don't skip anyone
      // and instead just re-recognize all, but in random order
      if(skipRecognition.size() == numDetections)
      {
        skipRecognition.clear();
      }
      
      std::random_shuffle(detectionIndices.begin(), detectionIndices.end(),
                          [this](int i) { return _rng->RandInt(i); });
    }
  
    for(auto const& detectionIndex : detectionIndices)
    {
      DETECTION_INFO detectionInfo;
      okaoResult = OKAO_DT_GetRawResultInfo(_okaoDetectionResultHandle, detectionIndex,
                                            &detectionInfo);
      
      if(OKAO_NORMAL != okaoResult) {
        PRINT_NAMED_WARNING("FaceTrackerImpl.Update.FaceLibGetResultInfoFail2",
                            "Detection index %d of %d. FaceLib Result Code=%d",
                            detectionIndex, numDetections, okaoResult);
        return RESULT_FAIL;
      }
      
      // Add a new face to the list
      faces.emplace_back();
      
      TrackedFace& face = faces.back();

      face.SetIsBeingTracked(detectionInfo.nDetectionMethod != DET_METHOD_DETECTED_HIGH);
 
      POINT ptLeftTop, ptRightTop, ptLeftBottom, ptRightBottom;
      okaoResult = OKAO_CO_ConvertCenterToSquare(detectionInfo.ptCenter,
                                                 detectionInfo.nHeight,
                                                 0, &ptLeftTop, &ptRightTop,
                                                 &ptLeftBottom, &ptRightBottom);
      if(OKAO_NORMAL != okaoResult) {
        PRINT_NAMED_WARNING("FaceTrackerImpl.Update.FaceLibCenterToSquareFail",
                            "Detection index %d of %d. FaceLib Result Code=%d",
                            detectionIndex, numDetections, okaoResult);
        return RESULT_FAIL;
      }
      
      face.SetRect(Rectangle<f32>(ptLeftTop.x, ptLeftTop.y,
                                  ptRightBottom.x-ptLeftTop.x,
                                  ptRightBottom.y-ptLeftTop.y));
      
      face.SetTimeStamp(frameOrig.GetTimestamp());
      
      // Try finding face parts
      Tic("FacePartDetection");
      const bool facePartsFound = DetectFaceParts(nWidth, nHeight, dataPtr, detectionIndex, face);
      Toc("FacePartDetection");
      
      if(facePartsFound)
      {
        if(_detectEmotion)
        {
          // Expression detection
          Tic("ExpressionRecognition");
          Result expResult = EstimateExpression(nWidth, nHeight, dataPtr, face);
          Toc("ExpressionRecognition");
          if(RESULT_OK != expResult) {
            PRINT_NAMED_WARNING("FaceTrackerImpl.Update.EstimateExpressiongFailed",
                                "Detection index %d of %d.",
                                detectionIndex, numDetections);
          }
        } // if(_detectEmotion)
        
        if(_detectSmiling)
        {
          Tic("SmileDetection");
          Result smileResult = DetectSmile(nWidth, nHeight, dataPtr, face);
          Toc("SmileDetection");
          
          if(RESULT_OK != smileResult) {
            PRINT_NAMED_WARNING("FaceTrackerImpl.Update.DetectSmileFailed",
                                "Detection index %d of %d.",
                                detectionIndex, numDetections);
          }
        }
        
        if(_detectGaze || _detectBlinks) // In OKAO, gaze and blink are part of the same detector
        {
          Tic("GazeAndBlinkDetection");
          Result gbResult = DetectGazeAndBlink(nWidth, nHeight, dataPtr, face);
          Toc("GazeAndBlinkDetection");
          
          if(RESULT_OK != gbResult) {
            PRINT_NAMED_WARNING("FaceTrackerImpl.Update.DetectGazeAndBlinkFailed",
                                "Detection index %d of %d.",
                                detectionIndex, numDetections);
          }
        }
        
        //
        // Face Recognition:
        //
        const bool enableEnrollment = IsEnrollable(detectionInfo, face);
        
        // Very Verbose:
        //        PRINT_NAMED_DEBUG("FaceTrackerImpl.Update.IsEnrollable",
        //                          "TrackerID:%d EnableEnrollment:%d",
        //                          -detectionInfo.nID, enableEnrollment);
        
        const bool doRecognition = !(skipRecognition.count(detectionInfo.nID)>0);
        if(doRecognition)
        {
          const bool recognizing = _recognizer.SetNextFaceToRecognize(frameOrig,
                                                                      detectionInfo,
                                                                      _okaoPartDetectionResultHandle,
                                                                      enableEnrollment);
          if(recognizing) {
            // The FaceRecognizer is now using whatever the partDetectionResultHandle is pointing to.
            // Switch to using the other handle so we don't step on its toes.
            std::swap(_okaoPartDetectionResultHandle, _okaoPartDetectionResultHandle2);
          }
        }
        // Very verbose:
        //        else
        //        {
        //          PRINT_NAMED_DEBUG("FaceTrackerImpl.Update.SkipRecognitionForAlreadyKnown",
        //                            "TrackingID %d already known and there are %d faces detected",
        //                            -detectionInfo.nID, numDetections);
        //        }
        
      } // if(facePartsFound)
      
      // Get whatever is the latest recognition information for the current tracker ID
      s32 enrollmentCompleted = 0;
      auto recognitionData = _recognizer.GetRecognitionData(detectionInfo.nID, enrollmentCompleted);
      
      if(recognitionData.WasFaceIDJustUpdated())
      {
        // We either just assigned a recognition ID to a tracker ID or we updated
        // the recognition ID (e.g. due to merging)
        UpdatedFaceID update{
          .oldID   = (recognitionData.GetPreviousFaceID() == UnknownFaceID ?
                      -detectionInfo.nID : recognitionData.GetPreviousFaceID()),
          .newID   = recognitionData.GetFaceID(),
          .newName = recognitionData.GetName()
        };
        
        updatedIDs.push_back(std::move(update));
      }
      
      if(recognitionData.GetFaceID() != UnknownFaceID &&
         recognitionData.GetTrackingID() != recognitionData.GetPreviousTrackingID())
      {
        // We just updated the track ID for a recognized face.
        // So we should notify listeners that tracking ID is now
        // associated with this recognized ID.
        UpdatedFaceID update{
          .oldID   = -recognitionData.GetTrackingID(),
          .newID   = recognitionData.GetFaceID(),
          .newName = recognitionData.GetName()
        };
        
        // Don't send this update if it turns out to contain the same info as
        // the last one (even if for different reasons)
        if(updatedIDs.empty() ||
           (update.oldID != updatedIDs.back().oldID &&
            update.newID != updatedIDs.back().newID))
        {
          updatedIDs.push_back(std::move(update));
        }
      }
      
      face.SetScore(recognitionData.GetScore()); // could still be zero!
      if(UnknownFaceID == recognitionData.GetFaceID()) {
        // No recognition ID: use the tracker ID as the face's handle/ID
        DEV_ASSERT(detectionInfo.nID > 0, "FaceTrackerImpl.Update.InvalidTrackerID");
        face.SetID(-detectionInfo.nID);
      } else {
        face.SetID(recognitionData.GetFaceID());
        face.SetName(recognitionData.GetName()); // Could be empty!
        face.SetNumEnrollments(enrollmentCompleted);
        
        face.SetRecognitionDebugInfo(recognitionData.GetDebugMatchingInfo());
      }

      // Use a camera from the robot's pose history to estimate the head's
      // 3D translation, w.r.t. that camera. Also puts the face's pose in
      // the camera's pose chain. This needs to happen before Detecting
      // Eye Contact, there is a assert in there that should catch it
      // if the pose is uninitialized but won't catch on going cases of the
      // dependence.
      face.UpdateTranslation(_camera);

      if(_detectGaze && facePartsFound)
      {
        face.SetEyeContact(DetectEyeContact(face, frameOrig.GetTimestamp()));
      }
      
    } // FOR each face
    
    return RESULT_OK;
  } // Update()

  Result FaceTracker::Impl::AssignNameToID(FaceID_t faceID, const std::string& name, FaceID_t mergeWithID)
  {
    return _recognizer.AssignNameToID(faceID, name, mergeWithID);
  }

  Result FaceTracker::Impl::EraseFace(FaceID_t faceID)
  {
    return _recognizer.EraseFace(faceID);
  }
  
  void FaceTracker::Impl::EraseAllFaces()
  {
    _recognizer.EraseAllFaces();
  }
  
  Result FaceTracker::Impl::SaveAlbum(const std::string& albumName)
  {
    return _recognizer.SaveAlbum(albumName);
  }
  
  Result FaceTracker::Impl::RenameFace(FaceID_t faceID, const std::string& oldName, const std::string& newName,
                                       Vision::RobotRenamedEnrolledFace& renamedFace)
  {
    return _recognizer.RenameFace(faceID, oldName, newName, renamedFace);
  }
  
  Result FaceTracker::Impl::LoadAlbum(const std::string& albumName, std::list<LoadedKnownFace>& loadedFaces)
  {
    if(!_isInitialized) {
      PRINT_NAMED_ERROR("FaceTrackerImpl.LoadAlbum.NotInitialized", "");
      return RESULT_FAIL;
    }
    
    if(NULL == _okaoCommonHandle) {
      PRINT_NAMED_ERROR("FaceTrackerImpl.LoadAlbum.NullFaceLibCommonHandle", "");
      return RESULT_FAIL;
    }
    
    return _recognizer.LoadAlbum(albumName, loadedFaces);
  }
  
  float FaceTracker::Impl::GetMinEyeDistanceForEnrollment()
  {
    return FaceEnrollParams::kFarDistanceBetweenEyesMin;
  }
  
  void FaceTracker::Impl::SetFaceEnrollmentMode(Vision::FaceEnrollmentPose pose,
                                                Vision::FaceID_t forFaceID,
                                                s32 numEnrollments)
  { 
    _enrollPose = pose;
    _recognizer.SetAllowedEnrollments(numEnrollments, forFaceID);
  }


  bool FaceTracker::Impl::IsEnrollable(const DETECTION_INFO& detectionInfo, const TrackedFace& face)
  {
#   define DEBUG_ENROLLABILITY 0
    
    using namespace FaceEnrollParams;
    
    bool enableEnrollment = false;
    
    if(detectionInfo.nConfidence > kMinDetectionConfidence)
    {
      const f32 d = face.GetIntraEyeDistance();
      
      switch(_enrollPose)
      {
        case FaceEnrollmentPose::LookingStraight:
        {
          if(detectionInfo.nPose == POSE_YAW_FRONT &&
             face.IsFacingCamera() &&
             d >= kFarDistanceBetweenEyesMin)
          {
            enableEnrollment = true;
          }
          else if(DEBUG_ENROLLABILITY) {
            PRINT_NAMED_DEBUG("FaceTrackerImpl.IsEnrollable.NotLookingStraight",
                              "EyeDist=%.1f (vs. %.1f)",
                              d, kFarDistanceBetweenEyesMin);
          }
          break;
        }
          
        case FaceEnrollmentPose::LookingStraightClose:
        {
          // Close enough and not too much head angle
          if(d >= kCloseDistanceBetweenEyesMin &&
             d <= kCloseDistanceBetweenEyesMax &&
             detectionInfo.nPose == POSE_YAW_FRONT &&
             face.IsFacingCamera())
          {
            enableEnrollment = true;
          }
          else if(DEBUG_ENROLLABILITY) {
            PRINT_NAMED_DEBUG("FaceTrackerImpl.IsEnrollable.NotLookingStraightClose",
                              "EyeDist=%.1f [%.1f,%.1f], Roll=%.1f, Pitch%.1f, Yaw=%.1f",
                              d, kCloseDistanceBetweenEyesMin, kCloseDistanceBetweenEyesMax,
                              face.GetHeadRoll().getDegrees(),
                              face.GetHeadPitch().getDegrees(),
                              face.GetHeadYaw().getDegrees());
          }
          break;
        }
          
        case FaceEnrollmentPose::LookingStraightFar:
        {
          // Far enough and not too much head angle
          if(d >= kFarDistanceBetweenEyesMin &&
             d <= kFarDistanceBetweenEyesMax &&
             detectionInfo.nPose == POSE_YAW_FRONT &&
             face.IsFacingCamera())
          {
            enableEnrollment = true;
          }
          else if(DEBUG_ENROLLABILITY) {
            PRINT_NAMED_DEBUG("FaceTrackerImpl.IsEnrollable.NotLookingStraightFar",
                              "EyeDist=%.1f [%.1f,%.1f], Roll=%.1f, Pitch%.1f, Yaw=%.1f",
                              d, kFarDistanceBetweenEyesMin, kFarDistanceBetweenEyesMax,
                              face.GetHeadRoll().getDegrees(),
                              face.GetHeadPitch().getDegrees(),
                              face.GetHeadYaw().getDegrees());
          }
          break;
        }
          
        case FaceEnrollmentPose::LookingLeft:
        {
          // Looking left enough, but not too much. "No" pitch/roll.
          if(detectionInfo.nPose == POSE_YAW_LH_PROFILE /*
             std::abs(face.GetHeadRoll().getDegrees())  <= kLookingStraightMaxAngle_deg &&
             std::abs(face.GetHeadPitch().getDegrees()) <= kLookingStraightMaxAngle_deg &&
             face.GetHeadYaw().getDegrees() >= kLookingLeftRightMinAngle_deg &&
             face.GetHeadYaw().getDegrees() <= kLookingLeftRightMaxAngle_deg*/)
          {
            enableEnrollment = true;
          }
          else if(DEBUG_ENROLLABILITY) {
            PRINT_NAMED_DEBUG("FaceTrackerImpl.IsEnrollable.NotLookingLeft",
                              "Roll=%.1f, Pitch%.1f, Yaw=%.1f",
                              face.GetHeadRoll().getDegrees(),
                              face.GetHeadPitch().getDegrees(),
                              face.GetHeadYaw().getDegrees());
          }
          break;
        }
          
        case FaceEnrollmentPose::LookingRight:
        {
          // Looking right enough, but not too much. "No" pitch/roll.
          if(detectionInfo.nPose == POSE_YAW_RH_PROFILE /*
             std::abs(face.GetHeadRoll().getDegrees())  <= kLookingStraightMaxAngle_deg &&
             std::abs(face.GetHeadPitch().getDegrees()) <= kLookingStraightMaxAngle_deg &&
             face.GetHeadYaw().getDegrees() <= -kLookingLeftRightMinAngle_deg &&
             face.GetHeadYaw().getDegrees() >= -kLookingLeftRightMaxAngle_deg*/)
          {
            enableEnrollment = true;
          }
          else if(DEBUG_ENROLLABILITY) {
            PRINT_NAMED_DEBUG("FaceTrackerImpl.IsEnrollable.NotLookingRight",
                              "Roll=%.1f, Pitch%.1f, Yaw=%.1f",
                              face.GetHeadRoll().getDegrees(),
                              face.GetHeadPitch().getDegrees(),
                              face.GetHeadYaw().getDegrees());
          }
          break;
        }
          
        case FaceEnrollmentPose::LookingUp:
        {
          // Looking up enough, but not too much. "No" pitch/roll.
          if(detectionInfo.nPose == POSE_YAW_FRONT && d >= kFarDistanceBetweenEyesMax &&
             //std::abs(face.GetHeadRoll().getDegrees())  <= kLookingStraightMaxAngle_deg &&
             //std::abs(face.GetHeadYaw().getDegrees()) <= kLookingStraightMaxAngle_deg &&
             face.GetHeadPitch().getDegrees() >= kLookingUpMinAngle_deg &&
             face.GetHeadPitch().getDegrees() <= kLookingUpMaxAngle_deg)
          {
            enableEnrollment = true;
          }
          else if(DEBUG_ENROLLABILITY) {
            PRINT_NAMED_DEBUG("FaceTrackerImpl.IsEnrollable.NotLookingUp",
                              "Roll=%.1f, Pitch%.1f, Yaw=%.1f",
                              face.GetHeadRoll().getDegrees(),
                              face.GetHeadPitch().getDegrees(),
                              face.GetHeadYaw().getDegrees());
          }
          break;
        }
          
        case FaceEnrollmentPose::LookingDown:
        {
          // Looking up enough, but not too much. "No" pitch/roll.
          if(detectionInfo.nPose == POSE_YAW_FRONT && d >= kFarDistanceBetweenEyesMax &&
             //std::abs(face.GetHeadRoll().getDegrees())  <= kLookingStraightMaxAngle_deg &&
             //std::abs(face.GetHeadYaw().getDegrees()) <= kLookingStraightMaxAngle_deg &&
             face.GetHeadPitch().getDegrees() <= -kLookingDownMinAngle_deg &&
             face.GetHeadPitch().getDegrees() >= -kLookingDownMaxAngle_deg)
          {
            enableEnrollment = true;
          }
          else if(DEBUG_ENROLLABILITY) {
            PRINT_NAMED_DEBUG("FaceTrackerImpl.IsEnrollable.NotLookingDown",
                              "Roll=%.1f, Pitch%.1f, Yaw=%.1f",
                              face.GetHeadRoll().getDegrees(),
                              face.GetHeadPitch().getDegrees(),
                              face.GetHeadYaw().getDegrees());
          }
          break;
        }
        
        case FaceEnrollmentPose::Disabled:
          break;
          
      } // switch(_enrollPose)
    } // if detectionConfidence high enough

    if(DEBUG_ENROLLABILITY && enableEnrollment) {
      PRINT_NAMED_DEBUG("FaceTrackerImpl.IsEnrollable", "Mode=%d", (u8)_enrollPose);
    }
    
    return enableEnrollment;
    
  } // IsEnrollable()
  
  Result FaceTracker::Impl::GetSerializedData(std::vector<u8>& albumData,
                                              std::vector<u8>& enrollData)
  {
    return _recognizer.GetSerializedData(albumData, enrollData);
  }
  
  Result FaceTracker::Impl::SetSerializedData(const std::vector<u8>& albumData,
                                              const std::vector<u8>& enrollData,
                                              std::list<LoadedKnownFace>& loadedFaces)
  {
    return _recognizer.SetSerializedData(albumData, enrollData, loadedFaces);
  }

  
} // namespace Vision
} // namespace Anki

#endif // #if FACE_TRACKER_PROVIDER == FACE_TRACKER_OKAO


/**
 * File: animationStreamer.cpp
 *
 * Authors: Andrew Stein
 * Created: 2015-06-25
 *
 * Description: 
 * 
 *   Handles streaming a given animation from a CannedAnimationContainer
 *   to a robot.
 *
 * Copyright: Anki, Inc. 2015
 *
 **/

#include "coretech/common/engine/array2d_impl.h"
#include "coretech/common/engine/utils/timer.h"
#include "coretech/common/engine/utils/data/dataPlatform.h"
#include "cozmoAnim/animation/animationStreamer.h"
#include "coretech/vision/shared/compositeImage/compositeImageBuilder.h"
//#include "cozmoAnim/animation/trackLayerManagers/faceLayerManager.h"

#include "cannedAnimLib/proceduralFace/proceduralFaceDrawer.h"
#include "cannedAnimLib/spriteSequences/spriteSequence.h"
#include "cannedAnimLib/spriteSequences/spriteSequenceContainer.h"

#include "cozmoAnim/animation/trackLayerComponent.h"
#include "cozmoAnim/audio/animationAudioClient.h"
#include "cozmoAnim/audio/proceduralAudioClient.h"
#include "cozmoAnim/faceDisplay/faceDisplay.h"
#include "cozmoAnim/faceDisplay/faceInfoScreenManager.h"
#include "cozmoAnim/animContext.h"
#include "cozmoAnim/animProcessMessages.h"
#include "cozmoAnim/robotDataLoader.h"
#include "anki/cozmo/shared/cozmoConfig.h"
#include "clad/types/animationTypes.h"
#include "osState/osState.h"
#include "util/console/consoleInterface.h"
#include "util/cpuProfiler/cpuProfiler.h"
#include "util/fileUtils/fileUtils.h"
#include "util/helpers/templateHelpers.h"
#include "util/logging/logging.h"
#include "util/time/universalTime.h"
#include "webServerProcess/src/webService.h"

#include "clad/robotInterface/messageRobotToEngine.h"
#include "clad/robotInterface/messageEngineToRobot.h"
#include "clad/robotInterface/messageRobotToEngine_sendAnimToEngine_helper.h"
#include "clad/robotInterface/messageEngineToRobot_sendAnimToRobot_helper.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include "jo_gif/jo_gif.h"

#include <stdio.h>
#include <time.h>

#define DEBUG_ANIMATION_STREAMING 0
#define DEBUG_ANIMATION_STREAMING_AUDIO 0

namespace Anki {
namespace Cozmo {

  enum class FaceDisplayType {
    Normal,
    Test,
    OverrideIndividually, // each eyes parameters operate on their respective eye
    OverrideTogether      // left eye parameters drive both left and right eyes
  };

  // Overrides whatever faces we're sending with a 3-stripe test pattern
  // (seems more related to the other ProceduralFace console vars, so putting it in that group instead)
  CONSOLE_VAR_ENUM(int, kProcFace_Display,             "ProceduralFace", 0, "Normal,Test,Override individually,Override together"); // Override procedural face with ConsoleVars edited version
#if PROCEDURALFACE_NOISE_FEATURE
  CONSOLE_VAR_EXTERN(s32, kProcFace_NoiseNumFrames);
#endif
  CONSOLE_VAR_ENUM(int, kProcFace_GammaType,            "ProceduralFace", 0, "None,FromLinear,ToLinear,AddGamma,RemoveGamma,Custom");
  CONSOLE_VAR_RANGED(f32, kProcFace_Gamma,              "ProceduralFace", 1.f, 1.f, 4.f);

  enum class FaceGammaType {
    None,
    FromLinear,
    ToLinear,
    AddGamma,    // Use value of kProcFace_Gamma
    RemoveGamma, // Use value of kProcFace_Gamma
    Custom
  };

  static ProceduralFace s_faceDataOverride; // incoming values from console var system
  static ProceduralFace s_faceDataBaseline; // baseline to compare against, differences mean override the incoming animation
  static const AnimContext* s_context; // copy of AnimContext in first constructed AnimationStreamer, needed for GetDataPlatform
  static bool s_faceDataOverrideRegistered = false;
  static bool s_faceDataReset = false;
  static uint8_t s_gammaLUT[3][256];// RGB x 256 entries

  void ResetFace(ConsoleFunctionContextRef context)
  {
    s_faceDataReset = true;
  }
  
  CONSOLE_FUNC(ResetFace, "ProceduralFace");

  static void LoadFaceGammaLUT(ConsoleFunctionContextRef context)
  {
    const std::string filename = ConsoleArg_GetOptional_String(context, "filename", "screenshot.tga");

    const Util::Data::DataPlatform* dataPlatform = s_context->GetDataPlatform();
    const std::string cacheFilename = dataPlatform->pathToResource(Util::Data::Scope::Cache, filename);

    Vision::Image tga;
    Result result = tga.Load(cacheFilename);
    if(result == RESULT_OK) {
      const int width = tga.GetNumCols();
      const int height = tga.GetNumRows();
      const int channels = tga.GetNumChannels();
      if (width != 256 || height != 1) {
        const std::string html = std::string("<html>\n") +filename+" must be either a 256x1 image file\n" + "</html>\n";
        context->channel->WriteLog(html.c_str());
      } else {
        for(int channel = 0; channel < 3; ++channel) {
          // greyscale: offset = 0 always, RGB and RGBA: offset is channel, A is ignored
          uint8_t* srcPtr = tga.GetRawDataPointer() + (channel % channels);
          for(int x = 0; x < width; ++x) {
            s_gammaLUT[channel][x] = *srcPtr;
            srcPtr += channels;
          }
        }

        kProcFace_GammaType = (int)FaceGammaType::Custom;
      }
    } else {
      // see https://ankiinc.atlassian.net/browse/VIC-1646 to productize .tga loading
      std::vector<uint8_t> tga = Anki::Util::FileUtils::ReadFileAsBinary(cacheFilename);
      if(tga.size() < 18) {
        const std::string html = std::string("<html>\n") +filename+" is not a .tga file\n" + "</html>\n";
        context->channel->WriteLog(html.c_str());
      } else {
        const int width = tga[12]+tga[13]*256;
        const int height = tga[14]+tga[15]*256;
        const int bytesPerPixel = tga[16] / 8;
        if(tga[2] != 2 && tga[2] != 3) {
          const std::string html = std::string("<html>\n") +filename+" is not an uncompressed, true-color or grayscale .tga file\n" + "</html>\n";
          context->channel->WriteLog(html.c_str());
        } else if (width != 256 || height != 1) {
          const std::string html = std::string("<html>\n") +filename+" must be a 256x1 .tga file\n" + "</html>\n";
          context->channel->WriteLog(html.c_str());
        } else {
          for(int channel = 0; channel < 3; ++channel) {
            // greyscale: offset = 0 always, RGB and RGBA: offset is channel, A is ignored
            uint8_t* srcPtr = &tga[18 + (channel % bytesPerPixel)];
            for(int x = 0; x < width; ++x) {
              s_gammaLUT[channel][x] = *srcPtr;
              srcPtr += bytesPerPixel;
            }
          }

          kProcFace_GammaType = (int)FaceGammaType::Custom;
        }
      }
    }
  }

  CONSOLE_FUNC(LoadFaceGammaLUT, "ProceduralFace", const char* filename);

  namespace{
    
  const char* kLogChannelName = "Animations";

  // Specifies how often to send AnimState message
  static const u32 kAnimStateReportingPeriod_tics = 2;

  // Minimum amount of time that must expire after the last non-procedural face
  // is drawn and the next procedural face can be drawn
  static const u32 kMinTimeBetweenLastNonProcFaceAndNextProcFace_ms = 2 * ANIM_TIME_STEP_MS;
    
  // Default time to wait before forcing KeepFaceAlive() after the latest stream has stopped
  const f32 kDefaultLongEnoughSinceLastStreamTimeout_s = 0.5f;

  // Default KeepFaceAliveParams
  #define SET_DEFAULT(param, value) {KeepFaceAliveParameter::param, static_cast<f32>(value)}
  const std::unordered_map<KeepFaceAliveParameter, f32> _kDefaultKeepFaceAliveParams = {
    SET_DEFAULT(BlinkSpacingMinTime_ms, 3000),
    SET_DEFAULT(BlinkSpacingMaxTime_ms, 4000),
    SET_DEFAULT(EyeDartSpacingMinTime_ms, 250),
    SET_DEFAULT(EyeDartSpacingMaxTime_ms, 1000),
    SET_DEFAULT(EyeDartMaxDistance_pix, 6),
    SET_DEFAULT(EyeDartMinDuration_ms, 50),
    SET_DEFAULT(EyeDartMaxDuration_ms, 200),
    SET_DEFAULT(EyeDartOuterEyeScaleIncrease, 0.1f),
    SET_DEFAULT(EyeDartUpMaxScale, 1.1f),
    SET_DEFAULT(EyeDartDownMinScale, 0.85f)
  };
  #undef SET_DEFAULT
  
  CONSOLE_VAR(bool, kFullAnimationAbortOnAudioTimeout, "AnimationStreamer", false);
  CONSOLE_VAR(u32, kAnimationAudioAllowedBufferTime_ms, "AnimationStreamer", 250);

  // Whether or not to display themal throttling indicator on face
  CONSOLE_VAR(bool, kDisplayThermalThrottling, "AnimationStreamer", true);

  // Temperature beyond which the thermal indicator is displayed on face    
  CONSOLE_VAR(u32,  kThermalAlertTemp_C,           "AnimationStreamer", 80);

  // Must be at least this hot for CPU throttling to be considered caused
  // by thermal conditions. It can also throttle because the system is idle
  // which we don't care to indicate on the face.
  CONSOLE_VAR(u32,  kThermalThrottlingMinTemp_C,   "AnimationStreamer", 65);

  // Allows easy disabling of KeepFaceAlive using the console system (i.e., without a message interface)
  // This is useful for the animators to disable KeepFaceAlive while testing eye shapes, etc.
  static bool s_enableKeepFaceAlive = true;
  void ToggleKeepFaceAlive(ConsoleFunctionContextRef context)
  {
    s_enableKeepFaceAlive = !s_enableKeepFaceAlive;
    PRINT_CH_INFO(kLogChannelName, "ConsoleFunc.ToggleKeepFaceAlive", "KeepFaceAlive now %s",
                  (s_enableKeepFaceAlive ? "ON" : "OFF"));
  }
  
  CONSOLE_FUNC(ToggleKeepFaceAlive, "ProceduralFace");

  static std::string s_frameFilename;
  static int s_frame = 0;
  static int s_framesToCapture = 0;
  static jo_gif_t s_gif;
  static clock_t s_frameStart;
  static FILE* s_tga = nullptr;

  static void CaptureFace(ConsoleFunctionContextRef context)
  {
    std::string html;

    if(s_framesToCapture == 0) {
      s_frameFilename = ConsoleArg_GetOptional_String(context, "filename", "screenshot.tga");
      const int numFrames = ConsoleArg_GetOptional_Int(context, "numFrames", 1);

      const Util::Data::DataPlatform* dataPlatform = s_context->GetDataPlatform();
      const std::string cacheFilename = dataPlatform->pathToResource(Util::Data::Scope::Cache, s_frameFilename);

      if(s_frameFilename.find(".gif") != std::string::npos) {
        s_gif = jo_gif_start(cacheFilename.c_str(), FACE_DISPLAY_WIDTH, FACE_DISPLAY_HEIGHT, -1, 255);
        if(s_gif.fp != nullptr) {
          s_framesToCapture = numFrames;
        }
      } else {
        s_tga = fopen(cacheFilename.c_str(), "wb");
        if(s_tga != nullptr) {
          uint8_t head[18] = {0};
          head[ 2] = 2; // uncompressed, true-color image
          head[12] = FACE_DISPLAY_WIDTH & 0xff;
          head[13] = (FACE_DISPLAY_WIDTH >> 8) & 0xff;
          head[14] = FACE_DISPLAY_HEIGHT & 0xff;
          head[15] = (FACE_DISPLAY_HEIGHT >> 8) & 0xff;
          head[16] = 32;   /** 32 bits depth **/
          head[17] = 0x08; /** top-down flag, 8 bits alpha **/
          fwrite(head, sizeof(uint8_t), 18, s_tga);

          s_framesToCapture = 1;
        }
      }

      if(s_framesToCapture > 0) {
        s_frameStart = clock();
        s_frame = 0;

        if(s_enableKeepFaceAlive) {
          html = std::string("<html>\nCapturing frames as <a href=\"/cache/")+s_frameFilename+"\">"+s_frameFilename+"\n" + "</html>\n";
        } else {
          html = std::string("<html>\nWaiting to capture frames as <a href=\"/cache/")+s_frameFilename+"\">"+s_frameFilename+"\n" + "</html>\n";
        }
      } else {
        html = std::string("<html>\nError: unable to open file <a href=\"/cache/")+s_frameFilename+"\">"+s_frameFilename+"\n" + "</html>\n";
      }
    } else {
      html = std::string("Capture already in progress as <a href=\"/cache/")+s_frameFilename+"\">"+s_frameFilename+"\n" + "</html>\n";
    }

    context->channel->WriteLog(html.c_str());
  }

  CONSOLE_FUNC(CaptureFace, "ProceduralFace", optional const char* filename, optional int numFrames);
  } // namespace
  
  AnimationStreamer::AnimationStreamer(const AnimContext* context)
  : _context(context)
  , _trackLayerComponent(new TrackLayerComponent(context))
  , _lockedTracks(0)
  , _tracksInUse(0)
  , _animAudioClient( new Audio::AnimationAudioClient(context->GetAudioController()) )
  , _proceduralAudioClient( new Audio::ProceduralAudioClient(context->GetAudioController()) )
  , _longEnoughSinceLastStreamTimeout_s(kDefaultLongEnoughSinceLastStreamTimeout_s)
  , _numTicsToSendAnimState(kAnimStateReportingPeriod_tics)
  {
    _proceduralAnimation = new Animation(EnumToString(AnimConstants::PROCEDURAL_ANIM));
    _proceduralAnimation->SetIsLive(true);

    if(ANKI_DEV_CHEATS) {
      if (!s_faceDataOverrideRegistered) {
        s_context = context;
        s_faceDataOverride.RegisterFaceWithConsoleVars();
        s_faceDataOverrideRegistered = true;
      }
    }
  }
  
  Result AnimationStreamer::Init()
  {
    SetDefaultKeepFaceAliveParams();
    
    // TODO: Restore ability to subscribe to messages here?
    //       It's currently hard to do with CPPlite messages.
    // SetupHandlers(_context->GetExternalInterface());

    // Set neutral face
    DEV_ASSERT(nullptr != _context, "AnimationStreamer.Init.NullContext");
    DEV_ASSERT(nullptr != _context->GetDataLoader(), "AnimationStreamer.Init.NullRobotDataLoader");
    const std::string neutralFaceAnimName = "anim_neutral_eyes_01";
    _neutralFaceAnimation = _context->GetDataLoader()->GetCannedAnimation(neutralFaceAnimName);
    if (nullptr != _neutralFaceAnimation)
    {
      auto frame = _neutralFaceAnimation->GetTrack<ProceduralFaceKeyFrame>().GetFirstKeyFrame();
      ProceduralFace::SetResetData(frame->GetFace());
    }
    else
    {
      PRINT_NAMED_ERROR("AnimationStreamer.Constructor.NeutralFaceDataNotFound",
                        "Could not find expected neutral face animation file called %s",
                        neutralFaceAnimName.c_str());
    }
    
    
    // Do this after the ProceduralFace class has set to use the right neutral face
    _trackLayerComponent->Init();
    
    _faceDrawBuf.Allocate(FACE_DISPLAY_HEIGHT, FACE_DISPLAY_WIDTH);
    _procFaceImg.Allocate(FACE_DISPLAY_HEIGHT, FACE_DISPLAY_WIDTH);
    _faceImageRGB565.Allocate(FACE_DISPLAY_HEIGHT, FACE_DISPLAY_WIDTH);
    _faceImageGrayscale.Allocate(FACE_DISPLAY_HEIGHT, FACE_DISPLAY_WIDTH);
   

    // TODO: _Might_ need to disable this eventually if there are conflicts
    //       with wake up animations or something on startup. The only reason
    //       this is here is to make sure there's something on the face and
    //       currently there's no face animation that the engine automatically
    //       initiates on startup.
    // 
    // Turn on neutral face by default at startup
    // Otherwise face is blank until an animation is played.
    SetStreamingAnimation(_neutralFaceAnimation, kNotAnimatingTag);

    return RESULT_OK;
  }
  
  AnimationStreamer::~AnimationStreamer()
  {
    Util::SafeDelete(_proceduralAnimation);

    FaceDisplay::removeInstance();
  }
  
  Result AnimationStreamer::SetStreamingAnimation(const std::string& name, Tag tag, u32 numLoops, bool interruptRunning)
  {
    // Special case: stop streaming the current animation
    if(name.empty()) {
      if(DEBUG_ANIMATION_STREAMING) {
        PRINT_CH_DEBUG(kLogChannelName, "AnimationStreamer.SetStreamingAnimation.StoppingCurrent",
                       "Stopping streaming of animation '%s'.",
                       GetStreamingAnimationName().c_str());
      }

      Abort();
      return RESULT_OK;
    }
    return SetStreamingAnimation(_context->GetDataLoader()->GetCannedAnimation(name), tag, numLoops, interruptRunning, false);
  }
  
  Result AnimationStreamer::SetStreamingAnimation(Animation* anim, Tag tag, u32 numLoops, bool interruptRunning, bool isInternalAnim)
  {
    if(DEBUG_ANIMATION_STREAMING)
    {
      PRINT_CH_DEBUG(kLogChannelName, 
                     "AnimationStreamer.SetStreamingAnimation", "Name:%s Tag:%d NumLoops:%d",
                     anim != nullptr ? anim->GetName().c_str() : "NULL", tag, numLoops);
    }
    
    const bool wasStreamingSomething = nullptr != _streamingAnimation;
    
    if(wasStreamingSomething)
    {
      if(nullptr != anim && !interruptRunning) {
        PRINT_CH_INFO(kLogChannelName,
                      "AnimationStreamer.SetStreamingAnimation.NotInterrupting",
                      "Already streaming %s, will not interrupt with %s",
                      _streamingAnimation->GetName().c_str(),
                      anim->GetName().c_str());
        return RESULT_FAIL;
      }
      
      PRINT_NAMED_WARNING("AnimationStreamer.SetStreamingAnimation.Aborting",
                          "Animation %s is interrupting animation %s",
                          anim != nullptr ? anim->GetName().c_str() : "NULL",
                          _streamingAnimation->GetName().c_str());

      Abort();                          
    }
    
    _streamingAnimation = anim;
    if(_streamingAnimation == nullptr) {
      return RESULT_OK;
    }

    _wasAnimationInterruptedWithNothing = false;
  
    // Get the animation ready to play
    InitStream(_streamingAnimation, tag);
    
    _numLoops = numLoops;
    _loopCtr = 0;

    _playingInternalAnim = isInternalAnim;
    
    if(DEBUG_ANIMATION_STREAMING) {
      PRINT_CH_DEBUG(kLogChannelName, 
                     "AnimationStreamer.SetStreamingAnimation",
                     "Will start streaming '%s' animation %d times with tag=%d.",
                     _streamingAnimation->GetName().c_str(), numLoops, tag);
    }
    
    return RESULT_OK;

  }
  
  Result AnimationStreamer::SetProceduralFace(const ProceduralFace& face, u32 duration_ms)
  {
    DEV_ASSERT(nullptr != _proceduralAnimation, "AnimationStreamer.SetProceduralFace.NullProceduralAnimation");
    
    // Always add one keyframe
    ProceduralFaceKeyFrame keyframe(face);
    Result result = _proceduralAnimation->AddKeyFrameToBack(keyframe);
    
    // Add a second one later to interpolate to, if duration is longer than one keyframe
    if(RESULT_OK == result && duration_ms > ANIM_TIME_STEP_MS)
    {
      keyframe.SetTriggerTime(duration_ms-ANIM_TIME_STEP_MS);
      result = _proceduralAnimation->AddKeyFrameToBack(keyframe);
    }

    if(!(ANKI_VERIFY(RESULT_OK == result, "AnimationStreamer.SetProceduralFace.FailedToCreateAnim", "")))
    {
      return result;
    }
    
    // ProceduralFace is always played as an "internal" animation since it's not considered 
    // a regular animation by the engine so we don't need to send AnimStarted and AnimEnded
    // messages for it.
    result = SetStreamingAnimation(_proceduralAnimation, 0);
    
    return result;
  }

  void AnimationStreamer::Process_displayFaceImageChunk(const RobotInterface::DisplayFaceImageBinaryChunk& msg) 
  {
    // Since binary images and grayscale images both use the same underlying image, ensure that
    // only one type is being sent at a time
    DEV_ASSERT(_faceImageGrayscaleChunksReceivedBitMask == 0,
               "AnimationStreamer.Process_displayFaceImageChunk.AlreadyReceivingGrayscaleImage");
    
    // Expand the bit-packed msg.faceData (every bit == 1 pixel) to byte array (every byte == 1 pixel)
    static const u32 kExpectedNumPixels = FACE_DISPLAY_NUM_PIXELS/2;
    static const u32 kDataLength = sizeof(msg.faceData);
    static_assert(8 * kDataLength == kExpectedNumPixels, "Mismatched face image and bit image sizes");

    if (msg.imageId != _faceImageId) {
      if (_faceImageChunksReceivedBitMask != 0) {
        PRINT_NAMED_WARNING("AnimationStreamer.Process_displayFaceImageChunk.UnfinishedFace", 
                            "Overwriting ID %d with ID %d", 
                            _faceImageId, msg.imageId);
      }
      _faceImageId = msg.imageId;
      _faceImageChunksReceivedBitMask = 1 << msg.chunkIndex;
    } else {
      _faceImageChunksReceivedBitMask |= 1 << msg.chunkIndex;
    }
    
    uint8_t* imageData_i = _faceImageGrayscale.GetDataPointer();

    uint32_t destI = msg.chunkIndex * kExpectedNumPixels;
    
    for (int i = 0; i < kDataLength; ++i)
    {
      uint8_t currentByte = msg.faceData[i];
        
      for (uint8_t bit = 0; bit < 8; ++bit)
      {
        imageData_i[destI] = ((currentByte & 0x80) > 0) ? 255 : 0;
        ++destI;
        currentByte = (uint8_t)(currentByte << 1);
      }
    }
    assert(destI == kExpectedNumPixels * (1+msg.chunkIndex));
    
    if (_faceImageChunksReceivedBitMask == kAllFaceImageChunksReceivedMask) {
      //PRINT_CH_DEBUG(kLogChannelName, "AnimationStreamer.Process_displayFaceImageChunk.CompleteFaceReceived", "");
      SetFaceImage(_faceImageGrayscale, msg.duration_ms);
      _faceImageId = 0;
      _faceImageChunksReceivedBitMask = 0;
    }
  }

  void AnimationStreamer::Process_displayFaceImageChunk(const RobotInterface::DisplayFaceImageGrayscaleChunk& msg)
  {
    // Since binary images and grayscale images both use the same underlying image, ensure that
    // only one type is being sent at a time
    DEV_ASSERT(_faceImageChunksReceivedBitMask == 0,
               "AnimationStreamer.Process_displayFaceImageChunk.AlreadyReceivingBinaryImage");
    
    if (msg.imageId != _faceImageGrayscaleId) {
      if (_faceImageGrayscaleChunksReceivedBitMask != 0) {
        PRINT_NAMED_WARNING("AnimationStreamer.Process_displayFaceImageGrayscaleChunk.UnfinishedFace",
                            "Overwriting ID %d with ID %d",
                            _faceImageGrayscaleId, msg.imageId);
      }
      _faceImageGrayscaleId = msg.imageId;
      _faceImageGrayscaleChunksReceivedBitMask = 1 << msg.chunkIndex;
    } else {
      _faceImageGrayscaleChunksReceivedBitMask |= 1 << msg.chunkIndex;
    }
    
    static const u16 kMaxNumPixelsPerChunk = sizeof(msg.faceData) / sizeof(msg.faceData[0]);
    const auto numPixels = std::min(msg.numPixels, kMaxNumPixelsPerChunk);
    uint8_t* imageData_i = _faceImageGrayscale.GetDataPointer();
    std::copy_n(msg.faceData, numPixels, imageData_i + (msg.chunkIndex * kMaxNumPixelsPerChunk) );
    
    if (_faceImageGrayscaleChunksReceivedBitMask == kAllFaceImageGrayscaleChunksReceivedMask) {
      //PRINT_CH_DEBUG(kLogChannelName, "AnimationStreamer.Process_displayFaceImageGrayscaleChunk.CompleteFaceReceived", "");
      SetFaceImage(_faceImageGrayscale, msg.duration_ms);
      _faceImageGrayscaleId = 0;
      _faceImageGrayscaleChunksReceivedBitMask = 0;
    }
  }
  
  void AnimationStreamer::Process_displayFaceImageChunk(const RobotInterface::DisplayFaceImageRGBChunk& msg)
  {
    if (msg.imageId != _faceImageRGBId) {
      if (_faceImageRGBChunksReceivedBitMask != 0) {
        PRINT_NAMED_WARNING("AnimationStreamer.Process_displayFaceImageRGBChunk.UnfinishedFace", 
                            "Overwriting ID %d with ID %d", 
                            _faceImageRGBId, msg.imageId);
      }
      _faceImageRGBId = msg.imageId;
      _faceImageRGBChunksReceivedBitMask = 1 << msg.chunkIndex;
    } else {
      _faceImageRGBChunksReceivedBitMask |= 1 << msg.chunkIndex;
    }
    
    static const u16 kMaxNumPixelsPerChunk = sizeof(msg.faceData) / sizeof(msg.faceData[0]);
    const auto numPixels = std::min(msg.numPixels, kMaxNumPixelsPerChunk);
    std::copy_n(msg.faceData, numPixels, _faceImageRGB565.GetRawDataPointer() + (msg.chunkIndex * kMaxNumPixelsPerChunk) );
    
    if (_faceImageRGBChunksReceivedBitMask == kAllFaceImageRGBChunksReceivedMask) {
      //PRINT_CH_DEBUG(kLogChannelName, "AnimationStreamer.Process_displayFaceImageRGBChunk.CompleteFaceReceived", "");
      SetFaceImage(_faceImageRGB565, msg.duration_ms);
      _faceImageRGBId = 0;
      _faceImageRGBChunksReceivedBitMask = 0;
    }
  }

  void AnimationStreamer::Process_displayCompositeImageChunk(const RobotInterface::DisplayCompositeImageChunk& msg)
  {
    // Check for image ID mismatches
    if(_compositeImageBuilder != nullptr){
      if(msg.compositeImageID != _compositeImageID){
        _compositeImageBuilder.reset();
        PRINT_NAMED_WARNING("AnimationStreamer.Process_displayCompositeImageChunk.MissingChunk",
                            "Composite image was being built with image ID %d, but new ID %d received so wiping image",
                            _compositeImageID, msg.compositeImageID);
      }
    }
    _compositeImageID = msg.compositeImageID;
   
    // Add the chunk to the builder
    if(_compositeImageBuilder == nullptr){
      auto* builder = new Vision::CompositeImageBuilder(_context->GetDataLoader()->GetSpritePaths(),
                                                        msg.compositeImageChunk);
      _compositeImageBuilder.reset(builder);
    }else{
      _compositeImageBuilder->AddImageChunk(msg.compositeImageChunk);
    }

    // Display the image if all chunks received
    if(_compositeImageBuilder->CanBuildImage()){
      Vision::CompositeImage outImage;
      const bool builtImage = _compositeImageBuilder->GetCompositeImage(outImage);
      if(ANKI_VERIFY(builtImage, 
                     "AnimationStreamer.Process_displayCompositeImageChunk.FailedToBuildImage",
                     "Composite image failed to build")){
        SetFaceImageHelper(outImage.RenderImage().ToGray(), msg.duration_ms, true);
      }

      _compositeImageBuilder.reset();
    }
    
  }


  Result AnimationStreamer::SetFaceImage(const Vision::Image& img, u32 duration_ms)
  {
    return SetFaceImageHelper(img, duration_ms, true);
  }


  Result AnimationStreamer::SetFaceImage(const Vision::ImageRGB565& imgRGB565, u32 duration_ms)
  {
    if (_redirectFaceImagesToDebugScreen) {
      FaceInfoScreenManager::getInstance()->DrawCameraImage(imgRGB565);

      // TODO: Return here or will that screw up stuff on the engine side?
      //return RESULT_OK;
    }
    return SetFaceImageHelper(imgRGB565, duration_ms, false);
  }
  
  void AnimationStreamer::Abort()
  {
    if (nullptr != _streamingAnimation)
    {
      PRINT_CH_INFO(kLogChannelName,
                    "AnimationStreamer.Abort",
                    "Tag=%d %s hasFramesLeft=%d startSent=%d endSent=%d",
                    _tag,
                    _streamingAnimation->GetName().c_str(),
                    _streamingAnimation->HasFramesLeft(),
                    _startOfAnimationSent,
                    _endOfAnimationSent);
      
      if (_startOfAnimationSent) {
        SendEndOfAnimation(true);
      }

      EnableBackpackAnimationLayer(false);

      _animAudioClient->AbortAnimation();

      if (_streamingAnimation == _proceduralAnimation) {
        _proceduralAnimation->Clear();
      }

      // Reset the current SpriteSequenceKeyFrame if there is one.
      // Note: This is currently the only keyframe that modifies a variable
      // as it's read and needs to be reset before the next time it's read,
      // which is why we're not resetting all tracks in the same way
      auto & spriteSeqTrack = _streamingAnimation->GetTrack<SpriteSequenceKeyFrame>();
      if (spriteSeqTrack.HasFramesLeft()) {
        auto & faceKeyFrame = spriteSeqTrack.GetCurrentKeyFrame();
        faceKeyFrame.Reset();
      }

      // Reset animation pointer
      _streamingAnimation = nullptr;

      // If we get to KeepFaceAlive with this flag set, we'll stream neutral face for safety.
      _wasAnimationInterruptedWithNothing = true;
    }
  } // Abort()

  
  
  Result AnimationStreamer::InitStream(Animation* anim, Tag withTag)
  {
    Result lastResult = anim->Init();
    if(lastResult == RESULT_OK)
    {
     
      _tag = withTag;
      
      _startTime_ms = BaseStationTimer::getInstance()->GetCurrentTimeStamp();
      
      // Initialize "fake" streaming time to the same start time so we can compare
      // to it for determining when its time to stream out a keyframe
      _streamingTime_ms = _startTime_ms;

      _endOfAnimationSent = false;
      _startOfAnimationSent = false;
      
      // Make sure any eye dart (which is persistent) gets removed so it doesn't
      // affect the animation we are about to start streaming. Give it a little
      // duration so it doesn't pop.
      _trackLayerComponent->RemoveKeepFaceAlive(3*ANIM_TIME_STEP_MS);
    }
    return lastResult;
  }
  
  // Sends message to robot if track is unlocked and deletes it.
  //
  // TODO: Take in EngineToRobot& instead of ptr?
  //       i.e. Why doesn't KeyFrame::GetStreamMessage() return a ref?
  bool AnimationStreamer::SendIfTrackUnlocked(RobotInterface::EngineToRobot* msg, AnimTrackFlag track)
  {
    bool res = false;
    if(msg != nullptr) {
      if (!IsTrackLocked((u8)track)) {
        switch(track) {
          case AnimTrackFlag::HEAD_TRACK:
          case AnimTrackFlag::LIFT_TRACK:
          case AnimTrackFlag::BODY_TRACK:
          case AnimTrackFlag::BACKPACK_LIGHTS_TRACK:
            res = AnimProcessMessages::SendAnimToRobot(*msg);
            _tracksInUse |= (u8)track;
            break;
          default:
            // Audio, face, and event frames are handled separately since they don't actually result in a EngineToRobot message
            PRINT_NAMED_WARNING("AnimationStreamer.SendIfTrackUnlocked.InvalidTrack", "%s", EnumToString(track));
            break;
        }
      }
      delete msg;
    }
    return res;
  }
  
  void AnimationStreamer::SetParam(KeepFaceAliveParameter whichParam, float newValue)
  {
    switch(whichParam) {
      case KeepFaceAliveParameter::BlinkSpacingMaxTime_ms:
      {
        const auto maxSpacing_ms = _trackLayerComponent->GetMaxBlinkSpacingTimeForScreenProtection_ms();
        if( newValue > maxSpacing_ms)
        {
          PRINT_NAMED_WARNING("AnimationStreamer.SetParam.MaxBlinkSpacingTooLong",
                              "Clamping max blink spacing to %dms to avoid screen burn-in",
                              maxSpacing_ms);
          
          newValue = maxSpacing_ms;
        }
        // intentional fall through
      }
      case KeepFaceAliveParameter::BlinkSpacingMinTime_ms:
      case KeepFaceAliveParameter::EyeDartMinDuration_ms:
      case KeepFaceAliveParameter::EyeDartMaxDuration_ms:
      case KeepFaceAliveParameter::EyeDartSpacingMinTime_ms:
      case KeepFaceAliveParameter::EyeDartSpacingMaxTime_ms:
      {
        if (_keepFaceAliveParams[whichParam] != newValue) {
          _trackLayerComponent->ResetKeepFaceAliveTimers();
        }
        break;
      }
      default:
        break;
    }

    _keepFaceAliveParams[whichParam] = newValue;
    PRINT_CH_INFO(kLogChannelName,
                  "AnimationStreamer.SetParam", "%s : %f", 
                  EnumToString(whichParam), newValue);
  }
  
  
  void AnimationStreamer::BufferFaceToSend(const ProceduralFace& procFace)
  {
    if(kProcFace_Display == (int)FaceDisplayType::Test)
    {
      // Display three color strips increasing in brightness from left to right
      for(int i=0; i<FACE_DISPLAY_HEIGHT/3; ++i)
      {
        Vision::PixelRGB565* red_i   = _faceDrawBuf.GetRow(i);
        Vision::PixelRGB565* green_i = _faceDrawBuf.GetRow(i + FACE_DISPLAY_HEIGHT/3);
        Vision::PixelRGB565* blue_i  = _faceDrawBuf.GetRow(i + 2*FACE_DISPLAY_HEIGHT/3);
        for(int j=0; j<FACE_DISPLAY_WIDTH; ++j)
        {
          const u8 value = Util::numeric_cast_clamped<u8>(std::round((f32)j/(f32)FACE_DISPLAY_WIDTH * 255.f));
          red_i[j]   = Vision::PixelRGB565(value, 0, 0);
          green_i[j] = Vision::PixelRGB565(0, value, 0);
          blue_i[j]  = Vision::PixelRGB565(0, 0, value);
        }
      }
    }
    else
    {

#if PROCEDURALFACE_NOISE_FEATURE
      if(kProcFace_NoiseNumFrames == 0) {
#else
      {
#endif
        static ProceduralFace previousFace;
        if (previousFace == procFace) {
          return;
        }
        previousFace = procFace;
      }
        
      DEV_ASSERT(_context != nullptr, "AnimationStreamer.BufferFaceToSend.NoContext");
      DEV_ASSERT(_context->GetRandom() != nullptr, "AnimationStreamer.BufferFaceToSend.NoRNGinContext");

      if(s_faceDataReset) {
        s_faceDataOverride = s_faceDataBaseline = procFace;
        ProceduralFace::SetHue(ProceduralFace::DefaultHue);

        // animationStreamer.cpp
        
        NativeAnkiUtilConsoleResetValueToDefault("ProcFace_OverrideEyeParams");
        NativeAnkiUtilConsoleResetValueToDefault("ProcFace_OverrideRightEyeParams");
        NativeAnkiUtilConsoleResetValueToDefault("ProcFace_Gamma");
        NativeAnkiUtilConsoleResetValueToDefault("ProcFace_FromLinear");
        NativeAnkiUtilConsoleResetValueToDefault("ProcFace_ToLinear");

        NativeAnkiUtilConsoleResetValueToDefault("ProcFace_DefaultScanlineOpacity");
        NativeAnkiUtilConsoleResetValueToDefault("ProcFace_NominalEyeSpacing");

        // proceduralFace.cpp

        NativeAnkiUtilConsoleResetValueToDefault("ProcFace_DefaultScanlineOpacity");
        NativeAnkiUtilConsoleResetValueToDefault("ProcFace_NominalEyeSpacing");

        // proceduralFaceDrawer.cpp
        
        NativeAnkiUtilConsoleResetValueToDefault("ProcFace_NoiseNumFrames");
        NativeAnkiUtilConsoleResetValueToDefault("ProcFace_NoiseMinLightness");
        NativeAnkiUtilConsoleResetValueToDefault("ProcFace_NoiseMaxLightness");
        NativeAnkiUtilConsoleResetValueToDefault("ProcFace_NoiseFraction");
        NativeAnkiUtilConsoleResetValueToDefault("ProcFace_NoiseFraction");

        NativeAnkiUtilConsoleResetValueToDefault("ProcFace_UseAntiAliasedLines");
        NativeAnkiUtilConsoleResetValueToDefault("ProcFace_EyeLightnessMultiplier");

        NativeAnkiUtilConsoleResetValueToDefault("ProcFace_HotspotRender");
        NativeAnkiUtilConsoleResetValueToDefault("ProcFace_HotspotFalloff");
        NativeAnkiUtilConsoleResetValueToDefault("ProcFace_GlowRender");
        NativeAnkiUtilConsoleResetValueToDefault("ProcFace_GlowSizeMultiplier");
        NativeAnkiUtilConsoleResetValueToDefault("ProcFace_GlowLightnessMultiplier");
        NativeAnkiUtilConsoleResetValueToDefault("ProcFace_GlowGaussianFilter");

        NativeAnkiUtilConsoleResetValueToDefault("ProcFace_AntiAliasingSize");
        NativeAnkiUtilConsoleResetValueToDefault("ProcFace_AntiAliasingGaussianFilter");

        s_faceDataReset = false;
      }

      if(kProcFace_Display == (int)FaceDisplayType::OverrideIndividually || kProcFace_Display == (int)FaceDisplayType::OverrideTogether) {
        // compare override face data with baseline, if different update the rendered face

        ProceduralFace newProcFace = procFace;

        // for each eye parameter
        if(kProcFace_Display == (int)FaceDisplayType::OverrideTogether) {
          s_faceDataOverride.SetParameters(ProceduralFace::WhichEye::Right, s_faceDataOverride.GetParameters(ProceduralFace::WhichEye::Left));
        }
        for(auto whichEye : {ProceduralFace::WhichEye::Left, ProceduralFace::WhichEye::Right}) {
          for (std::underlying_type<ProceduralFace::Parameter>::type iParam=0;
               iParam < Util::EnumToUnderlying(ProceduralFace::Parameter::NumParameters);
               ++iParam) {
            if(s_faceDataOverride.GetParameter(whichEye, (ProceduralFace::Parameter)iParam) !=
               s_faceDataBaseline.GetParameter(whichEye, (ProceduralFace::Parameter)iParam)) {
              newProcFace.SetParameter(whichEye,
                                       (ProceduralFace::Parameter)iParam,
                                       s_faceDataOverride.GetParameter(whichEye, (ProceduralFace::Parameter)iParam));
            }
          }
        }

        // for each face parameter
        if(s_faceDataOverride.GetFaceAngle() != s_faceDataBaseline.GetFaceAngle()) {
          newProcFace.SetFaceAngle(s_faceDataOverride.GetFaceAngle());
        }
        if(s_faceDataOverride.GetFaceScale()[0] != s_faceDataBaseline.GetFaceScale()[0] ||
           s_faceDataOverride.GetFaceScale()[1] != s_faceDataBaseline.GetFaceScale()[1]) {
          newProcFace.SetFaceScale(s_faceDataOverride.GetFaceScale());
        }
        if(s_faceDataOverride.GetFacePosition()[0] != s_faceDataBaseline.GetFacePosition()[0] ||
           s_faceDataOverride.GetFacePosition()[1] != s_faceDataBaseline.GetFacePosition()[1]) {
          newProcFace.SetFacePosition(s_faceDataOverride.GetFacePosition());
        }
        if(s_faceDataOverride.GetScanlineOpacity() != s_faceDataBaseline.GetScanlineOpacity()) {
          newProcFace.SetScanlineOpacity(s_faceDataOverride.GetScanlineOpacity());
        }

        ProceduralFaceDrawer::DrawFace(newProcFace, *_context->GetRandom(), _faceDrawBuf);
      } else {
        ProceduralFaceDrawer::DrawFace(procFace, *_context->GetRandom(), _faceDrawBuf);
      }
    }
    
    BufferFaceToSend(_faceDrawBuf);
  }

  // Conversions to and from linear space i.e. sRGB to linear and linear to sRGB
  // used when populating the lookup tables for gamma correction.
  // https://github.com/hsluv/hsluv/releases/tag/_legacyjs6.0.4

  static inline float fromLinear(float c)
  {
    if (c <= 0.0031308f) {
      return 12.92f * c;
    } else {
      return (float)(1.055f * pow(c, 1.f / 2.4f) - 0.055f);
    }
  }

  static inline float toLinear(float c)
  {
    float a = 0.055f;

    if (c > 0.04045f) {
      return (float)(powf((c + a) / (1.f + a), 2.4f));
    } else {
      return (float)(c / 12.92f);
    }
  }

  void AnimationStreamer::UpdateCaptureFace(Vision::ImageRGB565& faceImg565)
  {
    if(s_framesToCapture > 0) {
      clock_t end = clock();
      int elapsed = (end - s_frameStart)/float(CLOCKS_PER_SEC);
      s_frameStart = end;

      Vision::ImageRGBA frame(FACE_DISPLAY_HEIGHT, FACE_DISPLAY_WIDTH);
      frame.SetFromRGB565(faceImg565);

      if(s_tga != NULL) {
        fwrite(frame.GetDataPointer(), sizeof(uint8_t), FACE_DISPLAY_WIDTH*FACE_DISPLAY_HEIGHT*4, s_tga);
      } else {
        if(elapsed == 0) elapsed = 1;
        jo_gif_frame(&s_gif, (uint8_t*)frame.GetDataPointer(), elapsed, false);
      }

      ++s_frame;
      if(s_frame == s_framesToCapture) {
        if(s_tga != NULL) {
          fclose(s_tga);
        } else {
          jo_gif_end(&s_gif);
        }

        s_framesToCapture = 0;
      }
    }
  }

  void AnimationStreamer::BufferFaceToSend(Vision::ImageRGB565& faceImg565)
  {
    DEV_ASSERT_MSG(faceImg565.GetNumCols() == FACE_DISPLAY_WIDTH &&
                   faceImg565.GetNumRows() == FACE_DISPLAY_HEIGHT,
                   "AnimationStreamer.BufferFaceToSend.InvalidImageSize",
                   "Got %d x %d. Expected %d x %d",
                   faceImg565.GetNumCols(), faceImg565.GetNumRows(),
                   FACE_DISPLAY_WIDTH, FACE_DISPLAY_HEIGHT);

    static int kProcFace_GammaType_old = (int)FaceGammaType::None;
    static f32 kProcFace_Gamma_old = -1.f;

    if((kProcFace_GammaType != kProcFace_GammaType_old) || (kProcFace_Gamma_old != kProcFace_Gamma)) {
      switch(kProcFace_GammaType) {
      case (int)FaceGammaType::FromLinear:
        for (int i = 0; i < 256; i++) {
          s_gammaLUT[0][i] = s_gammaLUT[1][i] = s_gammaLUT[2][i] = cv::saturate_cast<uchar>(fromLinear(i / 255.f) * 255.0f);
        }
        break;
      case (int)FaceGammaType::ToLinear:
        for (int i = 0; i < 256; i++) {
          s_gammaLUT[0][i] = s_gammaLUT[1][i] = s_gammaLUT[2][i] = cv::saturate_cast<uchar>(toLinear(i / 255.f) * 255.0f);
        }
        break;
      case (int)FaceGammaType::AddGamma:
        for (int i = 0; i < 256; i++) {
          s_gammaLUT[0][i] = s_gammaLUT[1][i] = s_gammaLUT[2][i] = cv::saturate_cast<uchar>(powf((float)(i / 255.f), 1.f/kProcFace_Gamma) * 255.0f);
        }
        break;
      case (int)FaceGammaType::RemoveGamma:
        for (int i = 0; i < 256; i++) {
          s_gammaLUT[0][i] = s_gammaLUT[1][i] = s_gammaLUT[2][i] = cv::saturate_cast<uchar>(powf((float)(i / 255.f), kProcFace_Gamma) * 255.0f);
        }
        break;
      }

      kProcFace_GammaType_old = kProcFace_GammaType;
      kProcFace_Gamma_old = kProcFace_Gamma;
    }

    if(kProcFace_GammaType != (int)FaceGammaType::None) {
      int nrows = faceImg565.GetNumRows();
      int ncols = faceImg565.GetNumCols();
      if(faceImg565.IsContinuous())
      {
        ncols *= nrows;
        nrows = 1;
      }

      for(int i=0; i<nrows; ++i)
      {
        Vision::PixelRGB565* img_i = faceImg565.GetRow(i);
        for(int j=0; j<ncols; ++j) {
          img_i[j].SetValue(Vision::PixelRGB565(s_gammaLUT[0][img_i[j].r()], s_gammaLUT[1][img_i[j].g()], s_gammaLUT[2][img_i[j].b()]).GetValue());
        }
      }
    }

#if ANKI_DEV_CHEATS

    UpdateCaptureFace(faceImg565);

    // Draw red square in corner of face if thermal issues
    const bool isCPUThrottling = OSState::getInstance()->IsCPUThrottling();
    auto tempC = OSState::getInstance()->GetTemperature_C();
    const bool tempExceedsAlertThreshold = tempC >= kThermalAlertTemp_C;
    const bool tempExceedsThrottlingThreshold = tempC >= kThermalThrottlingMinTemp_C;
    if (kDisplayThermalThrottling && 
        ((isCPUThrottling && tempExceedsThrottlingThreshold) || (tempExceedsAlertThreshold)) ) {

      // Draw square if CPU is being throttled
      const ColorRGBA alertColor(1.f, 0.f, 0.f);
      if (isCPUThrottling) {
        const Rectangle<f32> rect( 0, 0, 20, 20);
        faceImg565.DrawFilledRect(rect, alertColor);
      }

      // Display temperature
      const std::string tempStr = std::to_string(tempC) + "C";
      const Point2f position(25, 25);
      faceImg565.DrawText(position, tempStr, alertColor, 1.f);
    }
#endif

    if(SHOULD_SEND_DISPLAYED_FACE_TO_ENGINE){
      // Send the final buffered face back over to engine
      ASSERT_NAMED(faceImg565.IsContinuous(), "AnimationComponent.DisplayFaceImage.NotContinuous");
      static int imageID = 0;
      static const int kMaxPixelsPerMsg = 600; // TODO - fix
      
      int chunkCount = 0;
      int pixelsLeftToSend = FACE_DISPLAY_NUM_PIXELS;
      const u16* startIt = faceImg565.GetRawDataPointer();
      while (pixelsLeftToSend > 0) {
        RobotInterface::DisplayedFaceImageRGBChunk msg;
        msg.imageId = imageID;
        msg.chunkIndex = chunkCount++;
        msg.numPixels = std::min(kMaxPixelsPerMsg, pixelsLeftToSend);

        std::copy_n(startIt, msg.numPixels, std::begin(msg.faceData));

        pixelsLeftToSend -= msg.numPixels;
        std::advance(startIt, msg.numPixels);
        RobotInterface::SendAnimToEngine(msg);
      }
      imageID++;

      static const int kExpectedNumChunks = static_cast<int>(std::ceilf( (f32)FACE_DISPLAY_NUM_PIXELS / kMaxPixelsPerMsg ));
      DEV_ASSERT_MSG(chunkCount == kExpectedNumChunks, "AnimationComponent.DisplayFaceImage.UnexpectedNumChunks", "%d", chunkCount);
    }

    FaceDisplay::getInstance()->DrawToFace(faceImg565);
  }
  
  Result AnimationStreamer::EnableBackpackAnimationLayer(bool enable)
  {
    RobotInterface::BackpackSetLayer msg;
    
    if (enable && !_backpackAnimationLayerEnabled) {
      msg.layer = 1; // 1 == BPL_ANIMATION
      _backpackAnimationLayerEnabled = true;
    } else if (!enable && _backpackAnimationLayerEnabled) {
      msg.layer = 0; // 1 == BPL_USER
      _backpackAnimationLayerEnabled = false;
    } else {
      // Do nothing
      return RESULT_OK;
    }

    if (!RobotInterface::SendAnimToRobot(msg)) {
      return RESULT_FAIL;
    }

    return RESULT_OK;
  }
  
  Result AnimationStreamer::SendStartOfAnimation()
  {
    DEV_ASSERT(!_startOfAnimationSent, "AnimationStreamer.SendStartOfAnimation.AlreadySent");
    DEV_ASSERT(_streamingAnimation != nullptr, "AnimationStreamer.SendStartOfAnimation.NullAnim");
    const std::string& streamingAnimName = _streamingAnimation->GetName();

    if(DEBUG_ANIMATION_STREAMING) {
      PRINT_CH_DEBUG(kLogChannelName,
                     "AnimationStreamer.SendStartOfAnimation", "Tag=%d, Name=%s, loopCtr=%d",
                     _tag, streamingAnimName.c_str(), _loopCtr);
    }

    if (_loopCtr == 0) {
      // Don't actually send start message for proceduralFace or neutralFace anims since
      // they weren't requested by engine
      if (!_playingInternalAnim) {
        AnimationStarted startMsg;
        memcpy(startMsg.animName, streamingAnimName.c_str(), streamingAnimName.length());
        startMsg.animName_length = streamingAnimName.length();
        startMsg.tag = _tag;
        if (!RobotInterface::SendAnimToEngine(startMsg)) {
          return RESULT_FAIL;
        }
      }
    }
  
    _startOfAnimationSent = true;
    _endOfAnimationSent = false;
    
    if( ANKI_DEV_CHEATS ) {
      SendAnimationToWebViz( true );
    }

    return RESULT_OK;
  }
  
  // TODO: Is this actually being called at the right time?
  //       Need to call this after triggerTime+durationTime of last keyframe has expired.
  Result AnimationStreamer::SendEndOfAnimation(bool abortingAnim)
  {
    DEV_ASSERT(_startOfAnimationSent && !_endOfAnimationSent, "AnimationStreamer.SendEndOfAnimation.StartNotSentOrEndAlreadySent");
    DEV_ASSERT(_streamingAnimation != nullptr, "AnimationStreamer.SendStartOfAnimation.NullAnim");
    const std::string& streamingAnimName = _streamingAnimation->GetName();

    if(DEBUG_ANIMATION_STREAMING) {
      PRINT_CH_INFO(kLogChannelName,
                    "AnimationStreamer.SendEndOfAnimation", "Tag=%d, Name=%s, t=%dms, loopCtr=%d, numLoops=%d",
                    _tag, streamingAnimName.c_str(), _streamingTime_ms - _startTime_ms, _loopCtr, _numLoops);
    }
    
    if (abortingAnim || (_loopCtr == _numLoops - 1)) {
      // Don't actually send end message for proceduralFace or neutralFace anims since
      // they weren't requested by engine
      if (!_playingInternalAnim) {
        AnimationEnded endMsg;
        memcpy(endMsg.animName, streamingAnimName.c_str(), streamingAnimName.length());
        endMsg.animName_length = streamingAnimName.length();
        endMsg.tag = _tag;
        endMsg.wasAborted = abortingAnim;
        if (!RobotInterface::SendAnimToEngine(endMsg)) {
          return RESULT_FAIL;
        }
      }
    }
    
    _endOfAnimationSent = true;
    _startOfAnimationSent = false;
    
    if( ANKI_DEV_CHEATS ) {
      SendAnimationToWebViz( false );
    }
    
    // Every time we end an animation we should also re-enable BPL_USER layer on robot
    EnableBackpackAnimationLayer(false);
    
    return RESULT_OK;
  } // SendEndOfAnimation()


  Result AnimationStreamer::StreamLayers()
  {
    Result lastResult = RESULT_OK;
    
    // Add more stuff to send buffer from various layers
    if(_trackLayerComponent->HaveLayersToSend())
    {
      // We don't have an animation but we still have procedural layers to so
      // apply them
      TrackLayerComponent::LayeredKeyFrames layeredKeyFrames;
      _trackLayerComponent->ApplyLayersToAnim(nullptr,
                                              _startTime_ms,
                                              _streamingTime_ms,
                                              layeredKeyFrames,
                                              false);
      
      // If we have backpack keyframes to send
      if(layeredKeyFrames.haveBackpackKeyFrame)
      {
        if (SendIfTrackUnlocked(layeredKeyFrames.backpackKeyFrame.GetStreamMessage(), AnimTrackFlag::BACKPACK_LIGHTS_TRACK)) {
          EnableBackpackAnimationLayer(true);
        }
      }
      
      // If we have face keyframes to send
      const bool shouldDrawFaceLayer = BaseStationTimer::getInstance()->GetCurrentTimeStamp() > _nextProceduralFaceAllowedTime_ms;
      if(layeredKeyFrames.haveFaceKeyFrame && shouldDrawFaceLayer)
      {
        BufferFaceToSend(layeredKeyFrames.faceKeyFrame.GetFace());
      }
      
      // Increment fake "streaming" time, so we can evaluate below whether
      // it's time to stream out any of the other tracks. Note that it is still
      // relative to the same start time.
      _streamingTime_ms += ANIM_TIME_STEP_MS;
    }
    
    return lastResult;
  }// StreamLayers()
  
  
  Result AnimationStreamer::UpdateStream(Animation* anim, bool storeFace)
  {
    Result lastResult = RESULT_OK;
    
    if(!anim->IsInitialized()) {
      PRINT_NAMED_ERROR("Animation.Update", "%s: Animation must be initialized before it can be played/updated.",
                        anim != nullptr ? anim->GetName().c_str() : "<NULL>");
      return RESULT_FAIL;
    }
    
    const TimeStamp_t currTime_ms = BaseStationTimer::getInstance()->GetCurrentTimeStamp();
    
    // Grab references to some of the tracks
    // Some tracks are managed by the TrackLayerComponent
    auto & robotAudioTrack  = anim->GetTrack<RobotAudioKeyFrame>();
    auto & headTrack        = anim->GetTrack<HeadAngleKeyFrame>();
    auto & liftTrack        = anim->GetTrack<LiftHeightKeyFrame>();
    auto & bodyTrack        = anim->GetTrack<BodyMotionKeyFrame>();
    auto & recordHeadingTrack = anim->GetTrack<RecordHeadingKeyFrame>();
    auto & turnToRecordedHeadingTrack = anim->GetTrack<TurnToRecordedHeadingKeyFrame>();
    auto & eventTrack       = anim->GetTrack<EventKeyFrame>();
    auto & spriteSeqTrack    = anim->GetTrack<SpriteSequenceKeyFrame>();
    

    if(!_startOfAnimationSent) {
      SendStartOfAnimation();
      _animAudioClient->InitAnimation();
    }
    
    // Is it time to play robot audio?
    // TODO: Do it this way or with GetCurrentStreamingMessage() like all the other tracks?
    if (robotAudioTrack.HasFramesLeft() &&
        robotAudioTrack.GetCurrentKeyFrame().IsTimeToPlay(_startTime_ms, currTime_ms))
    {
      _animAudioClient->PlayAudioKeyFrame( robotAudioTrack.GetCurrentKeyFrame(), _context->GetRandom() );
      robotAudioTrack.MoveToNextKeyFrame();
    }

    // Send keyframes at appropriates times
    if (anim->HasFramesLeft())
    {
      
      if(DEBUG_ANIMATION_STREAMING) {
        // Very verbose!
        //PRINT_CH_INFO(kLogChannelName,
        //              "Animation.Update", "%d bytes left to send this Update.",
        //              numBytesToSend);
      }
      
      // Apply any track layers to the animation
      TrackLayerComponent::LayeredKeyFrames layeredKeyFrames;
      _trackLayerComponent->ApplyLayersToAnim(anim,
                                              _startTime_ms,
                                              _streamingTime_ms,
                                              layeredKeyFrames,
                                              storeFace);
            
#     if DEBUG_ANIMATION_STREAMING
#       define DEBUG_STREAM_KEYFRAME_MESSAGE(__KF_NAME__) \
                  PRINT_CH_INFO(kLogChannelName, \
                                "AnimationStreamer.UpdateStream", \
                                "Streaming %sKeyFrame at t=%dms.", __KF_NAME__, \
                                _streamingTime_ms - _startTime_ms)
#     else
#       define DEBUG_STREAM_KEYFRAME_MESSAGE(__KF_NAME__)
#     endif
      
      if(SendIfTrackUnlocked(headTrack.GetCurrentStreamingMessage(_startTime_ms, _streamingTime_ms), AnimTrackFlag::HEAD_TRACK)) {
        DEBUG_STREAM_KEYFRAME_MESSAGE("HeadAngle");
      }
      
      if(SendIfTrackUnlocked(liftTrack.GetCurrentStreamingMessage(_startTime_ms, _streamingTime_ms), AnimTrackFlag::LIFT_TRACK)) {
        DEBUG_STREAM_KEYFRAME_MESSAGE("LiftHeight");
      }

      // Send AnimationEvent directly up to engine if it's time to play one
      if (eventTrack.HasFramesLeft() &&
          eventTrack.GetCurrentKeyFrame().IsTimeToPlay(_startTime_ms, currTime_ms))
      {
        DEBUG_STREAM_KEYFRAME_MESSAGE("Event");
        
        // Get keyframe and send contents to engine
        auto eventKeyFrame = eventTrack.GetCurrentKeyFrame();

        AnimationEvent eventMsg;
        eventMsg.event_id = eventKeyFrame.GetAnimEvent();
        eventMsg.timestamp = currTime_ms;
        eventMsg.tag = _tag;
        RobotInterface::SendAnimToEngine(eventMsg);

        eventTrack.MoveToNextKeyFrame();
      }
      
      // Non-procedural faces (raw pixel data/images) take precedence over procedural faces (parameterized faces
      // like idles, keep alive, or normal animated faces)
      const bool shouldPlayFaceAnim = !IsTrackLocked((u8)AnimTrackFlag::FACE_IMAGE_TRACK) &&
                                      spriteSeqTrack.HasFramesLeft() &&
                                      spriteSeqTrack.GetCurrentKeyFrame().IsTimeToPlay(_streamingTime_ms - _startTime_ms);
      if(shouldPlayFaceAnim)
      {
        auto & faceKeyFrame = spriteSeqTrack.GetCurrentKeyFrame();
        const bool isGrayscale = faceKeyFrame.IsGrayscale();
        bool gotImage = false;
        if (isGrayscale) {
          Vision::Image faceGray;
          gotImage = faceKeyFrame.GetFaceImage(faceGray);
          if (gotImage) {
            const float scanlineOpacity = faceKeyFrame.GetScanlineOpacity();
            const bool applyScanlines = !Util::IsNear(scanlineOpacity, 1.f);
            
            // Create an HSV image from the gray image, replacing the 'hue'
            // channel with the current face hue
            const std::vector<cv::Mat> channels {
              ProceduralFace::GetHueImage().get_CvMat_(),
              ProceduralFace::GetSaturationImage().get_CvMat_(),
              faceGray.get_CvMat_()
            };
            static Vision::ImageRGB faceHSV;
            cv::merge(channels, faceHSV.get_CvMat_());
            
            if (applyScanlines) {
              ProceduralFaceDrawer::ApplyScanlines(faceHSV, scanlineOpacity);
            }
              
            // convert HSV -> RGB565
            faceHSV.ConvertHSV2RGB565(_faceDrawBuf);
          }
        } else {
          // Display the ImageRGB565 directly to the face, without modification
          gotImage = faceKeyFrame.GetFaceImage(_faceDrawBuf);
        }
        
        if (gotImage) {
          DEBUG_STREAM_KEYFRAME_MESSAGE("FaceAnimation");
          BufferFaceToSend(_faceDrawBuf);
          _nextProceduralFaceAllowedTime_ms = currTime_ms + kMinTimeBetweenLastNonProcFaceAndNextProcFace_ms;
        }

        if(faceKeyFrame.IsDone()) {
          faceKeyFrame.Reset();
          spriteSeqTrack.MoveToNextKeyFrame();
        }
      }
      else if(layeredKeyFrames.haveFaceKeyFrame)
      {
        BufferFaceToSend(layeredKeyFrames.faceKeyFrame.GetFace());
      }
      
      if(layeredKeyFrames.haveBackpackKeyFrame)
      {
        if (SendIfTrackUnlocked(layeredKeyFrames.backpackKeyFrame.GetStreamMessage(), AnimTrackFlag::BACKPACK_LIGHTS_TRACK)) {
          EnableBackpackAnimationLayer(true);
        }
      }
      
      if(SendIfTrackUnlocked(bodyTrack.GetCurrentStreamingMessage(_startTime_ms, _streamingTime_ms), AnimTrackFlag::BODY_TRACK)) {
        DEBUG_STREAM_KEYFRAME_MESSAGE("BodyMotion");
      }
      
      if(SendIfTrackUnlocked(recordHeadingTrack.GetCurrentStreamingMessage(_startTime_ms, _streamingTime_ms), AnimTrackFlag::BODY_TRACK)) {
        DEBUG_STREAM_KEYFRAME_MESSAGE("RecordHeading");
      }

      if(SendIfTrackUnlocked(turnToRecordedHeadingTrack.GetCurrentStreamingMessage(_startTime_ms, _streamingTime_ms), AnimTrackFlag::BODY_TRACK)) {
        DEBUG_STREAM_KEYFRAME_MESSAGE("TurnToRecordedHeading");
      }
      
#     undef DEBUG_STREAM_KEYFRAME_MESSAGE
      
      
      // Increment fake "streaming" time, so we can evaluate below whether
      // it's time to stream out any of the other tracks. Note that it is still
      // relative to the same start time.
      _streamingTime_ms += ANIM_TIME_STEP_MS;
      
    } // while(buffering frames)
    
    // Send an end-of-animation keyframe when done
    if( !anim->HasFramesLeft() &&
        _startOfAnimationSent &&
        !_endOfAnimationSent)
    {      
      StopTracksInUse();
      lastResult = SendEndOfAnimation();
      if (_animAudioClient->HasActiveEvents()) {
        PRINT_NAMED_WARNING("AnimationStreamer.UpdateStream.EndOfAnimation.ActiveAudioEvent",
                            "AnimName: '%s'", anim->GetName().c_str());
      }
    }
    
    return lastResult;
  } // UpdateStream()
  
  
  Result AnimationStreamer::Update()
  {
    Result lastResult = RESULT_OK;
    
    bool streamUpdated = false;
    
    // TODO: Send this data up to engine via some new CozmoAnimState message
    /*
    const CozmoContext* cozmoContext = robot.GetContext();
    VizManager* vizManager = robot.GetContext()->GetVizManager();
    DEV_ASSERT(nullptr != vizManager, "Expecting a non-null VizManager");

    // Update name in viz:
    if(nullptr == _streamingAnimation && nullptr == _idleAnimation)
    {
      vizManager->SetText(VizManager::ANIMATION_NAME, NamedColors::WHITE, "Anim: <none>");
      cozmoContext->SetSdkStatus(SdkStatusType::Anim, "None");
    } else if(nullptr != _streamingAnimation) {
      vizManager->SetText(VizManager::ANIMATION_NAME, NamedColors::WHITE, "Anim: %s",
                          _streamingAnimation->GetName().c_str());
      cozmoContext->SetSdkStatus(SdkStatusType::Anim, std::string(_streamingAnimation->GetName()));
    } else if(nullptr != _idleAnimation) {
      vizManager->SetText(VizManager::ANIMATION_NAME, NamedColors::WHITE, "Anim[Idle]: %s",
                          _idleAnimation->GetName().c_str());
      cozmoContext->SetSdkStatus(SdkStatusType::Anim, std::string("Idle:") + _idleAnimation->GetName());
    }
     */
    
    _trackLayerComponent->Update();
    
    // Always keep face alive, unless we have a streaming animation, since we rely on it
    // to do all face updating and we don't want to step on it's hand-designed toes.
    // Wait a 1/2 second before running after we finish the last streaming animation
    // to help reduce stepping on the next animation's toes when we have things
    // sequenced.
    // NOTE: lastStreamTime>0 check so that we don't start keeping face alive before
    //       first animation of any kind is sent.
    const bool haveStreamingAnimation = _streamingAnimation != nullptr;
    const bool haveStreamedAnything   = _lastStreamTime > 0.f;
    const bool longEnoughSinceStream  = (BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() - _lastStreamTime) > _longEnoughSinceLastStreamTimeout_s;
    
    if(!haveStreamingAnimation &&
       haveStreamedAnything &&
       longEnoughSinceStream)
    {
      // If we were interrupted from streaming an animation and we've met all the
      // conditions to even be in this function, then we should make sure we've
      // got neutral face back on the screen
      if(_wasAnimationInterruptedWithNothing) {
        SetStreamingAnimation(_neutralFaceAnimation, kNotAnimatingTag);
        _wasAnimationInterruptedWithNothing = false;
      }
      
      if(s_enableKeepFaceAlive && !FACTORY_TEST)
      {
        _trackLayerComponent->KeepFaceAlive(_keepFaceAliveParams);
      }
    }
    
    if(_streamingAnimation != nullptr) {
      
      if(IsFinished(_streamingAnimation)) {
        
        ++_loopCtr;
        
        if(_numLoops == 0 || _loopCtr < _numLoops) {
         if(DEBUG_ANIMATION_STREAMING) {
           PRINT_CH_INFO(kLogChannelName,
                         "AnimationStreamer.Update.Looping",
                         "Finished loop %d of %d of '%s' animation. Restarting.",
                         _loopCtr, _numLoops,
                         _streamingAnimation->GetName().c_str());
         }
          
          // Reset the animation so it can be played again:
          InitStream(_streamingAnimation, _tag);
          
          // To avoid streaming faceLayers set true and start streaming animation next Update() tick.
          streamUpdated = true;
        }
        else {
          if(DEBUG_ANIMATION_STREAMING) {
            PRINT_CH_INFO(kLogChannelName,
                          "AnimationStreamer.Update.FinishedStreaming",
                          "Finished streaming '%s' animation.",
                          _streamingAnimation->GetName().c_str());
          }
          
          _streamingAnimation = nullptr;
        }
        
      }
      else {
        // We do want to store this face to the robot since it's coming from an actual animation
        lastResult = UpdateStream(_streamingAnimation, true);
        streamUpdated = true;
        _lastStreamTime = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
      }
    } // if(_streamingAnimation != nullptr)
    


    
    // If we didn't do any streaming above, but we've still got layers to stream
    if(!streamUpdated)
    {
      if(_trackLayerComponent->HaveLayersToSend())
      {
        lastResult = StreamLayers();
      }
    }

    // Tick audio engine
    _animAudioClient->Update();
    

    // Send animState message
    if (--_numTicsToSendAnimState == 0) {
      const auto numKeyframes = _proceduralAnimation->GetTrack<SpriteSequenceKeyFrame>().TrackLength();

      AnimationState msg;
      msg.numProcAnimFaceKeyframes = static_cast<uint32_t>(numKeyframes);
      msg.lockedTracks             = _lockedTracks;
      msg.tracksInUse              = _tracksInUse;

      RobotInterface::SendAnimToEngine(msg);
      _numTicsToSendAnimState = kAnimStateReportingPeriod_tics;
    }


    return lastResult;
  } // AnimationStreamer::Update()
  
  void AnimationStreamer::EnableKeepFaceAlive(bool enable, u32 disableTimeout_ms)
  {
    if (s_enableKeepFaceAlive && !enable) {
      _trackLayerComponent->RemoveKeepFaceAlive(disableTimeout_ms);
    }
    s_enableKeepFaceAlive = enable;
  }
  
  void AnimationStreamer::SetDefaultKeepFaceAliveParams()
  {
    PRINT_CH_INFO(kLogChannelName, "AnimationStreamer.SetDefaultKeepFaceAliveParams", "");
    
    for(auto param = Util::EnumToUnderlying(KeepFaceAliveParameter::BlinkSpacingMinTime_ms);
        param != Util::EnumToUnderlying(KeepFaceAliveParameter::NumParameters); ++param) {
      SetParamToDefault(static_cast<KeepFaceAliveParameter>(param));
    }
  } // SetDefaultKeepFaceAliveParams()

  void AnimationStreamer::SetParamToDefault(KeepFaceAliveParameter whichParam)
  {
    SetParam(whichParam, _kDefaultKeepFaceAliveParams.at(whichParam));
  }
  
  const std::string AnimationStreamer::GetStreamingAnimationName() const
  {
    return _streamingAnimation ? _streamingAnimation->GetName() : "";
  }
  
  bool AnimationStreamer::IsFinished(Animation* anim) const
  {
    return _endOfAnimationSent && !anim->HasFramesLeft();
  }

  void AnimationStreamer::ResetKeepFaceAliveLastStreamTimeout()
  {
    _longEnoughSinceLastStreamTimeout_s = kDefaultLongEnoughSinceLastStreamTimeout_s;
  }
  

  void AnimationStreamer::StopTracks(const u8 whichTracks)
  {
    if(whichTracks)
    {
      if(whichTracks & (u8)AnimTrackFlag::HEAD_TRACK)
      {
        RobotInterface::MoveHead msg;
        msg.speed_rad_per_sec = 0;
        RobotInterface::SendAnimToRobot(std::move(msg));
      }
      
      if(whichTracks & (u8)AnimTrackFlag::LIFT_TRACK)
      {
        RobotInterface::MoveLift msg;
        msg.speed_rad_per_sec = 0;
        RobotInterface::SendAnimToRobot(std::move(msg));
      }
      
      if(whichTracks & (u8)AnimTrackFlag::BODY_TRACK)
      {
        RobotInterface::DriveWheels msg;
        msg.lwheel_speed_mmps = 0;
        msg.rwheel_speed_mmps = 0;
        msg.lwheel_accel_mmps2 = 0;
        msg.rwheel_accel_mmps2 = 0;
        RobotInterface::SendAnimToRobot(std::move(msg));
      }
      
      _tracksInUse &= ~whichTracks;
    }
  }
  
  template<typename ImageType>
  Result AnimationStreamer::SetFaceImageHelper(const ImageType& img, const u32 duration_ms, bool isGrayscale)
  {
    DEV_ASSERT(nullptr != _proceduralAnimation, "AnimationStreamer.SetFaceImage.NullProceduralAnimation");
    DEV_ASSERT(img.IsContinuous(), "AnimationStreamer.SetFaceImage.ImageIsNotContinuous");
    
    // Clear out any runtime sequences currently set on the procedural animation
    auto& spriteSeqTrack = _proceduralAnimation->GetTrack<SpriteSequenceKeyFrame>();
    spriteSeqTrack.Clear();
    
    // Trigger time of keyframe is 0 since we want it to start playing immediately
    SpriteSequenceKeyFrame kf(Vision::SpriteName::Count, true, true);
    kf.SetRuntimeSequenceIsGrayscale(isGrayscale);
    kf.AddFrameToRuntimeSequence(img);
    kf.SetFrameDuration_ms(duration_ms);
    Result result = _proceduralAnimation->AddKeyFrameToBack(kf);
    if(!(ANKI_VERIFY(RESULT_OK == result, "AnimationStreamer.SetFaceImage.FailedToAddKeyFrame", "")))
    {
      return result;
    }
    
    if (_streamingAnimation != _proceduralAnimation) {
      result = SetStreamingAnimation(_proceduralAnimation, 0);
    }
    return result;
  }
  
  void AnimationStreamer::SendAnimationToWebViz( bool starting ) const
  {
    if( _context != nullptr ) {
      auto* webService = _context->GetWebService();
      if( (webService != nullptr) && (_streamingAnimation != nullptr) ) {
        Json::Value data;
        data["type"] = starting ? "start" : "stop";
        data["animation"] = _streamingAnimation->GetName();
        webService->SendToWebViz("animations", data);
      }
    }
  }
  
  
} // namespace Cozmo
} // namespace Anki


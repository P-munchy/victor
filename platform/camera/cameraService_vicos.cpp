/**
 * File: cameraService_vicos.cpp
 *
 *
 * Author: chapados
 * Created: 02/07/2018
 *
 * based on androidHAL_mac.cpp
 * Author: Kevin Yoon
 * Created: 02/17/2017
 *
 * Description:
 *               Defines interface to a camera system provided by the OS/platform
 *
 * Copyright: Anki, Inc. 2017
 *
 **/

#include "camera/cameraService.h"
#include "util/helpers/templateHelpers.h"
#include "util/logging/logging.h"

#include "camera/vicos/camera_client/camera_client.h"

#include <vector>
#include <mutex>
#include <chrono>
#include <unistd.h>

#ifdef SIMULATOR
#error SIMULATOR should NOT be defined by any target using cameraService_vicos.cpp
#endif

#define LOG_CHANNEL "CameraService"

namespace Anki {
  namespace Vector {

    namespace { // "Private members"
      struct anki_camera_handle* _camera = nullptr;
      bool     _isRestartingCamera = false;
      std::mutex _lock;
      std::function<void()> _onCameraRestart;
      
      bool _waitingForFormatChange = false;
      ImageEncoding _curFormat = ImageEncoding::NoneImageEncoding;

      enum class CameraPowerState {
        Off,
        WaitingToInit,
        Running,
        WaitingToDelete,
      };

      CameraPowerState _powerState = CameraPowerState::Off;
    } // "private" namespace


#pragma mark --- Hardware Method Implementations ---

    // Definition of static field
    CameraService* CameraService::_instance = nullptr;

    /**
     * Returns the single instance of the object.
     */
    CameraService* CameraService::getInstance() {
      // check if the instance has been created yet
      if(nullptr == _instance) {
        // if not, then create it
        _instance = new CameraService;
      }
      // return the single instance
      return _instance;
    }

    /**
     * Removes instance
     */
    void CameraService::removeInstance() {
      Util::SafeDelete(_instance);
    };

    CameraService::CameraService()
    : _timeOffset(std::chrono::steady_clock::now())
    , _imageFrameID(1)
    {
      InitCamera();
    }

    CameraService::~CameraService()
    {
      DeleteCamera();
    }

    void CameraService::RegisterOnCameraRestartCallback(std::function<void()> callback)
    {
      if(_onCameraRestart != nullptr)
      {
        LOG_WARNING("CameraService.RegisterOnCameraRestartCallback.Failed",
                    "Already have callback");
        return;
      }
      _onCameraRestart = callback;
    }

    bool IsCameraReady()
    {
      return (_camera != NULL &&
              _powerState == CameraPowerState::Running);
    }

    Result CameraService::InitCamera()
    {
      std::lock_guard<std::mutex> lock(_lock);

      anki_camera_status_t status = camera_status(_camera);
      if(status == ANKI_CAMERA_STATUS_RUNNING &&
         _powerState == CameraPowerState::Running)
      {
        LOG_INFO("CameraService.InitCamera.AlreadyInited", "");
        
        return RESULT_OK;
      }
      else if(status != ANKI_CAMERA_STATUS_OFFLINE ||
              _powerState != CameraPowerState::Off)
      {
        LOG_WARNING("CameraService.InitCamera.CameraStillRunning",
                    "Camera is in state %d, power state %d",
                    status,
                    _powerState);
        
        return RESULT_FAIL;
      }
      
      LOG_INFO("CameraService.InitCamera.StartingInit", "");

      _powerState = CameraPowerState::WaitingToInit;
      
      int rc = camera_init(&_camera);
      if (rc != 0) {
        LOG_ERROR("CameraService.InitCamera.CameraInitFailed", "camera_init error %d", rc);
        _powerState = CameraPowerState::Off;
        return RESULT_FAIL;
      }

      rc = camera_start(_camera);
      if (rc != 0) {
        LOG_ERROR("CameraService.InitCamera.CameraStartFailed", "camera_start error %d", rc);
        _powerState = CameraPowerState::Off;
        return RESULT_FAIL;
      }

      return RESULT_OK;
    }

    Result CameraService::DeleteCamera()
    {
      std::lock_guard<std::mutex> lock(_lock);

      if(_camera == NULL ||
         _powerState == CameraPowerState::Off)
      {
        LOG_INFO("CameraService.DeleteCamera.AlreadyDeleted", "");
        return RESULT_OK;
      }
      else if(_powerState != CameraPowerState::Running)
      {
        LOG_WARNING("CameraService.DeleteCamera.CameraNotRunning", "");
        return RESULT_FAIL;
      }

      _powerState = CameraPowerState::WaitingToDelete;
      
      int res = camera_stop(_camera);
      if (res != 0) {
        LOG_ERROR("CameraService.DeleteCamera.CameraStopFailed", "camera_stop error %d", res);
        _powerState = CameraPowerState::Running;
        return RESULT_FAIL;
      }

      res = camera_release(_camera);
      if (res != 0) {
        LOG_ERROR("CameraService.DeleteCamera.CameraReleaseFailed", "camera_release error %d", res);
        _powerState = CameraPowerState::Running;
        return RESULT_FAIL;
      }

      return RESULT_OK;
    }

    Result CameraService::Update()
    {
      //
      // Check camera_client status and re-init / re-start if necessary
      //
      if (_camera == NULL) {
        return RESULT_OK;
      }

      // Ask the camera if it has successfully stopped/released itself
      if(_powerState == CameraPowerState::WaitingToDelete)
      {
        if(camera_destroy(_camera))
        {
          _powerState = CameraPowerState::Off;
          _camera = NULL;
        }
        
        return RESULT_OK;
      }

      int rc = 0;
      anki_camera_status_t status = camera_status(_camera);
      
      if(_powerState == CameraPowerState::WaitingToInit)
      {
        if(status == ANKI_CAMERA_STATUS_RUNNING)
        {
          _powerState = CameraPowerState::Running;
        }
        
        return RESULT_OK;
      }

      
      if (_isRestartingCamera && (status == ANKI_CAMERA_STATUS_RUNNING))
      {
        LOG_INFO("CameraService.Update.RestartedCameraClient", "");

        _isRestartingCamera = false;
        _waitingForFormatChange = false;
        _curFormat = ImageEncoding::NoneImageEncoding;

        if(_onCameraRestart != nullptr)
        {
          _onCameraRestart();
        }
      }

      if (status != ANKI_CAMERA_STATUS_RUNNING)
      {
        _isRestartingCamera = true;
        
        if (status == ANKI_CAMERA_STATUS_OFFLINE)
        {
          LOG_INFO("CameraService.Update.Offline",
                   "Camera is offline, re-initing");

          rc = camera_init(&_camera);
          status = camera_status(_camera);
        }

        if((rc == 0) && (status == ANKI_CAMERA_STATUS_IDLE))
        {
          LOG_INFO("CameraService.Update.Idle",
                   "Camera is idle, restarting");

          rc = camera_start(_camera);
          status = camera_status(_camera);
        }
      }
      
      return (rc == 0 ? RESULT_OK : RESULT_FAIL);
    }

    TimeStamp_t CameraService::GetTimeStamp(void)
    {
      auto currTime = std::chrono::steady_clock::now();
      return static_cast<TimeStamp_t>(std::chrono::duration_cast<std::chrono::milliseconds>(currTime.time_since_epoch()).count());
    }

    void CameraService::CameraSetParameters(u16 exposure_ms, f32 gain)
    {
      if(!IsCameraReady()) {
        return;
      }

      if(_waitingForFormatChange)
      {
        PRINT_NAMED_INFO("CameraService.CameraSetParameters.FormatChanging",
                         "Not setting exposure and gain while format is changing");
        return;
      }

      camera_set_exposure(_camera, exposure_ms, gain);
    }

    void CameraService::CameraSetWhiteBalanceParameters(f32 r_gain, f32 g_gain, f32 b_gain)
    {
      if(!IsCameraReady()) {
        return;
      }
      
      if(_waitingForFormatChange)
      {
        PRINT_NAMED_INFO("CameraService.CameraSetWhiteBalanceParameters.FormatChanging",
                         "Not setting white balance while format is changing");
        return;
      }

      
      camera_set_awb(_camera, r_gain, g_gain, b_gain);
    }

    void CameraService::CameraSetCaptureFormat(ImageEncoding format)
    {
      if(!IsCameraReady()) {
        return;
      }

      anki_camera_pixel_format_t cameraFormat;
      switch(format)
      {
        case ImageEncoding::YUV420sp:
          cameraFormat = ANKI_CAM_FORMAT_YUV;
          break;
        case ImageEncoding::RawRGB:
          cameraFormat = ANKI_CAM_FORMAT_RGB888;
          break;
        case ImageEncoding::BAYER:
          cameraFormat = ANKI_CAM_FORMAT_BAYER_MIPI_BGGR10;
          break;
        default:
          PRINT_NAMED_WARNING("CameraService.CameraSetCaptureFormat.UnsupportedFormat",
                              "%s", EnumToString(format));
          return;
      }
      _waitingForFormatChange = true;
      PRINT_NAMED_INFO("CameraService.CameraSetCaptureFormat.SetFormat","%s", EnumToString(format));
      camera_set_capture_format(_camera, cameraFormat);
    }

    void CameraService::CameraSetCaptureSnapshot(bool start)
    {
      if(!IsCameraReady())
      {
        return;
      }
      
      PRINT_NAMED_INFO("CameraService.CameraSetCaptureSnapshot",
                       "%s snapshot mode",
                       (start ? "Starting" : "Stopping"));
      camera_set_capture_snapshot(_camera, start);
    }
    
    bool CameraService::CameraGetFrame(u8*& frame, u32& imageID, TimeStamp_t& imageCaptureSystemTimestamp_ms, ImageEncoding& format)
    {
      if(!IsCameraReady()) {
        return false;
      }

      std::lock_guard<std::mutex> lock(_lock);
      anki_camera_frame_t* capture_frame = NULL;
      int rc = camera_frame_acquire(_camera, &capture_frame);
      if (rc != 0) {
        return false;
      }

      if (capture_frame->timestamp != 0) {
        // Frame timestamp is nanoseconds of uptime (based on CLOCK_MONOTONIC)
        // Calculate an offset to convert to TimeStamp_t time base
        struct timespec now_ts = {0,0};
        clock_gettime(CLOCK_MONOTONIC, &now_ts);
        const uint64_t now_ns = (now_ts.tv_nsec + now_ts.tv_sec*1000000000LL);
        const uint64_t offset_ns = now_ns - capture_frame->timestamp;

        // Apply offset
        const TimeStamp_t now_ms = GetTimeStamp();
        const TimeStamp_t frame_time_ms = now_ms - static_cast<TimeStamp_t>(offset_ns / 1000000LL);

        imageCaptureSystemTimestamp_ms = frame_time_ms;
      } else {
        imageCaptureSystemTimestamp_ms = GetTimeStamp();
      }

      imageID = capture_frame->frame_id;
      _imageFrameID = imageID;
      frame = capture_frame->data;

      switch(capture_frame->format)
      {
        case ANKI_CAM_FORMAT_BAYER_MIPI_BGGR10:
          format = ImageEncoding::BAYER;
          break;
        case ANKI_CAM_FORMAT_RGB888:
          format = ImageEncoding::RawRGB;
          break;
        case ANKI_CAM_FORMAT_YUV:
          format = ImageEncoding::YUV420sp;
          break;
      }

      if(_curFormat != format)
      {
        _waitingForFormatChange = false;
        _curFormat = format;
      }
      
      return true;
    } // CameraGetFrame()

    bool CameraService::CameraReleaseFrame(u32 imageID)
    {
      if(!IsCameraReady()) {
        return false;
      }

      std::lock_guard<std::mutex> lock(_lock);
      int rc = camera_frame_release(_camera, imageID);
      return (rc == 0);
    }
  } // namespace Vector
} // namespace Anki

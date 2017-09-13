/**
 * File: androidHAL.h
 *
 * Author: Kevin Yoon
 * Created: 02/17/2017
 *
 * Description:
 *               Defines interface to all hardware accessible from Android
 *
 * Copyright: Anki, Inc. 2017
 *
 **/

#ifndef ANKI_COZMO_ANDROID_HARDWAREINTERFACE_H
#define ANKI_COZMO_ANDROID_HARDWAREINTERFACE_H
#include "anki/common/types.h"
#include "anki/cozmo/shared/cozmoConfig.h"
#include "clad/types/imageTypes.h"
#include "clad/types/cameraParams.h"

// Forward declaration
namespace webots {
  class Supervisor;
}
class ASensorManager;
class ASensor;
class ASensorEventQueue;
class ALooper;
class NativeCamera;
class ImageReader;


namespace Anki
{
  namespace Cozmo
  {
    class AndroidHAL
    {
    public:

      // Method to fetch singleton instance.
      static AndroidHAL* getInstance();
      
      // Removes instance
      static void removeInstance();
      
      // Dtor
      ~AndroidHAL();
      
      // TODO: Is this necessary?
      TimeStamp_t GetTimeStamp();
      
      Result Update();
  
#ifdef SIMULATOR
      // NOTE: Only NVStorageComponent::LoadSimData() should call this function.
      //       Everyone else should be getting CameraCalibration data from NVStorageComponent!
      const CameraCalibration* GetHeadCamInfo();
      
      // Assign Webots supervisor
      // Must do this before creating AndroidHAL for the first time
      static void SetSupervisor(webots::Supervisor *sup);
#endif

// #pragma mark --- IMU ---
      /////////////////////////////////////////////////////////////////////
      // Inertial measurement unit (IMU)
      //

      // IMU_DataStructure contains 3-axis acceleration and 3-axis gyro data
      struct IMU_DataStructure
      {
        void Reset() {
          acc_x = acc_y = acc_z = 0;
          rate_x = rate_y = rate_z = 0;
        }
        
        f32 acc_x;      // mm/s/s
        f32 acc_y;
        f32 acc_z;
        f32 rate_x;     // rad/s
        f32 rate_y;
        f32 rate_z;
      };

      // Read acceleration and rate
      // x-axis points out cozmo's face
      // y-axis points out of cozmo's left
      // z-axis points out the top of cozmo's head
      bool IMUReadData(IMU_DataStructure &IMUData);


// #pragma mark --- Cameras ---
      /////////////////////////////////////////////////////////////////////
      // CAMERAS
      // TODO: Add functions for adjusting ROI of cameras?
      //
      
      void InitCamera();
      
      void CameraGetParameters(DefaultCameraParams& params);

      // Sets the camera parameters (non-blocking call)
      void CameraSetParameters(u16 exposure_ms, f32 gain);

      // Points provided frame to a buffer of image data if available
      // Returns true if image available
      // TODO: How fast will this be in hardware? Is image ready and waiting?
      bool CameraGetFrame(u8*& frame, u32& imageID, std::vector<ImageImuData>& imuData);

      ImageResolution CameraGetResolution() const {return _imageCaptureResolution;}
      
    private:

      AndroidHAL();
      static AndroidHAL* _instance;

      void InitIMU();
      void ProcessIMUEvents();
      

      void DeleteCamera();
      
#ifdef SIMULATOR
      CameraCalibration headCamInfo_;
#else
      
      // Time
      std::chrono::steady_clock::time_point _timeOffset;
      
      
      // Android sensor (i.e. IMU)
      ASensorManager*    _sensorManager;
      const ASensor*     _accelerometer;
      const ASensor*     _gyroscope;
      ASensorEventQueue* _sensorEventQueue;
      ALooper*           _looper;
      
      static constexpr int SENSOR_REFRESH_RATE_HZ = 16;
      static constexpr int SENSOR_REFRESH_PERIOD_US = 1000000 / SENSOR_REFRESH_RATE_HZ;
      
      // Camera
      NativeCamera*   _androidCamera;
      ImageReader*    _reader;
#endif
      
      // Camera
      ImageResolution _imageCaptureResolution = ImageResolution::QVGA;
      u32             _imageFrameID;
      
      
    }; // class AndroidHAL
    
  } // namespace Cozmo
} // namespace Anki

#endif // ANKI_COZMO_ANDROID_HARDWAREINTERFACE_H

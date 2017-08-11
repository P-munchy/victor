/**
 * File: sim_androidHAL.cpp
 *
 * Author: Kevin Yoon
 * Created: 02/17/2017
 *
 * Description:
 *               Defines interface to all simulated hardware accessible from Android
 *
 * Copyright: Anki, Inc. 2017
 *
 **/

#include "androidHAL/androidHAL.h"
#include "util/logging/logging.h"

#include "util/random/randomGenerator.h"

#include <vector>

#include <webots/Supervisor.hpp>
#include <webots/Camera.hpp>
#include <webots/Display.hpp>
#include <webots/Gyro.hpp>
#include <webots/Accelerometer.hpp>

#define BLUR_CAPTURED_IMAGES 1

#if BLUR_CAPTURED_IMAGES
#include "opencv2/imgproc/imgproc.hpp"
#endif

#ifndef SIMULATOR
#error SIMULATOR should be defined by any target using sim_androidHAL.cpp
#endif

namespace Anki {
  namespace Cozmo {
    
    webots::Supervisor* _engineSupervisor = nullptr;
    
    namespace { // "Private members"

      // Const paramters / settings
      const u32 VISION_TIME_STEP = 65; // This should be a multiple of the world's basic time step!

      // Cameras / Vision Processing
      webots::Camera* headCam_;
      
      // IMU
      webots::Gyro* gyro_;
      webots::Accelerometer* accel_;

      // Face display
      webots::Display* face_;
      
      // Lens distortion
      const bool kUseLensDistortion = true;
      const f32 kRadialDistCoeff1     = -0.07178328295562293f;
      const f32 kRadialDistCoeff2     =  -0.2195788148163958f;
      const f32 kRadialDistCoeff3     = 0.13393879360293f;
      const f32 kTangentialDistCoeff1 = 0.001433240008548796f;
      const f32 kTangentialDistCoeff2 = 0.001523473592445885f;
      const f32 kDistCoeffNoiseFrac = 0.0f; // fraction of the true value to use for uniformly distributed noise (0 to disable)
      
      
      //Calibrated values
//      const f32 kRadialDistCoeff1     = -0.01195035398831179;
//      const f32 kRadialDistCoeff2     = -0.002578186894213538;
//      const f32 kRadialDistCoeff3     = -0.003164561938733199;
//      const f32 kTangentialDistCoeff1 = 0.00251398322942893;
//      const f32 kTangentialDistCoeff2 =  0.001282303969883647;
      
    } // "private" namespace


#pragma mark --- Simulated Hardware Method Implementations ---
    
    // Declarations
    void FillCameraInfo(const webots::Camera *camera, CameraCalibration &info);
    
    
    // Definition of static field
    AndroidHAL* AndroidHAL::_instance = 0;
    
    /**
     * Returns the single instance of the object.
     */
    AndroidHAL* AndroidHAL::getInstance() {
      // check if the instance has been created yet
      if(0 == _instance) {
        // if not, then create it
        _instance = new AndroidHAL;
      }
      // return the single instance
      return _instance;
    }
    
    /**
     * Removes instance
     */
    void AndroidHAL::removeInstance() {
      // check if the instance has been created yet
      if(0 != _instance) {
        delete _instance;
        _instance = 0;
      }
    };
    
    void AndroidHAL::SetSupervisor(webots::Supervisor *sup)
    {
      _engineSupervisor = sup;
    }
    
    AndroidHAL::AndroidHAL()
    {
      // Did you remember to call SetSupervisor()?
      DEV_ASSERT(_engineSupervisor != nullptr, "sim_androidHAL.NullWebotsSupervisor");
      
      // Is the step time defined in the world file >= than the robot time? It should be!
      DEV_ASSERT(TIME_STEP >= _engineSupervisor->getBasicTimeStep(), "sim_androidHAL.UnexpectedTimeStep");

      headCam_ = _engineSupervisor->getCamera("HeadCamera");

      if(VISION_TIME_STEP % static_cast<u32>(_engineSupervisor->getBasicTimeStep()) != 0) {
        PRINT_NAMED_WARNING("sim_androidHAL.InvalidVisionTimeStep",
                            "VISION_TIME_STEP (%d) must be a multiple of the world's basic timestep (%.0f).",
                            VISION_TIME_STEP, _engineSupervisor->getBasicTimeStep());
        return;
      }
      headCam_->enable(VISION_TIME_STEP);
      FillCameraInfo(headCam_, headCamInfo_);

      // Gyro
      gyro_ = _engineSupervisor->getGyro("gyro");
      gyro_->enable(TIME_STEP);

      // Accelerometer
      accel_ = _engineSupervisor->getAccelerometer("accel");
      accel_->enable(TIME_STEP);

      // Face display
      face_ = _engineSupervisor->getDisplay("face_display");
      assert(face_->getWidth() == FACE_DISPLAY_WIDTH);
      assert(face_->getHeight() == FACE_DISPLAY_HEIGHT);
      face_->setFont("Lucida Console", 8, true);
      FaceClear();
    }

    AndroidHAL::~AndroidHAL() {
      
    }
    

    TimeStamp_t AndroidHAL::GetTimeStamp(void)
    {
      return static_cast<TimeStamp_t>(_engineSupervisor->getTime() * 1000.0);
    }

    // TODO: If we want higher resolution IMU data than this we need to change this
    //       function and tic the supervisor at a faster rate than engine.
    bool AndroidHAL::IMUReadData(IMU_DataStructure &IMUData)
    {
      const double* vals = gyro_->getValues();  // rad/s
      IMUData.rate_x = (f32)(vals[0]);
      IMUData.rate_y = (f32)(vals[1]);
      IMUData.rate_z = (f32)(vals[2]);

      vals = accel_->getValues();   // m/s^2
      IMUData.acc_x = (f32)(vals[0] * 1000);  // convert to mm/s^2
      IMUData.acc_y = (f32)(vals[1] * 1000);
      IMUData.acc_z = (f32)(vals[2] * 1000);
      
      // Return true if IMU was already read this timestamp
      static TimeStamp_t lastReadTimestamp = 0;
      bool newReading = lastReadTimestamp != GetTimeStamp();
      lastReadTimestamp = GetTimeStamp();
      return newReading;
    }
    

    Result AndroidHAL::Update()
    {

      if(_engineSupervisor->step(Cozmo::TIME_STEP) == -1) {
        return RESULT_FAIL;
      } else {
        //AudioUpdate();
        return RESULT_OK;
      }

    } // step()


    // Helper function to create a CameraInfo struct from Webots camera properties:
    void FillCameraInfo(const webots::Camera *camera,
                        CameraCalibration &info)
    {

      const u16 nrows  = static_cast<u16>(camera->getHeight());
      const u16 ncols  = static_cast<u16>(camera->getWidth());
      const f32 width  = static_cast<f32>(ncols);
      const f32 height = static_cast<f32>(nrows);
      //f32 aspect = width/height;

      const f32 fov_hor = camera->getFov();

      // Compute focal length from simulated camera's reported FOV:
      const f32 f = width / (2.f * std::tan(0.5f*fov_hor));

      // There should only be ONE focal length, because simulated pixels are
      // square, so no need to compute/define a separate fy
      //f32 fy = height / (2.f * std::tan(0.5f*fov_ver));

      info.focalLength_x = f;
      info.focalLength_y = f;
      info.center_x      = 0.5f*(width-1);
      info.center_y      = 0.5f*(height-1);
      info.skew          = 0.f;
      info.nrows         = nrows;
      info.ncols         = ncols;
      info.distCoeffs.fill(0.f);
      
      if(kUseLensDistortion)
      {
        info.distCoeffs[0] = kRadialDistCoeff1;
        info.distCoeffs[1] = kRadialDistCoeff2;
        info.distCoeffs[2] = kTangentialDistCoeff1;
        info.distCoeffs[3] = kTangentialDistCoeff2;
        info.distCoeffs[4] = kRadialDistCoeff3;
        
        if(Util::IsFltGTZero(kDistCoeffNoiseFrac))
        {
          // Simulate not having perfectly calibrated distortion coefficients
          static Util::RandomGenerator rng(0);
          for(s32 i=0; i<5; ++i) {
            info.distCoeffs[i] *= rng.RandDblInRange(1.f-kDistCoeffNoiseFrac, 1.f+kDistCoeffNoiseFrac);
          }
        }
      }
    } // FillCameraInfo

    const CameraCalibration* AndroidHAL::GetHeadCamInfo(void)
    {
      return &headCamInfo_;
    }

    
    void AndroidHAL::CameraGetParameters(DefaultCameraParams& params)
    {
      params.minExposure_ms = 0;
      params.maxExposure_ms = 67;
      params.gain = 2.f;
      params.maxGain = 4.f;
      
      u8 count = 0;
      for(u8 i = 0; i < static_cast<u8>(CameraConstants::GAMMA_CURVE_SIZE); ++i)
      {
        params.gammaCurve[i] = count;
        count += 255/static_cast<u8>(CameraConstants::GAMMA_CURVE_SIZE);
      }
      
      return;
    }

    void AndroidHAL::CameraSetParameters(u16 exposure_ms, f32 gain)
    {
      // Can't control simulated camera's exposure.

      // TODO: Simulate this somehow?

      return;

    } // HAL::CameraSetParameters()


    // Starts camera frame synchronization
    bool AndroidHAL::CameraGetFrame(u8* frame, u32& imageID, std::vector<ImageImuData>& imuData )
    {
      DEV_ASSERT(frame != NULL, "sim_androidHAL.CameraGetFrame.NullFramePointer");
      
      const u8* image = headCam_->getImage();
      DEV_ASSERT(image != NULL, "sim_androidHAL.CameraGetFrame.NullImagePointer");

      s32 pixel = 0;
      s32 imgWidth = headCam_->getWidth();
      for (s32 y=0; y < headCamInfo_.nrows; y++) {
        for (s32 x=0; x < headCamInfo_.ncols; x++) {
          frame[pixel++] = webots::Camera::imageGetRed(image, imgWidth, x, y);
          frame[pixel++] = webots::Camera::imageGetGreen(image, imgWidth, x, y);
          frame[pixel++] = webots::Camera::imageGetBlue(image,  imgWidth, x, y);
        }
      }

      if(kUseLensDistortion)
      {
        // Apply radial/lens distortion. Note that cv::remap uses in inverse lookup to find where the pixels in
        // the output (distorted) image came from in the source. So we have to compute the inverse distortion here.
        // We do that using cv::undistortPoints to create the necessary x/y maps for remap:
        static cv::Mat_<f32> x_undistorted, y_undistorted;
        if(x_undistorted.empty())
        {
          // Compute distortion maps on first use
          std::vector<cv::Point2f> points;
          points.reserve(headCamInfo_.nrows * headCamInfo_.ncols);
          
          for (s32 i=0; i < headCamInfo_.nrows; i++) {
            for (s32 j=0; j < headCamInfo_.ncols; j++) {
              points.emplace_back(j,i);
            }
          }
          
          const std::vector<f32> distCoeffs{
            kRadialDistCoeff1, kRadialDistCoeff2, kTangentialDistCoeff1, kTangentialDistCoeff2, kRadialDistCoeff3
          };
          const cv::Matx<f32,3,3> cameraMatrix(headCamInfo_.focalLength_x, 0.f, headCamInfo_.center_x,
                                               0.f, headCamInfo_.focalLength_y, headCamInfo_.center_y,
                                               0.f, 0.f, 1.f);
          
          cv::undistortPoints(points, points, cameraMatrix, distCoeffs, cv::noArray(), cameraMatrix);
          
          x_undistorted.create(headCamInfo_.nrows, headCamInfo_.ncols);
          y_undistorted.create(headCamInfo_.nrows, headCamInfo_.ncols);
          std::vector<cv::Point2f>::const_iterator pointIter = points.begin();
          for (s32 i=0; i < headCamInfo_.nrows; i++)
          {
            f32* x_i = x_undistorted.ptr<f32>(i);
            f32* y_i = y_undistorted.ptr<f32>(i);
            
            for (s32 j=0; j < headCamInfo_.ncols; j++)
            {
              x_i[j] = pointIter->x;
              y_i[j] = pointIter->y;
              ++pointIter;
            }
          }
        }
        cv::Mat  cvFrame(headCamInfo_.nrows, headCamInfo_.ncols, CV_8UC3, frame);
        cv::remap(cvFrame, cvFrame, x_undistorted, y_undistorted, CV_INTER_LINEAR);
      }
      
      if(BLUR_CAPTURED_IMAGES)
      {
        // Add some blur to simulated images
        cv::Mat cvImg(headCamInfo_.nrows, headCamInfo_.ncols, CV_8UC3, frame);
        cv::GaussianBlur(cvImg, cvImg, cv::Size(0,0), 0.75f);
      }
      
      // Return a few pieces of ImageImuData.
      // Webots camera has no global shutter so sending the current IMU values
      // for all ImageImuData messages should be sufficient.
      IMU_DataStructure imu;
      IMUReadData(imu);
      
      ImageImuData data;
      data.imageId = _imageFrameID;
      data.rateX = imu.rate_x;
      data.rateY = imu.rate_y;
      data.rateZ = imu.rate_z;
      
      // IMU data point for middle of this image
      // See sim_hal::IMUGetCameraTime() for explanation of line2Number
      data.line2Number = 125;
      imuData.push_back(data);
      
      // Include IMU data for beginning of the next image (for rolling shutter correction purposes)
      data.imageId = _imageFrameID + 1;
      data.line2Number = 1;
      imuData.push_back(data);

      imageID = _imageFrameID;
      _imageFrameID++;
      
      return true;

    } // CameraGetFrame()
    
    void AndroidHAL::FaceClear()
    {
      face_->setColor(0);
      face_->fillRectangle(0,0, FACE_DISPLAY_WIDTH, FACE_DISPLAY_HEIGHT);
    }
    
    void AndroidHAL::FaceDraw(u16* frame)
    {
      const u16 Rmask = 0xf800;
      const u16 Gmask = 0x07e0;
      const u16 Bmask = 0x001f;
      const u16 Rshift = 8;
      const u16 Gshift = 5;
      const u16 Bshift = 3;
      
      for (u8 i = 0; i < FACE_DISPLAY_HEIGHT; ++i) {
        for (u8 j = 0; j < FACE_DISPLAY_WIDTH; ++j) {
          
          int color = ((*frame & Rmask) << Rshift) +
                      ((*frame & Gmask) << Gshift) +
                      ((*frame & Bmask) << Bshift);
          ++frame;
          
          face_->setColor(color);
          face_->drawPixel(j, i);
        }
      }
    }
    
    void AndroidHAL::FacePrintf(const char* format, ...)
    {
      // TODO: Smartly insert line breaks?

      face_->setColor(0xf0ff);
     
      #define MAX_FACE_DISPLAY_CHAR_LENGTH 30
      char line[MAX_FACE_DISPLAY_CHAR_LENGTH];
      
      va_list argptr;
      va_start(argptr, format);
      vsnprintf(line, MAX_FACE_DISPLAY_CHAR_LENGTH, format, argptr);
      va_end(argptr);
      
      face_->drawText(std::string(line), 0, 0);
    }
  
  } // namespace Cozmo
} // namespace Anki

/**
 * File: visionProcessingThread.h
 *
 * Author: Andrew Stein
 * Date:   11/20/2014
 *
 * Description: Container for the thread containing the basestation vision
 *              system, which provides methods for managing and communicating
 *              with it.
 *
 * Copyright: Anki, Inc. 2014
 **/

#ifndef ANKI_COZMO_BASESTATION_VISION_PROC_THREAD_H
#define ANKI_COZMO_BASESTATION_VISION_PROC_THREAD_H

#include "anki/cozmo/basestation/messages.h"

#include "anki/vision/basestation/cameraCalibration.h"
#include "anki/vision/basestation/image.h"
#include "anki/vision/basestation/visionMarker.h"

#include <thread>

namespace Anki {  
namespace Cozmo {
  
  // Forward declaration
  class VisionSystem;
  
  class VisionProcessingThread
  {
  public:
    
    VisionProcessingThread();
    ~VisionProcessingThread();
    
    //
    // Asynchronous operation
    //
    void Start(Vision::CameraCalibration& camCalib);
    void Stop();
    
    void SetNextImage(const Vision::Image& image,
                      const MessageRobotState& robotState);
    
    //
    // Synchronous operation
    //
    void SetCameraCalibration(Vision::CameraCalibration& camCalib);
    
    void Update(const Vision::Image& image,
                const MessageRobotState& robotState);

    
    void SetMarkerToTrack(const Vision::Marker::Code&  markerToTrack,
                          const f32                    markerWidth_mm,
                          const Point2f&               imageCenter,
                          const f32                    radius,
                          const bool                   checkAngleX);
    
    // Enable/disable different types of processing
    void EnableMarkerDetection(bool tf);
    void EnableFaceDetection(bool tf);
    
    // True if marker detection has completed since last call to
    // SetNextImage(). Use this to differentiate whether the VisionMarker
    // mailbox is empty because there were no markers detected in the last
    // image or because marker detection has not completed on the last image
    // yet.
    bool WasLastImageProcessed() const;
    
    // These return true if a mailbox messages was available, and they copy
    // that message into the passed-in message struct.
    //bool CheckMailbox(ImageChunk&          msg);
    bool CheckMailbox(MessageDockingErrorSignal&  msg);
    bool CheckMailbox(MessageFaceDetection&       msg);
    bool CheckMailbox(MessageVisionMarker&        msg);
    bool CheckMailbox(MessageTrackerQuad&         msg);
    bool CheckMailbox(MessagePanAndTiltHead&      msg);
    
    // If the current image is newer than the specified timestamp, copy it into
    // the given img and return true.
    bool GetCurrentImage(Vision::Image& img, TimeStamp_t newerThanTimestamp);
    
    bool GetLastProcessedImage(Vision::Image& img, TimeStamp_t newerThanTimestamp);
    
  protected:
    
    VisionSystem* _visionSystem;
    
    Vision::CameraCalibration _camCalib;
    bool   _isCamCalibSet;
    
    bool   _running;
    bool   _isLocked; // mutex for setting image and state
    bool   _wasLastImageProcessed;
    
    Vision::Image _currentImg;
    Vision::Image _nextImg;
    Vision::Image _lastImg; // the last image we processed
    
    MessageRobotState _currentRobotState;
    MessageRobotState _nextRobotState;
    
    std::thread _processingThread;
    
    void Processor();
    
    void Lock();
    void Unlock();
    
    
  }; // class VisionProcessingThread

  inline bool VisionProcessingThread::WasLastImageProcessed() const {
    return _wasLastImageProcessed;
  }
  
  inline void VisionProcessingThread::SetCameraCalibration(Vision::CameraCalibration& camCalib) {
    _camCalib = camCalib;
    _isCamCalibSet = true;
  }

} // namespace Cozmo
} // namespace Anki

#endif // ANKI_COZMO_BASESTATION_VISION_PROC_THREAD_H


/**
 * File: faceDisplay.h
 *
 * Author: Kevin Yoon
 * Created: 07/20/2017
 *
 * Description:
 *               Defines interface to face display
 *
 * Copyright: Anki, Inc. 2017
 *
 **/

#ifndef ANKI_COZMOANIM_FACE_DISPLAY_H
#define ANKI_COZMOANIM_FACE_DISPLAY_H

#include "util/singleton/dynamicSingleton.h"
#include "anki/cozmo/shared/factory/faultCodes.h"

#include "clad/types/lcdTypes.h"

#include <array>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>

#ifdef SIMULATOR
namespace webots {
  class Supervisor;
}
#endif

namespace Anki {

namespace Vision {
  class ImageRGB565;
}

namespace Vector {

class FaceDisplayImpl;
class FaceInfoScreenManager;

class FaceDisplay : public Util::DynamicSingleton<FaceDisplay>
{
  ANKIUTIL_FRIEND_SINGLETON(FaceDisplay); // Allows base class singleton access

public:
  void DrawToFace(const Vision::ImageRGB565& img);

  // For drawing to face in various debug modes
  void DrawToFaceDebug(const Vision::ImageRGB565& img);


  void SetFaceBrightness(LCDBrightness level);

  // Stops the boot animation process if it is running
  void StopBootAnim();
  
#ifdef SIMULATOR
  // Assign Webots supervisor
  // Webots processes must do this before creating FaceDisplay for the first time.
  // Unit test processes must call SetSupervisor(nullptr) to run without a supervisor.
  static void SetSupervisor(webots::Supervisor* sup);
#endif
  
protected:
  FaceDisplay();
  virtual ~FaceDisplay();

  void DrawToFaceInternal(const Vision::ImageRGB565& img);

private:
  std::unique_ptr<FaceDisplayImpl>  _displayImpl;

  // Members for managing the drawing thread
  std::unique_ptr<Vision::ImageRGB565>  _faceDrawImg[2];
  Vision::ImageRGB565*                  _faceDrawNextImg = nullptr;
  Vision::ImageRGB565*                  _faceDrawCurImg = nullptr;
  Vision::ImageRGB565*                  _faceDrawLastImg = nullptr;
  std::thread                           _faceDrawThread;
  std::mutex                            _faceDrawMutex;
  bool                                  _stopDrawFace = false;

  // Whether or not the boot animation process has been stopped
  // Atomic because it is checked by the face drawing thread
  std::atomic<bool> _stopBootAnim;
  
  void DrawFaceLoop();
  void UpdateNextImgPtr();
}; // class FaceDisplay

} // namespace Vector
} // namespace Anki

#endif // ANKI_COZMOANIM_FACE_DISPLAY_H

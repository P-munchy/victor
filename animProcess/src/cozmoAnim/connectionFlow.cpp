/** File: connectionFlow.cpp
 *
 * Author: Al Chaussee
 * Created: 02/28/2018
 *
 * Description: Functions for updating what to display on the face
 *              during various parts of the connection flow
 *
 * Copyright: Anki, Inc. 2018
 **/

#include "cozmoAnim/connectionFlow.h"

#include "cozmoAnim/animation/animationStreamer.h"
#include "cozmoAnim/animComms.h"
#include "cozmoAnim/animContext.h"
#include "cozmoAnim/faceDisplay/faceDisplay.h"
#include "cozmoAnim/faceDisplay/faceInfoScreenManager.h"

#include "coretech/common/engine/array2d_impl.h"
#include "coretech/common/engine/utils/data/dataPlatform.h"
#include "coretech/common/engine/utils/data/dataScope.h"
#include "coretech/vision/engine/image.h"
#include "coretech/vision/engine/image_impl.h"

#include "clad/robotInterface/messageEngineToRobot.h"

#include "util/console/consoleSystem.h"
#include "util/logging/logging.h"

#include "opencv2/imgproc.hpp"
#include "opencv2/highgui.hpp"

#include "anki/cozmo/shared/factory/emrHelper.h"

#include "osState/osState.h"

namespace Anki {
namespace Cozmo {

namespace {
u32 _pin = 123456;

const f32 kRobotNameScale = 0.5f;
const std::string kURL = "ddl.io/c";
const ColorRGBA   kColor(0.9f, 0.9f, 0.9f, 1.f);
}

// Draws BLE name and url to screen
bool DrawStartPairingScreen(AnimationStreamer* animStreamer)
{
  // Robot name will be empty until switchboard has set the property
  std::string robotName = OSState::getInstance()->GetRobotName();
  if(robotName == "")
  {
    return false;
  }
  
  animStreamer->EnableKeepFaceAlive(false, 0);
  animStreamer->Abort();

  Vision::ImageRGB565 img(FACE_DISPLAY_HEIGHT, FACE_DISPLAY_WIDTH);
  img.FillWith(Vision::PixelRGB565(0, 0, 0));

  img.DrawTextCenteredHorizontally(robotName, CV_FONT_NORMAL, kRobotNameScale, 1, kColor, 15, false);

  cv::Size textSize;
  float scale = 0;
  Vision::Image::MakeTextFillImageWidth(kURL, CV_FONT_NORMAL, 1, img.GetNumCols(), textSize, scale);
  img.DrawTextCenteredHorizontally(kURL, CV_FONT_NORMAL, scale, 1, kColor, (FACE_DISPLAY_HEIGHT + textSize.height) / 2, true);

  animStreamer->SetFaceImage(img, 0);
  return true;
}

// Draws BLE name, key icon, and BLE pin to screen
void DrawShowPinScreen(AnimationStreamer* animStreamer, const AnimContext* context, const std::string& pin)
{
  Vision::ImageRGB key;
  key.Load(context->GetDataPlatform()->pathToResource(Util::Data::Scope::Resources, 
                                                      "config/facePNGs/pairing_icon_key.png"));
  key.Resize(FACE_DISPLAY_HEIGHT - 5, FACE_DISPLAY_WIDTH - 20);

  Vision::ImageRGB img(FACE_DISPLAY_HEIGHT, FACE_DISPLAY_WIDTH);
  img.FillWith(0);

  Point2f p((FACE_DISPLAY_WIDTH - key.GetNumCols())/2,
            (FACE_DISPLAY_HEIGHT - key.GetNumRows())/2);
  img.DrawSubImage(key, p);

  Vision::ImageRGB565 i;
  i.SetFromImageRGB(img);

  i.DrawTextCenteredHorizontally(OSState::getInstance()->GetRobotName(), CV_FONT_NORMAL, kRobotNameScale, 1, kColor, 15, false);

  i.DrawTextCenteredHorizontally(pin, CV_FONT_NORMAL, 0.6f, 1, kColor, FACE_DISPLAY_HEIGHT-5, false);

  animStreamer->SetFaceImage(i, 0);
}

// Uses a png sequence animation to draw wifi icon to screen
void DrawWifiScreen(AnimationStreamer* animStreamer)
{
  animStreamer->SetStreamingAnimation("anim_pairing_icon_wifi", 0, 0);
}

// Uses a png sequence animation to draw os updating icon to screen
void DrawUpdatingOSScreen(AnimationStreamer* animStreamer)
{
  animStreamer->SetStreamingAnimation("anim_pairing_icon_update", 0, 0);
}

// Uses a png sequence animation to draw os updating error icon to screen
void DrawUpdatingOSErrorScreen(AnimationStreamer* animStreamer)
{
  animStreamer->SetStreamingAnimation("anim_pairing_icon_update_error", 0, 0);
}

// Uses a png sequence animation to draw waiting for app icon to screen
void DrawWaitingForAppScreen(AnimationStreamer* animStreamer)
{
  animStreamer->SetStreamingAnimation("anim_pairing_icon_awaitingapp", 0, 0);
}

void SetBLEPin(uint32_t pin)
{
  _pin = pin;
}

bool InitConnectionFlow(AnimationStreamer* animStreamer)
{
  // Don't start connection flow if not packed out
  if(!Factory::GetEMR()->fields.PACKED_OUT_FLAG)
  {
    return true;
  }

  return DrawStartPairingScreen(animStreamer);
}

void UpdatePairingLight(bool on)
{
  static bool isOn = false;
  if(!isOn && on)
  {
    // Start system pairing light (pulsing orange/green)
    RobotInterface::EngineToRobot m(RobotInterface::SetSystemLight({
          .light = {
            .onColor = 0xFFFF0000,
            .offColor = 0x00000000,
            .onFrames = 16,
            .offFrames = 16,
            .transitionOnFrames = 16,
            .transitionOffFrames = 16,
            .offset = 0
          }}));
    AnimComms::SendPacketToRobot((char*)m.GetBuffer(), m.Size());
    isOn = on; 
  }
  else if(isOn && !on)
  {
    // Turn system pairing light off
    RobotInterface::EngineToRobot m(RobotInterface::SetSystemLight({
          .light = {
            .onColor = 0x00000000,
            .offColor = 0x00000000,
            .onFrames = 1,
            .offFrames = 1,
            .transitionOnFrames = 0,
            .transitionOffFrames = 0,
            .offset = 0
          }}));
    AnimComms::SendPacketToRobot((char*)m.GetBuffer(), m.Size());
    isOn = on;
  }
}

void UpdateConnectionFlow(const SwitchboardInterface::SetConnectionStatus& msg,
                          AnimationStreamer* animStreamer,
                          const AnimContext* context)
{
  using namespace SwitchboardInterface;

  // Update the pairing light
  // Turn it on if we are on the START_PAIRING, SHOW_PRE_PIN, or SHOW_PIN screen
  // Otherwise turn it off
  UpdatePairingLight((msg.status == ConnectionStatus::START_PAIRING ||
                      msg.status == ConnectionStatus::SHOW_PRE_PIN ||
                      msg.status == ConnectionStatus::SHOW_PIN));

  // Enable pairing screen if status is anything besides NONE, COUNT, and END_PAIRING
  // Should do nothing if called multiple times with same argument such as when transitioning from
  // START_PAIRING to SHOW_PRE_PIN
  FaceInfoScreenManager::getInstance()->EnablePairingScreen((msg.status != ConnectionStatus::NONE &&
                                                             msg.status != ConnectionStatus::COUNT &&
                                                             msg.status != ConnectionStatus::END_PAIRING));

  switch(msg.status)
  {
    case ConnectionStatus::NONE:
    {

    }
    break;
    case ConnectionStatus::START_PAIRING:
    {
      // Throttling square is annoying when trying to inspect the display so disable
      NativeAnkiUtilConsoleSetValueWithString("DisplayThermalThrottling", "false");
      DrawStartPairingScreen(animStreamer);
    }
    break;
    case ConnectionStatus::SHOW_PRE_PIN:
    {
      DrawShowPinScreen(animStreamer, context, "######");
    }
    break;
    case ConnectionStatus::SHOW_PIN:
    {
      DrawShowPinScreen(animStreamer, context, std::to_string(_pin));
    }
    break;
    case ConnectionStatus::SETTING_WIFI:
    {
      DrawWifiScreen(animStreamer);
     }
    break;
    case ConnectionStatus::UPDATING_OS:
    {
      DrawUpdatingOSScreen(animStreamer);
    }
    break;
    case ConnectionStatus::UPDATING_OS_ERROR:
    {
      DrawUpdatingOSErrorScreen(animStreamer);
    }
    break;
    case ConnectionStatus::WAITING_FOR_APP:
    {
      DrawWaitingForAppScreen(animStreamer);
    }
    break;
    case ConnectionStatus::END_PAIRING:
    {
      NativeAnkiUtilConsoleSetValueWithString("DisplayThermalThrottling", "true");
      animStreamer->Abort();

      // Probably will never get here because we will restart
      // while updating os
      if(FACTORY_TEST)
      {
        DrawStartPairingScreen(animStreamer);
      }
      else
      {
        // Reenable keep face alive
        animStreamer->EnableKeepFaceAlive(true, 0);
      }
    }
    break;
    case ConnectionStatus::COUNT:
    {

    }
    break;
  }
}

}
}

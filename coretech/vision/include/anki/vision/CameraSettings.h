// CameraSettings.h
//
// Description: Types and constants pertaining to generic camera settings
//
//  Created by Kevin Yoon on 4/21/14.
//  Copyright (c) 2014 Anki, Inc. All rights reserved.
//

#ifndef ANKI_CAMERA_SETTINGS_H
#define ANKI_CAMERA_SETTINGS_H

namespace Anki {
  namespace Vision {

    typedef struct
    {
      uint16_t width;
      uint16_t height;
    } ImageDims;

    /// Must match definitions in ImageResolution enum in imageTypes.clad
    const ImageDims CameraResInfo[] =
    {
      {16,   16},
      {40,   30},
      {80,   60},
      {160,  120},
      {320,  240},
      {400,  296},
      {640,  480},
      {800,  600},
      {1024, 768},
      {1280, 960},
      {1600, 1200},
      {2048, 1536},
      {3200, 2400},
      {1280, 720}
    };

  } // namespace Vision
} // namespace Anki

#endif // ANKI_CAMERA_TYPES_H

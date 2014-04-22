//
//  imageIO.cpp
//  CoreTech_Vision_Basestation
//
//  Created by Kevin Yoon on 4/21/14.
//  Copyright (c) 2014 Anki, Inc. All rights reserved.
//

#include <stdio.h>
#include "anki/vision/basestation/imageIO.h"

namespace Anki {
  namespace Vision{
  
    void WritePGM(const char* filename, u8* imgData, u32 width, u32 height) {
      FILE * fp = fopen(filename, "w");
      
      fprintf(fp, "P2 \r\n%d %d \r\n255 \r\n", width, height);
      
      for (u32 row = 0; row < height; ++row) {
        for (u32 col = 0; col < width; ++col) {
          fprintf(fp, "%d ", imgData[row * width + col]);
        }
        fprintf(fp, "\n");
      }
      fclose(fp);
    }
    
  } // namespace Vision
} // namespace Anki
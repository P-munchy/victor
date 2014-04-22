//
//  imageIO.h
//  CoreTech_Vision
//
//  Created by Kevin Yoon on 4/21/14.
//  Copyright (c) 2014 Anki, Inc. All rights reserved.
//

#ifndef __CoreTech_Vision__imageIO__
#define __CoreTech_Vision__imageIO__

#include "anki/common/types.h"

namespace Anki {
  namespace Vision {
    
    void WritePGM(const char* filename, u8* imgData, u32 width, u32 height);
    
  } // namespace Vision
} // namespace Anki

#endif // __CoreTech_Vision__imageIO__
/**
 * File: droppedFrameStats.h
 *
 * Author: Andrew Stein
 * Date:   7/26/2016
 *
 * Description: Tracks total and recent frame drops and prints statistics for them.
 *
 * Copyright: Anki, Inc. 2016
 **/

#ifndef __Anki_Vision_Basestation_DropedFrameStats_H__
#define __Anki_Vision_Basestation_DropedFrameStats_H__

#include "util/logging/logging.h"

namespace Anki {
namespace Vision {

class DroppedFrameStats
{
public:
  
  DroppedFrameStats() { }
  
  // Defines how long the window is for computing "recent" drop stats,
  // in number of frames. Default is 100.
  void SetRecentWindowLength(u32 N) { _recentN = N; }
  
  // Set channel name for PRINT_CH_INFO messages. Default is "Performance".
  void SetChannelName(const char* channelName) { _channelName = channelName; }
  
  void Update(bool isDroppingFrame)
  {
    ++_numFrames;
    ++_numRecentFrames;
    
    if(isDroppingFrame)
    {
      ++_numTotalDrops;
      ++_numRecentDrops;
      
      PRINT_CH_INFO(_channelName, "DroppedFrameStats",
                    "Dropped %u of %u total images (%.1f%%), %u of last %u (%.1f%%)",
                    _numTotalDrops, _numFrames,
                    (f32)_numTotalDrops/(f32)_numFrames * 100.f,
                    _numRecentDrops, _numRecentFrames,
                    (f32)_numRecentDrops / (f32)_numRecentFrames * 100.f);
      
    }
    
    if(_numRecentFrames == _recentN) {
      _numRecentFrames = 0;
      _numRecentDrops = 0;
    }
  }
  
private:
  
  const char* _channelName = "Performance";
  
  u32  _recentN         = 100;
  u32  _numFrames       = 0;
  u32  _numRecentFrames = 0;
  u32  _numTotalDrops   = 0;
  u32  _numRecentDrops  = 0;
  
};

  
} // namespace Cozmo
} // namespace Anki

#endif // __Anki_Vision_Basestation_DropedFrameStats_H__

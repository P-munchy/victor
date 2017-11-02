/**
 * File: proxSensors.h
 *
 * Author: Kevin Yoon
 * Created: 8/12/2014
 *
 * Description:
 *
 *   Reads proximity sensors and
 *
 * Copyright: Anki, Inc. 2014
 *
 **/

#ifndef PROX_SENSORS_H_
#define PROX_SENSORS_H_

#include "anki/types.h"

namespace Anki {
  
  namespace Cozmo {
    
    namespace ProxSensors {

      // Since this re-enables 'cliff detect' and 'stop on cliff'
      // it should only be called when the robot disconnects,
      // otherwise you could desync stopOnCliff state with engine.
      void Reset();
      
      Result Update();

      void EnableCliffDetector(bool enable);
      
      void EnableStopOnCliff(bool enable);

      bool IsAnyCliffDetected();
      
      void SetCliffDetectThreshold(u32 ind, u16 level);
      
      void SetAllCliffDetectThresholds(u16 level);
      
      u16 GetRawCliffValue(u32 ind);

      u16 GetRawProxValue();

    } // namespace ProxSensors
  } // namespace Cozmo
} // namespace Anki

#endif // PROX_SENSORS_H_

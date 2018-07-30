/**
* File: micDirectionTypes.h
*
* Author: Lee Crippen / Jarrod Hatfield
* Created: 11/14/2017
*
* Description: defines mic direction types used in the engine
*
* Copyright: Anki, Inc. 2017
*
*/

#ifndef __Engine_MicDirectionTypes_H_
#define __Engine_MicDirectionTypes_H_

#include <deque>
#include <limits>

namespace Anki {
namespace Cozmo {

  using MicDirectionIndex = uint16_t;
  enum { kInvalidMicDirectionIndex = std::numeric_limits<MicDirectionIndex>::max() };

  using MicDirectionConfidence = int16_t;

  enum
  {
    kFirstMicDirectionIndex   = 0,
    kNumMicDirections         = 12,
    kMicDirectionUnknown      = kNumMicDirections,
    kLastMicDirectionIndex    = kMicDirectionUnknown,
  };

  // Interface for requesting a copy of direction history
  struct MicDirectionNode
  {
    TimeStamp_t             timestampBegin  = 0;
    TimeStamp_t             timestampEnd    = 0;
    MicDirectionIndex       directionIndex  = kMicDirectionUnknown;
    MicDirectionConfidence  confidenceAvg   = 0;
    MicDirectionConfidence  confidenceMax   = 0;
    TimeStamp_t             timestampAtMax  = 0;
    uint32_t                count           = 0;

    bool IsValid() const { return count > 0; }
  };
  using MicDirectionNodeList = std::deque<MicDirectionNode>;

} // namespace Cozmo
} // namespace Anki

#endif // __Engine_MicDirectionTypes_H_

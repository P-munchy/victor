/**
* File: micDataInfo.h
*
* Author: Lee Crippen
* Created: 10/25/2017
*
* Description: Holds onto info related to recording and processing mic data.
*
* Copyright: Anki, Inc. 2017
*
*/

#ifndef __AnimProcess_CozmoAnim_MicDataInfo_H_
#define __AnimProcess_CozmoAnim_MicDataInfo_H_

#include "cozmoAnim/micDataTypes.h"
#include "util/bitFlags/bitFlags.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace Anki {
namespace Cozmo {
namespace MicData {

class MicDataInfo
{
public:
  Util::BitFlags8<MicDataType>  _typesToRecord;
  uint32_t                      _timeRecorded_ms  = 0;
  bool                          _doFFTProcess     = false;
  bool                          _repeating        = false;
  uint32_t                      _numMaxFiles      = kDefaultFilesToCapture;
  std::string                   _writeLocationDir;
  std::string                   _writeNameBase;

  static constexpr uint32_t kMaxRecordTime_ms = std::numeric_limits<uint32_t>::max();
  
  // Note this will be called from a separate processing thread
  std::function<void(std::vector<uint32_t>&&)> _rawAudioFFTCallback;
  
  void SetTimeToRecord(uint32_t timeToRecord);
  void CollectRawAudio(const AudioUtil::AudioSample* audioChunk, size_t size);
  void CollectProcessedAudio(const AudioUtil::AudioSample* audioChunk, size_t size);

  AudioUtil::AudioChunkList GetProcessedAudio(size_t beginIndex);
  bool CheckDone();
  
private:
  // These members are accessed via multiple threads when the job is running, so they use a mutex
  uint32_t _timeToRecord_ms  = 0;
  AudioUtil::AudioChunkList _rawAudioData{};
  AudioUtil::AudioChunkList _processedAudioData{};
  std::mutex _dataMutex;

  void SaveCollectedAudio(const std::string& dataDirectory, const std::string& nameToUse, const std::string& nameToRemove);
  std::string ChooseNextFileNameBase(std::string& out_dirToDelete);
  
  static std::vector<uint32_t> GetFFTResultFromRaw(const AudioUtil::AudioChunkList& data, float length_s);
};

} // namespace MicData
} // namespace Cozmo
} // namespace Anki

#endif // __AnimProcess_CozmoAnim_MicDataInfo_H_

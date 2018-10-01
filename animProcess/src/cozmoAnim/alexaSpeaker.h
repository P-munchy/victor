#ifndef ANIMPROCESS_COZMO_ALEXASPEAKER_H
#define ANIMPROCESS_COZMO_ALEXASPEAKER_H


#include <memory>
#include <sstream>
#include <unordered_set>
#include <atomic>

#include <AVSCommon/Utils/MediaPlayer/MediaPlayerInterface.h>
#include <AVSCommon/Utils/MediaPlayer/MediaPlayerObserverInterface.h>
#include <AVSCommon/Utils/Threading/Executor.h>

#include "audioEngine/audioTools/standardWaveDataContainer.h"
#include "audioEngine/audioTools/streamingWaveDataInstance.h"

namespace Anki {
  
  namespace Vector {
    class AnimContext;
    class AudioDataBuffer;
    namespace Audio {
      class CozmoAudioController;
    }
  }
  namespace Util {
    namespace Dispatch {
      class Queue;
    }
  }
  
namespace Vector {
  
  

class AudioDataBuffer;

class AlexaSpeaker : public alexaClientSDK::avsCommon::utils::mediaPlayer::MediaPlayerInterface
{
  
public:
  
  AlexaSpeaker();
  ~AlexaSpeaker();
  
  void Init(const AnimContext* context);
  void Update();
  
  using SourceId = alexaClientSDK::avsCommon::utils::mediaPlayer::MediaPlayerInterface::SourceId;
  
  virtual SourceId   setSource (std::shared_ptr< alexaClientSDK::avsCommon::avs::attachment::AttachmentReader > attachmentReader, const alexaClientSDK::avsCommon::utils::AudioFormat *format=nullptr) override;
  
  virtual SourceId   setSource (const std::string &url, std::chrono::milliseconds offset=std::chrono::milliseconds::zero()) override;
  
  virtual SourceId   setSource (std::shared_ptr< std::istream > stream, bool repeat) override;
  
  virtual bool   play (SourceId id) override;
  
  virtual bool   stop (SourceId id) override;
  
  virtual bool   pause (SourceId id) override;
  
  virtual bool   resume (SourceId id) override;
  
  virtual std::chrono::milliseconds   getOffset (SourceId id) override;
  
  virtual uint64_t   getNumBytesBuffered () override;
  
  virtual void   setObserver (std::shared_ptr< alexaClientSDK::avsCommon::utils::mediaPlayer::MediaPlayerObserverInterface > playerObserver) override;
  
private:
  using StreamingWaveDataPtr = std::shared_ptr<AudioEngine::StreamingWaveDataInstance>;
  using AudioController = Audio::CozmoAudioController;
  using DispatchQueue = Util::Dispatch::Queue;
  
  // returns millisec decoded
  int Decode(const StreamingWaveDataPtr& data, bool flush = false);
  
  void CallOnPlaybackFinished( SourceId id );
  
  const char* const StateToString() const;
  
  void SavePCM( short* buff, size_t size=0 );
  
  enum class State {
    Idle=0,
    Preparing,
    Playable,
    Playing,
    Stopping,
    // todo: pausing etc
  };
  std::atomic<State> _state; // todo: thread locks
  
  SourceId m_sourceID=1; // 0 might be to be invalid?
  SourceId m_playingSource = 0;
  
  std::shared_ptr< alexaClientSDK::avsCommon::avs::attachment::AttachmentReader > m_source;
  
  /**
   * An internal executor that performs execution of callable objects passed to it sequentially but asynchronously.
   */
  alexaClientSDK::avsCommon::utils::threading::Executor m_executor;
  
  /// The set of @c MediaPlayerObserverInterface instances to notify of state changes.
  std::unordered_set<std::shared_ptr<alexaClientSDK::avsCommon::utils::mediaPlayer::MediaPlayerObserverInterface>> m_observers;
  
  std::unique_ptr<AudioDataBuffer> _mp3Buffer;
  
  uint64_t _offset_ms = 0;
  bool _first = true;
  
  StreamingWaveDataPtr _waveData;
  
  // worker thread
  DispatchQueue* _dispatchQueue = nullptr;
  
  // audio controller provided by context
  AudioController* _audioController = nullptr;
  
};

} // namespace Vector
} // namespace Anki

#endif // ANIMPROCESS_COZMO_ALEXASPEAKER_H

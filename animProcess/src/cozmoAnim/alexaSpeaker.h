#ifndef ANIMPROCESS_COZMO_ALEXASPEAKER_H
#define ANIMPROCESS_COZMO_ALEXASPEAKER_H


#include <memory>
#include <sstream>
#include <unordered_set>
#include <atomic>
#include <map>


#include <AVSCommon/SDKInterfaces/SpeakerInterface.h>
#include <AVSCommon/Utils/MediaPlayer/MediaPlayerInterface.h>

// could be fwd declared:

#include <AVSCommon/Utils/MediaPlayer/MediaPlayerObserverInterface.h>
#include <AVSCommon/Utils/Threading/Executor.h>
#include <PlaylistParser/UrlContentToAttachmentConverter.h>
#include <AVSCommon/SDKInterfaces/HTTPContentFetcherInterfaceFactoryInterface.h>

#include "audioEngine/audioTools/standardWaveDataContainer.h"
#include "audioEngine/audioTools/streamingWaveDataInstance.h"

#include "util/container/fixedCircularBuffer.h"

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

class AlexaSpeaker
  : public alexaClientSDK::avsCommon::utils::mediaPlayer::MediaPlayerInterface
  , public alexaClientSDK::avsCommon::sdkInterfaces::SpeakerInterface
  , public std::enable_shared_from_this<AlexaSpeaker>
  , public alexaClientSDK::playlistParser::UrlContentToAttachmentConverter::ErrorObserverInterface
{
  
public:
  
  AlexaSpeaker( alexaClientSDK::avsCommon::sdkInterfaces::SpeakerInterface::Type type,
                const std::string& name,
                std::shared_ptr<alexaClientSDK::avsCommon::sdkInterfaces::HTTPContentFetcherInterfaceFactoryInterface> contentFetcherFactory );
  ~AlexaSpeaker();
  
  void Init(const AnimContext* context);
  void Update();
  
  using SourceId = alexaClientSDK::avsCommon::utils::mediaPlayer::MediaPlayerInterface::SourceId;
  
  virtual SourceId   setSource (std::shared_ptr< alexaClientSDK::avsCommon::avs::attachment::AttachmentReader > attachmentReader, const alexaClientSDK::avsCommon::utils::AudioFormat *format=nullptr) override;
  
  virtual SourceId   setSource (const std::string &url, std::chrono::milliseconds offset=std::chrono::milliseconds::zero()) override;
  
  virtual SourceId   setSource (std::shared_ptr< std::istream > stream, bool repeat) override;
  
  void DisableSource( int source ) { if( source ==2 ) { _source2Enabled = false; } if( source == 3) { _source3Enabled = false; } }
  
  virtual bool   play (SourceId id) override;
  
  virtual bool   stop (SourceId id) override;
  
  virtual bool   pause (SourceId id) override;
  
  virtual bool   resume (SourceId id) override;
  
  virtual std::chrono::milliseconds   getOffset (SourceId id) override;
  
  virtual uint64_t   getNumBytesBuffered () override;
  
  virtual void   setObserver (std::shared_ptr< alexaClientSDK::avsCommon::utils::mediaPlayer::MediaPlayerObserverInterface > playerObserver) override;
  
  
  // Speaker interface (currently a no-op since that would require wwise events)
  virtual bool   setVolume (int8_t volume) override;
  
  virtual bool   adjustVolume (int8_t delta) override;
  
  virtual bool   setMute (bool mute) override;
  
  virtual bool   getSpeakerSettings (SpeakerSettings *settings) override;
  
  virtual Type   getSpeakerType ()  override { return _type; }
  
  // ErrorObserverInterface
  virtual void onError () override;
  
  
  
  // if set, gets call with a packet of audio that just played
  using OnPlayAudio = std::function<void(const int16_t*, int, int)>;
  void SetPlayedAudioCallback( const OnPlayAudio& onPlayAudio );
  
  using OnPlaybackStarted = std::function<void(void)>;
  using OnPlaybackEnded = std::function<void(void)>;
  void SetOnPlaybackStarted( const OnPlaybackStarted& onPlaybackStarted ) { _onPlaybackStarted = onPlaybackStarted; }
  void SetOnPlaybackEnded( const OnPlaybackEnded& onPlaybackEnded ) { _onPlaybackEnded = onPlaybackEnded; }
  
private:
  
  float _timeStartedPlaying_sec = 0.0f;
  
  bool _source3Enabled = true;
  bool _source2Enabled = true;
  Type _type;
  alexaClientSDK::avsCommon::sdkInterfaces::SpeakerInterface::SpeakerSettings _settings;
  
  enum class SourceType : uint8_t {
    AttachmentReader,
    Url,
    Stream,
  };
  std::map<SourceId, SourceType> _sourceTypes;
  
  using StreamingWaveDataPtr = std::shared_ptr<AudioEngine::StreamingWaveDataInstance>;
  using AudioController = Audio::CozmoAudioController;
  using DispatchQueue = Util::Dispatch::Queue;
  
  // returns millisec decoded
  int Decode(const StreamingWaveDataPtr& data, bool flush = false);
  
  void CallOnPlaybackFinished( SourceId id );
  
  const char* const StateToString() const;
  
  
  
  void SavePCM( short* buff, size_t size=0 );
  void SavePCM2( short* buff, size_t size=0 );
  
  enum class State {
    Idle=0,
    Preparing,
    Playable,
    Playing,
//    Stopping,
    // todo: pausing etc
  };
  std::atomic<State> _state;
  
  void SetState( State state );
  
  static SourceId m_sourceID;
  SourceId m_playingSource = 0;
  
  std::map<SourceId, std::shared_ptr< alexaClientSDK::avsCommon::avs::attachment::AttachmentReader >> m_sourceReaders;
  std::map<SourceId, std::shared_ptr< std::istream >> m_sourceStreams;
  
  /**
   * An internal executor that performs execution of callable objects passed to it sequentially but asynchronously.
   */
  alexaClientSDK::avsCommon::utils::threading::Executor m_executor;
  
  /// The set of @c MediaPlayerObserverInterface instances to notify of state changes.
  std::unordered_set<std::shared_ptr<alexaClientSDK::avsCommon::utils::mediaPlayer::MediaPlayerObserverInterface>> m_observers;
  
  std::unique_ptr<AudioDataBuffer> _mp3Buffer;
  
  uint64_t _offset_ms = 0;
  bool _first = true;
  
  int _currSampleRate = 0;
  
  StreamingWaveDataPtr _waveData;
  
  std::mutex _mutex;
  
  const std::string _name;
  
  // worker thread
  DispatchQueue* _dispatchQueue = nullptr;
  
  // audio controller provided by context
  AudioController* _audioController = nullptr;
  
  
  /// Used to create objects that can fetch remote HTTP content.
  std::shared_ptr<alexaClientSDK::avsCommon::sdkInterfaces::HTTPContentFetcherInterfaceFactoryInterface> m_contentFetcherFactory;
  
  /// Used to stream urls into attachments
  std::shared_ptr<alexaClientSDK::playlistParser::UrlContentToAttachmentConverter> m_urlConverter;
  
  OnPlayAudio _onPlayAudio;
  OnPlaybackStarted _onPlaybackStarted;
  OnPlaybackEnded _onPlaybackEnded;
  // this still sometimes overflows, so we need to do this another way. maybe by feeding the existing buffer to micdataprocessor
  std::unique_ptr<Util::FixedCircularBuffer<int16_t,131072>> _playedAudio;
  uint32_t _lastPlayHead = 0;
  uint32_t _numFramesPlayed = 0;
};

} // namespace Vector
} // namespace Anki

#endif // ANIMPROCESS_COZMO_ALEXASPEAKER_H

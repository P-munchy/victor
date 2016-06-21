/**
 * File: animationAnimationTestConfig.h
 *
 * Author: Jordan Rivas
 * Created: 6/17/16
 *
 * Description: Configure an audio animation scenario by inserting each audio event. Generate Animation &
 *              RobotAudioBuffer for given config.
 *
 * Copyright: Anki, Inc. 2016
 *
 **/

#ifndef animationAnimationTestConfig_hpp
#define animationAnimationTestConfig_hpp


#include "clad/audio/audioEventTypes.h"
#include "clad/audio/audioGameObjectTypes.h"

#include <vector>


namespace Anki {
namespace Cozmo {
namespace Audio {
class RobotAudioTestBuffer;
}
}
}


class AnimationAnimationTestConfig {
  
public:
  
  // Define an Audio Event
  struct TestAudioEvent {
    
    Anki::Cozmo::Audio::GameEvent::GenericEvent event = Anki::Cozmo::Audio::GameEvent::GenericEvent::Invalid;
    uint32_t startTime_ms = 0;
    uint32_t duration_ms = 0;
    TestAudioEvent( Anki::Cozmo::Audio::GameEvent::GenericEvent event, uint32_t startTime_ms, uint32_t duration_ms )
    : event( event )
    , startTime_ms( startTime_ms )
    , duration_ms( duration_ms )
    {}
    bool InAnimationTimeRange( uint32_t animationTime_ms )
    {
      return ( animationTime_ms >= startTime_ms ) && ( animationTime_ms < startTime_ms + duration_ms );
    }
    
    // TODO: Add methods to offset actual buffer createion to simulate the buffer not perfectly lining up with desired
    //       situation.
  };
  
  // Add Test Event
  // Note: Can not call this after Insert Complete has been called
  void Insert( TestAudioEvent&& audioEvent );

  // Call to lock insert and setup meta data
  void InsertComplete();
  
  // Get list of sorted events within frame time
  std::vector<TestAudioEvent> FrameAudioEvents( const uint32_t frameStartTime_ms, const uint32_t frameEndTime_ms );
  
  // Write audio into Test Robot Buffer
  const std::vector<TestAudioEvent>& GetAudioEvents() const { return _events; }
  
  // Add Config's events to animation
  void LoadAudioKeyFrames( Anki::Cozmo::Animation& outAnimation );
  
  // Create or write audio into Test Robot Buffer
  void LoadAudioBuffer( Anki::Cozmo::Audio::RobotAudioTestBuffer& outBuffer );
  
private:
  
  std::vector<TestAudioEvent> _events;
  bool _lockInsert = false;

  // Calculate how many samples are in milli sec duration
  uint32_t numberOfSamples_ms( uint32_t milliSec );
  
};


#endif /* animationAnimationTestConfig_hpp */

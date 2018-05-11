/**
 * File: audioLayerManager.h
 *
 * Authors: Al Chaussee
 * Created: 06/28/2017
 *
 * Description: Specific track layer manager for RobotAudioKeyFrame
 *
 * Copyright: Anki, Inc. 2017
 *
 **/

#ifndef __Anki_Cozmo_AudioLayerManager_H__
#define __Anki_Cozmo_AudioLayerManager_H__

#include "cannedAnimLib/baseTypes/track.h"
#include "cannedAnimLib/cannedAnims/animation.h"
#include "cannedAnimLib/proceduralFace/proceduralFaceModifierTypes.h"
#include "coretech/common/shared/types.h"
#include "cozmoAnim/animation/trackLayerManagers/iTrackLayerManager.h"
#include <string>


namespace Anki {
namespace Cozmo {

class AudioLayerManager : public ITrackLayerManager<RobotAudioKeyFrame>
{
public:
  
  AudioLayerManager(const Util::RandomGenerator& rng);
  
  // Add Audio Keyframes for Eye Blink
  Result AddEyeBlinkToAudioTrack(const std::string& layerName, const BlinkEventList& eventList);
  // Add Audio Keyframes for Eye Dart
  Result AddEyeDartToAudioTrack(const std::string& layerName, TimeStamp_t interpolationTime_ms);
  // Add Audio keyframes for Eye Squint
  Result AddEyeSquintToAudioTrack(const std::string& layerName);
  
  
  // TODO: VIC-447: Restore glitching
  // Generates a track of all audio "keyframes" necessary for creating audio glitch sounds
  // Needs to know how many keyframes to generate so that the audio matches with other
  // animation tracks
  void GenerateGlitchAudio(u32 numFramesToGen,
                           Animations::Track<RobotAudioKeyFrame>& outTrack) const;

};

}
}


#endif

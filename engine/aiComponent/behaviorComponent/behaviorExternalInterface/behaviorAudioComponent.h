/**
 * File: BehaviorAudioComponent.h
 *
 * Author: Jordan Rivas
 * Created: 11/02/2016
 *
 * Description: This Client provides behavior audio needs for updating Sparked Behavior music state and round. When
 *              using first call ActivateSparkedMusic() to activate audio client and when behavior is completed call
 *              DeactivateSparkedMusic().
 *
 * Copyright: Anki, Inc. 2016
 *
 **/

// TODO: VIC-25 - Migrate Behavior Audio Client to victor
// This class controlls behavior music state which is not realevent when playing audio on the robot

#ifndef __Basestation_Audio_BehaviorAudioComponent_H__
#define __Basestation_Audio_BehaviorAudioComponent_H__

#include "engine/aiComponent/behaviorComponent/behaviors/iCozmoBehavior_fwd.h"
#include "engine/events/ankiEventMgr.h"

#include "clad/audio/audioStateTypes.h"
#include "clad/audio/audioSwitchTypes.h"
#include "clad/types/robotPublicState.h"
#include "clad/types/unlockTypes.h"


#define kBehaviorRound  0

namespace Anki {
namespace Cozmo {

class BehaviorExternalInterface;
class BehaviorManager;

namespace Audio {

class EngineRobotAudioClient;

class BehaviorAudioComponent {
  
public:
  BehaviorAudioComponent(Audio::EngineRobotAudioClient* robotAudioClient);
  
  void Init(BehaviorExternalInterface& behaviorExternalInterface);

  // True if Client has been Activated
  bool IsActive() const { return _isActive; }
  
  // Get Current round
  int GetRound() const { return _round; }
  
  // Change music switch state for Ai Goals in freeplay
  void UpdateActivityMusicState(BehaviorID activityID);
  
protected:
  // Activate to allow behavior to update audio engine
  // If GameState::Music::Invalid is passed in audio engine music state will not be updated
  // If SwitchState::Sparked::Invalid is passed in audio engine switch state will not be updated
  // Return false if behavior UnlockId is Invalid
  bool ActivateSparkedMusic(const UnlockId behaviorUnlockId,
                            const AudioMetaData::GameState::Music musicState,
                            const AudioMetaData::SwitchState::Sparked sparkedState,
                            const int round = kBehaviorRound);
  
  // This deactivates the BehaviorAudioComponent and set new music state to freeplay
  void DeactivateSparkedMusic();
  
  // Update the behavior's current round
  // Return false behavior UnlockId does not match the UnlockId that was used to activate the sparked music
  bool UpdateBehaviorRound(const UnlockId behaviorUnlockId, const int round);
  
  void HandleRobotPublicStateChange(BehaviorExternalInterface& behaviorExternalInterface,
                                    const RobotPublicState& stateEvent);
  
private:
  // Track second unlockID value for instances where we receive the appropriate
  // music state from game after a spark activity has already started
  UnlockId  _activeSparkMusicID = UnlockId::Count;
  //UnlockId  _lastUnlockIDReceived = UnlockId::Count;
  
  AudioMetaData::SwitchState::Sparked _sparkedMusicState = AudioMetaData::SwitchState::Sparked::Invalid;
  bool      _isActive = false;
  int       _round    = kBehaviorRound;
  
// World Event tracking
//  bool      _isCubeInLift     = false;
//  bool      _isRequestingGame = false;
//  bool      _stackExists      = false;
  
  std::vector<::Signal::SmartHandle> _eventHandles;
  
  void SetDefaultBehaviorRound() { _round = kBehaviorRound; }
  
  // Keep track of the last AI goal name sent from the PublicStateBroadcaster so we
  //  can properly handle AI goal transitions.
  //BehaviorID _prevActivity;
  
  // Tracks the active behavior stage if custom music rounds are being set
  // Use setter/getter function rather than accessing directly
  BehaviorStageTag _activeBehaviorStage;
  
  // Keep track of the last GuardDog behavior stage so we can set the
  //  proper audio round when transitioning between stages.
//  GuardDogStage _prevGuardDogStage;
  
  // Keep track of the last feeding stage so we can identify music changes
//  FeedingStage _prevFeedingStage;
  
  // Communicate with the audio client
  //Audio::RobotAudioClient* _robotAudioClient;
  
  // Keep track of the last needs levels so we can broadcast when they change
//  NeedsLevels _needsLevel;
  
  
  BehaviorStageTag GetActiveBehaviorStage();
  void SetActiveBehaviorStage(BehaviorStageTag stageTag);
  
  void HandleWorldEventUpdates(const RobotPublicState& stateEvent);
  void HandleGuardDogUpdates(const BehaviorStageStruct& currPublicStateStruct);
  void HandleDancingUpdates(const BehaviorStageStruct& currPublicStateStruct);
  void HandleFeedingUpdates(const BehaviorStageStruct& currPublicStateStruct);
  void HandleNeedsUpdates(const NeedsLevels& needsLevel);
  void HandleDimMusicForActivity(const RobotPublicState& stateEvent);

  
};

} // Audio
} // Cozmo
} // Anki



#endif /* __Basestation_Audio_BehaviorAudioComponent_H__ */

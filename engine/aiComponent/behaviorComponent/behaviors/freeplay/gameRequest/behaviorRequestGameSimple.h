/**
 * File: behaviorRequestGameSimple.h
 *
 * Author: Brad Neuman
 * Created: 2016-02-03
 *
 * Description: re-usable game request behavior which works with or without blocks
 *
 * Copyright: Anki, Inc. 2016
 *
 **/

#ifndef __Cozmo_Basestation_Behaviors_GameRequest_BehaviorRequestGameSimple_H__
#define __Cozmo_Basestation_Behaviors_GameRequest_BehaviorRequestGameSimple_H__

#include "engine/aiComponent/behaviorComponent/behaviors/freeplay/gameRequest/behaviorGameRequest.h"
#include "clad/types/pathMotionProfile.h"
#include <string>

namespace Anki {
namespace Cozmo {

class BehaviorRequestGameSimple : public ICozmoBehaviorRequestGame
{
private:
  
  // Enforce creation through BehaviorContainer
  friend class BehaviorContainer;
  BehaviorRequestGameSimple(const Json::Value& config);

public:
  virtual ~BehaviorRequestGameSimple() {}
  
  void TriggeringAsInterrupt() { _wasTriggeredAsInterrupt = true;}

protected:
  virtual void RequestGame_OnBehaviorActivated() override;
  virtual void RequestGame_UpdateInternal() override;
  virtual void   RequestGame_OnBehaviorDeactivated() override;

  virtual void HandleGameDeniedRequest() override;
  virtual f32 GetRequestMinDelayComplete_s() const override;

  virtual u32 GetNumBlocks() const override;

private:

  // ========== Members ==========

  enum class State {
    PlayingInitialAnimation,
    FacingBlock,
    PlayingPreDriveAnimation,
    PickingUpBlock,
    DrivingToFace,
    PlacingBlock,
    LookingAtFace,
    VerifyingFace,
    PlayingRequestAnim,
    Idle,
    PlayingDenyAnim,
    SearchingForBlock
  };

  State _state = State::PlayingInitialAnimation;

  // there are two sets of values, based on whether there are 0 or more blocks available (at the time of Init)
  struct ConfigPerNumBlocks {
    void LoadFromJson(const Json::Value& config);
    AnimationTrigger initialAnimTrigger = AnimationTrigger::Count;
    AnimationTrigger preDriveAnimTrigger = AnimationTrigger::Count;
    AnimationTrigger requestAnimTrigger = AnimationTrigger::Count;
    AnimationTrigger denyAnimTrigger = AnimationTrigger::Count;
    AnimationTrigger idleAnimTrigger = AnimationTrigger::Count;
    float            minRequestDelay;
    float            scoreFactor;
  };

  ConfigPerNumBlocks _zeroBlockConfig;
  ConfigPerNumBlocks _oneBlockConfig;

  // if true, disable (some) reaction triggers right away rather than waiting for the request
  bool _disableReactionsEarly;

  PathMotionProfile _driveToPickupProfile;
  PathMotionProfile _driveToPlaceProfile;

  float _driveToPlacePoseThreshold_mm;
  float _driveToPlacePoseThreshold_rads;

  float _afterPlaceBackupDist_mm;
  float _afterPlaceBackupSpeed_mmps;

  ConfigPerNumBlocks* _activeConfig = nullptr;
  
  float  _verifyStartTime_s = 0.0f;

  Pose3d _faceInteractionPose;
  bool   _hasFaceInteractionPose = false;
  
  bool   _shouldUseBlocks;
  
  int    _numRetriesDrivingToFace;
  int    _numRetriesPlacingBlock;
  
  bool   _wasTriggeredAsInterrupt;
  
  void SetState_internal(State state, const std::string& stateName);
  
  void TransitionToPlayingInitialAnimation();
  void TransitionToFacingBlock();
  void TransitionToPlayingPreDriveAnimation();
  void TransitionToPickingUpBlock();
  void TransitionToDrivingToFace();
  void TransitionToPlacingBlock();
  void TransitionToLookingAtFace();
  void TransitionToVerifyingFace();
  void TransitionToPlayingRequstAnim();
  void TransitionToIdle();
  void IdleLoop();
  void TransitionToPlayingDenyAnim();
  bool GetFaceInteractionPose(Pose3d& pose);
  void ComputeFaceInteractionPose();
  bool CheckRequestTimeout();

};

}
}



#endif

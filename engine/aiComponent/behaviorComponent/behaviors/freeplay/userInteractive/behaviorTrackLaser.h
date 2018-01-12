/**
 * File: behaviorTrackLaser.h
 *
 * Author: Andrew Stein
 * Created: 2017-03-11
 *
 * Description: Follows a laser point around (using TrackGroundPointAction) and tries to pounce on it. 
 *              Adapted from PounceOnMotion
 *
 * Copyright: Anki, Inc. 2017
 *
 **/

#ifndef __Cozmo_Basestation_Behaviors_BehaviorTrackLaser_H__
#define __Cozmo_Basestation_Behaviors_BehaviorTrackLaser_H__

#include "engine/aiComponent/behaviorComponent/behaviors/iCozmoBehavior.h"
#include "util/graphEvaluator/graphEvaluator2d.h"

namespace Anki {
namespace Cozmo {

class BehaviorTrackLaser : public ICozmoBehavior
{
private:
  
  // Enforce creation through BehaviorContainer
  friend class BehaviorContainer;
  BehaviorTrackLaser(const Json::Value& config);
  
public:  
  virtual bool WantsToBeActivatedBehavior(BehaviorExternalInterface& behaviorExternalInterface) const override;

protected:
  
  virtual void AlwaysHandleInScope(const EngineToGameEvent& event, BehaviorExternalInterface& behaviorExternalInterface) override;

  virtual void GetBehaviorOperationModifiers(BehaviorOperationModifiers& modifiers) const override {
    modifiers.behaviorAlwaysDelegates = false;
  }
  virtual void OnBehaviorActivated(BehaviorExternalInterface& behaviorExternalInterface) override;
  virtual void BehaviorUpdate(BehaviorExternalInterface& behaviorExternalInterface) override;
  virtual void OnBehaviorDeactivated(BehaviorExternalInterface& behaviorExternalInterface) override;

private:

  enum class State {
    Inactive,
    InitialSearch,
    BringingHeadDown,
    RotateToWatchingNewArea,
    WaitingForExposureChange,
    WaitingForLaser,
    RespondToLaser,
    TrackLaser,
    Pouncing,
    GetOutBored,
    WaitForStop,
    Complete,
  };
  
  // used for tracking total turn angle
  Radians _cumulativeTurn_rad;
  
  // Set from Json config (use keys named exactly the same as the struct members).
  // Rough "reasonable" values are provided in comments to give you an idea of where to start.
  // NOTE: All are stored as floats to make it easy to set them from Json via one simple macro
  struct {
    
    // Behavior is activatable if a possible laser was seen within this long
    float    startIfLaserSeenWithin_sec; // E.g. 1.0sec
    
    // Must see possible laser within this distance to start to try to confirm.
    // This distance threshold does not apply while already tracking a confirmed laser.
    float    maxDistToGetAttention_mm; // E.g. = 80mm
    
    // Once we see a possible laser (w/ camera at normal exposure), we darken the exposure
    // to confirm the laser. We wait a small amount of time to let the exposure settings
    // take effect. If we observe the mean drop by the specified fraction, we immediately
    // assume the change has taken effect.
    float    darkenedExposure_ms; // E.g. 1ms
    float    darkenedGain; // E.g. 0.1
    float    numImagesToWaitForExposureChange; // E.g. 2
    float    imageMeanFractionForExposureChange; // E.g. 0.5
    
    // After changing exposure, we'll wait this long to confirm the laser
    float    maxTimeToConfirm_ms; // E.g. 65ms
    
    float    searchAmplitude_deg; // E.g. 90deg
    
    // Various timeouts
    Util::GraphEvaluator2d maxLostLaserTimeoutGraph_sec; // E.g. time behavior's been running -> s to search
    float    maxTimeBehaviorTimeout_sec;  // E.g. 30sec
    float    maxTimeBeforeRotate_sec;  // E.g. 4sec
    float    trackingTimeout_sec;  // E.g. 1.5fsec
    
    // Pounce settings
    // Cozmo pounces after maintaining the laser point within the given distance and angle tolerances
    // for the given amoung of time. After pouncing, he backs up a little.
    float    pounceAfterTrackingFor_sec;  // E.g. 1sec
    float    pounceIfWithinDist_mm;  // E.g. 50mm
    float    pouncePanTol_deg;  // E.g. 10deg
    float    pounceTiltTol_deg;  // E.g. 15deg
    float    backupDistAfterPounce_mm;  // E.g. -15mm
    float    backupDurationAfterPounce_sec;  // E.g. 0.25sec

    // For randomly searching for the laser if forcibly started (i.e. sparked)
    float    randomInitialSearchPanMin_deg;  // E.g. 20deg
    float    randomInitialSearchPanMax_deg;  // E.g. 45deg
    
    // Control how fast the robot rotates when point turning towards laser by
    // adjusting the time spent doing so. Chosen randomly each time a tracking
    // action is created, between min and max.
    float    minPanDuration_sec;  // E.g. 0.2sec
    float    maxPanDuration_sec;  // E.g. 0.4sec
    
    // Control how fast the robot will drive to reach the laser, but adjusting the time to
    // to drive the distance to it. Chosen randomly  each time
    float    minTimeToReachLaser_sec;  // E.g. 0.6sec
    float    maxTimeToReachLaser_sec;  // E.g. 0.8sec
    
    // For how long after losing the laser the robot will try to predict where it went and turn there
    // Set to 0 to disable
    float    predictionDuration_sec;  // E.g. 1sec
    
    // If we track the laser for this long, we achieve the LaserTracked objective
    float    trackingTimeToAchieveObjective_sec;  // E.g. 5sec
    
    bool     skipGetOutAnim = false;
    
  } _params;
  
  struct LaserObservation {
    enum Type {
      None,         // Have not observed anything
      Unconfirmed,  // Seen while not running (and auto exposure on)
      Confirmed     // Seen while running (with reduced exposure)
    };
    
    Type        type;
    TimeStamp_t timestamp_ms;
    TimeStamp_t timestamp_prev_ms;
    Point2f     pointWrtRobot;
  };
  
  LaserObservation _lastLaserObservation;
  bool  _haveEverConfirmedLaser = false;
  bool  _haveAdjustedAnimations = false;
  bool  _shouldSendTrackingObjectiveAchieved = false;
  
  s16 _imageMean = -1;
  TimeStamp_t _exposureChangedTime_ms = 0;
  
  float _lastTimeRotate = 0.f;
  float _startedTracking_sec = 0.f;
  float _currentLostLaserTimeout_s = 0.f;
  
  State _state = State::Inactive;
  
  // So that we can restore when done
  struct {
    s32  exposureTime_ms;
    f32  gain;
  } _originalCameraSettings;
  
  // reset everything for when the behavior is finished
  void Cleanup(BehaviorExternalInterface& behaviorExternalInterface);
  
  // check if it's been too long since we saw a laser or we've been running too long
  // if so, return true and transition to GetOutBored state
  bool CheckForTimeout(BehaviorExternalInterface& behaviorExternalInterface);
  
  void SetState_internal(State state, const std::string& stateName);

  void TransitionToInitialSearch(BehaviorExternalInterface& behaviorExternalInterface);
  void TransitionToBringingHeadDown(BehaviorExternalInterface& behaviorExternalInterface);
  void TransitionToRotateToWatchingNewArea(BehaviorExternalInterface& behaviorExternalInterface);
  void TransitionToWaitForExposureChange(BehaviorExternalInterface& behaviorExternalInterface);
  void TransitionToWaitForLaser(BehaviorExternalInterface& behaviorExternalInterface);
  void TransitionToRespondToLaser(BehaviorExternalInterface& behaviorExternalInterface);
  void TransitionToTrackLaser(BehaviorExternalInterface& behaviorExternalInterface);
  void TransitionToPounce(BehaviorExternalInterface& behaviorExternalInterface);
  void TransitionToGetOutBored(BehaviorExternalInterface& behaviorExternalInterface);
  
  void InitHelper(BehaviorExternalInterface& behaviorExternalInterface);
  
  void SetParamsFromConfig(const Json::Value& config);
  
  void SetLastLaserObservation(const BehaviorExternalInterface& behaviorExternalInterface, const EngineToGameEvent& event);
  
};

} // namespace Cozmo
} // namespace Anki

#endif /* __Cozmo_Basestation_Behaviors_BehaviorTrackLaser_H__ */

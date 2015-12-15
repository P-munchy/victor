/**
 * File: behaviorInteractWithFaces.h
 *
 * Author: Andrew Stein
 * Date:   7/30/15
 *
 * Description: Defines Cozmo's "InteractWithFaces" behavior, which tracks/interacts with faces if it finds one.
 *
 * Copyright: Anki, Inc. 2015
 **/

#ifndef __Cozmo_Basestation_Behaviors_BehaviorInteractWithFaces_H__
#define __Cozmo_Basestation_Behaviors_BehaviorInteractWithFaces_H__

#include "anki/cozmo/basestation/behaviors/behaviorInterface.h"
#include "anki/cozmo/basestation/proceduralFace.h"
#include "anki/vision/basestation/trackedFace.h"

#include <list>

namespace Anki {
namespace Cozmo {
  
  class BehaviorInteractWithFaces : public IBehavior
  {
  protected:
    
    // Enforce creation through BehaviorFactory
    friend class BehaviorFactory;
    BehaviorInteractWithFaces(Robot& robot, const Json::Value& config);
    
  public:
    
    virtual ~BehaviorInteractWithFaces() override;
    
    virtual bool IsRunnable(const Robot& robot, double currentTime_sec) const override;
    
    virtual bool WantsToResume() const override { return (_resumeState != State::Interrupted); }
    
  protected:
    
    virtual Result InitInternal(Robot& robot, double currentTime_sec, bool isResuming) override;
    virtual Status UpdateInternal(Robot& robot, double currentTime_sec) override;
    virtual Result InterruptInternal(Robot& robot, double currentTime_sec, bool isShortInterrupt) override;
    

  private:
    using Face = Vision::TrackedFace;
    
    virtual void AlwaysHandle(const EngineToGameEvent& event, const Robot& robot) override;
    virtual void HandleWhileRunning(const EngineToGameEvent& event, Robot& robot) override;
    
    void HandleRobotObservedFace(const Robot& robot, const EngineToGameEvent& event);
    void HandleRobotDeletedFace(const EngineToGameEvent& event);
    void HandleRobotCompletedAction(Robot& robot, const EngineToGameEvent& event);
    
    void UpdateBaselineFace(Robot& robot, const Face* face);
    void RemoveFaceID(Face::ID_t faceID);
    void UpdateProceduralFace(Robot& robot, ProceduralFace& proceduralFace, const Face& face) const;
    void PlayAnimation(Robot& robot, const std::string& animName);
    
    // Sets face tracking ID and queues TrackFaceAction "now".
    void StartTracking(Robot& robot, Face::ID_t faceID);
    
    // Unsets face tracking ID and cancels last action tag. Also sets current state to Inactive.
    void StopTracking(Robot& robot);
    
    enum class State {
      Inactive,
      TrackingFace,
      Interrupted
    };
    
    State _currentState = State::Interrupted;
    State _resumeState  = State::Interrupted;
    
    Face::ID_t _trackedFaceID = Face::UnknownFace;
    
    f32 _trackingTimeout_sec = 3;
    
    ProceduralFace _lastProceduralFace, _crntProceduralFace;;
    
    f32 _baselineEyeHeight;
    f32 _baselineIntraEyeDistance;
    f32 _baselineLeftEyebrowHeight;
    f32 _baselineRightEyebrowHeight;
    
    u32    _trackActionTag = (u32)ActionConstants::INVALID_TAG;
    u32    _lastActionTag = (u32)ActionConstants::INVALID_TAG;
    bool   _isActing = false;
    double _lastGlanceTime = 0;
    double _lastTooCloseScaredTime = 0;
    double _newFaceAnimCooldownTime = 0.0;
    double _timeWhenInterrupted = 0.0;
    
    struct FaceData
    {
      double _lastSeen_sec = 0;
      double _trackingStart_sec = 0;
      bool _playedInitAnim = false;
    };
    
    std::list<Face::ID_t> _interestingFacesOrder;
    std::unordered_map<Face::ID_t, FaceData> _interestingFacesData;
    std::unordered_map<Face::ID_t, double> _cooldownFaces;
    
    // Length of time in seconds to keep interacting with the same face non-stop
    constexpr static float kFaceInterestingDuration_sec = 20;
    
    // Length of time in seconds to ignore a specific face that has hit the kFaceInterestingDuration limit
    constexpr static float kFaceCooldownDuration_sec = 20;
    
    // Distance inside of which Cozmo will start noticing a face
    constexpr static float kCloseEnoughDistance_mm = 1250;
    
    // Defines size of zone between "close enough" and "too far away", which prevents faces quickly going back and forth
    // over threshold of close enough or not
    constexpr static float kFaceBufferDistance_mm = 350;
    
    // Distance to trigger Cozmo to start ignoring a face
    constexpr static float kTooFarDistance_mm = kCloseEnoughDistance_mm + kFaceBufferDistance_mm;
    
    // Distance to trigger Cozmo to get further away from the focused face
    constexpr static float kTooCloseDistance_mm = 200;
    
    // Maximum frequency that Cozmo should glance down when interacting with faces (could be longer if he has a stable
    // face to focus on; this interval shouln't interrupt his interaction)
    constexpr static float kGlanceDownInterval_sec = 12;
    
    // Min time between plays of the animation when we see a new face
    constexpr static float kSeeNewFaceAnimationCooldown_sec = 10;
    
    // Min time between playing the shocked/scared animation when a face gets
    // too close
    constexpr static float kTooCloseScaredInterval_sec = 2;
    
  }; // BehaviorInteractWithFaces
  
} // namespace Cozmo
} // namespace Anki

#endif // __Cozmo_Basestation_Behaviors_BehaviorInteractWithFaces_H__

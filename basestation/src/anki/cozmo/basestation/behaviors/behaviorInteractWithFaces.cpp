/**
 * File: behaviorInteractWithFaces.cpp
 *
 * Author: Andrew Stein
 * Date:   7/30/15
 *
 * Description: Implements Cozmo's "InteractWithFaces" behavior, which tracks/interacts with faces if it finds one.
 *
 * Copyright: Anki, Inc. 2015
 **/

#include "anki/cozmo/basestation/behaviors/behaviorInteractWithFaces.h"

#include "anki/cozmo/basestation/robot.h"
#include "anki/cozmo/basestation/cozmoActions.h"
#include "anki/cozmo/basestation/events/ankiEvent.h"
#include "anki/cozmo/basestation/externalInterface/externalInterface.h"
#include "anki/cozmo/basestation/keyframe.h"
#include "anki/cozmo/basestation/faceAnimationManager.h"
#include "anki/cozmo/basestation/moodSystem/moodManager.h"

#include "anki/common/basestation/math/point_impl.h"

#include "anki/cozmo/shared/cozmoConfig.h"

#include <opencv2/highgui/highgui.hpp>

#include "clad/externalInterface/messageEngineToGame.h"

#define DO_FACE_MIMICKING 0
#define DO_TOO_CLOSE_SCARED 0

namespace Anki {
namespace Cozmo {
  
  using namespace ExternalInterface;

  BehaviorInteractWithFaces::BehaviorInteractWithFaces(Robot &robot, const Json::Value& config)
  : IBehavior(robot, config)
  {
    _name = "Faces";

    // TODO: Init timeouts, etc, from Json config

    SubscribeToTags({{
      EngineToGameTag::RobotObservedFace,
      EngineToGameTag::RobotDeletedFace,
      EngineToGameTag::RobotCompletedAction
    }});
    
    // Primarily loneliness and then boredom -> InteractWithFaces
    AddEmotionScorer(EmotionScorer(EmotionType::Social,  Anki::Util::GraphEvaluator2d({{-1.0f, 1.0f}, { 0.0f, 1.0f}, {0.2f, 0.5f}, {1.0f, 0.1f}}), false));
    AddEmotionScorer(EmotionScorer(EmotionType::Excited, Anki::Util::GraphEvaluator2d({{-1.0f, 1.0f}, { 0.0f, 1.0f}, {0.5f, 0.6f}, {1.0f, 0.5f}}), false));
  }
  
  Result BehaviorInteractWithFaces::InitInternal(Robot& robot, double currentTime_sec, bool isResuming)
  {
    if (isResuming && (_resumeState != State::Interrupted))
    {
      if (currentTime_sec > _timeWhenInterrupted)
      {
        const double timeWaitingToResume = currentTime_sec - _timeWhenInterrupted;
        if (_newFaceAnimCooldownTime > 0.0)
        {
          _newFaceAnimCooldownTime += timeWaitingToResume;
        }
      }
      _currentState = _resumeState;
      _resumeState = State::Interrupted;
      //robot.GetMoveComponent().EnableTrackToFace(); // [MarkW:TODO] If we disabled TrackToFace on interrupt we might want to restore it here?
    }
    else
    {
      _currentState = State::Inactive;
    }
    
    _timeWhenInterrupted = 0.0;
    
    // Make sure the robot's idle animation is set to use Live, since we are
    // going to stream live face mimicking
    robot.SetIdleAnimation(AnimationStreamer::LiveAnimation);
    
    return RESULT_OK;
  }
  
  BehaviorInteractWithFaces::~BehaviorInteractWithFaces()
  {

  }
  
  void BehaviorInteractWithFaces::AlwaysHandle(const EngineToGameEvent& event, const Robot& robot)
  {
    switch(event.GetData().GetTag())
    {
      case EngineToGameTag::RobotObservedFace:
        HandleRobotObservedFace(robot, event);
        break;
        
      case EngineToGameTag::RobotDeletedFace:
        HandleRobotDeletedFace(event);
        break;
        
      case EngineToGameTag::RobotCompletedAction:
        // handled by WhileRunning handler
        break;
        
      default:
        PRINT_NAMED_ERROR("BehaviorInteractWithFaces.AlwaysHandle.InvalidTag",
                          "Received event with unhandled tag %hhu.",
                          event.GetData().GetTag());
        break;
    }
  }
  
  void BehaviorInteractWithFaces::HandleWhileRunning(const EngineToGameEvent& event, Robot& robot)
  {
    switch(event.GetData().GetTag())
    {
      case EngineToGameTag::RobotObservedFace:
      case EngineToGameTag::RobotDeletedFace:
        // Handled by AlwaysHandle
        break;
        
      case EngineToGameTag::RobotCompletedAction:
        HandleRobotCompletedAction(robot, event);
        break;
        
      default:
        PRINT_NAMED_ERROR("BehaviorInteractWithFaces.AlwaysHandle.InvalidTag",
                          "Received event with unhandled tag %hhu.",
                          event.GetData().GetTag());
        break;
    }
  }
  
  void ResetFaceToNeutral(Robot& robot)
  {
#if DO_FACE_MIMICKING
    robot.GetMoveComponent().DisableTrackToFace();
    ProceduralFace resetFace;
    auto oldTimeStamp = robot.GetProceduralFace().GetTimeStamp();
    oldTimeStamp += IKeyFrame::SAMPLE_LENGTH_MS;
    resetFace.SetTimeStamp(oldTimeStamp);
    robot.SetProceduralFace(resetFace);
#endif
  }
  
  bool BehaviorInteractWithFaces::IsRunnable(const Robot& robot, double currentTime_sec) const
  {
    return !_interestingFacesOrder.empty();
  }
  
  IBehavior::Status BehaviorInteractWithFaces::UpdateInternal(Robot& robot, double currentTime_sec)
  {
    MoodManager& moodManager = robot.GetMoodManager();
    
    Status status = Status::Running;
    
    switch(_currentState)
    {
      case State::Inactive:
      {
        _stateName = "Inactive";
        
        // If we're still finishing an action, just wait
        if(_isActing)
        {
          break;
        }
        
        // If enough time has passed since we looked down toward the ground, do that now
        if (currentTime_sec - _lastGlanceTime >= kGlanceDownInterval_sec)
        {
          float headAngle = robot.GetHeadAngle();
          
          // Move head down to check for a block
          MoveHeadToAngleAction* moveHeadAction = new MoveHeadToAngleAction(0);
          robot.GetActionList().QueueActionAtEnd(IBehavior::sActionSlot, moveHeadAction);
          
          // Now move the head back up to the angle it was previously at
          moveHeadAction = new MoveHeadToAngleAction(headAngle);
          
          robot.GetActionList().QueueActionAtEnd(IBehavior::sActionSlot, moveHeadAction);
          _lastActionTag = moveHeadAction->GetTag();
          _isActing = true;
          _lastGlanceTime = currentTime_sec;
          break;
        }
        
        // If we don't have any faces to care about, we're done here
        auto iterFirst = _interestingFacesOrder.begin();
        if (_interestingFacesOrder.end() == iterFirst)
        {
          _currentState = State::Interrupted;
          break;
        }
        
        auto faceID = *iterFirst;
        const Face* face = robot.GetFaceWorld().GetFace(faceID);
        if(face == nullptr)
        {
          PRINT_NAMED_ERROR("BehaviorInteractWithFaces.Update.InvalidFaceID",
                            "Got event that face ID %lld was observed, but it wasn't found.",
                            faceID);
          break;
        }
        
        auto dataIter = _interestingFacesData.find(faceID);
        if (_interestingFacesData.end() == dataIter)
        {
          PRINT_NAMED_ERROR("BehaviorInteractWithFaces.Update.MissingInteractionData",
                            "Failed to find interaction data associated with faceID %llu", faceID);
          break;
        }
        
        if (_newFaceAnimCooldownTime == 0.0)
        {
          _newFaceAnimCooldownTime = currentTime_sec;
        }
        // If we haven't played our init anim yet for this face and it's been awhile since we did so, do so and break early
        if (!dataIter->second._playedInitAnim && currentTime_sec >= _newFaceAnimCooldownTime)
        {
          robot.GetActionList().QueueActionAtEnd(IBehavior::sActionSlot, new FacePoseAction(face->GetHeadPose(), 0, DEG_TO_RAD(179)));
          PlayAnimation(robot, "ID_react2block_01");
          moodManager.AddToEmotions(EmotionType::Happy,  kEmotionChangeMedium,
                                    EmotionType::Social, kEmotionChangeMedium,
                                    EmotionType::Excited,    kEmotionChangeSmall,  "SeeSomethingNew");
          dataIter->second._playedInitAnim = true;
          _newFaceAnimCooldownTime = currentTime_sec + kSeeNewFaceAnimationCooldown_sec;
          break;
        }
        
        dataIter->second._trackingStart_sec = currentTime_sec;
        
        // Start tracking face
        UpdateBaselineFace(robot, face);
        
        PRINT_NAMED_INFO("BehaviorInteractWithFaces.Update.SwitchToTracking",
                         "Observed face %llu while looking around, switching to tracking.", faceID);
        _currentState = State::TrackingFace;
        break;
      }
        
      case State::TrackingFace:
      {
        _stateName = "TrackingFace";
        
        auto faceID = robot.GetMoveComponent().GetTrackToFace();
        // If we aren't tracking the first faceID in the list, something's wrong
        if (_interestingFacesOrder.empty() || _interestingFacesOrder.front() != faceID)
        {
          // The face we're tracking doesn't match the first one in our list, so reset our state to select the right one
          PRINT_NAMED_INFO("BehaviorInteractWithFaces.Update.SwitchToInactive",
                           "faceID %lld not first of %lu interesting faces",
                           faceID,
                           _interestingFacesOrder.size());
          robot.GetMoveComponent().DisableTrackToFace();
          _currentState = State::Inactive;
          break;
        }
        
        // If too much time has passed since we last saw this face, remove it go back to inactive state and find a new face
        auto lastSeen = _interestingFacesData[faceID]._lastSeen_sec;
        if(currentTime_sec - lastSeen > _trackingTimeout_sec)
        {
          robot.GetMoodManager().AddToEmotions(EmotionType::Happy,  -kEmotionChangeVerySmall,
                                               EmotionType::Social, -kEmotionChangeVerySmall, "LostFace");
          
          robot.GetMoveComponent().DisableTrackToFace();
          _interestingFacesOrder.erase(_interestingFacesOrder.begin());
          _interestingFacesData.erase(faceID);
          
          PRINT_NAMED_INFO("BehaviorInteractWithFaces.Update.DisablingTracking",
                           "Current t=%.2f - lastSeen time=%.2f > timeout=%.2f. "
                           "Switching back to looking around.",
                           currentTime_sec, lastSeen, _trackingTimeout_sec);
          _currentState = State::Inactive;
          break;
        }
        
        // If we've watched this face longer than it's considered interesting, put it on cooldown and go to inactive
        auto watchingFaceDuration = currentTime_sec - _interestingFacesData[faceID]._trackingStart_sec;
        if (watchingFaceDuration >= kFaceInterestingDuration_sec)
        {
          robot.GetMoodManager().AddToEmotions(EmotionType::Happy,   kEmotionChangeSmall,
                                               EmotionType::Excited, kEmotionChangeSmall,
                                               EmotionType::Social,  kEmotionChangeLarge,  "LotsOfFace");

          robot.GetMoveComponent().DisableTrackToFace();
          _interestingFacesOrder.erase(_interestingFacesOrder.begin());
          _interestingFacesData.erase(faceID);
          _cooldownFaces[faceID] = currentTime_sec + kFaceCooldownDuration_sec;
          
          PRINT_NAMED_INFO("BehaviorInteractWithFaces.Update.FaceOnCooldown",
                           "WatchingFaceDuration %.2f >= InterestingDuration %.2f.",
                           watchingFaceDuration, kFaceInterestingDuration_sec);
          _currentState = State::Inactive;
        }
        
        // We need a face to work with
        const Face* face = robot.GetFaceWorld().GetFace(faceID);
        if(face == nullptr)
        {
          robot.GetMoodManager().AddToEmotions(EmotionType::Happy,  -kEmotionChangeVerySmall,
                                               EmotionType::Social, -kEmotionChangeVerySmall, "InvalidFace");
          
          PRINT_NAMED_ERROR("BehaviorInteractWithFaces.Update.InvalidFaceID",
                            "Updating with face ID %lld, but it wasn't found.",
                            faceID);
          robot.GetMoveComponent().DisableTrackToFace();
          _currentState = State::Inactive;
          break;
        }
        
        // Update cozmo's face based on our currently focused face
        UpdateProceduralFace(robot, _crntProceduralFace, *face);
        
#if DO_TOO_CLOSE_SCARED
        if(!_isActing &&
           (currentTime_sec - _lastTooCloseScaredTime) > kTooCloseScaredInterval_sec)
        {
          Pose3d headWrtRobot;
          bool headPoseRetrieveSuccess = face->GetHeadPose().GetWithRespectTo(robot.GetPose(), headWrtRobot);
          if(!headPoseRetrieveSuccess)
          {
            PRINT_NAMED_ERROR("BehaviorInteractWithFaces.HandleRobotObservedFace.PoseWrtFail","");
            break;
          }
          
          Vec3f headTranslate = headWrtRobot.GetTranslation();
          headTranslate.z() = 0.0f; // We only want to work with XY plane distance
          auto distSqr = headTranslate.LengthSq();
          if(distSqr < (kTooCloseDistance_mm * kTooCloseDistance_mm))
          {
            // The head is very close (scary!). Move backward along the line from the
            // robot to the head.
            PRINT_NAMED_INFO("BehaviorInteractWithFaces.HandleRobotObservedFace.Shocked",
                             "Head is %.1fmm away: playing shocked anim.",
                             headWrtRobot.GetTranslation().Length());
            
            // Relinquish control over head/wheels so animation plays correctly,
            robot.GetMoveComponent().DisableTrackToFace();
            
            PlayAnimation(robot, "Demo_Face_Interaction_ShockedScared_A");
            robot.GetMoodManager().AddToEmotion(EmotionType::Brave, -kEmotionChangeMedium, "CloseFace");
            _lastTooCloseScaredTime = currentTime_sec;
          }
        }
#else
        // avoid pesky unused variable error
        (void)_lastTooCloseScaredTime;
#endif
        
        break;
      }
        
      case State::Interrupted:
      {
        _stateName = "Interrupted";
        
        status = Status::Complete;
        break;
      }
        
      default:
        status = Status::Failure;
        
    } // switch(_currentState)
    
    if(Status::Running != status || _currentState == State::Inactive) {
      ResetFaceToNeutral(robot);
    }
    
    return status;
  } // Update()
  
  void BehaviorInteractWithFaces::PlayAnimation(Robot& robot, const std::string& animName)
  {
    PlayAnimationAction* animAction = new PlayAnimationAction(animName);
    robot.GetActionList().QueueActionAtEnd(IBehavior::sActionSlot, animAction);
    _lastActionTag = animAction->GetTag();
    _isActing = true;
  }
  
  Result BehaviorInteractWithFaces::InterruptInternal(Robot& robot, double currentTime_sec, bool isShortInterrupt)
  {
    _resumeState = isShortInterrupt ? _currentState : State::Interrupted;
    _timeWhenInterrupted = currentTime_sec;

    if (_resumeState == State::Interrupted)
    {
      robot.GetMoveComponent().DisableTrackToFace();
    }
    _currentState = State::Interrupted;
    
    return RESULT_OK;
  }
  
#pragma mark -
#pragma mark Signal Handlers

#if DO_FACE_MIMICKING
  inline static f32 GetAverageHeight(const Vision::TrackedFace::Feature& feature,
                                     const Point2f relativeTo, const Radians& faceAngle_rad)
  {
    f32 height = 0.f;
    for(auto point : feature) {
      point -= relativeTo;
      height += -point.x()*std::sin(-faceAngle_rad.ToFloat()) + -point.y()*std::cos(-faceAngle_rad.ToFloat());
    }
    height /= static_cast<f32>(feature.size());
    return height;
  }
  
  inline static f32 GetEyeHeight(const Vision::TrackedFace* face)
  {
    RotationMatrix2d R(-face->GetHeadRoll());
    
    f32 avgEyeHeight = 0.f;
    
    for(auto iFeature : {Vision::TrackedFace::FeatureName::LeftEye, Vision::TrackedFace::FeatureName::RightEye})
    {
      f32 maxY = std::numeric_limits<f32>::min();
      f32 minY = std::numeric_limits<f32>::max();
      
      for(auto point : face->GetFeature(iFeature)) {
        point = R*point;
        if(point.y() < minY) {
          minY = point.y();
        }
        if(point.y() > maxY) {
          maxY = point.y();
        }
      }
      
      avgEyeHeight += maxY - minY;
    }
    
    avgEyeHeight *= 0.5f;
    return avgEyeHeight;
  }
#endif

  void BehaviorInteractWithFaces::UpdateBaselineFace(Robot& robot, const Vision::TrackedFace* face)
  {
    robot.GetMoveComponent().EnableTrackToFace(face->GetID(), false);

#if DO_FACE_MIMICKING
    
    const Radians& faceAngle = face->GetHeadRoll();
    
    // Record baseline eyebrow heights to compare to for checking if they've
    // raised/lowered in the future
    const Face::Feature& leftEyeBrow  = face->GetFeature(Face::FeatureName::LeftEyebrow);
    const Face::Feature& rightEyeBrow = face->GetFeature(Face::FeatureName::RightEyebrow);
    
    // TODO: Roll correction (normalize roll before checking height?)
    _baselineLeftEyebrowHeight = GetAverageHeight(leftEyeBrow, face->GetLeftEyeCenter(), faceAngle);
    _baselineRightEyebrowHeight = GetAverageHeight(rightEyeBrow, face->GetRightEyeCenter(), faceAngle);
    
    _baselineEyeHeight = GetEyeHeight(face);
    
    _baselineIntraEyeDistance = face->GetIntraEyeDistance();
#else
    // hack to avoid unused warning
    (void)_baselineEyeHeight;
    (void)_baselineIntraEyeDistance;
    (void)_baselineLeftEyebrowHeight;
    (void)_baselineRightEyebrowHeight;
#endif
  }
  
  void BehaviorInteractWithFaces::HandleRobotObservedFace(const Robot& robot, const EngineToGameEvent& event)
  {
    assert(event.GetData().GetTag() == EngineToGameTag::RobotObservedFace);
    
    const RobotObservedFace& msg = event.GetData().Get_RobotObservedFace();
    
    Face::ID_t faceID = static_cast<Face::ID_t>(msg.faceID);
    
    // We need a face to work with
    const Face* face = robot.GetFaceWorld().GetFace(faceID);
    if(face == nullptr)
    {
      PRINT_NAMED_ERROR("BehaviorInteractWithFaces.HandleRobotObservedFace.InvalidFaceID",
                        "Got event that face ID %lld was observed, but it wasn't found.", faceID);
      return;
    }
    
    auto iter = _cooldownFaces.find(faceID);
    // If we have a cooldown entry for this face, check if the cooldown time has passed
    if (_cooldownFaces.end() != iter && iter->second < event.GetCurrentTime())
    {
      _cooldownFaces.erase(iter);
      iter = _cooldownFaces.end();
    }
    
    // This face is still on cooldown, so ignore its observation
    if (_cooldownFaces.end() != iter)
    {
      return;
    }
    
    Pose3d headPose;
    bool gotPose = face->GetHeadPose().GetWithRespectTo(robot.GetPose(), headPose);
    if (!gotPose)
    {
      PRINT_NAMED_ERROR("BehaviorInteractWithFaces.HandleRobotObservedFace.InvalidFacePose",
                        "Got event that face ID %lld was observed, but face pose wasn't found.", faceID);

      return;
    }
    
    Vec3f distVec = headPose.GetTranslation();
    distVec.z() = 0;
    auto dataIter = _interestingFacesData.find(faceID);

    // If we do have data on this face id but now it's too far, remove it
    if (_interestingFacesData.end() != dataIter
        && distVec.LengthSq() > (kTooFarDistance_mm * kTooFarDistance_mm))
    {
      PRINT_NAMED_DEBUG("BehaviorInteractWithFaces.RemoveFace",
                        "face %lld is too far (%f > %f), removing",
                        faceID,
                        distVec.Length(),
                        kTooFarDistance_mm);
      RemoveFaceID(faceID);
      return;
    }
    // If we aren't tracking this face and it's close enough, add it
    else if (_interestingFacesData.end() == dataIter
             && distVec.LengthSq() < (kCloseEnoughDistance_mm * kCloseEnoughDistance_mm))
    {
      
      _interestingFacesOrder.push_back(faceID);
      auto insertRet = _interestingFacesData.insert( { faceID, FaceData() } );
      if (insertRet.second)
      {
        dataIter = insertRet.first;
      }
    }

    // If we are now keeping track of this faceID, update its last seen
    if (_interestingFacesData.end() != dataIter)
    {
      dataIter->second._lastSeen_sec = event.GetCurrentTime();
    }
  }
  
  void BehaviorInteractWithFaces::HandleRobotDeletedFace(const EngineToGameEvent& event)
  {
    const RobotDeletedFace& msg = event.GetData().Get_RobotDeletedFace();
    
    RemoveFaceID(static_cast<Face::ID_t>(msg.faceID));
  }
  
  void BehaviorInteractWithFaces::RemoveFaceID(Face::ID_t faceID)
  {
    auto dataIter = _interestingFacesData.find(faceID);
    if (_interestingFacesData.end() != dataIter)
    {
      _interestingFacesData.erase(dataIter);
    }
    
    auto orderIter = _interestingFacesOrder.begin();
    while (_interestingFacesOrder.end() != orderIter)
    {
      if ((*orderIter) == faceID)
      {
        orderIter = _interestingFacesOrder.erase(orderIter);
      }
      else
      {
        orderIter++;
      }
    }
  }
  
  void BehaviorInteractWithFaces::UpdateProceduralFace(Robot& robot, ProceduralFace& proceduralFace, const Face& face) const
  {
#if DO_FACE_MIMICKING
    ProceduralFace prevProcFace(proceduralFace);
    
    const Radians& faceAngle = face.GetHeadRoll();
    const f32 distanceNorm =  face.GetIntraEyeDistance() / _baselineIntraEyeDistance;
    
    if(_baselineLeftEyebrowHeight != 0.f && _baselineRightEyebrowHeight != 0.f)
    {
      // If eyebrows have raised/lowered (based on distance from eyes), mimic their position:
      const Face::Feature& leftEyeBrow  = face.GetFeature(Face::FeatureName::LeftEyebrow);
      const Face::Feature& rightEyeBrow = face.GetFeature(Face::FeatureName::RightEyebrow);
      
      const f32 leftEyebrowHeight  = GetAverageHeight(leftEyeBrow, face.GetLeftEyeCenter(), faceAngle);
      const f32 rightEyebrowHeight = GetAverageHeight(rightEyeBrow, face.GetRightEyeCenter(), faceAngle);
      
      // Get expected height based on intra-eye distance
      const f32 expectedLeftEyebrowHeight = distanceNorm * _baselineLeftEyebrowHeight;
      const f32 expectedRightEyebrowHeight = distanceNorm * _baselineRightEyebrowHeight;
      
      // Compare measured distance to expected
      const f32 leftEyebrowHeightScale = (leftEyebrowHeight - expectedLeftEyebrowHeight)/expectedLeftEyebrowHeight;
      const f32 rightEyebrowHeightScale = (rightEyebrowHeight - expectedRightEyebrowHeight)/expectedRightEyebrowHeight;
      
      // Map current eyebrow heights onto Cozmo's face, based on measured baseline values
      proceduralFace.GetParams().SetParameter(ProceduralFace::WhichEye::Left,
                                              ProceduralFace::Parameter::UpperLidY,
                                              leftEyebrowHeightScale);
      
      proceduralFace.GetParams().SetParameter(ProceduralFace::WhichEye::Right,
                                              ProceduralFace::Parameter::UpperLidY,
                                              rightEyebrowHeightScale);
      
    }
    
    const f32 expectedEyeHeight = distanceNorm * _baselineEyeHeight;
    const f32 eyeHeightFraction = (GetEyeHeight(&face) - expectedEyeHeight)/expectedEyeHeight + .1f; // bias a little larger
    
    // Adjust pupil positions depending on where face is in the image
    Point2f newPupilPos(face.GetLeftEyeCenter());
    newPupilPos += face.GetRightEyeCenter();
    newPupilPos *= 0.5f;
    
    const Vision::CameraCalibration& camCalib = robot.GetVisionComponent().GetCameraCalibration();
    Point2f imageHalfSize(camCalib.GetNcols()/2, camCalib.GetNrows()/2);
    newPupilPos -= imageHalfSize; // make relative to image center
    newPupilPos /= imageHalfSize; // scale to be between -1 and 1

    // magic value to make pupil tracking feel more realistic
    // TODO: Actually intersect vector from robot head to tracked face with screen
    newPupilPos *= .75f;
    
    for(auto whichEye : {ProceduralFace::WhichEye::Left, ProceduralFace::WhichEye::Right}) {
      if(_baselineEyeHeight != 0.f) {
        proceduralFace.GetParams().SetParameter(whichEye, ProceduralFace::Parameter::EyeScaleX,
                                                std::max(-.8f, std::min(.8f, eyeHeightFraction)));
      }
    }
    
    // If face angle is rotated, mirror the rotation (with a deadzone)
    if(std::abs(faceAngle.getDegrees()) > 5) {
      proceduralFace.GetParams().SetFaceAngle(faceAngle.getDegrees());
    } else {
      proceduralFace.GetParams().SetFaceAngle(0);
    }
    
    // Smoothing
    proceduralFace.GetParams().Interpolate(prevProcFace.GetParams(), proceduralFace.GetParams(), 0.9f);
    
    proceduralFace.SetTimeStamp(face.GetTimeStamp());
    proceduralFace.MarkAsSentToRobot(false);
    robot.SetProceduralFace(proceduralFace);
#endif // DO_FACE_MIMICKING
  }
  
  void BehaviorInteractWithFaces::HandleRobotCompletedAction(Robot& robot, const EngineToGameEvent& event)
  {
    const RobotCompletedAction& msg = event.GetData().Get_RobotCompletedAction();
    
    if(msg.idTag == _lastActionTag)
    {
#if DO_FACE_MIMICKING
      robot.SetProceduralFace(_crntProceduralFace);
#endif
      _isActing = false;
    }
  }
} // namespace Cozmo
} // namespace Anki

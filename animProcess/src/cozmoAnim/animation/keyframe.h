/**
 * File: keyframe.h
 *
 * Authors: Andrew Stein
 * Created: 2015-06-25
 *
 * Description: 
 *   Defines the various KeyFrames used to store an animation on the
 *   the robot, all of which inherit from a common interface, 
 *   IKeyFrame.
 *
 * Copyright: Anki, Inc. 2015
 *
 **/


#ifndef ANKI_COZMO_CANNED_KEYFRAME_H
#define ANKI_COZMO_CANNED_KEYFRAME_H

#include "anki/common/basestation/colorRGBA.h"
#include "anki/vision/basestation/image.h"
#include "cozmoAnim/animation/proceduralFace.h"
#include "clad/robotInterface/messageEngineToRobot.h"
#include "clad/types/ledTypes.h"
#include "clad/audio/audioEventTypes.h"
#include "util/random/randomGenerator.h"
#include "json/json-forwards.h"

#define kNoAudioRefIndex -1

namespace CozmoAnim {
  struct HeadAngle;
  struct LiftHeight;
  struct RobotAudio;
  struct FaceAnimation;
  struct ProceduralFace;
  struct Event;
  struct BackpackLights;
  struct BodyMotion;
  struct RecordHeading;
  struct TurnToRecordedHeading;
}

namespace Anki {
  
  namespace Vision {
    class ImageRGB565;
  }
  
namespace Cozmo {
  
  // Forward declaration
  namespace RobotInterface {
  struct EngineToRobot;
  //enum class EngineToRobotTag : uint8_t;
  }
  
  // IKeyFrame defines an abstract interface for all KeyFrames below.
  class IKeyFrame
  {
  public:
    
    IKeyFrame();
    //IKeyFrame(const Json::Value& root);
    ~IKeyFrame();
    
    // Returns true if the animation's time has reached frame's "trigger" time
    bool IsTimeToPlay(TimeStamp_t animationTime_ms) const;
    // Returns true if current time has reached frame's "trigger" time, relative
    // to the given start time
    bool IsTimeToPlay(TimeStamp_t startTime_ms, TimeStamp_t currTime_ms) const;
    
    // Returns the time to trigger whatever change is implied by the KeyFrame
    TimeStamp_t GetTriggerTime() const { return _triggerTime_ms; }
    
    // Returns the last time specified by the keyframe - in most cases the
    // trigger time + duration
    virtual TimeStamp_t GetKeyFrameFinalTimestamp_ms() const = 0;
    
    // Set the triggert time, relative to the start time of track the animation
    // is playing in
    void SetTriggerTime(TimeStamp_t triggerTime_ms) { _triggerTime_ms = triggerTime_ms; }
    
    // Set all members from Json or FlatBuffers. Calls virtual SetMembersFromJson() method so subclasses can specify
    // how to populate their members. Second argument is used to print nicer debug strings if something goes wrong
    Result DefineFromJson(const Json::Value &json, const std::string& animNameDebug = "");
    
    // Fill some kind of message for streaming and return it. Return nullptr
    // if not available.
    virtual RobotInterface::EngineToRobot* GetStreamMessage() = 0;
    
    // Whether or not this KeyFrame is "done" after calling GetStreamMessage().
    // Override for special keyframes that need to keep parceling out data into
    // multiple returned messages.
    virtual bool IsDone() { return true; }
    
  protected:
    
    // Populate members from Json
    virtual Result SetMembersFromJson(const Json::Value &jsonRoot, const std::string& animNameDebug = "") = 0;
    
    TimeStamp_t GetCurrentTime() const { return _currentTime_ms; }
    
    // Increments member currentTime_ms by ANIM_TIME_STEP_MS and checks it against durationTime_ms.
    // Once currentTime_ms >= durationTime, it gets reset to 0 to be ready to call again.
    bool IsDoneHelper(TimeStamp_t durationTime_ms);
    
    //void SetIsValid(bool isValid) { _isValid = isValid; }
    
    Util::RandomGenerator& GetRNG() const;

    // The trigger time is protected instead of private so derived classes can access it.
    TimeStamp_t   _triggerTime_ms = 0;

  private:
    
    // A random number generator for all keyframes to share (for adding variability)
    static Util::RandomGenerator sRNG;

    TimeStamp_t   _currentTime_ms = 0;
    
  }; // class IKeyFrame
  
  inline Util::RandomGenerator& IKeyFrame::GetRNG() const {
    return sRNG;
  }
  
  
  // A HeadAngleKeyFrame specifies the time to _start_ moving the head towards
  // a given angle (with optional variation), and how long to take to get there.
  class HeadAngleKeyFrame : public IKeyFrame
  {
  public:
    HeadAngleKeyFrame() { }
    HeadAngleKeyFrame(s8 angle_deg, u8 angle_variability_deg, TimeStamp_t duration_ms);
    
    Result DefineFromFlatBuf(const CozmoAnim::HeadAngle* headAngleKeyframe, const std::string& animNameDebug);

    virtual RobotInterface::EngineToRobot* GetStreamMessage() override;
    
    static const std::string& GetClassName() {
      static const std::string ClassName("HeadAngleKeyFrame");
      return ClassName;
    }
    
    virtual TimeStamp_t GetKeyFrameFinalTimestamp_ms() const override { return _triggerTime_ms + _durationTime_ms;}
    
  protected:
    virtual Result SetMembersFromJson(const Json::Value &jsonRoot, const std::string& animNameDebug = "") override;
    virtual Result SetMembersFromFlatBuf(const CozmoAnim::HeadAngle* headAngleKeyframe, const std::string& animNameDebug = "");
    
  private:
    TimeStamp_t _durationTime_ms;
    s8          _angle_deg;
    u8          _angleVariability_deg;
    
    RobotInterface::SetHeadAngle _streamHeadMsg;
  }; // class HeadAngleKeyFrame
  
  
  // A LiftHeightKeyFrame specifies the time to _start_ moving the lift towards
  // a given height (with optional variation), and how long to take to get there.
  class LiftHeightKeyFrame : public IKeyFrame
  {
  public:
    LiftHeightKeyFrame() { }
    LiftHeightKeyFrame(u8 height_mm, u8 heightVariability_mm, TimeStamp_t duration_ms);
    
    Result DefineFromFlatBuf(const CozmoAnim::LiftHeight* liftHeightKeyframe, const std::string& animNameDebug);

    virtual RobotInterface::EngineToRobot* GetStreamMessage() override;
    
    static const std::string& GetClassName() {
      static const std::string ClassName("LiftHeightKeyFrame");
      return ClassName;
    }
    
    virtual TimeStamp_t GetKeyFrameFinalTimestamp_ms() const override { return _triggerTime_ms + _durationTime_ms;}
    
  protected:
    virtual Result SetMembersFromJson(const Json::Value &jsonRoot, const std::string& animNameDebug = "") override;
    virtual Result SetMembersFromFlatBuf(const CozmoAnim::LiftHeight* liftHeightKeyframe, const std::string& animNameDebug = "");
    
  private:
    TimeStamp_t _durationTime_ms;
    u8          _height_mm;
    u8          _heightVariability_mm;
    
    RobotInterface::SetLiftHeight _streamLiftMsg;
    
  }; // class LiftHeightKeyFrame


  // A RobotAudioKeyFrame references a single "sound" which is made of lots
  // of "samples" to be individually streamed to the robot.
  class RobotAudioKeyFrame : public IKeyFrame
  {
  public:
    
    struct AudioRef {
      AudioMetaData::GameEvent::GenericEvent audioEvent;
      float volume;
      float probability;   // random play weight
      bool audioAlts; // The audio event has alternate or random audio track playback, avoid replaying event
      
      AudioRef( AudioMetaData::GameEvent::GenericEvent audioEvent = AudioMetaData::GameEvent::GenericEvent::Invalid,
                float volume      = 1.0f,
                float probability = 1.0f,
                bool audioAlts    = false )
      : audioEvent( audioEvent ),
        volume( volume )
      , probability( probability )
      , audioAlts( audioAlts ) {};
    };
    
    RobotAudioKeyFrame() { }
    RobotAudioKeyFrame( AudioRef&& audioRef, TimeStamp_t triggerTime_ms );
    
    Result DefineFromFlatBuf(const CozmoAnim::RobotAudio* audioKeyframe, const std::string& animNameDebug);

    // NOTE: Always returns nullptr for RobotAudioKeyframe!
    virtual RobotInterface::EngineToRobot* GetStreamMessage() override { return nullptr; };
    
    static const std::string& GetClassName() {
      static const std::string ClassName("RobotAudioKeyFrame");
      return ClassName;
    }

    // The GetAudioRefIndex() method will return the index that should be used to return the audio
    // reference from '_audioReferences'. By default, this method will take the probability of each
    // audio reference into account, but that can be overridden by passing in 'false'. This method
    // will return 'kNoAudioRefIndex' if '_audioReferences' is empty, if the total probability of
    // all audio events combined exceeds 1.0 or if probabilities were taken into account and no
    // audio event should be used.
    const int8_t GetAudioRefIndex(bool useProbability = true) const;

    // The GetNumAudioRefs() method will return the number of audio references for
    // this RobotAudioKeyFrame (which is the size of the '_audioReferences' vector)
    const int8_t GetNumAudioRefs() const;

    // The GetAudioRef() method will return an AudioRef. Callers should specify which one to return
    // as an '_audioReferences' index and GetAudioRefIndex() can be used to lookup that index.
    const AudioRef& GetAudioRef(const int8_t selectedAudioIndex) const;
    
    virtual TimeStamp_t GetKeyFrameFinalTimestamp_ms() const override { return _triggerTime_ms;}
    
  protected:
    virtual Result SetMembersFromJson(const Json::Value &jsonRoot, const std::string& animNameDebug = "") override;
    virtual Result SetMembersFromFlatBuf(const CozmoAnim::RobotAudio* audioKeyframe, const std::string& animNameDebug = "");
    
  private:
    
    Result AddAudioRef(AudioRef&& audioRef);

    std::vector<AudioRef> _audioReferences;
    
  }; // class RobotAudioKeyFrame
    

  // A FaceAnimationKeyFrame is for streaming a set of images to display on the
  // robot's face. It will return a non-NULL message each time GetStreamMessage()
  // is called until there are no more frames left in the animation.
  class FaceAnimationKeyFrame : public IKeyFrame
  {
  public:
    FaceAnimationKeyFrame(const std::string& faceAnimName = "") : _animName(faceAnimName) { }
    
    Result DefineFromFlatBuf(const CozmoAnim::FaceAnimation* faceAnimKeyframe, const std::string& animNameDebug);

    Result Process(const std::string& animNameDebug);

    // The face image isn't actually returned via this function since the
    // message does not go to robot process. Instead, images are grabbed via GetFaceImage().
    // TODO: Is it better to create a wrapper EngineToRobot message so that we don't have
    //       to duplicate keyframe checking logic in animationStreamer?
    virtual RobotInterface::EngineToRobot* GetStreamMessage() override {return nullptr;}
    
    static const std::string& GetClassName() {
      static const std::string ClassName("FaceAnimationKeyFrame");
      return ClassName;
    }

    virtual bool IsDone() override;
    
    const std::string& GetName() const { return _animName; }
    
    // This function actually retrieves image data and increments the frame count so that it will
    // retrieve the next image on the next call.
    bool GetFaceImage(Vision::ImageRGB565& imgRGB565);
    
    virtual TimeStamp_t GetKeyFrameFinalTimestamp_ms() const override { return _triggerTime_ms;}
    
  protected:
    virtual Result SetMembersFromJson(const Json::Value &jsonRoot, const std::string& animNameDebug = "") override;
    virtual Result SetMembersFromFlatBuf(const CozmoAnim::FaceAnimation* faceAnimKeyframe, const std::string& animNameDebug = "");
    
  private:
    std::string  _animName;
    s32          _curFrame = 0;
    
  }; // class FaceAnimationKeyFrame
  

  class ProceduralFaceKeyFrame : public IKeyFrame
  {
  public:
    ProceduralFaceKeyFrame() { }
    ProceduralFaceKeyFrame(const ProceduralFace& face, TimeStamp_t triggerTime_ms = 0);

    Result DefineFromFlatBuf(const CozmoAnim::ProceduralFace* procFaceKeyframe, const std::string& animNameDebug);
    
    // Always returns nullptr. Use GetInterpolatedFace() to get the face stored in this
    // keyframe.
    virtual RobotInterface::EngineToRobot* GetStreamMessage() override { return nullptr; }
    
    // Returns message for the face interpolated between the stored face in this
    // keyframe and the one in the next keyframe.
    RobotInterface::EngineToRobot* GetInterpolatedStreamMessage(const ProceduralFaceKeyFrame& nextFrame);
    
    // Returns the interpolated face between the current keyframe and the next.
    // If the nextFrame is nullptr, then this frame's procedural face are returned.
    ProceduralFace GetInterpolatedFace(const ProceduralFaceKeyFrame& nextFrame, const TimeStamp_t currentTime_ms);
    
    static const std::string& GetClassName() {
      static const std::string ClassName("ProceduralFaceKeyFrame");
      return ClassName;
    }
    
    virtual bool IsDone() override;
    
    const ProceduralFace& GetFace() const { return _procFace; }
    
    virtual TimeStamp_t GetKeyFrameFinalTimestamp_ms() const override { return _triggerTime_ms;}

  protected:
    virtual Result SetMembersFromJson(const Json::Value &jsonRoot, const std::string& animNameDebug = "") override;
    virtual Result SetMembersFromFlatBuf(const CozmoAnim::ProceduralFace* procFaceKeyframe, const std::string& animNameDebug = "");
    
  private:
    ProceduralFace  _procFace;
    bool            _isDone = false;

    
    // This is what actually populates the message to stream, and is used
    // by GetStreamMessage() and GetInterpolatedStreamMessage().
    RobotInterface::EngineToRobot* GetStreamMessageHelper(const ProceduralFace& procFace);
    
    void Reset();
    
  }; // class ProceduralFaceKeyFrame
  
  inline ProceduralFaceKeyFrame::ProceduralFaceKeyFrame(const ProceduralFace& face,
                                                        TimeStamp_t triggerTime)
  : _procFace(face)
  {
    SetTriggerTime(triggerTime);
    Reset();
  }

  
  // An EventKeyFrame simply returns an AnimEvent message from the robot
  // for higher precision event timing... like in Speed Tap.
  class EventKeyFrame : public IKeyFrame
  {
  public:
    EventKeyFrame() { }

    Result DefineFromFlatBuf(const CozmoAnim::Event* eventKeyframe, const std::string& animNameDebug);
    
    virtual RobotInterface::EngineToRobot* GetStreamMessage() override;
    
    static const std::string& GetClassName() {
      static const std::string ClassName("EventKeyFrame");
      return ClassName;
    }
    
    virtual TimeStamp_t GetKeyFrameFinalTimestamp_ms() const override { return _triggerTime_ms;}
    
    Anki::Cozmo::AnimEvent GetAnimEvent() const { return _event_id; }
    
  protected:
    virtual Result SetMembersFromJson(const Json::Value &jsonRoot, const std::string& animNameDebug = "") override;
    virtual Result SetMembersFromFlatBuf(const CozmoAnim::Event* eventKeyframe, const std::string& animNameDebug = "");
    
  private:

    Anki::Cozmo::AnimEvent _event_id;
    
  }; // class EventKeyFrame
  
  
  // A BackpackLightsKeyFrame sets the colors of the robot's five backpack lights
  class BackpackLightsKeyFrame : public IKeyFrame
  {
  public:
    BackpackLightsKeyFrame();

    Result DefineFromFlatBuf(CozmoAnim::BackpackLights* backpackKeyframe, const std::string& animNameDebug);
    
    virtual RobotInterface::EngineToRobot* GetStreamMessage() override;
    
    static const std::string& GetClassName() {
      static const std::string ClassName("BackpackLightsKeyFrame");
      return ClassName;
    }
    
    void SetDuration(s32 duration_ms) { _durationTime_ms = duration_ms; }
    
    virtual bool IsDone() override;
    
    virtual TimeStamp_t GetKeyFrameFinalTimestamp_ms() const override { return _triggerTime_ms + _durationTime_ms;}
    
  protected:
    virtual Result SetMembersFromJson(const Json::Value &jsonRoot, const std::string& animNameDebug = "") override;
    virtual Result SetMembersFromFlatBuf(CozmoAnim::BackpackLights* backpackKeyframe, const std::string& animNameDebug = "");
    
  private:
    
    s32 _durationTime_ms;
    RobotInterface::SetBackpackLights _streamMsg;
    
  }; // class BackpackLightsKeyFrame
  
  
  // A BodyMotionKeyFrame controls the wheels to drive straight, turn in place, or
  // drive arcs. They specify the speed and duration of the motion.
  class BodyMotionKeyFrame : public IKeyFrame
  {
  public:
    BodyMotionKeyFrame();
    BodyMotionKeyFrame(s16 speed, s16 curvatureRadius_mm, s32 duration_ms);
    
    Result DefineFromFlatBuf(const CozmoAnim::BodyMotion* bodyKeyframe, const std::string& animNameDebug);

    void CheckRotationSpeed(const std::string& animNameDebug);
    void CheckStraightSpeed(const std::string& animNameDebug);
    void CheckTurnSpeed(const std::string& animNameDebug);

    Result ProcessRadiusString(const std::string& radiusStr, const std::string& animNameDebug);

    virtual RobotInterface::EngineToRobot* GetStreamMessage() override;
    
    static const std::string& GetClassName() {
      static const std::string ClassName("BodyMotionKeyFrame");
      return ClassName;
    }
    
    virtual bool IsDone() override;
    
    s32 GetDurationTime_ms() const { return _durationTime_ms; }
    void EnableStopMessage(bool enable) { _enableStopMessage = enable; }
    
    virtual TimeStamp_t GetKeyFrameFinalTimestamp_ms() const override { return _triggerTime_ms + _durationTime_ms;}
    
  protected:
    virtual Result SetMembersFromJson(const Json::Value &jsonRoot, const std::string& animNameDebug = "") override;
    virtual Result SetMembersFromFlatBuf(const CozmoAnim::BodyMotion* bodyKeyframe, const std::string& animNameDebug = "");
    
  private:
    
    s32 _durationTime_ms;
    bool _enableStopMessage = true;
    
    RobotInterface::DriveWheelsCurvature _streamMsg;
    RobotInterface::DriveWheelsCurvature _stopMsg;
    
  }; // class BodyMotionKeyFrame
  
  
  // A RecordHeadingKeyFrame records an angular heading so that it can be returned
  // to (with an optional offset) using TurnToRecordedHeadingKeyFrame
  class RecordHeadingKeyFrame : public IKeyFrame
  {
  public:
    RecordHeadingKeyFrame();
    
    Result DefineFromFlatBuf(const CozmoAnim::RecordHeading* recordHeadingKeyframe, const std::string& animNameDebug);
    
    virtual RobotInterface::EngineToRobot* GetStreamMessage() override;
    
    static const std::string& GetClassName() {
      static const std::string ClassName("RecordHeadingKeyFrame");
      return ClassName;
    }
    
    virtual bool IsDone() override;
    
    virtual TimeStamp_t GetKeyFrameFinalTimestamp_ms() const override { return _triggerTime_ms;}
    
  protected:
    virtual Result SetMembersFromJson(const Json::Value &jsonRoot, const std::string& animNameDebug = "") override;
    virtual Result SetMembersFromFlatBuf(const CozmoAnim::RecordHeading* recordHeadingKeyframe, const std::string& animNameDebug = "");
    
  private:
    
    RobotInterface::RecordHeading _streamMsg;
    
  }; // class RecordHeadingKeyFrame
  
  
  // A TurnToRecordedHeadingKeyFrame commands the robot to turn to the heading that was
  // previously recorded by a RecordHeadingKeyFrame
  class TurnToRecordedHeadingKeyFrame : public IKeyFrame
  {
  public:
    TurnToRecordedHeadingKeyFrame();
    TurnToRecordedHeadingKeyFrame(s16 offset_deg,
                                  s16 speed_degPerSec,
                                  s16 accel_degPerSec2,
                                  s16 decel_degPerSec2,
                                  u16 tolerance_deg,
                                  u16 numHalfRevs,
                                  bool useShortestDir,
                                  s32 duration_ms);
    
    Result DefineFromFlatBuf(const CozmoAnim::TurnToRecordedHeading* turnToRecordedHeadingKeyframe, const std::string& animNameDebug);
    
    void CheckRotationSpeed(const std::string& animNameDebug);
    
    virtual RobotInterface::EngineToRobot* GetStreamMessage() override;
    
    static const std::string& GetClassName() {
      static const std::string ClassName("TurnToRecordedHeadingKeyFrame");
      return ClassName;
    }
    
    virtual bool IsDone() override;
    
    s32 GetDurationTime_ms() const { return _durationTime_ms; }
    virtual TimeStamp_t GetKeyFrameFinalTimestamp_ms() const override { return _triggerTime_ms + _durationTime_ms;}
    
  protected:
    virtual Result SetMembersFromJson(const Json::Value &jsonRoot, const std::string& animNameDebug = "") override;
    virtual Result SetMembersFromFlatBuf(const CozmoAnim::TurnToRecordedHeading* turnToRecordedHeadingKeyFrame, const std::string& animNameDebug = "");
    
  private:
    
    s32 _durationTime_ms;
    
    RobotInterface::TurnToRecordedHeading _streamMsg;
    
  }; // class TurnToRecordedHeadingKeyFrame
  
  
} // namespace Cozmo
} // namespace Anki

#endif // ANKI_COZMO_CANNED_KEYFRAME_H

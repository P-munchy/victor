/**
 * File: actionInterface.cpp
 *
 * Author: Andrew Stein
 * Date:   8/29/2014
 *
 * Description: Implements interfaces for action states for a robot.
 *
 *              Note about subActions (manually ticking actions inside another action)
 *              Store subActions as unique_ptrs since the subAction is unique to the 
 *              parent and the parent is responsible for managing everything about the
 *              subAction. (see PickupObjectAction for examples)
 *
 * Copyright: Anki, Inc. 2014
 **/

#include "anki/common/basestation/math/poseBase_impl.h"
#include "anki/common/basestation/utils/timer.h"
#include "anki/cozmo/basestation/actions/actionInterface.h"
#include "anki/cozmo/basestation/actions/actionWatcher.h"
#include "anki/cozmo/basestation/components/animTrackHelpers.h"
#include "anki/cozmo/basestation/components/movementComponent.h"
#include "anki/cozmo/basestation/cozmoContext.h"
#include "anki/cozmo/basestation/externalInterface/externalInterface.h"
#include "anki/cozmo/basestation/moodSystem/moodManager.h"
#include "anki/cozmo/basestation/robot.h"
#include "clad/externalInterface/messageEngineToGame.h"
#include "clad/robotInterface/messageEngineToRobot.h"
#include "util/helpers/templateHelpers.h"

#define DEBUG_ANIM_TRACK_LOCKING 0

#define DEBUG_ACTION_RUNNING 0

namespace Anki {
  
  namespace Cozmo {
    
    // Ensure that nobody sets bad tag ranges (we want them all to be mutually exclusive
    static_assert(ActionConstants::FIRST_GAME_TAG   > ActionConstants::INVALID_TAG,      "Game Tag Overlap");
    static_assert(ActionConstants::FIRST_SDK_TAG    > ActionConstants::LAST_GAME_TAG,    "Sdk Tag Overlap");
    static_assert(ActionConstants::FIRST_ENGINE_TAG > ActionConstants::LAST_SDK_TAG,     "Engine Tag Overlap");
    static_assert(ActionConstants::LAST_GAME_TAG    > ActionConstants::FIRST_GAME_TAG,   "Bad Game Tag Range");
    static_assert(ActionConstants::LAST_SDK_TAG     > ActionConstants::FIRST_SDK_TAG,    "Bad Sdk Tag Range");
    static_assert(ActionConstants::LAST_ENGINE_TAG  > ActionConstants::FIRST_ENGINE_TAG, "Bad Engine Tag Range");
    
    static const char * const kLogChannelName = "Actions";
  
    u32 IActionRunner::sTagCounter = ActionConstants::FIRST_ENGINE_TAG;
    std::set<u32> IActionRunner::sInUseTagSet;
    
    u32 IActionRunner::NextIdTag()
    {
      // Post increment IActionRunner::sTagCounter (and loop within the ENGINE_TAG range)
      u32 nextIdTag = IActionRunner::sTagCounter;
      if (IActionRunner::sTagCounter == ActionConstants::LAST_ENGINE_TAG)
      {
        IActionRunner::sTagCounter = ActionConstants::FIRST_ENGINE_TAG;
      }
      else
      {
        ++IActionRunner::sTagCounter;
      }
      
      assert((nextIdTag >= ActionConstants::FIRST_ENGINE_TAG) && (nextIdTag <= ActionConstants::LAST_ENGINE_TAG));
      assert(nextIdTag != ActionConstants::INVALID_TAG);
      
      return nextIdTag;
    }
    
    
    IActionRunner::IActionRunner(Robot& robot,
                                 const std::string name,
                                 const RobotActionType type,
                                 const u8 trackToLock)
    : _robot(robot)
    , _completionUnion(ActionCompletedUnion())
    , _type(type)
    , _name(name)
    , _tracks(trackToLock)
    {
      // Assign every action a unique tag that is not currently in use
      _idTag = NextIdTag();
      
      while (!IActionRunner::sInUseTagSet.insert(_idTag).second) {
        PRINT_NAMED_ERROR("IActionRunner.TagCounterClash", "TagCounters shouldn't overlap");
        _idTag = NextIdTag();
      }
      
      _customTag = _idTag;
      
      // This giant switch is necessary to set the appropriate completion union tags in order
      // to avoid emitting a completion union with an invalid tag
      // There is no default case in order to prevent people from forgetting to add it here
      switch(type)
      {
        case RobotActionType::ALIGN_WITH_OBJECT:
        case RobotActionType::DRIVE_TO_OBJECT:
        case RobotActionType::FACE_PLANT:
        case RobotActionType::PICK_AND_PLACE_INCOMPLETE:
        case RobotActionType::PICKUP_OBJECT_HIGH:
        case RobotActionType::PICKUP_OBJECT_LOW:
        case RobotActionType::PLACE_OBJECT_HIGH:
        case RobotActionType::PLACE_OBJECT_LOW:
        case RobotActionType::POP_A_WHEELIE:
        case RobotActionType::ROLL_OBJECT_LOW:
        case RobotActionType::TURN_TOWARDS_OBJECT:
        {
          _completionUnion.Set_objectInteractionCompleted(ObjectInteractionCompleted());
          break;
        }
      
        case RobotActionType::READ_TOOL_CODE:
        {
          _completionUnion.Set_readToolCodeCompleted(ReadToolCodeCompleted());
          break;
        }
        
        case RobotActionType::PLAY_ANIMATION:
        {
          _completionUnion.Set_animationCompleted(AnimationCompleted());
          break;
        }
      
        case RobotActionType::DEVICE_AUDIO:
        {
          _completionUnion.Set_deviceAudioCompleted(DeviceAudioCompleted());
          break;
        }
        
        case RobotActionType::TRACK_FACE:
        case RobotActionType::TRACK_PET_FACE:
        {
          _completionUnion.Set_trackFaceCompleted(TrackFaceCompleted());
          break;
        }
        
        // These actions don't set completion unions
        case RobotActionType::ASCEND_OR_DESCEND_RAMP:
        case RobotActionType::CALIBRATE_MOTORS:
        case RobotActionType::COMPOUND:
        case RobotActionType::CROSS_BRIDGE:
        case RobotActionType::DISPLAY_FACE_IMAGE:
        case RobotActionType::DISPLAY_PROCEDURAL_FACE:
        case RobotActionType::DRIVE_OFF_CHARGER_CONTACTS:
        case RobotActionType::DRIVE_PATH:
        case RobotActionType::DRIVE_STRAIGHT:
        case RobotActionType::DRIVE_TO_FLIP_BLOCK_POSE:
        case RobotActionType::DRIVE_TO_PLACE_CARRIED_OBJECT:
        case RobotActionType::DRIVE_TO_POSE:
        case RobotActionType::FLIP_BLOCK:
        case RobotActionType::HANG:
        case RobotActionType::MOUNT_CHARGER:
        case RobotActionType::MOVE_HEAD_TO_ANGLE:
        case RobotActionType::MOVE_LIFT_TO_HEIGHT:
        case RobotActionType::PAN_AND_TILT:
        case RobotActionType::PLAY_CUBE_ANIMATION:
        case RobotActionType::SAY_TEXT:
        case RobotActionType::SEARCH_FOR_NEARBY_OBJECT:
        case RobotActionType::TRACK_MOTION:
        case RobotActionType::TRACK_OBJECT:
        case RobotActionType::TRAVERSE_OBJECT:
        case RobotActionType::TURN_IN_PLACE:
        case RobotActionType::TURN_TOWARDS_FACE:
        case RobotActionType::TURN_TOWARDS_IMAGE_POINT:
        case RobotActionType::TURN_TOWARDS_LAST_FACE_POSE:
        case RobotActionType::TURN_TOWARDS_POSE:
        case RobotActionType::UNKNOWN:
        case RobotActionType::VISUALLY_VERIFY_FACE:
        case RobotActionType::VISUALLY_VERIFY_NO_OBJECT_AT_POSE:
        case RobotActionType::VISUALLY_VERIFY_OBJECT:
        case RobotActionType::WAIT:
        case RobotActionType::WAIT_FOR_IMAGES:
        case RobotActionType::WAIT_FOR_LAMBDA:
        {
          _completionUnion.Set_defaultCompleted(DefaultCompleted());
          break;
        }
      }
    }
    
    IActionRunner::~IActionRunner()
    {
      if(!_preppedForCompletion) {
        PRINT_NAMED_ERROR("IActionRunner.Destructor.NotPreppedForCompletion", "[%d]", GetTag());
      }
      
      // Erase the tags as they are no longer in use
      IActionRunner::sInUseTagSet.erase(_customTag);
      IActionRunner::sInUseTagSet.erase(_idTag);
    
      if(!_suppressTrackLocking && _state != ActionResult::NOT_STARTED)
      {
        if(DEBUG_ANIM_TRACK_LOCKING)
        {
          PRINT_CH_INFO(kLogChannelName, "IActionRunner.Destroy.UnlockTracks",
                        "unlocked: (0x%x) %s by %s [%d]",
                        _tracks,
                        AnimTrackHelpers::AnimTrackFlagsToString(_tracks).c_str(),
                        _name.c_str(),
                        _idTag);
        }
        _robot.GetMoveComponent().UnlockTracks(_tracks, GetTag());
      }
      
      _robot.GetActionList().GetActionWatcher().ActionEnding(this);
    }

    
    bool IActionRunner::SetTag(u32 tag)
    {
      // Probably a bad idea to be able to change the tag while the action is running
      if(_state == ActionResult::RUNNING)
      {
        PRINT_NAMED_WARNING("IActionRunner.SetTag", "Action %s [%d] is running unable to set tag to %d",
                            GetName().c_str(),
                            GetTag(),
                            tag);
        _state = ActionResult::BAD_TAG;
        return false;
      }
      
      // If the tag has already been set and the action is not running then erase the current tag in order to
      // set the new one
      if(_customTag != _idTag)
      {
        IActionRunner::sInUseTagSet.erase(_customTag);
      }
      // If this is an invalid tag or is currently in use
      if(tag == static_cast<u32>(ActionConstants::INVALID_TAG) ||
         !IActionRunner::sInUseTagSet.insert(tag).second)
      {
        PRINT_NAMED_ERROR("IActionRunner.SetTag.InvalidTag", "Tag [%d] is invalid", tag);
        _state = ActionResult::BAD_TAG;
        return false;
      }
      _customTag = tag;
      return true;
    }
    
    bool IActionRunner::Interrupt()
    {
      if(InterruptInternal())
      {
        // Only need to unlock tracks if this is running because Update() locked tracks
        if(!_suppressTrackLocking && _state == ActionResult::RUNNING)
        {
          u8 tracks = GetTracksToLock();
          if(DEBUG_ANIM_TRACK_LOCKING)
          {
            PRINT_CH_INFO(kLogChannelName, "IActionRunner.Interrupt.UnlockTracks",
                          "unlocked: (0x%x) %s by %s [%d]",
                          tracks,
                          AnimTrackHelpers::AnimTrackFlagsToString(tracks).c_str(),
                          _name.c_str(),
                          _idTag);
          }

          _robot.GetMoveComponent().UnlockTracks(tracks, GetTag());
        }
        Reset(false);
        _state = ActionResult::INTERRUPTED;
        return true;
      }
      return false;
    }
    
    void IActionRunner::ForceComplete()
    {
      PRINT_CH_INFO("Actions", "IActionRunner.ForceComplete",
                    "Forcing %s[%d] in state %s to complete",
                    GetName().c_str(),
                    GetTag(),
                    EnumToString(_state));
      
      _state = ActionResult::SUCCESS;
    }
    
    ActionResult IActionRunner::Update()
    {
      _robot.GetActionList().GetActionWatcher().ActionStartUpdating(this);
      switch(_state)
      {
        case ActionResult::RETRY:
        case ActionResult::NOT_STARTED:
        case ActionResult::INTERRUPTED:
        {
          _state = ActionResult::RUNNING;
          if(!_suppressTrackLocking)
          {
            // When the ActionRunner first starts, lock any specified subsystems
            u8 tracksToLock = GetTracksToLock();
            
            if(_robot.GetMoveComponent().AreAnyTracksLocked(tracksToLock))
            {
              // Print special, more helpful message in SDK mode, if on charger
              if(_robot.GetContext()->IsInSdkMode() && _robot.IsOnCharger())
              {
                PRINT_CH_INFO(kLogChannelName, "IActionRunner.Update.TracksLockedOnChargerInSDK",
                              "Use of head/lift/body motors is limited while on charger in SDK mode");
              }
              
              PRINT_NAMED_WARNING("IActionRunner.Update.TracksLocked",
                                  "Action %s [%d] not running because required tracks (0x%x) %s are locked by: %s",
                                  GetName().c_str(),
                                  GetTag(),
                                  tracksToLock,
                                  AnimTrackHelpers::AnimTrackFlagsToString(tracksToLock).c_str(),
                                  _robot.GetMoveComponent().WhoIsLocking(tracksToLock).c_str());
              
              _state = ActionResult::TRACKS_LOCKED;
              _robot.GetActionList().GetActionWatcher().ActionEndUpdating(this);
              return ActionResult::TRACKS_LOCKED;
            }
            
            if(DEBUG_ANIM_TRACK_LOCKING)
            {
              PRINT_CH_INFO(kLogChannelName, "IActionRunner.Update.LockTracks",
                            "locked: (0x%x) %s by %s [%d]",
                            tracksToLock,
                            AnimTrackHelpers::AnimTrackFlagsToString(tracksToLock).c_str(),
                            GetName().c_str(),
                            GetTag());
            }
            
            _robot.GetMoveComponent().LockTracks(tracksToLock, GetTag(), GetName());
          }
          
          if( DEBUG_ACTION_RUNNING && _displayMessages )
          {
            PRINT_CH_DEBUG(kLogChannelName, "IActionRunner.Update.IsRunning",
                           "Action [%d] %s running",
                           GetTag(),
                           GetName().c_str());
          }
        }
        case ActionResult::RUNNING:
        {
          _state = UpdateInternal();
          
          if(_state == ActionResult::RUNNING)
          {
            // Still running dont fall through
            break;
          }
          // UpdateInternal() returned something other than running so fall through to handle action
          // completion
        }
        // Every other case is a completion case (ie the action is no longer running due to success, failure, or
        // cancel)
        default:
        {
          if(_displayMessages) {
            PRINT_CH_INFO(kLogChannelName, "IActionRunner.Update.ActionCompleted",
                          "%s [%d] %s with state %s.", GetName().c_str(),
                          GetTag(),
                          (_state==ActionResult::SUCCESS ? "succeeded" :
                           _state==ActionResult::CANCELLED ? "was cancelled" : "failed"),
                          EnumToString(_state));
          }
          
          PrepForCompletion();
          
          if( DEBUG_ACTION_RUNNING && _displayMessages ) {
            PRINT_CH_DEBUG(kLogChannelName, "IActionRunner.Update.IsRunning",
                           "Action [%d] %s NOT running",
                           GetTag(),
                           GetName().c_str());
          }
        }
      }
      _robot.GetActionList().GetActionWatcher().ActionEndUpdating(this);
      return _state;
    }

    void IActionRunner::SetEnableMoodEventOnCompletion(bool enable)
    {
      _robot.GetMoodManager().SetEnableMoodEventOnCompletion(GetTag(), enable);
    }
  
    void IActionRunner::PrepForCompletion()
    {
      if(!_preppedForCompletion)
      {
        GetCompletionUnion(_completionUnion);
        _preppedForCompletion = true;
      } else {
        PRINT_CH_DEBUG(kLogChannelName, "IActionRunner.PrepForCompletion.AlreadyPrepped",
                       "%s [%d]", _name.c_str(), GetTag());
      }
    }
    
    bool IActionRunner::RetriesRemain()
    {
      if(_numRetriesRemaining > 0) {
        --_numRetriesRemaining;
        return true;
      } else {
        return false;
      }
    }
    
#   if USE_ACTION_CALLBACKS
    void IActionRunner::AddCompletionCallback(ActionCompletionCallback callback)
    {
      _completionCallbacks.emplace_back(callback);
    }
    
    void IActionRunner::RunCallbacks(ActionResult result) const
    {
      for(const auto& callback : _completionCallbacks) {
        callback(result);
      }
    }
#   endif // USE_ACTION_CALLBACKS

    
    void IActionRunner::UnlockTracks()
    {
      // Tracks aren't locked until the action starts so don't unlock them until then
      if(!_suppressTrackLocking && _state != ActionResult::NOT_STARTED)
      {
        u8 tracks = GetTracksToLock();
        if(DEBUG_ANIM_TRACK_LOCKING)
        {
          PRINT_CH_INFO(kLogChannelName, "IActionRunner.UnlockTracks",
                        "unlocked: (0x%x) %s by %s [%d]",
                        tracks,
                        AnimTrackHelpers::AnimTrackFlagsToString(tracks).c_str(),
                        _name.c_str(),
                        _idTag);
        }
        _robot.GetMoveComponent().UnlockTracks(tracks, GetTag());
      }
    }
    
    void IActionRunner::SetTracksToLock(const u8 tracks)
    {
      if(_state == ActionResult::NOT_STARTED)
      {
        _tracks = tracks;
      }
      else
      {
        PRINT_NAMED_WARNING("IActionRunner.SetTracksToLock", "Trying to set tracks to lock while running");
      }
    }
    
    void IActionRunner::Cancel()
    {
      if(_state != ActionResult::NOT_STARTED)
      {
        PRINT_CH_INFO(kLogChannelName, "IActionRunner.Cancel",
                      "Cancelling action %s[%d]",
                      _name.c_str(), GetTag());
        _state = ActionResult::CANCELLED;
      }
    }
    
    void IActionRunner::GetRobotCompletedActionMessage(ExternalInterface::RobotCompletedAction& msg)
    {
      std::vector<ActionResult> subActionResults;
      _robot.GetActionList().GetActionWatcher().GetSubActionResults(GetTag(), subActionResults);
      
      ActionCompletedUnion acu;
      GetCompletionUnion(acu);
      
      msg = ExternalInterface::RobotCompletedAction(_robot.GetID(),
                                                    GetTag(),
                                                    GetType(),
                                                    GetState(),
                                                    subActionResults,
                                                    acu);
    }
    
#pragma mark ---- IAction ----
    
    IAction::IAction(Robot& robot,
                     const std::string name,
                     const RobotActionType type,
                     const u8 trackToLock)
    : IActionRunner(robot,
                    name,
                    type,
                    trackToLock)
    {
      Reset();
    }
    
    void IAction::Reset(bool shouldUnlockTracks)
    {
      _preconditionsMet = false;
      _startTime_sec = -1.f;
      if(shouldUnlockTracks)
      {
        UnlockTracks();
      }
      ResetState();
    }
    
    Util::RandomGenerator& IAction::GetRNG() const
    {
      return _robot.GetRNG();
    }
    
    ActionResult IAction::UpdateInternal()
    {
      ActionResult result = ActionResult::RUNNING;
      SetStatus(GetName());
      
      // On first call to Update(), figure out the waitUntilTime
      const f32 currentTimeInSeconds = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
      
      if(_startTime_sec < 0.f) {
        // Record first update time
        _startTime_sec = currentTimeInSeconds;
      }
      
      // Update timeout/wait times in case they have been adjusted since the action
      // started. Time to wait until is always relative to original start, however.
      // (Include CheckIfDoneDelay in wait time if we have already met pre-conditions
      const f32 waitUntilTime = (_startTime_sec + GetStartDelayInSeconds() +
                                 (_preconditionsMet ? GetCheckIfDoneDelayInSeconds() : 0.f));
      const f32 timeoutTime   = _startTime_sec + GetTimeoutInSeconds();

      // Fail if we have exceeded timeout time
      if(currentTimeInSeconds >= timeoutTime) {
        if(IsMessageDisplayEnabled()) {
          PRINT_NAMED_WARNING("IAction.Update.TimedOut",
                              "%s timed out after %.1f seconds.",
                              GetName().c_str(), GetTimeoutInSeconds());
        }
        result = ActionResult::TIMEOUT;
      }
      
      // Don't do anything until we have reached the waitUntilTime
      else if(currentTimeInSeconds >= waitUntilTime)
      {
        if(!_preconditionsMet) {
          //PRINT_CH_INFO(kLogChannelName, "IAction.Update", "Updating %s: checking preconditions.", GetName().c_str());
          SetStatus(GetName() + ": check preconditions");

          // Note that derived classes will define what to do when pre-conditions
          // are not met: if they return RUNNING, then the action will effectively
          // just wait for the preconditions to be met. Otherwise, a failure
          // will get propagated out as the return value of the Update method.
          result = Init();

          if(result == ActionResult::SUCCESS) {
            if(IsMessageDisplayEnabled()) {
              PRINT_CH_DEBUG(kLogChannelName, "IAction.Update.PreconditionsMet",
                             "Preconditions for %s [%d] successfully met.",
                             GetName().c_str(),
                             GetTag());
            }
            
            // If preconditions were successfully met, switch result to RUNNING
            // so that we don't think the entire action is completed. (We still
            // need to do CheckIfDone() calls!)
            // TODO: there's probably a tidier way to do this.
            _preconditionsMet = true;
            result = ActionResult::RUNNING;
          }
        }

        // Re-check if preconditions are met, since they could have _just_ been met
        if(_preconditionsMet && currentTimeInSeconds >= waitUntilTime) {
          //PRINT_CH_INFO(kLogChannelName, "IAction.Update", "Updating %s: checking if done.", GetName().c_str());
          SetStatus(GetName() + ": check if done");
          
          // Pre-conditions already met, just run until done
          result = CheckIfDone();
        }
      } // if(currentTimeInSeconds > _waitUntilTime)
      
      const bool shouldRetry = (IActionRunner::GetActionResultCategory(result) == ActionResultCategory::RETRY);
      if(shouldRetry && RetriesRemain()) {
        if(IsMessageDisplayEnabled()) {
          PRINT_CH_INFO(kLogChannelName, "IAction.Update.CurrentActionFailedRetrying",
                        "Robot %d failed running action %s. Retrying.",
                        _robot.GetID(), GetName().c_str());
        }
        
        // Don't unlock the tracks if retrying
        Reset(false);
        result = ActionResult::RUNNING;
      }

#     if USE_ACTION_CALLBACKS
      if(result != ActionResult::RUNNING) {
        RunCallbacks(result);
      }
#     endif
      return result;
    } // UpdateInternal()
    
  } // namespace Cozmo
} // namespace Anki

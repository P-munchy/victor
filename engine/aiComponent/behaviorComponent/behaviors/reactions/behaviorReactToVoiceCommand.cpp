/**
* File: behaviorReactToVoiceCommand.cpp
*
* Author: Jarrod Hatfield
* Created: 2/16/2018
*
* Description: Simple behavior to immediately respond to the voice command keyphrase, while waiting for further commands.
*
* Copyright: Anki, Inc. 2017
*
**/

#include "engine/aiComponent/behaviorComponent/behaviors/reactions/behaviorReactToVoiceCommand.h"

#include "clad/robotInterface/messageRobotToEngineTag.h"
#include "clad/types/animationTrigger.h"
#include "clad/types/behaviorComponent/behaviorTypes.h"
#include "coretech/common/engine/jsonTools.h"
#include "coretech/common/engine/math/pose.h"
#include "engine/actions/animActions.h"
#include "engine/actions/basicActions.h"
#include "engine/actions/compoundActions.h"
#include "engine/aiComponent/aiComponent.h"
#include "engine/aiComponent/behaviorComponent/attentionTransferComponent.h"
#include "engine/aiComponent/behaviorComponent/behaviorComponent.h"
#include "engine/aiComponent/behaviorComponent/behaviorContainer.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/behaviorExternalInterface.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/beiRobotInfo.h"
#include "engine/aiComponent/behaviorComponent/behaviors/reactions/behaviorReactToMicDirection.h"
#include "engine/aiComponent/behaviorComponent/userIntentComponent.h"
#include "engine/aiComponent/behaviorComponent/userIntentData.h"
#include "engine/audio/engineRobotAudioClient.h"
#include "engine/components/backpackLights/backpackLightComponent.h"
#include "engine/components/mics/micComponent.h"
#include "engine/components/mics/micDirectionHistory.h"
#include "engine/components/movementComponent.h"
#include "engine/cozmoContext.h"
#include "engine/externalInterface/externalMessageRouter.h"
#include "engine/externalInterface/gatewayInterface.h"
#include "engine/faceWorld.h"
#include "engine/moodSystem/moodManager.h"
#include "engine/robotInterface/messageHandler.h"
#include "micDataTypes.h"
#include "osState/osState.h"
#include "util/console/consoleInterface.h"

#include "coretech/common/engine/utils/timer.h"


#define DEBUG_TRIGGER_WORD_VERBOSE 0 // add some verbose debugging if trying to track down issues

#define CONSOLE_GROUP  "TriggerWord"

#if DEBUG_TRIGGER_WORD_VERBOSE
  // only print these debug if we're tracking down something so we don't spam the logs
  #define PRINT_TRIGGER_DEBUG( format, ... ) \
    PRINT_CH_DEBUG( "MicData", "BehaviorReactToVoiceCommand.TriggerWord", format, ##__VA_ARGS__ )
#else
  #define PRINT_TRIGGER_DEBUG( format, ... )
#endif

#define PRINT_TRIGGER_INFO( format, ... ) \
  PRINT_CH_INFO( "MicData", "BehaviorReactToVoiceCommand.TriggerWord", format, ##__VA_ARGS__ )


namespace Anki {
namespace Vector {

namespace {

  const char* kEarConBegin                         = "earConAudioEventBegin";
  const char* kEarConSuccess                       = "earConAudioEventSuccess";
  const char* kEarConFail                          = "earConAudioEventNeutral";
  const char* kIntentBehaviorKey                   = "behaviorOnIntent";
  const char* kProceduralBackpackLights            = "backpackLights";
  const char* kNotifyOnErrors                      = "notifyOnErrors";
  const char* kAnimListeningGetIn                  = "animListeningGetIn";
  const char* kExitAfterGetInKey                   = "exitAfterGetIn";
  const char* kExitAfterListeningIfNotStreamingKey = "exitAfterListeningIfNotStreaming";

  CONSOLE_VAR( bool, kRespondsToTriggerWord, CONSOLE_GROUP, true );

  // the behavior will always "listen" for at least this long once it hears the wakeword, even if we receive
  // an error sooner than this. Note that the behavior will also consider the intent to be an error if the
  // stream doesn't open within this amount of time, so don't lower this number too much
  CONSOLE_VAR_RANGED( float, kMinListeningTimeout_s, CONSOLE_GROUP, 5.0f, 0.0f, 30.0f );
  // this is the maximum duration we'll wait from streaming begin
  CONSOLE_VAR_RANGED( float, kMaxStreamingDuration_s, CONSOLE_GROUP, 10.0f, 0.0f, 20.0f );


  // when our streaming begins/ends there is a high chance that we will record some non-intent sound, these
  // values allow us to chop off the front and back of the streaming window when determining the intent direction
  CONSOLE_VAR_RANGED( double, kDirStreamingTimeToIgnoreBegin,  CONSOLE_GROUP, 0.5, 0.0, 2.0 );
  CONSOLE_VAR_RANGED( double, kDirStreamingTimeToIgnoreEnd,    CONSOLE_GROUP, 1.25, 0.0, 2.0 );
  // ignore mic direction with confidence below this when trying to determine streaming direction
  CONSOLE_VAR_RANGED( MicDirectionConfidence, kDirStreamingConfToIgnore, CONSOLE_GROUP, 500, 0, 10000 );
  // if we cannot determine the mic direction, we fall back to the most recent direction
  // this allows you to specify how far back we sample for the most recent direction
  CONSOLE_VAR_RANGED( double, kRecentDirFallbackTime,          CONSOLE_GROUP, 1.0, 0.0, 10.0 );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorReactToVoiceCommand::InstanceConfig::InstanceConfig() :
  earConBegin( AudioMetaData::GameEvent::GenericEvent::Invalid ),
  earConSuccess( AudioMetaData::GameEvent::GenericEvent::Invalid ),
  earConFail( AudioMetaData::GameEvent::GenericEvent::Invalid ),
  animListeningGetIn( AnimationTrigger::VC_ListeningGetIn ),
  backpackLights( true ),
  exitAfterGetIn( false ),
  exitAfterListeningIfNotStreaming( false ),
  cloudErrorTracker( "VoiceCommandErrorTracker" )
{

}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorReactToVoiceCommand::DynamicVariables::DynamicVariables() :
  state( EState::GetIn ),
  reactionDirection( kMicDirectionUnknown ),
  streamingBeginTime( 0.0 ),
  streamingEndTime( 0.0 ),
  intentStatus( EIntentStatus::NoIntentHeard ),
  timestampToDisableTurnFor(0)
{

}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorReactToVoiceCommand::BehaviorReactToVoiceCommand( const Json::Value& config ) :
  ICozmoBehavior( config ),
  _triggerDirection( kMicDirectionUnknown )
{
  // do we play ear-con sounds to notify the user when Victor is listening
  {
    std::string earConString;
    if ( JsonTools::GetValueOptional( config, kEarConBegin, earConString ) )
    {
      _iVars.earConBegin = AudioMetaData::GameEvent::GenericEventFromString( earConString );
    }

    if ( JsonTools::GetValueOptional( config, kEarConSuccess, earConString ) )
    {
      _iVars.earConSuccess = AudioMetaData::GameEvent::GenericEventFromString( earConString );
    }

    if ( JsonTools::GetValueOptional( config, kEarConFail, earConString ) )
    {
      _iVars.earConFail = AudioMetaData::GameEvent::GenericEventFromString( earConString );
    }
  }

  // do we play the backpack lights from the behavior, else assume anims will handle it
  JsonTools::GetValueOptional( config, kProceduralBackpackLights, _iVars.backpackLights );

  // get the behavior to play after an intent comes in
  JsonTools::GetValueOptional( config, kIntentBehaviorKey, _iVars.reactionBehaviorString );
  
  std::string animGetIn;
  if( JsonTools::GetValueOptional( config, kAnimListeningGetIn, animGetIn ) && !animGetIn.empty() )
  {
    ANKI_VERIFY( AnimationTriggerFromString(animGetIn, _iVars.animListeningGetIn),
                 "BehaviorReactToVoiceCommand.Ctor.InvalidGetIn",
                 "Get-in %s is not a valid animation trigger",
                 animGetIn.c_str() );
  }
  
  JsonTools::GetValueOptional( config, kExitAfterGetInKey, _iVars.exitAfterGetIn );

  JsonTools::GetValueOptional( config, kExitAfterListeningIfNotStreamingKey, _iVars.exitAfterListeningIfNotStreaming );
  
  if( !config[kNotifyOnErrors].isNull() )
  {
    int numErrorsToTriggerAnim;
    float errorTrackingWindow_s;
    
    ANKI_VERIFY( RecentOccurrenceTracker::ParseConfig(config[kNotifyOnErrors],
                                                      numErrorsToTriggerAnim,
                                                      errorTrackingWindow_s),
                 "BehaviorReactToVoiceCommand.Constructor.InvalidConfig",
                 "Behavior '%s' specified invalid recent occurrence config",
                 GetDebugLabel().c_str() );
    _iVars.cloudErrorHandle = _iVars.cloudErrorTracker.GetHandle(numErrorsToTriggerAnim, errorTrackingWindow_s);
  }

  SetRespondToTriggerWord( true );
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToVoiceCommand::GetBehaviorJsonKeys(std::set<const char*>& expectedKeys) const
{
  const char* list[] = {
    kEarConBegin,
    kEarConSuccess,
    kEarConFail,
    kProceduralBackpackLights,
    kIntentBehaviorKey,
    kNotifyOnErrors,
    kAnimListeningGetIn,
    kExitAfterGetInKey,
    kExitAfterListeningIfNotStreamingKey,
  };
  expectedKeys.insert( std::begin(list), std::end(list) );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToVoiceCommand::InitBehavior()
{
  // grab our reaction behavior ...
  if ( !_iVars.reactionBehaviorString.empty() )
  {
    ICozmoBehaviorPtr reactionBehavior = FindBehavior( _iVars.reactionBehaviorString );
    // downcast to a BehaviorReactToMicDirection since we're forcing all reactions to be of this behavior
    DEV_ASSERT_MSG( reactionBehavior->GetClass() == BehaviorClass::ReactToMicDirection,
                    "BehaviorReactToVoiceCommand.Init.IncorrectMicDirectionBehavior",
                    "Reaction behavior specified is not of valid class BehaviorClass::ReactToMicDirection");
    _iVars.reactionBehavior = std::static_pointer_cast<BehaviorReactToMicDirection>(reactionBehavior);
  }

  _iVars.unmatchedIntentBehavior = GetBEI().GetBehaviorContainer().FindBehaviorByID( BEHAVIOR_ID(IntentUnmatched) );
  DEV_ASSERT( _iVars.unmatchedIntentBehavior != nullptr,
              "BehaviorReactToVoiceCommand.Init.UnmatchedIntentBehaviorMissing");

  _iVars.silenceIntentBehavior = GetBEI().GetBehaviorContainer().FindBehaviorByID(BEHAVIOR_ID(TriggerWordWithoutIntent));
  DEV_ASSERT( _iVars.silenceIntentBehavior != nullptr,
              "BehaviorReactToVoiceCommand.Init.silenceIntentBehavior");

  _iVars.noCloudBehavior = GetBEI().GetBehaviorContainer().FindBehaviorByID( BEHAVIOR_ID(NoCloud) );
  DEV_ASSERT( _iVars.noCloudBehavior != nullptr,
              "BehaviorReactToVoiceCommand.Init.NoCloudBehaviorMissing");

  _iVars.noWifiBehavior = GetBEI().GetBehaviorContainer().FindBehaviorByID( BEHAVIOR_ID(NoWifi) );
  DEV_ASSERT( _iVars.noWifiBehavior != nullptr,
              "BehaviorReactToVoiceCommand.Init.NoWifiBehaviorMissing");

  SubscribeToTags(
  {
    RobotInterface::RobotToEngineTag::triggerWordDetected
  });
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToVoiceCommand::GetAllDelegates( std::set<IBehavior*>& delegates ) const
{
  if ( _iVars.reactionBehavior )
  {
    delegates.insert( _iVars.reactionBehavior.get() );
  }

  delegates.insert( _iVars.unmatchedIntentBehavior.get() );
  delegates.insert( _iVars.silenceIntentBehavior.get() );
  delegates.insert( _iVars.noCloudBehavior.get() );
  delegates.insert( _iVars.noWifiBehavior.get() );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToVoiceCommand::GetBehaviorOperationModifiers( BehaviorOperationModifiers& modifiers ) const
{
  modifiers.wantsToBeActivatedWhenCarryingObject  = true;
  modifiers.wantsToBeActivatedWhenOnCharger       = true;
  modifiers.wantsToBeActivatedWhenOffTreads       = true;
  modifiers.behaviorAlwaysDelegates               = true;

  // Since so many voice commands need faces, this helps improve the changes that a behavior following
  // this one will know about faces when the behavior starts
  modifiers.visionModesForActiveScope->insert({ VisionMode::DetectingFaces, EVisionUpdateFrequency::High });
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorReactToVoiceCommand::WantsToBeActivatedBehavior() const
{
  return kRespondsToTriggerWord;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToVoiceCommand::AlwaysHandleInScope( const RobotToEngineEvent& event )
{
  if ( event.GetData().GetTag() == RobotInterface::RobotToEngineTag::triggerWordDetected )
  {
    _triggerDirection = event.GetData().Get_triggerWordDetected().direction;

    #if DEBUG_TRIGGER_WORD_VERBOSE
    {
      PRINT_CH_DEBUG( "MicData", "BehaviorReactToVoiceCommand.Debug",
                     "Received TriggerWordDetected event with diretion [%d]", (int)_triggerDirection );
    }
    #endif
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToVoiceCommand::OnBehaviorActivated()
{
  _dVars = DynamicVariables();

  auto* gi = GetBEI().GetRobotInfo().GetGatewayInterface();
  if( gi != nullptr ) {
    auto* wakeWordBegin = new external_interface::WakeWordBegin;
    gi->Broadcast( ExternalMessageRouter::Wrap(wakeWordBegin) );
  }

  if ( GetBEI().HasMoodManager() )
  {
    auto& moodManager = GetBEI().GetMoodManager();
    moodManager.TriggerEmotionEvent( "ReactToTriggerWord", MoodManager::GetCurrentTimeInSeconds() );
  }

  // Trigger word is heard (since we've been activated) ...
  PRINT_CH_DEBUG( "MicData", "BehaviorReactToVoiceCommand.Activated",
                 "Reacting to trigger word from direction [%d] ...",
                 (int)GetReactionDirection() );

  StartListening();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToVoiceCommand::OnBehaviorDeactivated()
{
  // in case we were interrupted before we had a chance to turn off backpack ligths, do so now ...
  if ( _iVars.backpackLights && _dVars.lightsHandle.IsValid() )
  {
    BackpackLightComponent& blc = GetBEI().GetBackpackLightComponent();
    blc.StopLoopingBackpackAnimation( _dVars.lightsHandle );
  }

  auto* gi = GetBEI().GetRobotInfo().GetGatewayInterface();
  if( gi != nullptr ) {
    auto* wakeWordEnd = new external_interface::WakeWordEnd;
    const bool intentHeard = ( (_dVars.intentStatus != EIntentStatus::NoIntentHeard) &&
                               (_dVars.intentStatus != EIntentStatus::SilenceTimeout) &&
                               (_dVars.intentStatus != EIntentStatus::Error) );
    wakeWordEnd->set_intent_heard( intentHeard );
    if( intentHeard ) {
      UserIntentComponent& uic = GetBehaviorComp<UserIntentComponent>();
      // we use this dirty dirty method here instead of sending this message directly from the uic
      // since we know whether the intent was heard here, and it's nice that the same behavior
      // on activation/deactivation sends the two messages. if the uic sent the end message, it
      // might be sent without an initial message.
      const UserIntentData* intentData = uic.GetPendingUserIntent();
      if( intentData != nullptr ) {
        // ideally we'd send a proto message structured the same as the intent, but this would mean
        // duplicating the entire userIntent.clad file for proto, or converting the engine handling
        // of intents to clad, neither of which I've got time for.
        std::stringstream ss;
        ss << intentData->intent.GetJSON();
        wakeWordEnd->set_intent_json( ss.str() );
      }
    }
    gi->Broadcast( ExternalMessageRouter::Wrap(wakeWordEnd) );
  }

  // we've done all we can, now it's up to the next behavior to consume the user intent
  GetBehaviorComp<UserIntentComponent>().SetUserIntentTimeoutEnabled( true );

  // reset this bad boy
  _triggerDirection = kMicDirectionUnknown;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToVoiceCommand::BehaviorUpdate()
{
  DEV_ASSERT( ( GetStreamingDuration() >= ( MicData::kStreamingTimeout_ms / 1000.0 ) ),
              "BehaviorReactToVoiceCommand: Behavior streaming timeout is less than mic streaming timeout" );

  const bool wasStreaming = ( _dVars.streamingBeginTime > 0.0 );
  const bool isStreaming = GetBehaviorComp<UserIntentComponent>().IsCloudStreamOpen();

  // track when our stream opens and closes; technically this is not synced with our states which is why we track
  // it independently.
  if ( !wasStreaming )
  {
    if ( isStreaming )
    {
      OnStreamingBegin();
    }
  }
  else
  {
    const bool notAlreadyRecordedEnd = ( _dVars.streamingEndTime <= 0.0 );
    if ( !isStreaming && notAlreadyRecordedEnd )
    {
      OnStreamingEnd();
    }
  }

  if ( _dVars.state == EState::Listening )
  {
    const bool isIntentPending = GetBehaviorComp<UserIntentComponent>().IsAnyUserIntentPending();
    if ( isIntentPending )
    {
      // kill delegates, we'll handle next steps with callbacks
      // note: passing true to CancelDelegatees doesn't call the callback if we also delegate
      PRINT_CH_INFO("MicData", "BehaviorReactToVoiceCommand.StopListening.IntentPending",
                    "Stopping listening because an intent is pending");
      CancelDelegates( false );
      StopListening();
    }
    else
    {
      // there are a few ways we can timeout from the Listening state;
      // + error received
      // + streaming never started
      // + streaming started but no intent came back
      const double currTime_s = BaseStationTimer::getInstance()->GetCurrentTimeInSecondsDouble();
      const double listeningTimeout = GetListeningTimeout();
      if ( currTime_s >= listeningTimeout )
      {
        PRINT_CH_INFO( "MicData", "BehaviorReactToVoiceCommand.StopListening.Error",
                       "Stopping listening because of a(n) %s",
                       GetBehaviorComp<UserIntentComponent>().WasUserIntentError() ? "error" : "timeout" );
        CancelDelegates( false );
        StopListening();
      }
    }
  }
  else if ( _dVars.state == EState::Thinking )
  {
    // we may receive an intent AFTER we're done listening for various reasons,
    // so poll for it while we're in the thinking state
    // note: does nothing if intent is already set
    UpdateUserIntentStatus();
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToVoiceCommand::ComputeReactionDirectionFromStream()
{
  // what we are trying to do is figure out the direction that the user is located with respect to the robot.
  // we know the robot will not be moving while streaming, so we can assume all directions recorded during streaming
  // are in the same "coordinate space".
  // -> We will take the most common direction recorded during streaming as the most probable direction the user is located
  // we can expect a few spikes in noise causing the speaking direction to be false, but hopefully the user's actual
  // speaking direction will be the most consistent
  const MicDirectionHistory& micHistory = GetBEI().GetMicComponent().GetMicDirectionHistory();
  if ( _dVars.streamingBeginTime > 0.0f )
  {
    // take into account the "uknown" direction
    // we are going to ignore the unknown direction in our calculations since it is not helpful to us and very misleading
    // as any time the robot is moving or making noise will record the direction as unknown, skewing us to that direction
    constexpr uint16_t kNumHistoryIndices = ( kNumMicDirections + 1 );
    using DirectionHistoryCount = std::array<uint32_t, kNumHistoryIndices>;
    DirectionHistoryCount micDirectionCounts{};

    const TimeStamp_t streamBeginTime = ( _dVars.streamingBeginTime + kDirStreamingTimeToIgnoreBegin ) * 1000.0;
    const TimeStamp_t streamEndTime = ( _dVars.streamingEndTime - kDirStreamingTimeToIgnoreEnd ) * 1000.0;

    if ( streamEndTime >= streamBeginTime )
    {
      const TimeStamp_t elapsed = ( streamEndTime - streamBeginTime );
      MicDirectionNodeList list = micHistory.GetHistoryAtTime( streamEndTime, elapsed );

      // in the case where we have more than 1 sample, we want ignore partial samples outside of our time range, so
      // we'll just lop off the front and back portions of the list nodes
      if ( list.size() > 1 )
      {
        // case where the front extends beyond our streaming begin time ...
        MicDirectionNode& front = list.front();
        if ( front.timestampBegin < streamBeginTime )
        {
          DEV_ASSERT( ( front.timestampEnd >= streamBeginTime ), "Including node that is outside of streaming window" );
          const double nodeDuration = TimeStamp_t( front.timestampEnd - front.timestampBegin );
          const double timeInNode = TimeStamp_t( front.timestampEnd - streamBeginTime );
          front.count *= ( timeInNode / nodeDuration );
        }

        // case where the back extends beyond our streamin end time ...
        MicDirectionNode& back = list.back();
        if ( back.timestampEnd > streamEndTime )
        {
          DEV_ASSERT( ( back.timestampBegin <= streamEndTime ), "Including node that is outside of streaming window" );
          const double nodeDuration = TimeStamp_t( back.timestampEnd - back.timestampBegin );
          const double timeInNode = TimeStamp_t( streamEndTime - back.timestampBegin );
          back.count *= ( timeInNode / nodeDuration );
        }
      }

      // walk our list of heard directions and add up all of their counts
      // we're assuming a constant sample rate; we could always add up times as well
      for ( const auto& node : list )
      {
        // ignore directions that have too low of a confidence
        if ( node.confidenceAvg > kDirStreamingConfToIgnore )
        {
          PRINT_TRIGGER_DEBUG( "Heard valid direction [%d], with confidence [%d]", node.directionIndex, node.confidenceAvg );
          micDirectionCounts[node.directionIndex] += node.count;
        }
      }

      // now go through all of our valid directions and find the one with the highset count
      uint32_t highestCount = 0;
      for ( MicDirectionIndex i = 0; i < kNumHistoryIndices; ++i )
      {
        PRINT_TRIGGER_DEBUG( "Direction [%d], Count [%d]", i, micDirectionCounts[i] );
        if ( ( i != kMicDirectionUnknown ) && ( micDirectionCounts[i] > highestCount ) )
        {
          _dVars.reactionDirection = i;
          highestCount = micDirectionCounts[i];
        }
      }

      PRINT_TRIGGER_INFO( "Computed trigger reaction direction of %d", (int)_dVars.reactionDirection );
    }
    else
    {
      PRINT_TRIGGER_INFO( "Streaming duration was too short, falling back to most recent direction" );
      _dVars.reactionDirection = GetDirectionFromMicHistory();
    }
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
MicDirectionIndex BehaviorReactToVoiceCommand::GetReactionDirection() const
{
  MicDirectionIndex direction = _dVars.reactionDirection;
  if ( kMicDirectionUnknown == direction )
  {
    PRINT_TRIGGER_INFO( "Didn't have a reaction or trigger direction, so falling back to trigger direction" );

    // fallback to our trigger direction
    // accuracy is generally off by the amount the robot has turned
    // there's been some observed inaccuracy with this direction reported from trigger word event
    direction = _triggerDirection;
  }

  if ( kMicDirectionUnknown == direction )
  {
    PRINT_TRIGGER_INFO( "Didn't have a reaction or trigger direction, so falling back to latest selected direction" );

    // this is the least accurate if called post-intent
    // no difference if called pre-intent / post-trigger word
    direction = GetDirectionFromMicHistory();
  }

  return direction;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
MicDirectionIndex BehaviorReactToVoiceCommand::GetDirectionFromMicHistory() const
{
  const TimeStamp_t duration = ( kRecentDirFallbackTime * 1000.0 );

  const MicDirectionHistory& micHistory = GetBEI().GetMicComponent().GetMicDirectionHistory();
  MicDirectionIndex direction = micHistory.GetRecentDirection( duration );

  return direction;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToVoiceCommand::OnStreamingBegin()
{
  PRINT_CH_INFO("MicData", "BehaviorReactToVoiceCommand.OnStreamingBegin",
                "Got notice that cloud stream is open");
  _dVars.streamingBeginTime = BaseStationTimer::getInstance()->GetCurrentTimeInSecondsDouble();
  _dVars.streamingEndTime = 0.0; // reset this to we can match our start/ends
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToVoiceCommand::OnStreamingEnd()
{
  // only record end time if we've begun streaming
  const bool hasBegunStreaming = ( _dVars.streamingBeginTime > 0.0 );
  const bool notAlreadyRecordedEnd = ( _dVars.streamingEndTime <= 0.0 );
  if ( hasBegunStreaming && notAlreadyRecordedEnd )
  {
    PRINT_CH_INFO( "MicData", "BehaviorReactToVoiceCommand.OnStreamingEnd",
                   "Got notice that cloud stream is closed" );

    _dVars.streamingEndTime = BaseStationTimer::getInstance()->GetCurrentTimeInSecondsDouble();

    // let's attempt to compute the reaction direction as soon as we know the stream is closed
    // note: this can be called outside of IsActivated(), but it doesn't matter to us
    ComputeReactionDirectionFromStream();
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToVoiceCommand::StartListening()
{
  _dVars.state = EState::Listening;

  // to get into our listening state, we need to play our get-in anim followed by our
  // looping animation.
  OnVictorListeningBegin();

  // we don't want to enter EState::Listening until we're in our loop or else
  // we could end up exiting too soon and looking like garbage
  auto callback = [this]()
  {
    if( _iVars.exitAfterGetIn )
    {
      OnVictorListeningEnd();
      return; // and the behavior ends
    }

    // we now loop indefinitely and wait for the timeout in the update function
    // this is because we don't know when the streaming will begin (if it hasn't already) so we can't time it accurately
    DelegateIfInControl( new TriggerLiftSafeAnimationAction( AnimationTrigger::VC_ListeningLoop, 0 ) );
  };

  DelegateIfInControl( new TriggerLiftSafeAnimationAction( _iVars.animListeningGetIn ), callback );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToVoiceCommand::StopListening()
{
  ASSERT_NAMED_EVENT( _dVars.state == EState::Listening,
                      "BehaviorReactToVoiceCommand.State",
                      "Transitioning to EState::IntentReceived from invalid state [%hhu]", _dVars.state );

  // force our model of the streaming to close, in the case that we timed out (etc) before the actual stream closed
  OnStreamingEnd();

  UpdateUserIntentStatus();
  TransitionToThinking();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToVoiceCommand::OnVictorListeningBegin()
{
  using namespace AudioMetaData::GameEvent;

  static const BackpackLightAnimation::BackpackAnimation kStreamingLights =
  {
    .onColors               = {{NamedColors::CYAN,NamedColors::CYAN,NamedColors::CYAN}},
    .offColors              = {{NamedColors::CYAN,NamedColors::CYAN,NamedColors::CYAN}},
    .onPeriod_ms            = {{0,0,0}},
    .offPeriod_ms           = {{0,0,0}},
    .transitionOnPeriod_ms  = {{0,0,0}},
    .transitionOffPeriod_ms = {{0,0,0}},
    .offset                 = {{0,0,0}}
  };

  if ( _iVars.backpackLights )
  {
    BackpackLightComponent& blc = GetBEI().GetBackpackLightComponent();
    blc.StartLoopingBackpackAnimation( kStreamingLights, BackpackLightSource::Behavior, _dVars.lightsHandle );
  }

  if ( GenericEvent::Invalid != _iVars.earConBegin )
  {
    // Play earcon begin audio
    GetBEI().GetRobotAudioClient().PostEvent( _iVars.earConBegin, AudioMetaData::GameObjectType::Behavior );
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToVoiceCommand::OnVictorListeningEnd()
{
  using namespace AudioMetaData::GameEvent;

  if ( _iVars.backpackLights && _dVars.lightsHandle.IsValid() )
  {
    BackpackLightComponent& blc = GetBEI().GetBackpackLightComponent();
    blc.StopLoopingBackpackAnimation( _dVars.lightsHandle );
  }

  // play our "earcon end" audio, which depends on if our intent was successfully heard or not
  BehaviorExternalInterface& bei = GetBEI();
  if ( EIntentStatus::IntentHeard == _dVars.intentStatus )
  {
    if ( GenericEvent::Invalid != _iVars.earConSuccess )
    {
      // Play earcon end audio
      bei.GetRobotAudioClient().PostEvent( _iVars.earConSuccess, AudioMetaData::GameObjectType::Behavior );
    }
  }
  else
  {
    if ( GenericEvent::Invalid != _iVars.earConFail )
    {
      // Play earcon end audio
      bei.GetRobotAudioClient().PostEvent( _iVars.earConFail, AudioMetaData::GameObjectType::Behavior );
    }
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToVoiceCommand::UpdateUserIntentStatus()
{
  UserIntentComponent& uic = GetBehaviorComp<UserIntentComponent>();
  if ( ( _dVars.intentStatus == EIntentStatus::NoIntentHeard ) && uic.IsAnyUserIntentPending() )
  {
    // next behavior is going to deal with the intent, but we still have more shit to do
    uic.SetUserIntentTimeoutEnabled( false );

    _dVars.intentStatus = EIntentStatus::IntentHeard;

    PRINT_CH_DEBUG("MicData", "BehaviorReactToVoiceCommand.UpdateUserIntentStatus.Heard",
                   "Heard an intent");

    static const UserIntentTag unmatched = USER_INTENT(unmatched_intent);
    if ( uic.IsUserIntentPending( unmatched ) )
    {
      SmartActivateUserIntent( unmatched );
      _dVars.intentStatus = EIntentStatus::IntentUnknown;
      PRINT_CH_DEBUG("MicData", "BehaviorReactToVoiceCommand.UpdateUserIntentStatus.Unknown",
                     "Heard an intent, but it was unknown");
    }

    static const UserIntentTag silence = USER_INTENT(silence);
    if ( uic.IsUserIntentPending( silence ) )
    {
      SmartActivateUserIntent( silence );
      _dVars.intentStatus = EIntentStatus::SilenceTimeout;
      PRINT_CH_DEBUG("MicData", "BehaviorReactToVoiceCommand.UpdateUserIntentStatus.Silence",
                     "Got response declaring silence timeout occurred");
    }

  }
  else if( uic.WasUserIntentError() ) {
    uic.ResetUserIntentError();
    _dVars.intentStatus = EIntentStatus::Error;
    PRINT_CH_DEBUG("MicData", "BehaviorReactToVoiceCommand.UpdateUserIntentStatus.Error",
                   "latest user intent was an error");
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToVoiceCommand::TransitionToThinking()
{
  _dVars.state = EState::Thinking;

  PRINT_CH_DEBUG("MicData", "BehaviorReactToVoiceCommand.TransitionToThinking", "Thinking state starting");

  auto callback = [this]()
  {
    // we're keeping our "listening feedback" open until the last possible moment, since the intent can come
    // in after we've closed our recording stream.
    OnVictorListeningEnd();

    const bool streamingToCloud = GetBEI().GetMicComponent().GetShouldStreamAfterWakeWord();
    if (!streamingToCloud && _iVars.exitAfterListeningIfNotStreaming) {
      PRINT_CH_INFO("Behaviors", "BehaviorReactToVoiceCommand.TransitionToThinkingCallback.NotStreaming",
                    "We are not streaming to the cloud currently, so no point in continuing with the behavior (since "
                    "we do not want to increment the error count, etc.). Playing the \"unheard\" anim then exiting");
      DelegateIfInControl( new TriggerLiftSafeAnimationAction( AnimationTrigger::VC_IntentNeutral ) );
      return;
    }
    
    // Play a reaction behavior if we were told to ...
    // ** only in the case that we've heard a valid intent **
    UpdateUserIntentStatus();
    const bool heardValidIntent = ( _dVars.intentStatus == EIntentStatus::IntentHeard );
    if ( heardValidIntent && _iVars.reactionBehavior )
    {
      if( IsTurnEnabled() ) {
        const MicDirectionIndex triggerDirection = GetReactionDirection();
        PRINT_NAMED_INFO("BehaviorReactToVoiceCommand.TransitionToThinking.ReactionDirection","%d",triggerDirection);
        _iVars.reactionBehavior->SetReactDirection( triggerDirection );

        PRINT_CH_DEBUG("MicData", "BehaviorReactToVoiceCommand.Thinking.SetReactionDirection",
                       "Setting reaction behavior direction to [%d]",
                       (int)triggerDirection);

        // allow the reaction to not want to run in certain directions/states
        if ( _iVars.reactionBehavior->WantsToBeActivated() )
        {
          DelegateIfInControl( _iVars.reactionBehavior.get(), &BehaviorReactToVoiceCommand::TransitionToIntentReceived );
        }
        else
        {
          PRINT_CH_DEBUG("MicData", "BehaviorReactToVoiceCommand.Thinking.ReactionDoesntWantToActivate",
                         "%s: intent reaction behavior '%s' doesn't want to activate",
                         GetDebugLabel().c_str(),
                         _iVars.reactionBehavior->GetDebugLabel().c_str());
        }
      }
      else
      {
        PRINT_CH_DEBUG("MicData", "BehaviorReactToVoiceCommand.Thinking.TurnDisabled",
                       "Turn after intent is received has been disabled this tick, skipping");
      }
    }

    if ( !IsControlDelegated() )
    {
      // handle intent now
      TransitionToIntentReceived();
    }
  };

  // we need to get out of our listening loop anim before we react
  DelegateIfInControl( new TriggerLiftSafeAnimationAction( AnimationTrigger::VC_ListeningGetOut ), callback );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorReactToVoiceCommand::TransitionToIntentReceived()
{
  _dVars.state = EState::IntentReceived;

  switch ( _dVars.intentStatus )
  {
    case EIntentStatus::IntentHeard: {
      // no animation for valid intent, go straight into the intent behavior
      PRINT_CH_DEBUG( "MicData", "BehaviorReactToVoiceCommand.Intent.Heard", "Heard valid user intent, woot!" );

      // also reset the attention transfer counters since we got a valid intent
      GetBehaviorComp<AttentionTransferComponent>().ResetAttentionTransfer(AttentionTransferReason::UnmatchedIntent);
      GetBehaviorComp<AttentionTransferComponent>().ResetAttentionTransfer(AttentionTransferReason::NoWifi);
      GetBehaviorComp<AttentionTransferComponent>().ResetAttentionTransfer(AttentionTransferReason::NoCloudConnection);
      _iVars.cloudErrorTracker.Reset();
      break;
    }

    case EIntentStatus::IntentUnknown: {
      PRINT_CH_DEBUG( "MicData", "BehaviorReactToVoiceCommand.Intent.Unknown",
                      "Heard user intent but could not understand it" );
      GetBEI().GetMoodManager().TriggerEmotionEvent( "NoValidVoiceIntent" );
      if( _iVars.unmatchedIntentBehavior->WantsToBeActivated() ) {
        // this behavior will (should) interact directly with the attention transfer component
        DelegateIfInControl(_iVars.unmatchedIntentBehavior.get());
      }

      // even an unknown intent means we got something back from the cloud, so reset those transfer counters
      GetBehaviorComp<AttentionTransferComponent>().ResetAttentionTransfer(AttentionTransferReason::NoWifi);
      GetBehaviorComp<AttentionTransferComponent>().ResetAttentionTransfer(AttentionTransferReason::NoCloudConnection);
      break;
    }

    case EIntentStatus::SilenceTimeout: {
      PRINT_CH_DEBUG( "MicData", "BehaviorReactToVoiceCommand.Intent.Silence",
                      "Heard silence from the user");
      GetBEI().GetMoodManager().TriggerEmotionEvent( "NoValidVoiceIntent" );
      if( _iVars.silenceIntentBehavior->WantsToBeActivated() ) {
        DelegateIfInControl(_iVars.silenceIntentBehavior.get());
      }

      // even a silent intent means we got something back from the cloud, so reset those transfer
      // counters. This does not reset unmatched intent because the user may have difficulty speaking
      GetBehaviorComp<AttentionTransferComponent>().ResetAttentionTransfer(AttentionTransferReason::NoWifi);
      GetBehaviorComp<AttentionTransferComponent>().ResetAttentionTransfer(AttentionTransferReason::NoCloudConnection);
      break;
    }

    case EIntentStatus::NoIntentHeard:
    case EIntentStatus::Error: {

      PRINT_CH_DEBUG( "MicData", "BehaviorReactToVoiceCommand.Intent.Error",
                      "Intent processing returned an error (or timeout)" );
      GetBEI().GetMoodManager().TriggerEmotionEvent( "NoValidVoiceIntent" );

      // track that an error occurred
      _iVars.cloudErrorTracker.AddOccurrence();

      if( _iVars.cloudErrorHandle && _iVars.cloudErrorHandle->AreConditionsMet() )
      {
        // time to let the user know

        auto errorBehaviorCallback = [this]() {
          const bool updateNow = true;
          const bool hasSSID = !OSState::getInstance()->GetSSID(updateNow).empty();

          if( hasSSID ) {
            PRINT_CH_DEBUG( "MicData", "BehaviorReactToVoiceCommand.Intent.Error.NoCloud",
                            "has wifi, so error must be on the internet or cloud side" );

            // has wifi, but no cloud
            if( ANKI_VERIFY( _iVars.noCloudBehavior && _iVars.noCloudBehavior->WantsToBeActivated(),
                             "BehaviorReactToVoiceCommand.Intent.Error.NoCloud.BehaviorWontActivate",
                             "No cloud behavior '%s' doesn't want to be activated",
                             _iVars.noCloudBehavior ? _iVars.noCloudBehavior->GetDebugLabel().c_str() : "<NULL>" ) ) {
              // this behavior will (should) interact directly with the attention transfer component
              DelegateIfInControl(_iVars.noCloudBehavior.get());
            }
          }
          else {
            PRINT_CH_DEBUG( "MicData", "BehaviorReactToVoiceCommand.Intent.Error.NoWifi",
                            "no wifi SSID, error is local" );

            // no wifi
            if( ANKI_VERIFY( _iVars.noWifiBehavior && _iVars.noWifiBehavior->WantsToBeActivated(),
                             "BehaviorReactToVoiceCommand.Intent.Error.NoWifi.BehaviorWontActivate",
                             "No wifi behavior '%s' doesn't want to be activated",
                             _iVars.noWifiBehavior ? _iVars.noWifiBehavior->GetDebugLabel().c_str() : "<NULL>" ) ) {
              // this behavior will (should) interact directly with the attention transfer component
              DelegateIfInControl(_iVars.noWifiBehavior.get());
            }
          }
        };

        const MicDirectionIndex triggerDirection = GetReactionDirection();
        _iVars.reactionBehavior->SetReactDirection( triggerDirection );

        PRINT_CH_DEBUG("MicData", "BehaviorReactToVoiceCommand.Intent.Error.SetReactionDirection",
                       "Setting reaction behavior direction to [%d]",
                       (int)triggerDirection);

        // allow the reaction to not want to run in certain directions/states
        if ( _iVars.reactionBehavior->WantsToBeActivated() )
        {
          DelegateIfInControl( _iVars.reactionBehavior.get(), errorBehaviorCallback );
        }
        else
        {
          PRINT_CH_DEBUG("MicData", "BehaviorReactToVoiceCommand.Intent.Error",
                         "%s: intent reaction behavior '%s' doesn't want to activate (in case of intent error)",
                         GetDebugLabel().c_str(),
                         _iVars.reactionBehavior->GetDebugLabel().c_str());
          errorBehaviorCallback();
        }
      }
      else {
        // not time to tell the user, just play the normal unheard animation
        DelegateIfInControl( new TriggerLiftSafeAnimationAction( AnimationTrigger::VC_IntentNeutral ) );
      }
      break;
    }
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorReactToVoiceCommand::IsTurnEnabled() const
{
  const EngineTimeStamp_t ts = BaseStationTimer::getInstance()->GetCurrentTimeStamp();
  return ts != _dVars.timestampToDisableTurnFor;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
double BehaviorReactToVoiceCommand::GetStreamingDuration() const
{
  // our streaming duration is how long after streaming begins do we wait for an intent
  return kMaxStreamingDuration_s;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
double BehaviorReactToVoiceCommand::GetListeningTimeout() const
{
  double timeout = 0.0;

  const bool errorPending = GetBehaviorComp<UserIntentComponent>().WasUserIntentError();
  const bool streamingHasBegun = ( _dVars.streamingBeginTime > 0.0 );

  if ( errorPending || !streamingHasBegun )
  {
    // we haven't start streaming, so timeout this much after we've been active
    timeout = ( GetTimeActivated_s() + kMinListeningTimeout_s );
  }
  else
  {
    // we're currently streaming, so timeout when we hit our streaming duration
    timeout = ( _dVars.streamingBeginTime + GetStreamingDuration() );
  }

  return timeout;
}

} // namespace Vector
} // namespace Anki

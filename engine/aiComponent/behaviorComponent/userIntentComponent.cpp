/**
 * File: userIntentComponent.cpp
 *
 * Author: Brad Neuman
 * Created: 2018-02-01
 *
 * Description: Component to hold and query user intents (e.g. voice or app commands)
 *
 * Copyright: Anki, Inc. 2018
 *
 **/

#include "engine/aiComponent/behaviorComponent/userIntentComponent.h"

#include "engine/aiComponent/behaviorComponent/behaviorComponentCloudServer.h"
#include "engine/aiComponent/behaviorComponent/userIntentData.h"
#include "engine/aiComponent/behaviorComponent/userIntentMap.h"
#include "engine/aiComponent/behaviorComponent/userIntents.h"
#include "engine/components/animationComponent.h"
#include "engine/cozmoContext.h"
#include "engine/externalInterface/externalInterface.h"
#include "engine/robot.h"
#include "engine/robotDataLoader.h"
#include "engine/robotInterface/messageHandler.h"

#include "clad/externalInterface/messageGameToEngine.h"
#include "clad/types/behaviorComponent/userIntent.h"

#include "coretech/common/engine/jsonTools.h"
#include "coretech/common/engine/utils/timer.h"

#include "webServerProcess/src/webVizSender.h"


#include "json/json.h"

namespace Anki {
namespace Vector {

namespace {

static const size_t kMaxTicksToWarn = 2;
static const size_t kMaxTicksToClear = 3;
static const char* kCloudIntentJsonKey = "intent";
static const char* kParamsKey = "params";
static const char* kAltParamsKey = "parameters"; // "params" is reserved in CLAD

}

size_t UserIntentComponent::sActivatedIntentID = 0;

const UserIntentSource& GetIntentSource(const UserIntentData& intentData)
{
  return intentData.source;
}

UserIntentComponent::UserIntentComponent(const Robot& robot, const Json::Value& userIntentMapConfig)
  : IDependencyManagedComponent( this, BCComponentID::UserIntentComponent )
  , _intentMap( new UserIntentMap(userIntentMapConfig, robot.GetContext()) )
  , _context( robot.GetContext() )
{
  
  // setup cloud intent handler
  const auto& serverName = GetServerName( robot );
  _server.reset( new BehaviorComponentCloudServer( _context, std::bind( &UserIntentComponent::OnCloudData,
                                                                        this,
                                                                        std::placeholders::_1),
                                                                        serverName ) );

  // setup trigger word handler
  auto triggerWordCallback = [this]( const AnkiEvent<RobotInterface::RobotToEngine>& event ){
    SetTriggerWordPending();
  };
  if( robot.GetRobotMessageHandler() != nullptr ) {
    _eventHandles.push_back( robot.GetRobotMessageHandler()->Subscribe( RobotInterface::RobotToEngineTag::triggerWordDetected,
                                                                        triggerWordCallback ) );
  }
  
  // setup app intent handler
  if( robot.HasExternalInterface() ){
    using namespace ExternalInterface;
    auto onEvent = [this](const AnkiEvent<MessageGameToEngine>& event) {
      if( event.GetData().GetTag() == MessageGameToEngineTag::AppIntent ) {
        OnAppIntent( event.GetData().Get_AppIntent() );
      }
    };
    _eventHandles.push_back( robot.GetExternalInterface()->Subscribe( MessageGameToEngineTag::AppIntent, onEvent ));
  }
  
}

UserIntentComponent::~UserIntentComponent()
{
}


bool UserIntentComponent::IsTriggerWordPending() const
{
  return _pendingTrigger;
}

void UserIntentComponent::ClearPendingTriggerWord()
{
  if( !_pendingTrigger ) {
    PRINT_NAMED_WARNING("UserIntentComponent.ClearPendingTrigger.TriggerNotSet",
                        "Trying to clear trigger but the trigger isn't set. This is likely a bug");
  }
  else {
    _pendingTrigger = false;
  }
}

void UserIntentComponent::SetTriggerWordPending()
{
  _waitingForTriggerWordGetInToFinish = true;
  if(!_responseToTriggerWordMap.empty()){
    // TODO: VIC-5733 This also needs to either check if _responseToTriggerWordMap contains an empty anim, or listen
    // for playing anims and compare tags
    auto lastElemIter = _responseToTriggerWordMap.rbegin();
    _robot->GetAnimationComponent().NotifyComponentOfAnimationStartedByAnimProcess(
      lastElemIter->response.getInAnimationName, lastElemIter->response.getInAnimationTag);
  }
  if(!GetEngineShouldRespondToTriggerWord()){
    PRINT_NAMED_INFO("UserIntentComponent.SetPendingTrigger.TriggerWordDetectionDisabled", 
                     "Trigger word detection disabled, so ignoring message");
    return;
  }

  if( _pendingTrigger ) {
    PRINT_NAMED_WARNING("UserIntentComponent.SetPendingTrigger.AlreadyPending",
                        "setting a pending trigger word but the last one hasn't been cleared");
  }

  _pendingTrigger = true;
  _pendingTriggerTick = BaseStationTimer::getInstance()->GetTickCount();

  if( _wasIntentError ) {
    PRINT_NAMED_WARNING("UserIntentComponent.SetTriggerWordPending.ClearingError",
                        "Previous intent gave us an error, but a new trigger word came in. Clearing the old error");
    _wasIntentError = false;
  }
}

bool UserIntentComponent::IsAnyUserIntentPending() const
{
  return (_pendingIntent != nullptr);
}

bool UserIntentComponent::IsUserIntentPending(UserIntentTag userIntent) const
{
  return (_pendingIntent != nullptr) && (_pendingIntent->intent.GetTag() == userIntent);
}

UserIntentPtr UserIntentComponent::ActivateUserIntent(UserIntentTag userIntent, const std::string& owner)
{
  if( !IsUserIntentPending(userIntent) ) {
    PRINT_NAMED_ERROR("UserIntentComponent.ActivateIntent.NoActive",
                      "'%s' is attempting to activate intent '%s', but %s is pending",
                      owner.c_str(),
                      UserIntentTagToString(userIntent),
                      _pendingIntent ? UserIntentTagToString(_pendingIntent->intent.GetTag()) : "nothing");
    return nullptr;
  }

  if( _activeIntent != nullptr ) {
    PRINT_NAMED_WARNING("UserIntentComponent.ActivateIntent.IntentAlreadyActive",
                        "%s is Trying to activate user intent '%s', but '%s' is still active",
                        owner.c_str(),
                        UserIntentTagToString(userIntent),
                        UserIntentTagToString(_activeIntent->intent.GetTag()));
  }
  
  PRINT_CH_DEBUG("BehaviorSystem", "UserIntentComponent.ActivateUserIntent",
                 "%s is activating intent '%s'",
                 owner.c_str(),
                 UserIntentTagToString(userIntent));

  _activeIntent = std::move(_pendingIntent);
  _activeIntent->activationID = ++sActivatedIntentID;

  // track the owner for easier debugging
  _activeIntentOwner = owner;

  return _activeIntent;
}

void UserIntentComponent::DeactivateUserIntent(UserIntentTag userIntent)
{
  if( !IsUserIntentActive(userIntent) ) {
    PRINT_NAMED_ERROR("UserIntentComponent.DeactivateUserIntent.NotActive",
                      "Attempting to deactivate intent '%s' (activated by %s) but '%s' is active",
                      UserIntentTagToString(userIntent),
                      _activeIntentOwner.c_str(),
                      _activeIntent ? UserIntentTagToString(_activeIntent->intent.GetTag()) : "nothing");
    return;
  }
  else {
    PRINT_CH_DEBUG("BehaviorSystem", "UserIntentComponent.DeactivateUserIntent",
                   "Deactivating intent '%s' (activated by %s)",
                   UserIntentTagToString(userIntent),
                   _activeIntentOwner.c_str());
    _activeIntent.reset();
    _activeIntentOwner.clear();
  }
}

bool UserIntentComponent::IsUserIntentActive(UserIntentTag userIntent) const
{
  return ( _activeIntent != nullptr ) && ( _activeIntent->intent.GetTag() == userIntent );
}

UserIntentPtr UserIntentComponent::GetUserIntentIfActive(UserIntentTag forIntent) const
{
  auto ret = IsUserIntentActive(forIntent) ? _activeIntent : nullptr;
  return ret;
}
  
UserIntentPtr UserIntentComponent::GetActiveUserIntent() const
{
  return _activeIntent;
}

void UserIntentComponent::DropUserIntent(UserIntentTag userIntent)
{
  if( IsUserIntentPending(userIntent) ) {
    _pendingIntent.reset();
  }
  else {
    PRINT_NAMED_WARNING("UserIntentComponent.DropUserIntent.NotPending",
                        "Trying to drop intent '%s' but %s is pending",
                        UserIntentTagToString(userIntent),
                        _pendingIntent ? UserIntentTagToString(_pendingIntent->intent.GetTag()) : "nothing");
  }
}
  
void UserIntentComponent::DropAnyUserIntent()
{
  if( !IsAnyUserIntentPending() ) {
    PRINT_NAMED_WARNING("UserIntentComponent.DropAnyUserIntent.IntentNotSet",
                        "Trying to clear a pending intent but the intent isn't set. This is likely a bug");
  }
  _pendingIntent.reset();
}

bool UserIntentComponent::IsUserIntentPending(UserIntentTag userIntent, UserIntent& extraData) const
{
  if( IsUserIntentPending(userIntent) ) {
    extraData = _pendingIntent->intent;
    return true;
  }
  else {
    return false;
  }
}

void UserIntentComponent::SetUserIntentPending(UserIntentTag userIntent, const UserIntentSource& source)
{
  // The following ensures that this method is only called for intents of type UserIntent_Void.
  // Ideally this would be a compile-time check, but that won't be possible unless all user intents
  // are hardcoded.
  // The uint8 below is a buffer of size 1, which matches the size of a UserIntent if it
  // has type UserIntent_Void. The first byte of the buffer is always the tag, so unpacking the
  // UserIntent from this buffer will succeed if the type is UserIntent_Void. If it fails, it will
  // assert in dev, but have no other repercussions.
  
  uint8_t buffer = Anki::Util::numeric_cast<uint8_t>( userIntent );
  UserIntent intent;
  intent.Unpack( &buffer, 1 ); // hit an assert? your userIntent is not of type UserIntent_Void. use overloaded method
  
  SetUserIntentPending( std::move(intent), source );
  
  static_assert(std::is_same<std::underlying_type<UserIntentTag>::type, uint8_t>::value,
                "If you change type, the above needs revisiting");
}
  
void UserIntentComponent::SetUserIntentPending(UserIntent&& userIntent, const UserIntentSource& source)
{
  if( _pendingIntent != nullptr ) {
    PRINT_NAMED_WARNING("UserIntentComponent.SetUserIntentPending.AlreadyPending",
                        "Setting pending user intent to '%s' which will overwrite '%s'",
                        UserIntentTagToString(userIntent.GetTag()),
                        UserIntentTagToString(_pendingIntent->intent.GetTag()));
  }
  
  
  if( _pendingIntent == nullptr ) {
    _pendingIntent.reset( new UserIntentData(userIntent, source) );
  } else {
    _pendingIntent->intent = std::move(userIntent);
    _pendingIntent->source = source;
  }
  
  if( ANKI_DEV_CHEATS ) {
    SendWebVizIntents();
  }

  _pendingIntentTick = BaseStationTimer::getInstance()->GetTickCount();
  _pendingIntentTimeoutEnabled = true;
}

void UserIntentComponent::DevSetUserIntentPending(UserIntentTag userIntent, const UserIntentSource& source)
{
  SetUserIntentPending(userIntent, source);
}

void UserIntentComponent::DevSetUserIntentPending(UserIntent&& userIntent, const UserIntentSource& source)
{
  SetUserIntentPending(std::move(userIntent), source);
}

void UserIntentComponent::DevSetUserIntentPending(UserIntentTag userIntent)
{
  SetUserIntentPending(userIntent, UserIntentSource::Unknown);
}

void UserIntentComponent::DevSetUserIntentPending(UserIntent&& userIntent)
{
  SetUserIntentPending(std::move(userIntent), UserIntentSource::Unknown);
}

void UserIntentComponent::SetUserIntentTimeoutEnabled(bool isEnabled)
{
  // if we're re-enabling the timeout warning, reset the tick count
  if( isEnabled && !_pendingIntentTimeoutEnabled ) {
    _pendingIntentTick = BaseStationTimer::getInstance()->GetTickCount();
  }
  _pendingIntentTimeoutEnabled = isEnabled;
}

void UserIntentComponent::SetCloudIntentPending(const std::string& cloudIntent)
{
  _devLastReceivedCloudIntent = cloudIntent;
  
  SetUserIntentPending( _intentMap->GetUserIntentFromCloudIntent(cloudIntent), UserIntentSource::Voice );
}

bool UserIntentComponent::SetCloudIntentPendingFromJSON(const std::string& cloudJsonStr)
{
  // use the most permissive reader possible
  Json::Reader reader(Json::Features::all());
  Json::Value json;

  const bool parsedOK = reader.parse(cloudJsonStr, json, false);
  if( !parsedOK ) {
    PRINT_NAMED_WARNING("UserIntentComponent.SetCloudIntentPendingFromJSON.BadJson",
                        "Could not parse json from cloud string!");
    return false;
  }

  return SetCloudIntentPendingFromJSONValue(std::move(json));
}

bool UserIntentComponent::SetCloudIntentPendingFromJSONValue(Json::Value json)
{
  std::string cloudIntent;
  if( !JsonTools::GetValueOptional(json, kCloudIntentJsonKey, cloudIntent) ) {
    PRINT_NAMED_WARNING("UserIntentComponent.SetCloudIntentPendingFromJSON.MissingIntentKey",
                        "Cloud json missing key '%s'",
                        kCloudIntentJsonKey);
    return false;
  }
  
  auto& params = json[kParamsKey];
  const bool hasParams = !params.isNull();
  
  Json::Value emptyJson;
  Json::Value& intentJson = hasParams ? params : emptyJson;
  
  UserIntentTag userIntentTag = _intentMap->GetUserIntentFromCloudIntent(cloudIntent);
  
  if( hasParams ) {
    // translate variable names, if necessary
    _intentMap->SanitizeCloudIntentVariables( cloudIntent, params );
  }
  
  ANKI_VERIFY( json["type"].isNull(),
               "UserIntentComponent.SetCloudIntentPendingFromJson.Reserved",
               "cloud intent '%s' contains reserved key 'type'",
               cloudIntent.c_str() );

  UserIntent pendingIntent;
  
  // Set up json to look like a union
  intentJson["type"] = UserIntentTagToString(userIntentTag);
  const bool setOK = pendingIntent.SetFromJSON(intentJson);
  
  // the UserIntent will have size 1 if it's a UserIntent_Void, which means the user intent
  // corresponding to this cloud intent should _not_ have data.
  using Tag = std::underlying_type<UserIntentTag>::type;
  const bool expectedParams = (pendingIntent.Size() > sizeof(Tag));
  static_assert( std::is_same<Tag, uint8_t>::value,
                 "If the type changes, you need to rethink this");
    
  if( !setOK ) {
    PRINT_NAMED_WARNING("UserIntentComponent.SetCloudIntentPendingFromJSON.BadParams",
                        "could not parse user intent '%s' from cloud intent of type '%s'",
                        UserIntentTagToString(userIntentTag),
                        cloudIntent.c_str());
    // NOTE: also don't set the pending intent, since the request was malformed
    return false;
  } else if( !expectedParams && hasParams ) {
    // simply ignore the extraneous data but continue
    PRINT_NAMED_WARNING( "UserIntentComponent.SetCloudIntentPendingFromJson.ExtraData",
                         "Intent '%s' has unexpected params",
                         cloudIntent.c_str() );
  } else if( expectedParams && !hasParams ) {
    // missing params, bail
    PRINT_NAMED_WARNING( "UserIntentComponent.SetCloudIntentPendingFromJson.MissingParams",
                         "Intent '%s' did not contain required params",
                         cloudIntent.c_str() );
    return false;
  }
  
  
  if( !_whitelistedIntents.empty() ) {
    // only pass on whitelisted intents
    if( _whitelistedIntents.find(userIntentTag) == _whitelistedIntents.end() ) {
      PRINT_NAMED_INFO( "UserIntentComponent.IgnoringNonWhitelist.Cloud", "Ignoring intent %s", UserIntentTagToString(userIntentTag) );
      pendingIntent = UserIntent::Createunmatched_intent({});
    }
  }
  
  _devLastReceivedCloudIntent = cloudIntent;
  
  DevSetUserIntentPending( std::move(pendingIntent), UserIntentSource::Voice );
  
  return true;
}

void UserIntentComponent::InitDependent( Vector::Robot* robot, const BCCompMap& dependentComps )
{
  _robot = robot;

  auto callback = [this](){
    _waitingForTriggerWordGetInToFinish = false;
  };

  _tagForTriggerWordGetInCallbacks = _robot->GetAnimationComponent().SetTriggerWordGetInCallback(callback);
}

void UserIntentComponent::UpdateDependent(const BCCompMap& dependentComps)
{ 
  {
    std::lock_guard<std::mutex> lock{_mutex};
    if( _pendingCloudIntent.GetTag() != CloudMic::MessageTag::INVALID ) {
      switch ( _pendingCloudIntent.GetTag() ) {
        case CloudMic::MessageTag::result:
        {
          auto json = _pendingCloudIntent.Get_result().GetJSON();
          bool ok = true;
          if ( json[kAltParamsKey].isString() ) {
            // params are encoded as a string, but we need to make it a Json::Value
            std::string jsonStr{ json[kAltParamsKey].asString() };
            if ( jsonStr.size() > 0 ) {
              Json::Reader reader;
              Json::Value val;
              ok = reader.parse( jsonStr, val, false );
              if( !ok ) {
                PRINT_NAMED_WARNING( "UserIntentComponent.UpdatePendingIntent.BadJson",
                                    "Could not parse json from cloud string: %s", jsonStr.c_str() );
              }
              else if ( val.size() > 0 ) {
                json[kParamsKey] = std::move(val);
              }
            }
            json.removeMember(kAltParamsKey);
          }
          if ( ok ) {
            SetCloudIntentPendingFromJSONValue( std::move( json ) );
          }
          _isStreamOpen = false;

          if( _wasIntentError ) {
            PRINT_NAMED_WARNING("UserIntentComponent.GotCloudIntent.ClearingError",
                                "Previous intent gave us an error, but a new intent word came in. Clearing the error");
            _wasIntentError = false;
          }

          break;
        }

        case CloudMic::MessageTag::streamTimeout:
        case CloudMic::MessageTag::error:
        {
          PRINT_NAMED_INFO("UserIntentComponent.UpdatePendingIntent.GotError",
                           "Got cloud error message type %s",
                           CloudMic::MessageTagToString( _pendingCloudIntent.GetTag() ));

          _wasIntentError = true;
          _isStreamOpen = false;
          break;
        }

        case CloudMic::MessageTag::streamOpen:
        {
          PRINT_NAMED_INFO("UserIntentComponent.UpdatePendingIntent.StreamOpen",
                           "Now streaming to cloud");
          _isStreamOpen = true;
          break;
        }
        
        default:
          PRINT_NAMED_WARNING("UserIntentComponent.UpdatePendingIntent.SkipOther",
                        "Skipping non-intent (and non-error) result cloud message: '%s'",
                        CloudMic::MessageTagToString( _pendingCloudIntent.GetTag() ) );
      }
      _pendingCloudIntent = {};
    }
  }
  
  
  const size_t currTick = BaseStationTimer::getInstance()->GetTickCount();
  // const float currTime = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();

  // if things pend too long they will queue up and trigger at the wrong time, which will be wrong and
  // confusing. Issue warnings here and clear the pending tick / intent if they aren't handled quickly enough
  
  if( _pendingTrigger ) {
    const size_t dt = currTick - _pendingTriggerTick;
    if( dt >= kMaxTicksToWarn ) {
      PRINT_NAMED_WARNING("UserIntentComponent.Update.PendingTriggerNotCleared",
                          "Trigger has been pending for %zu ticks",
                          dt);
    }
    if( dt >= kMaxTicksToClear ) {      
      PRINT_NAMED_ERROR("UserIntentComponent.Update.PendingTriggerNotCleared.ForceClear",
                        "Trigger has been pending for %zu ticks, forcing a clear",
                        dt);
      _pendingTrigger = false;
    }
  }

  if( _pendingIntent != nullptr ) {
    if( _pendingIntentTimeoutEnabled ) {
      const size_t dt = currTick - _pendingIntentTick;
      if( dt >= kMaxTicksToWarn ) {
        PRINT_NAMED_WARNING("UserIntentComponent.Update.PendingIntentNotCleared.Warn",
                            "Intent '%s' has been pending for %zu ticks",
                            UserIntentTagToString(_pendingIntent->intent.GetTag()),
                            dt);
      }
      if( dt >= kMaxTicksToClear ) {
        PRINT_NAMED_ERROR("UserIntentComponent.Update.PendingIntentNotCleared.ForceClear",
                          "Intent '%s' has been pending for %zu ticks, forcing a clear",
                          UserIntentTagToString(_pendingIntent->intent.GetTag()),
                          dt);
        _pendingIntent.reset();
        _wasIntentUnclaimed = true;
      }
    }
  }
}


void UserIntentComponent::StartWakeWordlessStreaming( CloudMic::StreamType streamType )
{
  RobotInterface::StartWakeWordlessStreaming message{ static_cast<uint8_t>(streamType) };
  _robot->SendMessage( RobotInterface::EngineToRobot( std::move(message) ) );
}


void UserIntentComponent::PushResponseToTriggerWord(const std::string& id, const AnimationTrigger& getInAnimTrigger, 
                                                    const AudioEngine::Multiplexer::PostAudioEvent& postAudioEvent, 
                                                    bool shouldTriggerWordStartStream)
{
  std::string animName;
  auto* data_ldr = _robot->GetContext()->GetDataLoader();
  if( data_ldr->HasAnimationForTrigger(getInAnimTrigger) )
  {
    const auto groupName = data_ldr->GetAnimationForTrigger(getInAnimTrigger);
    if( !groupName.empty() ) {
      animName = _robot->GetAnimationComponent().GetAnimationNameFromGroup(groupName);
      if(animName.empty()){
        PRINT_NAMED_WARNING("UserIntentComponent.PushResponseToTriggerWord.AnimationNotFound",
                            "No animation returned for group %s",
                            groupName.c_str());
      }
    }else{
      PRINT_NAMED_WARNING("UserIntentComponent.PushResponseToTriggerWord.GroupNotFound",
                          "Group not found for trigger %s",
                          AnimationTriggerToString(getInAnimTrigger));
    }
  }

  PushResponseToTriggerWord(id, animName, postAudioEvent, shouldTriggerWordStartStream);
}


void UserIntentComponent::PushResponseToTriggerWord(const std::string& id, const std::string& getInAnimationName, 
                                                    const AudioEngine::Multiplexer::PostAudioEvent& postAudioEvent, 
                                                    bool shouldTriggerWordStartStream)
{
  RobotInterface::SetTriggerWordResponse msg;
  msg.getInAnimationTag = _tagForTriggerWordGetInCallbacks;
  msg.postAudioEvent = postAudioEvent;
  msg.getInAnimationName = getInAnimationName;
  msg.shouldTriggerWordStartStream = shouldTriggerWordStartStream;
  PushResponseToTriggerWordInternal(id, std::move(msg));
}


void UserIntentComponent::PopResponseToTriggerWord(const std::string& id)
{
  auto compareFunc = [&id](const TriggerWordResponseEntry& entry){
    return entry.setID == id;
  };
  auto iter = std::find_if(_responseToTriggerWordMap.begin(), _responseToTriggerWordMap.end(), compareFunc);
  if(iter == _responseToTriggerWordMap.end()){
    PRINT_NAMED_WARNING("UserIntentComponent.PopResponseToTriggerWord.idNotInStack",
                        "request to remove id %s, but it has not set a trigger word response",
                        id.c_str());
    return;
  }

  iter = _responseToTriggerWordMap.erase(iter);
  // Check to see if the top of the stack was removed, and send a new trigger response
  if(iter == _responseToTriggerWordMap.end()){
    if(!_responseToTriggerWordMap.empty()){
      RobotInterface::SetTriggerWordResponse intentionalCopy = _responseToTriggerWordMap.rbegin()->response;
      _robot->SendMessage(RobotInterface::EngineToRobot(std::move(intentionalCopy)));
    }else{
      RobotInterface::SetTriggerWordResponse blankMessage;
      _robot->SendMessage(RobotInterface::EngineToRobot(std::move(blankMessage)));
    }
  }
}


void UserIntentComponent::AlterStreamStateForCurrentResponse(const std::string& id,  bool shouldTriggerWordStartStream)
{
  if(_responseToTriggerWordMap.empty()){
    PRINT_NAMED_WARNING("UserIntentComponent.AlterStreamStateForCurrentResponse.NoResponseToAlter","");
    return;
  }

  auto intentionalCopy = _responseToTriggerWordMap.rbegin()->response;
  intentionalCopy.shouldTriggerWordStartStream = shouldTriggerWordStartStream;
  if(_responseToTriggerWordMap.rbegin()->setID != id ||
     intentionalCopy != _responseToTriggerWordMap.rbegin()->response){
    PushResponseToTriggerWordInternal(id, std::move(intentionalCopy));
  }

}


void UserIntentComponent::PushResponseToTriggerWordInternal(const std::string& id, 
                                                            RobotInterface::SetTriggerWordResponse&& response)
{
  auto compareFunc = [&id](const TriggerWordResponseEntry& entry){
    return entry.setID == id;
  };
  auto iter = std::find_if(_responseToTriggerWordMap.begin(), _responseToTriggerWordMap.end(), compareFunc);

  if(iter != _responseToTriggerWordMap.end()){
    PRINT_NAMED_WARNING("UserIntentComponent.PushResponseToTriggerWord.idAlreadyPushedResponse",
                        "id %s already in use, removing old entry and adding new response to top of the stack",
                        id.c_str());
    _responseToTriggerWordMap.erase(iter);
  }

  _responseToTriggerWordMap.emplace_back(TriggerWordResponseEntry(id, std::move(response)));
  _robot->SendMessage(RobotInterface::EngineToRobot( std::move(response)) );
}



void UserIntentComponent::DisableEngineResponseToTriggerWord( const std::string& disablerName,  bool disable )
{
  if(disable){
     const auto res = _disableTriggerWordNames.insert(disablerName).second;
     if(!res){
       PRINT_NAMED_WARNING("UserIntentComponent.DisableEngineResponseToTriggerWord.AlreadyDisabled", 
                           "%s is attempting to disable the trigger word response, but it's already locking the trigger word",
                           disablerName.c_str());
     }
  }else{
    const auto numRemoved = _disableTriggerWordNames.erase(disablerName);
    if(numRemoved == 0){
      PRINT_NAMED_WARNING("UserIntentComponent.DisableEngineResponseToTriggerWord.DisablerNotDisablingTrigger", 
                          "%s is attempting to enable the trigger word, but it's not disabling it",
                          disablerName.c_str());
    }
  }
}
  
void UserIntentComponent::OnCloudData(CloudMic::Message&& data)
{
  PRINT_CH_INFO( "BehaviorSystem", "UserIntentComponent.OnCloudData", "'%s'", CloudMic::MessageTagToString(data.GetTag()) );
  
  std::lock_guard<std::mutex> lock{_mutex};
  _pendingCloudIntent = std::move(data);
}
  
void UserIntentComponent::OnAppIntent(const ExternalInterface::AppIntent& appIntent )
{
  UserIntentTag userIntentTag = _intentMap->GetUserIntentFromAppIntent( appIntent.intent );
  
  Json::Value json;
  // todo: eventually AppIntent should be its own union of structures, but
  // currently there's only one intent, with one arg, and it's not possible to transmit
  // a union over the temporary webservice handler. Once the real app-engine channels are open,
  // these two lines will need replacing
  json["type"] = UserIntentTagToString(userIntentTag);
  json["param"] = appIntent.param;
  
  _intentMap->SanitizeAppIntentVariables( appIntent.intent, json );
  
  UserIntent intent;
  if( ANKI_VERIFY( intent.SetFromJSON(json),
                   "UserIntentComponent.OnAppIntent.BadJson",
                   "Could not create user intent from app intent '%s'",
                   appIntent.intent.c_str() ) )
  {
    _devLastReceivedAppIntent = appIntent.intent;
    
    if( !_whitelistedIntents.empty() ) {
      // only pass on whitelisted intents
      if( _whitelistedIntents.find(userIntentTag) == _whitelistedIntents.end() ) {
        PRINT_NAMED_INFO( "UserIntentComponent.IgnoringNonWhitelist.App", "Ignoring intent %s", UserIntentTagToString(userIntentTag) );
        const static UserIntent unmatchedIntent = UserIntent::Createunmatched_intent({});
        intent = unmatchedIntent;
      }
    }
    
    DevSetUserIntentPending( std::move(intent), UserIntentSource::App );
  }
}
  
std::string UserIntentComponent::GetServerName(const Robot& robot) const
{
  return "ai_sock" + ((robot.GetID() == 0)
                      ? ""
                      : std::to_string(robot.GetID()));  // Offset port by robotID so that we can run sims with multiple robots
}
  
std::vector<std::string> UserIntentComponent::DevGetCloudIntentsList() const
{
  return _intentMap->DevGetCloudIntentsList();
}
  
std::vector<std::string> UserIntentComponent::DevGetAppIntentsList() const
{
  return _intentMap->DevGetAppIntentsList();
}
  
void UserIntentComponent::SendWebVizIntents()
{
  if( _context != nullptr ) {
    if( auto webSender = WebService::WebVizSender::CreateWebVizSender("intents",
                                                                      _context->GetWebService()) ) {

      Json::Value& toSend = webSender->Data();
      toSend = Json::arrayValue;

      {
        Json::Value blob;
        blob["intentType"] = "user";
        blob["type"] = "current-intent";
        blob["value"] = UserIntentTagToString( _pendingIntent->intent.GetTag() );
        toSend.append(blob);
      }

      if( !_devLastReceivedCloudIntent.empty() ) {
        Json::Value blob;
        blob["intentType"] = "cloud";
        blob["type"] = "current-intent";
        blob["value"] = _devLastReceivedCloudIntent;
        toSend.append(blob);
        _devLastReceivedCloudIntent.clear();
      }

      if( !_devLastReceivedAppIntent.empty() ) {
        Json::Value blob;
        blob["intentType"] = "app";
        blob["type"] = "current-intent";
        blob["value"] = _devLastReceivedAppIntent;
        toSend.append(blob);
        _devLastReceivedAppIntent.clear();
      }
    } // if (webSender ...
  }
}

}
}


/**
 * File: behaviorManager.cpp
 *
 * Author: Kevin Yoon
 * Date:   2/27/2014
 *
 * Description:
 *
 * Copyright: Anki, Inc. 2014
 **/

#include "anki/cozmo/basestation/behaviorManager.h"
#include "anki/cozmo/basestation/behaviors/behaviorInterface.h"
#include "anki/cozmo/basestation/demoBehaviorChooser.h"
#include "anki/cozmo/basestation/investorDemoFacesAndBlocksBehaviorChooser.h"
#include "anki/cozmo/basestation/investorDemoMotionBehaviorChooser.h"
#include "anki/cozmo/basestation/selectionBehaviorChooser.h"

#include "anki/cozmo/basestation/behaviorSystem/behaviorFactory.h"

#include "anki/cozmo/basestation/robot.h"
#include "anki/cozmo/basestation/events/ankiEvent.h"
#include "anki/cozmo/basestation/externalInterface/externalInterface.h"

#include "anki/cozmo/basestation/moodSystem/moodDebug.h"

#include "clad/types/behaviorChooserType.h"

#include "util/logging/logging.h"
#include "util/helpers/templateHelpers.h"

#define DEBUG_BEHAVIOR_MGR 0

namespace Anki {
namespace Cozmo {
  
  
  BehaviorManager::BehaviorManager(Robot& robot)
  : _isInitialized(false)
  , _forceReInit(false)
  , _robot(robot)
  , _behaviorFactory(new BehaviorFactory())
  , _minBehaviorTime_sec(1)
  {

  }
  
  Result BehaviorManager::Init(const Json::Value &config)
  {
    BEHAVIOR_VERBOSE_PRINT(DEBUG_BEHAVIOR_MGR, "BehaviorManager.Init.Initializing", "");
    
    // TODO: Set configuration data from Json...
    
    // TODO: Only load behaviors specified by Json?
    
    SetupOctDemoBehaviorChooser(config);
    
    if (_robot.HasExternalInterface())
    {
      IExternalInterface* externalInterface = _robot.GetExternalInterface();
      _eventHandlers.push_back(externalInterface->Subscribe(ExternalInterface::MessageGameToEngineTag::ActivateBehaviorChooser,
       [this, config] (const AnkiEvent<ExternalInterface::MessageGameToEngine>& event)
       {
         switch (event.GetData().Get_ActivateBehaviorChooser().behaviorChooserType)
         {
           case BehaviorChooserType::Demo:
           {
             SetupOctDemoBehaviorChooser(config);
             break;
           }
           case BehaviorChooserType::Selection:
           {
             SetBehaviorChooser(new SelectionBehaviorChooser(_robot, config));
             break;
           }
           case BehaviorChooserType::InvestorDemoMotion:
           {
             SetBehaviorChooser( new InvestorDemoMotionBehaviorChooser(_robot, config) );

             // BehaviorFactory& behaviorFactory = GetBehaviorFactory();
             // AddReactionaryBehavior( behaviorFactory.CreateBehavior(BehaviorType::ReactToPickup, _robot, config)->AsReactionaryBehavior() );
             // AddReactionaryBehavior( behaviorFactory.CreateBehavior(BehaviorType::ReactToCliff,  _robot, config)->AsReactionaryBehavior() );
             // AddReactionaryBehavior( behaviorFactory.CreateBehavior(BehaviorType::ReactToPoke,   _robot, config)->AsReactionaryBehavior() );
             break;
           }
           case BehaviorChooserType::InvestorDemoFacesAndBlocks:
           {
             SetBehaviorChooser( new InvestorDemoFacesAndBlocksBehaviorChooser(_robot, config) );
             
             // BehaviorFactory& behaviorFactory = GetBehaviorFactory();
             // AddReactionaryBehavior( behaviorFactory.CreateBehavior(BehaviorType::ReactToPoke,   _robot, config)->AsReactionaryBehavior() );
             break;
           }
           default:
           {
             PRINT_NAMED_WARNING("BehaviorManager.ActivateBehaviorChooser.InvalidChooser",
                                 "don't know how to create a chooser of type '%s'",
                                 BehaviorChooserTypeToString(
                                   event.GetData().Get_ActivateBehaviorChooser().behaviorChooserType));
             break;
           }
         }
       }));
    }
    _isInitialized = true;
    
    _lastSwitchTime_sec = 0.f;
    
    return RESULT_OK;
  }
  
  void BehaviorManager::SetupOctDemoBehaviorChooser(const Json::Value &config)
  {
    SetBehaviorChooser( new DemoBehaviorChooser(_robot, config) );
    
    BehaviorFactory& behaviorFactory = GetBehaviorFactory();
    AddReactionaryBehavior( behaviorFactory.CreateBehavior(BehaviorType::ReactToPickup, _robot, config)->AsReactionaryBehavior() );
    AddReactionaryBehavior( behaviorFactory.CreateBehavior(BehaviorType::ReactToCliff,  _robot, config)->AsReactionaryBehavior() );
    AddReactionaryBehavior( behaviorFactory.CreateBehavior(BehaviorType::ReactToPoke,   _robot, config)->AsReactionaryBehavior() );
  }
  
  // The AddReactionaryBehavior wrapper is responsible for setting up the callbacks so that important events will be
  // reacted to correctly - events will be given to the Chooser which may return a behavior to force switch to
  void BehaviorManager::AddReactionaryBehavior(IReactionaryBehavior* behavior)
  {
    // We map reactionary behaviors to the tag types they're going to care about
    _behaviorChooser->AddReactionaryBehavior(behavior);
    
    // If we don't have an external interface (Unit tests), bail early; we can't setup callbacks
    if (!_robot.HasExternalInterface()) {
      return;
    }
    
    // Callback for EngineToGame event that a reactionary behavior (possibly) cares about
    auto reactionsEngineToGameCallback = [this](const AnkiEvent<ExternalInterface::MessageEngineToGame>& event)
    {
      _forceSwitchBehavior = _behaviorChooser->GetReactionaryBehavior(_robot, event);
    };
    
    // Subscribe our own callback to these events
    IExternalInterface* interface = _robot.GetExternalInterface();
    for (auto tag : behavior->GetEngineToGameTags())
    {
      _eventHandlers.push_back(interface->Subscribe(tag, reactionsEngineToGameCallback));
    }
    
    // Callback for GameToEngine event that a reactionary behavior (possibly) cares about
    auto reactionsGameToEngineCallback = [this](const AnkiEvent<ExternalInterface::MessageGameToEngine>& event)
    {
      _forceSwitchBehavior = _behaviorChooser->GetReactionaryBehavior(_robot, event);
    };
    
    // Subscribe our own callback to these events
    for (auto tag : behavior->GetGameToEngineTags())
    {
      _eventHandlers.push_back(interface->Subscribe(tag, reactionsGameToEngineCallback));
    }
  }
  
  BehaviorManager::~BehaviorManager()
  {
    Util::SafeDelete(_behaviorChooser);
    Util::SafeDelete(_behaviorFactory);
  }
  
  void BehaviorManager::SwitchToNextBehavior(double currentTime_sec)
  {
    // If we're currently running our forced behavior but now switching away, clear it
    if (_currentBehavior == _forceSwitchBehavior)
    {
      _forceSwitchBehavior = nullptr;
    }
    
    // Initialize next behavior and make it the current one
    if (nullptr != _nextBehavior && _currentBehavior != _nextBehavior) {
      const bool isResuming = (_nextBehavior == _resumeBehavior);
      if (_nextBehavior->Init(currentTime_sec, isResuming) != RESULT_OK) {
        PRINT_NAMED_ERROR("BehaviorManager.SwitchToNextBehavior.InitFailed",
                          "Failed to initialize %s behavior.",
                          _nextBehavior->GetName().c_str());
      }
      
      #if SEND_MOOD_TO_VIZ_DEBUG
      {
        VizInterface::NewBehaviorSelected newBehaviorSelected;
        newBehaviorSelected.newCurrentBehavior = _nextBehavior ? _nextBehavior->GetName() : "null";
        VizManager::getInstance()->SendNewBehaviorSelected(std::move(newBehaviorSelected));
      }
      #endif // SEND_MOOD_TO_VIZ_DEBUG
      
      _resumeBehavior = nullptr;
      if (_currentBehavior && _nextBehavior->IsShortInterruption() && _currentBehavior->WantsToResume())
      {
        _resumeBehavior = _currentBehavior;
      }

      _currentBehavior = _nextBehavior;
      _nextBehavior = nullptr;
    }
  }
  
  Result BehaviorManager::Update(double currentTime_sec)
  {
    Result lastResult = RESULT_OK;
    
    if(!_isInitialized) {
      PRINT_NAMED_ERROR("BehaviorManager.Update.NotInitialized", "");
      return RESULT_FAIL;
    }
    
    _behaviorChooser->Update(currentTime_sec);
    
    // If we happen to have a behavior we really want to switch to, do so
    if (nullptr != _forceSwitchBehavior && _currentBehavior != _forceSwitchBehavior)
    {
      _nextBehavior = _forceSwitchBehavior;
      
      lastResult = InitNextBehaviorHelper(currentTime_sec);
      if(lastResult != RESULT_OK) {
        PRINT_NAMED_WARNING("BehaviorManager.Update.InitForcedBehavior",
                            "Failed trying to force next behavior, continuing with current.");
        lastResult = RESULT_OK;
      }
    }
    else if (nullptr == _currentBehavior ||
             currentTime_sec - _lastSwitchTime_sec > _minBehaviorTime_sec ||
             ( nullptr != _currentBehavior && ! _currentBehavior->IsRunnable(_robot, currentTime_sec) ))
    {
      // We've been in the current behavior long enough to consider switching
      lastResult = SelectNextBehavior(currentTime_sec);
      if(lastResult != RESULT_OK) {
        PRINT_NAMED_WARNING("BehaviorManager.Update.SelectNextFailed",
                            "Failed trying to select next behavior, continuing with current.");
        lastResult = RESULT_OK;
      }
      
      
      if (_currentBehavior != _nextBehavior && nullptr != _nextBehavior)
      {
        std::string nextName = _nextBehavior->GetName();
        BEHAVIOR_VERBOSE_PRINT(DEBUG_BEHAVIOR_MGR, "BehaviorManager.Update.SelectedNext",
                               "Selected next behavior '%s' at t=%.1f, last was t=%.1f",
                               nextName.c_str(), currentTime_sec, _lastSwitchTime_sec);
        
        _lastSwitchTime_sec = currentTime_sec;
      }
    }
    
    if(nullptr != _currentBehavior) {
      // We have a current behavior, update it.
      IBehavior::Status status = _currentBehavior->Update(currentTime_sec);
     
      switch(status)
      {
        case IBehavior::Status::Running:
          // Nothing to do! Just keep on truckin'....
          _currentBehavior->SetIsRunning(true);
          break;
          
        case IBehavior::Status::Complete:
          // Behavior complete, switch to next
          _currentBehavior->SetIsRunning(false);
          SwitchToNextBehavior(currentTime_sec);
          break;
          
        case IBehavior::Status::Failure:
          PRINT_NAMED_ERROR("BehaviorManager.Update.FailedUpdate",
                            "Behavior '%s' failed to Update().",
                            _currentBehavior->GetName().c_str());
          lastResult = RESULT_FAIL;
          _currentBehavior->SetIsRunning(false);
          
          // Force a re-init so if we reselect this behavior
          _forceReInit = true;
          SelectNextBehavior(currentTime_sec);
          break;
          
        default:
          PRINT_NAMED_ERROR("BehaviorManager.Update.UnknownStatus",
                            "Behavior '%s' returned unknown status %d",
                            _currentBehavior->GetName().c_str(), status);
          lastResult = RESULT_FAIL;
      } // switch(status)
    }
    else if(nullptr != _nextBehavior) {
      // No current behavior, but next behavior defined, so switch to it.
      SwitchToNextBehavior(currentTime_sec);
    }
    
    return lastResult;
  } // Update()
  
  
  Result BehaviorManager::InitNextBehaviorHelper(float currentTime_sec)
  {
    Result initResult = RESULT_OK;
    
    // Initialize the selected behavior, if it's not the one we're already running
    if(_nextBehavior != _currentBehavior || _forceReInit)
    {
      _forceReInit = false;
      if(nullptr != _currentBehavior) {
        // Interrupt the current behavior that's running if there is one. It will continue
        // to run on calls to Update() until it completes and then we will switch
        // to the selected next behavior
        const bool isShortInterrupt = _nextBehavior && _nextBehavior->IsShortInterruption();
        initResult = _currentBehavior->Interrupt(currentTime_sec, isShortInterrupt);
        
        if (nullptr != _nextBehavior)
        {
          BEHAVIOR_VERBOSE_PRINT(DEBUG_BEHAVIOR_MGR, "BehaviorManger.InitNextBehaviorHelper.Selected",
                                 "Selected %s to run next.", _nextBehavior->GetName().c_str());
        }
      }
    }
    return initResult;
  }
  
  Result BehaviorManager::SelectNextBehavior(double currentTime_sec)
  {
    _nextBehavior = _behaviorChooser->ChooseNextBehavior(_robot, currentTime_sec);

    if(nullptr == _nextBehavior) {
      PRINT_NAMED_ERROR("BehaviorManager.SelectNextBehavior.NoneRunnable", "");
      return RESULT_FAIL;
    }
    
    // Initialize the selected behavior
    return InitNextBehaviorHelper(currentTime_sec);
    
  } // SelectNextBehavior()
  
  Result BehaviorManager::SelectNextBehavior(const std::string& name, double currentTime_sec)
  {
    _nextBehavior = _behaviorChooser->GetBehaviorByName(name);
    if(nullptr == _nextBehavior) {
      PRINT_NAMED_ERROR("BehaviorManager.SelectNextBehavior.UnknownName",
                        "No behavior named '%s'", name.c_str());
      return RESULT_FAIL;
    }
    else if(_nextBehavior->IsRunnable(_robot, currentTime_sec) == false) {
      PRINT_NAMED_ERROR("BehaviorManager.SelecteNextBehavior.NotRunnable",
                        "Behavior '%s' is not runnable.", name.c_str());
      return RESULT_FAIL;
    }
    
    return InitNextBehaviorHelper(currentTime_sec);
  }
  
  void BehaviorManager::SetBehaviorChooser(IBehaviorChooser* newChooser)
  {
    // These behavior pointers are going to be invalidated, so clear them
    _currentBehavior = _nextBehavior = _forceSwitchBehavior = nullptr;
    _resumeBehavior = nullptr;

    if( _behaviorChooser != nullptr ) {
      PRINT_NAMED_INFO("BehaviorManager.SetBehaviorChooser.DeleteOld",
                       "deleting behavior chooser '%s'",
                       _behaviorChooser->GetName());
    }
    
    Util::SafeDelete(_behaviorChooser);
    
    _behaviorChooser = newChooser;
  }
  
  IBehavior* BehaviorManager::LoadBehaviorFromJson(const Json::Value& behaviorJson)
  {
    IBehavior* newBehavior = _behaviorFactory->CreateBehavior(behaviorJson, _robot);
    return newBehavior;
  }
  
} // namespace Cozmo
} // namespace Anki

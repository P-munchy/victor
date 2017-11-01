/**
 * File: ActivityBuildPyramid.h
 *
 * Author: Kevin M. Karol
 * Created: 04/27/17
 *
 * Description: Activity for building a pyramid
 *
 * Copyright: Anki, Inc. 2017
 *
 **/

#include "engine/aiComponent/behaviorComponent/activities/activities/activityBuildPyramid.h"

#include "engine/activeObject.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/behaviorAudioComponent.h"
#include "engine/aiComponent/behaviorComponent/behaviorContainer.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/behaviorExternalInterface.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/behaviorEventComponent.h"
#include "engine/aiComponent/behaviorComponent/behaviorManager.h"
#include "engine/aiComponent/behaviorComponent/behaviors/freeplay/buildPyramid/behaviorRespondPossiblyRoll.h"
#include "engine/aiComponent/behaviorComponent/behaviors/freeplay/buildPyramid/behaviorBuildPyramid.h"
#include "engine/aiComponent/behaviorComponent/behaviors/freeplay/buildPyramid/behaviorBuildPyramidBase.h"
#include "engine/aiComponent/behaviorComponent/behaviors/freeplay/buildPyramid/behaviorPyramidThankYou.h"
#include "engine/aiComponent/behaviorComponent/behaviorChoosers/behaviorChooserFactory.h"
#include "engine/aiComponent/behaviorComponent/behaviorChoosers/scoringBehaviorChooser.h"
#include "engine/blockWorld/blockConfigurationManager.h"
#include "engine/blockWorld/blockConfigurationPyramid.h"
#include "engine/blockWorld/blockWorld.h"
#include "engine/components/carryingComponent.h"
#include "engine/components/publicStateBroadcaster.h"
#include "anki/common/basestation/jsonTools.h"
#include "anki/common/basestation/utils/timer.h"
#include "anki/common/basestation/objectIDs.h"
#include "engine/robot.h"

#include "clad/externalInterface/messageEngineToGameTag.h"
#include "clad/types/behaviorComponent/behaviorTypes.h"


namespace Anki {
namespace Cozmo {

namespace{
using EngineToGameEvent = AnkiEvent<ExternalInterface::MessageEngineToGame>;
using GameToEngineEvent = AnkiEvent<ExternalInterface::MessageGameToEngine>;

static const char* kSetupChooserConfigKey = "setupChooser";
static const char* kBuildChooserConfigKey = "buildChooser";


static const int kMinUprightBlocksForPyramid      = 3;
static const float kDelayAccountForPlacing_s      = 3.0f;
static const float kDelayAccountForBaseCreation_s = 5.0f;

// Interval at which disconnected cube orientations are pulled from block world
static const float kIntervalCheckCubeOrientation = 1.0f;
static const float kIntervalForceUpdateLightMusicState = 1.0f;

// Pyramid Light constants
static const constexpr uint kBaseFormedTimeOn                  = 500;
static const constexpr uint kPyramidDenouementBaseOff_ms       = 650;
static const constexpr uint kPyramidDenouementAdditionalOff_ms = 75;


static const std::map<AxisName,UpAxis> kAxisNameMap = {
  {AxisName::Z_POS, UpAxis::ZPositive},
  {AxisName::Z_NEG, UpAxis::ZNegative},
  {AxisName::Y_POS, UpAxis::YPositive},
  {AxisName::Y_NEG, UpAxis::YNegative},
  {AxisName::X_POS, UpAxis::XPositive},
  {AxisName::X_NEG, UpAxis::XNegative}
};



static ObjectLights kEmptyObjectLights = {};


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
static const char* kLockForFullPyramidProcess = "lockTriggersFullPyramid";

constexpr ReactionTriggerHelpers::FullReactionArray kAffectFullPyramidProcessArray = {
  {ReactionTrigger::CliffDetected,                false},
  {ReactionTrigger::CubeMoved,                    false},
  {ReactionTrigger::FacePositionUpdated,          false},
  {ReactionTrigger::FistBump,                     true},
  {ReactionTrigger::Frustration,                  false},
  {ReactionTrigger::Hiccup,                       false},
  {ReactionTrigger::MotorCalibration,             false},
  {ReactionTrigger::NoPreDockPoses,               false},
  {ReactionTrigger::ObjectPositionUpdated,        false},
  {ReactionTrigger::PlacedOnCharger,              false},
  {ReactionTrigger::PetInitialDetection,          false},
  {ReactionTrigger::RobotPickedUp,                false},
  {ReactionTrigger::RobotPlacedOnSlope,           false},
  {ReactionTrigger::ReturnedToTreads,             false},
  {ReactionTrigger::RobotOnBack,                  false},
  {ReactionTrigger::RobotOnFace,                  false},
  {ReactionTrigger::RobotOnSide,                  false},
  {ReactionTrigger::RobotShaken,                  false},
  {ReactionTrigger::Sparked,                      false},
  {ReactionTrigger::UnexpectedMovement,           false},
  {ReactionTrigger::VC,                           false}
};

static_assert(ReactionTriggerHelpers::IsSequentialArray(kAffectFullPyramidProcessArray),
              "Reaction triggers duplicate or non-sequential");

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
static const char* kLockForPyramidSetup = "lockTriggersPyramidSetup";

constexpr ReactionTriggerHelpers::FullReactionArray kAffectPyramidSetupArray = {
  {ReactionTrigger::CliffDetected,                false},
  {ReactionTrigger::CubeMoved,                    false},
  {ReactionTrigger::FacePositionUpdated,          false},
  {ReactionTrigger::FistBump,                     false},
  {ReactionTrigger::Frustration,                  false},
  {ReactionTrigger::Hiccup,                       false},
  {ReactionTrigger::MotorCalibration,             false},
  {ReactionTrigger::NoPreDockPoses,               false},
  {ReactionTrigger::ObjectPositionUpdated,        true},
  {ReactionTrigger::PlacedOnCharger,              false},
  {ReactionTrigger::PetInitialDetection,          false},
  {ReactionTrigger::RobotPickedUp,                false},
  {ReactionTrigger::RobotPlacedOnSlope,           false},
  {ReactionTrigger::ReturnedToTreads,             false},
  {ReactionTrigger::RobotOnBack,                  false},
  {ReactionTrigger::RobotOnFace,                  false},
  {ReactionTrigger::RobotOnSide,                  false},
  {ReactionTrigger::RobotShaken,                  false},
  {ReactionTrigger::Sparked,                      false},
  {ReactionTrigger::UnexpectedMovement,           false},
  {ReactionTrigger::VC,                           false}
};

static_assert(ReactionTriggerHelpers::IsSequentialArray(kAffectPyramidSetupArray),
              "Reaction triggers duplicate or non-sequential");
  
} // end namespace

struct PyramidCubePropertiesTracker{
private:
  ObjectID _objectID = 0;
  UpAxis _currentUpAxis = UpAxis::UnknownAxis;
  CubeAnimationTrigger _currentLightTrigger = CubeAnimationTrigger::Count;
  CubeAnimationTrigger _desiredLightTrigger = CubeAnimationTrigger::Count;
  ObjectLights _desiredLightModifier = ObjectLights();
  ActivityBuildPyramid::PyramidAssignment _assignment = ActivityBuildPyramid::PyramidAssignment::None;
  bool _hasAcknowledgedPositively = false;
  bool _hasEverBeenUpright = false;
  
public:
  ObjectID GetObjectID() const{ return _objectID;}
  UpAxis GetCurrentUpAxis() const{ return _currentUpAxis;}
  CubeAnimationTrigger GetCurrentLightTrigger() const{ return _currentLightTrigger;}
  CubeAnimationTrigger GetDesiredLightTrigger() const{ return _desiredLightTrigger;}
  const ObjectLights& GetDesiredLightModifier() const{ return _desiredLightModifier;}
  ActivityBuildPyramid::PyramidAssignment GetPyramidAssignment() const{ return _assignment;}
  bool GetHasAcknowledgedPositively() const { return _hasAcknowledgedPositively;}
  bool GetHasEverBeenUpright(){ return _hasEverBeenUpright;}
  
  void SetObjectID(ObjectID objectID){ _objectID = objectID;}
  void SetUpAxis(UpAxis upAxis){
    _currentUpAxis = upAxis;
  }
  void SetCurrentLightTrigger(CubeAnimationTrigger trigger)
  { _currentLightTrigger = trigger;}
  void SetDesiredLightModifier(const ObjectLights& modifier)
  { _desiredLightModifier = modifier;}
  void SetDesiredLightTrigger(CubeAnimationTrigger trigger)
  { _desiredLightTrigger = trigger;}
  void SetPyramidAssignment(ActivityBuildPyramid::PyramidAssignment assignment)
  { _assignment = assignment;}
  void SetHasAcknowledgedPositively(bool hasAcknowledged) { _hasAcknowledgedPositively = hasAcknowledged;}
  void SetHasEverBeenUpright(bool upright){ _hasEverBeenUpright = upright;}
};
  
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
ActivityBuildPyramid::ActivityBuildPyramid(const Json::Value& config)
: IActivity(config)
, _activeBehaviorChooser(nullptr)
, _chooserPhase(ChooserPhase::None)
, _lastUprightBlockCount(-1)
, _pyramidObjectiveAchieved(false)
, _nextTimeCheckBlockOrientations_s(-1.0f)
, _nextTimeForceUpdateLightMusic_s(-1.0f)
, _currentPyramidConstructionStage(PyramidConstructionStage::NoneStage)
, _highestAudioStageReached(PyramidConstructionStage::NoneStage)
, _lastTimeConstructionStageChanged_s(0.0f)
, _lastCountBasesSeen(0)
, _uprightAnimIndex(0)
, _onSideAnimIndex(0)
, _forceLightMusicUpdate(false)
, _timeRespondedRollStartedPreviously_s(-1.0f)
{

}
  
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
ActivityBuildPyramid::~ActivityBuildPyramid()
{
  _behaviorPyramidThankYou = nullptr;
  _behaviorRespondPossiblyRoll = nullptr;
  _behaviorBuildPyramidBase = nullptr;
  _behaviorBuildPyramid = nullptr;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ActivityBuildPyramid::InitActivity(BehaviorExternalInterface& behaviorExternalInterface)
{
  /////////
  // Get pointers to all behaviors that must be manually called
  ///////
  
  // Get the build pyramid base behavior
  const BehaviorContainer& behaviorContainer = behaviorExternalInterface.GetBehaviorContainer();
  ICozmoBehaviorPtr baseRaw = behaviorContainer.FindBehaviorByID(BehaviorID::BuildPyramidBase);
  DEV_ASSERT(baseRaw != nullptr &&
             baseRaw->GetClass() == BehaviorClass::BuildPyramidBase,
             "BuildPyramidBehaviorChooser.BuildPyramidBase.ImproperClassRetrievedForName");
  
  _behaviorBuildPyramidBase = std::static_pointer_cast<BehaviorBuildPyramidBase>(baseRaw);
  DEV_ASSERT(_behaviorBuildPyramidBase,
             "BuildPyramidBehaviorChooser.BehaviorBuildBase.PointerNotSet");
  
  // Get the build pyramid behavior
  ICozmoBehaviorPtr pyramidRaw = behaviorContainer.FindBehaviorByID(BehaviorID::BuildPyramid);
  DEV_ASSERT(pyramidRaw != nullptr &&
             pyramidRaw->GetClass() == BehaviorClass::BuildPyramid,
             "BuildPyramidBehaviorChooser.BuildPyramid.ImproperClassRetrievedForName");
  
  _behaviorBuildPyramid = std::static_pointer_cast<BehaviorBuildPyramid>(pyramidRaw);
  DEV_ASSERT(_behaviorBuildPyramid,
             "BuildPyramidBehaviorChooser.BehaviorBuildPyramid.PointerNotSet");
  
  // Get the put down cube behavior
  ICozmoBehaviorPtr putDownRaw = behaviorContainer.FindBehaviorByID(BehaviorID::PyramidPutDownBlock);
  DEV_ASSERT(putDownRaw != nullptr &&
             putDownRaw->GetClass() == BehaviorClass::PutDownBlock,
             "BuildPyramidBehaviorChooser.PutDownBlock.ImproperClassRetrievedForName");
  
  // Get the respond possibly roll behavior
  ICozmoBehaviorPtr respondRoll = behaviorContainer.FindBehaviorByID(BehaviorID::PyramidRespondPossiblyRoll);
  DEV_ASSERT(respondRoll != nullptr &&
             respondRoll->GetClass() == BehaviorClass::RespondPossiblyRoll,
             "BuildPyramidBehaviorChooser.RespondRoll.ImproperClassRetrievedForName");
  
  _behaviorRespondPossiblyRoll = std::static_pointer_cast<BehaviorRespondPossiblyRoll>(respondRoll);
  DEV_ASSERT(_behaviorRespondPossiblyRoll,
             "BuildPyramidBehaviorChooser.RespondRoll.PointerNotSet");
  
  // Get the pyramid thank you behavior
  ICozmoBehaviorPtr pyramidThankYou = behaviorContainer.FindBehaviorByID(BehaviorID::PyramidThankYou);
  DEV_ASSERT(pyramidThankYou != nullptr &&
             pyramidThankYou->GetClass() == BehaviorClass::PyramidThankYou,
             "BuildPyramidBehaviorChooser.PyramidThankYou.ImproperClassRetrievedForName");
  
  _behaviorPyramidThankYou = std::static_pointer_cast<BehaviorPyramidThankYou>(pyramidThankYou);
  DEV_ASSERT(_behaviorRespondPossiblyRoll,
             "BuildPyramidBehaviorChooser.PyramidThankYou.PointerNotSet");
  
  
  /////////
  // Get Choosers for setup/build when simple scoring is needed
  ///////
  const Json::Value& simpleChooserJSON = _config[kSetupChooserConfigKey];
  const Json::Value& buildChooserJSON  = _config[kBuildChooserConfigKey];
  
  _setupSimpleChooser = BehaviorChooserFactory::CreateBehaviorChooser(behaviorExternalInterface, simpleChooserJSON);
  _buildSimpleChooser = BehaviorChooserFactory::CreateBehaviorChooser(behaviorExternalInterface, buildChooserJSON);
  _activeBehaviorChooser = _setupSimpleChooser.get();
  
  /////////
  // Setup callbacks to update cube light patterns/phase
  ///////
  
  behaviorExternalInterface.GetStateChangeComponent().SubscribeToTags(this,
      {
        ExternalInterface::MessageEngineToGameTag::ObjectUpAxisChanged,
        ExternalInterface::MessageEngineToGameTag::BehaviorObjectiveAchieved,
        ExternalInterface::MessageEngineToGameTag::ObjectConnectionState
      });
  
  behaviorExternalInterface.GetStateChangeComponent().SubscribeToTags(this,
      {
        ExternalInterface::MessageGameToEngineTag::RequestPyramidPreReqState
      });
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ActivityBuildPyramid::OnActivatedActivity(BehaviorExternalInterface& behaviorExternalInterface)
{
  _uprightAnimIndex = 0;
  _onSideAnimIndex = 0;
  _lastUprightBlockCount = -1;
  _currentPyramidConstructionStage = PyramidConstructionStage::NoneStage;
  _highestAudioStageReached = PyramidConstructionStage::NoneStage;
  _chooserPhase = ChooserPhase::None;
  _nextTimeCheckBlockOrientations_s = -1.0f;
  _nextTimeForceUpdateLightMusic_s = -1.0f;
  _timeRespondedRollStartedPreviously_s = _behaviorRespondPossiblyRoll->GetTimeActivated_s();
  
  _pyramidObjectiveAchieved = false;
  
  for(auto& entry: _pyramidCubePropertiesTrackers){
    entry.second.SetPyramidAssignment(ActivityBuildPyramid::PyramidAssignment::None);
    entry.second.SetHasAcknowledgedPositively(false);
    entry.second.SetDesiredLightTrigger(CubeAnimationTrigger::Count);
    entry.second.SetHasEverBeenUpright(UpAxis::ZPositive == entry.second.GetCurrentUpAxis());
  }
  
  {
    // DEPRECATED - Grabbing robot to support current cozmo code, but this should
    // be removed
    Robot& robot = behaviorExternalInterface.GetRobot();
    
    robot.GetBehaviorManager().DisableReactionsWithLock(kLockForFullPyramidProcess,
                                                        kAffectFullPyramidProcessArray);
  }
  
  _forceLightMusicUpdate = true;

  _behaviorBuildPyramid->SetNeedsActionID(_needsActionId);

  UpdateChooserPhase(behaviorExternalInterface);
  Update(behaviorExternalInterface);
}
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ActivityBuildPyramid::OnDeactivatedActivity(BehaviorExternalInterface& behaviorExternalInterface)
{
  // Make sure that all custom patterns are cleared off of the cubes
  for(auto& entry: _pyramidCubePropertiesTrackers){
    entry.second.SetDesiredLightTrigger(CubeAnimationTrigger::Count);
  }
  SetCubeLights(behaviorExternalInterface);
  _pyramidCubePropertiesTrackers.clear();
  
  if(behaviorExternalInterface.HasPublicStateBroadcaster()){
    auto& publicStateBroadcaster = behaviorExternalInterface.GetRobotPublicStateBroadcaster();
    publicStateBroadcaster.UpdateBroadcastBehaviorStage(BehaviorStageTag::Count, 0);
  }
  
  {
    // DEPRECATED - Grabbing robot to support current cozmo code, but this should
    // be removed
    Robot& robot = behaviorExternalInterface.GetRobot();
    
    // Make sure no behaviors are deactivated on leaving pyramid in case they're
    // also mapped to another behavior group
    robot.GetBehaviorManager().RemoveDisableReactionsLock(kLockForFullPyramidProcess);
    robot.GetBehaviorManager().RemoveDisableReactionsLock(kLockForPyramidSetup);
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ActivityBuildPyramid::HandleObjectConnectionStateChange(BehaviorExternalInterface& behaviorExternalInterface, const ObjectConnectionState& connectionState)
{
  // If object disconnected, remove it from the properties tracker map
  if(connectionState.connected){
    UpdateStateTrackerForUnrecognizedID(behaviorExternalInterface, connectionState.objectID);
  }else{
    _pyramidCubePropertiesTrackers.erase(connectionState.object_type);
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ActivityBuildPyramid::UpdateActiveBehaviorGroup(BehaviorExternalInterface& behaviorExternalInterface, bool settingUpPyramid)
{
  // DEPRECATED - Grabbing robot to support current cozmo code, but this should
  // be removed
  Robot& robot = behaviorExternalInterface.GetRobot();
  
  // Order matters
  if(settingUpPyramid){
    _activeBehaviorChooser = _setupSimpleChooser.get();
    // The setup phase has its own acknowledgments
    robot.GetBehaviorManager().DisableReactionsWithLock(kLockForPyramidSetup,
                                                        kAffectPyramidSetupArray);
  }else{
    _activeBehaviorChooser = _buildSimpleChooser.get();
    robot.GetBehaviorManager().RemoveDisableReactionsLock(kLockForPyramidSetup);
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool ActivityBuildPyramid::IsPyramidHardSpark(BehaviorExternalInterface& behaviorExternalInterface)
{
  // DEPRECATED - Grabbing robot to support current cozmo code, but this should
  // be removed
  const Robot& robot = behaviorExternalInterface.GetRobot();
  
  const bool isRequestedSparkHard = robot.GetBehaviorManager().IsRequestedSparkHard() &&
                    (robot.GetBehaviorManager().GetRequestedSpark() == UnlockId::BuildPyramid);
  const bool isActiveSparkHard = robot.GetBehaviorManager().IsActiveSparkHard() &&
                    (robot.GetBehaviorManager().GetActiveSpark() == UnlockId::BuildPyramid);
  
  return isRequestedSparkHard || isActiveSparkHard;
}



//////////////////////////////////////////////
//////////////////////////////////////////////
//////////////////////////////////////////////
// General Chooser Functions
//////////////////////////////////////////////
//////////////////////////////////////////////
//////////////////////////////////////////////


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool ActivityBuildPyramid::GetCubePropertiesTrackerByID(const ObjectID& id, PyramidCubePropertiesTracker*& pyramidCubeProperties)
{
  for(auto& entry: _pyramidCubePropertiesTrackers){
    if(entry.second.GetObjectID() == id){
      pyramidCubeProperties = &entry.second;
      return true;
    }
  }
  
  return false;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool ActivityBuildPyramid::GetCubePropertiesTrackerByAssignment(
                               const ActivityBuildPyramid::PyramidAssignment& id,
                               PyramidCubePropertiesTracker*& pyramidCubeProperties)
{
  for(auto& entry: _pyramidCubePropertiesTrackers){
    if(entry.second.GetPyramidAssignment() == id){
      pyramidCubeProperties = &entry.second;
      return true;
    }
  }
  
  return false;
}




// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ActivityBuildPyramid::CheckBlockWorldCubeOrientations(BehaviorExternalInterface& behaviorExternalInterface)
{
  typedef std::vector<const ObservableObject*> BlockList;
  
  BlockList knownDisconnectedBlocks;
  BlockWorldFilter knownDisconnectedBlockFilter;
  knownDisconnectedBlockFilter.SetAllowedFamilies(
                                                  {{ObjectFamily::LightCube, ObjectFamily::Block}});
  // Only rely on this block world update if the block is both known
  // and disconnected - otherwise, the up axis message is a more reliable update
  knownDisconnectedBlockFilter.SetFilterFcn([](const ObservableObject* ptr){
    if(ptr->IsPoseStateKnown() && ptr->GetActiveID() == -1){
      return true;
    }
    return false;
  });
  
  behaviorExternalInterface.GetBlockWorld().FindLocatedMatchingObjects(knownDisconnectedBlockFilter,
                                                                       knownDisconnectedBlocks);
  
  // we only want to update orientations from block world if the pose state is known
  // because the pose is only updated through observation, so if we've received
  // an axis changed message from the cube that is more accurate information which
  // the rotation matrix will contradict
  for(const ObservableObject* block: knownDisconnectedBlocks){
    if (nullptr != block)
    {
      PyramidCubePropertiesTracker* currentpyramidCubeProperties = nullptr;
      GetCubePropertiesTrackerByID(block->GetID(), currentpyramidCubeProperties);
      if(currentpyramidCubeProperties == nullptr)
      {
        // If the block with that ID doesn't exist, create a new one
        UpdateStateTrackerForUnrecognizedID(behaviorExternalInterface, block->GetID());
        ANKI_VERIFY(GetCubePropertiesTrackerByID(block->GetID(), currentpyramidCubeProperties),
                    "ActivityBuildPyramid.BlockWorldObjectNotAddedToTracker.TrackerIsStillNullptr", "");
      }
      
      AxisName name = block->GetPose().GetRotationMatrix().GetRotatedParentAxis<'Z'>();
      const UpAxis& currentUpAxis = kAxisNameMap.find(name)->second;
      
      if(currentUpAxis != currentpyramidCubeProperties->GetCurrentUpAxis()){
        currentpyramidCubeProperties->SetUpAxis(currentUpAxis);
        _objectAxisChangeIDs.insert(currentpyramidCubeProperties->GetObjectID());
      }
    }
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ActivityBuildPyramid::UpdateStateTrackerForUnrecognizedID(BehaviorExternalInterface& behaviorExternalInterface,
                                                               const ObjectID& objID)
{
  
  BlockWorldFilter blockInAnyFrameFilter;
  blockInAnyFrameFilter.SetAllowedIDs({objID});
  blockInAnyFrameFilter.SetOriginMode(BlockWorldFilter::OriginMode::InAnyFrame);
  const ObservableObject* block = behaviorExternalInterface.GetBlockWorld().FindLocatedMatchingObject(blockInAnyFrameFilter);
  
  if ( block == nullptr ) {
    // if there are no located instances, try with the connected ones
    block = behaviorExternalInterface.GetBlockWorld().GetConnectedActiveObjectByID(objID);
  }
  
  
  DEV_ASSERT(block != nullptr,
             "ActivityBuildPyramid.UpdateStateTracker.NoBlocksWithID");
  if(block != nullptr){
    // Remove previous entry for block type if it exists
    auto blockTypeIter = _pyramidCubePropertiesTrackers.find(block->GetType());
    if(blockTypeIter != _pyramidCubePropertiesTrackers.end()){
      _pyramidCubePropertiesTrackers.erase(blockTypeIter);
    }
    
    PyramidCubePropertiesTracker newTracker;
    newTracker.SetObjectID(objID);
    _pyramidCubePropertiesTrackers.insert(std::make_pair(block->GetType(), newTracker));
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ActivityBuildPyramid::UpdatePyramidAssignments(const std::shared_ptr<BehaviorBuildPyramidBase> behavior)
{
  for(auto& entry: _pyramidCubePropertiesTrackers){
    if(entry.second.GetPyramidAssignment() != ActivityBuildPyramid::PyramidAssignment::None){
      _forceLightMusicUpdate = true;
      entry.second.SetPyramidAssignment(ActivityBuildPyramid::PyramidAssignment::None);
    }
  }
  
  // Allows assignments to be cleared out by passing in nullptr
  if(behavior == nullptr){
    return;
  }
  
  
  ObjectID id;
  PyramidCubePropertiesTracker* tracker = nullptr;
  
  if(behavior->GetBaseBlockID(id)){
    if(GetCubePropertiesTrackerByID(id, tracker)){
      tracker->SetPyramidAssignment(ActivityBuildPyramid::PyramidAssignment::BaseBlock);
    }
  }
  
  if(behavior->GetStaticBlockID(id)){
    if(GetCubePropertiesTrackerByID(id, tracker)){
      tracker->SetPyramidAssignment(ActivityBuildPyramid::PyramidAssignment::StaticBlock);
    }
  }
  
  if(behavior->GetTopBlockID(id)){
    if(GetCubePropertiesTrackerByID(id, tracker)){
      tracker->SetPyramidAssignment(ActivityBuildPyramid::PyramidAssignment::TopBlock);
    }
  }
  
}

  
//////////////////////////////////////////////
//////////////////////////////////////////////
//////////////////////////////////////////////
// Functions relating to choose next behaivor
//////////////////////////////////////////////
//////////////////////////////////////////////
//////////////////////////////////////////////
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
ICozmoBehaviorPtr ActivityBuildPyramid::GetDesiredActiveBehaviorInternal(BehaviorExternalInterface& behaviorExternalInterface, const ICozmoBehaviorPtr currentRunningBehavior)
{
  UpdatePropertiesTrackerBasedOnRespondPossiblyRoll(behaviorExternalInterface,
                                                    currentRunningBehavior);
  
  ICozmoBehaviorPtr behavior = nullptr;
  switch(_chooserPhase){
    case ChooserPhase::SetupBlocks:
    {
      behavior = ChooseNextBehaviorSetup(behaviorExternalInterface, currentRunningBehavior);
      break;
    }
    case ChooserPhase::BuildingPyramid:
    {
      behavior = ChooseNextBehaviorBuilding(behaviorExternalInterface, currentRunningBehavior);
      break;
    }
    case ChooserPhase::None:
    {
      DEV_ASSERT(false, "ActivityBuildPyramid.ChooseNextBehavior.InvalidPhase");
      break;
    }
  }
  
  // There are a couple of behaviors that we don't want to interrupt with our custom
  // logic - so if the selected behavior is one of those, return it now, otherwise
  // see if there's a custom behavior that would like to take over.
  const bool behaviorCantBeOverriden = (behavior != nullptr) &&
                 (behavior->GetClass() == BehaviorClass::DriveOffCharger ||
                  behavior->GetClass() == BehaviorClass::KnockOverCubes);
  if(behaviorCantBeOverriden){
    return behavior;
  }
  
  
  // Thank the user if possible
  ICozmoBehaviorPtr customBehavior = CheckForShouldThankUser(behaviorExternalInterface, currentRunningBehavior);
  
  // Otherwise, see if we have to roll or respond to a block
  if(customBehavior == nullptr){
    customBehavior = CheckForResponsePossiblyRoll(behaviorExternalInterface, currentRunningBehavior);
  }
  
  _objectAxisChangeIDs.clear();
  return customBehavior != nullptr ? customBehavior : behavior;
}




// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
ICozmoBehaviorPtr  ActivityBuildPyramid::ChooseNextBehaviorSetup(BehaviorExternalInterface& behaviorExternalInterface,
                                                          const ICozmoBehaviorPtr currentRunningBehavior)
{
  return _activeBehaviorChooser->GetDesiredActiveBehavior(behaviorExternalInterface, currentRunningBehavior);
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
ICozmoBehaviorPtr  ActivityBuildPyramid::ChooseNextBehaviorBuilding(BehaviorExternalInterface& behaviorExternalInterface,
                                                             const ICozmoBehaviorPtr currentRunningBehavior)
{
  //  Priority of functions:
  //    Build full pyramid -> Build pyramid base -> Search/fast forward behaviors
  
  ICozmoBehaviorPtr bestBehavior = nullptr;
  
  if(_behaviorBuildPyramid->IsActivated() ||
     _behaviorBuildPyramid->WantsToBeActivated(behaviorExternalInterface)){
    
    bestBehavior = _behaviorBuildPyramid;
    // If the behavior has not been running, update pyramid assignments
    // and then re-set base lights to reflect any changes of base assignment
    if(!_behaviorBuildPyramid->IsActivated()){
      UpdatePyramidAssignments(_behaviorBuildPyramid);
      SetPyramidBaseLights(behaviorExternalInterface);
    }
    
  }else if(_behaviorBuildPyramidBase->IsActivated() ||
           _behaviorBuildPyramidBase->WantsToBeActivated(behaviorExternalInterface)){
    
    bestBehavior = _behaviorBuildPyramidBase;
    // If the behavior has not been running, update pyramid assignments
    // and then re-set base lights to reflect any changes of base assignment
    if(!_behaviorBuildPyramidBase->IsActivated()){
      UpdatePyramidAssignments(_behaviorBuildPyramidBase);
      SetPyramidBaseLights(behaviorExternalInterface);
    }
    
  }else{
    UpdatePyramidAssignments(nullptr);
    bestBehavior = _buildSimpleChooser->GetDesiredActiveBehavior(behaviorExternalInterface, currentRunningBehavior);
  }
  
  return bestBehavior;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
ICozmoBehaviorPtr ActivityBuildPyramid::CheckForShouldThankUser(BehaviorExternalInterface& behaviorExternalInterface,
                                                           const ICozmoBehaviorPtr currentRunningBehavior)
{
  // Run through all of the axis changes to find if thank you can run
  // and to update the pyramidCubeProperties tracking information
  ICozmoBehaviorPtr bestBehavior = nullptr;
  for(const auto& objectID: _objectAxisChangeIDs){
    PyramidCubePropertiesTracker* pyramidCubeProperties = nullptr;
    
    if(GetCubePropertiesTrackerByID(objectID, pyramidCubeProperties) &&
       pyramidCubeProperties != nullptr &&
       !pyramidCubeProperties->GetHasEverBeenUpright() &&
       pyramidCubeProperties->GetCurrentUpAxis() == UpAxis::ZPositive){
      
      const bool runningRollCube =
      currentRunningBehavior != nullptr &&
      currentRunningBehavior->GetClass() == BehaviorClass::RespondPossiblyRoll &&
      _behaviorRespondPossiblyRoll->GetResponseMetadata().GetObjectID() == objectID;
      
      bool rolledCubeHimself = false;
      if(runningRollCube){
        const auto& metadata = _behaviorRespondPossiblyRoll->GetResponseMetadata();
        rolledCubeHimself = metadata.GetReachedPreDocRoll();
      }
      
      if(!rolledCubeHimself){
        _behaviorPyramidThankYou->SetTargetID(objectID);
        if(_behaviorPyramidThankYou->IsActivated() ||
           _behaviorPyramidThankYou->WantsToBeActivated(behaviorExternalInterface)){
          bestBehavior = _behaviorPyramidThankYou;
        }
      }
    }
    
    if(pyramidCubeProperties != nullptr &&
       pyramidCubeProperties->GetCurrentUpAxis() == UpAxis::ZPositive){
      pyramidCubeProperties->SetHasEverBeenUpright(true);
    }
  }
  
  // If a thank you is already running, return it so that it's not interrupted
  if(currentRunningBehavior != nullptr &&
     currentRunningBehavior->GetClass() == _behaviorPyramidThankYou->GetClass()){
    return _behaviorPyramidThankYou;
  }
  
  // Otherwise, return the best new thank you (if there is one)
  return bestBehavior;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
ICozmoBehaviorPtr ActivityBuildPyramid::CheckForResponsePossiblyRoll(BehaviorExternalInterface& behaviorExternalInterface,
                                                                     const ICozmoBehaviorPtr currentRunningBehavior)
{
  // If any of the manually set behaviors are running, keep them running
  if(currentRunningBehavior != nullptr &&
     currentRunningBehavior->IsActivated()){
    if(currentRunningBehavior->GetClass() ==
       _behaviorRespondPossiblyRoll->GetClass()){
      return _behaviorRespondPossiblyRoll;
    }
  }
  
  ICozmoBehaviorPtr bestBehavior = nullptr;
  int numberOfCubesOnSide = 0;
  
  for(auto& entry: _pyramidCubePropertiesTrackers){
    if(entry.second.GetCurrentUpAxis() != UpAxis::ZPositive){
      numberOfCubesOnSide++;
    }
    
    const ObservableObject* object = behaviorExternalInterface.GetBlockWorld().GetLocatedObjectByID(entry.second.GetObjectID());
    if(object != nullptr){
      if(entry.second.GetCurrentUpAxis() != UpAxis::ZPositive){
        RespondPossiblyRollMetadata metadata(entry.second.GetObjectID(),
                                             _uprightAnimIndex,
                                             _onSideAnimIndex,
                                             false);

        _behaviorRespondPossiblyRoll->SetRespondPossiblyRollMetadata(metadata);
        if(_behaviorRespondPossiblyRoll->WantsToBeActivated(behaviorExternalInterface)){
          PRINT_CH_INFO("BuildPyramid",
                        "ActivityBuildPyramid.CheckForRespondPossiblyRoll.RespondToBlockOnSide",
                        "Responding to object %d which is on its side and rolling",
                        entry.second.GetObjectID().GetValue());
          bestBehavior = _behaviorRespondPossiblyRoll;
          break;
        }
      }
      
      if(bestBehavior == nullptr && !entry.second.GetHasAcknowledgedPositively()){
        const int onSideIdx = IsPyramidHardSpark(behaviorExternalInterface) ? _onSideAnimIndex : -1;
        RespondPossiblyRollMetadata metadata(entry.second.GetObjectID(),
                                             _uprightAnimIndex,
                                             onSideIdx,
                                             true);
        
        _behaviorRespondPossiblyRoll->SetRespondPossiblyRollMetadata(metadata);        
        if(_behaviorRespondPossiblyRoll->WantsToBeActivated(behaviorExternalInterface)){
          bestBehavior = _behaviorRespondPossiblyRoll;
          PRINT_CH_INFO("BuildPyramid",
                        "ActivityBuildPyramid.CheckForRespondPossiblyRoll.MayRespondToUpright",
                        "May respond to object %d positively if the block on its side is unknown",
                        entry.second.GetObjectID().GetValue());
        }
      }
    }
  }
  
  // We don't want to acknowledge positively if all cubes are upright and we can start
  // building
  return (numberOfCubesOnSide != 0)  ? bestBehavior : nullptr;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ActivityBuildPyramid::UpdatePropertiesTrackerBasedOnRespondPossiblyRoll(BehaviorExternalInterface& behaviorExternalInterface,
                                                                             const ICozmoBehaviorPtr currentRunningBehavior)
{
  // The respond possibly roll behavior may have updated properties while running
  const bool respondCurrentlyRunning = currentRunningBehavior != nullptr &&
  (currentRunningBehavior->GetClass() == BehaviorClass::RespondPossiblyRoll);
  
  // The chooser may not have gotten updated properties from the respond possibly
  // roll behavior before it stopped itself - if it has run since last updated
  // pull the properties just to check
  const bool runSinceLastTimeCheck = _timeRespondedRollStartedPreviously_s !=
  _behaviorRespondPossiblyRoll->GetTimeActivated_s();
  
  // If respond possibly roll isn't running, update the tracked last time it ran
  if(!respondCurrentlyRunning && runSinceLastTimeCheck){
    _timeRespondedRollStartedPreviously_s = _behaviorRespondPossiblyRoll->GetTimeActivated_s();
  }
  
  // Update respondPossiblyRoll tracker info
  if(respondCurrentlyRunning || runSinceLastTimeCheck){
    
    const auto& metadata =  _behaviorRespondPossiblyRoll->GetResponseMetadata();
    
    // Update animation trigger to play on the next time the behavior runs
    if(metadata.GetPlayedUprightAnim()){
      _uprightAnimIndex = metadata.GetUprightAnimIndex() + 1;
    }
    
    if(metadata.GetPlayedOnSideAnim()){
      _onSideAnimIndex = metadata.GetOnSideAnimIndex() + 1;
    }
    
    // Set acknowledged positevely if response was a positive response
    const ObjectID& target = metadata.GetObjectID();
    PyramidCubePropertiesTracker* tracker = nullptr;
    const ObservableObject* object = behaviorExternalInterface.GetBlockWorld().GetLocatedObjectByID(target);
    if(GetCubePropertiesTrackerByID(target, tracker) &&
       object != nullptr &&
       tracker->GetCurrentUpAxis() == UpAxis::ZPositive){
      tracker->SetHasAcknowledgedPositively(metadata.GetPlayedUprightAnim());
    }
  }
  
}

  
//////////////////////////////////////////////
//////////////////////////////////////////////
//////////////////////////////////////////////
// Functions relating to updating music/light state
//////////////////////////////////////////////
//////////////////////////////////////////////
//////////////////////////////////////////////


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Result ActivityBuildPyramid::Update_Legacy(BehaviorExternalInterface& behaviorExternalInterface)
{
  HandleMessageEvents(behaviorExternalInterface);
  
  const float currentTime_s = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
  if(currentTime_s > _nextTimeCheckBlockOrientations_s){
    CheckBlockWorldCubeOrientations(behaviorExternalInterface);
    _nextTimeCheckBlockOrientations_s = currentTime_s + kIntervalCheckCubeOrientation;
  }
  
  if(currentTime_s > _nextTimeForceUpdateLightMusic_s){
    _forceLightMusicUpdate = true;
    _nextTimeForceUpdateLightMusic_s = currentTime_s + kIntervalForceUpdateLightMusicState;
  }
  
  if(_objectAxisChangeIDs.size() > 0 ||
     _chooserPhase == ChooserPhase::None){
    UpdateChooserPhase(behaviorExternalInterface);
    
  }
  
  PyramidConstructionStage desiredState = CheckLightAndPyramidConstructionStage(behaviorExternalInterface);
  
  // Reasons why music/lights might need to be updated
  const bool constructionStageChanged = desiredState != _currentPyramidConstructionStage;
  const bool pyramidSetupStageChanged = _chooserPhase == ChooserPhase::SetupBlocks &&
  _objectAxisChangeIDs.size() > 0;
  
  const auto& pyramidBases = behaviorExternalInterface.GetBlockWorld().GetBlockConfigurationManager().GetPyramidBaseCache().GetBases();
  const bool numberOfPyramidBasesChanged = pyramidBases.size() != _lastCountBasesSeen;
  
  if(_forceLightMusicUpdate ||
     constructionStageChanged ||
     pyramidSetupStageChanged ||
     numberOfPyramidBasesChanged){
    UpdateMusic(behaviorExternalInterface, desiredState);
    UpdateDesiredLights(behaviorExternalInterface, desiredState);
    SetCubeLights(behaviorExternalInterface);
  }
  
  
  _lastCountBasesSeen = static_cast<int>(pyramidBases.size());
  _forceLightMusicUpdate = false;
  
  if(_currentPyramidConstructionStage != desiredState){
    _lastTimeConstructionStageChanged_s = currentTime_s;
    _currentPyramidConstructionStage = desiredState;
  }
  
  
  return Result::RESULT_OK;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ActivityBuildPyramid::UpdateChooserPhase(BehaviorExternalInterface& behaviorExternalInterface)
{
  // Check the up-axis of all cubes
  const int countOfBlocksUpright = GetNumberOfBlocksUpright();
  
  // If blocks have been removed since the last update,
  // they shouldn't be counted against the upright count
  if(_lastUprightBlockCount > _pyramidCubePropertiesTrackers.size()){
    _lastUprightBlockCount = static_cast<int>(_pyramidCubePropertiesTrackers.size());
  }
  
  // Check to see if the chooser phase has changed
  if((countOfBlocksUpright >= kMinUprightBlocksForPyramid) ||
     (countOfBlocksUpright == _pyramidCubePropertiesTrackers.size())){
    if(_chooserPhase != ChooserPhase::BuildingPyramid){
      _chooserPhase = ChooserPhase::BuildingPyramid;
      UpdateActiveBehaviorGroup(behaviorExternalInterface, false);
    }
  }else{
    if(_chooserPhase != ChooserPhase::SetupBlocks){
      _chooserPhase = ChooserPhase::SetupBlocks;
      UpdateActiveBehaviorGroup(behaviorExternalInterface, true);
    }
  }
  
  
  /////
  // Logic for when to notify game if the BuildPyramidPreReqs have changed
  /////
  const bool uprightCountChanged = _lastUprightBlockCount != countOfBlocksUpright;
  if(uprightCountChanged){
    NotifyGameOfPyramidPreReqs(behaviorExternalInterface);
  }
  
  _lastUprightBlockCount = countOfBlocksUpright;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
int  ActivityBuildPyramid::GetNumberOfBlocksUpright()
{
  int countOfBlocksUpright = 0;
  for(auto const& entry: _pyramidCubePropertiesTrackers){
    if(entry.second.GetCurrentUpAxis() == UpAxis::ZPositive){
      countOfBlocksUpright++;
    }
  }
  return countOfBlocksUpright;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ActivityBuildPyramid::NotifyGameOfPyramidPreReqs(BehaviorExternalInterface& behaviorExternalInterface)
{
  /**auto robotExternalInterface = behaviorExternalInterface.GetRobotExternalInterface().lock();
  if(robotExternalInterface != nullptr){
    const int countOfBlocksUpright = GetNumberOfBlocksUpright();

    // Collect information about state of world/game
    const bool isHardSpark = IsPyramidHardSpark(behaviorExternalInterface);
    
    // DEPRECATED - Grabbing robot to support current cozmo code, but this should
    // be removed
    const Robot& robot = behaviorExternalInterface.GetRobot();
    
    const bool didUserRequestSparkEnd =
            robot.GetBehaviorManager().DidGameRequestSparkEnd();
    
    const bool minimumUprightCountReached =
            countOfBlocksUpright >= kMinUprightBlocksForPyramid;
    const bool belowMinimumUprightCount =
            (countOfBlocksUpright < kMinUprightBlocksForPyramid);
    
    
    // Combining all of the above conditions into should send determination
    const bool shouldSendPreReqsMet = minimumUprightCountReached &&
                                      isHardSpark &&
                                      !didUserRequestSparkEnd;
    
    const bool shouldSendPreReqsNoLongerMet = belowMinimumUprightCount &&
                                              isHardSpark &&
                                              !didUserRequestSparkEnd &&
                                              !_pyramidObjectiveAchieved;
    if(shouldSendPreReqsMet){
      robotExternalInterface->BroadcastToGame<ExternalInterface::PyramidPreReqState>(true);
    }else if(shouldSendPreReqsNoLongerMet){
      robotExternalInterface->BroadcastToGame<ExternalInterface::PyramidPreReqState>(false);
    }
  }**/
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
PyramidConstructionStage ActivityBuildPyramid::CheckLightAndPyramidConstructionStage(BehaviorExternalInterface& behaviorExternalInterface) const
{
  float currentTime_s = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
  
  // Once we've started to play the success sequence, no going back
  if(_pyramidObjectiveAchieved){
    return PyramidConstructionStage::PyramidCompleteFlourish;
  }
  
  if(behaviorExternalInterface.GetOffTreadsState() != OffTreadsState::OnTreads ||
     _chooserPhase == ChooserPhase::SetupBlocks){
    return PyramidConstructionStage::NoneStage;
  }
  
  // Logic for updating lights/music while building pyramid
  const auto& pyramidBases = behaviorExternalInterface.GetBlockWorld().GetBlockConfigurationManager().GetPyramidBaseCache().GetBases();
  const auto& pyramids = behaviorExternalInterface.GetBlockWorld().GetBlockConfigurationManager().GetPyramidCache().GetPyramids();
  
  if((pyramidBases.size() == 0 && pyramids.size() == 0)){
    // there is a range in which we don't want to cancel lights while placing blocks
    const bool possiblyPlacingBase =
    _currentPyramidConstructionStage == PyramidConstructionStage::InitialCubeCarry &&
    (_behaviorBuildPyramid->IsActivated() || _behaviorBuildPyramidBase->IsActivated()) &&
    (_lastTimeConstructionStageChanged_s + kDelayAccountForPlacing_s > currentTime_s ||
     _lastTimeConstructionStageChanged_s + kDelayAccountForBaseCreation_s < currentTime_s);
    
    // DEPRECATED - Grabbing robot to support current cozmo code, but this should
    // be removed
    const Robot& robot = behaviorExternalInterface.GetRobot();
    
    if(robot.GetCarryingComponent().IsCarryingObject() || possiblyPlacingBase){
      return PyramidConstructionStage::InitialCubeCarry;
    }else{
      return PyramidConstructionStage::SearchingForCube;
    }
  }else{
    // There's a gap between when the top block is "placed" and the final
    // pyramid is recognized - if the behavior is still running and we've
    // just been in a carrying state, don't cut the music/lights suddenly
    const bool behaviorStillPlacingBlock = _behaviorBuildPyramid->IsActivated() &&
    (_currentPyramidConstructionStage == PyramidConstructionStage::TopBlockCarry);
    
    // DEPRECATED - Grabbing robot to support current cozmo code, but this should
    // be removed
    const Robot& robot = behaviorExternalInterface.GetRobot();
    
    if(robot.GetCarryingComponent().IsCarryingObject() || behaviorStillPlacingBlock){
      return PyramidConstructionStage::TopBlockCarry;
    }else{
      return PyramidConstructionStage::BaseFormed;
    }
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ActivityBuildPyramid::UpdateMusic(BehaviorExternalInterface& behaviorExternalInterface, const PyramidConstructionStage& desiredState)
{
  if(behaviorExternalInterface.HasPublicStateBroadcaster()){
    auto& publicStateBroadcaster = behaviorExternalInterface.GetRobotPublicStateBroadcaster();
    if(desiredState > _highestAudioStageReached){
      _highestAudioStageReached = desiredState;
      if(desiredState == PyramidConstructionStage::NoneStage){
        publicStateBroadcaster.UpdateBroadcastBehaviorStage(BehaviorStageTag::PyramidConstruction,
                                                            static_cast<int>(PyramidConstructionStage::SearchingForCube));
      }else{
        publicStateBroadcaster.UpdateBroadcastBehaviorStage(BehaviorStageTag::PyramidConstruction,
                                                            static_cast<int>(desiredState));
      }
    }
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ActivityBuildPyramid::UpdateDesiredLights(BehaviorExternalInterface& behaviorExternalInterface, const PyramidConstructionStage& desiredState)
{
  {
    // DEPRECATED - Grabbing robot to support current cozmo code, but this should
    // be removed
    const Robot& robot = behaviorExternalInterface.GetRobot();
    // If the user canceled out of the spark, we want to clear all
    // pyramid related lights since we may still sit in this activity for a while
    if(robot.GetBehaviorManager().DidGameRequestSparkEnd()){
      for(auto& entry: _pyramidCubePropertiesTrackers){
        entry.second.SetDesiredLightTrigger(CubeAnimationTrigger::Count);
      }
      return;
    }
  }
  
  CubeAnimationTrigger triggerForBase = CubeAnimationTrigger::Count;
  CubeAnimationTrigger triggerForStatic = CubeAnimationTrigger::Count;
  CubeAnimationTrigger triggerForTop = CubeAnimationTrigger::Count;
  
  ObjectLights baseModifier = kEmptyObjectLights;
  ObjectLights staticModifier = kEmptyObjectLights;
  ObjectLights topModifier = kEmptyObjectLights;
  
  bool baseLightsSet = false;
  bool staticLightsSet = false;
  bool topLightsSet = false;
  
  //////
  /// Determine the light triggers/modifiers to set
  //////
  
  switch(desiredState){
    case PyramidConstructionStage::SearchingForCube:
    {
      triggerForTop = CubeAnimationTrigger::Count;
      topLightsSet = true;
      if(!SetPyramidBaseLights(behaviorExternalInterface)){
        triggerForBase = CubeAnimationTrigger::Count;
        triggerForStatic = CubeAnimationTrigger::Count;
        baseLightsSet = true;
        staticLightsSet = true;
      }
      break;
    }
    case PyramidConstructionStage::InitialCubeCarry:
    {
      triggerForBase = CubeAnimationTrigger::PyramidSingle;
      triggerForStatic = CubeAnimationTrigger::PyramidPickup;
      baseLightsSet = true;
      staticLightsSet = true;
      break;
    }
    case PyramidConstructionStage::BaseFormed:
    case PyramidConstructionStage::TopBlockCarry:
    {
      if(desiredState == PyramidConstructionStage::BaseFormed){
        triggerForTop = CubeAnimationTrigger::PyramidSingle;
      }else{
        triggerForTop = CubeAnimationTrigger::PyramidPickup;
      }
      
      topLightsSet = true;
      SetPyramidBaseLights(behaviorExternalInterface);
      break;
    }
    case PyramidConstructionStage::PyramidCompleteFlourish:
    {
      triggerForBase = CubeAnimationTrigger::PyramidFlourish;
      triggerForStatic = CubeAnimationTrigger::PyramidFlourish;
      triggerForTop = CubeAnimationTrigger::PyramidFlourish;
      //baseModifier = GetDenouementBottomLightsModifier();
      //staticModifier = GetDenouementBottomLightsModifier();
      
      baseLightsSet = true;
      staticLightsSet = true;
      topLightsSet = true;
      break;
    }
    case PyramidConstructionStage::NoneStage:
    {
      // Update "onSide" lights based on block current state
      for(auto& entry: _pyramidCubePropertiesTrackers){
        
        if(entry.second.GetCurrentUpAxis() != UpAxis::ZPositive){
          auto animTrigger = GetAppropriateOnSideAnimation(behaviorExternalInterface,
                                                           entry.second.GetObjectID());
          if(entry.second.GetCurrentLightTrigger() != animTrigger){
            entry.second.SetDesiredLightTrigger(animTrigger);
          }
        }else if(entry.second.GetCurrentUpAxis() == UpAxis::ZPositive &&
                 entry.second.GetCurrentLightTrigger() != CubeAnimationTrigger::Count){
          entry.second.SetDesiredLightTrigger(CubeAnimationTrigger::Count);
        }
      }
      break;
    }
  }
  
  
  // Make sure that on side lights are cleared out if any cubes were on their side
  if(_currentPyramidConstructionStage == PyramidConstructionStage::NoneStage &&
     desiredState != PyramidConstructionStage::NoneStage){
    for(auto& entry:_pyramidCubePropertiesTrackers){
      if(IsAnOnSideCubeLight( entry.second.GetCurrentLightTrigger())){
        entry.second.SetDesiredLightTrigger(CubeAnimationTrigger::Count);
      }
    }
  }
  
  
  //////
  /// Set the light triggers/modifiers on the appropriate tracker
  //////
  
  PyramidCubePropertiesTracker* baseBlockTracker = nullptr;
  if(GetCubePropertiesTrackerByAssignment(ActivityBuildPyramid::PyramidAssignment::BaseBlock, baseBlockTracker) &&
     baseBlockTracker != nullptr &&
     baseLightsSet)
  {
    baseBlockTracker->SetDesiredLightTrigger(triggerForBase);
    baseBlockTracker->SetDesiredLightModifier(baseModifier);
  }
  
  PyramidCubePropertiesTracker* staticBlockTracker = nullptr;
  if(GetCubePropertiesTrackerByAssignment(ActivityBuildPyramid::PyramidAssignment::StaticBlock, staticBlockTracker) &&
     staticBlockTracker != nullptr &&
     staticLightsSet)
  {
    staticBlockTracker->SetDesiredLightTrigger(triggerForStatic);
    staticBlockTracker->SetDesiredLightModifier(staticModifier);
  }
  
  PyramidCubePropertiesTracker* topBlockTracker = nullptr;
  if(GetCubePropertiesTrackerByAssignment(ActivityBuildPyramid::PyramidAssignment::TopBlock, topBlockTracker) &&
     topBlockTracker != nullptr &&
     topLightsSet)
  {
    topBlockTracker->SetDesiredLightTrigger(triggerForTop);
    topBlockTracker->SetDesiredLightModifier(topModifier);
  }
}



// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ActivityBuildPyramid::SetCubeLights(BehaviorExternalInterface& behaviorExternalInterface)
{
  // Remove any lights that are currently set
  for(auto& entry: _pyramidCubePropertiesTrackers){
    PyramidCubePropertiesTracker& props = entry.second;
    
    if(props.GetCurrentLightTrigger() != props.GetDesiredLightTrigger() ||
       props.GetDesiredLightModifier() != kEmptyObjectLights)
    {
      const bool shouldSetForOnSide = IsPyramidHardSpark(behaviorExternalInterface) ||
             !IsAnOnSideCubeLight(props.GetDesiredLightTrigger());
      const bool areLightsPlayingAlready = props.GetCurrentLightTrigger() != CubeAnimationTrigger::Count;
      const bool shouldLightsTransition = props.GetDesiredLightTrigger() != CubeAnimationTrigger::Count;
      
      bool lightUpdateSuccessful = false;
      if(shouldLightsTransition && shouldSetForOnSide){
        if(!areLightsPlayingAlready)
        {
          // DEPRECATED - Grabbing robot to support current cozmo code, but this should
          // be removed
          Robot& robot = behaviorExternalInterface.GetRobot();
          
          lightUpdateSuccessful = robot.GetCubeLightComponent().PlayLightAnim(
                                                       props.GetObjectID(),
                                                       props.GetDesiredLightTrigger());
          PRINT_CH_INFO("Behaviors", "ActivityBuildPyramid.SetCubeLights.PlayLights",
                        "%s playing light trigger %s on object %d",
                        lightUpdateSuccessful ? "Succeeded" : "Failed",
                        CubeAnimationTriggerToString(props.GetDesiredLightTrigger()),
                        props.GetObjectID().GetValue());
          
        }
        else
        {
          // DEPRECATED - Grabbing robot to support current cozmo code, but this should
          // be removed
          Robot& robot = behaviorExternalInterface.GetRobot();
          lightUpdateSuccessful = robot.GetCubeLightComponent().StopAndPlayLightAnim(
                                                              props.GetObjectID(),
                                                              props.GetCurrentLightTrigger(),
                                                              props.GetDesiredLightTrigger(),
                                                              nullptr,
                                                              true,
                                                              props.GetDesiredLightModifier());
          PRINT_CH_INFO("Behaviors", "ActivityBuildPyramid.SetCubeLights.StopAndPlayLights",
                        "%s stopping light trigger %s in order to play %s on object %d",
                        lightUpdateSuccessful ? "Succeeded" : "Failed",
                        CubeAnimationTriggerToString(props.GetCurrentLightTrigger()),
                        CubeAnimationTriggerToString(props.GetDesiredLightTrigger()),
                        props.GetObjectID().GetValue());
        }
      }
      else
      {
        // DEPRECATED - Grabbing robot to support current cozmo code, but this should
        // be removed
        Robot& robot = behaviorExternalInterface.GetRobot();
        lightUpdateSuccessful = robot.GetCubeLightComponent().StopLightAnimAndResumePrevious(
                                                                      props.GetCurrentLightTrigger(),
                                                                      props.GetObjectID());
        PRINT_CH_INFO("Behaviors", "ActivityBuildPyramid.SetCubeLights.StoppingLights",
                      "%s stopping light trigger %s on object %d",
                      lightUpdateSuccessful ? "Succeeded" : "Failed",
                      CubeAnimationTriggerToString(props.GetCurrentLightTrigger()),
                      props.GetObjectID().GetValue());
      }
      if(lightUpdateSuccessful){
        props.SetCurrentLightTrigger(props.GetDesiredLightTrigger());
        props.SetDesiredLightModifier(kEmptyObjectLights);
      }
    }
  }
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool ActivityBuildPyramid::IsAnOnSideCubeLight(CubeAnimationTrigger anim)
{
  return (anim == CubeAnimationTrigger::PyramidOnSideLocated) ||
  (anim == CubeAnimationTrigger::PyramidOnSideNotLocated);
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
CubeAnimationTrigger ActivityBuildPyramid::GetAppropriateOnSideAnimation(BehaviorExternalInterface& behaviorExternalInterface, const ObjectID& staticID)
{
  const ObservableObject* obj = behaviorExternalInterface.GetBlockWorld().GetLocatedObjectByID(staticID);
  if(obj != nullptr){
    if(obj->IsPoseStateKnown()){
      return CubeAnimationTrigger::PyramidOnSideLocated;
    }else{
      return CubeAnimationTrigger::PyramidOnSideNotLocated;
    }
  }
  
  return CubeAnimationTrigger::PyramidOnSideNotLocated;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool ActivityBuildPyramid::SetPyramidBaseLights(BehaviorExternalInterface& behaviorExternalInterface)
{
  
  // Clear out any existing base light triggers - they should be re-set below
  // if still valid and the lights won't update on SetCubeLights
  for(auto& cubeProperties: _pyramidCubePropertiesTrackers){
    if(cubeProperties.second.GetCurrentLightTrigger() == CubeAnimationTrigger::PyramidBaseBottom){
      cubeProperties.second.SetDesiredLightTrigger(CubeAnimationTrigger::PyramidBaseBottom);
    }
  }
  
  ObjectID baseBlockID;
  ObjectID staticBlockID;
  const auto& pyramidBases = behaviorExternalInterface.GetBlockWorld().GetBlockConfigurationManager().GetPyramidBaseCache().GetBases();
  const auto& pyramids = behaviorExternalInterface.GetBlockWorld().GetBlockConfigurationManager().GetPyramidCache().GetPyramids();
  
  if(pyramidBases.size() == 1){
    const auto& base = pyramidBases[0];
    baseBlockID = base->GetBaseBlockID();
    staticBlockID = base->GetStaticBlockID();
  }else if(pyramidBases.size() > 1){
    // see if there's an assigment
    PyramidCubePropertiesTracker* baseBlockTracker = nullptr;
    PyramidCubePropertiesTracker* staticBlockTracker = nullptr;
    if(GetCubePropertiesTrackerByAssignment(ActivityBuildPyramid::PyramidAssignment::BaseBlock, baseBlockTracker) &&
       GetCubePropertiesTrackerByAssignment(ActivityBuildPyramid::PyramidAssignment::StaticBlock, staticBlockTracker) &&
       staticBlockTracker != nullptr &&
       baseBlockTracker != nullptr)
    {
      baseBlockID = baseBlockTracker->GetObjectID();
      staticBlockID = staticBlockTracker->GetObjectID();
    }else{
      const auto& base = pyramidBases[0];
      baseBlockID = base->GetBaseBlockID();
      staticBlockID = base->GetStaticBlockID();
    }
  }else if(pyramids.size() > 0){
    const auto& pyramid = pyramids[0];
    baseBlockID = pyramid->GetPyramidBase().GetBaseBlockID();
    staticBlockID = pyramid->GetPyramidBase().GetStaticBlockID();
  }
  
  if(baseBlockID.IsSet() && staticBlockID.IsSet()){
    PyramidCubePropertiesTracker* baseBlockTracker = nullptr;
    PyramidCubePropertiesTracker* staticBlockTracker = nullptr;
    if(GetCubePropertiesTrackerByID(baseBlockID, baseBlockTracker) &&
       GetCubePropertiesTrackerByID(staticBlockID, staticBlockTracker) &&
       staticBlockTracker != nullptr &&
       baseBlockTracker != nullptr)
    {
      if(baseBlockTracker->GetCurrentLightTrigger() != CubeAnimationTrigger::PyramidBaseBottom){
        baseBlockTracker->SetDesiredLightTrigger(CubeAnimationTrigger::PyramidBaseBottom);
        baseBlockTracker->SetDesiredLightModifier(
                                                  GetBaseFormedBaseLightsModifier(behaviorExternalInterface,
                                                                                  staticBlockTracker->GetObjectID(),
                                                                                  baseBlockTracker->GetObjectID()));
      }
      
      if(staticBlockTracker->GetCurrentLightTrigger() != CubeAnimationTrigger::PyramidBaseBottom){
        staticBlockTracker->SetDesiredLightTrigger(CubeAnimationTrigger::PyramidBaseBottom);
        staticBlockTracker->SetDesiredLightModifier(
                                                    GetBaseFormedStaticLightsModifier(behaviorExternalInterface,
                                                                                      staticBlockTracker->GetObjectID(),
                                                                                      baseBlockTracker->GetObjectID()));
      }
      return true;
    }
  }
  
  return false;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
ObjectLights ActivityBuildPyramid::GetBaseFormedBaseLightsModifier(BehaviorExternalInterface& behaviorExternalInterface,
                                                                   const ObjectID& staticID,
                                                                   const ObjectID& baseID) const
{
  ObjectLights baseBlockLights;
  baseBlockLights.makeRelative = MakeRelativeMode::RELATIVE_LED_MODE_BY_SIDE;
  
  const ObservableObject* staticBlock = behaviorExternalInterface.GetBlockWorld().GetLocatedObjectByID(staticID);
  const ObservableObject* baseBlock = behaviorExternalInterface.GetBlockWorld().GetLocatedObjectByID(baseID);
  if(staticBlock == nullptr || baseBlock == nullptr){
    return baseBlockLights;
  }
  
  
  Pose3d baseMidpoint;
  {
    // DEPRECATED - Grabbing robot to support current cozmo code, but this should
    // be removed
    const Robot& robot = behaviorExternalInterface.GetRobot();
    using namespace BlockConfigurations;
    PyramidBase::GetBaseInteriorMidpoint(robot, baseBlock, staticBlock, baseMidpoint);
  }
  
  baseBlockLights.relativePoint = {baseMidpoint.GetTranslation().x(),
    baseMidpoint.GetTranslation().y()};
  baseBlockLights.offset = {{kBaseFormedTimeOn*2,0,kBaseFormedTimeOn*4,kBaseFormedTimeOn*3}};
  
  return baseBlockLights;
  
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
ObjectLights ActivityBuildPyramid::GetBaseFormedStaticLightsModifier(BehaviorExternalInterface& behaviorExternalInterface,
                                                                     const ObjectID& staticID,
                                                                     const ObjectID& baseID) const
{
  ObjectLights staticBlockLights;
  staticBlockLights.makeRelative = MakeRelativeMode::RELATIVE_LED_MODE_BY_SIDE;
  
  const ObservableObject* staticBlock = behaviorExternalInterface.GetBlockWorld().GetLocatedObjectByID(staticID);
  const ObservableObject* baseBlock = behaviorExternalInterface.GetBlockWorld().GetLocatedObjectByID(baseID);
  if(staticBlock == nullptr || baseBlock == nullptr){
    return staticBlockLights;
  }
  
  using namespace BlockConfigurations;
  Pose3d staticMidpoint;
  {
    // DEPRECATED - Grabbing robot to support current cozmo code, but this should
    // be removed
    const Robot& robot = behaviorExternalInterface.GetRobot();
    PyramidBase::GetBaseInteriorMidpoint(robot, staticBlock, baseBlock, staticMidpoint);
  }
  
  staticBlockLights.relativePoint = {staticMidpoint.GetTranslation().x(),
    staticMidpoint.GetTranslation().y()};
  
  return staticBlockLights;
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
ObjectLights ActivityBuildPyramid::GetDenouementBottomLightsModifier() const
{
  ObjectLights kFlourishTopLights;
  kFlourishTopLights.offPeriod_ms ={{
    kPyramidDenouementBaseOff_ms - kPyramidDenouementAdditionalOff_ms,
    kPyramidDenouementBaseOff_ms,
    kPyramidDenouementBaseOff_ms + kPyramidDenouementAdditionalOff_ms,
    kPyramidDenouementBaseOff_ms + kPyramidDenouementAdditionalOff_ms*2
  }};
  
  return kFlourishTopLights;
  
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ActivityBuildPyramid::HandleMessageEvents(BehaviorExternalInterface& behaviorExternalInterface)
{
  
  const auto& stateChangeComp = behaviorExternalInterface.GetStateChangeComponent();
  for(const auto& event: stateChangeComp.GetGameToEngineEvents()){
    if(event.GetData().GetTag() == ExternalInterface::MessageGameToEngineTag::RequestPyramidPreReqState){
      NotifyGameOfPyramidPreReqs(behaviorExternalInterface);
    }
  }
  
  for(const auto& event: stateChangeComp.GetEngineToGameEvents()){
    if(event.GetData().GetTag() == ExternalInterface::MessageEngineToGameTag::ObjectUpAxisChanged){
      
      PyramidCubePropertiesTracker* propertyTracker = nullptr;
      ObjectID objID = event.GetData().Get_ObjectUpAxisChanged().objectID;
      if(!GetCubePropertiesTrackerByID(objID, propertyTracker))
      {
        UpdateStateTrackerForUnrecognizedID(behaviorExternalInterface, objID);
        ANKI_VERIFY(GetCubePropertiesTrackerByID(objID, propertyTracker),
                    "BuildPyramidBehaviorChooser.ObjectNotAddedToTracker.TrackerIsStillNullptr", "");
      }
      if(propertyTracker != nullptr){
        propertyTracker->SetUpAxis(event.GetData().Get_ObjectUpAxisChanged().upAxis);
        _objectAxisChangeIDs.insert(event.GetData().Get_ObjectUpAxisChanged().objectID);
      }
      
    }else if(event.GetData().GetTag() == ExternalInterface::MessageEngineToGameTag::BehaviorObjectiveAchieved){
      
      if(event.GetData().Get_BehaviorObjectiveAchieved().behaviorObjective ==
         BehaviorObjective::BuiltPyramid){
        _pyramidObjectiveAchieved = true;
      }
      
    }else if(event.GetData().GetTag() == ExternalInterface::MessageEngineToGameTag::ObjectConnectionState){
      HandleObjectConnectionStateChange(behaviorExternalInterface,
                                        event.GetData().Get_ObjectConnectionState());
    }
  }
}


} // namespace Cozmo
} // namespace Anki

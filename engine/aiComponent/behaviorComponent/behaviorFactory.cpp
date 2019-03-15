// This file is manually generated using ./tools/ai/generateBehaviorCode.py
// To add a behavior, just create the .cpp and .h files in the correct places
// and re-run ./tools/ai/generateBehavior.py

// This factory creates behaviors from behavior JSONs with a specified
// `behavior_class` where the behavior C++ class name and file name both match
// Behavior{behavior_class}.h/cpp

#include "engine/aiComponent/behaviorComponent/behaviorFactory.h"

#include "engine/aiComponent/behaviorComponent/behaviors/behaviorHighLevelAI.h"
#include "engine/aiComponent/behaviorComponent/behaviors/behaviorWait.h"
#include "engine/aiComponent/behaviorComponent/behaviors/animationWrappers/behaviorAnimGetInLoop.h"
#include "engine/aiComponent/behaviorComponent/behaviors/animationWrappers/behaviorAnimSequence.h"
#include "engine/aiComponent/behaviorComponent/behaviors/animationWrappers/behaviorAnimSequenceWithFace.h"
#include "engine/aiComponent/behaviorComponent/behaviors/animationWrappers/behaviorAnimSequenceWithObject.h"
#include "engine/aiComponent/behaviorComponent/behaviors/basicCubeInteractions/behaviorPickUpCube.h"
#include "engine/aiComponent/behaviorComponent/behaviors/basicCubeInteractions/behaviorPutDownBlock.h"
#include "engine/aiComponent/behaviorComponent/behaviors/basicCubeInteractions/behaviorRollBlock.h"
#include "engine/aiComponent/behaviorComponent/behaviors/basicWorldInteractions/behaviorDriveOffCharger.h"
#include "engine/aiComponent/behaviorComponent/behaviors/basicWorldInteractions/behaviorFindFaces.h"
#include "engine/aiComponent/behaviorComponent/behaviors/basicWorldInteractions/behaviorFindHome.h"
#include "engine/aiComponent/behaviorComponent/behaviors/basicWorldInteractions/behaviorGoHome.h"
#include "engine/aiComponent/behaviorComponent/behaviors/basicWorldInteractions/behaviorInteractWithFaces.h"
#include "engine/aiComponent/behaviorComponent/behaviors/basicWorldInteractions/behaviorLookAround.h"
#include "engine/aiComponent/behaviorComponent/behaviors/basicWorldInteractions/behaviorPopAWheelie.h"
#include "engine/aiComponent/behaviorComponent/behaviors/basicWorldInteractions/behaviorRequestToGoHome.h"
#include "engine/aiComponent/behaviorComponent/behaviors/basicWorldInteractions/behaviorSearchForFace.h"
#include "engine/aiComponent/behaviorComponent/behaviors/basicWorldInteractions/behaviorStackBlocks.h"
#include "engine/aiComponent/behaviorComponent/behaviors/basicWorldInteractions/behaviorTurn.h"
#include "engine/aiComponent/behaviorComponent/behaviors/basicWorldInteractions/behaviorTurnToFace.h"
#include "engine/aiComponent/behaviorComponent/behaviors/coordinators/behaviorCoordinateGlobalInterrupts.h"
#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/behaviorDevDisplayReadingsOnFace.h"
#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/behaviorDevImageCapture.h"
#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/behaviorDevPettingTestSimple.h"
#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/behaviorDevTouchDataCollection.h"
#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/behaviorDevTurnInPlaceTest.h"
#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/behaviorDispatchAfterShake.h"
#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/behaviorDockingTestSimple.h"
#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/behaviorFactoryCentroidExtractor.h"
#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/behaviorLiftLoadTest.h"
#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/playpen/behaviorPlaypenCameraCalibration.h"
#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/playpen/behaviorPlaypenDistanceSensor.h"
#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/playpen/behaviorPlaypenDriftCheck.h"
#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/playpen/behaviorPlaypenDriveForwards.h"
#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/playpen/behaviorPlaypenEndChecks.h"
#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/playpen/behaviorPlaypenInitChecks.h"
#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/playpen/behaviorPlaypenMotorCalibration.h"
#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/playpen/behaviorPlaypenPickupCube.h"
#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/playpen/behaviorPlaypenReadToolCode.h"
#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/playpen/behaviorPlaypenSoundCheck.h"
#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/playpen/behaviorPlaypenTest.h"
#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/playpen/behaviorPlaypenWaitToStart.h"
#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/selfTest/behaviorSelfTest.h"
#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/selfTest/behaviorSelfTestButton.h"
#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/selfTest/behaviorSelfTestDockWithCharger.h"
#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/selfTest/behaviorSelfTestLookAtCharger.h"
#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/selfTest/behaviorSelfTestSoundCheck.h"
#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/selfTest/behaviorSelfTestDriveForwards.h"
#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/selfTest/behaviorSelfTestInitChecks.h"
#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/selfTest/behaviorSelfTestDriftCheck.h"
#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/selfTest/behaviorSelfTestPickup.h"
#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/selfTest/behaviorSelfTestPutOnCharger.h"
#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/selfTest/behaviorSelfTestMotorCalibration.h"
#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/selfTest/behaviorSelfTestScreenAndBackpack.h"
#include "engine/aiComponent/behaviorComponent/behaviors/devBehaviors/selfTest/behaviorSelfTestTouch.h"
#include "engine/aiComponent/behaviorComponent/behaviors/dispatch/behaviorDispatcherQueue.h"
#include "engine/aiComponent/behaviorComponent/behaviors/dispatch/behaviorDispatcherRandom.h"
#include "engine/aiComponent/behaviorComponent/behaviors/dispatch/behaviorDispatcherRerun.h"
#include "engine/aiComponent/behaviorComponent/behaviors/dispatch/behaviorDispatcherScoring.h"
#include "engine/aiComponent/behaviorComponent/behaviors/dispatch/behaviorDispatcherStrictPriority.h"
#include "engine/aiComponent/behaviorComponent/behaviors/dispatch/behaviorDispatcherStrictPriorityWithCooldown.h"
#include "engine/aiComponent/behaviorComponent/behaviors/feeding/behaviorFeedingEat.h"
#include "engine/aiComponent/behaviorComponent/behaviors/freeplay/behaviorDriveToFace.h"
#include "engine/aiComponent/behaviorComponent/behaviors/freeplay/exploration/behaviorExploreBringCubeToBeacon.h"
#include "engine/aiComponent/behaviorComponent/behaviors/freeplay/exploration/behaviorExploreLookAroundInPlace.h"
#include "engine/aiComponent/behaviorComponent/behaviors/freeplay/exploration/behaviorExploreVisitPossibleMarker.h"
#include "engine/aiComponent/behaviorComponent/behaviors/freeplay/exploration/behaviorLookInPlaceMemoryMap.h"
#include "engine/aiComponent/behaviorComponent/behaviors/freeplay/exploration/behaviorThinkAboutBeacons.h"
#include "engine/aiComponent/behaviorComponent/behaviors/freeplay/exploration/behaviorVisitInterestingEdge.h"
#include "engine/aiComponent/behaviorComponent/behaviors/freeplay/oneShots/behaviorDance.h"
#include "engine/aiComponent/behaviorComponent/behaviors/freeplay/oneShots/behaviorSinging.h"
#include "engine/aiComponent/behaviorComponent/behaviors/freeplay/putDownDispatch/behaviorLookForFaceAndCube.h"
#include "engine/aiComponent/behaviorComponent/behaviors/freeplay/userInteractive/behaviorBouncer.h"
#include "engine/aiComponent/behaviorComponent/behaviors/freeplay/userInteractive/behaviorFistBump.h"
#include "engine/aiComponent/behaviorComponent/behaviors/freeplay/userInteractive/behaviorPounceOnMotion.h"
#include "engine/aiComponent/behaviorComponent/behaviors/freeplay/userInteractive/behaviorPounceWithProx.h"
#include "engine/aiComponent/behaviorComponent/behaviors/freeplay/userInteractive/behaviorPuzzleMaze.h"
#include "engine/aiComponent/behaviorComponent/behaviors/freeplay/userInteractive/behaviorTrackLaser.h"
#include "engine/aiComponent/behaviorComponent/behaviors/meetCozmo/behaviorEnrollFace.h"
#include "engine/aiComponent/behaviorComponent/behaviors/meetCozmo/behaviorRespondToRenameFace.h"
#include "engine/aiComponent/behaviorComponent/behaviors/observing/behaviorObservingLookAtFaces.h"
#include "engine/aiComponent/behaviorComponent/behaviors/observing/behaviorObservingOnCharger.h"
#include "engine/aiComponent/behaviorComponent/behaviors/proxBehaviors/behaviorProxGetToDistance.h"
#include "engine/aiComponent/behaviorComponent/behaviors/reactions/behaviorAcknowledgeCubeMoved.h"
#include "engine/aiComponent/behaviorComponent/behaviors/reactions/behaviorAcknowledgeFace.h"
#include "engine/aiComponent/behaviorComponent/behaviors/reactions/behaviorAcknowledgeObject.h"
#include "engine/aiComponent/behaviorComponent/behaviors/reactions/behaviorReactToCliff.h"
#include "engine/aiComponent/behaviorComponent/behaviors/reactions/behaviorReactToFrustration.h"
#include "engine/aiComponent/behaviorComponent/behaviors/reactions/behaviorReactToMotorCalibration.h"
#include "engine/aiComponent/behaviorComponent/behaviors/reactions/behaviorReactToPet.h"
#include "engine/aiComponent/behaviorComponent/behaviors/reactions/behaviorReactToPlacedOnSlope.h"
#include "engine/aiComponent/behaviorComponent/behaviors/reactions/behaviorReactToReturnedToTreads.h"
#include "engine/aiComponent/behaviorComponent/behaviors/reactions/behaviorReactToRobotOnBack.h"
#include "engine/aiComponent/behaviorComponent/behaviors/reactions/behaviorReactToRobotOnFace.h"
#include "engine/aiComponent/behaviorComponent/behaviors/reactions/behaviorReactToRobotOnSide.h"
#include "engine/aiComponent/behaviorComponent/behaviors/reactions/behaviorReactToRobotShaken.h"
#include "engine/aiComponent/behaviorComponent/behaviors/reactions/behaviorReactToUnexpectedMovement.h"
#include "engine/aiComponent/behaviorComponent/behaviors/reactions/behaviorReactToVoiceCommand.h"
#include "engine/aiComponent/behaviorComponent/behaviors/sleeping/behaviorSleeping.h"
#include "engine/aiComponent/behaviorComponent/behaviors/timer/behaviorProceduralClock.h"
#include "engine/aiComponent/behaviorComponent/behaviors/timer/behaviorTimerUtilityCoordinator.h"
#include "engine/aiComponent/behaviorComponent/behaviors/victor/behaviorComeHere.h"
#include "engine/aiComponent/behaviorComponent/behaviors/victor/behaviorReactToSound.h"
#include "engine/aiComponent/behaviorComponent/behaviors/victor/behaviorReactToUnclaimedIntent.h"

#include "clad/types/behaviorComponent/behaviorTypes.h"

namespace Anki {
namespace Cozmo {

ICozmoBehaviorPtr BehaviorFactory::CreateBehavior(const Json::Value& config)
{
  const BehaviorClass behaviorType = ICozmoBehavior::ExtractBehaviorClassFromConfig(config);

  ICozmoBehaviorPtr newBehavior;

  switch (behaviorType)
  {
    case BehaviorClass::HighLevelAI:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorHighLevelAI(config));
      break;
    }

    case BehaviorClass::Wait:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorWait(config));
      break;
    }

    case BehaviorClass::AnimGetInLoop:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorAnimGetInLoop(config));
      break;
    }

    case BehaviorClass::AnimSequence:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorAnimSequence(config));
      break;
    }

    case BehaviorClass::AnimSequenceWithFace:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorAnimSequenceWithFace(config));
      break;
    }

    case BehaviorClass::AnimSequenceWithObject:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorAnimSequenceWithObject(config));
      break;
    }

    case BehaviorClass::PickUpCube:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorPickUpCube(config));
      break;
    }

    case BehaviorClass::PutDownBlock:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorPutDownBlock(config));
      break;
    }

    case BehaviorClass::RollBlock:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorRollBlock(config));
      break;
    }

    case BehaviorClass::DriveOffCharger:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorDriveOffCharger(config));
      break;
    }

    case BehaviorClass::FindFaces:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorFindFaces(config));
      break;
    }

    case BehaviorClass::FindHome:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorFindHome(config));
      break;
    }

    case BehaviorClass::GoHome:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorGoHome(config));
      break;
    }

    case BehaviorClass::InteractWithFaces:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorInteractWithFaces(config));
      break;
    }

    case BehaviorClass::LookAround:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorLookAround(config));
      break;
    }

    case BehaviorClass::PopAWheelie:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorPopAWheelie(config));
      break;
    }

    case BehaviorClass::RequestToGoHome:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorRequestToGoHome(config));
      break;
    }

    case BehaviorClass::SearchForFace:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorSearchForFace(config));
      break;
    }

    case BehaviorClass::StackBlocks:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorStackBlocks(config));
      break;
    }

    case BehaviorClass::Turn:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorTurn(config));
      break;
    }

    case BehaviorClass::TurnToFace:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorTurnToFace(config));
      break;
    }

    case BehaviorClass::CoordinateGlobalInterrupts:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorCoordinateGlobalInterrupts(config));
      break;
    }

    case BehaviorClass::DevDisplayReadingsOnFace:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorDevDisplayReadingsOnFace(config));
      break;
    }

    case BehaviorClass::DevImageCapture:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorDevImageCapture(config));
      break;
    }

    case BehaviorClass::DevPettingTestSimple:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorDevPettingTestSimple(config));
      break;
    }

    case BehaviorClass::DevTouchDataCollection:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorDevTouchDataCollection(config));
      break;
    }

    case BehaviorClass::DevTurnInPlaceTest:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorDevTurnInPlaceTest(config));
      break;
    }

    case BehaviorClass::DispatchAfterShake:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorDispatchAfterShake(config));
      break;
    }

    case BehaviorClass::DockingTestSimple:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorDockingTestSimple(config));
      break;
    }

    case BehaviorClass::FactoryCentroidExtractor:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorFactoryCentroidExtractor(config));
      break;
    }

    case BehaviorClass::LiftLoadTest:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorLiftLoadTest(config));
      break;
    }

    case BehaviorClass::PlaypenCameraCalibration:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorPlaypenCameraCalibration(config));
      break;
    }

    case BehaviorClass::PlaypenDistanceSensor:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorPlaypenDistanceSensor(config));
      break;
    }

    case BehaviorClass::PlaypenDriftCheck:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorPlaypenDriftCheck(config));
      break;
    }

    case BehaviorClass::PlaypenDriveForwards:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorPlaypenDriveForwards(config));
      break;
    }

    case BehaviorClass::PlaypenEndChecks:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorPlaypenEndChecks(config));
      break;
    }

    case BehaviorClass::PlaypenInitChecks:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorPlaypenInitChecks(config));
      break;
    }

    case BehaviorClass::PlaypenMotorCalibration:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorPlaypenMotorCalibration(config));
      break;
    }

    case BehaviorClass::PlaypenPickupCube:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorPlaypenPickupCube(config));
      break;
    }

    case BehaviorClass::PlaypenReadToolCode:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorPlaypenReadToolCode(config));
      break;
    }

    case BehaviorClass::PlaypenSoundCheck:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorPlaypenSoundCheck(config));
      break;
    }

    case BehaviorClass::PlaypenTest:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorPlaypenTest(config));
      break;
    }

    case BehaviorClass::PlaypenWaitToStart:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorPlaypenWaitToStart(config));
      break;
    }

    case BehaviorClass::DispatcherQueue:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorDispatcherQueue(config));
      break;
    }

    case BehaviorClass::DispatcherRandom:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorDispatcherRandom(config));
      break;
    }

    case BehaviorClass::DispatcherRerun:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorDispatcherRerun(config));
      break;
    }

    case BehaviorClass::DispatcherScoring:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorDispatcherScoring(config));
      break;
    }

    case BehaviorClass::DispatcherStrictPriority:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorDispatcherStrictPriority(config));
      break;
    }

    case BehaviorClass::DispatcherStrictPriorityWithCooldown:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorDispatcherStrictPriorityWithCooldown(config));
      break;
    }

    case BehaviorClass::FeedingEat:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorFeedingEat(config));
      break;
    }

    case BehaviorClass::DriveToFace:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorDriveToFace(config));
      break;
    }

    case BehaviorClass::ExploreBringCubeToBeacon:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorExploreBringCubeToBeacon(config));
      break;
    }

    case BehaviorClass::ExploreLookAroundInPlace:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorExploreLookAroundInPlace(config));
      break;
    }

    case BehaviorClass::ExploreVisitPossibleMarker:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorExploreVisitPossibleMarker(config));
      break;
    }

    case BehaviorClass::LookInPlaceMemoryMap:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorLookInPlaceMemoryMap(config));
      break;
    }

    case BehaviorClass::ThinkAboutBeacons:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorThinkAboutBeacons(config));
      break;
    }

    case BehaviorClass::VisitInterestingEdge:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorVisitInterestingEdge(config));
      break;
    }

    case BehaviorClass::Dance:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorDance(config));
      break;
    }

    case BehaviorClass::Singing:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorSinging(config));
      break;
    }

    case BehaviorClass::LookForFaceAndCube:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorLookForFaceAndCube(config));
      break;
    }

    case BehaviorClass::Bouncer:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorBouncer(config));
      break;
    }

    case BehaviorClass::FistBump:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorFistBump(config));
      break;
    }

    case BehaviorClass::PounceOnMotion:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorPounceOnMotion(config));
      break;
    }

    case BehaviorClass::PounceWithProx:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorPounceWithProx(config));
      break;
    }

    case BehaviorClass::PuzzleMaze:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorPuzzleMaze(config));
      break;
    }

    case BehaviorClass::TrackLaser:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorTrackLaser(config));
      break;
    }

    case BehaviorClass::EnrollFace:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorEnrollFace(config));
      break;
    }

    case BehaviorClass::RespondToRenameFace:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorRespondToRenameFace(config));
      break;
    }

    case BehaviorClass::ObservingLookAtFaces:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorObservingLookAtFaces(config));
      break;
    }

    case BehaviorClass::ObservingOnCharger:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorObservingOnCharger(config));
      break;
    }

    case BehaviorClass::ProxGetToDistance:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorProxGetToDistance(config));
      break;
    }

    case BehaviorClass::AcknowledgeCubeMoved:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorAcknowledgeCubeMoved(config));
      break;
    }

    case BehaviorClass::AcknowledgeFace:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorAcknowledgeFace(config));
      break;
    }

    case BehaviorClass::AcknowledgeObject:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorAcknowledgeObject(config));
      break;
    }

    case BehaviorClass::ReactToCliff:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorReactToCliff(config));
      break;
    }

    case BehaviorClass::ReactToFrustration:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorReactToFrustration(config));
      break;
    }

    case BehaviorClass::ReactToMotorCalibration:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorReactToMotorCalibration(config));
      break;
    }

    case BehaviorClass::ReactToPet:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorReactToPet(config));
      break;
    }

    case BehaviorClass::ReactToPlacedOnSlope:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorReactToPlacedOnSlope(config));
      break;
    }

    case BehaviorClass::ReactToReturnedToTreads:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorReactToReturnedToTreads(config));
      break;
    }

    case BehaviorClass::ReactToRobotOnBack:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorReactToRobotOnBack(config));
      break;
    }

    case BehaviorClass::ReactToRobotOnFace:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorReactToRobotOnFace(config));
      break;
    }

    case BehaviorClass::ReactToRobotOnSide:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorReactToRobotOnSide(config));
      break;
    }

    case BehaviorClass::ReactToRobotShaken:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorReactToRobotShaken(config));
      break;
    }

    case BehaviorClass::ReactToUnexpectedMovement:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorReactToUnexpectedMovement(config));
      break;
    }

    case BehaviorClass::ReactToVoiceCommand:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorReactToVoiceCommand(config));
      break;
    }

    case BehaviorClass::Sleeping:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorSleeping(config));
      break;
    }

    case BehaviorClass::ProceduralClock:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorProceduralClock(config));
      break;
    }

    case BehaviorClass::TimerUtilityCoordinator:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorTimerUtilityCoordinator(config));
      break;
    }

    case BehaviorClass::ComeHere:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorComeHere(config));
      break;
    }

    case BehaviorClass::ReactToSound:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorReactToSound(config));
      break;
    }

    case BehaviorClass::ReactToUnclaimedIntent:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorReactToUnclaimedIntent(config));
      break;
    }

    case BehaviorClass::SelfTest:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorSelfTest(config));
      break;
    }

    case BehaviorClass::SelfTestButton:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorSelfTestButton(config));
      break;
    }

    case BehaviorClass::SelfTestTouch:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorSelfTestTouch(config));
      break;
    }

    case BehaviorClass::SelfTestPutOnCharger:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorSelfTestPutOnCharger(config));
      break;
    }

    case BehaviorClass::SelfTestScreenAndBackpack:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorSelfTestScreenAndBackpack(config));
      break;
    }

    case BehaviorClass::SelfTestInitChecks:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorSelfTestInitChecks(config));
      break;
    }

    case BehaviorClass::SelfTestMotorCalibration:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorSelfTestMotorCalibration(config));
      break;
    }

    case BehaviorClass::SelfTestDriftCheck:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorSelfTestDriftCheck(config));
      break;
    }

    case BehaviorClass::SelfTestSoundCheck:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorSelfTestSoundCheck(config));
      break;
    }

    case BehaviorClass::SelfTestDriveForwards:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorSelfTestDriveForwards(config));
      break;
    }

    case BehaviorClass::SelfTestLookAtCharger:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorSelfTestLookAtCharger(config));
      break;
    }

    case BehaviorClass::SelfTestDockWithCharger:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorSelfTestDockWithCharger(config));
      break;
    }

    case BehaviorClass::SelfTestPickup:
    {
      newBehavior = ICozmoBehaviorPtr(new BehaviorSelfTestPickup(config));
      break;
    }
  }

  return newBehavior;
}

}
}

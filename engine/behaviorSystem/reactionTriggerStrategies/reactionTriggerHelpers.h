/**
 * File: reactionTriggerHelpers.h
 *
 * Author: Kevin M. Karol
 * Created: 2/28/17
 *
 * Description: Helpers for translating between ReactionTrigger Structs and 
 * enumerated arrays.  Also contains convenience array constants
 *
 * Copyright: Anki, Inc. 2017
 *
 **/

#ifndef __Cozmo_Basestation_BehaviorSystem_ReactionTriggerStrategies_ReactionTriggerHelpers_H__
#define __Cozmo_Basestation_BehaviorSystem_ReactionTriggerStrategies_ReactionTriggerHelpers_H__

#define ALL_TRIGGERS_CONSIDERED_TO_FULL_ARRAY(triggers) { \
  {ReactionTrigger::CliffDetected,                triggers.cliffDetected}, \
  {ReactionTrigger::CubeMoved,                    triggers.cubeMoved}, \
  {ReactionTrigger::FacePositionUpdated,          triggers.facePositionUpdated}, \
  {ReactionTrigger::FistBump,                     triggers.fistBump}, \
  {ReactionTrigger::Frustration,                  triggers.frustration}, \
  {ReactionTrigger::Hiccup,                       triggers.hiccup}, \
  {ReactionTrigger::MotorCalibration,             triggers.motorCalibration}, \
  {ReactionTrigger::NoPreDockPoses,               triggers.noPreDockPoses}, \
  {ReactionTrigger::ObjectPositionUpdated,        triggers.objectPositionUpdated}, \
  {ReactionTrigger::PlacedOnCharger,              triggers.placedOnCharger}, \
  {ReactionTrigger::PetInitialDetection,          triggers.petInitialDetection}, \
  {ReactionTrigger::RobotFalling,                 triggers.robotFalling},  \
  {ReactionTrigger::RobotPickedUp,                triggers.robotPickedUp},  \
  {ReactionTrigger::RobotPlacedOnSlope,           triggers.robotPlacedOnSlope},  \
  {ReactionTrigger::ReturnedToTreads,             triggers.returnedToTreads},  \
  {ReactionTrigger::RobotOnBack,                  triggers.robotOnBack},  \
  {ReactionTrigger::RobotOnFace,                  triggers.robotOnFace},  \
  {ReactionTrigger::RobotOnSide,                  triggers.robotOnSide},  \
  {ReactionTrigger::RobotShaken,                  triggers.robotShaken},  \
  {ReactionTrigger::Sparked,                      triggers.sparked},  \
  {ReactionTrigger::UnexpectedMovement,           triggers.unexpectedMovement},  \
  {ReactionTrigger::VC,                           triggers.vc},  \
}


#include "clad/types/behaviorSystem/reactionTriggers.h"

#include "util/helpers/fullEnumToValueArrayChecker.h"
#include "util/logging/logging.h"
#include "util/math/numericCast.h"


namespace Anki {
namespace Cozmo {
  

  
namespace ReactionTriggerHelpers {
  
using FullReactionArray = Util::FullEnumToValueArrayChecker::FullEnumToValueArray<ReactionTrigger, bool, ReactionTrigger::Count>;
using Util::FullEnumToValueArrayChecker::IsSequentialArray; // import IsSequentialArray to this namespace
  
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
static inline bool IsTriggerAffected(ReactionTrigger reactionTrigger,
                                     const FullReactionArray& reactions)
{
  return reactions[Util::EnumToUnderlying(reactionTrigger)].Value();
}


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
static void EnsureFullReactionArrayConversionsValid(const FullReactionArray& reactions, const AllTriggersConsidered& triggersAffected)
{
  DEV_ASSERT(IsSequentialArray(reactions),
             "ReactionTriggerHelpers.ConvertReactionArray.InitialArrayNotSequential");
  FullReactionArray checkCommutativeArray = ALL_TRIGGERS_CONSIDERED_TO_FULL_ARRAY(triggersAffected);
  DEV_ASSERT(IsSequentialArray(checkCommutativeArray),
             "ReactionTriggerHelpers.ConvertReactionArray.NotCommutative");
  // Make sure that no values got swapped around in the conversion process
  
  for(int index = 0; index < Util::EnumToUnderlying(ReactionTrigger::Count); index++){
    auto& reactionEntry = reactions[index];
    auto& commutativeEntry = checkCommutativeArray[index];
    DEV_ASSERT(reactionEntry.EnumValue() == commutativeEntry.EnumValue(),
               "EnsureFullReactionArrayConversionsValid.ConversionEnumValueMismatch");
    DEV_ASSERT(reactionEntry.Value() == commutativeEntry.Value(),
               "EnsureFullReactionArrayConversionsValid.ConversionValueMismatch");
    switch(reactionEntry.EnumValue()){
      case ReactionTrigger::CliffDetected:
      {
        DEV_ASSERT((triggersAffected.cliffDetected == reactionEntry.Value()) &&
                   (triggersAffected.cliffDetected == commutativeEntry.Value()),
                   "EnsureFullReactionArrayConversionsValid.CliffDetectedIssue");
        break;
      }
      case ReactionTrigger::CubeMoved:
      {
        DEV_ASSERT((triggersAffected.cubeMoved == reactionEntry.Value()) &&
                   (triggersAffected.cubeMoved == commutativeEntry.Value()),
                   "EnsureFullReactionArrayConversionsValid.CubeMoved");
        break;
      }
      case ReactionTrigger::FacePositionUpdated:
      {
        DEV_ASSERT((triggersAffected.facePositionUpdated == reactionEntry.Value()) &&
                   (triggersAffected.facePositionUpdated == commutativeEntry.Value()),
                   "EnsureFullReactionArrayConversionsValid.FacePositionUpdated");
        break;
      }
      case ReactionTrigger::FistBump:
      {
        DEV_ASSERT((triggersAffected.fistBump == reactionEntry.Value()) &&
                   (triggersAffected.fistBump == commutativeEntry.Value()),
                   "EnsureFullReactionArrayConversionsValid.FistBump");
        break;
      }
      case ReactionTrigger::Frustration:
      {
        DEV_ASSERT((triggersAffected.frustration == reactionEntry.Value()) &&
                   (triggersAffected.frustration == commutativeEntry.Value()),
                   "EnsureFullReactionArrayConversionsValid.Frustration");
        break;
      }
      case ReactionTrigger::Hiccup:
      {
        DEV_ASSERT((triggersAffected.hiccup == reactionEntry.Value()) &&
                   (triggersAffected.hiccup == commutativeEntry.Value()),
                   "EnsureFullReactionArrayConversionsValid.Hiccup");
        break;
      }
      case ReactionTrigger::MotorCalibration:
      {
        DEV_ASSERT((triggersAffected.motorCalibration == reactionEntry.Value()) &&
                   (triggersAffected.motorCalibration == commutativeEntry.Value()),
                   "EnsureFullReactionArrayConversionsValid.MotorCalibration");
        break;
      }
      case ReactionTrigger::NoPreDockPoses:
      {
        DEV_ASSERT((triggersAffected.noPreDockPoses == reactionEntry.Value()) &&
                   (triggersAffected.noPreDockPoses == commutativeEntry.Value()),
                   "EnsureFullReactionArrayConversionsValid.NoPreDockPoses");
        break;
      }
      case ReactionTrigger::ObjectPositionUpdated:
      {
        DEV_ASSERT((triggersAffected.objectPositionUpdated == reactionEntry.Value()) &&
                   (triggersAffected.objectPositionUpdated == commutativeEntry.Value()),
                   "EnsureFullReactionArrayConversionsValid.ObjectPositionUpdated");
        break;
      }
      case ReactionTrigger::PlacedOnCharger:
      {
        DEV_ASSERT((triggersAffected.placedOnCharger == reactionEntry.Value()) &&
                   (triggersAffected.placedOnCharger == commutativeEntry.Value()),
                   "EnsureFullReactionArrayConversionsValid.PlacedOnCharger");
        break;
      }
      case ReactionTrigger::PetInitialDetection:
      {
        DEV_ASSERT((triggersAffected.petInitialDetection == reactionEntry.Value()) &&
                   (triggersAffected.petInitialDetection == commutativeEntry.Value()),
                   "EnsureFullReactionArrayConversionsValid.PetInitialDetection");
        break;
      }
      case ReactionTrigger::RobotFalling:
      {
        DEV_ASSERT((triggersAffected.robotFalling == reactionEntry.Value()) &&
                   (triggersAffected.robotFalling == commutativeEntry.Value()),
                   "EnsureFullReactionArrayConversionsValid.RobotFalling");
        break;
      }
      case ReactionTrigger::RobotPickedUp:
      {
        DEV_ASSERT((triggersAffected.robotPickedUp == reactionEntry.Value()) &&
                   (triggersAffected.robotPickedUp == commutativeEntry.Value()),
                   "EnsureFullReactionArrayConversionsValid.RobotPickedUp");
        break;
      }
      case ReactionTrigger::RobotPlacedOnSlope:
      {
        DEV_ASSERT((triggersAffected.robotPlacedOnSlope == reactionEntry.Value()) &&
                   (triggersAffected.robotPlacedOnSlope == commutativeEntry.Value()),
                   "EnsureFullReactionArrayConversionsValid.RobotPlacedOnSlope");
        break;
      }
      case ReactionTrigger::ReturnedToTreads:
      {
        DEV_ASSERT((triggersAffected.returnedToTreads == reactionEntry.Value()) &&
                   (triggersAffected.returnedToTreads == commutativeEntry.Value()),
                   "EnsureFullReactionArrayConversionsValid.ReturnedToTreads");
        break;
      }
      case ReactionTrigger::RobotOnBack:
      {
        DEV_ASSERT((triggersAffected.robotOnBack == reactionEntry.Value()) &&
                   (triggersAffected.robotOnBack == commutativeEntry.Value()),
                   "EnsureFullReactionArrayConversionsValid.RobotOnBack");
        break;
      }
      case ReactionTrigger::RobotOnFace:
      {
        DEV_ASSERT((triggersAffected.robotOnFace == reactionEntry.Value()) &&
                   (triggersAffected.robotOnFace == commutativeEntry.Value()),
                   "EnsureFullReactionArrayConversionsValid.RobotOnFace");
        break;
      }
      case ReactionTrigger::RobotOnSide:
      {
        DEV_ASSERT((triggersAffected.robotOnSide == reactionEntry.Value()) &&
                   (triggersAffected.robotOnSide == commutativeEntry.Value()),
                   "EnsureFullReactionArrayConversionsValid.RobotOnSide");
        break;
      }
      case ReactionTrigger::RobotShaken:
      {
        DEV_ASSERT((triggersAffected.robotShaken == reactionEntry.Value()) &&
                   (triggersAffected.robotShaken == commutativeEntry.Value()),
                   "EnsureFullReactionArrayConversionsValid.RobotShaken");
        break;
      }
      case ReactionTrigger::Sparked:
      {
        DEV_ASSERT((triggersAffected.sparked == reactionEntry.Value()) &&
                   (triggersAffected.sparked == commutativeEntry.Value()),
                   "EnsureFullReactionArrayConversionsValid.Sparked");
        break;
      }
      case ReactionTrigger::UnexpectedMovement:
      {
        DEV_ASSERT((triggersAffected.unexpectedMovement == reactionEntry.Value()) &&
                   (triggersAffected.unexpectedMovement == commutativeEntry.Value()),
                   "EnsureFullReactionArrayConversionsValid.UnexpectedMovement");
        break;
      }
      case ReactionTrigger::VC:
      {
        DEV_ASSERT((triggersAffected.vc == reactionEntry.Value()) &&
                   (triggersAffected.vc == commutativeEntry.Value()),
                   "EnsureFullReactionArrayConversionsValid.VoiceCommand");
        break;
      }
      case ReactionTrigger::Count:
      case ReactionTrigger::NoneTrigger:
      {
        DEV_ASSERT(false, "EnsureFullReactionArrayConversionsValid.InvalidTrigger");
        break;
      }
    }

  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Convenience function for parts of engine that want to use the AllTriggersConsidered struct so values can be assignable
// since the FullEnumArray is non-assignable
static AllTriggersConsidered ConvertReactionArrayToAllTriggersConsidered(const FullReactionArray& reactions)
{
  AllTriggersConsidered affected = AllTriggersConsidered(
       reactions[Util::EnumToUnderlying(ReactionTrigger::CliffDetected)].Value(),
       reactions[Util::EnumToUnderlying(ReactionTrigger::CubeMoved)].Value(),
       reactions[Util::EnumToUnderlying(ReactionTrigger::FacePositionUpdated)].Value(),
       reactions[Util::EnumToUnderlying(ReactionTrigger::FistBump)].Value(),
       reactions[Util::EnumToUnderlying(ReactionTrigger::Frustration)].Value(),
       reactions[Util::EnumToUnderlying(ReactionTrigger::Hiccup)].Value(),
       reactions[Util::EnumToUnderlying(ReactionTrigger::MotorCalibration)].Value(),
       reactions[Util::EnumToUnderlying(ReactionTrigger::NoPreDockPoses)].Value(),
       reactions[Util::EnumToUnderlying(ReactionTrigger::ObjectPositionUpdated)].Value(),
       reactions[Util::EnumToUnderlying(ReactionTrigger::PlacedOnCharger)].Value(),
       reactions[Util::EnumToUnderlying(ReactionTrigger::PetInitialDetection)].Value(),
       reactions[Util::EnumToUnderlying(ReactionTrigger::RobotFalling)].Value(),
       reactions[Util::EnumToUnderlying(ReactionTrigger::RobotPickedUp)].Value(),
       reactions[Util::EnumToUnderlying(ReactionTrigger::RobotPlacedOnSlope)].Value(),
       reactions[Util::EnumToUnderlying(ReactionTrigger::ReturnedToTreads)].Value(),
       reactions[Util::EnumToUnderlying(ReactionTrigger::RobotOnBack)].Value(),
       reactions[Util::EnumToUnderlying(ReactionTrigger::RobotOnFace)].Value(),
       reactions[Util::EnumToUnderlying(ReactionTrigger::RobotOnSide)].Value(),
       reactions[Util::EnumToUnderlying(ReactionTrigger::RobotShaken)].Value(),
       reactions[Util::EnumToUnderlying(ReactionTrigger::Sparked)].Value(),
       reactions[Util::EnumToUnderlying(ReactionTrigger::UnexpectedMovement)].Value(),
       reactions[Util::EnumToUnderlying(ReactionTrigger::VC)].Value());
  if(ANKI_DEV_CHEATS){
    EnsureFullReactionArrayConversionsValid(reactions, affected);
  }
  
  return affected;
}

const ReactionTriggerHelpers::FullReactionArray& GetAffectAllArray();

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

  
static const AllTriggersConsidered kAffectAllReactions =
         ConvertReactionArrayToAllTriggersConsidered(GetAffectAllArray());
  
} // namespace ReactionTriggerHelpers
} // namespace Cozmo
} // namespace Anki

#endif // __Cozmo_Basestation_BehaviorSystem_ReactionTriggerStrategies_ReactionTriggerHelpers_H__

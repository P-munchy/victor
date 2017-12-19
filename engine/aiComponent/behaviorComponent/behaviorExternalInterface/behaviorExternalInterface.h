/**
* File: behaviorExternalInterface.h
*
* Author: Kevin M. Karol
* Created: 08/30/17
*
* Description: Interface that behaviors use to interact with the rest of
* the Cozmo system
*
* Copyright: Anki, Inc. 2017
*
**/

#ifndef __Cozmo_Basestation_BehaviorSystem_BehaviorExternalInterface_H__
#define __Cozmo_Basestation_BehaviorSystem_BehaviorExternalInterface_H__


#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/behaviorEventComponent.h"
#include "engine/entity.h"

#include "clad/types/offTreadsStates.h"

#include "util/helpers/noncopyable.h"
#include "util/logging/logging.h"
#include "util/random/randomGenerator.h"

#include <memory>
#include <unordered_map>

namespace Anki {
namespace Cozmo {

// Forward Declaration
class AIComponent;
class AnimationComponent;
class BehaviorContainer;
class BehaviorEventComponent;
class BEIRobotInfo;
class BlockWorld;
class BodyLightComponent;
class CubeAccelComponent;
class CubeLightComponent;
class DelegationComponent;
class FaceWorld;
class ICozmoBehavior;
class IExternalInterface;
class NeedsManager;
class MapComponent;
class MicDirectionHistory;
class MoodManager;
class ObjectPoseConfirmer;
class PetWorld;
class ProgressionUnlockComponent;
class ProxSensorComponent;
class PublicStateBroadcaster;
class TouchSensorComponent;
class VisionComponent;
  
namespace Audio {
class EngineRobotAudioClient;
}


class BEIComponentAccessGuard{
};

enum class BEIComponentID{
  AIComponent,
  Animation,
  BehaviorContainer,
  BehaviorEvent,
  BlockWorld,
  BodyLightComponent,
  CubeAccel,
  CubeLight,
  Delegation,
  FaceWorld,
  Map,
  MicDirectionHistory,
  MoodManager,
  NeedsManager,
  ObjectPoseConfirmer,
  PetWorld,
  ProgressionUnlock,
  ProxSensor,
  PublicStateBroadcaster,
  RobotAudioClient,
  RobotInfo,
  TouchSensor,
  Vision,

  Count
};

class BEIComponentWrapper : public ComponentWrapper {
  public:
    template<typename T>
    BEIComponentWrapper(T* component)
    : ComponentWrapper(component){}

    // Maintain a reference to the access guard in order to strip the component out of BEI
    // when the access guard falls out of scope the component will be added back into BEI
    // automatically
    std::shared_ptr<BEIComponentAccessGuard> StripComponent() const {return _accessGuard;}

    virtual bool IsValueValidInternal() const override {return _accessGuard.use_count() == 1;}

  private:
    std::shared_ptr<BEIComponentAccessGuard> _accessGuard = std::make_shared<BEIComponentAccessGuard>();
};

class BehaviorExternalInterface : private Util::noncopyable{
public:
  BehaviorExternalInterface(){}
  
  void Init(AIComponent*                   aiComponent,
            AnimationComponent*            animationComponent,
            BehaviorContainer*             behaviorContainer,
            BehaviorEventComponent*        behaviorEventComponent,
            BlockWorld*                    blockWorld,
            BodyLightComponent*            bodyLightComponent,
            CubeAccelComponent*            cubeAccelComponent,
            CubeLightComponent*            cubeLightComponent,
            DelegationComponent*           delegationComponent,
            FaceWorld*                     faceWorld,
            MapComponent*                  mapComponent,
            MicDirectionHistory*           micDirectionHistory,
            MoodManager*                   moodManager,
            NeedsManager*                  needsManager,
            ObjectPoseConfirmer*           objectPoseConfirmer,
            PetWorld*                      petWorld,
            ProgressionUnlockComponent*    progressionUnlockComponent,
            ProxSensorComponent*           proxSensor,
            PublicStateBroadcaster*        publicStateBroadcaster,
            Audio::EngineRobotAudioClient* robotAudioClient,
            BEIRobotInfo*                  robotInfo,
            TouchSensorComponent*          touchSensorComponent,
            VisionComponent*               visionComponent);
    
  virtual ~BehaviorExternalInterface();

  const BEIComponentWrapper& GetComponentWrapper(BEIComponentID componentID) const;
  
  // Access components which the BehaviorSystem can count on will always exist
  // when making decisions
  AIComponent&             GetAIComponent()               const { return GetComponentWrapper(BEIComponentID::AIComponent).GetValue<AIComponent>();}
  const FaceWorld&         GetFaceWorld()                 const { return GetComponentWrapper(BEIComponentID::FaceWorld).GetValue<FaceWorld>();}
  FaceWorld&               GetFaceWorldMutable()                { return GetComponentWrapper(BEIComponentID::FaceWorld).GetValue<FaceWorld>();}
  const PetWorld&          GetPetWorld()                  const { return GetComponentWrapper(BEIComponentID::PetWorld).GetValue<PetWorld>();}
  const BlockWorld&        GetBlockWorld()                const { return GetComponentWrapper(BEIComponentID::BlockWorld).GetValue<BlockWorld>();}
  BlockWorld&              GetBlockWorld()                      { return GetComponentWrapper(BEIComponentID::BlockWorld).GetValue<BlockWorld>();}
  const BehaviorContainer& GetBehaviorContainer()         const { return GetComponentWrapper(BEIComponentID::BehaviorContainer).GetValue<BehaviorContainer>();}
  BehaviorEventComponent&  GetStateChangeComponent()      const { return GetComponentWrapper(BEIComponentID::BehaviorEvent).GetValue<BehaviorEventComponent>();}

  // Give behaviors/activities access to information about robot
  BEIRobotInfo& GetRobotInfo() { return GetComponentWrapper(BEIComponentID::RobotInfo).GetValue<BEIRobotInfo>();}
  const BEIRobotInfo& GetRobotInfo() const { return GetComponentWrapper(BEIComponentID::RobotInfo).GetValue<BEIRobotInfo>();}

  // Access components which may or may not exist - you must call
  // has before get or you may hit a nullptr assert
  inline bool HasDelegationComponent() const { return GetComponentWrapper(BEIComponentID::Delegation).IsValueValid();}
  inline DelegationComponent& GetDelegationComponent() const  { return GetComponentWrapper(BEIComponentID::Delegation).GetValue<DelegationComponent>();}
  
  inline bool HasPublicStateBroadcaster() const { return GetComponentWrapper(BEIComponentID::PublicStateBroadcaster).IsValueValid();}
  PublicStateBroadcaster& GetRobotPublicStateBroadcaster() const { return GetComponentWrapper(BEIComponentID::PublicStateBroadcaster).GetValue<PublicStateBroadcaster>();}
  
  inline bool HasProgressionUnlockComponent() const { return GetComponentWrapper(BEIComponentID::ProgressionUnlock).IsValueValid();}
  ProgressionUnlockComponent& GetProgressionUnlockComponent() const {return GetComponentWrapper(BEIComponentID::ProgressionUnlock).GetValue<ProgressionUnlockComponent>();}
  
  inline bool HasMoodManager() const { return GetComponentWrapper(BEIComponentID::MoodManager).IsValueValid();}
  MoodManager& GetMoodManager() const{ return GetComponentWrapper(BEIComponentID::MoodManager).GetValue<MoodManager>();}
  
  inline bool HasNeedsManager() const { return GetComponentWrapper(BEIComponentID::NeedsManager).IsValueValid();}
  NeedsManager& GetNeedsManager() const { return GetComponentWrapper(BEIComponentID::NeedsManager).GetValue<NeedsManager>();}

  inline bool HasTouchSensorComponent() const { return GetComponentWrapper(BEIComponentID::TouchSensor).IsValueValid();}
  TouchSensorComponent& GetTouchSensorComponent() const { return GetComponentWrapper(BEIComponentID::TouchSensor).GetValue<TouchSensorComponent>();}

  inline bool HasVisionComponent() const { return GetComponentWrapper(BEIComponentID::Vision).IsValueValid();}
  VisionComponent& GetVisionComponent() const { return GetComponentWrapper(BEIComponentID::Vision).GetValue<VisionComponent>();}

  inline bool HasMapComponent() const { return GetComponentWrapper(BEIComponentID::Map).IsValueValid();}
  MapComponent& GetMapComponent() const { return GetComponentWrapper(BEIComponentID::Map).GetValue<MapComponent>();}

  inline bool HasCubeLightComponent() const { return GetComponentWrapper(BEIComponentID::CubeLight).IsValueValid();}
  CubeLightComponent& GetCubeLightComponent() const { return GetComponentWrapper(BEIComponentID::CubeLight).GetValue<CubeLightComponent>();}

  inline bool HasObjectPoseConfirmer() const { return GetComponentWrapper(BEIComponentID::ObjectPoseConfirmer).IsValueValid();}
  ObjectPoseConfirmer& GetObjectPoseConfirmer() const { return GetComponentWrapper(BEIComponentID::ObjectPoseConfirmer).GetValue<ObjectPoseConfirmer>();}

  inline bool HasCubeAccelComponent() const { return GetComponentWrapper(BEIComponentID::CubeAccel).IsValueValid();}
  CubeAccelComponent& GetCubeAccelComponent() const { return GetComponentWrapper(BEIComponentID::CubeAccel).GetValue<CubeAccelComponent>();}

  inline bool HasAnimationComponent() const { return GetComponentWrapper(BEIComponentID::Animation).IsValueValid();}
  AnimationComponent& GetAnimationComponent() const { return GetComponentWrapper(BEIComponentID::Animation).GetValue<AnimationComponent>();}

  inline bool HasRobotAudioClient() const { return GetComponentWrapper(BEIComponentID::RobotAudioClient).IsValueValid();}
  Audio::EngineRobotAudioClient& GetRobotAudioClient() const { return GetComponentWrapper(BEIComponentID::RobotAudioClient).GetValue<Audio::EngineRobotAudioClient>();}
  
  inline bool HasBodyLightComponent() const { return GetComponentWrapper(BEIComponentID::BodyLightComponent).IsValueValid();}
  BodyLightComponent& GetBodyLightComponent() const { return GetComponentWrapper(BEIComponentID::BodyLightComponent).GetValue<BodyLightComponent>();}

  inline bool HasMicDirectionHistory() const { return GetComponentWrapper(BEIComponentID::MicDirectionHistory).IsValueValid();}
  const MicDirectionHistory& GetMicDirectionHistory() const {return GetComponentWrapper(BEIComponentID::MicDirectionHistory).GetValue<MicDirectionHistory>();}


  // Util functions
  OffTreadsState GetOffTreadsState() const;
  Util::RandomGenerator& GetRNG();

private:
  struct CompArrayWrapper{
    public:
      CompArrayWrapper(AIComponent*                  aiComponent,
                       AnimationComponent*            animationComponent,
                       BehaviorContainer*             behaviorContainer,
                       BehaviorEventComponent*        behaviorEventComponent,
                       BlockWorld*                    blockWorld,
                       BodyLightComponent*            bodyLightComponent,
                       CubeAccelComponent*            cubeAccelComponent,
                       CubeLightComponent*            cubeLightComponent,
                       DelegationComponent*           delegationComponent,
                       FaceWorld*                     faceWorld,
                       MapComponent*                  mapComponent,
                       MicDirectionHistory*           micDirectionHistory,
                       MoodManager*                   moodManager,
                       NeedsManager*                  needsManager,
                       ObjectPoseConfirmer*           objectPoseConfirmer,
                       PetWorld*                      petWorld,
                       ProgressionUnlockComponent*    progressionUnlockComponent,
                       ProxSensorComponent*           proxSensor,
                       PublicStateBroadcaster*        publicStateBroadcaster,
                       Audio::EngineRobotAudioClient* robotAudioClient,
                       BEIRobotInfo*                  robotInfo,
                       TouchSensorComponent*          touchSensorComponent,
                       VisionComponent*               visionComponent);
      ~CompArrayWrapper(){};
      EntityFullEnumeration<BEIComponentID, BEIComponentWrapper, BEIComponentID::Count> _array;
  };
  std::unique_ptr<CompArrayWrapper> _arrayWrapper;
};

} // namespace Cozmo
} // namespace Anki

#endif // __Cozmo_Basestation_BehaviorSystem_BehaviorExternalInterface_H__

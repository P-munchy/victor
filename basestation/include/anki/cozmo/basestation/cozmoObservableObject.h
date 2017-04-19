/**
 * File: cozmoObservableObject.h
 *
 * Author: Andrew Stein (andrew)
 * Created: ?/?/2015
 *
 *
 * Description: Extends Vision::ObservableObject to add some Cozmo-specific
 *              stuff, like object families and types.
 *
 * Copyright: Anki, Inc. 2015
 *
 **/

#ifndef __Anki_Cozmo_ObservableObject_H__
#define __Anki_Cozmo_ObservableObject_H__

#include "anki/cozmo/basestation/objectPoseConfirmer.h"
#include "anki/cozmo/shared/cozmoEngineConfig.h"

#include "anki/vision/basestation/observableObject.h"

#include "clad/types/activeObjectIdentityStates.h"
#include "clad/types/activeObjectConstants.h"
#include "clad/types/objectFamilies.h"
#include "clad/types/objectTypes.h"

#include "util/helpers/noncopyable.h"

namespace Anki {
namespace Cozmo {
  
  class VizManager;

  
  using ActiveID = s32;  // TODO: Change this to u32 and use 0 as invalid
  using FactoryID = u32;

  using FactoryIDArray = std::array<FactoryID, (size_t)ActiveObjectConstants::MAX_NUM_ACTIVE_OBJECTS>;

  class ObservableObject : public Vision::ObservableObject, private Util::noncopyable
  {
  public:

    static const ActiveID InvalidActiveID = -1;
    static const FactoryID InvalidFactoryID = 0;
    
    ObservableObject(ObjectFamily family, ObjectType type)
    : _family(family)
    , _type(type)
    {
      
    }
    
    virtual ObservableObject* CloneType() const override = 0;
    
    // Can only be called once and only before SetPose is called. Will assert otherwise,
    // since this indicates programmer error.
    void InitPose(const Pose3d& pose, PoseState poseState);
    
    // Override base class SetID to use unique ID for each type (base class has no concept of ObjectType)
    virtual void SetID() override;
    
    ObjectFamily  GetFamily()  const { return _family; }
    ObjectType    GetType()    const { return _type; }
    
    // Returns Identified for non-Active objects and the active identity state
    // for Active objects.
    ActiveIdentityState GetIdentityState() const;
    
    // Overload base IsSameAs() to first compare type and family
    // (Note that we have to overload all if we overload one)
    bool IsSameAs(const ObservableObject& otherObject,
                  const Point3f& distThreshold,
                  const Radians& angleThreshold,
                  Point3f& Tdiff,
                  Radians& angleDiff) const;
    
    bool IsSameAs(const ObservableObject& otherObject) const;

    bool IsSameAs(const ObservableObject& otherObject,
                  const Point3f& distThreshold,
                  const Radians& angleThreshold) const;
    
    void SetVizManager(VizManager* vizManager) { _vizManager = vizManager; }
    
    virtual bool IsActive()                     const override  { return false; }
    void         SetActiveID(ActiveID activeID);
    ActiveID     GetActiveID()                  const   { return _activeID; }
    void         SetFactoryID(FactoryID factoryID);
    FactoryID    GetFactoryID()                 const   { return _factoryID; }

    // Override in derived classes to allow them to exist co-located with robot
    virtual bool CanIntersectWithRobot()        const   { return false; }
    
    // Can we assume there is exactly one of these objects at a give time?
    virtual bool IsUnique()                     const   { return false; }

    // Get the distance within which we are allowed to localize to objects
    // (This will probably need to be updated with COZMO-9672)
    static f32 GetMaxLocalizationDistance_mm();
    
  protected:
    
    // Make SetPose and SetPoseParent protected and friend ObjectPoseConfirmer so only it can
    // update objects' poses
    virtual void SetPose(const Pose3d& newPose, f32 fromDistance, PoseState newPoseState) override;
    using Vision::ObservableObject::SetPoseParent;
    using Vision::ObservableObject::SetPoseState;
    friend ObjectPoseConfirmer;
    
    ActiveID _activeID = -1;
    FactoryID _factoryID = 0;
    
    ObjectFamily  _family = ObjectFamily::Unknown;
    ObjectType    _type   = ObjectType::UnknownObject;
    
    ActiveIdentityState _identityState = ActiveIdentityState::Unidentified;
    
    bool _poseHasBeenSet = false;
    
    VizManager* _vizManager = nullptr;
    
  }; // class ObservableObject
  
#pragma mark -
#pragma mark Inlined Implementations
  
  inline ActiveIdentityState ObservableObject::GetIdentityState() const {
    if(IsActive()) {
      return _identityState;
    } else {
      // Non-Active Objects are always "identified"
      return ActiveIdentityState::Identified;
    }
  }
  
  inline bool ObservableObject::IsSameAs(const ObservableObject& otherObject,
                                         const Point3f& distThreshold,
                                         const Radians& angleThreshold,
                                         Point3f& Tdiff,
                                         Radians& angleDiff) const
  {
    // The two objects can't be the same if they aren't the same type!
    bool isSame = this->GetType() == otherObject.GetType() && this->GetFamily() == otherObject.GetFamily();
    
    if(isSame) {
      isSame = Vision::ObservableObject::IsSameAs(otherObject, distThreshold, angleThreshold, Tdiff, angleDiff);
    }
    
    return isSame;
  }
  
  inline bool ObservableObject::IsSameAs(const ObservableObject& otherObject) const {
    return IsSameAs(otherObject, this->GetSameDistanceTolerance(), this->GetSameAngleTolerance());
  }

  inline bool ObservableObject::IsSameAs(const ObservableObject& otherObject,
                                         const Point3f& distThreshold,
                                         const Radians& angleThreshold) const
  {
    Point3f Tdiff;
    Radians angleDiff;
    return IsSameAs(otherObject, distThreshold, angleThreshold,
                    Tdiff, angleDiff);
  }
  
  inline void ObservableObject::SetActiveID(ActiveID activeID)
  {
    if(!IsActive()) {
      PRINT_NAMED_WARNING("ObservableObject.SetActiveID.NotActive", "ID: %d", GetID().GetValue());
      return;
    }
    
    _activeID = activeID;
    
    if (_activeID >= 0) {
      _identityState = ActiveIdentityState::Identified;
    }
  }
  
  inline void ObservableObject::SetFactoryID(FactoryID factoryID)
  {
    if(!IsActive()) {
      PRINT_NAMED_WARNING("ObservableObject.SetFactoryID.NotActive", "ID: %d", GetID().GetValue());
      return;
    }
    
    _factoryID = factoryID;
  }
  
  
} // namespace Cozmo
} // namespace Anki

#endif // __Anki_Cozmo_ObservableObject_H__

/**
 * File: blockWorldFilter.h
 *
 * Author: Andrew Stein (andrew)
 * Created: 10/27/2015
 *
 * Information on last revision to this file:
 *    $LastChangedDate$
 *    $LastChangedBy$
 *    $LastChangedRevision$
 *
 * Description: A helper class for filtering searches through objects in BlockWorld.
 *
 * Copyright: Anki, Inc. 2015
 *
 **/

#ifndef __Anki_Cozmo_BlockWorldFilter_H__
#define __Anki_Cozmo_BlockWorldFilter_H__

#include "anki/common/basestation/objectIDs.h"
#include "anki/common/basestation/math/poseOrigin.h"
#include "engine/cozmoObservableObject.h"
#include "clad/types/objectFamilies.h"
#include "clad/types/objectTypes.h"

#include <set>
#include <assert.h>


namespace Anki {
namespace Cozmo {

  // Forward declaration
  class ObservableObject;
  
  class BlockWorldFilter
  {
  public:

    BlockWorldFilter() { }
    
    // These are the methods called by BlockWorld when looping over existing
    // object families, types, and IDs to decide whether to continue.
    // An object cannot be in an ignore list and either the allowed list must be
    // empty or the object must be in it in order to pass.
    bool ConsiderOrigin(PoseOriginID_t objectOriginID, PoseOriginID_t robotOriginID) const;
    bool ConsiderFamily(ObjectFamily family) const;
    bool ConsiderType(ObjectType type) const;
    bool ConsiderObject(const ObservableObject* object) const; // Checks ID and runs FilterFcn(object)
    
    // Set the entire set of IDs, types, or families to ignore in one go.
    void SetIgnoreIDs(std::set<ObjectID>&& IDs);
    void SetIgnoreTypes(std::set<ObjectType>&& types);
    void SetIgnoreFamilies(std::set<ObjectFamily>&& families);
    void SetIgnoreOrigins(std::set<PoseOriginID_t>&& originIDs);
    
    // Add to the existing set of IDs, types, or families
    void AddIgnoreID(const ObjectID& ID);
    void AddIgnoreIDs(const std::set<ObjectID>& IDs);
    void AddIgnoreType(ObjectType type);
    void AddIgnoreFamily(ObjectFamily family);
    void AddIgnoreOrigin(PoseOriginID_t originID);
    
    // Set the entire set of IDs, types, or families to be allowed in one go.
    void SetAllowedIDs(std::set<ObjectID>&& IDs);
    void SetAllowedTypes(std::set<ObjectType>&& types);
    void SetAllowedFamilies(std::set<ObjectFamily>&& families);
    void SetAllowedOrigins(std::set<PoseOriginID_t>&& originIDs);
    
    // Add to the existing set of IDs, types, or families
    void AddAllowedID(const ObjectID& ID);
    void AddAllowedIDs(const std::set<ObjectID>& IDs);
    void AddAllowedType(ObjectType type);
    void AddAllowedFamily(ObjectFamily family);
    void AddAllowedOrigin(PoseOriginID_t originID);
    
    // Set the filtering function used at the object level
    // NOTE: Default filter requires object to have poseState != Unknown
    using FilterFcn = std::function<bool(const ObservableObject*)>;
    void SetFilterFcn(const FilterFcn& filterFcn); // replace any existing
    void AddFilterFcn(const FilterFcn& filterFcn); // add to list of filters (all must pass)
    
    // Handy, commonly-used filter functions
    static bool PoseStateKnownFilter(const ObservableObject* object);
    static bool ActiveObjectsFilter(const ObservableObject* object);
    static bool UniqueObjectsFilter(const ObservableObject* object);
    
    // Normally, all objects known to BlockWorld are checked. Setting this to
    // true will only check those objects observed in the most recent BlockWorld
    // Update() call.
    void OnlyConsiderLatestUpdate(bool tf) { _onlyConsiderLatestUpdate = tf; }
    bool IsOnlyConsideringLatestUpdate() const { return _onlyConsiderLatestUpdate; }
    
    enum class OriginMode : uint8_t {
      InRobotFrame,     // Only objects in the current robot coordinate frame are returned (Default)
      NotInRobotFrame,  // Only objects *not* in the current robot coordinate frame are returned
      InAnyFrame,       // Objects in any frame considered (ignore/allowed sets empty)
      Custom            // Uses allowed/ignored sets provided using methods above
    };
    void SetOriginMode(OriginMode mode) { _originMode = mode; }
    
  protected:
    std::set<ObjectID>             _ignoreIDs,      _allowedIDs;
    std::set<ObjectType>           _ignoreTypes,    _allowedTypes;
    std::set<ObjectFamily>         _ignoreFamilies, _allowedFamilies;
    std::set<PoseOriginID_t>       _ignoreOrigins,  _allowedOrigins;
    
    std::list<FilterFcn>    _filterFcns;
    
    bool _onlyConsiderLatestUpdate = false;
    OriginMode _originMode = OriginMode::InRobotFrame;
    
    template<class T>
    static bool ConsiderHelper(const std::set<T>& ignoreSet, const std::set<T>&allowSet, T x);
    
  }; // class BlockWorldFilter

  
# pragma mark - Inlined Implementations
  
  inline void BlockWorldFilter::SetIgnoreOrigins(std::set<PoseOriginID_t>&& origins) {
    _ignoreOrigins = origins;
  }
  
  inline void BlockWorldFilter::SetIgnoreFamilies(std::set<ObjectFamily> &&families) {
    _ignoreFamilies = families;
  }
  
  inline void BlockWorldFilter::SetIgnoreTypes(std::set<ObjectType> &&types) {
    _ignoreTypes = types;
  }
  
  inline void BlockWorldFilter::SetIgnoreIDs(std::set<ObjectID> &&IDs) {
    _ignoreIDs = IDs;
  }
  
  inline void BlockWorldFilter::SetAllowedIDs(std::set<ObjectID>&& IDs) {
    _allowedIDs = IDs;
  }
  
  inline void BlockWorldFilter::SetAllowedTypes(std::set<ObjectType>&& types) {
    _allowedTypes = types;
  }
  
  inline void BlockWorldFilter::SetAllowedFamilies(std::set<ObjectFamily>&& families) {
    _allowedFamilies = families;
  }
  
  inline void BlockWorldFilter::SetAllowedOrigins(std::set<PoseOriginID_t>&& origins) {
    _allowedOrigins = origins;
  }
  
  inline void BlockWorldFilter::SetFilterFcn(const FilterFcn& filterFcn) {
    _filterFcns.clear();
    AddFilterFcn(filterFcn);
  }
  
  inline void BlockWorldFilter::AddFilterFcn(const FilterFcn& filterFcn) {
    if(filterFcn != nullptr) {
      _filterFcns.push_back(filterFcn);
    }
  }
  
  inline void BlockWorldFilter::AddIgnoreID(const ObjectID& ID) {
    assert(_allowedIDs.count(ID) == 0); // Should not be in both lists
    _ignoreIDs.insert(ID);
  }
  
  inline void BlockWorldFilter::AddIgnoreIDs(const std::set<ObjectID>& IDs) {
    _ignoreIDs.insert(IDs.begin(), IDs.end());
  }
  
  inline void BlockWorldFilter::AddIgnoreType(ObjectType type) {
    assert(_allowedTypes.count(type) == 0); // Should not be in both lists
    _ignoreTypes.insert(type);
  }
  
  inline void BlockWorldFilter::AddIgnoreFamily(ObjectFamily family) {
    assert(_allowedFamilies.count(family) == 0); // Should not be in both lists
    _ignoreFamilies.insert(family);
  }
  
  inline void BlockWorldFilter::AddAllowedID(const ObjectID& ID) {
    assert(_ignoreIDs.count(ID) == 0); // Should not be in both lists
    _allowedIDs.insert(ID);
  }

  inline void BlockWorldFilter::AddAllowedIDs(const std::set<ObjectID>& IDs) {
    _allowedIDs.insert(IDs.begin(), IDs.end());
  }
  
  inline void BlockWorldFilter::AddAllowedType(ObjectType type) {
    assert(_ignoreTypes.count(type) == 0); // Should not be in both lists
    _allowedTypes.insert(type);
  }
  
  inline void BlockWorldFilter::AddAllowedFamily(ObjectFamily family) {
    assert(_ignoreFamilies.count(family) == 0); // Should not be in both lists
    _allowedFamilies.insert(family);
  }
  
  inline void BlockWorldFilter::AddAllowedOrigin(PoseOriginID_t originID) {
    assert(_ignoreOrigins.count(originID) == 0); // Should not be in both lists
    SetOriginMode(OriginMode::Custom);
    _allowedOrigins.insert(originID);
  }
  
  inline void BlockWorldFilter::AddIgnoreOrigin(PoseOriginID_t originID) {
    assert(_allowedOrigins.count(originID) == 0); // Should not be in both lists
    SetOriginMode(OriginMode::Custom);
    _ignoreOrigins.insert(originID);
  }

  template<class T>
  inline bool BlockWorldFilter::ConsiderHelper(const std::set<T>& ignoreSet, const std::set<T>&allowSet, T x)
  {
    const bool notInIgnoreSet = ignoreSet.count(x) == 0;
    const bool isAllowed = (allowSet.empty() || allowSet.count(x) > 0);
    const bool consider = (notInIgnoreSet && isAllowed);
    return consider;
  }
  
  inline bool BlockWorldFilter::ConsiderOrigin(PoseOriginID_t objectOrigin, PoseOriginID_t robotOrigin) const
  {
    switch(_originMode)
    {
      case BlockWorldFilter::OriginMode::Custom:
        return ConsiderHelper(_ignoreOrigins, _allowedOrigins, objectOrigin);
        
      case BlockWorldFilter::OriginMode::InAnyFrame:
        DEV_ASSERT(_ignoreOrigins.empty() && _allowedOrigins.empty(),
                   "BlockWorldFilter.ConsiderOrigin.IgnoringCustomOriginSets");
        return true;
        
      case BlockWorldFilter::OriginMode::InRobotFrame:
        DEV_ASSERT(_ignoreOrigins.empty() && _allowedOrigins.empty(),
                   "BlockWorldFilter.ConsiderOrigin.IgnoringCustomOriginSets");
        return objectOrigin == robotOrigin;
        
      case BlockWorldFilter::OriginMode::NotInRobotFrame:
        DEV_ASSERT(_ignoreOrigins.empty() && _allowedOrigins.empty(),
                   "BlockWorldFilter.ConsiderOrigin.IgnoringCustomOriginSets");
        return objectOrigin != robotOrigin;
    }
  }
  
  inline bool BlockWorldFilter::ConsiderFamily(ObjectFamily family) const {
    return ConsiderHelper(_ignoreFamilies, _allowedFamilies, family);
  }
  
  inline bool BlockWorldFilter::ConsiderType(ObjectType type) const {
    return ConsiderHelper(_ignoreTypes, _allowedTypes, type);
  }
  
  inline bool BlockWorldFilter::ConsiderObject(const ObservableObject* object) const
  {
    DEV_ASSERT(nullptr != object, "BlockWorldFilter.ConsiderObject.NullObject");
    
    const bool considerObj = ConsiderHelper(_ignoreIDs, _allowedIDs, object->GetID());
    if(considerObj)
    {
      for(auto & filterFcn : _filterFcns)
      {
        if(!filterFcn(object)) {
          // Fail as soon as any filter function returns false
          return false;
        }
      }
      
      return true;
    }
    return false;
  }
  
  inline bool BlockWorldFilter::PoseStateKnownFilter(const ObservableObject* object)
  {
    DEV_ASSERT(nullptr != object, "BlockWorldFilter.PoseStateKnownFilter.NullObject");
    return object->IsPoseStateKnown();
  }
  
  inline bool BlockWorldFilter::ActiveObjectsFilter(const ObservableObject* object) {
    DEV_ASSERT(nullptr != object, "BlockWorldFilter.ActiveObjectsFilter.NullObject");
    return object->IsActive();
  }
  
  inline bool BlockWorldFilter::UniqueObjectsFilter(const ObservableObject* object) {
    DEV_ASSERT(nullptr != object, "BlockWorldFilter.UniqueObjectsFilter.NullObject");
    return object->IsUnique();
  }
  
  

} // namespace Cozmo
} // namespace Anki



#endif // __Anki_Cozmo_BlockWorldFilter_H__

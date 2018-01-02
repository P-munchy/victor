/**
 * File: memoryMapData_ObservableObject.h
 *
 * Author: Michael Willett
 * Date:   2017-07-31
 *
 * Description: Data for obstacle quads.
 *
 * Copyright: Anki, Inc. 2017
 **/
 
 #ifndef __Anki_Cozmo_MemoryMapDataObservableObject_H__
 #define __Anki_Cozmo_MemoryMapDataObservableObject_H__

#include "memoryMapData.h"

#include "anki/common/basestation/math/point.h"
#include "anki/common/basestation/math/polygon.h"
#include "coretech/vision/engine/observableObject.h"

namespace Anki {
namespace Cozmo {

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// MemoryMapData_ObservableObject
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
class MemoryMapData_ObservableObject : public MemoryMapData
{
public:
  // constructor
  MemoryMapData_ObservableObject(MemoryMapTypes::EContentType type, const ObjectID& id, const Poly2f& p, TimeStamp_t t);
  
  // create a copy of self (of appropriate subclass) and return it
  MemoryMapData* Clone() const override;
  
  // compare to IMemoryMapData and return bool if the data stored is the same
  bool Equals(const MemoryMapData* other) const override;
  
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Attributes
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // If you add attributes, make sure you add them to ::Equals and ::Clone (if required)
  const ObjectID id;
  const Poly2f boundingPoly; 
  
protected: 
  MemoryMapData_ObservableObject() : MemoryMapData(MemoryMapTypes::EContentType::ObstacleCube, 0, true), id(), boundingPoly() {}
};
 
} // namespace
} // namespace

#endif //

/**
 * File: activeFeature.h
 *
 * Author: Brad Neuman
 * Created: 2018-05-07
 *
 * Description: Component to track the current active feature
 *
 * Copyright: Anki, Inc. 2018
 *
 **/

#ifndef __Engine_AiComponent_BehaviorComponent_ActiveFeature_H__
#define __Engine_AiComponent_BehaviorComponent_ActiveFeature_H__


#include "engine/aiComponent/behaviorComponent/behaviorComponents_fwd.h"
#include "util/entityComponent/iDependencyManagedComponent.h"
#include "util/helpers/noncopyable.h"

#include "clad/types/behaviorComponent/activeFeatures.h"

#include <cstdint>
#include <vector>

namespace Anki {
namespace Cozmo {

class CozmoContext;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
class ActiveFeatureComponent : public IDependencyManagedComponent<BCComponentID>
                             , public Anki::Util::noncopyable
{
public:
  ActiveFeatureComponent();

  virtual void GetInitDependencies( BCCompIDSet& dependencies ) const override {
    dependencies.insert(BCComponentID::RobotInfo);
  }
  virtual void InitDependent( Robot* robot, const BCCompMap& dependentComponents ) override;
  
  virtual void GetUpdateDependencies( BCCompIDSet& dependencies ) const override {
    // ensure the bsm updates first so that the stack is in the new state when this component ticks
    dependencies.insert(BCComponentID::BehaviorSystemManager);
    dependencies.insert(BCComponentID::ActiveBehaviorIterator);
    dependencies.insert(BCComponentID::UserIntentComponent);
  }
  virtual void UpdateDependent(const BCCompMap& dependentComponents) override;

  // get the current active feature (or ActiveFeature::None if none is active)
  ActiveFeature GetActiveFeature() const;

private:

  void SendActiveFeatureToWebViz(const std::string& intentSource) const;
  
  ActiveFeature _activeFeature = ActiveFeature::NoFeature;

  // only one feature should count as activated by a given active intent, so track the ID here
  size_t _lastUsedIntentActivationID = 0;

  const CozmoContext* _context = nullptr;
};

}
}

#endif

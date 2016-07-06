/**
 * File: visuallyVerifyActions.h
 *
 * Author: Andrew Stein
 * Date:   6/16/2016
 *
 * Description: Actions for visually verifying the existance of objects or face.
 *              Succeeds if the robot can see the given object or face in its current pose.
 *
 *
 * Copyright: Anki, Inc. 2016
 **/

#ifndef __Anki_Cozmo_Basestation_VisuallyVerifyActions_H__
#define __Anki_Cozmo_Basestation_VisuallyVerifyActions_H__

#include "anki/cozmo/basestation/actions/actionInterface.h"
#include "anki/cozmo/basestation/actions/basicActions.h"
#include "anki/cozmo/basestation/actions/compoundActions.h"
#include "anki/cozmo/basestation/externalInterface/externalInterface.h"
#include "anki/vision/basestation/faceIdTypes.h"

#include "clad/types/actionTypes.h"
#include "clad/types/visionModes.h"

namespace Anki {
namespace Cozmo {

  class IVisuallyVerifyAction : public IAction
  {
  public:
    using LiftPreset = MoveLiftToHeightAction::Preset;
    
    IVisuallyVerifyAction(Robot& robot,
                          const std::string name,
                          const RobotActionType type,
                          VisionMode imageTypeToWaitFor,
                          LiftPreset liftPosition);
    
    virtual ~IVisuallyVerifyAction();
    
    virtual int GetNumImagesToWaitFor() const { return _numImagesToWaitFor; }
    void SetNumImagesToWaitFor(int numImages) { _numImagesToWaitFor = numImages; }
    
  protected:
    
    virtual ActionResult Init() override final;
    virtual ActionResult CheckIfDone() override final;
    
    using EngineToGameEvent = AnkiEvent<ExternalInterface::MessageEngineToGame>;
    using EngineToGameTag   = ExternalInterface::MessageEngineToGameTag;
    using EventCallback     = std::function<void(const EngineToGameEvent&)>;
    
    // Should be called in InitInternal()
    void SetupEventHandler(EngineToGameTag tag, EventCallback callback);
    
    // Derived classes should implement these:
    virtual ActionResult InitInternal() = 0;
    virtual bool HaveSeenObject() = 0;
    
  private:
    
    VisionMode            _imageTypeToWaitFor = VisionMode::Count;
    LiftPreset            _liftPreset = MoveLiftToHeightAction::Preset::LOW_DOCK;
    ICompoundAction*      _compoundAction = nullptr;
    Signal::SmartHandle   _observationHandle;
    int                   _numImagesToWaitFor = 10;

  }; // class IVisuallyVerifyAction
  
  
  class VisuallyVerifyObjectAction : public IVisuallyVerifyAction
  {
  public:
    VisuallyVerifyObjectAction(Robot& robot,
                               ObjectID objectID,
                               Vision::Marker::Code whichCode = Vision::Marker::ANY_CODE);
    
    virtual ~VisuallyVerifyObjectAction();
    
  protected:
    
    virtual ActionResult InitInternal() override;
    virtual bool HaveSeenObject() override;
    
    ObjectID                _objectID;
    Vision::Marker::Code    _whichCode;
    bool                    _objectSeen = false;
    bool                    _markerSeen = false;
    
  }; // class VisuallyVerifyObjectAction
  
  
  class VisuallyVerifyFaceAction : public IVisuallyVerifyAction
  {
  public:
    VisuallyVerifyFaceAction(Robot& robot, Vision::FaceID_t faceID);
    
    virtual ~VisuallyVerifyFaceAction();
    
  protected:
    
    virtual ActionResult InitInternal() override;
    virtual bool HaveSeenObject() override;
    
    Vision::FaceID_t        _faceID;
    bool                    _faceSeen = false;
    
  }; // class VisuallyVerifyFaceAction
  
  
} // namespace Cozmo
} // namespace Anki

#endif /* __Anki_Cozmo_Basestation_VisuallyVerifyActions_H__ */

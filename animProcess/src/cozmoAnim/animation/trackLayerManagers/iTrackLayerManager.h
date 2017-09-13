/**
 * File: iTrackLayerManager.h
 *
 * Authors: Al Chaussee
 * Created: 06/28/2017
 *
 * Description: Templated class for managing animation track layers
 *              of a specific keyframe type
 *
 * Copyright: Anki, Inc. 2017
 *
 **/

#ifndef __Anki_Cozmo_ITrackLayerManager_H__
#define __Anki_Cozmo_ITrackLayerManager_H__

#include "anki/common/types.h"
#include "cozmoAnim/animation/animation.h"
#include "cozmoAnim/animation/track.h"

#include <map>

namespace Anki {
namespace Cozmo {

template<class FRAME_TYPE>
class ITrackLayerManager
{
public:

  ITrackLayerManager(const Util::RandomGenerator& rng);
  
  using ApplyLayerFunc = std::function<bool(Animations::Track<FRAME_TYPE>&,
                                            TimeStamp_t,
                                            TimeStamp_t,
                                            FRAME_TYPE&)>;
  
  // Updates frame by applying all layers to it using applyLayerFunc which should define
  // how to combine the current keyframe in the layer's track with another keyframe
  // Both applyFunc and this function should return whether or not the frame was updated
  // Note: applyLayerFunc is responsible for moving to the next keyframe of a layer's track
  bool ApplyLayersToFrame(FRAME_TYPE& frame,
                          ApplyLayerFunc applyLayerFunc);
  
  // Adds the given track as a new layer with an initial start delay of delay_ms
  Result AddLayer(const std::string& name,
                  const Animations::Track<FRAME_TYPE>& track,
                  TimeStamp_t delay_ms = 0);
  
  // Adds the given track as a persitent layer
  // Returns a tag as reference to the persistent layer
  AnimationTag AddPersistentLayer(const std::string& name,
                                  const Animations::Track<FRAME_TYPE>& track);
  
  // Adds a keyframe onto an existing persistent layer
  void AddToPersistentLayer(AnimationTag tag, FRAME_TYPE& keyFrame);
  
  // Removes a persitent layer after duration_ms has passed
  void RemovePersistentLayer(AnimationTag tag, s32 duration_ms = 0);
  
  // Returns true if there are any layers
  bool HaveLayersToSend() const;
  
  // Returns the number of layers
  size_t GetNumLayers() const { return _layers.size(); }
  
  // Returns true if there is a layer with name
  bool HasLayerWithName(const std::string& name) const;
  
  // Returns true if there is a layer with tag
  bool HasLayerWithTag(AnimationTag tag) const;
  
protected:
  
  const Util::RandomGenerator& GetRNG() const { return _rng; }
  
private:

  const Util::RandomGenerator& _rng;

  // Structure defining an individual layer
  struct Layer {
    Animations::Track<FRAME_TYPE> track;
    TimeStamp_t  startTime_ms;
    TimeStamp_t  streamTime_ms;
    bool         isPersistent;
    bool         sentOnce;
    AnimationTag tag;
    std::string  name;
  };

  std::map<AnimationTag, Layer> _layers;
  
  AnimationTag _layerTagCtr = 0;
  
  void IncrementLayerTagCtr();
};

}
}

#endif

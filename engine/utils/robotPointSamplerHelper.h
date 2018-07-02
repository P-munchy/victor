/**
 * File: robotPointSamplerHelper.h
 *
 * Author: ross
 * Created: 2018 Jun 29
 *
 * Description: Helper class for rejection sampling 2D positions that abide by some constraints related to the robot
 *
 * Copyright: Anki, Inc. 2018
 *
 **/

#ifndef __Engine_Utils_RobotPointSamplerHelper_H__
#define __Engine_Utils_RobotPointSamplerHelper_H__

#include "coretech/common/engine/math/point.h"
#include "coretech/common/engine/math/polygon.h"
#include "engine/navMap/memoryMap/memoryMapTypes.h"
#include "util/random/rejectionSamplerHelper.h"

#include <set>

namespace Anki{
  
class Pose3d;
  
namespace Util {
  class RandomGenerator;
}
  
namespace Cozmo{
  
class INavMap;
  
namespace RobotPointSamplerHelper {
  
// uniformly sample a point on circle of radius (0, radius)
Point2f SamplePointInCircle( Util::RandomGenerator& rng, f32 radius );

// uniformly sample a point on an annulus between radii (minRadius, maxRadius)
Point2f SamplePointInAnnulus( Util::RandomGenerator& rng, f32 minRadius, f32 maxRadius );

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
class RejectIfWouldCrossCliff : public Anki::Util::RejectionSamplingCondition<Point2f>
{
public:
  explicit RejectIfWouldCrossCliff( float minCliffDist_mm );
  
  void SetRobotPosition(const Point2f& pos);
  
  // If not set, any sample that is within minCliffDist_mm is accepted and any outside is rejected.
  // If set, then additionally, any sample between minCliffDist_mm and maxCliffDist_mm is accepted
  // with probability linearly increasing from 0 to 1 over that range
  void SetAcceptanceInterpolant( float maxCliffDist_mm, Util::RandomGenerator& rng );
  
  // This method caches cliff pointers, so must be called every time you want to use this condition
  // with the latest memory map data
  void UpdateCliffs( const INavMap* memoryMap );
  
  virtual bool operator()( const Point2f& sampledPos ) override;
  
private:
  std::set<const Pose3d*> _cliffs;
  Point2f _robotPos;
  bool _setRobotPos = false;
  float _minCliffDistSq;
  Util::RandomGenerator* _rng = nullptr;
  float _maxCliffDistSq = 0.0f;
};
  
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
class RejectIfNotInRange : public Anki::Util::RejectionSamplingCondition<Point2f>
{
  public:
    RejectIfNotInRange( float minDist_mm, float maxDist_mm );
    void SetOtherPosition(const Point2f& pos);
    
    // Requires SetOtherPosition to be set
    virtual bool operator()( const Point2f& sampledPos ) override;
  private:
    Point2f _otherPos;
    const float _minDistSq;
    const float _maxDistSq;
    bool _setOtherPos = false;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
class RejectIfCollidesWithMemoryMap : public Anki::Util::RejectionSamplingCondition<Poly2f>
{
public:
  explicit RejectIfCollidesWithMemoryMap( const MemoryMapTypes::FullContentArray& collisionTypes );
  
  void SetMemoryMap( const INavMap* memoryMap ) { _memoryMap = memoryMap; }
  
  // If not set, any sample that collides is rejected. If set, it is accepted with probability p
  void SetAcceptanceProbability( float p, Util::RandomGenerator& rng );
  
  virtual bool operator()( const Poly2f& sampledPoly ) override;
  
private:
  const INavMap* _memoryMap = nullptr;
  const MemoryMapTypes::FullContentArray& _collisionTypes;
  Util::RandomGenerator* _rng = nullptr;
  float _pAccept = 0.0f;
};
  
}
  
} // namespace
} // namespace

#endif

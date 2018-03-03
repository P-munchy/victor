/**
 * File: convexPolygon2d.h
 *
 * Author: Michael Willett
 * Created: 2017-10-3
 *
 * Description: A class wrapper for a 2d polygon with convexity helpers and constraints.
 *
 * Copyright: Anki, Inc. 2014
 *
 **/

#ifndef __COMMON_BASESTATION_MATH_CONVEXPOLYGON2D_H__
#define __COMMON_BASESTATION_MATH_CONVEXPOLYGON2D_H__

#include "coretech/common/engine/math/polygon.h"
#include "coretech/common/engine/math/point.h"

#include <vector>

namespace Anki {

class ConvexPolygon : public Poly2f
{
public:
  enum EClockDirection { CW, CCW };
  
  // must be constructed from an existing polygon
  ConvexPolygon(const Poly2f& basePolygon);
  
  virtual       Point2f& operator[](size_t i)       override { return _points[GetInternalIdx(i)]; }
  virtual const Point2f& operator[](size_t i) const override { return _points[GetInternalIdx(i)]; }

  void SetClockDirection(EClockDirection d) { _currDirection = d; };
  EClockDirection GetClockDirection() const { return _currDirection; }
    
  static bool IsConvex(const Poly2f& poly);
  
  void RadialExpand(f32 d);
  
private:
  EClockDirection _currDirection;
  size_t GetInternalIdx(size_t i) const { return (_currDirection == CW) ? i : size() - i;}
};

}

#endif

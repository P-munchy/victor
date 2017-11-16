/**
 * File: lineSegment2d.h
 *
 * Author: Michael Willett
 * Created: 2017-10-12
 *
 * Description: A class that holds a 2d line segment for fast intersection checks
 *
 * Copyright: Anki, Inc. 2017
 *
 **/

#ifndef __COMMON_BASESTATION_MATH_LINESEGMENT2D_H__
#define __COMMON_BASESTATION_MATH_LINESEGMENT2D_H__

#include "anki/common/basestation/math/point.h"

namespace Anki {

class LineSegment
{
public:  
  // cache useful data for minor speedup of intersection checks
  LineSegment(const Point2f& from, const Point2f& to) ;
  
  // check if the point is Co-linear with line segment and constrained by start and end points
  bool OnSegment(const Point2f& p) const;
  
  // check if the two line segments intersect
  bool IntersectsWith(const LineSegment& l) const;
  
  
  // compute the dot product of a point relative to the vector `from->to`
  float Dot(const Point2f& p) const;
  
  // length of line segment
  float Length() const { return std::hypot(dX, dY); }
  
  // Get start and end points
  const Point2f& GetFrom() const { return from; }
  const Point2f& GetTo()   const { return to; } 
  
private:
  
  enum class EOrientation {COLINEAR, CW, CCW};
  
  Point2f from;
  Point2f to;
  
  float minX;
  float maxX;
  float minY;
  float maxY;
  float dX;
  float dY;
  
  // check which clock direction the points form `from->to->p`
  EOrientation Orientation(const Point2f& p) const;

  // checks if point is constrained by AABB defined by `from->to`
  bool InBoundingBox(const Point2f& p) const;
};

}

#endif

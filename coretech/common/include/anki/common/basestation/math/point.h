/**
 * File: point.h
 *
 * Author: Andrew Stein (andrew)
 * Created: 9/10/2013
 *
 *
 * Description: Defines a general N-dimensional Point class and two 
 *              subclasses for commonly-used 2D and 3D points.  The latter have
 *              special accessors for x, y, and z components as well. All offer
 *              math capabilities and are templated to store any type.
 *
 *              NOTE: These classes may also be useful to store 2-, 3- or 
 *                    N-dimensional vectors as well. Thus there are also 
 *                    aliases for Vec3f and Vec2f.
 *
 * Copyright: Anki, Inc. 2013
 *
 **/

#ifndef _ANKICORETECH_COMMON_POINT_H_
#define _ANKICORETECH_COMMON_POINT_H_

#include "anki/common/types.h"

#if ANKICORETECH_USE_OPENCV
#include "opencv2/core/core.hpp"
#endif

#include <array>

namespace Anki {
  
  using PointDimType = size_t;
  
  // Generic N-dimensional Point class
  template<PointDimType N, typename T>
  class Point //: public std::array<T,N>
  {
    static_assert(N>0, "Cannot create an empty Point.");
    
  public:
    
    // Constructors
    Point( void );
    
    template<typename T_other>
    Point(const Point<N,T_other>& pt);
    //Point(const std::array<T,N>& array); // Creates ambiguity with opencv Point3_ constructor below
    

#if __cplusplus == 201103L
    // Point(T x1, T x2, ..., T xN);
    // This is ugly variadic template syntax to get this constructor to
    // work for all N and generate a compile-time error if you try to do
    // something like: Point<2,int> p(1, 2, 3);
    //
    // See here for more:
    //  http://stackoverflow.com/questions/8158261/templates-how-to-control-number-of-constructor-args-using-template-variable
    template <typename... Tail>
    Point(typename std::enable_if<sizeof...(Tail)+1 == N, T>::type head, Tail... tail)
    : data{{ head, T(tail)... }} {}
#else
#warning No variadic templates.
    Point(const T x, const T y); // Only valid if N==2
    Point(const T x, const T y, const T z); // Only valid if N==3
#endif
    
    // Assignment operator:
    template<typename T_other>
    Point<N,T>& operator=(const Point<N,T_other> &other);
    Point<N,T>& operator=(const T &value);
    
    // Accessors:
    T& operator[] (const PointDimType i);
    const T& operator[] (const PointDimType i) const;
    
    // Special mnemonic accessors for the first, second,
    // and third elements, available when N is large enough.
    
    T& x();
    T& y();
    T& z();
    const T& x() const;
    const T& y() const;
    const T& z() const;
    
#if ANKICORETECH_USE_OPENCV
    Point(const cv::Point_<T>& pt);
    Point(const cv::Point3_<T>& pt);
    cv::Point_<T> get_CvPoint_() const;
    cv::Point3_<T> get_CvPoint3_() const;
#endif
    
    // Arithmetic Operators
    Point<N,T>& operator+= (const T value);
    Point<N,T>& operator-= (const T value);
    Point<N,T>& operator*= (const T value);
    Point<N,T>& operator/= (const T value);
    Point<N,T> operator* (const T value) const;
    Point<N,T>& operator+= (const Point<N,T> &other);
    Point<N,T>& operator-= (const Point<N,T> &other);
    Point<N,T>& operator*= (const Point<N,T> &other);
    Point<N,T>  operator-() const;
    
    // Return length of the vector from the origin to the point
    T Length(void) const;
    
    // Makes the point into a unit vector from the origin, while
    // returning its original length. IMPORTANT: if the point was
    // originally the origin, it cannot be made into a unit vector
    // and will be left at the origin, and zero will be returned.
    T MakeUnitLength(void);
    
  protected:
    
    std::array<T,N> data;
    
  }; // class Point
  
  // Create some convenience aliases/typedefs for 2D and 3D points:
  template <typename T>
  using Point2 = Point<2, T>;
  
  template <typename T>
  using Point3 = Point<3, T>;
  
  using Point2f = Point2<f32>;
  using Point3f = Point3<f32>;
  
  using Vec2f = Point2f;
  using Vec3f = Point3f;
  
  const Vec2f X_AXIS_2D(1.f, 0.f);
  const Vec2f Y_AXIS_2D(0.f, 1.f);
  
  const Vec3f X_AXIS_3D(1.f, 0.f, 0.f);
  const Vec3f Y_AXIS_3D(0.f, 1.f, 0.f);
  const Vec3f Z_AXIS_3D(0.f, 0.f, 1.f);
  
  /*
  template<PointDimType N, typename T>
  class UnitVector : public Point<N,T>
  {
    template <typename... Tail>
    UnitVector(typename std::enable_if<sizeof...(Tail)+1 == N, T>::type head, Tail... tail)
    : Point<N,T>(head, T(tail)...)
    {
      this->makeUnitLength();
    }
    
    // All setters call superclass set function and then renormalize?
    
    // Always returns one:
    T length(void) const { return T(1); };
    
  }; // class UnitVector
  */
  
  /*
   // TODO: Do need a separate Vec class?
  template<PointDimType N, typename T>
  class Vec : public Point<N,T>
  {
    
  }; // class Vec
  
  template<typename T>
  using Vec2 = Vec<2,T>;
  
  template<typename T>
  using Vec3 = Vec<3,T>;
  */
  
  // Display / Logging:
  template<PointDimType N, typename T>
  std::ostream& operator<<(std::ostream& out, const Point<N,T>& p);
  
  // Binary mathematical operations:
  template<PointDimType N, typename T>
  bool operator== (const Point<N,T> &point1, const Point<N,T> &point2);
  
  template<PointDimType N, typename T>
  bool nearlyEqual(const Point<N,T> &point1, const Point<N,T> &point2,
                   const T eps = T(10)*std::numeric_limits<T>::epsilon());
  
  template<PointDimType N, typename T>
  Point<N,T> operator+ (const Point<N,T> &point1, const Point<N,T> &point2);
  
  template<PointDimType N, typename T>
  Point<N,T> operator- (const Point<N,T> &point1, const Point<N,T> &point2);
  
  template<PointDimType N, typename T>
  T DotProduct(const Point<N,T> &point1, const Point<N,T> &point2);

  template<typename T>
  Point3<T> CrossProduct(const Point3<T> &point1, const Point3<T> &point2);
  
  // TODO: should output type always be float/double?
  template<PointDimType N, typename T>
  T computeDistanceBetween(const Point<N,T>& point1, const Point<N,T>& point2);
  
  
} // namespace Anki

#endif // _ANKICORETECH_COMMON_POINT_H_

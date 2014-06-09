/**
 * File: rect_impl.h
 *
 * Author: Andrew Stein (andrew)
 * Created: 5/10/2014
 *
 *
 * Description: Defines a templated class for storing 2D rectangles according to
 *              their upper left corner and width + height.
 *
 * Copyright: Anki, Inc. 2014
 *
 **/

#ifndef _ANKICORETECH_COMMON_RECT_IMPL_H_
#define _ANKICORETECH_COMMON_RECT_IMPL_H_

#include "anki/common/basestation/math/rect.h"

#include "anki/common/basestation/math/quad_impl.h"

namespace Anki {
  
  template<typename T>
  Rectangle<T>::Rectangle()
#if ANKICORETECH_USE_OPENCV
  : cv::Rect_<T>()
#endif
  {
    
  }
  
  template<typename T>
  Rectangle<T>::Rectangle(T x, T y, T width, T height)
#if ANKICORETECH_USE_OPENCV
  : cv::Rect_<T>(x,y,width,height)
#endif
  {
    
  }
  
#if ANKICORETECH_USE_OPENCV
  template<typename T>
  Rectangle<T>::Rectangle(const cv::Rect_<T>& cvRect)
  : cv::Rect_<T>(cvRect)
  {
    
  }
  
  template<typename T>
  cv::Rect_<T>& Rectangle<T>::get_CvRect_()
  {
    return *this;
  }
  
  template<typename T>
  const cv::Rect_<T>& Rectangle<T>::get_CvRect_() const
  {
    return *this;
  }
#endif
  
  template<typename T>
  T Rectangle<T>::GetX()      const
  { return x; }
  
  template<typename T>
  T Rectangle<T>::GetY()      const
  { return y; }
  
  template<typename T>
  T Rectangle<T>::GetWidth()  const
  { return width; }
  
  template<typename T>
  T Rectangle<T>::GetHeight() const
  { return height; }
  
  template<typename T>
  T Rectangle<T>::GetXmax()   const
  { return x + width; }
  
  template<typename T>
  T Rectangle<T>::GetYmax()   const
  { return y + height; }
  
  
  template<typename T>
  template<class PointContainer>
  void Rectangle<T>::initFromPointContainer(const PointContainer& points)
  {
    const size_t N = points.size();
    if(N > 0) {
      
      auto pointIter = points.begin();
      
      T xmax(pointIter->x()), ymax(pointIter->y());
      
      this->x = pointIter->x();
      this->y = pointIter->y();
      
      ++pointIter;
      while(pointIter != points.end()) {
        this->x = std::min(this->x, pointIter->x());
        this->y = std::min(this->y, pointIter->y());
        xmax = std::max(xmax, pointIter->x());
        ymax = std::max(ymax, pointIter->y());
        
        ++pointIter;
      }
      
      this->width  = xmax - this->x;
      this->height = ymax - this->y;
      
    } else {
      this->x = 0;
      this->y = 0;
      this->width = 0;
      this->height = 0;
    }
  } // Rectangle<T>::initFromPointContainer
  
  template<typename T>
  Rectangle<T>::Rectangle(const Quadrilateral<2,T>& quad)
  {
    initFromPointContainer(quad);
  }
  
  template<typename T>
  Rectangle<T>::Rectangle(const std::vector<Point<2,T> >& points)
  {
    initFromPointContainer(points);
  }
  
  template<typename T>
  template<size_t NumPoints>
  Rectangle<T>::Rectangle(const std::array<Point<2,T>,NumPoints>& points)
  {
    initFromPointContainer<std::array<Point<2,T>,NumPoints> >(points);
  }
  
  
  template<typename T>
  Rectangle<T> Rectangle<T>::Intersect(const Rectangle<T>& other) const
  {
#if ANKICORETECH_USE_OPENCV
    cv::Rect_<T> temp = this->get_CvRect_() & other.get_CvRect_();
    return Rectangle<T>(temp.x, temp.y, temp.width, temp.height);
#else
    CORETECH_THROW("Rectangle::Intersect() currently relies on OpenCV.");
#endif    
  }
  
  template<typename T>
  bool Rectangle<T>::Contains(const Point<2,T>& point) const
  {
#if ANKICORETECH_USE_OPENCV
    return this->contains(point.get_CvPoint_());
#else
    CORETECH_THROW("Rectangle::Contains() currently relies on OpenCV.");
#endif
  }
  
} // namespace Anki

#endif // _ANKICORETECH_COMMON_RECT_IMPL_H_

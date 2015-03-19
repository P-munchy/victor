/**
 * File: image.cpp
 *
 * Author: Andrew Stein
 * Date:   11/20/2014
 *
 * Description: Implements a container for images on the basestation.
 *
 * Copyright: Anki, Inc. 2014
 **/

#include "anki/common/basestation/math/point_impl.h"

#include "anki/vision/basestation/image_impl.h"

#if ANKICORETECH_USE_OPENCV
#include "opencv2/core/core.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/highgui/highgui.hpp"
#endif


namespace Anki {
namespace Vision {
  
  Image::Image()
  : Array2d<u8>()
  {
    
  }
  
  Image::Image(s32 nrows, s32 ncols)
  : Array2d<u8>(nrows, ncols)
  {
    
  }
  
  Image::Image(s32 nrows, s32 ncols, u8* data)
  : Array2d<u8>(nrows,ncols,data)
  {
    
  }
  
#if ANKICORETECH_USE_OPENCV
  Image::Image(cv::Mat_<u8>& cvMat)
  : Array2d<u8>(cvMat)
  {
    
  }
#endif
  
  void Image::Display(const char *windowName, bool pause) const
  {
#   if ANKICORETECH_USE_OPENCV
    cv::imshow(windowName, this->get_CvMat_());
    if(pause) {
      cv::waitKey();
    }
#   endif
  }

  s32 Image::GetConnectedComponents(Array2d<s32>& labelImage,
                                    std::vector<std::vector< Point2<s32> > >& regionPoints) const
  {
    // Until we start using OpenCV 3, which has an actual connected components implementation,
    // this is adapted from here:
    //    http://nghiaho.com/uploads/code/opencv_connected_component/blob.cpp
    //
    
    regionPoints.clear();
    
    // Fill the label_image with the blobs
    // 0  - background
    // 1  - unlabelled foreground
    // 2+ - labelled foreground
    
    int labelCount = 2; // starts at 2 because 0,1 are used already
    
    for(int y=0; y < labelImage.GetNumRows(); y++) {
      s32 *row = labelImage.GetRow(y); // (s32*)labelImage.ptr(y);
      for(int x=0; x < labelImage.GetNumCols(); x++) {
        if(row[x] != 1) {
          continue;
        }
        
        cv::Rect rect;
        cv::floodFill(labelImage.get_CvMat_(), cv::Point(x,y), labelCount, &rect, 0, 0, 4);
        
        std::vector<Point2<s32> > blob;
        
        for(int i=rect.y; i < (rect.y+rect.height); i++) {
          int *row2 = (int*)labelImage.GetRow(i);
          for(int j=rect.x; j < (rect.x+rect.width); j++) {
            if(row2[j] != labelCount) {
              continue;
            }
            
            blob.emplace_back(j,i);
          }
        }
        
        regionPoints.emplace_back(blob);
        
        ++labelCount;
      }
    }
    
    return labelCount-2;
    
  } // GetConnectedComponents()
  
  
  void Image::Resize(f32 scaleFactor)
  {
    cv::resize(this->get_CvMat_(), this->get_CvMat_(), cv::Size(), scaleFactor, scaleFactor, CV_INTER_LINEAR);
  }
  
  void Image::Resize(s32 desiredRows, s32 desiredCols)
  {
    if(desiredRows != GetNumRows() || desiredCols != GetNumCols()) {
      const cv::Size desiredSize(desiredCols, desiredRows);
      cv::resize(this->get_CvMat_(), this->get_CvMat_(), desiredSize, 0, 0, CV_INTER_LINEAR);
    }
  }
  
  void Image::Resize(Image& resizedImage) const
  {
    if(resizedImage.IsEmpty()) {
      printf("Image::Resize - Output image should already be the desired size.\n");
    } else {
      const cv::Size desiredSize(resizedImage.GetNumCols(), resizedImage.GetNumRows());
      cv::resize(this->get_CvMat_(), resizedImage.get_CvMat_(), desiredSize, 0, 0, CV_INTER_LINEAR);
    }
  }
  
  
  
} // namespace Vision
} // namespace Anki

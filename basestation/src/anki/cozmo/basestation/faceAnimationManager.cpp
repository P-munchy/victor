/**
 * File: faceAnimationManager.cpp
 *
 * Author: Andrew Stein
 * Date:   7/7/2015
 *
 * Description: Implements container for managing available animations for the robot's face display.
 *
 * Copyright: Anki, Inc. 2015
 **/

#include "anki/cozmo/basestation/faceAnimationManager.h"
#include "anki/vision/basestation/image.h"
#include "anki/common/basestation/utils/data/dataPlatform.h"
#include "anki/common/basestation/array2d_impl.h"
#include "anki/cozmo/robot/faceDisplayDecode.h"
#include "clad/types/animationKeyFrames.h"
#include "util/logging/logging.h"

#include <opencv2/highgui.hpp>

#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>

#if defined(WIN32) || defined(_WIN32)
#define PATH_SEPARATOR "\\"
#else
#define PATH_SEPARATOR "/"
#endif

namespace Anki {
namespace Cozmo {

  FaceAnimationManager* FaceAnimationManager::_singletonInstance = nullptr;
  const std::string FaceAnimationManager::ProceduralAnimName("_PROCEDURAL_");
  
  FaceAnimationManager::FaceAnimationManager()
  {
    _availableAnimations[ProceduralAnimName];
  }
  
  void FaceAnimationManager::removeInstance()
  {
    // check if the instance has been created yet
    if(nullptr != _singletonInstance) {
      delete _singletonInstance;
      _singletonInstance = nullptr;
    }
  }
  
  
  // Read the animations in a dir
  void FaceAnimationManager::ReadFaceAnimationDir(Util::Data::DataPlatform* dataPlatform)
  {
    if (dataPlatform == nullptr) { return; }
    const std::string animationFolder = dataPlatform->pathToResource(Util::Data::Scope::Resources, "assets/faceAnimations/");

    DIR* dir = opendir(animationFolder.c_str());
    if ( dir != nullptr) {
      dirent* ent = nullptr;
      while ( (ent = readdir(dir)) != nullptr) {
        if (ent->d_type == DT_DIR && ent->d_name[0] != '.') {
          const std::string animName(ent->d_name);
          if(animName == ProceduralAnimName) {
            PRINT_NAMED_ERROR("FaceAnimationManager.ReadFaceAnimationDir.ReservedName",
                              "'_PROCEDURAL_' is a reserved face animation name. Ignoring.");
            continue;
          }
          
          std::string fullDirName = animationFolder + ent->d_name;
          struct stat attrib{0};
          int result = stat(fullDirName.c_str(), &attrib);
          if (result == -1) {
            PRINT_NAMED_WARNING("FaceAnimationManager.ReadFaceAnimationDir",
                                "could not get mtime for %s", fullDirName.c_str());
            continue;
          }
          bool loadAnimDir = false;
          auto mapIt = _availableAnimations.find(animName);
#ifdef __APPLE__ // TODO: COZMO-1057 
            time_t tmpSeconds = attrib.st_mtimespec.tv_sec; //This maps to __darwin_time_t
#else
            time_t tmpSeconds = attrib.st_mtime;
#endif
          if (mapIt == _availableAnimations.end()) {
            _availableAnimations[animName].lastLoadedTime = tmpSeconds;
            loadAnimDir = true;
          } else {
            if (mapIt->second.lastLoadedTime < tmpSeconds) {
              mapIt->second.lastLoadedTime = tmpSeconds;
              loadAnimDir = true;
            } else {
              //PRINT_NAMED_INFO("Robot.ReadAnimationFile", "old time stamp for %s", fullFileName.c_str());
            }
          }
          if (loadAnimDir) {
            
            DIR* animDir = opendir(fullDirName.c_str());
            if(animDir != nullptr) {
              dirent* frameEntry = nullptr;
              while ( (frameEntry = readdir(animDir)) != nullptr) {
                if(frameEntry->d_type == DT_REG && frameEntry->d_name[0] != '.') {
                  
                  // Get the frame number in this filename
                  const std::string filename(frameEntry->d_name);
                  size_t underscorePos = filename.find_last_of("_");
                  size_t dotPos = filename.find_last_of(".");
                  if(dotPos == std::string::npos) {
                    PRINT_NAMED_ERROR("FaceAnimationManager.ReadFaceAnimationDir",
                                      "Could not find '.' in frame filename %s",
                                      filename.c_str());
                    return;
                  } else if(underscorePos == std::string::npos) {
                    PRINT_NAMED_ERROR("FaceAnimationManager.ReadFaceAnimationDir",
                                      "Could not find '_' in frame filename %s",
                                      filename.c_str());
                    return;
                  } else if(dotPos <= underscorePos+1) {
                    PRINT_NAMED_ERROR("FaceAnimationManager.ReadFaceAnimationDir",
                                      "Unexpected relative positions for '.' and '_' in frame filename %s",
                                      filename.c_str());
                    return;
                  }
                  
                  const std::string digitStr(filename.substr(underscorePos+1,
                                                             (dotPos-underscorePos-1)));
                  
                  s32 frameNum = 0;
                  try {
                    frameNum = std::stoi(digitStr);
                  } catch (std::invalid_argument&) {
                    PRINT_NAMED_ERROR("FaceAnimationManager.ReadFaceAnimationDir",
                                      "Could not get frame number from substring '%s' "
                                      "of filename '%s'.",
                                      digitStr.c_str(), filename.c_str());
                    return;
                  }
                  
                  if(frameNum < 0) {
                    PRINT_NAMED_ERROR("FaceAnimationManager.ReadFaceAnimationDir",
                                      "Found negative frame number (%d) for filename '%s'.",
                                      frameNum, filename.c_str());
                    return;
                  }
                  
                  AvailableAnim& anim = _availableAnimations[animName];
                  
                  // Add empty frames if there's a gap
                  if(frameNum > 1) {
                    
                    s32 emptyFramesAdded = 0;
                    while(anim.rleFrames.size() < frameNum-1) {
                      anim.rleFrames.push_back({});
                      ++emptyFramesAdded;
                    }
                    
                    /*
                    if(emptyFramesAdded > 0) {
                      PRINT_NAMED_DEBUG("FaceAnimationManager.ReadFaceAnimationDir.InsertEmptyFrames",
                                        "Inserted %d empty frames before frame %d in animation %s.",
                                        emptyFramesAdded, frameNum, animName.c_str());
                    }
                     */
                  }
                  
                  // Read the image
                  const std::string fullFilename(fullDirName + PATH_SEPARATOR + frameEntry->d_name);
                  cv::Mat_<u8> img = cv::imread(fullFilename, CV_LOAD_IMAGE_GRAYSCALE);
                  
                  if(img.rows != IMAGE_HEIGHT || img.cols != IMAGE_WIDTH) {
                    PRINT_NAMED_ERROR("FaceAnimationManager.ReadFaceAnimationDir",
                                      "Image in %s is %dx%d instead of %dx%d.",
                                      fullFilename.c_str(),
                                      img.cols, img.rows,
                                      IMAGE_WIDTH, IMAGE_HEIGHT);
                    continue;
                  }
                  
                  // Binarize
                  img = img > 128;
                  
                  // DEBUG
                  //cv::imshow("FaceAnimImage", img);
                  //cv::waitKey(30);
                  
                  anim.rleFrames.push_back({});
                  CompressRLE(img, anim.rleFrames.back());
                }
              }
            }
            
            //PRINT_NAMED_INFO("FaceAnimationManager.ReadFaceAnimationDir",
            //                 "Added %lu files/frames to animation %s",
            //                 (unsigned long)_availableAnimations[animName].GetNumFrames(),
            //                 animName.c_str());
          }
        }
      }
      closedir(dir);
    } else {
      PRINT_NAMED_INFO("FaceAnimationManager.ReadFaceAnimationDir",
                       "folder not found, no face animations read %s",
                       animationFolder.c_str());
    }
    
  } // ReadFaceAnimationDir()
  
  FaceAnimationManager::AvailableAnim* FaceAnimationManager::GetAnimationByName(const std::string& name)
  {
    auto animIter = _availableAnimations.find(name);
    if(animIter == _availableAnimations.end()) {
      PRINT_NAMED_WARNING("FaceAnimationManager.GetAnimationByName.UnknownName",
                          "Unknown animation requested: %s", name.c_str());
      return nullptr;
    } else {
      return &animIter->second;
    }
  }
  
  Result FaceAnimationManager::AddImage(const std::string& animName, const Vision::Image& faceImg)
  {
    AvailableAnim* anim = GetAnimationByName(animName);
    if(nullptr == anim) {
      return RESULT_FAIL;
    }
    
    anim->rleFrames.push_back({});
    CompressRLE(faceImg.Threshold(128), anim->rleFrames.back());
    return RESULT_OK;
  }

  Result FaceAnimationManager::ClearAnimation(const std::string& animName)
  {
    AvailableAnim* anim = GetAnimationByName(animName);
    if(anim == nullptr) {
      return RESULT_FAIL;
    } else {
      anim->rleFrames.clear();
      return RESULT_OK;
    }
  }
  
  u32 FaceAnimationManager::GetNumFrames(const std::string &animName)
  {
    AvailableAnim* anim = GetAnimationByName(animName);
    if(nullptr == anim) {
      PRINT_NAMED_WARNING("FaceAnimationManager.GetNumFrames",
                          "Unknown animation requested: %s",
                          animName.c_str());
      return 0;
    } else {
      return static_cast<u32>(anim->GetNumFrames());
    }
  } // GetNumFrames()
  
  const std::vector<u8>* FaceAnimationManager::GetFrame(const std::string& animName, u32 frameNum) const
  {
    auto animIter = _availableAnimations.find(animName);
    if(animIter == _availableAnimations.end()) {
      PRINT_NAMED_ERROR("FaceAnimationManager.GetFrame",
                        "Unknown animation requested: %s.",
                        animName.c_str());
      return nullptr;
    } else {
      const AvailableAnim& anim = animIter->second;
      
      if(frameNum < anim.GetNumFrames()) {
        
        return &(anim.rleFrames[frameNum]);
        
      } else {
        PRINT_NAMED_ERROR("FaceAnimationManager.GetFrame",
                          "Requested frame number %d is invalid. "
                          "Only %lu frames available in animatino %s.",
                          frameNum, (unsigned long)animIter->second.GetNumFrames(),
                          animName.c_str());
        return nullptr;
      }
    }
  } // GetFrame()
  
  Result FaceAnimationManager::CompressRLE(const Vision::Image& img, std::vector<u8>& rleData)
  {
    // Frame is in 8-bit RLE format:
    // 00xxxxxx   CLEAR COLUMN (x = count)
    // 01xxxxxx   REPEAT COLUMN (x = count)
    // 1xxxxxyy   RLE PATTERN (x = count, y = pattern)
    
    if(img.GetNumRows() != IMAGE_HEIGHT || img.GetNumCols() != IMAGE_WIDTH) {
      PRINT_NAMED_ERROR("FaceAnimationManager.CompressRLE",
                        "Expected %dx%d image but got %dx%d image",
                        IMAGE_WIDTH, IMAGE_HEIGHT, img.GetNumCols(), img.GetNumRows());
      return RESULT_FAIL;
    }

    uint64_t packed[IMAGE_WIDTH];

    memset(packed, 0, sizeof(packed));
    rleData.clear();

    // Convert image into 1bpp column major format
    for(s32 i=0; i<IMAGE_HEIGHT; i++) {
      const u8* pixels = img.GetRow(i);
      
      for(s32 j=0; j<IMAGE_WIDTH; j++) {
        if (!*(pixels++)) { continue ; }

        // Note the trick here to force compiler to make this 64-bit
        // (Would like to just do: 1L << i, but compiler optimizes it out...)
        packed[j] |= 0x8000000000000000L >> (i ^ 63);
      }
    }

    // Begin RLE encoding
    for(int x = 0; x < IMAGE_WIDTH; ) {
      // Clear row encoding
      if (!packed[x]) {
        int count = 0;

        for (; !packed[x] && x < IMAGE_WIDTH && count < 0x40; x++, count++) ;
        rleData.push_back(count-1);

        continue ;
      }

      // Copy row encoding
      if (x >= 1 && packed[x] == packed[x-1]) {
        int count = 0;

        for (; packed[x] == packed[x-1] && x < IMAGE_WIDTH && count < 0x40; x++, count++) ;
        rleData.push_back((count-1) | 0x40);

        continue ;
      }

      // RLE pattern encoding
      uint64_t col = packed[x++];
      int pattern = -1;
      int count = 0;

      for (int y = 0; y < IMAGE_HEIGHT; y += 2, col >>= 2) {
        if ((col & 3) != pattern) {
          // Output value if primed
          if (count > 0) {
            rleData.push_back(0x80 | ((count-1) << 2) | pattern);
          }
          
          pattern = col & 3;
          count = 1;
        } else {
          count++;
        }
      }

      // Will next column use column encoding
      bool column = !packed[x] || (x < IMAGE_WIDTH && packed[x] == packed[x-1]);

      if (!pattern && column) {
        continue ;
      }
      
      rleData.push_back(0x80 | ((count-1) << 2) | pattern);
    }

    if (rleData.size() >= static_cast<size_t>(AnimConstants::MAX_FACE_FRAME_SIZE)) { // RLE compression didn't make the image smaller so just send it raw
      rleData.resize(static_cast<size_t>(AnimConstants::MAX_FACE_FRAME_SIZE));
      uint8_t* packedPtr = (uint8_t*)packed;
      for (int i=0; i<static_cast<size_t>(AnimConstants::MAX_FACE_FRAME_SIZE); ++i) {
        rleData[i] = *packedPtr;
        packedPtr++;
      }
    }
    return RESULT_OK;
  }
  
  
  void FaceAnimationManager::DrawFaceRLE(const std::vector<u8>& rleData,
                                         Vision::Image& outImg)
  {
    outImg = Vision::Image(FaceAnimationManager::IMAGE_HEIGHT, FaceAnimationManager::IMAGE_WIDTH);
    
    // Clear the display
    outImg.FillWith(0);
    
    uint64_t decodedImg[FaceAnimationManager::IMAGE_WIDTH];
    if (rleData.size() == static_cast<size_t>(AnimConstants::MAX_FACE_FRAME_SIZE)) {
      uint8_t* packedPtr = (uint8_t*)decodedImg;
      for (int i=0; i<static_cast<size_t>(AnimConstants::MAX_FACE_FRAME_SIZE); ++i) {
        *packedPtr = rleData[i];
        packedPtr++;
      }
    }
    else
    {
      FaceDisplayDecode(rleData.data(), FaceAnimationManager::IMAGE_HEIGHT, FaceAnimationManager::IMAGE_WIDTH, decodedImg);
    }
    
    // Translate from 1-bit/pixel,column-major ordering to 1-byte/pixel, row-major
    for (u8 i = 0; i < FaceAnimationManager::IMAGE_WIDTH; ++i) {
      for (u8 j = 0; j < FaceAnimationManager::IMAGE_HEIGHT; ++j) {
        if ((decodedImg[i] >> j) & 1) {
          outImg(j,i) = 255;
        }
      }
    }
  } // DrawFaceRLE()
  
} // namespace Cozmo
} // namespace Anki

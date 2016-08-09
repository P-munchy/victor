/**
 * File: faceAnimationManager.h
 *
 * Author: Andrew Stein
 * Date:   7/7/2015
 *
 * Description: Defines container for managing available animations for the robot's face display.
 *
 * Copyright: Anki, Inc. 2015
 **/

#ifndef ANKI_COZMO_FACE_ANIMATION_MANAGER_H
#define ANKI_COZMO_FACE_ANIMATION_MANAGER_H

#include <string>
#include <vector>
#include <unordered_map>

#include "anki/common/types.h"

namespace Anki {
  
  // Forward declaration
  namespace Util {
  namespace Data {
    class DataPlatform;
  }
  }
  
  namespace Vision {
    class Image;
  }
  
namespace Cozmo {

  // NOTE: this is a singleton class
  class FaceAnimationManager
  {
  public:
    static const s32 IMAGE_WIDTH = 128;
    static const s32 IMAGE_HEIGHT = 64;
    static const std::string ProceduralAnimName;
    
    // Get a pointer to the singleton instance
    inline static FaceAnimationManager* getInstance();
    static void removeInstance();
    
    void ReadFaceAnimationDir(const Util::Data::DataPlatform* dataPlatform);

    // Get a pointer to an RLE-compressed frame for the given animation.
    // Returns nullptr if animation or frame do not exist.
    const std::vector<u8>* GetFrame(const std::string& animName, u32 frameNum) const;
    
    // Return the total number of frames in the given animation. Returns 0 if the
    // animation doesn't exist.
    u32  GetNumFrames(const std::string& animName);
    
    // Ability to add keyframes at runtime, for procedural face streaming
    Result AddImage(const std::string& animName, const Vision::Image& faceImg);
    
    // Remove all frames from an existing animation
    Result ClearAnimation(const std::string& animName);
    
    // Clear all FaceAnimations
    void Clear();
    
    // Get the total number of available animations
    size_t GetNumAvailableAnimations() const;
    
    // Convert back and forth between an OpenCV image and our compressed RLE format:
    static Result CompressRLE(const Vision::Image& image, std::vector<u8>& rleData);
    static void   DrawFaceRLE(const std::vector<u8>& rleData, Vision::Image& outImg);
    
    // To avoid burn-in this switches which scanlines to use (odd or even), e.g.
    // to be called each time we blink.
    static void SwitchInterlacing();
    
  protected:
    
    // Protected default constructor for singleton.
    FaceAnimationManager();
    
    static FaceAnimationManager* _singletonInstance;

    struct AvailableAnim {
      time_t lastLoadedTime;
      std::vector< std::pair<std::vector<u8>, std::vector<u8>> > rleFrames;
      size_t GetNumFrames() const { return rleFrames.size(); }
    };
    
    AvailableAnim* GetAnimationByName(const std::string& name);
    
    std::unordered_map<std::string, AvailableAnim> _availableAnimations;
    
    static u8 _firstScanLine;
    
  }; // class FaceAnimationManager
  
  
  //
  // Inlined Implementations
  //
  
  inline FaceAnimationManager* FaceAnimationManager::getInstance()
  {
    // If we haven't already instantiated the singleton, do so now.
    if(0 == _singletonInstance) {
      _singletonInstance = new FaceAnimationManager();
    }
    
    return _singletonInstance;
  }
  
  inline void FaceAnimationManager::Clear() {
    _availableAnimations.clear();
  }
  
  // Get the total number of available animations
  inline size_t FaceAnimationManager::GetNumAvailableAnimations() const {
    return _availableAnimations.size();
  }
  
  inline void FaceAnimationManager::SwitchInterlacing() {
    _firstScanLine = 1 - _firstScanLine;
  }
  
} // namespace Cozmo
} // namespace Anki


#endif // ANKI_COZMO_FACE_ANIMATION_MANAGER_H


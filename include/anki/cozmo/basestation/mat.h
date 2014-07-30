/**
 * File: mat.h
 *
 * Author: Andrew Stein
 * Date:   (various)
 *
 * Description: Defines a MatPiece object, which is a "mat" that Cozmo drives 
 *              around on with VisionMarkers at known locations for localization.
 *
 *              MatPiece inherits from the generic Vision::ObservableObject.
 *
 *
 * Copyright: Anki, Inc. 2014
 **/

#ifndef __Products_Cozmo__Mat__
#define __Products_Cozmo__Mat__

#include "anki/vision/basestation/observableObject.h"

#include "vizManager.h"

namespace Anki {
  
  namespace Cozmo {
    
    class MatPiece : public Vision::ObservableObject 
    {
    public:
      
      // TODO: Use a MatDefinitions file, like with blocks
      class Type : public ObjectType
      {
        Type(const std::string& name) : ObjectType(name) { }
      public:
        // Define new mat piece types here, as static const Type:
        // (Note: don't forget to instantiate each in the .cpp file)
        static const Type INVALID;
        static const Type LETTERS_4x4;
        static const Type LARGE_PLATFORM;
      };
      
      // Constructor, based on Type
      MatPiece(ObjectType type, bool isFirstPiece = false);

      //
      // Inherited Virtual Methods
      //
      
      //virtual float GetMinDim() const {return 0;}
      
      virtual MatPiece* Clone() const;
      
      virtual std::vector<RotationMatrix3d> const& GetRotationAmbiguities() const;
      
      virtual void GetCorners(const Pose3d& atPose, std::vector<Point3f>& corners) const override;
      
      virtual void Visualize() override;
      virtual void EraseVisualization() override;
      
      virtual Quad2f GetBoundingQuadXY(const Pose3d& atPose, const f32 padding_mm = 0.f) const override;
      
      static ObjectType GetTypeByName(const std::string& name);
      
      //
      // MatPiece Methods
      //

      // Return true if given pose shares a common origin and is "on" this
      // MatPiece; i.e., within its bounding box, within the specified height
      // tolerance from the top surface, and with Z axis aligned to the mat's
      // z axis.
      bool IsPoseOn(const Pose3d& pose, const f32 heightTol) const;
      
      // Returns top surface height w.r.t. the mat's current pose origin
      f32 GetDrivingSurfaceHeight() const;
      
      void SetOrigin(const Pose3d* newOrigin);
      
    protected:
      static const std::vector<RotationMatrix3d> _rotationAmbiguities;
      static const s32 NUM_CORNERS = 8;
      static const std::array<Point3f, MatPiece::NUM_CORNERS> _canonicalCorners;
      
      // x = length, y = width, z = height
      Point3f _size;
      
      VizManager::Handle_t _vizHandle;
    };
    
    
    inline MatPiece* MatPiece::Clone() const
    {
      // Create an all-new mat piece of this type, to keep all the
      // bookkeeping and pointers (e.g. the pose tree) kosher.
      MatPiece* clone = new MatPiece(this->GetType());
      
      // Move the clone to this mat piece's pose
      clone->SetPose(this->GetPose());
      
      return clone;
    }
    
  } // namespace Cozmo

} // namespace Anki

#endif // __Products_Cozmo__Mat__

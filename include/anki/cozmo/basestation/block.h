//
//  block.h
//  Products_Cozmo
//
//  Created by Andrew Stein on 8/23/13.
//  Copyright (c) 2013 Anki, Inc. All rights reserved.
//

#ifndef __Products_Cozmo__block__
#define __Products_Cozmo__block__

#include "anki/common/basestation/math/pose.h"
#include "anki/common/basestation/math/quad.h"

#include "anki/vision/basestation/observableObject.h"

#include "anki/cozmo/basestation/messages.h"

#include "anki/vision/MarkerCodeDefinitions.h"

#include "dockableObject.h"
#include "vizManager.h"

namespace Anki {
  
  // Forward Declarations:
  class Camera;
  
  namespace Cozmo {
   
    // Forward Declarations:
    class Robot;

    using FaceType = u8;
    
    //
    // Block Class
    //
    //   Representation of a physical Block in the world.
    //
    class Block : public DockableObject //Base<Block>
    {
    public:
      using Color = VIZ_COLOR_ID;
      
#include "anki/cozmo/basestation/BlockDefinitions.h"
      
      // Enumerated block types
      class Type : public ObjectType {
        Type(const std::string& name) : ObjectType(name) { }
      public:
        static const Type INVALID;
#define BLOCK_DEFINITION_MODE BLOCK_ENUM_MODE
#include "anki/cozmo/basestation/BlockDefinitions.h"
      };
      
      // NOTE: if the ordering of these is modified, you must also update
      //       the static OppositeFaceLUT.
      enum FaceName {
        FIRST_FACE  = 0,
        FRONT_FACE  = 0,
        LEFT_FACE   = 1,
        BACK_FACE   = 2,
        RIGHT_FACE  = 3,
        TOP_FACE    = 4,
        BOTTOM_FACE = 5,
        NUM_FACES
      };
      
      // "Safe" conversion from FaceType to enum FaceName (at least in Debug mode)
      //static FaceName FaceType_to_FaceName(FaceType type);
      
      enum Corners {
        LEFT_FRONT_TOP =     0,
        RIGHT_FRONT_TOP =    1,
        LEFT_FRONT_BOTTOM =  2,
        RIGHT_FRONT_BOTTOM = 3,
        LEFT_BACK_TOP =      4,
        RIGHT_BACK_TOP =     5,
        LEFT_BACK_BOTTOM =   6,
        RIGHT_BACK_BOTTOM =  7,
        NUM_CORNERS       =  8
      };
      
      Block(const ObjectType type);
      
      Block(const Block& other); 
      
      virtual ~Block();
      
      //static unsigned int get_numBlocks();
      
      // Accessors:
      const Point3f&     GetSize()   const;
      float              GetWidth()  const;  // X dimension
      float              GetHeight() const;  // Z dimension
      float              GetDepth()  const;  // Y dimension
      const std::string& GetName()   const {return _name;}
      
      //virtual float GetMinDim() const;
      //using Vision::ObservableObjectBase<Block>::GetMinDim;

      void SetSize(const float width, const float height, const float depth);
      //void SetColor(const unsigned char red, const unsigned char green, const unsigned char blue);
      void SetName(const std::string name);
      
      
      void AddFace(const FaceName whichFace,
                   const Vision::MarkerType& code,
                   const float markerSize_mm);
      
      static ObjectType GetTypeByName(const std::string& name);
      
      // Return a reference to the marker on a particular face of the block.
      // Symmetry convention: if no marker was set for the requested face, the
      // one on the opposite face is returned.  If none is defined for the
      // opposite face either, the front marker is returned.  Not having
      // a marker defined for at least the front the block is an error, (which
      // should be caught in the constructor).
      Vision::KnownMarker const& GetMarker(FaceName onFace) const;
      
      /* Defined in ObservableObject class
      // Get the block's corners at its current pose
      void GetCorners(std::array<Point3f,8>& corners) const;
      */
      // Get the block's corners at a specified pose
      virtual void GetCorners(const Pose3d& atPose, std::vector<Point3f>& corners) const override;
      
      virtual Point3f GetSameDistanceTolerance() const override;
      virtual Radians GetSameAngleTolerance() const override;
      
      // Get possible poses to start docking/tracking procedure. These will be
      // a point a given distance away from each vertical face that has the
      // specified code, in the direction orthogonal to that face.  The points
      // will be w.r.t. same parent as the block, with the Z coordinate at the
      // height of the center of the block. The poses will be paired with
      // references to the corresponding marker. Optionally, only poses/markers
      // with the specified code can be returned.
      virtual void GetPreDockPoses(const float distance_mm,
                                   std::vector<PoseMarkerPair_t>& poseMarkerPairs,
                                   const Vision::Marker::Code withCode = Vision::Marker::ANY_CODE) const override;
      
      // Return the default distance from which to start docking
      virtual f32  GetDefaultPreDockDistance() const override;
      
      // Projects the box in its current 3D pose (or a given 3D pose) onto the
      // XY plane and returns the corresponding 2D quadrilateral. Pads the
      // quadrilateral (around its center) by the optional padding if desired.
      virtual Quad2f GetBoundingQuadXY(const Pose3d& atPose, const f32 padding_mm = 0.f) const override;
      
      // Projects the box in its current 3D pose (or a given 3D pose) onto the
      // XY plane and returns the corresponding quadrilateral. Adds optional
      // padding if desired.
      Quad3f GetBoundingQuadInPlane(const Point3f& planeNormal, const f32 padding_mm) const;
      Quad3f GetBoundingQuadInPlane(const Point3f& planeNormal, const Pose3d& atPose, const f32 padding_mm) const;
      
      // Visualize using VizManager.  If preDockPoseDistance > 0, pre dock poses
      // will also be drawn
      virtual void Visualize() override;
      virtual void Visualize(VIZ_COLOR_ID color) override;
      virtual void EraseVisualization() override;

    protected:
      
      static const FaceName OppositeFaceLUT[NUM_FACES];
      
      //static ObjectType NumTypes;
      
      // Make this protected so we have to use public AddFace() method
      using Vision::ObservableObject::AddMarker;
      
      //std::map<Vision::Marker::Code, std::vector<FaceName> > facesWithCode;
      
      // LUT of the marker on each face, NULL if none specified.

      std::array<const Vision::KnownMarker*, NUM_FACES> markersByFace_;
      
      // Static const lookup table for all block specs, by block ID, auto-
      // generated from the BlockDefinitions.h file using macros
      typedef struct {
        FaceName             whichFace;
        Vision::MarkerType   code;
        f32                  size;
      } BlockFaceDef_t;
      
      typedef struct {
        std::string          name;
        Block::Color         color;
        Point3f              size;
        std::vector<BlockFaceDef_t> faces;
      } BlockInfoTableEntry_t;
      
      static const std::map<ObjectType, BlockInfoTableEntry_t> BlockInfoLUT_;
      static const std::map<std::string, Block::Type> BlockNameToTypeMap;
      
      static const std::array<Point3f,NUM_FACES> CanonicalDockingPoints;
      
      static const std::array<Point3f,NUM_CORNERS> CanonicalCorners;
      
      constexpr static const f32 PreDockDistance = 100.f;
      
      Color       _color;
      Point3f     _size;
      std::string _name;
      
      VizManager::Handle_t _vizHandle;
      
      //std::vector<Point3f> blockCorners_;
      
    }; // class Block
    
    
    // prefix operator (++fname)
    Block::FaceName& operator++(Block::FaceName& fname);
    
    // postfix operator (fname++)
    Block::FaceName operator++(Block::FaceName& fname, int);
    
    
    
    // A cubical block with the same marker on all sides.
    class Block_Cube1x1 : public Block
    {
    public:
      
      Block_Cube1x1(Type type);
      
      virtual std::vector<RotationMatrix3d> const& GetRotationAmbiguities() const override;
      
      virtual Block_Cube1x1* Clone() const override
      {
        // Call the copy constructor
        return new Block_Cube1x1(*this);
      }
      
    protected:
      //static const ObjectType BlockType;
      static const std::vector<RotationMatrix3d> rotationAmbiguities_;
      
    };
    
    // Long dimension is along the x axis (so one unique face has x axis
    // sticking out of it, the other unique face type has y and z axes sticking
    // out of it).  One marker on
    class Block_2x1 : public Block
    {
    public:
      Block_2x1(Type type);
      
      virtual std::vector<RotationMatrix3d> const& GetRotationAmbiguities() const override;
      
      virtual Block_2x1* Clone() const override
      {
        // Call the copy constructor
        return new Block_2x1(*this);
      }
      
    protected:
      //static const ObjectType BlockType;
      static const std::vector<RotationMatrix3d> rotationAmbiguities_;
      
    };
#pragma mark --- Inline Accessors Implementations ---
    
       

    //
    // Block:
    //
    /*
    inline BlockID_t Block::GetID() const
    { return this->blockID_; }
    */
    
    inline Point3f const& Block::GetSize() const
    { return _size; }
    
    inline float Block::GetWidth() const
    { return _size.y(); }
    
    inline float Block::GetHeight() const
    { return _size.z(); }
    
    inline float Block::GetDepth() const
    { return _size.x(); }
    
    /*
    inline float Block::GetMinDim() const
    {
      return std::min(GetWidth(), std::min(GetHeight(), GetDepth()));
    }
     */
    
    inline void Block::SetSize(const float width,
                               const float height,
                               const float depth)
    {
      _size = {width, height, depth};
    }
    
    /*
    inline void Block::SetColor(const unsigned char red,
                                const unsigned char green,
                                const unsigned char blue)
    {
      _color = {red, green, blue};
    }
     */
    
    inline void Block::SetName(const std::string name)
    {
      _name = name;
    }
    
    /*
    inline void Block::SetPose(const Pose3d &newPose)
    { this->pose_ = newPose; }
    */
    
    /*
    inline const BlockMarker3d& Block::get_faceMarker(const FaceName face) const
    { return this->markers[face]; }
    */
    
    /*
    inline Block::FaceName Block::FaceType_to_FaceName(FaceType type)
    {
      CORETECH_ASSERT(type > 0 && type < NUM_FACES+1);
      return static_cast<FaceName>(type-1);
    }
     */
    
  } // namespace Cozmo
} // namespace Anki

#endif // __Products_Cozmo__block__
/**
 * File: observableObject.h
 *
 * Author: Andrew Stein
 * Date:   (various)
 *
 * Description: Defines an abstract ObservableObject class, which is an 
 *              general 3D object, with type, ID, and pose, and a set of Markers
 *              on it at known locations.  Thus, it is "observable" by virtue of
 *              having these markers, and its 3D / 6DoF pose can be estimated by
 *              matching up ObservedMarkers with the KnownMarkers it possesses.
 *
 *
 * Copyright: Anki, Inc. 2014
 **/

#ifndef ANKI_VISION_OBSERVABLE_OBJECT_H
#define ANKI_VISION_OBSERVABLE_OBJECT_H

#include <list>
#include <set>
#include "util/math/constantsAndMacros.h"

#include "anki/vision/basestation/visionMarker.h"

#include "anki/common/basestation/objectIDs.h"

#include "anki/common/basestation/colorRGBA.h"

#include "anki/common/basestation/math/point_impl.h"



namespace Anki {
  namespace Vision {
    
    // Forward declaration
    class Camera;
    
    // A marker match is a pairing of an ObservedMarker with a KnownMarker
    using MarkerMatch = std::pair<const Vision::ObservedMarker*, const Vision::KnownMarker*>;
    
    // Pairing of a pose and the match which implies it
    using PoseMatchPair = std::pair<Pose3d, MarkerMatch>;
    
    class ObservableObject
    {
    public:
      
      enum class PoseState
      {
        Known,
        Dirty,
        Unknown
      };
      
      
      // Do we want to be req'd to instantiate with all codes up front?
      ObservableObject();
      
      // For creating a fresh new derived object from a pointer to this base class.
      // NOTE: This just creates a fresh object, and does not copy the original
      //  object's pose, ID, or other state.
      virtual ObservableObject* CloneType() const = 0;
      
      virtual ~ObservableObject(){};
      
      // Specify the extistence of a marker with the given code at the given
      // pose (relative to the object's origin), and the specfied size in mm
      Vision::KnownMarker const& AddMarker(const Marker::Code& withCode,
                                           const Pose3d&       atPose,
                                           const Point2f&      size_mm);
      
      std::list<KnownMarker> const& GetMarkers() const {return _markers;}
      
      // Return a const reference to a vector all this object's Markers with the
      // specified code. The returned vector will be empty if there are no markers
      // with that code.
      std::vector<KnownMarker*> const& GetMarkersWithCode(const Marker::Code& whichCode) const;
      
      // Populate a vector of const pointers to all this object's markers that
      // have been observed since the specified time. The vector will be unchanged
      // if no markers have been observed since then. Note that the vector is
      // not cleared internally, so this method _adds_ markers to existing
      // container. It is the caller's responsibility to clear the vector if
      // desired.
      void GetObservedMarkers(std::vector<const KnownMarker*>& observedMarkers,
                              const TimeStamp_t sinceTime) const;
      
      // Same as above, but uses GetLastObservedTime() as "sinceTime"
      void GetObservedMarkers(std::vector<const KnownMarker*>& observedMarkers) const;

      // Updates the observation times of this object's markers with the newer
      // of the current times and the times of the corresponding markers on the
      // other object. If the other object is not the same type, RESULT_FAIL
      // is returned.
      Result UpdateMarkerObservationTimes(const ObservableObject& otherObject);
      
      // If object is moving, returns true and the time that it started moving in t.
      // If not moving, returns false and the time that it stopped moving in t.
      virtual bool IsMoving(TimeStamp_t* t = nullptr) const { return false; }
      
      // Set the moving state of the object and when it either started or stopped moving.
      virtual void SetIsMoving(bool isMoving, TimeStamp_t t) {};
      
      // Override for objects that can be used for localization (e.g., mats
      // or active blocks that have not moved since last localization)
      virtual bool CanBeUsedForLocalization() const { return false; }
      
      virtual bool IsMoveable() const { return true; }
      
      // Add possible poses implied by seeing the observed marker to the list.
      // Each pose will be paired with a pointer to the known marker on this
      // object from which it was computed.
      // If the marker doesn't match any found on this object, the possiblePoses
      // list will not be modified.
      void ComputePossiblePoses(const ObservedMarker*     obsMarker,
                                std::vector<PoseMatchPair>& possiblePoses) const;

      // Sets all markers with the specified code as having been observed
      // at the given time
      void SetMarkersAsObserved(const Marker::Code& withCode,
                                const TimeStamp_t atTime);
      
      // Set the marker whose centroid projects closest to the observed marker's
      // centroid (within threshold distance and within areaRatio threshold)
      // to have the given observed time.
      void SetMarkerAsObserved(const ObservedMarker* nearestTo,
                               const TimeStamp_t     atTime,
                               const f32             centroidDistThreshold = 5.f, // in pixels
                               const f32             areaRatioThreshold = 0.1f);  // i.e., 1 - abs(obsArea/knownArea) < threshold
      
      // Return true if any of the object's markers is visible from the given
      // camera. See also KnownMarker::IsVisibleFrom().
      bool IsVisibleFrom(const Camera& camera,
                         const f32     maxFaceNormalAngle,
                         const f32     minMarkerImageSize,
                         const bool    requireSomethingBehind,
                         const u16     xBorderPad = 0,
                         const u16     yBorderPad = 0) const;
      
      // Same as above, except requireSomethingBehind==true.
      // If the function returns false, then hasNothingBehind contains whether
      // or not any marker had nothing behind it. If so, then we would have
      // returned true for this function if not for that reason.
      bool IsVisibleFrom(const Camera& camera,
                         const f32     maxFaceNormalAngle,
                         const f32     minMarkerImageSize,
                         const u16     xBorderPad,
                         const u16     yBorderPad,
                         bool& hasNothingBehind) const;
                         
      
      // Accessors:
      ObjectID           GetID()     const;
      const Pose3d&      GetPose()   const;
      const ColorRGBA&   GetColor()  const;
      //virtual float GetMinDim() const = 0;
      
      // Auto-set ID to unique value
      void SetID();
      
      // For special situations where automatic unique ID is not desired, use this
      // method to deliberately copy the ID from another object
      void CopyID(const ObservableObject* fromOther);
            
      void SetColor(const ColorRGBA& color);
      void SetPose(const Pose3d& newPose, f32 fromDistance = -1.f, PoseState newPoseState = PoseState::Known);
      void SetPoseParent(const Pose3d* newParent);
      
      // Returns last "fromDistance" supplied to SetPose(), or -1 if none set.
      f32 GetLastPoseUpdateDistance() const;
      
      void SetLastObservedTime(TimeStamp_t t);
      const TimeStamp_t GetLastObservedTime() const;
      u32 GetNumTimesObserved() const;
      
      // For tracking how many times we expected to see this object but didn't.
      // Counter will reset each time SetLastObservedTime is called (i.e. at the same
      // time that num times _observed_ gets incremented).
      u32 GetNumTimesUnobserved() const;
      void IncrementNumTimesUnobserved();
      
      // Copy observation times from another object, keeping the max counts / latest times
      void SetObservationTimes(const ObservableObject* otherObject);
      
      // Return true if this object is the same as the other. Sub-classes can
      // overload this function to provide for rotational ambiguity when
      // comparing, e.g. for cubes or other blocks.
      bool IsSameAs(const ObservableObject&  otherObject,
                    const Point3f&  distThreshold,
                    const Radians&  angleThreshold) const;
      
      // Same as above, but returns translational and angular difference
      bool IsSameAs(const ObservableObject& otherObject,
                    const Point3f& distThreshold,
                    const Radians& angleThreshold,
                    Point3f& Tdiff,
                    Radians& angleDiff) const;
      
      // Same as other IsSameAs() calls above, except the tolerance thresholds
      // given by the virtual members below are used (so that per-object-class
      // thresholds can be defined)
      bool IsSameAs(const ObservableObject& otherObject) const;
      
      // Return the dimensions of the object's bounding cube in its canonical pose
      virtual const Point3f& GetSize() const = 0;
      
      /*
      // Return the bounding cube dimensions in the specified pose (i.e., apply the
      // rotation of the given pose to the canonical cube). If no pose is supplied,
      // the object's current pose is used.
      virtual Point3f GetRotatedBoundingCube(const Pose3d& atPose) const = 0;
      Point3f GetRotatedBoundingCube() const { return GetRotatedBoundingCube(GetPose()); }
      */
      
      // Return the same-distance tolerance to use in the X/Y/Z dimensions.
      // The default implementation simply uses the canonical bounding cube,
      // rotated to the object's current pose.
      constexpr static f32 DEFAULT_SAME_DIST_TOL_FRACTION = 0.8f; // fraction of GetSize() to use
      virtual Point3f GetSameDistanceTolerance() const;
      
      // Return the same angle tolerance for matching. Default is 45 degrees.
      virtual Radians GetSameAngleTolerance() const { return DEG_TO_RAD(45); }
      
      virtual std::vector<RotationMatrix3d> const& GetRotationAmbiguities() const;
      
      virtual void GetCorners(std::vector<Point3f>& corners) const;
      virtual void GetCorners(const Pose3d& atPose, std::vector<Point3f>& corners) const;
      
      virtual void Visualize() const; // using internal ColorRGBA
      virtual void Visualize(const ColorRGBA& color) const = 0; // using specified color
      virtual void EraseVisualization() const = 0;
      
      Quad2f GetBoundingQuadXY(const f32 padding_mm = 0.f) const;
      virtual Quad2f GetBoundingQuadXY(const Pose3d& atPose, const f32 padding_mm = 0.f) const;
      
      // Check whether the object's current pose is resting flat on one of its
      // sides (within the given tolerance). This means it is allowed to have any
      // orientation around the Z axis (w.r.t. its parent), but no rotation about the
      // X and Y axes.
      bool IsRestingFlat(const Radians& angleTol = DEG_TO_RAD(10)) const;

      PoseState GetPoseState() const { return _poseState; }
      void SetPoseState(PoseState newState) { _poseState = newState; }
      bool IsPoseStateKnown() const { return _poseState == PoseState::Known; }
      bool IsPoseStateUnknown() const { return _poseState == PoseState::Unknown; }

      static const char* PoseStateToString(const PoseState& state);
      
    protected:
      
      // Canonical corners are properties of each derived class and define the
      // objects' shape, in a canonical (unrotated, untranslated) state.
      virtual const std::vector<Point3f>& GetCanonicalCorners() const = 0;
      
      ObjectID     _ID;
      TimeStamp_t  _lastObservedTime = 0;
      u32          _numTimesObserved = 0;
      u32          _numTimesUnobserved = 0;
      ColorRGBA    _color;
      PoseState    _poseState = PoseState::Unknown;
      
      // Using a list here so that adding new markers does not affect references
      // to pre-existing markers
      std::list<KnownMarker> _markers;
      
      // Holds a LUT (by code) to all the markers of this object which have that
      // code.
      std::map<Marker::Code, std::vector<KnownMarker*> > _markersWithCode;
      
      // For subclasses can get a modifiable pose reference
      Pose3d& GetNonConstPose() { return _pose; }
      
    private:
      // Force setting of pose through SetPose() to keep pose name updated
      Pose3d _pose;
      f32    _lastSetPoseDistance = -1.f;
      
      // Don't allow a copy constuctor, because it won't handle fixing the
      // marker pointers and pose parents
      //ObservableObject(const ObservableObject& other);
      
    };
  
    
    //
    // Inline accessors
    //
    
    inline ObjectID ObservableObject::GetID() const {
      return _ID;
    }
    
    inline const ColorRGBA& ObservableObject::GetColor() const {
      return _color;
    }
    
    inline const Pose3d& ObservableObject::GetPose() const {
      return _pose;
    }
    
    inline void ObservableObject::SetID() { //const ObjectID newID) {
      _ID.Set();
    }
    
    inline void ObservableObject::CopyID(const ObservableObject* other) {
      _ID = other->GetID();
    }
  
    inline void ObservableObject::SetPose(const Pose3d& newPose, f32 fromDistance, PoseState newPoseState) {
      _pose = newPose;
      
      SetPoseState(newPoseState);
      
      std::string poseName("Object");
      poseName += "_" + std::to_string(GetID().GetValue());
      _pose.SetName(poseName);
      
      if(fromDistance >= 0.f) {
        _lastSetPoseDistance = fromDistance;
      }
    }
    
    inline f32 ObservableObject::GetLastPoseUpdateDistance() const {
      return _lastSetPoseDistance;
    }
    
    inline void ObservableObject::SetPoseParent(const Pose3d* newParent) {
      _pose.SetParent(newParent);
    }
    
    inline bool ObservableObject::IsSameAs(const ObservableObject& otherObject) const {
      return IsSameAs(otherObject, this->GetSameDistanceTolerance(), this->GetSameAngleTolerance());
    }
    
    inline void ObservableObject::SetColor(const Anki::ColorRGBA &color) {
      _color = color;
    }
    
    inline void ObservableObject::Visualize() const {
      Visualize(_color);
    }
    
    inline Quad2f ObservableObject::GetBoundingQuadXY(const f32 padding_mm) const
    {
      return GetBoundingQuadXY(_pose, padding_mm);
    }
    
    inline Point3f ObservableObject::GetSameDistanceTolerance() const {
      // TODO: This is only ok because we're only using totally symmetric 1x1x1 blocks at the moment.
      //       The proper way to do the IsSameAs check is to have objects return a scaled down bounding box of themselves
      //       and see if the the origin of the candidate object is within it.
      Point3f distTol(GetSize() * DEFAULT_SAME_DIST_TOL_FRACTION);
      return distTol;
    }
    
    inline bool ObservableObject::IsSameAs(const ObservableObject& otherObject,
                                           const Point3f& distThreshold,
                                           const Radians& angleThreshold) const
    {
      Point3f Tdiff;
      Radians angleDiff;
      return IsSameAs(otherObject, distThreshold, angleThreshold,
                      Tdiff, angleDiff);
    }
    
    inline const TimeStamp_t ObservableObject::GetLastObservedTime() const {
      return _lastObservedTime;
    }
    
    inline u32 ObservableObject::GetNumTimesObserved() const {
      return _numTimesObserved;
    }

    inline void ObservableObject::SetLastObservedTime(TimeStamp_t t) {
      _lastObservedTime = t;
      ++_numTimesObserved;
      _numTimesUnobserved = 0;
    }

    inline u32 ObservableObject::GetNumTimesUnobserved() const {
      return _numTimesUnobserved;
    }
    
    inline void ObservableObject::IncrementNumTimesUnobserved() {
      ++_numTimesUnobserved;
    }
    
    inline void ObservableObject::GetObservedMarkers(std::vector<const KnownMarker*>& observedMarkers) const {
      GetObservedMarkers(observedMarkers, GetLastObservedTime());
    }
    
    
  } // namespace Vision
} // namespace Anki

#endif // ANKI_VISION_OBSERVABLE_OBJECT_H

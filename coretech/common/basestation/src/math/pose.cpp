
#include "anki/common/basestation/math/pose.h"

#include "anki/common/basestation/math/matrix_impl.h"
#include "anki/common/basestation/math/point_impl.h"
#include "anki/common/basestation/math/poseBase_impl.h"
#include "anki/common/basestation/math/quad_impl.h"

#include "anki/common/shared/utilities_shared.h"

#include <stdexcept>

namespace Anki {

  
#pragma mark --- Pose2d Implementations ---
  
  Pose2d::Pose2d()
  : Pose2d(0, {{0.f, 0.f}})
  {
    
  }
  
  Pose2d::Pose2d(const Radians &theta, const Point2f &t)
  : _translation(t)
  , _angle(theta)
  , _planeNormal(Z_AXIS_3D)
  {
    
  }
  
  Pose2d::Pose2d(const Radians &theta, const float x, const float y)
  : _translation(x,y)
  , _angle(theta)
  , _planeNormal(Z_AXIS_3D)
  {
    
  }
  
  Pose2d::Pose2d(const Pose3d& pose3d)
  : _translation(pose3d.GetTranslation().x(), pose3d.GetTranslation().y())
  , _angle(pose3d.GetRotationAngle<'Z'>())
  , _planeNormal(Z_AXIS_3D)
  {
    
  }
  
  void Pose2d::operator*=(const Pose2d &other)
  {
    _angle += other._angle;
    
    _translation += this->GetRotationMatrix() * other._translation;
    
  }
  
  Pose2d Pose2d::operator* (const Pose2d &other) const
  {
    Radians newAngle(_angle + other._angle);
    RotationMatrix2d newRotation(newAngle);
    Point2f newTranslation(newRotation * other._translation);
    newTranslation += _translation;
    
    return Pose2d(newAngle, newTranslation);
  }
  
  void Pose2d::PreComposeWith(const Pose2d &other)
  {
    _angle += other._angle;

    _translation = other.GetRotationMatrix() * _translation;
    _translation += other._translation;
  }
  
  Pose2d Pose2d::GetInverse(void) const
  {
    Pose2d returnPose(*this);
    returnPose.Invert();
    return returnPose;
  }
  
  Pose2d& Pose2d::Invert(void)
  {
    _angle *= -1.f;
    RotationMatrix2d R(_angle);

    _translation *= -1.f;
    _translation = R * _translation;
    
    return *this;
  }
    
  
#pragma mark --- Pose3d Implementations ---
  
  Pose3d::Pose3d()
  : Pose3d(0, Z_AXIS_3D, {{0.f, 0.f, 0.f}})
  {
    
  } // Constructor: Pose3d()  
  
  Pose3d::Pose3d(const RotationVector3d &Rvec_in, const Vec3f &T_in, const Pose3d *parentPose)
  : PoseBase<Pose3d>(parentPose)
  , _rotationMatrix(Rvec_in)
  , _translation(T_in)
  {

  } // Constructor: Pose3d(Rvec, T)
  
  /* TODO: Add this constructor once we're using covariance
  Pose3d::Pose3d(const RotationVector3d &Rvec_in, const Vec3f &T_in,
             const Matrix<float> &cov_in, const Pose3d *parentPose)
  : Pose3d(Rvec_in, T_in, parentPose)
  {
    covariance = cov_in;
  } // Constructor: Pose3d(Rvec, T, cov)
  */
  
  Pose3d::Pose3d(const RotationMatrix3d &Rmat_in, const Vec3f &T_in, const Pose3d *parentPose)
  : PoseBase<Pose3d>(parentPose)
  , _rotationMatrix(Rmat_in)
  , _translation(T_in)
  {

  } // Constructor: Pose3d(Rmat, T)
  
  Pose3d::Pose3d(const Radians angle, const Vec3f axis,
                 const Vec3f T, const Pose3d *parentPose)
  : PoseBase<Pose3d>(parentPose)
  , _rotationMatrix(angle, axis)
  , _translation(T)
  {
    
  } // Constructor: Pose3d(angle, axis, T)
  
  Pose3d::Pose3d(const Pose3d &otherPose)
  : Pose3d(otherPose._rotationMatrix, otherPose._translation, otherPose._parent)
  {
    
  }
  
  Pose3d::Pose3d(const Pose2d &pose2d)
  : Pose3d(pose2d.GetAngle(), {{0.f, 0.f, 0.f}},
           {{pose2d.GetX(), pose2d.GetY(), 0.f}})
  {
    // At this point, we have initialized a 3D pose corresponding
    // just to the 2D pose (i.e. w.r.t. the (0,0,0) origin and
    // with rotation around the Z axis).
    //
    // Next compute the 3D pose of the plane in which the 2D Pose
    // is embedded.  Then compose the two poses to get the final
    // 3D pose.
    
    const Vec3f Zaxis(0.f, 0.f, 1.f);
    
    const float dotProduct = DotProduct(pose2d.GetPlaneNormal(), Zaxis);
    
    CORETECH_ASSERT(std::abs(dotProduct) <= 1.f);
    
    // We will rotate the pose2d's plane normal into the Z axis.
    // The dot product gives us the angle needed to do that, and
    // the cross product gives us the axis around which we will
    // rotate.
    Radians angle3d = std::acos(dotProduct);
    Vec3f   axis3d  = CrossProduct(Zaxis, pose2d.GetPlaneNormal());
    
    Pose3d planePose(angle3d, axis3d, pose2d.GetPlaneOrigin());
    this->PreComposeWith(planePose);
    
  } // Constructor: Pose3d(Pose2d)

  
#pragma mark --- Operator Overloads ---
  // Composition: this = this*other
  void Pose3d::operator*=(const Pose3d &other)
  {
   
    // this.T = this.R*other.T + this.T;
    Vec3f thisTranslation(_translation); // temp storage
    _translation = _rotationMatrix * other._translation;
    _translation += thisTranslation;

    // this.R = this.R * other.R
    // Note: must do this _after_ the translation update, since that uses this.R
    _rotationMatrix *= other._rotationMatrix;

  } // operatore*=(Pose3d)
  
  // Composition: new = this * other
  Pose3d Pose3d::operator*(const Pose3d &other) const
  {
    Vec3f newTranslation = _rotationMatrix * other._translation;
    newTranslation += _translation;
    
    RotationMatrix3d newRotation(_rotationMatrix);
    newRotation *= other._rotationMatrix;
    
    return Pose3d(newRotation, newTranslation);
  } // operator*(Pose3d)
  
  
  bool Pose3d::operator==(const Pose3d &other) const
  {
    return (_rotationMatrix == other._rotationMatrix &&
            _translation == other._translation);
  }
  
  // Composition: this = other * this
  void Pose3d::PreComposeWith(const Pose3d &other)
  {
    _rotationMatrix.preMultiplyBy(other._rotationMatrix);
    SetRotation(_rotationMatrix); // keep Rvec and Rmat in sync
    _translation = other._rotationMatrix * _translation;
    _translation += other._translation;
  }
  
  
#pragma mark --- Member Methods ---
  Pose3d Pose3d::GetInverse(void) const
  {
    Pose3d returnPose(*this);
    returnPose.Invert();
    return returnPose;
  }
  
  Pose3d& Pose3d::Invert(void)
  {
    _rotationMatrix.Transpose();
    _translation *= -1.f;
    _translation = _rotationMatrix * _translation;
    
    return *this;
  }
  
  void Pose3d::RotateBy(const Radians& angleIn) {
    // Keep same rotation axis, but add the incoming angle
    RotationVector3d Rvec(_rotationMatrix);
    RotationMatrix3d Rnew(angleIn, Rvec.GetAxis());
    _translation = Rnew * _translation;
    Rnew *= _rotationMatrix;
    SetRotation(Rnew);
  }
  
  void Pose3d::RotateBy(const RotationVector3d& Rvec)
  {
    RotationMatrix3d Rnew(Rvec);
    _translation = Rnew * _translation;
    Rnew *= _rotationMatrix;
    SetRotation(Rnew);
  }
  
  void Pose3d::RotateBy(const RotationMatrix3d& Rmat)
  {
    _translation = Rmat * _translation;
    _rotationMatrix.preMultiplyBy(Rmat);
    SetRotation(_rotationMatrix);
  }


  
  /*
  unsigned int Pose2d::GetTreeDepth(void) const
  {
    return getTreeDepthHelper<Pose2d>(this);
  } // GetTreeDepth()

  unsigned int Pose3d::GetTreeDepth(void) const
  {
    return getTreeDepthHelper<Pose3d>(this);
  } // GetTreeDepth()
  */
  
  
  
  /*
  Pose2d Pose2d::GetWithRespectTo(const Anki::Pose2d *otherPose) const
  {
    return GetWithRespectToHelper<Pose2d>(this, otherPose);
  }
  
  Pose3d Pose3d::GetWithRespectTo(const Anki::Pose3d *otherPose) const
  {
    return GetWithRespectToHelper<Pose3d>(this, otherPose);
  }
   */
  
  bool Pose3d::IsSameAs(const Pose3d& P_other,
                        const float distThreshold,
                        const Radians angleThreshold,
                        Pose3d& P_diff) const
  {
    bool isSame = false;
    
    // Compute the transformation that takes P1 to P2
    // Pdiff = P_other * inv(P_this)
    P_diff = this->GetInverse();
    P_diff.PreComposeWith(P_other);
    
    // First, check to see if the translational difference between the two
    // poses is small enough to call them a match
    if(P_diff.GetTranslation().Length() < distThreshold) {
      
      // Next check to see if the rotational difference is small
      RotationVector3d Rvec(P_diff.GetRotationMatrix());
      if(Rvec.GetAngle() < angleThreshold) {
        isSame = true;
      }

    } // if translation component is small enough
    
    return isSame;

  } // IsSameAs()
  
  
  bool Pose3d::IsSameAs_WithAmbiguity(const Pose3d& P_other,
                                      const std::vector<RotationMatrix3d>& R_ambiguities,
                                      const float   distThreshold,
                                      const Radians angleThreshold,
                                      const bool    useAbsRotation,
                                      Pose3d& P_diff) const
  {
    bool isSame = false;

    // P_this represents the canonical/reference pose after some arbitrary
    // transformation, T:
    //    P_this = T * P_ref
    //
    // If P_other is another ambigously-rotated version of this canonical pose
    // that has undergone the same transformation, T, then it is:
    //    P_other = T * [R_amb | 0] * P_ref
    //
    // So we compute P_diff = inv(P_this) * P_other.  If the above is true, then
    // P_diff is equivalent to:
    //    P_diff = inv(P_ref) * inv(T) * T * P_amb * P_ref
    //           = inv(P_ref) * [R_amb | 0] * P_ref
    //
    // Without loss of generality, we can assume P_ref is the identity
    // transformation (or, equivalently, that the input poses -- this and other
    // have been pre-adjusted by inv(P_ref) before calling this function). In
    // that case, P_diff = [R_amb | 0].  
    //
    
    P_diff = this->GetInverse();
    P_diff *= P_other;
    
    // First, check to see if the translational difference between the two
    // poses is small enough to call them a match
    if(P_diff.GetTranslation().Length() < distThreshold) {
      
      // Next check to see if the rotational difference is small
      RotationVector3d Rvec(P_diff.GetRotationMatrix());
      if(Rvec.GetAngle() < angleThreshold) {
        // Rotation is same, without even considering the ambiguities
        isSame = true;
      }
      else {
        // Need to consider ambiguities...
        
        RotationMatrix3d R(P_diff.GetRotationMatrix());
        
        if(useAbsRotation) {
          // The ambiguities are assumed to be defined up various sign flips
          R.Abs();
        }
        
        // Check to see if the rotational part of the pose difference is
        // similar enough to one of the rotational ambiguities
        for(auto R_ambiguity : R_ambiguities) {
          if(R.GetAngleDiffFrom(R_ambiguity) < angleThreshold) {
            isSame = true;
            break;
          }
        }
      }
    } // if translation component is small enough
    
    return isSame;
    
  } // IsSameAs_WithAmbiguity()
  
  
  
  void Pose3d::Print() const
  {
    CoreTechPrint("Point (%f, %f %f), RotVec (%f %f %f), RotAng %f rad, parent 0x%x\n",
                  _translation.x(), _translation.y(), _translation.z(),
                  GetRotationAxis().x(), GetRotationAxis().y(), GetRotationAxis().z(),
                  GetRotationAngle().ToFloat(),
                  _parent
                  );
  }
  
  
#pragma mark --- Global Functions ---
  
  float ComputeDistanceBetween(const Pose3d& pose1, const Pose3d& pose2)
  {
    // Compute distance between the two poses' translation vectors
    // TODO: take rotation into account?
    Vec3f distVec(pose1.GetTranslation());
    distVec -= pose2.GetTranslation();
    const float dist = distVec.Length();
    return dist;
  }

  


  
} // namespace Anki

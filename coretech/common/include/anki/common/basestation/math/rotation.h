/**
 * File: rotation.h
 *
 * Author: Andrew Stein (andrew)
 * Created: 8/23/2013
 *
 * Information on last revision to this file:
 *    $LastChangedDate$
 *    $LastChangedBy$
 *    $LastChangedRevision$
 *
 * Description: Implements objects for storing rotation operations, in two and
 *              three dimensions.
 *
 *              RotationMatrix2d and RotationMatrix3d are subclasses of
 *              SmallSquareMatrix for storing 2x2 and 3x3 rotation matrices, 
 *              respectively.
 *
 *              RotationVector3d stores an angle+axis rotation vector.
 *
 *              Rodrigues functions are provided to convert between 
 *              RotationMatrix3d and RotationVector3d containers.
 *
 * Copyright: Anki, Inc. 2013
 *
 **/

#ifndef __CoreTech_Math__rotation__
#define __CoreTech_Math__rotation__

#include "anki/common/shared/radians.h"
#include "matrix.h"
#include "point.h"

namespace Anki {
  
  template<MatDimType DIM>
  class RotationMatrixBase : public SmallSquareMatrix<DIM, float>
  {
  public:
    RotationMatrixBase(); // init to identity matrix
    RotationMatrixBase(const SmallSquareMatrix<DIM, float> &matrix);
    RotationMatrixBase(std::initializer_list<float> initValues);
    
    // Matrix multiplication operations just call base class functions but then
    // also make sure things stay normalized
    RotationMatrixBase<DIM>  operator* (const RotationMatrixBase<DIM>& R_other) const;
    RotationMatrixBase<DIM>& operator*=(const RotationMatrixBase<DIM>& R_other);
    RotationMatrixBase<DIM>& PreMultiplyBy(const RotationMatrixBase<DIM>& R_other);
    
    // Matrix inversion and transpose just negate the change the sign of the
    // rotation angle
    RotationMatrixBase<DIM>& Transpose(void);
    void              GetTranspose(RotationMatrixBase<DIM>& outTransposed) const;
    RotationMatrixBase<DIM>& Invert(void); // same as transpose
    void              GetInverse(RotationMatrixBase<DIM>& outInverted) const;

    
    bool IsValid(const float tolerance = 1e-6f) const;
    
  protected:
    constexpr static const float OrthogonalityToleranceLow  = 1e-6f;
    constexpr static const float OrthogonalityToleranceHigh = 1e-2f;
    
    // Keep this an orthogonal matrix.  Throw exception if things get too
    // far from orthogonal, i.e. if any of the rows' norms are more than
    // OrthogonalityTolerance from 1.f.
    void Renormalize();
    
  }; // class RotationMatrixBase
  
  
  class RotationMatrix2d : public RotationMatrixBase<2>
  {
  public:
    RotationMatrix2d();
    RotationMatrix2d(const Radians angle);
    RotationMatrix2d(const Matrix_2x2f &matrix2x2);
    RotationMatrix2d(std::initializer_list<float> initVals);
    
  }; // class RotationMatrix2d
  
  // Forward declaratin:
  class RotationMatrix3d;
  
  
  class RotationVector3d
  {
  public:
    RotationVector3d(); // no rotation around z axis
    RotationVector3d(const Radians angle, const Vec3f &axis);
    RotationVector3d(const Vec3f &rvec);
    RotationVector3d(const RotationMatrix3d &rmat);
    
    bool operator==(const RotationVector3d &other) const;
    
    // Accessors for angle and axis.  Note that it is more efficient
    // to request both simultaneously if you need both, because the
    // angle must be computed to get the axis anyway.
    Radians       GetAngle() const;
    const Vec3f&  GetAxis()  const;
    void          GetAngleAndAxis(Radians &angle, Vec3f &axis) const;
    
  private:
    Radians _angle;
    Vec3f   _axis; // unit vector
    
  }; // class RotationVector3d
  
  // A class for working with UnitQuaternions.
  // Type T must be float or double.
  template<typename T>
  class UnitQuaternion : public Point<4,T>
  {
  public:
    UnitQuaternion();
    UnitQuaternion(const UnitQuaternion& other);
    UnitQuaternion(const T w, const T x, const T y, const T z); // will normalize the inputs
    
    // Named accessors for the four elements of the quatnerion (w,x,y,z)
    inline T w() const { return this->operator[](0); }
    inline T x() const { return this->operator[](1); }
    inline T y() const { return this->operator[](2); }
    inline T z() const { return this->operator[](3); }
    
    inline T& w() { return this->operator[](0); }
    inline T& x() { return this->operator[](1); }
    inline T& y() { return this->operator[](2); }
    inline T& z() { return this->operator[](3); }
    
    bool operator==(const UnitQuaternion<T>& other) const;
    
    // Quaternion multiplication
    UnitQuaternion<T>  operator* (const UnitQuaternion<T>& other) const;
    UnitQuaternion<T>& operator*=(const UnitQuaternion<T>& other);
    
    // Rotation of a point/vector
    Point3<T> operator*(const Point3<T>& p) const;
    
    // Normalize to unit length
    UnitQuaternion<T>& Normalize();
    
    // Conjugate
    UnitQuaternion<T>& Conj(); // in place
    UnitQuaternion<T>  GetConj() const;
    
    // How far from unit length the quaternion needs to get in order to trigger
    // a renormalization
    static T NormalizationTolerance;
    
  }; // class UnitQuaternion
  
  
  // A general storage class for storing and converting between different Rotation
  // formats. Internally, uses a UnitQuaternion, but can be constructed from and
  // converted to RotationMatrices and RotationVectors.
  class Rotation3d
  {
  public:

    Rotation3d(const Radians& angle, const Vec3f& axis);
    Rotation3d(const RotationVector3d& Rvec);
    Rotation3d(const RotationMatrix3d& Rmat);
    
    bool operator==(const Rotation3d& other) const;
    
    // Compose rotations
    Rotation3d& operator*=(const Rotation3d& other);
    Rotation3d  operator* (const Rotation3d& other) const;
    Rotation3d& PreMultiplyBy(const Rotation3d& other);
    
    // Rotate a point/vector
    Point3<float> operator*(const Point3<float>& p) const;    
    
    const UnitQuaternion<float>& GetQuaternion()     const { return _q; }
    const RotationMatrix3d       GetRotationMatrix() const;
    const RotationVector3d       GetRotationVector() const;
    
    const Radians GetAngle() const;
    const Vec3f   GetAxis()  const;
    
    Radians GetAngleAroundXaxis() const;
    Radians GetAngleAroundYaxis() const;
    Radians GetAngleAroundZaxis() const;
    
    Radians GetAngleDiffFrom(const Rotation3d& otherRotation) const;
    
    Rotation3d& Invert();
    Rotation3d  GetInverse() const;
    
  private:
    UnitQuaternion<float> _q;
    
  }; // Rotation3d
  
  bool IsNearlyEqual(const Rotation3d& R1, const Rotation3d& R2, const f32 tolerance);
  
  
  class RotationMatrix3d : public RotationMatrixBase<3>
  {
  public:
    RotationMatrix3d(); // 3x3 identity matrix (no rotation)
    RotationMatrix3d(const RotationVector3d &rotationVector);
    RotationMatrix3d(const Matrix_3x3f &matrix3x3);
    RotationMatrix3d(std::initializer_list<float> initVals);
    RotationMatrix3d(const Radians angle, const Vec3f &axis);
    
    // Return total angular rotation from the identity (no rotation)
    Radians GetAngle() const;
    
    // Return angular rotation difference from another rotation matrix
    Radians GetAngleDiffFrom(const RotationMatrix3d &other) const;
    
    /*
    // Return the Euler angles, under the convention that the
    // rotation matrix is the composition of three ordered rotations around
    // each of the axes, namely:  R = Rz * Ry * Rx    
    void GetEulerAngles(Radians& angle_x, Radians& angle_y, Radians& angle_z) const;
    */
    
    Radians GetAngleAroundXaxis() const;
    Radians GetAngleAroundYaxis() const;
    Radians GetAngleAroundZaxis() const;
    
    
  }; // class RotationMatrix3d
  
  // Rodrigues' formula for converting between angle+axis representation and 3x3
  // matrix representation.
  void Rodrigues(const RotationVector3d &Rvec_in,
                       RotationMatrix3d &Rmat_out);
  
  void Rodrigues(const RotationMatrix3d &Rmat_in,
                       RotationVector3d &Rvec_out);

  
#pragma mark --- Inline Implementations ---
  /*
  inline Radians RotationMatrix3d::GetAngle(void) const
  {
    return this->rotationVector.GetAngle();
  }
  
  inline Vec3f RotationMatrix3d::get_axis(void) const
  {
    return this->rotationVector.get_axis();
  }
   */
  
  inline Radians RotationVector3d::GetAngle(void) const {
    return _angle;
  }
  
  inline const Vec3f& RotationVector3d::GetAxis(void) const {
    if (_angle == 0) {
      // A zero degree rotation means the axis of rotation will be a null vector
      static const Vec3f AXIS_FOR_ZERO_ANGLE(Z_AXIS_3D());
      return AXIS_FOR_ZERO_ANGLE;
    }
    return _axis;
  }
  
  inline void RotationVector3d::GetAngleAndAxis(Radians &angle,
                                                Vec3f   &axis) const
  {
    axis = _axis;
    angle = _angle;
  }
  


} // namespace Anki

#endif /* defined(__CoreTech_Math__rotation__) */

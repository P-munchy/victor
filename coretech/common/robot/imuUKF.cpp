/**
 * File: imuUKF.cpp
 *
 * Author: Michael Willett
 * Created: 1/7/2019
 *
 * Description:
 *
 *   UKF implementation for orientation and gyro bias tracking
 *   Orientation of gyro axes is assumed to be identical to that of robot when the head is at 0 degrees.
 *   i.e. x-axis points forward, y-axis points to robot's left, z-axis points up.
 *
 * Copyright: Anki, Inc. 2019
 *
 **/

#include "coretech/common/robot/imuUKF.h"
#include "coretech/common/shared/math/matrix_impl.h"

namespace Anki {

namespace {
  // calculates the decomposition of a positive definite NxN matrix A s.t. A = L' * L    
  template<MatDimType N>
  SmallSquareMatrix<N,double> Cholesky(const SmallSquareMatrix<N,double>& A) {
    SmallSquareMatrix<N,double> L;

    for (size_t i = 0; i < N; ++i) {
      for (size_t j = 0; j <= i; ++j) {
          double s = 0;
          for (size_t k = 0; k < j; ++k) {
              s += L(j, k) * L(i, k);
          }
          L(i, j) = (i == j) ? sqrt(A(i, i) - s) : (A(i, j) - s) / L(j, j);
      }
    }
    return L;
  }
  
  // transforms rotation vector into a quaternion
  inline UnitQuaternion ToQuat(const Point<3,double>& v) { 
    const double alpha = v.Length();
    const double sinAlpha = sin(alpha * .5) / (NEAR_ZERO(alpha) ? 1. : alpha);
    return {cos(alpha * .5), sinAlpha * v.x(),  sinAlpha * v.y(),  sinAlpha * v.z()};
  };
  
  // transforms quaternion into a rotation vector 
  inline Point<3,double> FromQuat(const UnitQuaternion& q) { 
    Point<3,double> axis = q.Slice<1,3>();
    const double alpha = asin( axis.MakeUnitLength() ) * 2;
    return axis * alpha;
  };

  // fast mean calculation for columns of a matrix
  template <MatDimType M, MatDimType N>
  Point<M,double> CalculateMean(const SmallMatrix<M,N,double>& A) {
    return A * Point<N,double>(1./N);
  }

  template<MatDimType M, MatDimType N, MatDimType O>
  SmallMatrix<M,O,double> GetCovariance(const SmallMatrix<M,N,double>& A, const SmallMatrix<N,O,double>& B) {
    return (A*B) * (1./N);
  }
  
  template<MatDimType M, MatDimType N>
  SmallSquareMatrix<M,double> GetCovariance(const SmallMatrix<M,N,double>& A) {
    return GetCovariance(A, A.GetTranspose());
  }

  // Process Noise
  constexpr const double kRotStability_rad    = .0005;     // assume pitch & roll don't change super fast when driving
  constexpr const double kGyroStability_radps = 1.;        // half max observable rotation from gyro
  constexpr const double kBiasStability_radps = .0000145;  // bias stability

  // Measurement Noise
  // NOTES: 1) measured rms noise on the gyro (~.003 rms) is higher on Z axis than the spec sheet (.00122 rms)
  //        2) we should be careful with using noise anyway - if the integration from the gyro is off and the
  //           calculated pitch/roll conflict with the accelerometer reading, using a lower noise on the gyro
  //           will result in trusting the integration more, causing very slow adjustments to gravity vector.
  constexpr const double kAccelNoise_rad      = .0018;  // rms noise
  constexpr const double kGyroNoise_radps     = .003;   // see note
  constexpr const double kBiasNoise_radps     = .00003; // bias stability

  // extra tuning param for how much we distribute the sigma points
  constexpr const double kWsigma = .08;
  constexpr const double kCholScaleSq = 1./(2*kWsigma);

  // Gravity constants
  constexpr const Point<3,double> kGravity_mmpsSq = {0., 0., 9810.};
  constexpr const double          kG_over_mmpsSq  = 1./9810.;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// UKF Implementation
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Process Uncertainty
const SmallSquareMatrix<ImuUKF::Error::Size,double> ImuUKF::_Q{{  
  Util::Square(kRotStability_rad), 0., 0., 0., 0., 0., 0., 0., 0.,
  0., Util::Square(kRotStability_rad), 0., 0., 0., 0., 0., 0., 0.,
  0., 0., Util::Square(kRotStability_rad), 0., 0., 0., 0., 0., 0.,
  0., 0., 0., Util::Square(kGyroStability_radps), 0., 0., 0., 0., 0.,
  0., 0., 0., 0., Util::Square(kGyroStability_radps), 0., 0., 0., 0.,
  0., 0., 0., 0., 0., Util::Square(kGyroStability_radps), 0., 0., 0.,
  0., 0., 0., 0., 0., 0., Util::Square(kBiasStability_radps), 0., 0.,
  0., 0., 0., 0., 0., 0., 0., Util::Square(kBiasStability_radps), 0.,
  0., 0., 0., 0., 0., 0., 0., 0., Util::Square(kBiasStability_radps)
}}; 

// Measurement Uncertainty
const SmallSquareMatrix<ImuUKF::Error::Size,double> ImuUKF::_R{{  
  Util::Square(kAccelNoise_rad), 0., 0., 0., 0., 0., 0., 0., 0.,
  0., Util::Square(kAccelNoise_rad), 0., 0., 0., 0., 0., 0., 0.,
  0., 0., Util::Square(kAccelNoise_rad), 0., 0., 0., 0., 0., 0.,
  0., 0., 0., Util::Square(kGyroNoise_radps), 0., 0., 0., 0., 0.,
  0., 0., 0., 0., Util::Square(kGyroNoise_radps), 0., 0., 0., 0.,
  0., 0., 0., 0., 0., Util::Square(kGyroNoise_radps), 0., 0., 0.,
  0., 0., 0., 0., 0., 0., Util::Square(kBiasNoise_radps), 0., 0.,
  0., 0., 0., 0., 0., 0., 0., Util::Square(kBiasNoise_radps), 0.,
  0., 0., 0., 0., 0., 0., 0., 0., Util::Square(kBiasNoise_radps)
}}; 

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
ImuUKF::ImuUKF()
{ 
  Reset( UnitQuaternion() ); 
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ImuUKF::Reset(const Rotation3d& rot) {
  _state = {rot.GetQuaternion(), {}, {}};
  _lastMeasurement_s = 0.;
  _P = _Q;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ImuUKF::Update(const Point<3,double>& accel, const Point<3,double>& gyro, const float timestamp_s, bool isMoving)
{
  const auto measurement = Join(Join(accel, gyro), (isMoving ? _state.GetGyroBias() : gyro));
  ProcessUpdate( timestamp_s - _lastMeasurement_s );
  MeasurementUpdate( measurement );
  _lastMeasurement_s = timestamp_s;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ImuUKF::ProcessUpdate(double dt_s)
{ 
  // sample the covariance, generating the set {𝑌ᵢ} and add the mean
  const auto S = Cholesky(_P + _Q) * sqrt(kCholScaleSq);
  for (int i = 0; i < Error::Size; ++i) {
    // current process model assumes we continue moving at constant velocity
    const Error Si = S.GetColumn(i);
    const auto  q  = ToQuat(Si.GetRotation());
    const auto  w1 = _state.GetVelocity() + Si.GetVelocity();
    const auto  w2 = _state.GetVelocity() - Si.GetVelocity();
    const auto  b1 = _state.GetGyroBias() + Si.GetGyroBias();
    const auto  b2 = _state.GetGyroBias() - Si.GetGyroBias();

    _Y.SetColumn(2*i,     State{_state.GetRotation() * q * ToQuat((w1-b1) * dt_s), w1, b1} );
    _Y.SetColumn((2*i)+1, State{_state.GetRotation() * q.GetConj() * ToQuat((w2-b2) * dt_s), w2, b2} );
  }

  // NOTE: we are making a huge assumption here. Technically, quaternions cannot be averaged using
  //       typical average calculation: Σ(xᵢ)/N. However, we assume in this model that we are calling
  //       the process update frequently enough that the elements of _Y do not diverge very quickly,
  //       in which case an element wise mean will converge to the same result as more accurate
  //       quaternion mean calculation methods. If this does not hold in the future, I have verified
  //       that both a gradient decent method and largest Eigen Vector method work well.
  _state = CalculateMean(_Y);

  // Calculate Process Noise {𝑊ᵢ} by mean centering {𝑌ᵢ}
  const auto meanRot  = _state.GetRotation();
  const auto meanVel  = _state.GetVelocity();
  const auto meanBias = _state.GetGyroBias();
  for (int i = 0; i < 2*Error::Size; ++i) {
    const State yi    = _Y.GetColumn(i);
    const auto  err   = FromQuat(meanRot.GetConj() * yi.GetRotation());
    const auto  omega = yi.GetVelocity() - meanVel;
    const auto  bias  = yi.GetGyroBias() - meanBias;
    const auto  wi    = Join(Join(err, omega), bias);
    _W.SetColumn(i, wi);
  }
  _P = GetCovariance(_W) * kWsigma;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ImuUKF::MeasurementUpdate(const Point<9,double>& measurement)
{
  // Calculate Predicted Measurement Distribution {𝑍ᵢ}
  SmallMatrix<Error::Size,Error::Size*2,double> Z;
  for (int i = 0; i < 2*Error::Size; ++i) {
    const State yi = _Y.GetColumn(i);
    const auto zi = Join(Join( yi.GetRotation().GetConj() * kGravity_mmpsSq, yi.GetVelocity() ), yi.GetGyroBias());
    Z.SetColumn(i, zi);
  }

  // mean center {𝑍ᵢ}
  const auto meanZ = CalculateMean(Z);
  for (int i = 0; i < 2*Error::Size; ++i) { 
    Z.SetColumn(i, Z.GetColumn(i) - meanZ); 
  }

  // get Kalman gain and update covariance
  const auto Pvv = GetCovariance(Z) * kWsigma + _R;
  const auto Pxz = GetCovariance(_W, Z.GetTranspose()) * kWsigma;
  const auto K = Pxz * Pvv.GetInverse();
  _P -= K * Pvv * K.GetTranspose();

  // get measurement residual
  const Error residual = K*(measurement - meanZ);
    
  // Add the residual to the current state. 
  _state = State{ _state.GetRotation() * ToQuat(residual.GetRotation() * kG_over_mmpsSq),
                  _state.GetVelocity() + residual.GetVelocity(),
                  _state.GetGyroBias() + residual.GetGyroBias()
                };
}
      
} // namespace Anki

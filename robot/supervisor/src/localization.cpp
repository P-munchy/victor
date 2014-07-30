#include "anki/cozmo/robot/hal.h"
#include "localization.h"
#include "anki/common/robot/geometry.h"
#include "imuFilter.h"

#define DEBUG_LOCALIZATION 0
#define DEBUG_POSE_HISTORY 0

#ifdef SIMULATOR
// Whether or not to use simulator "ground truth" pose
#define USE_SIM_GROUND_TRUTH_POSE 0
#define USE_OVERLAY_DISPLAY 1
#else // else not simulator
#define USE_SIM_GROUND_TRUTH_POSE 0
#define USE_OVERLAY_DISPLAY 0
#endif // #ifdef SIMULATOR


// Whether or not to use the orientation given by the gyro
#define USE_GYRO_ORIENTATION 1


#if(USE_OVERLAY_DISPLAY)
#include "anki/cozmo/robot/hal.h"
#include "sim_overlayDisplay.h"
#endif

namespace Anki {
  namespace Cozmo {
    namespace Localization {
      
      struct PoseStamp {
        TimeStamp_t t;
        f32 x;
        f32 y;
        f32 angle;
        PoseFrameID_t frame;
      };
      
      namespace {
        
        const f32 BIG_RADIUS = 5000;
        
        // private members
        ::Anki::Embedded::Pose2d currMatPose;
        
        
        // Localization:
        f32 x_=0.f, y_=0.f;  // mm
        Radians orientation_(0.f);
        bool onRamp_ = false;
       
#if(USE_OVERLAY_DISPLAY)
        f32 xTrue_, yTrue_, angleTrue_;
        f32 prev_xTrue_, prev_yTrue_, prev_angleTrue_;
#endif
        
        f32 prevLeftWheelPos_ = 0;
        f32 prevRightWheelPos_ = 0;
        
        f32 gyroRotOffset_ = 0;
        
        PoseFrameID_t frameId_ = 0;
        
        
        // Pose history
        // Never need to erase elements, just overwrite with new data.
        const u16 POSE_HISTORY_SIZE = 300;
        PoseStamp hist_[POSE_HISTORY_SIZE];
        u16 hStart_ = 0;
        u16 hEnd_ = 0;
        u16 hSize_ = 0;
        
        // MemoryStack for rotation matrices and operations on them
        const s32 SCRATCH_BUFFER_SIZE = 75*4 + 128;
        char scratchBuffer[SCRATCH_BUFFER_SIZE];
        Embedded::MemoryStack scratch(scratchBuffer, SCRATCH_BUFFER_SIZE);
        
        // Poses
        Embedded::Point3<f32> currPoseTrans;
        Embedded::Array<f32> currPoseRot(3,3,scratch);
        
        Embedded::Point3<f32> p0Trans;
        Embedded::Array<f32> p0Rot(3,3,scratch);
        
        Embedded::Point3<f32> pDiffTrans;
        Embedded::Array<f32> pDiffRot(3,3,scratch);
        
        Embedded::Point3<f32> keyPoseTrans;
        Embedded::Array<f32> keyPoseRot(3,3,scratch);
        
        // The time of the last keyframe that was used to update the robot's pose.
        // Using this to limit how often keyframes are used to compute the robot's
        // current pose so that we don't have to do multiple
        TimeStamp_t lastKeyframeUpdate_ = 0;
      }
      
      
      
      /// ============= Pose history ==============
      void ClearHistory() {
        hStart_ = 0;
        hEnd_ = 0;
        hSize_ = 0;
        lastKeyframeUpdate_ = 0;
      }
      
      
      Result GetHistIdx(TimeStamp_t t, u16& idx)
      {
        // TODO: Binary search for timestamp
        //       For now just doing a straight up linear search.
        
        
        if (hEnd_ < hStart_) {
          for (idx = hStart_; idx < POSE_HISTORY_SIZE; ++idx) {
            if (hist_[idx].t == t) {
              return RESULT_OK;
            }
          }
          
          for (idx = 0; idx <= hEnd_; ++idx) {
            if (hist_[idx].t == t) {
              return RESULT_OK;
            }
          }
          
        } else {
          for (idx = hStart_; idx <= hEnd_; ++idx) {
            if (hist_[idx].t == t) {
              return RESULT_OK;
            }
          }
        }
        return RESULT_FAIL;
      }
      
      Result UpdatePoseWithKeyframe(PoseFrameID_t frameID, TimeStamp_t t, const f32 x, const f32 y, const f32 angle)
      {
        // Update frameID
        frameId_ = frameID;
        
        u16 i;
        if (GetHistIdx(t, i) == RESULT_FAIL) {
          PRINT("ERROR: Couldn't find timestamp %d in history (oldest(%d) %d, newest(%d) %d)\n", t, hStart_, hist_[hStart_].t, hEnd_, hist_[hEnd_].t);
          return RESULT_FAIL;
        }
        
        
        // TODO: Replace lastKeyFrameUpdate with actually computing
        // pDiff by chaining pDiffs per frame all the way up to current frame.
        // The frame distance between the historical pose and current pose depends on the comms latency!
        // ... as well as how often the mat markers are sent, obviously.
        if (lastKeyframeUpdate_ > hist_[i].t) {
          #if(DEBUG_POSE_HISTORY)
          PRINT("Ignoring keyframe %d\n", frameId_);
          #endif
          return RESULT_FAIL;
        }
        
        
        
        // Compute new pose based on key frame pose and the diff between the historical
        // pose at time t and the latest pose.
        
        // Historical pose
        p0Trans.x = hist_[i].x;
        p0Trans.y = hist_[i].y;
        p0Trans.z = 0;
        
        f32 s0 = sinf(hist_[i].angle);
        f32 c0 = cosf(hist_[i].angle);
        p0Rot[0][0] = c0;    p0Rot[0][1] = -s0;    p0Rot[0][2] = 0;
        p0Rot[1][0] = s0;    p0Rot[1][1] =  c0;    p0Rot[1][2] = 0;
        p0Rot[2][0] =  0;    p0Rot[2][1] =   0;    p0Rot[2][2] = 1;
        
        // Current pose
        currPoseTrans.x = x_;
        currPoseTrans.y = y_;
        currPoseTrans.z = 0;
        
        f32 s1 = sinf(orientation_.ToFloat());
        f32 c1 = cosf(orientation_.ToFloat());
        currPoseRot[0][0] = c1;    currPoseRot[0][1] = -s1;    currPoseRot[0][2] = 0;
        currPoseRot[1][0] = s1;    currPoseRot[1][1] =  c1;    currPoseRot[1][2] = 0;
        currPoseRot[2][0] =  0;    currPoseRot[2][1] =   0;    currPoseRot[2][2] = 1;
        
        // Compute the difference between the historical pose and the current pose
        if (ComputePoseDiff(p0Rot, p0Trans, currPoseRot, currPoseTrans, pDiffRot, pDiffTrans, scratch) == RESULT_FAIL) {
          PRINT("Failed to compute pose diff\n");
          return RESULT_FAIL;
        }
        
        // Compute pose of the keyframe
        keyPoseTrans.x = x;
        keyPoseTrans.y = y;
        keyPoseTrans.z = 0;
        
        f32 sk = sinf(angle);
        f32 ck = cosf(angle);
        keyPoseRot[0][0] = ck;    keyPoseRot[0][1] = -sk;    keyPoseRot[0][2] = 0;
        keyPoseRot[1][0] = sk;    keyPoseRot[1][1] =  ck;    keyPoseRot[1][2] = 0;
        keyPoseRot[2][0] =  0;    keyPoseRot[2][1] =   0;    keyPoseRot[2][2] = 1;

        #if(DEBUG_POSE_HISTORY)
        PRINT("pHist: %f %f %f (frame %d, curFrame %d)\n", hist_[i].x, hist_[i].y, hist_[i].angle, hist_[i].frame, frameId_);
        PRINT("pCurr: %f %f %f\n", currPoseTrans.x, currPoseTrans.y, orientation_.ToFloat());
        PRINT("pKey: %f %f %f\n", x, y, angle);
        #endif
        

        // Apply the pose diff to the keyframe pose to get the new curr pose
        Embedded::Matrix::Multiply(keyPoseRot, pDiffRot, currPoseRot);
        currPoseTrans = keyPoseRot*pDiffTrans + keyPoseTrans;

        // NOTE: Expecting only rotation about the z-axis.
        //       If this is not the case, we need to do something more mathy.
        f32 newAngle = acosf(currPoseRot[0][0]);
        if (currPoseRot[0][1] > 0) {
          newAngle *= -1;
        }
        
        SetCurrentMatPose(currPoseTrans.x, currPoseTrans.y, newAngle);
        
        #if(DEBUG_POSE_HISTORY)
        f32 pDiffAngle = acosf(pDiffRot[0][0]);
        if (pDiffRot[0][1] > 0) {
          pDiffAngle *= -1;
        }
        PRINT("pDiff: %f %f %f\n", pDiffTrans.x, pDiffTrans.y, pDiffAngle);
        PRINT("pCurrNew: %f %f %f\n", x_, y_, orientation_.ToFloat());
        #endif
        
        lastKeyframeUpdate_ = HAL::GetTimeStamp();
        
        return RESULT_OK;
      }
      
      void AddPoseToHist()
      {
        if (++hEnd_ == POSE_HISTORY_SIZE) {
          hEnd_ = 0;
        }
        
        if (hEnd_ == hStart_) {
          if (++hStart_ == POSE_HISTORY_SIZE) {
            hStart_ = 0;
          }
        } else {
          ++hSize_;
        }
        
        hist_[hEnd_].t = HAL::GetTimeStamp();
        hist_[hEnd_].x = x_;
        hist_[hEnd_].y = y_;
        hist_[hEnd_].angle = orientation_.ToFloat();
        hist_[hEnd_].frame = frameId_;
      }
      
      
      
      /// ========= Localization ==========

      Result Init() {
        SetCurrentMatPose(0,0,0);
        
        onRamp_ = false;
        
        prevLeftWheelPos_ = HAL::MotorGetPosition(HAL::MOTOR_LEFT_WHEEL);
        prevRightWheelPos_ = HAL::MotorGetPosition(HAL::MOTOR_RIGHT_WHEEL);

        gyroRotOffset_ =  -IMUFilter::GetRotation();
      
        ClearHistory();
        
        return RESULT_OK;
      }
/*
      Anki::Embedded::Pose2d GetCurrMatPose()
      {
        return currMatPose;
      }
*/
      
      Result SendRampTraverseStartMessage()
      {
        Messages::RampTraverseStart msg;
        msg.timestamp = HAL::GetTimeStamp();
        if(HAL::RadioSendMessage(GET_MESSAGE_ID(Messages::RampTraverseStart), &msg)) {
          return RESULT_OK;
        }
        return RESULT_FAIL;
      }
      
      Result SendRampTraverseComplete(const bool success)
      {
        Messages::RampTraverseComplete msg;
        msg.timestamp = HAL::GetTimeStamp();
        msg.didSucceed = success;
        if(HAL::RadioSendMessage(GET_MESSAGE_ID(Messages::RampTraverseComplete), &msg)) {
          return RESULT_OK;
        }
        return RESULT_FAIL;
      }

      Result SetOnRamp(bool onRamp)
      {
        Result lastResult = RESULT_OK;
        if(onRamp == true && onRamp_ == false) {
          // We weren't on a ramp but now we are
          Messages::RampTraverseStart msg;
          msg.timestamp = HAL::GetTimeStamp();
          if(HAL::RadioSendMessage(GET_MESSAGE_ID(Messages::RampTraverseStart), &msg) == false) {
            lastResult = RESULT_FAIL;
          }
        }
        else if(onRamp == false && onRamp_ == true) {
          // We were on a ramp and now we're not
          Messages::RampTraverseComplete msg;
          msg.timestamp = HAL::GetTimeStamp();
          if(HAL::RadioSendMessage(GET_MESSAGE_ID(Messages::RampTraverseComplete), &msg) == false) {
            lastResult = RESULT_FAIL;
          }
        }
        
        onRamp_ = onRamp;
        
        return lastResult;
      }
      
      bool IsOnRamp() {
        return onRamp_;
      }
      
      void Update()
      {

#if(USE_SIM_GROUND_TRUTH_POSE)
        // For initial testing only
        float angle;
        HAL::GetGroundTruthPose(x_,y_,angle);
        
        // Convert to mm
        x_ *= 1000;
        y_ *= 1000;
        
        orientation_ = angle;
#else
     
        bool movement = false;
        
        // Update current pose estimate based on wheel motion
        
        f32 currLeftWheelPos = HAL::MotorGetPosition(HAL::MOTOR_LEFT_WHEEL);
        f32 currRightWheelPos = HAL::MotorGetPosition(HAL::MOTOR_RIGHT_WHEEL);
        
        // Compute distance traveled by each wheel
        f32 lDist = currLeftWheelPos - prevLeftWheelPos_;
        f32 rDist = currRightWheelPos - prevRightWheelPos_;

        
        // Compute new pose based on encoders and gyros, but only if there was any motion.
        movement = (!FLT_NEAR(rDist, 0) || !FLT_NEAR(lDist,0));
        if (movement ) {
#if(DEBUG_LOCALIZATION)
          PRINT("\ncurrWheelPos (%f, %f)   prevWheelPos (%f, %f)\n",
                currLeftWheelPos, currRightWheelPos, prevLeftWheelPos_, prevRightWheelPos_);
#endif
          
          f32 lRadius, rRadius; // Radii of each wheel arc path (+ve radius means origin of arc is to the left)
          f32 cRadius; // Radius of arc traversed by center of robot
          f32 cDist;   // Distance traversed by center of robot
          f32 cTheta;  // Theta traversed by center of robot
          
          
      
          // lDist / lRadius = rDist / rRadius = theta
          // rRadius - lRadius = wheel_dist  => rRadius = wheel_dist + lRadius
          
          // lDist / lRadius = rDist / (wheel_dist + lRadius)
          // (wheel_dist + lRadius) / lRadius = rDist / lDist
          // wheel_dist / lRadius = rDist / lDist - 1
          // lRadius = wheel_dist / (rDist / lDist - 1)

          if ((rDist != 0) && (lDist / rDist) < 1.01f && (lDist / rDist) > 0.99f) {
//          if (FLT_NEAR(lDist, rDist)) {
            lRadius = BIG_RADIUS;
            rRadius = BIG_RADIUS;
            cRadius = BIG_RADIUS;
            cTheta = 0;
            cDist = lDist;
          } else {
            if (FLT_NEAR(lDist,0)) {
              lRadius = 0;
            } else {
              lRadius = WHEEL_DIST_MM / (rDist / lDist - 1);
            }
            rRadius = WHEEL_DIST_MM + lRadius;
            if (ABS(rRadius) > ABS(lRadius)) {
              cTheta = rDist / rRadius;
            } else {
              cTheta = lDist / lRadius;
            }
            cDist = 0.5f*(lDist + rDist);
            cRadius = lRadius + WHEEL_DIST_HALF_MM;
          }

#if(DEBUG_LOCALIZATION)
          PRINT("lRadius %f, rRadius %f, lDist %f, rDist %f, cTheta %f, cDist %f, cRadius %f\n",
                lRadius, rRadius, lDist, rDist, cTheta, cDist, cRadius);
          
          PRINT("oldPose: %f %f %f\n", x_, y_, orientation_.ToFloat());
#endif
          
          if (ABS(cRadius) >= BIG_RADIUS) {

            x_ += cDist * cosf(orientation_.ToFloat());
            y_ += cDist * sinf(orientation_.ToFloat());

            /*
            f32 dx = cDist * cosf(orientation_.ToFloat());
            f32 dy = cDist * sinf(orientation_.ToFloat());
            
            // Only update z position when moving straight
            if (onRamp_) {
              f32 pitch = IMUFilter::GetPitch();
              f32 cosp = cosf(pitch);
              x_ += dx * cosp;
              y_ += dy * cosp;
              z_ += cDist * tanf(pitch);
              PRINT("dx %f, dy %f, pitch %f  (z %f)\n", dx, dy, pitch, z_);
            } else {
              x_ += dx;
              y_ += dy;
            }
            */
          } else {
            
            
#if(1)
            // Compute distance traveled relative to previous position.
            // Drawing a straight line from the previous position to the new position forms a chord
            // in the circle defined by the turning radius as determined by the incremental wheel motion this tick.
            // The angle of this circle that this chord spans is cTheta.
            // The angle of the chord relative to the robot's previous trajectory is cTheta / 2.
            f32 alpha = cTheta * 0.5f;
            
            // The chord length is 2 * cRadius * sin(cTheta / 2).
            f32 chord_length = ABS(2 * cRadius * sinf(alpha));
            
            // The new pose is then
            x_ += (cDist > 0 ? 1 : -1) * chord_length * cosf(orientation_.ToFloat() + alpha);
            y_ += (cDist > 0 ? 1 : -1) * chord_length * sinf(orientation_.ToFloat() + alpha);
            orientation_ += cTheta;
#else
            // Naive approximation, but seems to work nearly as well as non-naive with one less sin() call.
            x_ += cDist * cosf(orientation_.ToFloat());
            y_ += cDist * sinf(orientation_.ToFloat());
            orientation_ += cTheta;
#endif
          }
          
#if(DEBUG_LOCALIZATION)
          PRINT("newPose: %f %f %f\n", x_, y_, orientation_.ToFloat());
#endif
       
        }

        
#if(USE_GYRO_ORIENTATION)
        // Set orientation according to gyro
        orientation_ = IMUFilter::GetRotation() + gyroRotOffset_;
#endif
        
        prevLeftWheelPos_ = HAL::MotorGetPosition(HAL::MOTOR_LEFT_WHEEL);
        prevRightWheelPos_ = HAL::MotorGetPosition(HAL::MOTOR_RIGHT_WHEEL);

        
#if(USE_OVERLAY_DISPLAY)
        if(movement)
        {
          using namespace Sim::OverlayDisplay;
          SetText(CURR_EST_POSE, "Est. Pose: (x,y)=(%.4f, %.4f) at deg=%.1f",
                  x_, y_,
                  orientation_.getDegrees());
          //PRINT("Est. Pose: (x,y)=(%.4f, %.4f) at deg=%.1f\n",
          //      x_, y_,
          //      orientation_.getDegrees());
          
          HAL::GetGroundTruthPose(xTrue_, yTrue_, angleTrue_);
          Radians angleRad(angleTrue_);
          
          
          SetText(CURR_TRUE_POSE, "True Pose: (x,y)=(%.4f, %.4f) at deg=%.1f",
                  xTrue_ * 1000, yTrue_ * 1000, angleRad.getDegrees());
          //f32 trueDist = sqrtf((yTrue_ - prev_yTrue_)*(yTrue_ - prev_yTrue_) + (xTrue_ - prev_xTrue_)*(xTrue_ - prev_xTrue_));
          //PRINT("True Pose: (x,y)=(%.4f, %.4f) at deg=%.1f (trueDist = %f)\n", xTrue_, yTrue_, angleRad.getDegrees(), trueDist);
          
          prev_xTrue_ = xTrue_;
          prev_yTrue_ = yTrue_;
          prev_angleTrue_ = angleTrue_;
          
          UpdateEstimatedPose(x_, y_, orientation_.ToFloat());
        }
#endif

        
        
#endif  //else (!USE_SIM_GROUND_TRUTH_POSE)
        

        // Add new current pose to history
        AddPoseToHist();
        
#if(DEBUG_LOCALIZATION)
        PRINT("LOC: %f, %f, %f\n", x_, y_, orientation_.getDegrees());
#endif
      }

      void SetCurrentMatPose(f32  x, f32  y, Radians  angle)
      {
        x_ = x;
        y_ = y;
        orientation_ = angle;
        gyroRotOffset_ = angle.ToFloat() - IMUFilter::GetRotation();
      } // SetCurrentMatPose()
      
      void GetCurrentMatPose(f32& x, f32& y, Radians& angle)
      {
        x = x_;
        y = y_;
        angle = orientation_;
      } // GetCurrentMatPose()
      
  
      Radians GetCurrentMatOrientation()
      {
        return orientation_;
      }

      PoseFrameID_t GetPoseFrameId()
      {
        return frameId_;
      }
      
      void ResetPoseFrame()
      {
        frameId_ = 0;
        ClearHistory();
      }

    }
  }
}

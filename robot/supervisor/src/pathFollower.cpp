#include "dockingController.h"
#include "pathFollower.h"
#include "localization.h"
#include "steeringController.h"
#include "wheelController.h"
#include "speedController.h"
#include <math.h>
#include <float.h>
#include "trig_fast.h"
#include "velocityProfileGenerator.h"
#include "anki/cozmo/robot/logging.h"
#include "anki/cozmo/robot/hal.h"
#include "messages.h"


#define DEBUG_PATH_FOLLOWER 0

// At low speeds Cozmo was stuttering/struggling while following a path which was due to resetting
// wheelcontroller's integral gains, this enables or disables resetting those gains. If Cozmo suddenly
// has trouble following paths this can be reenabled
#define RESET_INTEGRAL_GAINS_AT_END_OF_SEGMENT 0

// The number of tics desired in between debug prints
#define DBG_PERIOD 200

namespace Anki
{
  namespace Cozmo
  {
    namespace PathFollower
    {

      namespace {

        const f32 CONTINUITY_TOL_MM2 = 1;

        const f32 LOOK_AHEAD_DIST_MM = 10;

        const f32 TOO_FAR_FROM_PATH_DIST_MM = 50;

        Planning::Path path_;
        s8 currPathSegment_ = -1;  // Segment index within local path array.
        s8 realPathSegment_ = -1;  // Segment index of the global path. Reset only on StartPathTraversal().

        // Shortest distance to path
        f32 distToPath_mm_ = 0;

        // Angular error with path
        f32 radToPath_ = 0;

        bool pointTurnStarted_ = false;

        // ID of the current path that is being followed
        // or the last path that was followed if not currently path following
        u16 lastPathID_ = 0;

        const f32 COAST_VELOCITY_MMPS = 25.f;
        const f32 COAST_VELOCITY_RADPS = 0.4f; // Same as POINT_TURN_TERMINAL_VEL_RAD_PER_S

        // If true, then the path is not traversed according to its speed parameters, but
        // instead by the SetPathSpeed message.
        bool manualSpeedControl_ = false;
        f32 manualPathSpeed_ = 0;
        f32 manualPathAccel_ = 100;
        f32 manualPathDecel_ = 100;

        // Max speed the robot can travel when in assisted RC mode
        const f32 MAX_ASSISTED_RC_SPEED = 50.f;

        // Target speed to decelerate to when slowing down at end of segment
        const u16 END_OF_PATH_TARGET_SPEED_MMPS = 20;

        // Whether or not deceleration to end of current segment has started
        bool startedDecelOnSegment_ = false;

      } // Private Members


      Result Init(void)
      {
        ClearPath();

        return RESULT_OK;
      }


      // Deletes current path
      void ClearPath(void)
      {
        path_.Clear();
        currPathSegment_ = -1;
        manualPathSpeed_ = false;
        pointTurnStarted_ = false;
        realPathSegment_ = -1;
      } // Update()


      // Trims off segments that have already been traversed
      void TrimPath() {
        if (currPathSegment_ > 0) {
          TrimPath(currPathSegment_, 0);
        }
      }


      // Add path segment
      bool AppendPathSegment_Line(u32 matID, f32 x_start_mm, f32 y_start_mm, f32 x_end_mm, f32 y_end_mm,
                                  f32 targetSpeed, f32 accel, f32 decel)
      {
        TrimPath();
        return path_.AppendLine(matID, x_start_mm, y_start_mm, x_end_mm, y_end_mm,
                                targetSpeed, accel, decel);
      }


      bool AppendPathSegment_Arc(u32 matID, f32 x_center_mm, f32 y_center_mm, f32 radius_mm, f32 startRad, f32 sweepRad,
                                 f32 targetSpeed, f32 accel, f32 decel)
      {
        TrimPath();
        return path_.AppendArc(matID, x_center_mm, y_center_mm, radius_mm, startRad, sweepRad,
                               targetSpeed, accel, decel);
      }


      bool AppendPathSegment_PointTurn(u32 matID, f32 x, f32 y, f32 targetAngle,
                                       f32 targetRotSpeed, f32 rotAccel, f32 rotDecel,
                                       f32 angleTolerance,
                                       bool useShortestDir)
      {
        TrimPath();
        return path_.AppendPointTurn(matID, x, y, targetAngle,
                                     targetRotSpeed, rotAccel, rotDecel,
                                     angleTolerance,
                                     useShortestDir);
      }

      u8 GenerateDubinsPath(f32 start_x, f32 start_y, f32 start_theta,
                            f32 end_x, f32 end_y, f32 end_theta,
                            f32 start_radius, f32 end_radius,
                            f32 targetSpeed, f32 accel, f32 decel,
                            f32 final_straight_approach_length,
                            f32 *path_length)
      {
        return Planning::GenerateDubinsPath(path_,
                                            start_x, start_y, start_theta,
                                            end_x, end_y, end_theta,
                                            start_radius, end_radius,
                                            targetSpeed, accel, decel,
                                            final_straight_approach_length,
                                            path_length);
      }

      u8 GetClosestSegment(const f32 x, const f32 y, const f32 angle)
      {
        AnkiAssert(path_.GetNumSegments() > 0, 286);

        Planning::SegmentRangeStatus res;
        f32 distToSegment, angError;

        u8 closestSegId = 0;
        f32 distToClosestSegment = FLT_MAX;

        for (u8 i=0; i<path_.GetNumSegments(); ++i) {
          res = path_[i].GetDistToSegment(x,y,angle,distToSegment,angError);
#if(DEBUG_PATH_FOLLOWER)
          AnkiDebug( 342, "PathFollower.GetClosestSegment.PathDist", 586, "%f  (res=%d)", 2, distToSegment, res);
#endif
          if (ABS(distToSegment) < distToClosestSegment && (res == Planning::IN_SEGMENT_RANGE || res == Planning::OOR_NEAR_START)) {
            closestSegId = i;
            distToClosestSegment = ABS(distToSegment);
#if(DEBUG_PATH_FOLLOWER)
            AnkiDebug( 343, "PathFollower.GetClosestSegment", 587, " New closest seg: %d, distToSegment %f (res=%d)", 3, i, distToSegment, res);
#endif
          }
        }

        return closestSegId;
      }


      void TrimPath(const u8 numPopFrontSegments, const u8 numPopBackSegments)
      {
        path_.PopBack(numPopBackSegments);
        if (path_.PopFront(numPopFrontSegments) && currPathSegment_ > 0) {
          currPathSegment_ -= numPopFrontSegments;
        }
      }

      bool StartPathTraversal(u16 path_id, bool manualSpeedControl)
      {
        // Set first path segment
        if (path_.GetNumSegments() > 0) {

#if(DEBUG_PATH_FOLLOWER)
          path_.PrintPath();
#endif

          AnkiConditionalErrorAndReturnValue(path_.CheckContinuity(CONTINUITY_TOL_MM2), false, 344, "PathFollower.StartPathTraversal.PathIsDiscontinuous", 305, "", 0);

          // Set whether or not path is traversed according to speed in path parameters
          manualSpeedControl_ = manualSpeedControl;

          currPathSegment_ = 0;
          realPathSegment_ = currPathSegment_;
          startedDecelOnSegment_ = false;


          /*
          // If the first part of the path is some tiny arc,
          // skip it because it tends to report gross errors that
          // make the steeringController jerky.
          // This mainly occurs during docking.
          // TODO: Do this check for EVERY path segment?
          if ((currPathSegment_ == 0) &&
              (path_[0].GetType() == Planning::PST_ARC) &&
              (ABS(path_[0].GetLength() < 10))
              path_[0].GetDef().arc.radius <= 50) {

            PRINT("Skipping short arc: sweep %f deg, radius %f mm, length %fmm\n",
                  RAD_TO_DEG_F32( path_[0].GetDef().arc.sweepRad ),
                  path_[0].GetDef().arc.radius,
                  path_[0].GetLength());

            currPathSegment_++;
            realPathSegment_ = currPathSegment_;
          }
           */



          // Set speed
          // (Except for point turns whose speeds are handled at the steering controller level)
          if (path_[currPathSegment_].GetType() != Planning::PST_POINT_TURN) {
            if (manualSpeedControl_) {
              SpeedController::SetUserCommandedDesiredVehicleSpeed( manualPathSpeed_ );
              SpeedController::SetUserCommandedAcceleration( manualPathAccel_ );
              SpeedController::SetUserCommandedDeceleration( manualPathDecel_ );
            } else {
              SpeedController::SetUserCommandedDesiredVehicleSpeed( path_[currPathSegment_].GetTargetSpeed() );
              SpeedController::SetUserCommandedAcceleration( path_[currPathSegment_].GetAccel() );
              SpeedController::SetUserCommandedDeceleration( path_[currPathSegment_].GetDecel() );
            }
          }

          AnkiDebug( 345, "PathFollower.StartPathTraversal", 588, "Start segment %d, speed = %f, accel = %f, decel = %f", 4,
                currPathSegment_,
                path_[currPathSegment_].GetTargetSpeed(),
                path_[currPathSegment_].GetAccel(),
                path_[currPathSegment_].GetDecel());

          //Robot::SetOperationMode(Robot::FOLLOW_PATH);
          SteeringController::SetPathFollowMode();
        }

        // Set id of path
        if (path_id != 0) {
          lastPathID_ = path_id;
        }

        return TRUE;
      }


      bool IsTraversingPath()
      {
        return currPathSegment_ >= 0;
      }

      bool IsInManualSpeedMode()
      {
        return manualSpeedControl_;
      }

      void SetManualPathSpeed(f32 speed_mmps, f32 accel_mmps2, f32 decel_mmps2)
      {
        manualPathSpeed_ = CLIP(speed_mmps, -MAX_ASSISTED_RC_SPEED, MAX_ASSISTED_RC_SPEED);
        manualPathAccel_ = accel_mmps2;
        manualPathDecel_ = decel_mmps2;
      }


      s8 GetCurrPathSegment()
      {
        return realPathSegment_;// currPathSegment_;
      }

      u8 GetNumFreeSegmentSlots()
      {
        return MAX_NUM_PATH_SEGMENTS - (path_.GetNumSegments() - currPathSegment_ + 1);
      }

      Planning::SegmentRangeStatus ProcessPathSegment(f32 &shortestDistanceToPath_mm, f32 &radDiff)
      {
        // Get current robot pose
        f32 distToEnd = 0;
        f32 x, y, lookaheadX, lookaheadY;
        Radians angle;
        Localization::GetDriveCenterPose(x, y, angle);

        lookaheadX = x;
        lookaheadY = y;

        AnkiAssert(Planning::PST_LINE == path_[currPathSegment_].GetType() || Planning::PST_ARC == path_[currPathSegment_].GetType(), 287);

        // Compute lookahead position
        if (LOOK_AHEAD_DIST_MM != 0) {
          if (path_[currPathSegment_].GetTargetSpeed() > 0) {
            lookaheadX += LOOK_AHEAD_DIST_MM * cosf(angle.ToFloat());
            lookaheadY += LOOK_AHEAD_DIST_MM * sinf(angle.ToFloat());
          } else {
            lookaheadX -= LOOK_AHEAD_DIST_MM * cosf(angle.ToFloat());
            lookaheadY -= LOOK_AHEAD_DIST_MM * sinf(angle.ToFloat());
          }
        }

        Planning::SegmentRangeStatus status = path_[currPathSegment_].GetDistToSegment(lookaheadX,lookaheadY,angle.ToFloat(),shortestDistanceToPath_mm,radDiff, &distToEnd);

        // If this is the last segment or the next segment is a point turn we need to
        // (1) check if the lookahead point is out of range and if so, use the
        //     robot drive center to compute distance to segment, and
        // (2) decelerate towards the end of the piece according to the segment's
        //     deceleration or faster if necessary.
        if ((currPathSegment_ == path_.GetNumSegments() - 1) || (path_[currPathSegment_+1].GetType() == Planning::PST_POINT_TURN)) {

          // 1) Check if time to switch to robot drive center instead of origin
          if (status == Planning::OOR_NEAR_END) {
            if (LOOK_AHEAD_DIST_MM != 0) {
              f32 junk_mm, junk_rad;
              status = path_[currPathSegment_].GetDistToSegment(x,y,angle.ToFloat(),junk_mm, junk_rad, &distToEnd);
              //PRINT("PATH-OOR: status %d (distToEnd %f, currCmdSpeed %d mm/s, currSpeed %d mm/s)\n", status, distToEnd, (s32)
              //      SpeedController::GetUserCommandedCurrentVehicleSpeed(), (s32)SpeedController::GetCurrentMeasuredVehicleSpeed());
            }
          } else {
            // For purposes of determining if we should decelerate we should consider the
            // distance remaining between the segment end and the robot's drive center
            // rather than the lookahead point.
            distToEnd += LOOK_AHEAD_DIST_MM;
          }

          // 2) Check if time to decelerate
          if (!startedDecelOnSegment_) {
            // See if the current deceleration rate is sufficient to stop the robot at the end of the path
            u16 decel = SpeedController::GetUserCommandedDeceleration();
            s32 currSpeed = SpeedController::GetUserCommandedCurrentVehicleSpeed();
            f32 distToStopIfDecelNow = 0.5f*currSpeed*currSpeed / decel;

            // Is it time to start slowing down?
            if (distToStopIfDecelNow >= distToEnd) {
              // Compute the deceleration necessary to stop robot at end of segment in the remaining distance
              // Since we're actually decelerating to END_OF_PATH_TARGET_SPEED_MMPS, this is just an approximation.
              u16 requiredDecel = 0.5f*currSpeed*currSpeed / distToEnd;
              SpeedController::SetUserCommandedDeceleration(requiredDecel);
              SpeedController::SetUserCommandedDesiredVehicleSpeed(copysign(END_OF_PATH_TARGET_SPEED_MMPS, path_[currPathSegment_].GetTargetSpeed()));
              startedDecelOnSegment_ = true;
              //AnkiDebug( 33, "PathFollower", 219, "PathFollower: Decel to end of segment %d (of %d) at %d mm/s^2 from speed of %d mm/s (meas %d mm/s) over %f mm\n", 6,
              //      currPathSegment_, path_.GetNumSegments(), requiredDecel, currSpeed, (s32)SpeedController::GetCurrentMeasuredVehicleSpeed(), distToEnd);
            }
          }
#         if(DEBUG_PATH_FOLLOWER)
          else {
            AnkiDebug( 346, "PathFollower.ProcessPathSegment.Decel", 589, "currCmdSpeed %d mm/s, currSpeed %d mm/s)", 2,
                  (s32)SpeedController::GetUserCommandedCurrentVehicleSpeed(),
                  (s32)SpeedController::GetCurrentMeasuredVehicleSpeed());
          }
#         endif
        }

        return status;
      }



      Planning::SegmentRangeStatus ProcessPathSegmentPointTurn(f32 &shortestDistanceToPath_mm, f32 &radDiff)
      {
        const Planning::PathSegmentDef::s_turn* currSeg = &(path_[currPathSegment_].GetDef().turn);

#if(DEBUG_PATH_FOLLOWER)
        Radians currOrientation = Localization::GetCurrPose_angle();
        AnkiDebug( 347, "PathFollower.ProcessPathSegmentPointTurn", 590, "currPathSeg: %d, TURN currAngle: %f, targetAngle: %f", 3,
               currPathSegment_, currOrientation.ToFloat(), currSeg->targetAngle);
#endif

        // When the car is stopped, initiate point turn
        if (!pointTurnStarted_) {
#if(DEBUG_PATH_FOLLOWER)
          AnkiDebug( 348, "PathFollower.ProcessPathSegmentPointTurn.ExecutePointTurn", 305, "", 0);
#endif
          SteeringController::ExecutePointTurn(currSeg->targetAngle,
                                               path_[currPathSegment_].GetTargetSpeed(),
                                               path_[currPathSegment_].GetAccel(),
                                               path_[currPathSegment_].GetDecel(),
                                               currSeg->angleTolerance,
                                               currSeg->useShortestDir);
          pointTurnStarted_ = true;

        } else {
          if (SteeringController::GetMode() != SteeringController::SM_POINT_TURN) {
            pointTurnStarted_ = false;
            return Planning::OOR_NEAR_END;
          }
        }


        return Planning::IN_SEGMENT_RANGE;
      }

      // Post-path completion cleanup
      void PathComplete()
      {
        pointTurnStarted_ = false;
        currPathSegment_ = -1;
        realPathSegment_ = -1;

        manualSpeedControl_ = false;
        manualPathSpeed_ = 0;

        AnkiEvent( 349, "PathFollower.PathComplete", 305, "", 0);
      }


      bool GetPathError(f32 &shortestDistanceToPath_mm, f32 &radDiff)
      {
        if (!IsTraversingPath()) {
          return false;
        }

        shortestDistanceToPath_mm = distToPath_mm_;
        radDiff = radToPath_;
        return true;
      }



      Result Update()
      {
        if (currPathSegment_ < 0) {
          SpeedController::SetUserCommandedDesiredVehicleSpeed(0);
          return RESULT_FAIL;
        }

        Planning::SegmentRangeStatus segRes = Planning::OOR_NEAR_END;
        switch (path_[currPathSegment_].GetType()) {
          case Planning::PST_LINE:
          case Planning::PST_ARC:
            segRes = ProcessPathSegment(distToPath_mm_, radToPath_);
            break;
          case Planning::PST_POINT_TURN:
            segRes = ProcessPathSegmentPointTurn(distToPath_mm_, radToPath_);
            break;
          default:
            AnkiWarn( 350, "PathFollower.Update.InvalidSegmentType", 591, "Segment %d has invalid type %d", 2, currPathSegment_, path_[currPathSegment_].GetType());
            break;
        }

#if(DEBUG_PATH_FOLLOWER)
        AnkiDebug( 351, "PathFollower.Update.DistToPath", 592, "%f mm, %f deg, segRes %d, segType %d, currSeg %d", 5, distToPath_mm_, RAD_TO_DEG(radToPath_), segRes, path_[currPathSegment_].GetType(), currPathSegment_);
#endif

        // Go to next path segment if no longer in range of the current one
        if (segRes == Planning::OOR_NEAR_END) {
          if (++currPathSegment_ >= path_.GetNumSegments()) {
            // Path is complete
            PathComplete();
            return RESULT_OK;
          }
          ++realPathSegment_;
          startedDecelOnSegment_ = false;

          // Command new speed for segment
          // (Except for point turns whose speeds are handled at the steering controller level)
          if (path_[currPathSegment_].GetType() != Planning::PST_POINT_TURN) {
            SpeedController::SetUserCommandedDesiredVehicleSpeed( path_[currPathSegment_].GetTargetSpeed() );
            SpeedController::SetUserCommandedAcceleration( path_[currPathSegment_].GetAccel() );
            SpeedController::SetUserCommandedDeceleration( path_[currPathSegment_].GetDecel() );
          }
#if(DEBUG_PATH_FOLLOWER)
          AnkiDebug( 352, "PathFollower.Update.SegmentSpeed", 593, "Segment %d, speed = %f, accel = %f, decel = %f", 4,
                currPathSegment_,
                path_[currPathSegment_].GetTargetSpeed(),
                path_[currPathSegment_].GetAccel(),
                path_[currPathSegment_].GetDecel());
#endif

#if(RESET_INTEGRAL_GAINS_AT_END_OF_SEGMENT)
          WheelController::ResetIntegralGainSums();
#endif

        }

        // If in manual speed control, apply speed here
        if (manualSpeedControl_) {
          SpeedController::SetUserCommandedDesiredVehicleSpeed( manualPathSpeed_ );
          SpeedController::SetUserCommandedAcceleration( manualPathAccel_ );
          SpeedController::SetUserCommandedDeceleration( manualPathDecel_ );
        }

        if (!DockingController::IsBusy()) {
          // Check that starting error is not too big
          // TODO: Check for excessive heading error as well?
          if (distToPath_mm_ > TOO_FAR_FROM_PATH_DIST_MM) {
            currPathSegment_ = -1;
            realPathSegment_ = -1;
#if(DEBUG_PATH_FOLLOWER)
            AnkiWarn( 353, "PathFollower.Update.StartingErrorTooLarge", 594, "%f mm", 1, distToPath_mm_);
#endif
            return RESULT_FAIL;
          }
        }

        return RESULT_OK;
      }


      void PrintPath()
      {
        path_.PrintPath();
      }

      void PrintPathSegment(s16 segment)
      {
        path_.PrintSegment(segment);
      }

      u16 GetLastPathID() {
        return lastPathID_;
      }

      const Planning::Path& GetPath()
      {
        return path_;
      }


      bool DriveStraight(f32 dist_mm, f32 acc_start_frac, f32 acc_end_frac, f32 duration_sec)
      {
        VelocityProfileGenerator vpg;

        // Compute profile
        f32 curr_x, curr_y;
        Radians curr_angle;
        Localization::GetDriveCenterPose(curr_x, curr_y, curr_angle);
        f32 currSpeed = SpeedController::GetCurrentMeasuredVehicleSpeed();

        // Check for valid fractions
        if (acc_start_frac < 0 || acc_end_frac < 0) {
          AnkiWarn( 95, "PathFollower.DriveStraight.NegativeFraction", 349, "start: %f, end: %f", 2, acc_start_frac, acc_end_frac);
          return false;
        }
        
        acc_start_frac = MAX(acc_start_frac, 0.01f);
        acc_end_frac   = MAX(acc_end_frac,   0.01f);
        
        if (!vpg.StartProfile_fixedDuration(0, currSpeed, acc_start_frac * duration_sec,
                                            dist_mm, acc_end_frac * duration_sec,
                                            MAX_WHEEL_SPEED_MMPS, MAX_WHEEL_ACCEL_MMPS2,
                                            duration_sec, CONTROL_DT) ) {
          AnkiWarn( 354, "PathFollower.DriveStraight.VPGFail", 305, "", 0);
          return false;
        }


        // Compute the destination pose
        f32 dest_x = curr_x + dist_mm * cosf(curr_angle.ToFloat());
        f32 dest_y = curr_y + dist_mm * sinf(curr_angle.ToFloat());


        // Compute start and end acceleration distances.
        // Some compensation here for lookahead distance.
        // NOTE: PathFollower actually transitions to the speed settings of the following segment
        //       at LOOK_AHEAD_DIST_MM before it actually reaches the next segment.
        f32 startAccelDist = vpg.GetStartAccelDist();
        f32 endAccelDist = vpg.GetEndAccelDist();
        if (fabsf(endAccelDist) > LOOK_AHEAD_DIST_MM) {
          endAccelDist += LOOK_AHEAD_DIST_MM * signbit(endAccelDist);
        }
        AnkiInfo( 355, "PathFollower.DriveStraight.Params", 595, "total dist %f, startDist %f, endDist %f", 3, dist_mm, startAccelDist, endAccelDist);

        // Get intermediate poses: (1) after starting accel phase and (2) before ending accel phase
        f32 int_x1 = curr_x + startAccelDist * cosf(curr_angle.ToFloat());
        f32 int_y1 = curr_y + startAccelDist * sinf(curr_angle.ToFloat());

        f32 int_x2 = dest_x - endAccelDist * cosf(curr_angle.ToFloat());
        f32 int_y2 = dest_y - endAccelDist * sinf(curr_angle.ToFloat());


        // Get intermediate speed and accels
        f32 maxReachableVel = vpg.GetMaxReachableVel();
        f32 startAccel = fabsf(vpg.GetStartAccel());
        f32 endAccel = fabsf(vpg.GetEndAccel());


        // Create 3-segment path
        AnkiDebug( 356, "PathFollower.DriveStraight.Accels", 596, "start %f, end %f, vel %f\n", 3, startAccel, endAccel, maxReachableVel);
        AnkiDebug( 357, "PathFollower.DriveStraight.Points", 597, "(%f, %f) to (%f, %f) to (%f, %f) to (%f, %f)\n", 8, curr_x, curr_y, int_x1, int_y1, int_x2, int_y2, dest_x, dest_y);
        ClearPath();
        AppendPathSegment_Line(0, curr_x, curr_y, int_x1, int_y1, maxReachableVel, startAccel, startAccel);
        AppendPathSegment_Line(0, int_x1, int_y1, int_x2, int_y2, maxReachableVel, startAccel, startAccel);
        AppendPathSegment_Line(0, int_x2, int_y2, dest_x, dest_y, dist_mm > 0 ? COAST_VELOCITY_MMPS : -COAST_VELOCITY_MMPS, endAccel, endAccel);
        StartPathTraversal();

        return true;
      }

      bool DriveArc(f32 sweep_rad, f32 radius_mm, f32 acc_start_frac, f32 acc_end_frac, f32 duration_sec)
      {
        VelocityProfileGenerator vpg;

        // Compute profile
        f32 curr_x, curr_y;
        Radians curr_angle;
        Localization::GetDriveCenterPose(curr_x, curr_y, curr_angle);
        f32 currAngSpeed = -SpeedController::GetCurrentMeasuredVehicleSpeed() / radius_mm;

        // Check for valid fractions
        if (acc_start_frac < 0 || acc_end_frac < 0) {
          AnkiWarn( 99, "PathFollower.DriveArc.NegativeFraction", 349, "start: %f, end: %f", 2, acc_start_frac, acc_end_frac);
          return false;
        }
        
        acc_start_frac = MAX(acc_start_frac, 0.01f);
        acc_end_frac   = MAX(acc_end_frac,   0.01f);
        
        if (!vpg.StartProfile_fixedDuration(0, currAngSpeed, acc_start_frac * duration_sec,
                                            sweep_rad, acc_end_frac * duration_sec,
                                            MAX_BODY_ROTATION_SPEED_RAD_PER_SEC, MAX_BODY_ROTATION_ACCEL_RAD_PER_SEC2,
                                            duration_sec, CONTROL_DT) ) {
          AnkiWarn( 358, "PathFollower.DriveArc.VPGFail", 305, "", 0);
          return false;
        }

        // Compute x_center,y_center
        f32 angToCenter = curr_angle.ToFloat() + (radius_mm > 0 ? -1 : 1) * PIDIV2_F;
        f32 absRadius = fabsf(radius_mm);
        f32 x_center = curr_x + absRadius * cosf(angToCenter);
        f32 y_center = curr_y + absRadius * sinf(angToCenter);

        // Compute startRad relative to (x_center, y_center)
        f32 startRad = angToCenter + PI_F;

        // Get intermediate poses: (1) after starting accel phase and (2) before ending accel phase
        f32 startAccelSweep = vpg.GetStartAccelDist();
        f32 endAccelSweep = vpg.GetEndAccelDist();
        //if (endAccelDist < -LOOK_AHEAD_DIST_MM) {
        //  endAccelDist += LOOK_AHEAD_DIST_MM;
        //}


        f32 int_ang1 = startRad + startAccelSweep;
        f32 int_ang2 = startRad + sweep_rad - endAccelSweep;

        // Get intermediate speed and accels.
        // Need to convert angular speeds/accel into linear speeds/accel
        f32 targetAngSpeed = fabsf(vpg.GetMaxReachableVel());
        f32 startAngAccel = fabsf(vpg.GetStartAccel());
        f32 endAngAccel = fabsf(vpg.GetEndAccel());


        u8 drivingFwd = (signbit(sweep_rad) != signbit(radius_mm)) ? 1 : -1;
        f32 targetSpeed = drivingFwd * targetAngSpeed * absRadius;
        f32 startAccel = startAngAccel * absRadius;
        f32 endAccel = endAngAccel * absRadius;


        AnkiDebug( 359, "PathFollower.DriveArc", 598, "curr_x,y  (%f, %f), center x,y (%f, %f), radius %f", 5, curr_x, curr_y, x_center, y_center, radius_mm);
        AnkiDebug( 359, "PathFollower.DriveArc", 599, "start + sweep1 = ang1 (%f + %f = %f), end + sweep2 = ang2 ang2 (%f - %f = %f)", 6, startRad, startAccelSweep, int_ang1, startRad + sweep_rad, endAccelSweep, int_ang2);
        AnkiDebug( 359, "PathFollower.DriveArc", 600, "targetSpeed %f, startAccel %f, endAccel %f", 3, targetSpeed, startAccel, endAccel);

        // Create 3-segment path
        ClearPath();
        AppendPathSegment_Arc(0, x_center, y_center, absRadius, startRad, startAccelSweep, targetSpeed, startAccel, startAccel);
        AppendPathSegment_Arc(0, x_center, y_center, absRadius, int_ang1, int_ang2-int_ang1, targetSpeed, startAccel, startAccel);
        AppendPathSegment_Arc(0, x_center, y_center, absRadius, int_ang2, endAccelSweep, drivingFwd > 0 ? COAST_VELOCITY_MMPS : -COAST_VELOCITY_MMPS, endAccel, endAccel);

        StartPathTraversal();

        return true;
      }

      bool DrivePointTurn(f32 sweep_rad, f32 acc_start_frac, f32 acc_end_frac, f32 angleTolerance, f32 duration_sec)
      {
        VelocityProfileGenerator vpg;

        f32 curr_x, curr_y;
        Radians curr_angle;
        Localization::GetDriveCenterPose(curr_x, curr_y, curr_angle);

        // Check for valid fractions
        if (acc_start_frac < 0 || acc_end_frac < 0) {
          AnkiWarn( 97, "PathFollower.DrivePointTurn.NegativeFraction", 349, "start: %f, end: %f", 2, acc_start_frac, acc_end_frac);
          return false;
        }
        
        acc_start_frac = MAX(acc_start_frac, 0.01f);
        acc_end_frac   = MAX(acc_end_frac,   0.01f);
        
        if (!vpg.StartProfile_fixedDuration(0, 0, acc_start_frac * duration_sec,
                                            sweep_rad, acc_end_frac * duration_sec,
                                            MAX_BODY_ROTATION_SPEED_RAD_PER_SEC, MAX_BODY_ROTATION_ACCEL_RAD_PER_SEC2,
                                            duration_sec, CONTROL_DT)) {
          AnkiWarn( 360, "PathFollower.DrivePointTurn.VPGFail", 601, "sweep_rad: %f, acc_start_frac %f, acc_end_frac %f, duration_sec %f", 4,
                   sweep_rad, acc_start_frac, acc_end_frac, duration_sec);
          return false;
        }


        f32 targetRotVel = vpg.GetMaxReachableVel();
        f32 startAccelSweep = vpg.GetStartAccelDist();
        f32 endAccelSweep = vpg.GetEndAccelDist();
        f32 startAngAccel = fabsf(vpg.GetStartAccel());
        f32 endAngAccel = fabsf(vpg.GetEndAccel());

        // Compute intermediate angles
        f32 dest_ang = curr_angle.ToFloat() + sweep_rad;
        f32 int_ang1 = curr_angle.ToFloat() + startAccelSweep;
        f32 int_ang2 = dest_ang - endAccelSweep;


        AnkiDebug( 361, "PathFollower.DrivePointTurn", 602, "start %f, int_ang1 %f, int_ang2 %f, dest %f", 4, curr_angle.ToFloat(), int_ang1, int_ang2, dest_ang);
        AnkiDebug( 361, "PathFollower.DrivePointTurn", 603, "targetRotSpeed %f, startRotAccel %f, endRotAccel %f", 3, targetRotVel, startAngAccel, endAngAccel);

        // Create 3-segment path
        ClearPath();
        AppendPathSegment_PointTurn(0, curr_x, curr_y, int_ang1, targetRotVel, startAngAccel, startAngAccel, angleTolerance, false);
        AppendPathSegment_PointTurn(0, curr_x, curr_y, int_ang2, targetRotVel, startAngAccel, startAngAccel, angleTolerance, false);
        AppendPathSegment_PointTurn(0, curr_x, curr_y, dest_ang, sweep_rad > 0 ? COAST_VELOCITY_RADPS : -COAST_VELOCITY_RADPS, endAngAccel, endAngAccel, angleTolerance, false);

        StartPathTraversal();

        return true;
      }





    } // namespace PathFollower
  } // namespace Cozmo
} // namespace Anki

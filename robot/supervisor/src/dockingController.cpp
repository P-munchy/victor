#include "anki/common/robot/config.h"
#include "anki/common/robot/trig_fast.h"
#include "dockingController.h"
#include "gripController.h"
#include "headController.h"
#include "liftController.h"

#include "anki/cozmo/robot/cozmoConfig.h"
#include "anki/common/robot/geometry.h"
#include "anki/cozmo/robot/hal.h"
#include "localization.h"
#include "visionSystem.h"
#include "speedController.h"
#include "steeringController.h"
#include "pathFollower.h"
#include "messages.h"


#define DEBUG_DOCK_CONTROLLER 0

// Resets localization pose to (0,0,0) every time a relative block pose update is received.
// Recalculates the start pose of the path that is at the same position relative to the block
// as it was when tracking was initiated. If encoder-based localization is reasonably accurate,
// this probably won't be necessary.
#define RESET_LOC_ON_BLOCK_UPDATE 0

namespace Anki {
  namespace Cozmo {
    namespace DockingController {
      
      namespace {
        
        // Constants
        
        enum Mode {
          IDLE,
          LOOKING_FOR_BLOCK,
          APPROACH_FOR_DOCK
        };

        // Turning radius of docking path
        const f32 DOCK_PATH_START_RADIUS_MM = 50;
        const f32 DOCK_PATH_END_RADIUS_MM = 100;
        
        // The length of the straight tail end of the dock path.
        // Should be roughly the length of the forks on the lift.
        const f32 FINAL_APPROACH_STRAIGHT_SEGMENT_LENGTH_MM = 30;

        //const f32 FAR_DIST_TO_BLOCK_THRESH_MM = 100;
        
        // Distance from block face at which robot should "dock"
        f32 dockOffsetDistX_ = 0.f;
        
        u32 lastDockingErrorSignalRecvdTime_ = 0;
        
        // If error signal not received in this amount of time, tracking is considered to have failed.
        const u32 STOPPED_TRACKING_TIMEOUT_US = 500000;
        
        // If an initial track cannot start for this amount of time, block is considered to be out of
        // view and docking is aborted.
        const u32 GIVEUP_DOCKING_TIMEOUT_US = 1000000;
        
        const u16 DOCK_APPROACH_SPEED_MMPS = 10;
        //const u16 DOCK_FAR_APPROACH_SPEED_MMPS = 30;
        const u16 DOCK_APPROACH_ACCEL_MMPS2 = 60;
        const u16 DOCK_APPROACH_DECEL_MMPS2 = 200;
        
        // Lateral tolerance at dock pose
        const u16 LATERAL_DOCK_TOLERANCE_AT_DOCK_MM = 1;
        
        // Code of the VisionMarker we are trying to dock to
        Vision::MarkerType dockMarker_;

        Mode mode_ = IDLE;
        
        // Whether or not the last docking attempt succeeded
        bool success_  = false;

        // True if docking off one relative position signal
        // received via StartDockingToRelPose().
        // i.e. no vision marker required.
        bool markerlessDocking_ = false;
        
        // Whether or not a valid path was generated from the received error signal
        bool createdValidPath_ = false;
        
        // Whether or not we're already following the block surface normal as a path
        bool followingBlockNormalPath_ = false;
        
        // The pose of the robot at the start of docking.
        // While block tracking is maintained the robot follows
        // a path from this initial pose to the docking pose.
        Anki::Embedded::Pose2d approachStartPose_;

        // The pose of the block as we're docking
        Anki::Embedded::Pose2d blockPose_;

        // The docking pose
        Anki::Embedded::Pose2d dockPose_;
        
#if(RESET_LOC_ON_BLOCK_UPDATE)
        // Since the physical robot currently does not localize,
        // we need to store the transform from docking pose
        // to the approachStartPose, which we then use to compute
        // a new approachStartPose with every block pose update.
        // We're faking a different approachStartPose because without
        // localization it looks like the block is moving and not the robot.

        f32 approachPath_dist, approachPath_dtheta, approachPath_dOrientation;
#endif
        
        // Whether or not the lift should track the angle of the camera so that the
        // lift crossbar is just out of the field of view of the camera.
        bool trackCamWithLift_ = false;
        
        // If trackCamWithLift_ == true, start actually doing the tracking only when
        // the block is at least START_LIFT_TRACKING_DIST_MM close and START_LIFT_TRACKING_HEIGHT_MM high
        const f32 START_LIFT_TRACKING_DIST_MM = 70.f;
        const f32 START_LIFT_TRACKING_HEIGHT_MM = 44.f;
        
      } // "private" namespace
      
      bool IsBusy()
      {
        return (mode_ != IDLE);
      }
      
      bool DidLastDockSucceed()
      {
        return success_;
      }
      
      void TrackCamWithLift(bool on)
      {
        trackCamWithLift_ = on;
      }
      
      // Returns the height that the lift should be moved to such that the
      // lift crossbar is just out of the field of view of the camera.
      // TODO: Should this be in some kinematics module where we have functions to get things wrt other things?
      f32 GetCamFOVLowerHeight()
      {
        f32 x, z, angle, liftH;
        HeadController::GetCamPose(x, z, angle);
 
        // Compute the angle of the line extending from the camera that represents
        // the lower bound of its field of view
        f32 lowerCamFOVangle = angle - 0.45f * VisionSystem::GetVerticalFOV();
        
        // Compute the lift height required to raise the cross bar to be at
        // the height of that line.
        // TODO: This is really rough computation approximating with a fixed horizontal distance between
        //       the camera and the lift. make better!
        const f32 liftDistToCam = 26;
        liftH = liftDistToCam * sinf(lowerCamFOVangle) + z;
        liftH -= LIFT_XBAR_HEIGHT_WRT_WRIST_JOINT;
        
        //PRINT("CAM POSE: x %f, z %f, angle %f (lowerCamAngle %f, liftH %f)\n", x, z, angle, lowerCamFOVangle, liftH);
        
        return CLIP(liftH, LIFT_HEIGHT_LOWDOCK, LIFT_HEIGHT_CARRY);
      }
      
      Result Update()
      {
        
        // Get any docking error signal available from the vision system
        // and update our path accordingly.
        Messages::DockingErrorSignal dockMsg;
        while( Messages::CheckMailbox(dockMsg) )
        {
          
#if(0)
          // Print period of tracker (i.e. messages coming in from tracker)
          static u32 lastTime = 0;
          u32 currTime = HAL::GetMicroCounter();
          if (lastTime != 0) {
            u32 period = (currTime - lastTime)/1000;
            PRINT("PERIOD: %d ms\n", period);
          }
          lastTime = currTime;
#endif
          
          
          if(dockMsg.didTrackingSucceed) {
            
            //PRINT("ErrSignal %d (msgTime %d)\n", HAL::GetMicroCounter(), dockMsg.timestamp);
            
            // Convert from camera coordinates to robot coordinates
            if(dockMsg.isApproximate) 
            {
              dockMsg.x_distErr += HEAD_CAM_POSITION[0]*cosf(HeadController::GetAngleRad()) + NECK_JOINT_POSITION[0];
            }
            else {
              Embedded::Point3<f32> tempPoint;
              VisionSystem::GetWithRespectToRobot(Embedded::Point3<f32>(dockMsg.x_distErr, dockMsg.y_horErr, dockMsg.z_height),
                                                  tempPoint);
              
              dockMsg.x_distErr = tempPoint.x;
              dockMsg.y_horErr  = tempPoint.y + ( (HAL::GetIDCard()->esn == 2) ? COZMO2_CAM_LATERAL_POSITION_HACK : 0 );
              dockMsg.z_height  = tempPoint.z;
            }
            
#if(DEBUG_DOCK_CONTROLLER)
            PRINT("Received%sdocking error signal: x_distErr=%f, y_horErr=%f, "
                  "z_height=%f, angleErr=%fdeg\n",
                  (dockMsg.isApproximate ? " approximate " : " "),
                  dockMsg.x_distErr, dockMsg.y_horErr,
                  dockMsg.z_height, RAD_TO_DEG_F32(dockMsg.angleErr));
#endif
            
            // Check that error signal is plausible
            // If not, treat as if tracking failed.
            // TODO: Get tracker to detect these situations and not even send the error message here.
            if (dockMsg.x_distErr > 0 && ABS(dockMsg.angleErr) < 0.75*PIDIV2_F) {
             
              // Set relative block pose to start/continue docking
              SetRelDockPose(dockMsg.x_distErr, dockMsg.y_horErr, dockMsg.angleErr);

              if(!dockMsg.isApproximate) // will be -1 if not computed
              {
                // If we have the height of the marker for docking, we can also
                // compute the head angle to keep it centered
                HeadController::SetSpeedAndAccel(0.2, 1);
                //f32 desiredHeadAngle = atan_fast( (dockMsg.z_height - NECK_JOINT_POSITION[2])/dockMsg.x_distErr);
                
                // Make sure bottom of camera FOV doesn't tilt below the bottom of the block
                // or that the camera FOV center doesn't tilt below the marker center.
                // Otherwise try to maintain the lowest tilt possible
                f32 minDesiredHeadAngle1 = atan_fast( (dockMsg.z_height - NECK_JOINT_POSITION[2] - 20.f)/dockMsg.x_distErr) + 0.5*VisionSystem::GetVerticalFOV(); // TODO: Marker size should come from VisionSystem?
                f32 minDesiredHeadAngle2 = atan_fast( (dockMsg.z_height - NECK_JOINT_POSITION[2])/dockMsg.x_distErr);
                f32 desiredHeadAngle = MAX(minDesiredHeadAngle1, minDesiredHeadAngle2);
                
                HeadController::SetDesiredAngle(desiredHeadAngle);
                //PRINT("desHeadAngle %f\n", desiredHeadAngle);
                
                // Track camera with lift.
                // Do it only when it's a high block and we're within a certain distance of it.
                // Don't lift higher than HIGHDOCK height.
                if (trackCamWithLift_ &&
                    dockMsg.z_height > START_LIFT_TRACKING_HEIGHT_MM &&
                    dockMsg.x_distErr < START_LIFT_TRACKING_DIST_MM) {
                  f32 liftHeight = GetCamFOVLowerHeight();
                  if (liftHeight > LIFT_HEIGHT_HIGHDOCK) {
                    liftHeight = LIFT_HEIGHT_HIGHDOCK;
                  }
                  //PRINT("TrackLiftHeight: %f\n", liftHeight);
                  LiftController::SetDesiredHeight(liftHeight);
                }
              }
              
              // Send to basestation for visualization
              HAL::RadioSendMessage(GET_MESSAGE_ID(Messages::DockingErrorSignal), &dockMsg);
              continue;
            }

          }  // IF tracking succeeded
          
          SpeedController::SetUserCommandedDesiredVehicleSpeed(0);
          //PathFollower::ClearPath();
          SteeringController::ExecuteDirectDrive(0,0);
          if (mode_ != IDLE) {
            mode_ = LOOKING_FOR_BLOCK;
          }

            
          
          
        } // while dockErrSignalMailbox has mail

        
        Result retVal = RESULT_OK;
        
        switch(mode_)
        {
          case IDLE:
            break;
          case LOOKING_FOR_BLOCK:
            if (HAL::GetMicroCounter() - lastDockingErrorSignalRecvdTime_ > GIVEUP_DOCKING_TIMEOUT_US) {
              ResetDocker();
#if(DEBUG_DOCK_CONTROLLER)
              PRINT("Too long without block pose (currTime %d, lastErrSignal %d). Giving up.\n", HAL::GetMicroCounter(), lastDockingErrorSignalRecvdTime_);
#endif
            }
            break;
          case APPROACH_FOR_DOCK:
          {
            // Stop if we haven't received error signal for a while
            if (!markerlessDocking_ && HAL::GetMicroCounter() - lastDockingErrorSignalRecvdTime_ > STOPPED_TRACKING_TIMEOUT_US) {
              PathFollower::ClearPath();
              SpeedController::SetUserCommandedDesiredVehicleSpeed(0);
              mode_ = LOOKING_FOR_BLOCK;
#if(DEBUG_DOCK_CONTROLLER)
              PRINT("Too long without block pose (currTime %d, lastErrSignal %d). Looking for block...\n", HAL::GetMicroCounter(), lastDockingErrorSignalRecvdTime_);
#endif
              break;
            }
            
            // If finished traversing path
            if (createdValidPath_ && !PathFollower::IsTraversingPath()) {
#if(DEBUG_DOCK_CONTROLLER)
              PRINT("*** DOCKING SUCCESS ***\n");
#endif
              ResetDocker();
              success_ = true;
              break;
            }
            
            break;
          }
          default:
            mode_ = IDLE;
            success_ = false;
            PRINT("Reached default case in DockingController "
                  "mode switch statement.(1)\n");
            break;
        }
        

        if(success_ == false)
        {
          retVal = RESULT_FAIL;
        }
        
        return retVal;
        
      } // Update()
      
      
      void SetRelDockPose(f32 rel_x, f32 rel_y, f32 rel_rad)
      {
        // Check for readings that we do not expect to get
        if (rel_x < 0 || ABS(rel_rad) > 0.75*PIDIV2_F
            ) {
          PRINT("WARN: Ignoring out of range docking error signal (%f, %f, %f)\n", rel_x, rel_y, rel_rad);
          return;
        }
      
        
        lastDockingErrorSignalRecvdTime_ = HAL::GetMicroCounter();
        
        if (mode_ == IDLE || success_) {
          // We already accomplished the dock. We're done!
          return;
        }
        
#if(RESET_LOC_ON_BLOCK_UPDATE)
        // Reset localization to zero buildup of localization error.
        Localization::Init();
#endif
        
        // Set mode to approach if looking for a block
        if (mode_ == LOOKING_FOR_BLOCK) {
          
          mode_ = APPROACH_FOR_DOCK;

          // Set approach start pose
          Localization::GetCurrentMatPose(approachStartPose_.x(), approachStartPose_.y(), approachStartPose_.angle);
          
#if(RESET_LOC_ON_BLOCK_UPDATE)
          // If there is no localization (as is currently the case on the robot)
          // we adjust the path's starting point as the robot progresses along
          // the path so that the relative position of the starting point to the
          // block is the same as it was when tracking first started.
          approachPath_dist = sqrtf(rel_x*rel_x + rel_y*rel_y);
          approachPath_dtheta = atan2_acc(rel_y, rel_x);
          approachPath_dOrientation = rel_rad;
          
          //PRINT("Approach start delta: distToBlock: %f, angleToBlock: %f, blockAngleRelToRobot: %f\n", approachPath_dist,approachPath_dtheta, approachPath_dOrientation);
#endif
          
          followingBlockNormalPath_ = false;
        }
        
        // Clear current path
        PathFollower::ClearPath();
        
        // Create new path that is aligned with the normal of the block we want to dock to.
        // End point: Where the robot origin should be by the time the robot has docked.
        // Start point: Projected from end point at specified rad.
        //              Just make length as long as distance to block.
        //
        //   ______
        //   |     |
        //   |     *  End ---------- Start              * == (rel_x, rel_y)
        //   |_____|      \ ) rad
        //    Block        \
        //                  \
        //                   \ Aligned with robot x axis (but opposite direction)
        //
        //
        //               \ +ve x axis
        //                \
        //                / ROBOT
        //               /
        //              +ve y-axis
        
        
        if (rel_x <= dockOffsetDistX_ && ABS(rel_y) < LATERAL_DOCK_TOLERANCE_AT_DOCK_MM) {
#if(DEBUG_DOCK_CONTROLLER)
          PRINT("DOCK POSE REACHED\n");
#endif
          return;
        }
        
        Anki::Embedded::Pose2d currPose;
        Localization::GetCurrentMatPose(currPose.x(), currPose.y(), currPose.angle);
        
        // Compute absolute block pose
        f32 distToBlock = sqrtf((rel_x * rel_x) + (rel_y * rel_y));
        f32 rel_angle_to_block = atan2_acc(rel_y, rel_x);
        blockPose_.x() = currPose.x() + distToBlock * cosf(rel_angle_to_block + currPose.angle.ToFloat());
        blockPose_.y() = currPose.y() + distToBlock * sinf(rel_angle_to_block + currPose.angle.ToFloat());
        blockPose_.angle = currPose.angle + rel_rad;
        
        
#if(RESET_LOC_ON_BLOCK_UPDATE)
        // Rotate block so that it is parallel with approach start pose
        f32 rel_blockAngle = rel_rad - approachPath_dOrientation;
        
        // Subtract dtheta so that angle points to where start pose is
        rel_blockAngle += approachPath_dtheta;
        
        // Compute dx and dy from block pose in current robot frame
        f32 dx = approachPath_dist * cosf(rel_blockAngle);
        f32 dy = approachPath_dist * sinf(rel_blockAngle);

        approachStartPose_.x() = blockPose_.x() - dx;
        approachStartPose_.y() = blockPose_.y() - dy;
        approachStartPose_.angle = rel_blockAngle - approachPath_dtheta;
        
        //PRINT("Approach start pose: x = %f, y = %f, angle = %f\n", approachStartPose_.x(), approachStartPose_.y(), approachStartPose_.angle.ToFloat());
#endif

        
        // Compute dock pose
        dockPose_.x() = blockPose_.x() - dockOffsetDistX_ * cosf(blockPose_.angle.ToFloat());
        dockPose_.y() = blockPose_.y() - dockOffsetDistX_ * sinf(blockPose_.angle.ToFloat());
        dockPose_.angle = blockPose_.angle;
        
        
        f32 path_length;
        u8 numPathSegments = PathFollower::GenerateDubinsPath(approachStartPose_.x(),
                                                              approachStartPose_.y(),
                                                              approachStartPose_.angle.ToFloat(),
                                                              dockPose_.x(),
                                                              dockPose_.y(),
                                                              dockPose_.angle.ToFloat(),
                                                              DOCK_PATH_START_RADIUS_MM,
                                                              DOCK_PATH_END_RADIUS_MM,
                                                              DOCK_APPROACH_SPEED_MMPS,
                                                              DOCK_APPROACH_ACCEL_MMPS2,
                                                              DOCK_APPROACH_DECEL_MMPS2,
                                                              FINAL_APPROACH_STRAIGHT_SEGMENT_LENGTH_MM,
                                                              &path_length);

        //PRINT("numPathSegments: %d, path_length: %f, distToBlock: %f, followBlockNormalPath: %d\n",
        //      numPathSegments, path_length, distToBlock, followingBlockNormalPath_);

        
        // No reasonable Dubins path exists.
        // Either try again with smaller radii or just let the controller
        // attempt to get on to a straight line normal path.
        if (numPathSegments == 0 || path_length > 2 * distToBlock || followingBlockNormalPath_) {
          
          // Compute new starting point for path
          // HACK: Feeling lazy, just multiplying path by some scalar so that it's likely to be behind the current robot pose.
          f32 x_start_mm = dockPose_.x() - 3 * distToBlock * cosf(dockPose_.angle.ToFloat());
          f32 y_start_mm = dockPose_.y() - 3 * distToBlock * sinf(dockPose_.angle.ToFloat());
          
          PathFollower::ClearPath();
          PathFollower::AppendPathSegment_Line(0, x_start_mm, y_start_mm, dockPose_.x(), dockPose_.y(),
                                               DOCK_APPROACH_SPEED_MMPS, DOCK_APPROACH_ACCEL_MMPS2, DOCK_APPROACH_DECEL_MMPS2);
          
          followingBlockNormalPath_ = true;
          //PRINT("Computing straight line path (%f, %f) to (%f, %f)\n", x_start_m, y_start_m, dockPose_.x(), dockPose_.y());
        }

        /*
        // Set speed
        // TODO: Add hysteresis
        if (distToBlock < FAR_DIST_TO_BLOCK_THRESH_MM) {
          SpeedController::SetUserCommandedDesiredVehicleSpeed( DOCK_APPROACH_SPEED_MMPS );
        } else {
          SpeedController::SetUserCommandedDesiredVehicleSpeed( DOCK_FAR_APPROACH_SPEED_MMPS );
        }
        SpeedController::SetUserCommandedAcceleration( DOCK_APPROACH_ACCEL_MMPS2 );
        */
        
        // Start following path
        createdValidPath_ = PathFollower::StartPathTraversal();
        
        // Debug
        if (!createdValidPath_) {
          PRINT("ERROR DockingController: Failed to create path\n");
          PathFollower::PrintPath();
        }
        
      }
      
      
      void StartDocking(const Vision::MarkerType& dockingMarker,
                        const f32 markerWidth_mm,
                        const f32 dockOffsetDistX, const f32 dockOffsetDistY, const f32 dockOffsetAngle)
      {
        StartDocking(dockingMarker, markerWidth_mm, Embedded::Point2f(-1,-1), u8_MAX, dockOffsetDistX, dockOffsetDistY, dockOffsetAngle);
      }

      void StartDocking(const Vision::MarkerType& dockingMarker,
                        const f32 markerWidth_mm,
                        const Embedded::Point2f &markerCenter, const u8 pixel_radius,
                        const f32 dockOffsetDistX, const f32 dockOffsetDistY, const f32 dockOffsetAngle)
      {
        AnkiAssert(markerWidth_mm > 0.f);
        
        dockMarker_      = dockingMarker;
        dockOffsetDistX_ = dockOffsetDistX;
        
        if (pixel_radius == u8_MAX) {
          VisionSystem::SetMarkerToTrack(dockMarker_, markerWidth_mm);
        } else {
          VisionSystem::SetMarkerToTrack(dockMarker_, markerWidth_mm, markerCenter, static_cast<f32>(pixel_radius));
        }
        
        lastDockingErrorSignalRecvdTime_ = HAL::GetMicroCounter();
        mode_ = LOOKING_FOR_BLOCK;
        
        success_ = false;
      }
      
      void StartDockingToRelPose(const f32 rel_x, const f32 rel_y, const f32 rel_angle)
      {
        mode_ = LOOKING_FOR_BLOCK;
        markerlessDocking_ = true;
        SetRelDockPose(rel_x, rel_y, rel_angle);
        success_ = false;
      }


      void ResetDocker() {
        
        SpeedController::SetUserCommandedDesiredVehicleSpeed(0);
        PathFollower::ClearPath();
        SteeringController::ExecuteDirectDrive(0,0);
        mode_ = IDLE;
        
        // Command VisionSystem to stop processing images
        VisionSystem::StopTracking();

        markerlessDocking_ = false;
        success_ = false;
      }
      
      } // namespace DockingController
    } // namespace Cozmo
  } // namespace Anki

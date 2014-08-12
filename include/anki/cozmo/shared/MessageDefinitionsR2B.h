// All message definitions should look like this:
//
//   START_MESSAGE_DEFINITION(MessageTypeName, Priority)
//   ADD_MESSAGE_MEMBER(MemberType, MemberName)
//   ADD_MESSAGE_MEMBER(MemberType, MemberName)
//   ...
//   END_MESSAGE_DEFINITION(MessageTypeName)
//
// For example:
//
//   START_MESSAGE_DEFINITION(CozmoMsg_Foo, 1)
//   ADD_MESSAGE_MEMBER(f32, fooMember1)
//   ADD_MESSAGE_MEMBER(u16, fooMember2)
//   ADD_MESSAGE_MEMBER(u8,  fooMember3)
//   END_MESSAGE_DEFINITION(CozmoMsg_Foo)
//
// To add a timestamp member, use the START_TIMESTAMPED_MESSAGE_DEFINITION
// command, which will add a member "timestamp" to the struct, with type
// "TimeStamp".
//
// IMPORTANT NOTE: You should always add members from largest to smallest type!
//                 (This prevents memory alignment badness when doing memcopies
//                  or casts later.)
//

// This file is not specific to robot or basestation, as it defines the common
// message protocol between them.  However, the macros used to generate actual
// code based on the definitions below *is* specific to the two platforms.
// Include the correct one based on the definition of COZMO_ROBOT / COZMO_BASESTATION

#if defined(COZMO_ROBOT)
#include "anki/cozmo/shared/MessageDefMacros_Robot.h"
#elif defined(COZMO_BASESTATION)
#include "anki/cozmo/shared/MessageDefMacros_Basestation.h"
#else
#error Either COZMO_ROBOT or COZMO_BASESTATION should be defined.
#endif

#if 0 // EXAMPLE
// Foo message
START_MESSAGE_DEFINITION(Foo, 1)
ADD_MESSAGE_MEMBER(f32, fooMember1)
ADD_MESSAGE_MEMBER(u16, fooMember2)
ADD_MESSAGE_MEMBER(u8,  fooMember3)
END_MESSAGE_DEFINITION(Foo)
#endif 

START_TIMESTAMPED_MESSAGE_DEFINITION(RobotState, 1)
ADD_MESSAGE_MEMBER(u32, pose_frame_id)
ADD_MESSAGE_MEMBER(f32, pose_x)
ADD_MESSAGE_MEMBER(f32, pose_y)
ADD_MESSAGE_MEMBER(f32, pose_z)
ADD_MESSAGE_MEMBER(f32, pose_angle)
ADD_MESSAGE_MEMBER(f32, pose_pitch_angle)
ADD_MESSAGE_MEMBER(f32, lwheel_speed_mmps)
ADD_MESSAGE_MEMBER(f32, rwheel_speed_mmps)
ADD_MESSAGE_MEMBER(f32, headAngle)
ADD_MESSAGE_MEMBER(f32, liftAngle)
ADD_MESSAGE_MEMBER(f32, liftHeight) // TODO: Need this?
ADD_MESSAGE_MEMBER(u16, lastPathID)
ADD_MESSAGE_MEMBER(s8, currPathSegment) // -1 if not traversing a path
ADD_MESSAGE_MEMBER(u8, numFreeSegmentSlots)
ADD_MESSAGE_MEMBER(u8, status)  // See RobotStatusFlag
// ...
END_MESSAGE_DEFINITION(RobotState)


// VisionMarker
//#define VISION_MARKER_CODE_LENGTH 11 // ceil( (9*9 + 4)/8 )
START_TIMESTAMPED_MESSAGE_DEFINITION(VisionMarker, 1)
// TODO: make the corner coordinates fixed point
ADD_MESSAGE_MEMBER(f32, x_imgUpperLeft)
ADD_MESSAGE_MEMBER(f32, y_imgUpperLeft)
ADD_MESSAGE_MEMBER(f32, x_imgLowerLeft)
ADD_MESSAGE_MEMBER(f32, y_imgLowerLeft)
ADD_MESSAGE_MEMBER(f32, x_imgUpperRight)
ADD_MESSAGE_MEMBER(f32, y_imgUpperRight)
ADD_MESSAGE_MEMBER(f32, x_imgLowerRight)
ADD_MESSAGE_MEMBER(f32, y_imgLowerRight)
ADD_MESSAGE_MEMBER(u16, markerType)
//ADD_MESSAGE_MEMBER_ARRAY(u8, code, VISION_MARKER_CODE_LENGTH)
END_MESSAGE_DEFINITION(VisionMarker)

// DockingErrorSignal
START_TIMESTAMPED_MESSAGE_DEFINITION(DockingErrorSignal, 1)
ADD_MESSAGE_MEMBER(f32, x_distErr)
ADD_MESSAGE_MEMBER(f32, y_horErr)
ADD_MESSAGE_MEMBER(f32, z_height)
ADD_MESSAGE_MEMBER(f32, angleErr) // in radians
ADD_MESSAGE_MEMBER(u8,  didTrackingSucceed)
ADD_MESSAGE_MEMBER(u8,  isApproximate)
END_MESSAGE_DEFINITION(DockingErrorSignal)

// BlockPickedUp
START_TIMESTAMPED_MESSAGE_DEFINITION(BlockPickedUp, 1)
ADD_MESSAGE_MEMBER(bool, didSucceed) // true if robot thinks it picked up a block (from low or high position)
END_MESSAGE_DEFINITION(BlockPickedUp)

// BlockPlaced
START_TIMESTAMPED_MESSAGE_DEFINITION(BlockPlaced, 1)
ADD_MESSAGE_MEMBER(bool, didSucceed) // true if robot thinks it placed up a block (from low or high position)
END_MESSAGE_DEFINITION(BlockPlaced)

// RampTraverseStart
START_TIMESTAMPED_MESSAGE_DEFINITION(RampTraverseStart, 1)
END_MESSAGE_DEFINITION(RampTraverseStart)

// RampTraverseComplete
START_TIMESTAMPED_MESSAGE_DEFINITION(RampTraverseComplete, 1)
ADD_MESSAGE_MEMBER(bool, didSucceed) // true if robot thinks it finished traversing (the sloped part of) a ramp
END_MESSAGE_DEFINITION(RampTraverseComplete)

// BridgeTraverseStart
START_TIMESTAMPED_MESSAGE_DEFINITION(BridgeTraverseStart, 1)
END_MESSAGE_DEFINITION(BridgeTraverseStart)

// BridgeTraverseComplete
START_TIMESTAMPED_MESSAGE_DEFINITION(BridgeTraverseComplete, 1)
ADD_MESSAGE_MEMBER(bool, didSucceed) // true if robot thinks it finished traversing the bridge
END_MESSAGE_DEFINITION(BridgeTraverseComplete)

// CameraCalibration
// TODO: Assume zero skew and remove that member?
START_MESSAGE_DEFINITION(CameraCalibration, 1)
ADD_MESSAGE_MEMBER(f32, focalLength_x)
ADD_MESSAGE_MEMBER(f32, focalLength_y)
ADD_MESSAGE_MEMBER(f32, center_x)
ADD_MESSAGE_MEMBER(f32, center_y)
ADD_MESSAGE_MEMBER(f32, skew)
ADD_MESSAGE_MEMBER(u16, nrows)
ADD_MESSAGE_MEMBER(u16, ncols)
END_MESSAGE_DEFINITION(CameraCalibration)


// Robot Available
START_MESSAGE_DEFINITION(RobotAvailable, 1)
ADD_MESSAGE_MEMBER(u32, robotID)
// TODO: Add other members here?
END_MESSAGE_DEFINITION(RobotAvailable)


// PrintText
#define PRINT_TEXT_MSG_LENGTH 50
START_MESSAGE_DEFINITION(PrintText, 1)
ADD_MESSAGE_MEMBER_ARRAY(u8, text, PRINT_TEXT_MSG_LENGTH)
END_MESSAGE_DEFINITION(PrintText)

// ImageChunk
#define IMAGE_CHUNK_SIZE 80
START_MESSAGE_DEFINITION(ImageChunk, 1)
ADD_MESSAGE_MEMBER(u8, imageId)
ADD_MESSAGE_MEMBER(u8, chunkId)
ADD_MESSAGE_MEMBER(u8, chunkSize)
ADD_MESSAGE_MEMBER(u8, resolution)
ADD_MESSAGE_MEMBER_ARRAY(u8, data, IMAGE_CHUNK_SIZE)
END_MESSAGE_DEFINITION(ImageChunk)

// TrackerQuad
START_MESSAGE_DEFINITION(TrackerQuad, 1)
ADD_MESSAGE_MEMBER(u16, topLeft_x)
ADD_MESSAGE_MEMBER(u16, topLeft_y)
ADD_MESSAGE_MEMBER(u16, topRight_x)
ADD_MESSAGE_MEMBER(u16, topRight_y)
ADD_MESSAGE_MEMBER(u16, bottomRight_x)
ADD_MESSAGE_MEMBER(u16, bottomRight_y)
ADD_MESSAGE_MEMBER(u16, bottomLeft_x)
ADD_MESSAGE_MEMBER(u16, bottomLeft_y)
END_MESSAGE_DEFINITION(TrackerQuad)


// MainCycleTimeError
START_MESSAGE_DEFINITION(MainCycleTimeError, 1)
ADD_MESSAGE_MEMBER(u32, numMainTooLongErrors)
ADD_MESSAGE_MEMBER(u32, avgMainTooLateTime)
ADD_MESSAGE_MEMBER(u32, numMainTooLateErrors)
ADD_MESSAGE_MEMBER(u32, avgMainTooLongTime)
END_MESSAGE_DEFINITION(MainCycleTimeError)


// IMUDataChunk
#define IMU_CHUNK_SIZE 32
START_MESSAGE_DEFINITION(IMUDataChunk, 1)
ADD_MESSAGE_MEMBER(u8, seqId)
ADD_MESSAGE_MEMBER(u8, chunkId)
ADD_MESSAGE_MEMBER(u8, totalNumChunks)
ADD_MESSAGE_MEMBER_ARRAY(s8, aX, IMU_CHUNK_SIZE)
ADD_MESSAGE_MEMBER_ARRAY(s8, aY, IMU_CHUNK_SIZE)
ADD_MESSAGE_MEMBER_ARRAY(s8, aZ, IMU_CHUNK_SIZE)
ADD_MESSAGE_MEMBER_ARRAY(s8, gX, IMU_CHUNK_SIZE)
ADD_MESSAGE_MEMBER_ARRAY(s8, gY, IMU_CHUNK_SIZE)
ADD_MESSAGE_MEMBER_ARRAY(s8, gZ, IMU_CHUNK_SIZE)
END_MESSAGE_DEFINITION(IMUDataChunk)

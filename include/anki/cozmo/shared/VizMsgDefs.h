// All message definitions should look like this:
//
//   START_MESSAGE_DEFINITION(MessageTypeName, Priority, DispatchFunctionPtr)
//   ADD_MESSAGE_MEMBER(MemberType, MemberName)
//   ADD_MESSAGE_MEMBER(MemberType, MemberName)
//   ...
//   END_MESSAGE_DEFINITION(MessageTypeName)
//
// For example:
//
//   START_MESSAGE_DEFINITION(CozmoMsg_Foo, 1, ProcessFoo)
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

// For now at least, Viz uses Robot-style message structures
#include "anki/cozmo/shared/MessageDefMacros_Robot.h"


// VizObject
START_MESSAGE_DEFINITION(VizObject, 1)
ADD_MESSAGE_MEMBER(u32, objectID)
ADD_MESSAGE_MEMBER(u32, objectTypeID)
ADD_MESSAGE_MEMBER(u32, color)
ADD_MESSAGE_MEMBER(f32, x_size_m)
ADD_MESSAGE_MEMBER(f32, y_size_m)
ADD_MESSAGE_MEMBER(f32, z_size_m)
ADD_MESSAGE_MEMBER(f32, x_trans_m)
ADD_MESSAGE_MEMBER(f32, y_trans_m)
ADD_MESSAGE_MEMBER(f32, z_trans_m)
ADD_MESSAGE_MEMBER(f32, rot_deg)
ADD_MESSAGE_MEMBER(f32, rot_axis_x)
ADD_MESSAGE_MEMBER(f32, rot_axis_y)
ADD_MESSAGE_MEMBER(f32, rot_axis_z)
END_MESSAGE_DEFINITION(VizObject)

// EraseObject
START_MESSAGE_DEFINITION(VizEraseObject, 1)
ADD_MESSAGE_MEMBER(u32, objectID)
END_MESSAGE_DEFINITION(VizEraseObject)

// VizSetRobot
START_MESSAGE_DEFINITION(VizSetRobot, 1)
ADD_MESSAGE_MEMBER(u32, robotID)
ADD_MESSAGE_MEMBER(f32, x_trans_m)
ADD_MESSAGE_MEMBER(f32, y_trans_m)
ADD_MESSAGE_MEMBER(f32, z_trans_m)
ADD_MESSAGE_MEMBER(f32, rot_rad)
ADD_MESSAGE_MEMBER(f32, rot_axis_x)
ADD_MESSAGE_MEMBER(f32, rot_axis_y)
ADD_MESSAGE_MEMBER(f32, rot_axis_z)
ADD_MESSAGE_MEMBER(f32, head_angle)
ADD_MESSAGE_MEMBER(f32, lift_angle)
END_MESSAGE_DEFINITION(VizSetRobot)


// VizAppendPathSegmentLine
START_MESSAGE_DEFINITION(VizAppendPathSegmentLine, 1)
ADD_MESSAGE_MEMBER(u32, pathID)
ADD_MESSAGE_MEMBER(f32, x_start_m)
ADD_MESSAGE_MEMBER(f32, y_start_m)
ADD_MESSAGE_MEMBER(f32, z_start_m)
ADD_MESSAGE_MEMBER(f32, x_end_m)
ADD_MESSAGE_MEMBER(f32, y_end_m)
ADD_MESSAGE_MEMBER(f32, z_end_m)
END_MESSAGE_DEFINITION(VizAppendPathSegmentLine)

// VizAppendPathSegmentArc
START_MESSAGE_DEFINITION(VizAppendPathSegmentArc, 1)
ADD_MESSAGE_MEMBER(u32, pathID)
ADD_MESSAGE_MEMBER(f32, x_center_m)
ADD_MESSAGE_MEMBER(f32, y_center_m)
ADD_MESSAGE_MEMBER(f32, radius_m)
ADD_MESSAGE_MEMBER(f32, start_rad)
ADD_MESSAGE_MEMBER(f32, sweep_rad)
END_MESSAGE_DEFINITION(VizAppendPathSegmentArc)

// VizSetPathColor
START_MESSAGE_DEFINITION(VizSetPathColor, 1)
ADD_MESSAGE_MEMBER(u32, pathID)
ADD_MESSAGE_MEMBER(u32, colorID)
END_MESSAGE_DEFINITION(VizSetPathColor)


// ErasePath
START_MESSAGE_DEFINITION(VizErasePath, 1)
ADD_MESSAGE_MEMBER(u32, pathID)
END_MESSAGE_DEFINITION(VizErasePath)


// VizDefineColor
START_MESSAGE_DEFINITION(VizDefineColor, 1)
ADD_MESSAGE_MEMBER(u32, colorID)
ADD_MESSAGE_MEMBER(f32, r)
ADD_MESSAGE_MEMBER(f32, g)
ADD_MESSAGE_MEMBER(f32, b)
ADD_MESSAGE_MEMBER(f32, alpha)
END_MESSAGE_DEFINITION(VizDefineColor)


// VizSetLabel
#define MAX_VIZ_LABEL_LENGTH 64
START_MESSAGE_DEFINITION(VizSetLabel, 1)
ADD_MESSAGE_MEMBER(u32, labelID)
ADD_MESSAGE_MEMBER(u32, colorID)
ADD_MESSAGE_MEMBER_ARRAY(u8, text, MAX_VIZ_LABEL_LENGTH)
END_MESSAGE_DEFINITION(VizSetLabel)


// VizDockingErrorSignal
START_MESSAGE_DEFINITION(VizDockingErrorSignal, 1)
ADD_MESSAGE_MEMBER(f32, x_dist)
ADD_MESSAGE_MEMBER(f32, y_dist)
ADD_MESSAGE_MEMBER(f32, angle)
END_MESSAGE_DEFINITION(VizDockingErrorSignal)


// VizImageChunk
#define MAX_VIZ_IMAGE_CHUNK_SIZE (4800)
START_MESSAGE_DEFINITION(VizImageChunk, 1)
ADD_MESSAGE_MEMBER(u32, chunkSize)
ADD_MESSAGE_MEMBER(u8, chunkId)
ADD_MESSAGE_MEMBER(u8, imgId)
ADD_MESSAGE_MEMBER(u8, resolution)
ADD_MESSAGE_MEMBER_ARRAY(u8, data, MAX_VIZ_IMAGE_CHUNK_SIZE)
END_MESSAGE_DEFINITION(VizImageChunk)

// VizShowObjects
START_MESSAGE_DEFINITION(VizShowObjects, 1)
ADD_MESSAGE_MEMBER(u8, show)
END_MESSAGE_DEFINITION(VizShowObjects)

// The maximum size of a VizMsg
#define MAX_VIZ_MSG_SIZE (MAX_VIZ_IMAGE_CHUNK_SIZE+8)
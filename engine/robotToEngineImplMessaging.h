/*
 * File: RobotToEngineImplMessaging.h
 * Author: Meith Jhaveri
 * Created: 7/11/16
 * Description: System for handling Robot to Engine messages.
 * Copyright: Anki, inc. 2016
 */

#ifndef __Anki_Cozmo_Basestation_RobotToEngineImplMessaging_H__
#define __Anki_Cozmo_Basestation_RobotToEngineImplMessaging_H__

#include "engine/robotInterface/messageHandler.h"
#include "clad/robotInterface/messageRobotToEngine_hash.h"
#include "util/helpers/noncopyable.h"
#include "util/signals/signalHolder.h"


#include <fstream>
#include <memory.h>

namespace Anki {
namespace Cozmo {

class Robot;
struct ObjectMoved;
struct ObjectPowerLevel;
struct ObjectStoppedMoving;
struct ObjectUpAxisChanged;
  
class RobotToEngineImplMessaging : private Util::noncopyable, public Util::SignalHolder
{
public:
  
  RobotToEngineImplMessaging(Robot* robot);
  ~RobotToEngineImplMessaging();
  
  // Version checks
  const RobotInterface::FWVersionInfo& GetFWVersionInfo() const { return _factoryFirmwareVersion; }
  bool HasMismatchedCLAD() const { return _hasMismatchedEngineToRobotCLAD || _hasMismatchedRobotToEngineCLAD; }
  
  
  void InitRobotMessageComponent(RobotInterface::MessageHandler* messageHandler, RobotID_t robotId, Robot* const robot);
  void HandleRobotSetHeadID(const AnkiEvent<RobotInterface::RobotToEngine>& message, Robot* const robot);
  void HandleRobotSetBodyID(const AnkiEvent<RobotInterface::RobotToEngine>& message, Robot* const robot);
  void HandleFirmwareVersion(const AnkiEvent<RobotInterface::RobotToEngine>& message, Robot* const robot);
  void HandlePrint(const AnkiEvent<RobotInterface::RobotToEngine>& message, Robot* const robot);
  void HandleFWVersionInfo(const AnkiEvent<RobotInterface::RobotToEngine>& message, Robot* const robot);
  void HandlePickAndPlaceResult(const AnkiEvent<RobotInterface::RobotToEngine>& message, Robot* const robot);
  void HandleDockingStatus(const AnkiEvent<RobotInterface::RobotToEngine>& message);
  void HandleActiveObjectMoved(const ObjectMoved& message, Robot* const robot);
  void HandleActiveObjectStopped(const ObjectStoppedMoving& message, Robot* const robot);
  void HandleActiveObjectUpAxisChanged(const ObjectUpAxisChanged& message, Robot* const robot);
  void HandleFallingEvent(const AnkiEvent<RobotInterface::RobotToEngine>& message, Robot* const robot);
  void HandleGoalPose(const AnkiEvent<RobotInterface::RobotToEngine>& message, Robot* const robot);
  void HandleRobotStopped(const AnkiEvent<RobotInterface::RobotToEngine>& message, Robot* const robot);
  void HandleCliffEvent(const AnkiEvent<RobotInterface::RobotToEngine>& message, Robot* const robot);
  void HandlePotentialCliffEvent(const AnkiEvent<RobotInterface::RobotToEngine>& message, Robot* const robot);
  
  // For processing imu data chunks arriving from robot.
  // Writes the entire log of 3-axis accelerometer and 3-axis
  // gyro readings to a .m file in kP_IMU_LOGS_DIR so they
  // can be read in from Matlab. (See robot/util/imuLogsTool.m)
  void HandleImuData(const AnkiEvent<RobotInterface::RobotToEngine>& message, Robot* const robot);
  void HandleImuRawData(const AnkiEvent<RobotInterface::RobotToEngine>& message, Robot* const robot);
  void HandleImageImuData(const AnkiEvent<RobotInterface::RobotToEngine>& message, Robot* const robot);
  void HandleSyncTimeAck(const AnkiEvent<RobotInterface::RobotToEngine>& message, Robot* const robot);
  void HandleRobotPoked(const AnkiEvent<RobotInterface::RobotToEngine>& message, Robot* const robot);
  void HandleMotorCalibration(const AnkiEvent<RobotInterface::RobotToEngine>& message, Robot* const robot);
  void HandleMotorAutoEnabled(const AnkiEvent<RobotInterface::RobotToEngine>& message, Robot* const robot);
  void HandleObjectPowerLevel(const ObjectPowerLevel& message, Robot* const robot);
  void HandleTimeProfileStat(const AnkiEvent<RobotInterface::RobotToEngine>& message);
  void HandleAudioInput(const AnkiEvent<RobotInterface::RobotToEngine>& message, Robot* const robot);
  void HandleMicDirection(const AnkiEvent<RobotInterface::RobotToEngine>& message, Robot* const robot);
  
  double GetLastImageReceivedTime() const { return _lastImageRecvTime; }
  
private:
  // Copy of last received firmware version info from robot
  RobotInterface::FWVersionInfo _factoryFirmwareVersion;
  bool _hasMismatchedEngineToRobotCLAD;
  bool _hasMismatchedRobotToEngineCLAD;

  
  ///////// Messaging ////////
  // These methods actually do the creation of messages and sending
  // (via MessageHandler) to the physical robot

  uint32_t _imuSeqID = 0;
  std::ofstream _imuLogFileStream;
  
  // For handling multiple images coming in on the same tick
  u32          _repeatedImageCount = 0;
  double       _lastImageRecvTime  = -1.0;
  
  // For tracking time since last power level report (per accessory)
  std::map<uint32_t, uint32_t> _lastPowerLevelSentTime;
  std::map<uint32_t, uint32_t> _lastMissedPacketCount;

  // Internal helpers
  bool ShouldIgnoreMultipleImages() const;

};

} // namespace Cozmo
} // namespace Anki
#endif /* RobotToEngineImplMessaging_h */

/**
 * File: OSState_vicos.cpp
 *
 * Authors: Kevin Yoon
 * Created: 2017-12-11
 *
 * Description:
 *
 *   Keeps track of OS-level state, mostly for development/debugging purposes
 *
 * Copyright: Anki, Inc. 2017
 *
 **/

#include "osState/osState.h"
#include "anki/cozmo/shared/cozmoConfig.h"
#include "util/console/consoleInterface.h"
#include "util/fileUtils/fileUtils.h"
#include "util/logging/logging.h"
#include "util/time/universalTime.h"

#include "cutils/properties.h"

// For getting our ip address
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <string.h>
#include <netdb.h>

#include <linux/wireless.h>

#include <fstream>
#include <array>
#include <stdlib.h>

#ifdef SIMULATOR
#error SIMULATOR should NOT be defined by any target using osState_vicos.cpp
#endif

namespace Anki {
namespace Cozmo {

namespace {

  std::ifstream _cpuFile;
  std::ifstream _tempFile;

  const char* kNominalCPUFreqFile = "/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq";
  const char* kCPUFreqFile = "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_cur_freq";
  const char* kTemperatureFile = "/sys/devices/virtual/thermal/thermal_zone3/temp";
  const char* kMACAddressFile = "/sys/class/net/wlan0/address";
  const char* kRecoveryModeFile = "/data/unbrick";

  // System vars
  uint32_t _cpuFreq_kHz; // CPU freq
  uint32_t _cpuTemp_C;   // Temperature in Celsius

  // How often state variables are updated
  uint32_t _updatePeriod_ms = 0;
  uint32_t _lastUpdateTime_ms = 0;

} // namespace

std::string GetProperty(const std::string& key)
{
  char propBuf[PROPERTY_VALUE_MAX] = {0};
  int rc = property_get(key.c_str(), propBuf, "");
  if(rc <= 0)
  {
    PRINT_NAMED_WARNING("OSState.GetProperty.FailedToFindProperty",
                        "Property %s not found",
                        key.c_str());
  }

  return std::string(propBuf);
}

OSState::OSState()
{
  // Get nominal CPU frequency for this robot
  _tempFile.open(kNominalCPUFreqFile, std::ifstream::in);
  if(_tempFile.is_open()) {
    _tempFile >> kNominalCPUFreq_kHz;
    PRINT_NAMED_INFO("OSState.Constructor.NominalCPUFreq", "%dkHz", kNominalCPUFreq_kHz);
    _tempFile.close();
  }
  else {
    PRINT_NAMED_WARNING("OSState.Constructor.FailedToOpenNominalCPUFreqFile", "%s", kNominalCPUFreqFile);
  }

  _cpuFreq_kHz = kNominalCPUFreq_kHz;
  _cpuTemp_C = 0;

  _tempFile.open(kTemperatureFile, std::ifstream::in);
  _cpuFile.open(kCPUFreqFile, std::ifstream::in);
}

OSState::~OSState()
{
  if (_tempFile.is_open()) {
    _tempFile.close();
  }
  if (_cpuFile.is_open()) {
    _cpuFile.close();
  }
}

RobotID_t OSState::GetRobotID() const
{
  return DEFAULT_ROBOT_ID;
}

void OSState::Update()
{
  if (_updatePeriod_ms != 0) {
    const double now_ms = Util::Time::UniversalTime::GetCurrentTimeInMilliseconds();
    if (now_ms - _lastUpdateTime_ms > _updatePeriod_ms) {

      // Update cpu freq
      _cpuFreq_kHz = UpdateCPUFreq_kHz();

      // Update temperature reading
      _cpuTemp_C = UpdateTemperature_C();

      _lastUpdateTime_ms = now_ms;
    }
  }
}

void OSState::SetUpdatePeriod(uint32_t milliseconds)
{
  _updatePeriod_ms = milliseconds;
}

uint32_t OSState::UpdateCPUFreq_kHz() const
{
  // Update cpu freq
  uint32_t cpuFreq_kHz;
  _cpuFile.seekg(0, _cpuFile.beg);
  _cpuFile >> cpuFreq_kHz;
  return cpuFreq_kHz;
}

uint32_t OSState::UpdateTemperature_C() const
{
  // Update temperature reading
  uint32_t cpuTemp_C;
  _tempFile.seekg(0, _tempFile.beg);
  _tempFile >> cpuTemp_C;
  return cpuTemp_C;
}

uint32_t OSState::GetCPUFreq_kHz() const
{
  DEV_ASSERT(_updatePeriod_ms != 0, "OSState.GetCPUFreq_kHz.ZeroUpdate");
  return _cpuFreq_kHz;
}


bool OSState::IsCPUThrottling() const
{
  DEV_ASSERT(_updatePeriod_ms != 0, "OSState.IsCPUThrottling.ZeroUpdate");
  return (_cpuFreq_kHz < kNominalCPUFreq_kHz);
}

uint32_t OSState::GetTemperature_C() const
{
  DEV_ASSERT(_updatePeriod_ms != 0, "OSState.GetTemperature_C.ZeroUpdate");
  return _cpuTemp_C;
}

const std::string& OSState::GetSerialNumberAsString()
{
  if(_serialNumString.empty())
  {
    std::ifstream infile("/proc/cmdline");

    std::string line;
    while(std::getline(infile, line))
    {
      static const std::string kProp = "androidboot.serialno=";
      size_t index = line.find(kProp);
      if(index != std::string::npos)
      {
        _serialNumString = line.substr(index + kProp.length(), 8);
      }
    }

    infile.close();
  }

  return _serialNumString;
}

const std::string& OSState::GetOSBuildVersion()
{
  if(_osBuildVersion.empty())
  {
    _osBuildVersion = GetProperty("ro.build.display.id");
  }

  return _osBuildVersion;
}

const std::string& OSState::GetRobotName() const
{
  static std::string name = GetProperty("anki.robot.name");
  if(name.empty())
  {
    name = GetProperty("anki.robot.name");
  }
  return  name;
}

static std::string GetIPV4AddressForInterface(const char* if_name) {
  struct ifaddrs* ifaddr = nullptr;
  struct ifaddrs* ifa = nullptr;
  char host[NI_MAXHOST] = {0};

  int rc = getifaddrs(&ifaddr);
  if (rc == -1) {
    PRINT_NAMED_ERROR("OSState.GetIPAddress.GetIfAddrsFailed", "%s", strerror(errno));
    return "";
  }

  for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == nullptr) {
      continue;
    }

    if (ifa->ifa_addr->sa_family != AF_INET) {
      continue;
    }

    if (!strcmp(ifa->ifa_name, if_name)) {
      break;
    }
  }

  if (ifa != nullptr) {
    int s = getnameinfo(ifa->ifa_addr,
                        sizeof(struct sockaddr_in),
                        host, sizeof(host),
                        NULL, 0, NI_NUMERICHOST);
    if (s != 0) {
      PRINT_NAMED_ERROR("OSState.GetIPAddress.GetNameInfoFailed", "%s", gai_strerror(s));
      memset(host, 0, sizeof(host));
    }
  }

  if (host[0]) {
    PRINT_NAMED_INFO("OSState.GetIPAddress.IPV4AddressFound", "iface = %s , ip = %s",
                     if_name, host);
  } else {
    PRINT_NAMED_INFO("OSState.GetIPAddress.IPV4AddressNotFound", "iface = %s", if_name);
  }
  freeifaddrs(ifaddr);
  return std::string(host);
}

static std::string GetWiFiSSIDForInterface(const char* if_name) {
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd == -1) {
    ASSERT_NAMED_EVENT(false, "OSState.GetSSID.OpenSocketFail", "");
    return "";
  }

  iwreq req;
  memset(&req, 0, sizeof(req));
  (void) strncpy(req.ifr_name, if_name, sizeof(req.ifr_name) - 1);
  char essid[IW_ESSID_MAX_SIZE + 2] = {0};
  req.u.essid.pointer = essid;
  req.u.essid.length = sizeof(essid) - 2;

  if (ioctl(fd, SIOCGIWESSID, &req) == -1) {
    PRINT_NAMED_INFO("OSState.UpdateWifiInfo.FailedToGetSSID", "iface = %s , errno = %s",
                     if_name, strerror(errno));
    memset(essid, 0, sizeof(essid));
  }
  (void) close(fd);
  PRINT_NAMED_INFO("OSState.GetSSID", "%s", essid);
  return std::string(essid);
}

void OSState::UpdateWifiInfo()
{
  const char* const if_name = "wlan0";
  _ipAddress = GetIPV4AddressForInterface(if_name);
  _ssid = GetWiFiSSIDForInterface(if_name);
}

const std::string& OSState::GetIPAddress(bool update)
{
  if(_ipAddress.empty() || update)
  {
    UpdateWifiInfo();
  }

  return _ipAddress;
}

const std::string& OSState::GetSSID(bool update)
{
  if(_ssid.empty() || update)
  {
    UpdateWifiInfo();
  }

  return _ssid;
}

std::string OSState::GetMACAddress() const
{
  std::ifstream macFile;
  macFile.open(kMACAddressFile);
  if (macFile.is_open()) {
    std::string macStr;
    macFile >> macStr;
    macFile.close();
    return macStr;
  }
  return "";
}

bool OSState::IsInRecoveryMode()
{
  return Util::FileUtils::FileExists(kRecoveryModeFile);
}

} // namespace Cozmo
} // namespace Anki

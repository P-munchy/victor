/// Implementation for printTrace functionality

#include "anki/cozmo/basestation/tracePrinter.h"
#include "anki/common/basestation/jsonTools.h"
#include "util/logging/logging.h"
#include <stdlib.h>
#include <stdarg.h>
#include <fstream>

namespace Anki {
namespace Cozmo {

const std::string TracePrinter::UnknownTraceName   = "Unknown trace name";
const std::string TracePrinter::UnknownTraceFormat = "Unknown trace format [%d] with %d parameters";
const std::string TracePrinter::RobotNamePrefix    = "RobotFirmware.";

TracePrinter::TracePrinter(Util::Data::DataPlatform* dp):
  printThreshold(RobotInterface::LogLevel::ANKI_LOG_LEVEL_DEBUG) {
  if (dp) {
    Json::Value jsonDict;
    const std::string jsonFilename = "config/basestation/AnkiLogStringTables.json";
    bool success = dp->readAsJson(Util::Data::Scope::Resources, jsonFilename, jsonDict);
    if (!success)
    {
      PRINT_NAMED_ERROR("Robot.AnkiLogStringTablesNotFound",
                        "Robot PrintTrace string table Json config file %s not found.",
                        jsonFilename.c_str());
    }
    
    const Json::Value jsonNameTable   = jsonDict["nameTable"];
    for (Json::ValueIterator itr = jsonNameTable.begin(); itr != jsonNameTable.end(); itr++) {
      nameTable.insert(std::pair<const int, const std::string>(atoi(itr.key().asString().c_str()), itr->asString()));
    }
    
    const Json::Value jsonFormatTable = jsonDict["formatTable"];
    for (Json::ValueIterator itr = jsonFormatTable.begin(); itr != jsonFormatTable.end(); itr++) {
      const int key = atoi(itr.key().asString().c_str());
      const std::string fmt = (*itr)[0].asString();
      const int nargs = (*itr)[1].asInt();
      formatTable.insert(std::pair<const int, const FormatInfo>(key, FormatInfo(fmt, nargs)));
    }
  }
}

void TracePrinter::HandleTrace(const AnkiEvent<RobotInterface::RobotToEngine>& message) const {
  const RobotInterface::PrintTrace& trace = message.GetData().Get_trace();
  if (trace.level >= printThreshold) {
    const std::string name = RobotNamePrefix + GetName(trace.name);
    const std::string mesg = GetFormatted(trace);
    switch (trace.level)
    {
      case RobotInterface::LogLevel::ANKI_LOG_LEVEL_DEBUG:
      case RobotInterface::LogLevel::ANKI_LOG_LEVEL_PRINT:
      {
        PRINT_NAMED_DEBUG(name.c_str(), "%s", mesg.c_str()); //< Nessisary because format must be a string literal
        break;
      }
      case RobotInterface::LogLevel::ANKI_LOG_LEVEL_INFO:
      {
        PRINT_NAMED_INFO(name.c_str(), "%s", mesg.c_str());
        break;
      }
      case RobotInterface::LogLevel::ANKI_LOG_LEVEL_EVENT:
      {
        PRINT_NAMED_EVENT(name.c_str(), "%s", mesg.c_str());
        break;
      }
      case RobotInterface::LogLevel::ANKI_LOG_LEVEL_WARN:
      {
        PRINT_NAMED_WARNING(name.c_str(), "%s", mesg.c_str());
        break;
      }
      case RobotInterface::LogLevel::ANKI_LOG_LEVEL_ASSERT:
      case RobotInterface::LogLevel::ANKI_LOG_LEVEL_ERROR:
      {
        PRINT_NAMED_ERROR(name.c_str(), "%s", mesg.c_str());
        break;
      }
    }
  }
}

void TracePrinter::HandleCrashReport(const AnkiEvent<RobotInterface::RobotToEngine>& message) const {
  const RobotInterface::CrashReport& report = message.GetData().Get_crashReport();
  PRINT_NAMED_ERROR("RobotFirmware.CrashDump", "Firmware crash report received: %d\n", (int)report.which);
  char dumpFileName[512];
  snprintf(dumpFileName, sizeof(dumpFileName), "robot_fw_crash_%d_%lld.bin", (int)report.which, std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
  std::ofstream fileOut;
  fileOut.open(dumpFileName, std::ios::out | std::ofstream::binary);
  if( fileOut.is_open() ) {
    fileOut.write(reinterpret_cast<const char*>(report.dump.begin()), report.dump.size()*sizeof(uint32_t));
    fileOut.close();
    printf("\treport written to \"%s\"\n", dumpFileName);
  }
  else
  {
    printf("\tCouldn't write report to file \"%s\"\n", dumpFileName);
  }
}

const std::string& TracePrinter::GetName(const int nameId) const {
  const IntStringMap::const_iterator it = nameTable.find(nameId);
  if (it == nameTable.end()) return UnknownTraceName;
  else return it->second;
}

std::string TracePrinter::GetFormatted(const RobotInterface::PrintTrace& trace) const {
  char pbuf[512];
  char fbuf[64];
  const IntFormatMap::const_iterator it = formatTable.find(trace.stringId);
  if (it == formatTable.end()) {
    snprintf(pbuf, sizeof(pbuf), UnknownTraceFormat.c_str(), trace.stringId, trace.value.size());
    return pbuf;
  }
  else {
    const FormatInfo& fi(it->second);
    const int nargs = fi.second;
    if (nargs != trace.value.size()) {
      snprintf(pbuf, sizeof(pbuf), "Trace nargs missmatch. Expected %d values but got %d for format string (%d) \"%s\"",
               nargs, (int)trace.value.size(), trace.stringId, fi.first.c_str());
      return pbuf;
    }
    else if (nargs == 0)
    {
      return fi.first;
    }
    else {
      int index = 0;
      int argInd = 0;
      const char* fmtPtr = fi.first.c_str();
      int subFmtInd = -1;
      while ((*fmtPtr != 0) && (index < (sizeof(pbuf)-1)) && (subFmtInd < (sizeof(fbuf)-1)))
      {
        if (subFmtInd >= 0)
        {
          switch(fmtPtr[subFmtInd])
          {
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
            case '.':
            {
              fbuf[subFmtInd] = fmtPtr[subFmtInd];
              subFmtInd++;
              continue;
            }
            case '%':
            {
              pbuf[index++] = '%';
              fmtPtr += subFmtInd + 1;
              subFmtInd = -1;
              continue;
            }
            case 'd':
            case 'i':
            case 'x':
            case 'f':
            {
              fbuf[subFmtInd] = fmtPtr[subFmtInd];
              fbuf[subFmtInd+1] = NULL;
              if (fmtPtr[subFmtInd] == 'f')
              {
                const float fltArg = *(reinterpret_cast<const float*>(&trace.value[argInd]));
                index += snprintf(pbuf + index, sizeof(pbuf)-index, fbuf, fltArg);
              }
              else
              {
                index += snprintf(pbuf + index, sizeof(pbuf)-index, fbuf, trace.value[argInd]);
              }
              fmtPtr += subFmtInd + 1;
              subFmtInd = -1;
              argInd++;
              continue;
            }
            default: // So copy it over and return to normal operation
            {
              pbuf[index++] = *(fmtPtr++);
              subFmtInd = -1;
              continue;
            }
          }
        }
        else if (*fmtPtr == '%')
        {
          fbuf[0] = '%';
          subFmtInd = 1;
          continue;
        }
        else
        {
          pbuf[index++] = *(fmtPtr++);
        }
      }
      pbuf[index] = NULL;
      return pbuf;
    }
  }
}


} // Namespace Cozmo
} // Namespace Anki

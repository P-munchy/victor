/**
* File: rollingFileLogger
*
* Author: Lee Crippen
* Created: 3/29/2016
*
* Description: 
*
* Copyright: Anki, inc. 2016
*
*/
#ifndef __Util_Logging_RollingFileLogger_H_
#define __Util_Logging_RollingFileLogger_H_

#include "util/helpers/noncopyable.h"

#include <fstream>
#include <cstdlib>
#include <string>

namespace Anki {
namespace Util {
  
// Forward declarations
namespace Dispatch {
  class Queue;
}

class RollingFileLogger : noncopyable {
public:
  static constexpr std::size_t  kDefaultMaxFileSize = 1024 * 1024 * 20;
  static const char * const     kDefaultFileExtension;
  
  RollingFileLogger(const std::string& baseDirectory, const std::string& extension = kDefaultFileExtension, std::size_t maxFileSize = kDefaultMaxFileSize);
  virtual ~RollingFileLogger();
  
  void Write(const std::string& message);
  
  static std::string GetDateTimeString(const std::chrono::system_clock::time_point& time);
  
private:
  
  Dispatch::Queue*  _dispatchQueue;
  std::string       _baseDirectory;
  std::string       _extension;
  std::string       _currentFileName;
  std::size_t       _maxFileSize;
  std::size_t       _numBytesWritten = 0;
  std::ofstream     _currentLogFileHandle;
  
  void WriteInternal(const std::string& message);
  void RollLogFile();
  std::string GetNextFileName();
};

} // end namespace Util
} // end namespace Anki


#endif //__Util_Logging_RollingFileLogger_H_

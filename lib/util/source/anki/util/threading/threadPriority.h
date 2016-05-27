/**
 * File: threadPriority
 *
 * Author: Mark Wesley
 * Created: 05/24/16
 *
 * Description: Support for setting a desired thread priority
 *
 * Copyright: Anki, Inc. 2016
 *
 **/


#ifndef __Util_Threading_ThreadPriority_H__
#define __Util_Threading_ThreadPriority_H__


#include <stdint.h>
#include <thread>


namespace Anki {
namespace Util {


enum class ThreadPriority : uint8_t
{
  Min = 0,
  Low,
  Default,
  High,
  Max,
};
  
  
void SetThreadPriority(std::thread& inThread, ThreadPriority threadPriority);


} // namespace Util
} // namespace Anki


#endif // __Util_Threading_ThreadPriority_H__

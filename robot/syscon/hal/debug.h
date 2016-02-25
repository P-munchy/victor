#ifndef UART_H
#define UART_H

#include <stdint.h>

namespace UART {
  extern bool initialized;
  
  int DebugQueue();
  void DebugChar();

  bool waitIdle();
  int get();
  void print( const char* fmt, ...);
  void dump(int count, char* data);
}
  
#endif

//
//  csharp-binding.h
//  CozmoGame
//
//  Created by Greg Nagel on 4/11/15.
//
//

#ifndef __CSharpBinding__csharp_binding__
#define __CSharpBinding__csharp_binding__

#include <string>

#ifndef _cplusplus
extern "C" {
#endif

  // Hook for initialization code. If this errors out, do not run anything else.
  int cozmo_startup(const char *configuration_data);
    
  // Hook for deinitialization. Should be fine to call startup after this call, even on failure.
  // Return value is just for informational purposes. Should never fail, even if not initialized.
  int cozmo_shutdown();
  
  // Hook for triggering setup of the desired wifi details
  int cozmo_wifi_setup(const char* wifiSSID, const char* wifiPasskey);
  
  void cozmo_send_to_clipboard(const char* log);
  
  void Unity_DAS_Event(const char* eventName, const char* eventValue, const char** keys, const char** values, unsigned keyValueCount);
  
  void Unity_DAS_LogE(const char* eventName, const char* eventValue, const char** keys, const char** values, unsigned keyValueCount);
  
  void Unity_DAS_LogW(const char* eventName, const char* eventValue, const char** keys, const char** values, unsigned keyValueCount);
  
  void Unity_DAS_LogI(const char* eventName, const char* eventValue, const char** keys, const char** values, unsigned keyValueCount);
  
  void Unity_DAS_LogD(const char* eventName, const char* eventValue, const char** keys, const char** values, unsigned keyValueCount);
  
  void Unity_DAS_SetGlobal(const char* key, const char* value);

#ifndef _cplusplus
}
#endif

#endif // __CSharpBinding__csharp_binding__

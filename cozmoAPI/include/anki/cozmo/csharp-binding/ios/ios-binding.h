//
//  ios-binding.h
//  CozmoGame
//
//  Created by Greg Nagel on 2/3/15.
//
//

#ifndef __ios_binding__
#define __ios_binding__

namespace Anki {
    
namespace Util {
namespace Data {
    class DataPlatform;
}
}

namespace Cozmo {
namespace iOSBinding {

// iOS specific initialization 
int cozmo_startup(Anki::Util::Data::DataPlatform* dataPlatform);

// iOS specific finalization
int cozmo_shutdown();
  
int cozmo_engine_wifi_setup(const char* wifiSSID, const char* wifiPasskey);
  
void cozmo_engine_send_to_clipboard(const char* log);

} // namespace CSharpBinding
} // namespace Cozmo
} // namespace Anki

#endif // __ios_binding__

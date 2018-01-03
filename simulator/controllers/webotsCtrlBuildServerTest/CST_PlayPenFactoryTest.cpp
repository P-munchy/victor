/**
 * File: CST_PlayPenFactoryTest.cpp
 *
 * Author: Kevin Yoon
 * Created: 5/10/16
 *
 * Description: See BehaviorFactoryTest.cpp
 *
 * Copyright: Anki, inc. 2016
 *
 */

#include "simulator/game/cozmoSimTestController.h"
#include "clad/types/behaviorComponent/behaviorTypes.h"

namespace Anki {
  namespace Cozmo {
    
    // ============ Test class declaration ============
    class CST_PlayPenFactoryTest : public CozmoSimTestController {
      
    private:
      
      virtual s32 UpdateSimInternal() override;
      
      bool _testStarted = false;
      bool _testResultReceived = false;
      const u32 TEST_TIMEOUT_SEC = 60;
      
      // Message handlers
      virtual void HandleFactoryTestResultEntry(const FactoryTestResultEntry& msg) override;
    };
    
    // Register class with factory
    REGISTER_COZMO_SIM_TEST_CLASS(CST_PlayPenFactoryTest);
    
    
    // =========== Test class implementation ===========
    
    s32 CST_PlayPenFactoryTest::UpdateSimInternal()
    {
      if (!_testStarted) {
        SendMessage(ExternalInterface::MessageGameToEngine(ExternalInterface::SetDebugConsoleVarMessage("BFT_PlaySound", "false")));
        SendMessage(ExternalInterface::MessageGameToEngine(ExternalInterface::ExecuteBehaviorByID(BehaviorIDToString(BehaviorID::FactoryTest), -1)));
        StartMovieConditional("PlayPenFactoryTest");
        //TakeScreenshotsAtInterval("PlayPenFactoryTest", 1.f);
        
        _testStarted = true;
      } else {

        IF_CONDITION_WITH_TIMEOUT_ASSERT(_testResultReceived, TEST_TIMEOUT_SEC)
        {
          StopMovie();
          CST_EXIT();
        }
      }
      return _result;
    }
    
    
    // ================ Message handler callbacks ==================
    void CST_PlayPenFactoryTest::HandleFactoryTestResultEntry(const FactoryTestResultEntry& msg)
    {
      if (msg.result != FactoryTestResultCode::SUCCESS) {
        _result = static_cast<u8>(msg.result);
      }
      _testResultReceived = true;
    }
    
    // ================ End of message handler callbacks ==================
    
  } // end namespace Cozmo
} // end namespace Anki


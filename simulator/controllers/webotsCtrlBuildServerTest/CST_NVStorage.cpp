/**
 * File: CST_NVStorage.cpp
 *
 * Author: Al Chaussee
 * Created: 4/25/2016
 *
 * Description: See TestStates below
 *
 * Copyright: Anki, inc. 2016
 *
 */

#include "anki/cozmo/simulator/game/cozmoSimTestController.h"
#include "anki/cozmo/basestation/robot.h"
#include "util/random/randomGenerator.h"


namespace Anki {
  namespace Cozmo {
    
    enum class TestState {
      WriteSingleBlob,
      ReadSingleBlob,
      VerifySingleBlob,
      
      WriteMultiBlob,
      ReadMultiBlob,
      VerifyMultiBlob,
      
      EraseSingleBlob,
      VerifySingleErase,
      
      EraseMultiBlob,
      VerifyMultiErase,
      
      WritingToInvalidMultiTag,
      
      WriteData,
      WipeAll,
      
      WriteWipeAll,
      ReadWipeAll,
      
      Final
    };
    
    typedef NVStorage::NVEntryTag Tag;
    
    // ============ Test class declaration ============
    class CST_NVStorage : public CozmoSimTestController {
      
    private:
    
      void RandomData(int length, uint8_t* data);
      
      bool IsDataSame(const uint8_t* d1, const uint8_t* d2, int length);
      
      void ClearAcks();
      
      virtual s32 UpdateSimInternal() override;
      
      TestState _testState = TestState::WriteSingleBlob;
      
      // Message handlers
      virtual void HandleNVStorageOpResult(const ExternalInterface::NVStorageOpResult &msg) override;
    
      Util::RandomGenerator r;
      
      const Tag singleBlobTag = (Tag)100;
      const Tag multiBlobTag = (Tag)65536;
      const static int numMultiBlobs = 5;
      
      uint8_t _dataWritten[numMultiBlobs][1024];
      
      bool _writeAckd = false;
      bool _readAckd = false;
      bool _eraseAckd = false;
      
      int _numWrites = 0;
      
      NVStorage::NVResult _lastResult = NVStorage::NVResult::NV_OKAY;
    };
    
    // Register class with factory
    REGISTER_COZMO_SIM_TEST_CLASS(CST_NVStorage);
    
    
    void CST_NVStorage::RandomData(int length, uint8_t* data)
    {
      for(int i=0;i<length;i++)
      {
        int n = r.RandInt(256);
        data[i] = (uint8_t)n;
      }
    }
    
    bool CST_NVStorage::IsDataSame(const uint8_t* d1, const uint8_t* d2, int length)
    {
      return (memcmp(d1, d2, length) == 0);
    }
    
    void CST_NVStorage::ClearAcks()
    {
      _writeAckd = false;
      _readAckd = false;
      _eraseAckd = false;
      _numWrites = 0;
    }
    
    
    // =========== Test class implementation ===========
    
    s32 CST_NVStorage::UpdateSimInternal()
    {
      switch (_testState) {
        case TestState::WriteSingleBlob:
        {
          ExternalInterface::NVStorageWriteEntry msg;
          RandomData(5, msg.data.data());
          memcpy(_dataWritten[0], msg.data.data(), 5);
          msg.tag = singleBlobTag;
          msg.data_length = 5;
          msg.index = 0;
          msg.numTotalBlobs = 1;
          
          ExternalInterface::MessageGameToEngine message;
          message.Set_NVStorageWriteEntry(msg);
          SendMessage(message);
          
          _testState = TestState::ReadSingleBlob;
          break;
        }
        case TestState::ReadSingleBlob:
        {
          IF_CONDITION_WITH_TIMEOUT_ASSERT(_writeAckd && _lastResult == NVStorage::NVResult::NV_OKAY, DEFAULT_TIMEOUT)
          {
            ClearAcks();
          
            ExternalInterface::NVStorageReadEntry msg;
            msg.tag = singleBlobTag;
            
            ExternalInterface::MessageGameToEngine message;
            message.Set_NVStorageReadEntry(msg);
            SendMessage(message);
          
            _testState = TestState::VerifySingleBlob;
          }
          break;
        }
        case TestState::VerifySingleBlob:
        {
          IF_CONDITION_WITH_TIMEOUT_ASSERT(_readAckd && _lastResult == NVStorage::NVResult::NV_OKAY, DEFAULT_TIMEOUT)
          {
            ClearAcks();
            
            const std::vector<u8>* data = GetReceivedNVStorageData(singleBlobTag);
            
            CST_ASSERT(IsDataSame(_dataWritten[0], data->data(), 5),
                       "Data written to and read from single blob is not the same");
            CST_ASSERT(data->size() == 8, "Data read from single blob is not expected word-aligned size");
            
            _testState = TestState::WriteMultiBlob;
          }
          break;
        }
        case TestState::WriteMultiBlob:
        {
          for(int i = 0; i < numMultiBlobs; i++)
          {
            ExternalInterface::NVStorageWriteEntry msg;
            RandomData(1024, msg.data.data());
            memcpy(_dataWritten[i], msg.data.data(), 1024);
            msg.tag = multiBlobTag; // First multiblob tag
            msg.data_length = 1024;
            msg.index = i;
            msg.numTotalBlobs = numMultiBlobs;
            
            ExternalInterface::MessageGameToEngine message;
            message.Set_NVStorageWriteEntry(msg);
            SendMessage(message);
          }
          
          _testState = TestState::ReadMultiBlob;
          break;
        }
        case TestState::ReadMultiBlob:
        {
          IF_CONDITION_WITH_TIMEOUT_ASSERT(_writeAckd && _lastResult == NVStorage::NVResult::NV_OKAY, 20)
          {
            ClearAcks();
            
            ExternalInterface::NVStorageReadEntry msg;
            msg.tag = multiBlobTag;
            
            ExternalInterface::MessageGameToEngine message;
            message.Set_NVStorageReadEntry(msg);
            SendMessage(message);
            
            _testState = TestState::VerifyMultiBlob;
          }
          break;
        }
        case TestState::VerifyMultiBlob:
        {
          IF_CONDITION_WITH_TIMEOUT_ASSERT(_readAckd && _lastResult == NVStorage::NVResult::NV_OKAY, DEFAULT_TIMEOUT)
          {
            ClearAcks();
            
            const std::vector<u8>* data = GetReceivedNVStorageData(multiBlobTag);
            
            for(int i = 0; i < numMultiBlobs; i++)
            {
              uint8_t d2[1024];
              memcpy(d2, data->data() + (i*1024), 1024);
              CST_ASSERT(IsDataSame(_dataWritten[i], d2, 1024),
                         "Data written to and read from multi blob is not the same");
            }
            CST_ASSERT(data->size() == numMultiBlobs*1024,
                       "Data read from multi blob is not expected word-aligned size");
            
            _testState = TestState::EraseSingleBlob;
          }
          break;
        }
        case TestState::EraseSingleBlob:
        {
          // Erase
          ExternalInterface::NVStorageEraseEntry msg;
          msg.tag = singleBlobTag;
          
          ExternalInterface::MessageGameToEngine message;
          message.Set_NVStorageEraseEntry(msg);
          SendMessage(message);
          
          // Try to read
          ExternalInterface::NVStorageReadEntry msg1;
          msg1.tag = singleBlobTag;
          
          ExternalInterface::MessageGameToEngine message1;
          message1.Set_NVStorageReadEntry(msg1);
          SendMessage(message1);
        
          _testState = TestState::VerifySingleErase;
          break;
        }
        case TestState::VerifySingleErase:
        {
          IF_CONDITION_WITH_TIMEOUT_ASSERT(_eraseAckd && _readAckd, 20)
          {
            ClearAcks();
            
            CST_ASSERT(_lastResult == NVStorage::NVResult::NV_NOT_FOUND, "Read data after erasing");
           
            _testState = TestState::EraseMultiBlob;
          }
          break;
        }
        case TestState::EraseMultiBlob:
        {
          // Erase
          ExternalInterface::NVStorageEraseEntry msg;
          msg.tag = multiBlobTag;
          
          ExternalInterface::MessageGameToEngine message;
          message.Set_NVStorageEraseEntry(msg);
          SendMessage(message);
          
          // Try to read
          ExternalInterface::NVStorageReadEntry msg1;
          msg1.tag = multiBlobTag;
          
          ExternalInterface::MessageGameToEngine message1;
          message1.Set_NVStorageReadEntry(msg1);
          SendMessage(message1);
          
          _testState = TestState::VerifyMultiErase;
        
          break;
        }
        case TestState::VerifyMultiErase:
        {
          IF_CONDITION_WITH_TIMEOUT_ASSERT(_eraseAckd && _readAckd, 20)
          {
            ClearAcks();
            
            CST_ASSERT(_lastResult == NVStorage::NVResult::NV_NOT_FOUND, "Read data after erasing");
            
            _testState = TestState::WritingToInvalidMultiTag;
          }
          break;
        }
        case TestState::WritingToInvalidMultiTag:
        {
          ExternalInterface::NVStorageWriteEntry msg;
          RandomData(1024, msg.data.data());
          msg.tag = (Tag)((uint32_t)multiBlobTag + 1); // Invalid multiblob tag
          msg.data_length = 1024;
          msg.index = 0;
          msg.numTotalBlobs = 1;
          
          ExternalInterface::MessageGameToEngine message;
          message.Set_NVStorageWriteEntry(msg);
          SendMessage(message);
          
          _testState = TestState::WriteData;
          break;
        }
        case TestState::WriteData:
        {
          IF_CONDITION_WITH_TIMEOUT_ASSERT(_writeAckd &&
                                           _lastResult == NVStorage::NVResult::NV_ERROR, DEFAULT_TIMEOUT)
          {
            ClearAcks();
            
            ExternalInterface::NVStorageWriteEntry msg;
            RandomData(5, msg.data.data());
            msg.tag = singleBlobTag;
            msg.data_length = 5;
            msg.index = 0;
            msg.numTotalBlobs = 1;
            
            ExternalInterface::MessageGameToEngine message;
            message.Set_NVStorageWriteEntry(msg);
            SendMessage(message);
            
            for(int i = 0; i < numMultiBlobs; i++)
            {
              ExternalInterface::NVStorageWriteEntry msg;
              RandomData(1024, msg.data.data());
              memcpy(_dataWritten[i], msg.data.data(), 1024);
              msg.tag = multiBlobTag; // First multiblob tag
              msg.data_length = 1024;
              msg.index = i;
              msg.numTotalBlobs = numMultiBlobs;
              
              ExternalInterface::MessageGameToEngine message;
              message.Set_NVStorageWriteEntry(msg);
              SendMessage(message);
            }
            
            _testState = TestState::WipeAll;
          }
          break;
        }
        case TestState::WipeAll:
        {
          IF_CONDITION_WITH_TIMEOUT_ASSERT(_numWrites == numMultiBlobs + 1, 20)
          {
            ClearAcks();
            
            // Erase all
            ExternalInterface::NVStorageEraseEntry msg1;
            msg1.tag = NVStorage::NVEntryTag::NVEntry_WipeAll;
            
            ExternalInterface::MessageGameToEngine message1;
            message1.Set_NVStorageEraseEntry(msg1);
            SendMessage(message1);
            
            // Try to read
            ExternalInterface::NVStorageReadEntry msg;
            msg.tag = singleBlobTag;
            
            ExternalInterface::MessageGameToEngine message;
            message.Set_NVStorageReadEntry(msg);
            SendMessage(message);
            
            _testState = TestState::ReadWipeAll;
          }
          break;
        }
        case TestState::ReadWipeAll:
        {
          IF_CONDITION_WITH_TIMEOUT_ASSERT(_readAckd && _eraseAckd && _lastResult == NVStorage::NVResult::NV_NOT_FOUND, DEFAULT_TIMEOUT)
          {
            ClearAcks();
            
            _lastResult = NVStorage::NVResult::NV_OKAY;
            
            ExternalInterface::NVStorageReadEntry msg;
            msg.tag = NVStorage::NVEntryTag::NVEntry_WipeAll;
            
            ExternalInterface::MessageGameToEngine message;
            message.Set_NVStorageReadEntry(msg);
            SendMessage(message);
            
            _testState = TestState::WriteWipeAll;
          }
          break;
        }
        case TestState::WriteWipeAll:
        {
          IF_CONDITION_WITH_TIMEOUT_ASSERT(_readAckd && _lastResult == NVStorage::NVResult::NV_ERROR, DEFAULT_TIMEOUT)
          {
            ClearAcks();
            
            ExternalInterface::NVStorageWriteEntry msg;
            RandomData(5, msg.data.data());
            msg.tag = NVStorage::NVEntryTag::NVEntry_WipeAll;
            msg.data_length = 5;
            msg.index = 0;
            msg.numTotalBlobs = 1;
            
            ExternalInterface::MessageGameToEngine message;
            message.Set_NVStorageWriteEntry(msg);
            SendMessage(message);
            
            _testState = TestState::Final;
          }
          break;
        }
        case TestState::Final:
        {
          IF_CONDITION_WITH_TIMEOUT_ASSERT(_writeAckd && _lastResult == NVStorage::NVResult::NV_ERROR, DEFAULT_TIMEOUT)
          {
            CST_EXIT();
          }
          break;
        }
      }
      return _result;
    }
    
    
    // ================ Message handler callbacks ==================
    void CST_NVStorage::HandleNVStorageOpResult(const ExternalInterface::NVStorageOpResult &msg)
    {
      if(msg.op == NVStorage::NVOperation::NVOP_READ)
      {
        _readAckd = true;
      }
      else if(msg.op == NVStorage::NVOperation::NVOP_WRITE)
      {
        _writeAckd = true;
        _numWrites++;
      }
      else
      {
        _eraseAckd = true;
      }
      _lastResult = msg.result;
    }
    
    // ================ End of message handler callbacks ==================
    
  } // end namespace Cozmo
} // end namespace Anki


/*
 * File:          cozmoEngine.cpp
 * Date:          12/23/2014
 *
 * Description:   (See header file.)
 *
 * Author: Andrew Stein / Kevin Yoon
 *
 * Modifications:
 */

#include "cozmoGame_impl.h"

#include "anki/cozmo/basestation/cozmoEngine.h"
#include "anki/cozmo/basestation/robot.h"
#include "anki/cozmo/basestation/soundManager.h"
#include "anki/cozmo/basestation/utils/parsingConstants/parsingConstants.h"

#include "anki/common/basestation/utils/logging/logging.h"
#include "anki/common/basestation/math/rect_impl.h"
#include "anki/common/basestation/math/quad_impl.h"

#include "anki/cozmo/game/comms/uiMessageHandler.h"
#include "anki/cozmo/basestation/multiClientComms.h"
#include "anki/cozmo/basestation/signals/cozmoEngineSignals.h"
#include "anki/cozmo/game/signals/cozmoGameSignals.h"

namespace Anki {
namespace Cozmo {
  
  const float UI_PING_TIMEOUT_SEC = 5.0f;
  
#pragma mark - CozmoGame Implementation
    
  CozmoGameImpl::CozmoGameImpl()
  : _isHost(true)
  , _isEngineStarted(false)
  , _runState(CozmoGame::STOPPED)
  , _cozmoEngine(nullptr)
  , _desiredNumUiDevices(1)
  , _desiredNumRobots(1)
  , _uiAdvertisementService("UIAdvertisementService")
  , _hostUiDeviceID(1)
  {
    _pingToUI.counter = 0;
    
    SetupSignalHandlers();
    
    
    PRINT_NAMED_INFO("CozmoEngineHostImpl.Constructor",
                     "Starting UIAdvertisementService, reg port %d, ad port %d\n",
                     UI_ADVERTISEMENT_REGISTRATION_PORT, UI_ADVERTISING_PORT);
    
    _uiAdvertisementService.StartService(UI_ADVERTISEMENT_REGISTRATION_PORT,
                                         UI_ADVERTISING_PORT);
  }
  
  CozmoGameImpl::~CozmoGameImpl()
  {
    if(_cozmoEngine != nullptr) {
      delete _cozmoEngine;
      _cozmoEngine = nullptr;
    }
  
    VizManager::getInstance()->Disconnect();
    
    // Remove singletons
    SoundManager::removeInstance();
    VizManager::removeInstance();
  }
  
  CozmoGameImpl::RunState CozmoGameImpl::GetRunState() const
  {
    return _runState;
  }
  
  Result CozmoGameImpl::Init(const Json::Value& config)
  {
    _lastPingTimeFromUI_sec = -1.f;
    
    if(_isEngineStarted) {
      // We've already initialzed and started running before, so shut down the
      // already-running engine.
      PRINT_NAMED_INFO("CozmoGameImpl.Init",
                       "Re-initializing, so destroying existing cozmo engine and "
                       "waiting for another StartEngine command.\n");
      
      delete _cozmoEngine;
      _cozmoEngine = nullptr;
      _isEngineStarted = false;
    }
    
    if(!config.isMember(AnkiUtil::kP_ADVERTISING_HOST_IP) ||
       !config.isMember(AnkiUtil::kP_UI_ADVERTISING_PORT)) {
      
      PRINT_NAMED_ERROR("CozmoGameHostImpl.Init", "Missing advertising hosdt / UI advertising port in Json config file.\n");
      return RESULT_FAIL;
    }
    
    Result lastResult = _uiComms.Init(config[AnkiUtil::kP_ADVERTISING_HOST_IP].asCString(),
                                      config[AnkiUtil::kP_UI_ADVERTISING_PORT].asInt());

    if(lastResult != RESULT_OK) {
      PRINT_NAMED_ERROR("CozmoGameHostImpl.Init", "Failed to initialize host uiComms.\n");
      return lastResult;
    }
    
    _uiMsgHandler.Init(&_uiComms);
    RegisterCallbacksU2G();
    
    if(!config.isMember(AnkiUtil::kP_NUM_ROBOTS_TO_WAIT_FOR)) {
      PRINT_NAMED_WARNING("CozmoGameHostImpl.Init", "No NumRobotsToWaitFor defined in Json config, defaulting to 1.\n");
      _desiredNumRobots = 1;
    } else {
      _desiredNumRobots    = config[AnkiUtil::kP_NUM_ROBOTS_TO_WAIT_FOR].asInt();
    }
    
    if(!config.isMember(AnkiUtil::kP_NUM_UI_DEVICES_TO_WAIT_FOR)) {
      PRINT_NAMED_WARNING("CozmoGameHostImpl.Init", "No NumUiDevicesToWaitFor defined in Json config, defaulting to 1.\n");
      _desiredNumUiDevices = 1;
    } else {
      _desiredNumUiDevices = config[AnkiUtil::kP_NUM_UI_DEVICES_TO_WAIT_FOR].asInt();
    }

    _config = config;
    
    _runState = CozmoGame::STOPPED;
        
    return lastResult;
  }
  
  Result CozmoGameImpl::StartEngine(Json::Value config)
  {
    Result lastResult = RESULT_FAIL;
    
    if(!config.isMember("asHost")) {
      
      PRINT_NAMED_ERROR("CozmoGameImpl.StartEngine",
                        "Missing 'asHost' field in configuration.\n");
      return RESULT_FAIL;
    }
    
    // Pass the game's advertising IP/port info along to the engine:
    config[AnkiUtil::kP_ADVERTISING_HOST_IP]    = _config[AnkiUtil::kP_ADVERTISING_HOST_IP];
    config[AnkiUtil::kP_ROBOT_ADVERTISING_PORT] = _config[AnkiUtil::kP_ROBOT_ADVERTISING_PORT];
    config[AnkiUtil::kP_UI_ADVERTISING_PORT]    = _config[AnkiUtil::kP_UI_ADVERTISING_PORT];
    
    _isHost = config["asHost"].asBool();
    
    //if(_runState == CozmoGame::WAITING_FOR_UI_DEVICES) {
      
      if(_isEngineStarted) {
        delete _cozmoEngine;
      }
      
      if(_isHost) {
        PRINT_NAMED_INFO("CozmoGameImpl.StartEngine", "Creating HOST engine.\n");
        CozmoEngineHost* engineHost = new CozmoEngineHost();
        engineHost->ListenForRobotConnections(true);
        _cozmoEngine = engineHost;
      } else {
        PRINT_NAMED_INFO("CozmoGameImpl.StartEngine", "Creating CLIENT engine.\n");
        _cozmoEngine = new CozmoEngineClient();
      }
      
      // Init the engine with the given configuration info:
      lastResult = _cozmoEngine->Init(config);
      
      if(lastResult == RESULT_OK) {
        _isEngineStarted = true;
      } else {
        PRINT_NAMED_ERROR("CozmoGameHostImpl.StartEngine",
                          "Failed to initialize the engine.\n");
      }
    /*
    } else {
      PRINT_NAMED_ERROR("CozmoGameHostImpl.StartEngine",
                        "Engine already running, must start from stopped state.\n");
    }
     */
    
    _runState = CozmoGame::WAITING_FOR_UI_DEVICES;
    
    return lastResult;
  }
  
  void CozmoGameImpl::SetImageSendMode(RobotID_t forRobotID, Cozmo::ImageSendMode_t newMode)
  {
    _imageSendMode[forRobotID] = newMode;
  }
  
  bool CozmoGameImpl::GetCurrentRobotImage(RobotID_t robotId, Vision::Image& img, TimeStamp_t newerThanTime)
  {
    return _cozmoEngine->GetCurrentRobotImage(robotId, img, newerThanTime);
  }
  
  void CozmoGameImpl::ProcessDeviceImage(const Vision::Image& image)
  {
    _visionMarkersDetectedByDevice.clear();
    
    _cozmoEngine->ProcessDeviceImage(image);
  }

  const std::vector<Cozmo::G2U::DeviceDetectedVisionMarker>& CozmoGameImpl::GetVisionMarkersDetectedByDevice() const
  {
    return _visionMarkersDetectedByDevice;
  }
  
  void CozmoGameImpl::ForceAddRobot(int              robotID,
                                    const char*      robotIP,
                                    bool             robotIsSimulated)
  {
    if(_isHost) {
      CozmoEngineHost* cozmoEngineHost = reinterpret_cast<CozmoEngineHost*>(_cozmoEngine);
      assert(cozmoEngineHost != nullptr);
      cozmoEngineHost->ForceAddRobot(robotID, robotIP, robotIsSimulated);
    } else {
      PRINT_NAMED_ERROR("CozmoGameImpl.ForceAddRobot",
                        "Cannot force-add a robot to game running as client.\n");
    }
  }
  
  bool CozmoGameImpl::ConnectToUiDevice(AdvertisingUiDevice whichDevice)
  {
    const bool success = _uiComms.ConnectToDeviceByID(whichDevice);
    if(success) {
      _connectedUiDevices.push_back(whichDevice);
    }
    CozmoGameSignals::UiDeviceConnectedSignal().emit(whichDevice, success);
    return success;
  }

  bool CozmoGameImpl::ConnectToRobot(AdvertisingRobot whichRobot)
  {
    return _cozmoEngine->ConnectToRobot(whichRobot);
  }
  
  int CozmoGameImpl::GetNumRobots() const
  {
    if(_isHost) {
      CozmoEngineHost* cozmoEngineHost = reinterpret_cast<CozmoEngineHost*>(_cozmoEngine);
      assert(cozmoEngineHost != nullptr);
      return cozmoEngineHost->GetNumRobots();
    } else {
      PRINT_NAMED_ERROR("CozmoGameImpl.GetNumRobots",
                        "Cannot request number of robots from game running as client.\n");
      return -1;
    }
  }

  Result CozmoGameImpl::Update(const float currentTime_sec)
  {
    Result lastResult = RESULT_OK;
  
    if(_lastPingTimeFromUI_sec > 0.f) {
      const f32 timeSinceLastUiPing = currentTime_sec - _lastPingTimeFromUI_sec;
      
      if(timeSinceLastUiPing > UI_PING_TIMEOUT_SEC) {
        /*
        PRINT_NAMED_ERROR("CozmoGameImpl.Update",
                          "Lost connection to UI (no ping in %.2f seconds). Resetting.\n",
                          timeSinceLastUiPing);
        
        Init(_config);
        return lastResult;
         */
        
        PRINT_NAMED_WARNING("CozmoGameImpl.Update",
                            "No ping from UI in %.2f seconds, but NOT ressetting.\n",
                            timeSinceLastUiPing);
        _lastPingTimeFromUI_sec = -1.f;
      }
    }
    
    // Update UI comms
    if(_uiComms.IsInitialized()) {
      _uiComms.Update();
      
      if(_uiComms.GetNumConnectedDevices() > 0) {
        // Ping the UI to let them know we're still here
        G2U::Message message;
        message.Set_Ping(_pingToUI);
        _uiMsgHandler.SendMessage(_hostUiDeviceID, message);
        ++_pingToUI.counter;
      }
    }
    
    // Handle UI messages
    _uiMsgHandler.ProcessMessages();
    
    if(!_isEngineStarted || _runState == CozmoGame::WAITING_FOR_UI_DEVICES) {
      // If we are still waiting on the engine to start, or even if it is started
      // but we have not connected to enough UI devices, then keep ticking the
      // UI advertisement service and connect to anything advertising until we
      // have enough devices and can switch to looking for robots.
      
      _uiAdvertisementService.Update();
      
      // TODO: Do we want to do this all the time in case UI devices want to join later?
      // Notify the UI that there are advertising devices
      std::vector<int> advertisingUiDevices;
      _uiComms.GetAdvertisingDeviceIDs(advertisingUiDevices);
      for(auto & device : advertisingUiDevices) {
        if(device == _hostUiDeviceID) {
          // Force connection to first (local) UI device
          if(true == ConnectToUiDevice(device)) {
            PRINT_NAMED_INFO("CozmoGameHostImpl.Update",
                             "Automatically connected to local UI device %d!\n", device);
          }
        } else {
          CozmoGameSignals::UiDeviceAvailableSignal().emit(device);
        }
      }
      
      if(_uiComms.GetNumConnectedDevices() >= _desiredNumUiDevices) {
        PRINT_NAMED_INFO("CozmoGameImpl.UpdateAsHost",
                         "Enough UI devices connected (%d), will wait for %d robots.\n",
                         _desiredNumUiDevices, _desiredNumRobots);
        _runState = CozmoGame::WAITING_FOR_ROBOTS;
      }
      
    } else {
      if(_isHost) {
        lastResult = UpdateAsHost(currentTime_sec);
      } else {
        lastResult = UpdateAsClient(currentTime_sec);
      }
    }
    
    return lastResult;
    
  } // Update()
  
  Result CozmoGameImpl::UpdateAsHost(const float currentTime_sec)
  {
    Result lastResult = RESULT_OK;
    
    CozmoEngineHost* cozmoEngineHost = reinterpret_cast<CozmoEngineHost*>(_cozmoEngine);
    assert(cozmoEngineHost != nullptr);
    
    switch(_runState)
    {
      case CozmoGame::STOPPED:
        // Nothing to do
        break;
        
      case CozmoGame::WAITING_FOR_UI_DEVICES:
      {
        /*
        _uiAdvertisementService.Update();
        
        // TODO: Do we want to do this all the time in case UI devices want to join later?
        // Notify the UI that there are advertising devices
        std::vector<int> advertisingUiDevices;
        _uiComms.GetAdvertisingDeviceIDs(advertisingUiDevices);
        for(auto & device : advertisingUiDevices) {
          if(device == _hostUiDeviceID) {
            // Force connection to first (local) UI device
            if(true == ConnectToUiDevice(device)) {
              PRINT_NAMED_INFO("CozmoGameHostImpl.Update",
                               "Automatically connected to local UI device %d!\n", device);
            }
          } else {
            CozmoGameSignals::UiDeviceAvailableSignal().emit(device);
          }
        }
        
        if(_uiComms.GetNumConnectedDevices() >= _desiredNumUiDevices) {
          PRINT_NAMED_INFO("CozmoGameImpl.UpdateAsHost",
                           "Enough UI devices connected (%d), will wait for %d robots.\n",
                           _desiredNumUiDevices, _desiredNumRobots);
          cozmoEngineHost->ListenForRobotConnections(true);
          _runState = CozmoGame::WAITING_FOR_ROBOTS;
        }
         */
        break;
      }
        
      case CozmoGame::WAITING_FOR_ROBOTS:
      {
        lastResult = cozmoEngineHost->Update(currentTime_sec);
        if (lastResult != RESULT_OK) {
          PRINT_NAMED_WARNING("CozmoGameImpl.UpdateAsHost",
                              "Bad engine update: status = %d\n", lastResult);
        }
        
        // Tell the engine to keep listening for robots until it reports that
        // it has connections to enough
        if(cozmoEngineHost->GetNumRobots() >= _desiredNumRobots) {
          PRINT_NAMED_INFO("CozmoGameImpl.UpdateAsHost",
                           "Enough robots connected (%d), will run engine.\n",
                           _desiredNumRobots);
          // TODO: We could keep listening for others to join mid-game...
          //cozmoEngineHost->ListenForRobotConnections(false);
          _runState = CozmoGame::ENGINE_RUNNING;
        }
        break;
      }
        
      case CozmoGame::ENGINE_RUNNING:
      {
        lastResult = cozmoEngineHost->Update(currentTime_sec);
        
        if (lastResult != RESULT_OK) {
          PRINT_NAMED_WARNING("CozmoGameImpl.UpdateAsHost",
                              "Bad engine update: status = %d\n", lastResult);
        } else {
          // Send out robot state information for each robot:
          auto robotIDs = cozmoEngineHost->GetRobotIDList();
          for(auto & robotID : robotIDs) {
            Robot* robot = cozmoEngineHost->GetRobotByID(robotID);
            if(robot == nullptr) {
              PRINT_NAMED_ERROR("CozmoGameImpl.UpdateAsHost", "Null robot returned for ID=%d!\n", robotID);
              lastResult = RESULT_FAIL;
            } else {
              if(robot->HasReceivedRobotState()) {
                G2U::RobotState msg;
                
                msg.robotID = robotID;
                
                msg.pose_x = robot->GetPose().GetTranslation().x();
                msg.pose_y = robot->GetPose().GetTranslation().y();
                msg.pose_z = robot->GetPose().GetTranslation().z();
                
                msg.poseAngle_rad = robot->GetPose().GetRotationAngle<'Z'>().ToFloat();
                const UnitQuaternion<float>& q = robot->GetPose().GetRotation().GetQuaternion();
                msg.pose_quaternion0 = q.w();
                msg.pose_quaternion1 = q.x();
                msg.pose_quaternion2 = q.y();
                msg.pose_quaternion3 = q.z();
                
                msg.leftWheelSpeed_mmps  = robot->GetLeftWheelSpeed();
                msg.rightWheelSpeed_mmps = robot->GetRigthWheelSpeed();
                
                msg.headAngle_rad = robot->GetHeadAngle();
                msg.liftHeight_mm = robot->GetLiftHeight();
                
                msg.status = 0;
                if(robot->IsMoving())           { msg.status |= IS_MOVING; }
                if(robot->IsPickingOrPlacing()) { msg.status |= IS_PICKING_OR_PLACING; }
                if(robot->IsPickedUp())         { msg.status |= IS_PICKED_UP; }
                if(robot->IsAnimating())        { msg.status |= IS_ANIMATING; }
                if(robot->IsCarryingObject())   {
                  msg.status |= IS_CARRYING_BLOCK;
                  msg.carryingObjectID = robot->GetCarryingObject();
                  msg.carryingObjectOnTopID = robot->GetCarryingObjectOnTop();
                } else {
                  msg.carryingObjectID = -1;
                }
                if(!robot->GetActionList().IsEmpty()) {
                  msg.status |= IS_PERFORMING_ACTION;
                }
                
                msg.headTrackingObjectID = robot->GetTrackHeadToObject();
                
                // TODO: Add proximity sensor data to state message
                
                msg.batteryVoltage = robot->GetBatteryVoltage();
                
                G2U::Message message;
                message.Set_RobotState(msg);
                _uiMsgHandler.SendMessage(_hostUiDeviceID, message);
              } else {
                PRINT_NAMED_WARNING("CozmoGameImpl.UpdateAsHost",
                                    "Not sending robot %d state (none available).\n",
                                    robotID);
              }
            }
          }
        }
        break;
      }
        
      default:
        PRINT_NAMED_ERROR("CozmoGameImpl.UpdateAsHost",
                          "Reached unknown RunState %d.\n", _runState);
        
    }
    
    return lastResult;
    
  }
  
  Result CozmoGameImpl::UpdateAsClient(const float currentTime_sec)
  {
    Result lastResult = RESULT_OK;
    
    // Don't tick the engine until it has been started
    if(_runState != CozmoGame::STOPPED) {
      lastResult = _cozmoEngine->Update(currentTime_sec);
    }
    
    return lastResult;
  } // UpdateAsClient()
  
  bool CozmoGameImpl::SendRobotImage(RobotID_t robotID)
  {
    PRINT_NAMED_WARNING("CozmoGameImpl.SendRobotImage",
                        "SendRobotImage is deprecated. Expecting to use direct forwarding of compressed image chunks to UI.\n");
    
    // Get the image from the robot
    Vision::Image img;
    // TODO: fill in the timestamp?
    const bool gotImage = GetCurrentRobotImage(robotID, img, 0);
    
    // TODO: Send full resolution images
    // For now, just resize to QVGA for sending to UI
    img.Resize(240, 320);
    
    if(gotImage) {
      
      static u32 imgID = 0;
      
      // Downsample and split into image chunk message
      const s32 ncols = img.GetNumCols();
      const s32 nrows = img.GetNumRows();
      
      const u32 numTotalBytes = nrows*ncols;

      G2U::ImageChunk m;
      const int G2U_IMAGE_CHUNK_SIZE = m.data.size();
      
      // TODO: pass this in so it corresponds to actual frame capture time instead of send time
      m.frameTimeStamp = img.GetTimestamp();
      m.nrows = nrows;
      m.ncols = ncols;
      m.imageId = ++imgID;
      m.chunkId = 0;
      m.chunkSize = m.data.size();
      m.imageChunkCount = ceilf((f32)numTotalBytes / m.data.size());
      m.imageEncoding = Vision::IE_RAW_GRAY;
      
      u32 totalByteCnt = 0;
      u32 chunkByteCnt = 0;
      
      //PRINT("Downsample: from %d x %d  to  %d x %d\n", img.get_size(1), img.get_size(0), xRes, yRes);
      
      G2U::Message message;
      
      for(s32 i=0; i<nrows; ++i) {
        
        const u8* img_i = img.GetRow(i);
        
        for(s32 j=0; j<ncols; ++j) {
          m.data[chunkByteCnt] = img_i[j];
          ++chunkByteCnt;
          ++totalByteCnt;
          
          if(chunkByteCnt == m.data.size()) {
            // Filled this chunk
            message.Set_ImageChunk(m);
            _uiMsgHandler.SendMessage(_hostUiDeviceID, message);
            ++m.chunkId;
            chunkByteCnt = 0;
          } else if(totalByteCnt == numTotalBytes) {
            // This is the last chunk!
            m.chunkSize = chunkByteCnt;
            message.Set_ImageChunk(m);
            _uiMsgHandler.SendMessage(_hostUiDeviceID, message);
          }
        } // for each col
      } // for each row
      
    } // if gotImage
    
    return gotImage;
    
  } // SendImage()
  
  
#pragma mark - CozmoGame Wrappers
  
  CozmoGame::CozmoGame()
  : _impl(nullptr)
  {
    _impl = new CozmoGameImpl();
  }
  
  CozmoGame::~CozmoGame()
  {
    delete _impl;
  }
  
  Result CozmoGame::Init(const Json::Value &config)
  {
    return _impl->Init(config);
  }
  
  Result CozmoGame::StartEngine(Json::Value config)
  {
    return _impl->StartEngine(config);
  }
  
  void CozmoGame::ForceAddRobot(int robotID, const char *robotIP, bool robotIsSimulated)
  {
    _impl->ForceAddRobot(robotID, robotIP, robotIsSimulated);
  }
  
  Result CozmoGame::Update(const float currentTime_sec)
  {
    return _impl->Update(currentTime_sec);
  }
  
  bool CozmoGame::GetCurrentRobotImage(RobotID_t robotId, Vision::Image& img, TimeStamp_t newerThanTime)
  {
    assert(_impl != nullptr);
    return _impl->GetCurrentRobotImage(robotId, img, newerThanTime);
  }
  
  void CozmoGame::ProcessDeviceImage(const Vision::Image& image)
  {
    _impl->ProcessDeviceImage(image);
  }
  
  CozmoGame::RunState CozmoGame::GetRunState() const
  {
    assert(_impl != nullptr);
    return _impl->GetRunState();
  }
  
  const std::vector<Cozmo::G2U::DeviceDetectedVisionMarker>& CozmoGame::GetVisionMarkersDetectedByDevice() const
  {
    return _impl->GetVisionMarkersDetectedByDevice();
  }

  
} // namespace Cozmo
} // namespace Anki



/*
 * File:          UiGameController.cpp
 * Date:
 * Description:   
 * Author:        
 * Modifications: 
 */

#include "anki/cozmo/simulator/game/uiGameController.h"
#include "anki/cozmo/shared/cozmoEngineConfig.h"
#include "anki/cozmo/game/comms/gameMessageHandler.h"
#include "anki/cozmo/game/comms/gameComms.h"
#include "anki/cozmo/basestation/behaviorSystem/behaviorTypesHelpers.h"
#include "anki/common/basestation/math/point_impl.h"
#include "clad/externalInterface/messageEngineToGame.h"
#include "clad/externalInterface/messageGameToEngine.h"
#include <stdio.h>
#include <string.h>



namespace Anki {
  namespace Cozmo {
    
      // Private members:
      namespace {

        const f32 TIME_UNTIL_READY_SEC = 1.5;
        
        s32 _stepTimeMS;
        webots::Supervisor _supervisor;
        
        webots::Node* _robotNode = nullptr;
        std::vector<std::pair<webots::Node*, Pose3d> > _lightCubes;
        auto _lightCubeOriginIter = _lightCubes.end();
        
        Pose3d _robotPose;
        Pose3d _robotPoseActual;
        
        ExternalInterface::RobotState _robotStateMsg;
        
        UiGameController::ObservedObject _lastObservedObject;
        std::map<s32, std::pair<ObjectFamily, ObjectType> > _objectIDToFamilyTypeMap;
        std::map<ObjectFamily, std::map<ObjectType, std::vector<s32> > > _objectFamilyToTypeToIDMap;
        std::map<s32, Pose3d> _objectIDToPoseMap;
        
        Vision::TrackedFace::ID_t _lastObservedFaceID;
        
        std::unordered_set<std::string> _availableAnimations;
        
        webots::Node* _root = nullptr;
        
        typedef enum {
          UI_WAITING_FOR_GAME = 0,
          UI_RUNNING
        } UI_State_t;
        
        UI_State_t _uiState;
        
        GameMessageHandler _msgHandler;
        GameComms *_gameComms = nullptr;

        Util::Data::DataPlatform* _dataPlatform = nullptr;
      } // private namespace

    
    // ======== Message handler callbacks =======
      
    // TODO: Update these not to need robotID
    
    void UiGameController::HandleRobotStateUpdateBase(ExternalInterface::RobotState const& msg)
    {
      _robotPose.SetTranslation({msg.pose_x, msg.pose_y, msg.pose_z});
      _robotPose.SetRotation(msg.poseAngle_rad, Z_AXIS_3D());
      
      _robotStateMsg = msg;
      
      HandleRobotStateUpdate(msg);
    }
    
    void UiGameController::HandleRobotObservedObjectBase(ExternalInterface::RobotObservedObject const& msg)
    {
      // Get object info
      s32 objID = msg.objectID;
      ObjectFamily objFamily = msg.objectFamily;
      ObjectType objType = msg.objectType;
      UnitQuaternion<float> q(msg.quaternion_w, msg.quaternion_x, msg.quaternion_y, msg.quaternion_z);
      Vec3f trans(msg.world_x, msg.world_y, msg.world_z);
      
      // If an object with the same ID already exists in the map, make sure that it's type hasn't changed
      auto it = _objectIDToFamilyTypeMap.find(objID);
      if (it != _objectIDToFamilyTypeMap.end()) {
        if (it->second.first != objFamily || it->second.second != objType) {
          PRINT_NAMED_WARNING("UiGameController.HandleRobotObservedObjectBase.ObjectChangedFamilyOrType", "");
        }
      } else {
        // Insert new object into maps
        _objectIDToFamilyTypeMap.insert(std::make_pair(objID, std::make_pair(objFamily, objType)));
        _objectFamilyToTypeToIDMap[objFamily][objType].push_back(objID);
      }
      
      // Update pose
      _objectIDToPoseMap[objID] = Pose3d(q, trans);
      

      
      // TODO: Move this to WebotsKeyboardController?
      const f32 area = msg.img_width * msg.img_height;
      _lastObservedObject.family = msg.objectFamily;
      _lastObservedObject.type   = msg.objectType;
      _lastObservedObject.id     = msg.objectID;
      _lastObservedObject.isActive = msg.isActive;
      _lastObservedObject.area   = area;

      
      HandleRobotObservedObject(msg);
    }
    
    void UiGameController::HandleRobotObservedFaceBase(ExternalInterface::RobotObservedFace const& msg)
    {
      _lastObservedFaceID = msg.faceID;
      
      HandleRobotObservedFace(msg);
    }
    
    void UiGameController::HandleRobotObservedNothingBase(ExternalInterface::RobotObservedNothing const& msg)
    {
      _lastObservedObject.Reset();
      
      HandleRobotObservedNothing(msg);
    }
    
    void UiGameController::HandleRobotDeletedObjectBase(ExternalInterface::RobotDeletedObject const& msg)
    {
      PRINT_NAMED_INFO("UiGameController.HandleRobotDeletedObjectBase", "Robot %d reported deleting object %d", msg.robotID, msg.objectID);
      
      _objectIDToPoseMap.erase(msg.objectID);
      _objectIDToFamilyTypeMap.erase(msg.objectID);
      
      for (auto famIt = _objectFamilyToTypeToIDMap.begin(); famIt != _objectFamilyToTypeToIDMap.end(); ++famIt) {
        for (auto typeIt = famIt->second.begin(); typeIt != famIt->second.end(); ++typeIt) {
          auto objIt = std::find(typeIt->second.begin(), typeIt->second.end(), msg.objectID);
          if (objIt != typeIt->second.end()) {
            typeIt->second.erase(objIt);
          }
        }
      }
      
      HandleRobotDeletedObject(msg);
    }

    void UiGameController::HandleRobotConnectionBase(ExternalInterface::RobotAvailable const& msgIn)
    {
      // Just send a message back to the game to connect to any robot that's
      // advertising (since we don't have a selection mechanism here)
      PRINT_NAMED_INFO("UiGameController.HandleRobotConnectionBase", "Sending message to command connection to robot %d.", msgIn.robotID);
      ExternalInterface::ConnectToRobot msgOut;
      msgOut.robotID = msgIn.robotID;
      ExternalInterface::MessageGameToEngine message;
      message.Set_ConnectToRobot(msgOut);
      SendMessage(message);
      
      HandleRobotConnection(msgIn);
    }
    
    void UiGameController::HandleUiDeviceConnectionBase(ExternalInterface::UiDeviceAvailable const& msgIn)
    {
      // Just send a message back to the game to connect to any UI device that's
      // advertising (since we don't have a selection mechanism here)
      PRINT_NAMED_INFO("UiGameController.HandleUiDeviceConnectionBase", "Sending message to command connection to UI device %d.", msgIn.deviceID);
      ExternalInterface::ConnectToUiDevice msgOut;
      msgOut.deviceID = msgIn.deviceID;
      ExternalInterface::MessageGameToEngine message;
      message.Set_ConnectToUiDevice(msgOut);
      SendMessage(message);
      
      HandleUiDeviceConnection(msgIn);
    }
    
    void UiGameController::HandleRobotConnectedBase(ExternalInterface::RobotConnected const &msg)
    {
      // Once robot connects, set resolution
      //SendSetRobotImageSendMode(ISM_STREAM);
      
      HandleRobotConnected(msg);
    }
    
    void UiGameController::HandleRobotCompletedActionBase(ExternalInterface::RobotCompletedAction const& msg)
    {
      switch((RobotActionType)msg.actionType)
      {
        case RobotActionType::PICKUP_OBJECT_HIGH:
        case RobotActionType::PICKUP_OBJECT_LOW:
          printf("Robot %d %s picking up stack of %d objects with IDs: ",
                 msg.robotID, ActionResultToString(msg.result),
                 msg.completionInfo.numObjects);
          for(int i=0; i<msg.completionInfo.numObjects; ++i) {
            printf("%d ", msg.completionInfo.objectIDs[i]);
          }
          printf("[Tag=%d]\n", msg.idTag);
          break;
          
        case RobotActionType::PLACE_OBJECT_HIGH:
        case RobotActionType::PLACE_OBJECT_LOW:
          printf("Robot %d %s placing stack of %d objects with IDs: ",
                 msg.robotID, ActionResultToString(msg.result),
                 msg.completionInfo.numObjects);
          for(int i=0; i<msg.completionInfo.numObjects; ++i) {
            printf("%d ", msg.completionInfo.objectIDs[i]);
          }
          printf("[Tag=%d]\n", msg.idTag);
          break;

        case RobotActionType::PLAY_ANIMATION:
          PRINT_NAMED_INFO("UiGameController.HandleRobotCompletedActionBase", "Robot %d finished playing animation %s. [Tag=%d]",
                 msg.robotID, msg.completionInfo.animName.c_str(), msg.idTag);
          break;
          
        default:
          PRINT_NAMED_INFO("UiGameController.HandleRobotCompletedActionBase", "Robot %d completed action with type=%d and tag=%d: %s.",
                 msg.robotID, msg.actionType, msg.idTag, ActionResultToString(msg.result));
      }
      
      HandleRobotCompletedAction(msg);
    }
    
    // For processing image chunks arriving from robot.
    // Sends complete images to VizManager for visualization (and possible saving).
    void UiGameController::HandleImageChunkBase(ImageChunk const& msg)
    {
      HandleImageChunk(msg);
    } // HandleImageChunk()
    
    
    void UiGameController::HandleActiveObjectMovedBase(ObjectMoved const& msg)
    {
     PRINT_NAMED_INFO("HandleActiveObjectMoved", "Received message that object %d moved. Accel=(%f,%f,%f). UpAxis=%s",
                      msg.objectID, msg.accel.x, msg.accel.y, msg.accel.z, UpAxisToString(msg.upAxis));
      
      HandleActiveObjectMoved(msg);
    }
    
    void UiGameController::HandleActiveObjectStoppedMovingBase(ObjectStoppedMoving const& msg)
    {
      PRINT_NAMED_INFO("HandleActiveObjectStoppedMoving", "Received message that object %d stopped moving%s. UpAxis=%s",
                       msg.objectID, (msg.rolled ? " and rolled" : ""), UpAxisToString(msg.upAxis));
      
      HandleActiveObjectStoppedMoving(msg);
    }
    
    void UiGameController::HandleActiveObjectTappedBase(ObjectTapped const& msg)
    {
      PRINT_NAMED_INFO("HandleActiveObjectTapped", "Received message that object %d was tapped %d times.",
                       msg.objectID, msg.numTaps);
      
      HandleActiveObjectTapped(msg);
    }

    void UiGameController::HandleAnimationAvailableBase(ExternalInterface::AnimationAvailable const& msg)
    {
      PRINT_NAMED_INFO("HandleAnimationAvailable", "Animation available: %s", msg.animName.c_str());
      _availableAnimations.insert(msg.animName);
      
      HandleAnimationAvailable(msg);
    }
    
    // ===== End of message handler callbacks ====
    
  
    UiGameController::UiGameController(s32 step_time_ms)
    {
      _stepTimeMS = step_time_ms;
      _robotNode = nullptr;
      _robotPose.SetTranslation({0.f, 0.f, 0.f});
      _robotPose.SetRotation(0, Z_AXIS_3D());
      _robotPoseActual.SetTranslation({0.f, 0.f, 0.f});
      _robotPoseActual.SetRotation(0, Z_AXIS_3D());
      
      _lastObservedObject.Reset();
    }
    
    UiGameController::~UiGameController()
    {
      if (_gameComms) {
        delete _gameComms;
      }
    }
    
    void UiGameController::Init()
    {
      // Make root point to WebotsKeyBoardController node
      _root = _supervisor.getSelf();
      
      // Set deviceID
      // TODO: Get rid of this. The UI should not be assigning it's own ID.
      int deviceID = 1;
      webots::Field* deviceIDField = _root->getField("deviceID");
      if (deviceIDField) {
        deviceID = deviceIDField->getSFInt32();
      }
      
      // Get engine IP
      std::string engineIP = "127.0.0.1";
      webots::Field* engineIPField = _root->getField("engineIP");
      if (engineIPField) {
        engineIP = engineIPField->getSFString();
      }
      
      // Startup comms with engine
      if (!_gameComms) {
        printf("Registering with advertising service at %s:%d", engineIP.c_str(), UI_ADVERTISEMENT_REGISTRATION_PORT);
        _gameComms = new GameComms(deviceID, UI_MESSAGE_SERVER_LISTEN_PORT,
                                   engineIP.c_str(),
                                   UI_ADVERTISEMENT_REGISTRATION_PORT);
      }
      
      
      while(!_gameComms->IsInitialized()) {
        PRINT_NAMED_INFO("UiGameController.Init",
                         "Waiting for gameComms to initialize...");
        _supervisor.step(_stepTimeMS);
        _gameComms->Update();
      }
      _msgHandler.Init(_gameComms);
      
      // Register callbacks for incoming messages from game
      // TODO: Have CLAD generate this?
      _msgHandler.RegisterCallbackForMessage([this](const ExternalInterface::MessageEngineToGame& message) {
        switch (message.GetTag()) {
          case ExternalInterface::MessageEngineToGame::Tag::RobotConnected:
            HandleRobotConnectedBase(message.Get_RobotConnected());
            break;
          case ExternalInterface::MessageEngineToGame::Tag::RobotState:
            HandleRobotStateUpdateBase(message.Get_RobotState());
            break;
          case ExternalInterface::MessageEngineToGame::Tag::RobotObservedObject:
            HandleRobotObservedObjectBase(message.Get_RobotObservedObject());
            break;
          case ExternalInterface::MessageEngineToGame::Tag::RobotObservedFace:
            HandleRobotObservedFaceBase(message.Get_RobotObservedFace());
            break;
          case ExternalInterface::MessageEngineToGame::Tag::UiDeviceAvailable:
            HandleUiDeviceConnectionBase(message.Get_UiDeviceAvailable());
            break;
          case ExternalInterface::MessageEngineToGame::Tag::RobotAvailable:
            HandleRobotConnectionBase(message.Get_RobotAvailable());
            break;
          case ExternalInterface::MessageEngineToGame::Tag::ImageChunk:
            HandleImageChunkBase(message.Get_ImageChunk());
            break;
          case ExternalInterface::MessageEngineToGame::Tag::RobotDeletedObject:
            HandleRobotDeletedObjectBase(message.Get_RobotDeletedObject());
            break;
          case ExternalInterface::MessageEngineToGame::Tag::RobotCompletedAction:
            HandleRobotCompletedActionBase(message.Get_RobotCompletedAction());
            break;
          case ExternalInterface::MessageEngineToGame::Tag::ObjectMoved:
            HandleActiveObjectMovedBase(message.Get_ObjectMoved());
            break;
          case ExternalInterface::MessageEngineToGame::Tag::ObjectStoppedMoving:
            HandleActiveObjectStoppedMovingBase(message.Get_ObjectStoppedMoving());
            break;
          case ExternalInterface::MessageEngineToGame::Tag::ObjectTapped:
            HandleActiveObjectTappedBase(message.Get_ObjectTapped());
            break;
          case ExternalInterface::MessageEngineToGame::Tag::AnimationAvailable:
            HandleAnimationAvailableBase(message.Get_AnimationAvailable());
            break;
          default:
            // ignore
            break;
        }
      });

      _uiState = UI_WAITING_FOR_GAME;
      
      InitInternal();
    }
    
  
    bool ForceAddRobotIfSpecified()
    {
      bool doForceAddRobot = false;
      bool forcedRobotIsSim = false;
      std::string forcedRobotIP;
      int  forcedRobotId = 1;
      
      webots::Field* forceAddRobotField = _root->getField("forceAddRobot");
      if(forceAddRobotField != nullptr) {
        doForceAddRobot = forceAddRobotField->getSFBool();
        if(doForceAddRobot) {
          webots::Field *forcedRobotIsSimField = _root->getField("forcedRobotIsSimulated");
          if(forcedRobotIsSimField == nullptr) {
            PRINT_NAMED_ERROR("KeyboardController.Update",
                              "Could not find 'forcedRobotIsSimulated' field.");
            doForceAddRobot = false;
          } else {
            forcedRobotIsSim = forcedRobotIsSimField->getSFBool();
          }
          
          webots::Field* forcedRobotIpField = _root->getField("forcedRobotIP");
          if(forcedRobotIpField == nullptr) {
            PRINT_NAMED_ERROR("KeyboardController.Update",
                              "Could not find 'forcedRobotIP' field.");
            doForceAddRobot = false;
          } else {
            forcedRobotIP = forcedRobotIpField->getSFString();
          }
          
          webots::Field* forcedRobotIdField = _root->getField("forcedRobotID");
          if(forcedRobotIdField == nullptr) {
            
          } else {
            forcedRobotId = forcedRobotIdField->getSFInt32();
          }
        } // if(doForceAddRobot)
      }
      
      if(doForceAddRobot) {
        ExternalInterface::ForceAddRobot msg;
        msg.isSimulated = forcedRobotIsSim;
        msg.robotID = forcedRobotId;
        
        std::fill(msg.ipAddress.begin(), msg.ipAddress.end(), '\0');
        std::copy(forcedRobotIP.begin(), forcedRobotIP.end(), msg.ipAddress.begin());
        
        ExternalInterface::MessageGameToEngine message;
        message.Set_ForceAddRobot(msg);
        _msgHandler.SendMessage(1, message); // TODO: don't hardcode ID here
      }
      
      return doForceAddRobot;
      
    } // ForceAddRobotIfSpecified()
  
    s32 UiGameController::Update()
    {
      s32 res = 0;
      
      if (_supervisor.step(_stepTimeMS) == -1) {
        PRINT_NAMED_INFO("UiGameController.Update.StepFailed", "");
        return -1;
      }
      
      _gameComms->Update();
      
      switch(_uiState) {
        case UI_WAITING_FOR_GAME:
        {
          if (!_gameComms->HasClient()) {
            return 0;
          } else {
            // Once gameComms has a client, tell the engine to start, force-add
            // robot if necessary, and switch states in the UI
            
            PRINT_NAMED_INFO("KeyboardController.Update", "Sending StartEngine message.");
            ExternalInterface::StartEngine msg;
            msg.asHost = 1; // TODO: Support running as client?
            std::string vizIpStr = "127.0.0.1";
            std::fill(msg.vizHostIP.begin(), msg.vizHostIP.end(), '\0'); // ensure null termination
            std::copy(vizIpStr.begin(), vizIpStr.end(), msg.vizHostIP.begin());
            ExternalInterface::MessageGameToEngine message;
            message.Set_StartEngine(msg);
            _msgHandler.SendMessage(1, message); // TODO: don't hardcode ID here
            
            bool didForceAdd = ForceAddRobotIfSpecified();
            
            if(didForceAdd) {
              PRINT_NAMED_INFO("KeyboardController.Update", "Sent force-add robot message.");
            }
            
            // Turn on image streaming to game/UI by default:
            SendImageRequest(ImageSendMode::Stream, 1);
            
            _uiState = UI_RUNNING;
          }
          break;
        }
          
        case UI_RUNNING:
        {
          SendPing();
          
          UpdateActualObjectPoses();
          
          _msgHandler.ProcessMessages();
          
          // TODO: Better way to wait for ready. Ready message to game?
          if (_supervisor.getTime() > TIME_UNTIL_READY_SEC) {
            res = UpdateInternal();
          }
          
          break;
        }
          
        default:
          PRINT_NAMED_ERROR("KeyboardController.Update", "Reached default switch case.");
          
      } // switch(_uiState)
      
      return res;
    }
    
    void UiGameController::UpdateActualObjectPoses()
    {
      // Only look for the robot node once at the beginning
      if (_robotNode == nullptr)
      {
        webots::Field* rootChildren = GetSupervisor()->getRoot()->getField("children");
        int numRootChildren = rootChildren->getCount();
        for (int n = 0 ; n<numRootChildren; ++n) {
          webots::Node* nd = rootChildren->getMFNode(n);
          
          // Get the node name
          std::string nodeName = "";
          webots::Field* nameField = nd->getField("name");
          if (nameField) {
            nodeName = nameField->getSFString();
          }
          
          //PRINT_NAMED_INFO("UiGameController.UpdateActualObjectPoses", " Node %d: name \"%s\" typeName \"%s\" controllerName \"%s\"",
          //       n, nodeName.c_str(), nd->getTypeName().c_str(), controllerName.c_str());
          
          if (nd->getTypeName().find("Supervisor") != std::string::npos &&
              nodeName.find("CozmoBot") != std::string::npos) {

            PRINT_NAMED_INFO("UiGameController.UpdateActualObjectPoses",
                             "Found robot with name %s", nodeName.c_str());
            
            _robotNode = nd;
            
            break;
          }
          else if(nodeName.find("LightCube") != std::string::npos) {
            _lightCubes.emplace_back(std::make_pair(nd, Pose3d()));
            _lightCubeOriginIter = _lightCubes.begin();
            
            PRINT_NAMED_INFO("UiGameController.UpdateActualObjectPoses",
                             "Found LightCube with name %s", nodeName.c_str());

          }
          
        }
      }
      
      const double* transActual = _robotNode->getPosition();
      _robotPoseActual.SetTranslation( {static_cast<f32>(M_TO_MM(transActual[0])),
                                        static_cast<f32>(M_TO_MM(transActual[1])),
                                        static_cast<f32>(M_TO_MM(transActual[2]))} );
      
      const double *orientationActual = _robotNode->getOrientation();
      _robotPoseActual.SetRotation({static_cast<f32>(orientationActual[0]),
                                    static_cast<f32>(orientationActual[1]),
                                    static_cast<f32>(orientationActual[2]),
                                    static_cast<f32>(orientationActual[3]),
                                    static_cast<f32>(orientationActual[4]),
                                    static_cast<f32>(orientationActual[5]),
                                    static_cast<f32>(orientationActual[6]),
                                    static_cast<f32>(orientationActual[7]),
                                    static_cast<f32>(orientationActual[8])} );
      
      
      for(auto & lightCube : _lightCubes)
      {
        transActual = lightCube.first->getPosition();
        orientationActual = lightCube.first->getOrientation();
        
        lightCube.second.SetTranslation({static_cast<f32>(M_TO_MM(transActual[0])),
          static_cast<f32>(M_TO_MM(transActual[1])),
          static_cast<f32>(M_TO_MM(transActual[2]))} );

        lightCube.second.SetRotation({static_cast<f32>(orientationActual[0]),
          static_cast<f32>(orientationActual[1]),
          static_cast<f32>(orientationActual[2]),
          static_cast<f32>(orientationActual[3]),
          static_cast<f32>(orientationActual[4]),
          static_cast<f32>(orientationActual[5]),
          static_cast<f32>(orientationActual[6]),
          static_cast<f32>(orientationActual[7]),
          static_cast<f32>(orientationActual[8])} );
      }
      
    }
    
    void UiGameController::UpdateVizOrigin()
    {
      SetVizOrigin msg;
      
      Pose3d correctionPose;
      if(_robotStateMsg.localizedToObjectID >= 0)
      {
        // Align the pose of the object to which the robot is localized to the
        // the next actual light cube in the world
        ++_lightCubeOriginIter;
        if(_lightCubeOriginIter == _lightCubes.end()) {
          _lightCubeOriginIter = _lightCubes.begin();
        }
       
        PRINT_NAMED_INFO("UiGameController.UpdateVizOrigin",
                         "Aligning viz to match next known LightCube to object %d",
                         _robotStateMsg.localizedToObjectID);
        
        correctionPose = _lightCubeOriginIter->second * _objectIDToPoseMap[_robotStateMsg.localizedToObjectID].GetInverse();
      } else {
        // Robot is not localized to any object, so align the robot's estimated
        // pose to its actual pose in the world

        PRINT_NAMED_INFO("UiGameController.UpdateVizOrigin",
                         "Aligning viz to match robot's pose.");
                         
        correctionPose = _robotPoseActual * _robotPose.GetInverse();
      }
      
      
      const RotationVector3d Rvec(correctionPose.GetRotationVector());
      
      msg.rot_rad = Rvec.GetAngle().ToFloat();
      msg.rot_axis_x = Rvec.GetAxis().x();
      msg.rot_axis_y = Rvec.GetAxis().y();
      msg.rot_axis_z = Rvec.GetAxis().z();
      
      msg.trans_x = correctionPose.GetTranslation().x();
      msg.trans_y = correctionPose.GetTranslation().y();
      msg.trans_z = correctionPose.GetTranslation().z();
      
      SendMessage(ExternalInterface::MessageGameToEngine(std::move(msg)));
    }

    
    void UiGameController::SetDataPlatform(Util::Data::DataPlatform* dataPlatform) {
      _dataPlatform = dataPlatform;
    }
    
    Util::Data::DataPlatform* UiGameController::GetDataPlatform()
    {
      return _dataPlatform;
    }
    
    void UiGameController::SendMessage(const ExternalInterface::MessageGameToEngine& msg)
    {
      UserDeviceID_t devID = 1; // TODO: Should this be a RobotID_t?
      _msgHandler.SendMessage(devID, msg); 
    }
    


    void UiGameController::SendPing()
    {
      static ExternalInterface::Ping m;
      ExternalInterface::MessageGameToEngine message;
      message.Set_Ping(m);
      SendMessage(message);

      ++m.counter;
    }
    
    void UiGameController::SendDriveWheels(const f32 lwheel_speed_mmps, const f32 rwheel_speed_mmps)
    {
      ExternalInterface::DriveWheels m;
      m.lwheel_speed_mmps = lwheel_speed_mmps;
      m.rwheel_speed_mmps = rwheel_speed_mmps;
      ExternalInterface::MessageGameToEngine message;
      message.Set_DriveWheels(m);
      SendMessage(message);
    }
    
    void UiGameController::SendTurnInPlace(const f32 angle_rad)
    {
      ExternalInterface::TurnInPlace m;
      m.robotID = 1;
      m.angle_rad = angle_rad;
      m.isAbsolute = false;
      ExternalInterface::MessageGameToEngine message;
      message.Set_TurnInPlace(m);
      SendMessage(message);
    }

    void UiGameController::SendTurnInPlaceAtSpeed(const f32 speed_rad_per_sec, const f32 accel_rad_per_sec2)
    {
      ExternalInterface::TurnInPlaceAtSpeed m;
      m.robotID = 1;
      m.speed_rad_per_sec = speed_rad_per_sec;
      m.accel_rad_per_sec2 = accel_rad_per_sec2;
      ExternalInterface::MessageGameToEngine message;
      message.Set_TurnInPlaceAtSpeed(m);
      SendMessage(message);
    }
    
    void UiGameController::SendMoveHead(const f32 speed_rad_per_sec)
    {
      ExternalInterface::MoveHead m;
      m.speed_rad_per_sec = speed_rad_per_sec;
      ExternalInterface::MessageGameToEngine message;
      message.Set_MoveHead(m);
      SendMessage(message);
    }
    
    void UiGameController::SendMoveLift(const f32 speed_rad_per_sec)
    {
      ExternalInterface::MoveLift m;
      m.speed_rad_per_sec = speed_rad_per_sec;
      ExternalInterface::MessageGameToEngine message;
      message.Set_MoveLift(m);
      SendMessage(message);
    }
    
    void UiGameController::SendMoveHeadToAngle(const f32 rad, const f32 speed, const f32 accel, const f32 duration_sec)
    {
      ExternalInterface::SetHeadAngle m;
      m.angle_rad = rad;
      m.max_speed_rad_per_sec = speed;
      m.accel_rad_per_sec2 = accel;
      m.duration_sec = duration_sec;
      ExternalInterface::MessageGameToEngine message;
      message.Set_SetHeadAngle(m);
      SendMessage(message);
    }
    
    void UiGameController::SendMoveLiftToHeight(const f32 mm, const f32 speed, const f32 accel, const f32 duration_sec)
    {
      ExternalInterface::SetLiftHeight m;
      m.height_mm = mm;
      m.max_speed_rad_per_sec = speed;
      m.accel_rad_per_sec2 = accel;
      m.duration_sec = duration_sec;
      ExternalInterface::MessageGameToEngine message;
      message.Set_SetLiftHeight(m);
      SendMessage(message);
    }
    
    void UiGameController::SendStopAllMotors()
    {
      ExternalInterface::StopAllMotors m;
      ExternalInterface::MessageGameToEngine message;
      message.Set_StopAllMotors(m);
      SendMessage(message);
    }
    
    void UiGameController::SendImageRequest(ImageSendMode mode, u8 robotID)
    {
      ExternalInterface::ImageRequest m;
      m.robotID = robotID;
      m.mode = mode;
      ExternalInterface::MessageGameToEngine message;
      message.Set_ImageRequest(m);
      SendMessage(message);
    }
    
    void UiGameController::SendSetRobotImageSendMode(ImageSendMode mode, ImageResolution resolution)
    {
      ExternalInterface::SetRobotImageSendMode m;
      m.mode = mode;
      m.resolution = resolution;
      ExternalInterface::MessageGameToEngine message;
      message.Set_SetRobotImageSendMode(m);
      SendMessage(message);
    }
    
    void UiGameController::SendSaveImages(SaveMode_t mode, bool alsoSaveState)
    {
      ExternalInterface::SaveImages m;
      m.mode = mode;
      ExternalInterface::MessageGameToEngine message;
      message.Set_SaveImages(m);
      SendMessage(message);
      
      if(alsoSaveState) {
        ExternalInterface::SaveRobotState msgSaveState;
        msgSaveState.mode = mode;
        ExternalInterface::MessageGameToEngine messageWrapper;
        messageWrapper.Set_SaveRobotState(msgSaveState);
        SendMessage(messageWrapper);
      }
    }
    
    void UiGameController::SendEnableDisplay(bool on)
    {
      ExternalInterface::EnableDisplay m;
      m.enable = on;
      ExternalInterface::MessageGameToEngine message;
      message.Set_EnableDisplay(m);
      SendMessage(message);
   }
    
    void UiGameController::SendExecutePathToPose(const Pose3d& p,
                                                 PathMotionProfile motionProf,
                                                 const bool useManualSpeed)
    {
      ExternalInterface::GotoPose m;
      m.x_mm = p.GetTranslation().x();
      m.y_mm = p.GetTranslation().y();
      m.rad = p.GetRotationAngle<'Z'>().ToFloat();
      m.motionProf = motionProf;
      m.level = 0;
      m.useManualSpeed = useManualSpeed;
      ExternalInterface::MessageGameToEngine message;
      message.Set_GotoPose(m);
      SendMessage(message);
    }
    
    void UiGameController::SendGotoObject(const s32 objectID,
                                          const f32 distFromObjectOrigin_mm,
                                          PathMotionProfile motionProf,
                                          const bool useManualSpeed)
    {
      ExternalInterface::GotoObject msg;
      msg.objectID = objectID;
      msg.distanceFromObjectOrigin_mm = distFromObjectOrigin_mm;
      msg.motionProf = motionProf;
      msg.useManualSpeed = useManualSpeed;
      
      ExternalInterface::MessageGameToEngine msgWrapper;
      msgWrapper.Set_GotoObject(msg);
      SendMessage(msgWrapper);
    }
    
    void UiGameController::SendAlignWithObject(const s32 objectID,
                                               const f32 distFromMarker_mm,
                                               PathMotionProfile motionProf,
                                               const bool usePreDockPose,
                                               const bool useApproachAngle,
                                               const f32 approachAngle_rad,
                                               const bool useManualSpeed)
    {
      ExternalInterface::AlignWithObject msg;
      msg.objectID = objectID;
      msg.distanceFromMarker_mm = distFromMarker_mm;
      msg.motionProf = motionProf;
      msg.useApproachAngle = useApproachAngle;
      msg.approachAngle_rad = approachAngle_rad;
      msg.usePreDockPose = usePreDockPose;
      msg.useManualSpeed = useManualSpeed;
      
      ExternalInterface::MessageGameToEngine msgWrapper;
      msgWrapper.Set_AlignWithObject(msg);
      SendMessage(msgWrapper);
    }
    
    
    void UiGameController::SendPlaceObjectOnGroundSequence(const Pose3d& p,
                                                           PathMotionProfile motionProf,
                                                           const bool useExactRotation,
                                                           const bool useManualSpeed)
    {
      ExternalInterface::PlaceObjectOnGround m;
      m.x_mm = p.GetTranslation().x();
      m.y_mm = p.GetTranslation().y();
      m.level = 0;
      m.useManualSpeed = useManualSpeed;
      UnitQuaternion<f32> q(p.GetRotation().GetQuaternion());
      m.qw = q.w();
      m.qx = q.x();
      m.qy = q.y();
      m.qz = q.z();
      m.motionProf = motionProf;
      m.useExactRotation = useExactRotation;
      ExternalInterface::MessageGameToEngine message;
      message.Set_PlaceObjectOnGround(m);
      SendMessage(message);
    }
    
    
    void UiGameController::SendTrackToObject(const u32 objectID, bool headOnly)
    {
      ExternalInterface::TrackToObject m;
      m.robotID = 1;
      m.objectID = objectID;
      m.headOnly = headOnly;
      
      ExternalInterface::MessageGameToEngine message;
      message.Set_TrackToObject(m);
      SendMessage(message);
    }
    
    void UiGameController::SendTrackToFace(const u32 faceID, bool headOnly)
    {
      ExternalInterface::TrackToFace m;
      m.robotID = 1;
      m.faceID = faceID;
      m.headOnly = headOnly;
      
      ExternalInterface::MessageGameToEngine message;
      message.Set_TrackToFace(m);
      SendMessage(message);
    }
    
    
    void UiGameController::SendExecuteTestPlan()
    {
      ExternalInterface::ExecuteTestPlan m;
      ExternalInterface::MessageGameToEngine message;
      message.Set_ExecuteTestPlan(m);
      SendMessage(message);
    }
    
    void UiGameController::SendClearAllBlocks()
    {
      ExternalInterface::ClearAllBlocks m;
      m.robotID = 1;
      ExternalInterface::MessageGameToEngine message;
      message.Set_ClearAllBlocks(m);
      SendMessage(message);
    }
    
    void UiGameController::SendClearAllObjects()
    {
      ExternalInterface::ClearAllObjects m;
      m.robotID = 1;
      ExternalInterface::MessageGameToEngine message;
      message.Set_ClearAllObjects(m);
      SendMessage(message);
    }
    
    void UiGameController::SendSelectNextObject()
    {
      ExternalInterface::SelectNextObject m;
      ExternalInterface::MessageGameToEngine message;
      message.Set_SelectNextObject(m);
      SendMessage(message);
    }
    
    void UiGameController::SendPickupObject(const s32 objectID,
                                            PathMotionProfile motionProf,
                                            const bool usePreDockPose,
                                            const bool useApproachAngle,
                                            const f32 approachAngle_rad,
                                            const bool useManualSpeed)
    {
      ExternalInterface::PickupObject m;
      m.objectID = objectID,
      m.motionProf = motionProf;
      m.usePreDockPose = usePreDockPose;
      m.useApproachAngle = useApproachAngle;
      m.approachAngle_rad = approachAngle_rad;
      m.useManualSpeed = useManualSpeed;
      ExternalInterface::MessageGameToEngine message;
      message.Set_PickupObject(m);
      SendMessage(message);
    }
    
    
    void UiGameController::SendPlaceOnObject(const s32 objectID,
                                             PathMotionProfile motionProf,
                                             const bool usePreDockPose,
                                             const bool useApproachAngle,
                                             const f32 approachAngle_rad,
                                             const bool useManualSpeed)
    {
      ExternalInterface::PlaceOnObject m;
      m.objectID = objectID,
      m.motionProf = motionProf;
      m.usePreDockPose = usePreDockPose;
      m.useApproachAngle = useApproachAngle;
      m.approachAngle_rad = approachAngle_rad;
      m.useManualSpeed = useManualSpeed;
      ExternalInterface::MessageGameToEngine message;
      message.Set_PlaceOnObject(m);
      SendMessage(message);
    }
    
    void UiGameController::SendPlaceRelObject(const s32 objectID,
                                              PathMotionProfile motionProf,
                                              const bool usePreDockPose,
                                              const f32 placementOffsetX_mm,
                                              const bool useApproachAngle,
                                              const f32 approachAngle_rad,
                                              const bool useManualSpeed)
    {
      ExternalInterface::PlaceRelObject m;
      m.objectID = objectID,
      m.motionProf = motionProf;
      m.usePreDockPose = usePreDockPose;
      m.placementOffsetX_mm = placementOffsetX_mm;
      m.useApproachAngle = useApproachAngle;
      m.approachAngle_rad = approachAngle_rad;
      m.useManualSpeed = useManualSpeed;
      ExternalInterface::MessageGameToEngine message;
      message.Set_PlaceRelObject(m);
      SendMessage(message);
    }

    void UiGameController::SendPickupSelectedObject(PathMotionProfile motionProf,
                                                    const bool usePreDockPose,
                                                    const bool useApproachAngle,
                                                    const f32 approachAngle_rad,
                                                    const bool useManualSpeed)
    {
      SendPickupObject(-1,
                       motionProf,
                       usePreDockPose,
                       useApproachAngle,
                       approachAngle_rad,
                       useManualSpeed);
    }
    
    
    void UiGameController::SendPlaceOnSelectedObject(PathMotionProfile motionProf,
                                                     const bool usePreDockPose,
                                                     const bool useApproachAngle,
                                                     const f32 approachAngle_rad,
                                                     const bool useManualSpeed)
    {
      SendPlaceOnObject(-1,
                        motionProf,
                        usePreDockPose,
                        useApproachAngle,
                        approachAngle_rad,
                        useManualSpeed);
    }
    
    void UiGameController::SendPlaceRelSelectedObject(PathMotionProfile motionProf,
                                                      const bool usePreDockPose,
                                                      const f32 placementOffsetX_mm,
                                                      const bool useApproachAngle,
                                                      const f32 approachAngle_rad,
                                                      const bool useManualSpeed)
    {
      SendPlaceRelObject(-1,
                         motionProf,
                         usePreDockPose,
                         placementOffsetX_mm,
                         useApproachAngle,
                         approachAngle_rad,
                         useManualSpeed);
    }
    
    
    
    void UiGameController::SendRollObject(const s32 objectID,
                                          PathMotionProfile motionProf,
                                          const bool usePreDockPose,
                                          const bool useApproachAngle,
                                          const f32 approachAngle_rad,
                                          const bool useManualSpeed)
    {
      ExternalInterface::RollObject m;
      m.motionProf = motionProf;
      m.usePreDockPose = usePreDockPose;
      m.useApproachAngle = useApproachAngle,
      m.approachAngle_rad = approachAngle_rad,
      m.useManualSpeed = useManualSpeed;
      m.objectID = -1;
      ExternalInterface::MessageGameToEngine message;
      message.Set_RollObject(m);
      SendMessage(message);
    }
    
    void UiGameController::SendRollSelectedObject(PathMotionProfile motionProf,
                                                  const bool usePreDockPose,
                                                  const bool useApproachAngle,
                                                  const f32 approachAngle_rad,
                                                  const bool useManualSpeed)
    {
      SendRollObject(-1,
                     motionProf,
                     usePreDockPose,
                     useApproachAngle,
                     approachAngle_rad,
                     useManualSpeed);
    }
    
    void UiGameController::SendPopAWheelie(const s32 objectID,
                                           PathMotionProfile motionProf,
                                           const bool usePreDockPose,
                                           const bool useApproachAngle,
                                           const f32 approachAngle_rad,
                                           const bool useManualSpeed)
    {
      ExternalInterface::PopAWheelie m;
      m.motionProf = motionProf;
      m.usePreDockPose = usePreDockPose;
      m.useApproachAngle = useApproachAngle,
      m.approachAngle_rad = approachAngle_rad,
      m.useManualSpeed = useManualSpeed;
      m.objectID = -1;
      ExternalInterface::MessageGameToEngine message;
      message.Set_PopAWheelie(m);
      SendMessage(message);
    }
    
    void UiGameController::SendTraverseSelectedObject(PathMotionProfile motionProf,
                                                      const bool usePreDockPose,
                                                      const bool useManualSpeed)
    {
      ExternalInterface::TraverseObject m;
      m.motionProf = motionProf;
      m.usePreDockPose = usePreDockPose;
      m.useManualSpeed = useManualSpeed;
      ExternalInterface::MessageGameToEngine message;
      message.Set_TraverseObject(m);
      SendMessage(message);
    }

    void UiGameController::SendMountCharger(s32 objectID,
                                            PathMotionProfile motionProf,
                                            const bool usePreDockPose,
                                            const bool useManualSpeed)
    {
      ExternalInterface::MountCharger m;
      m.objectID = objectID;
      m.motionProf = motionProf;
      m.usePreDockPose = usePreDockPose;
      m.useManualSpeed = useManualSpeed;
      ExternalInterface::MessageGameToEngine message;
      message.Set_MountCharger(m);
      SendMessage(message);
    }

    
    void UiGameController::SendMountSelectedCharger(PathMotionProfile motionProf,
                                                    const bool usePreDockPose,
                                                    const bool useManualSpeed)
    {
      SendMountCharger(-1, motionProf, usePreDockPose, useManualSpeed);
    }
    
    BehaviorType UiGameController::GetBehaviorType(const std::string& behaviorName) const
    {
      const BehaviorType behaviorType = BehaviorTypeFromString(behaviorName);
      return (behaviorType != BehaviorType::Count) ? behaviorType : BehaviorType::NoneBehavior;
    }
    
    void UiGameController::SendAbortPath()
    {
      ExternalInterface::AbortPath m;
      ExternalInterface::MessageGameToEngine message;
      message.Set_AbortPath(m);
      SendMessage(message);
    }
    
    void UiGameController::SendAbortAll()
    {
      ExternalInterface::AbortAll m;
      ExternalInterface::MessageGameToEngine message;
      message.Set_AbortAll(m);
      SendMessage(message);
    }
    
    void UiGameController::SendDrawPoseMarker(const Pose3d& p)
    {
      ExternalInterface::DrawPoseMarker m;
      m.x_mm = p.GetTranslation().x();
      m.y_mm = p.GetTranslation().y();
      m.rad = p.GetRotationAngle<'Z'>().ToFloat();
      m.level = 0;
      ExternalInterface::MessageGameToEngine message;
      message.Set_DrawPoseMarker(m);
      SendMessage(message);
    }
    
    void UiGameController::SendErasePoseMarker()
    {
      ExternalInterface::ErasePoseMarker m;
      ExternalInterface::MessageGameToEngine message;
      message.Set_ErasePoseMarker(m);
      SendMessage(message);
    }

    void UiGameController::SendWheelControllerGains(const f32 kpLeft, const f32 kiLeft, const f32 maxErrorSumLeft,
                                  const f32 kpRight, const f32 kiRight, const f32 maxErrorSumRight)
    {
      ExternalInterface::SetWheelControllerGains m;
      m.kpLeft = kpLeft;
      m.kiLeft = kiLeft;
      m.maxIntegralErrorLeft = maxErrorSumLeft;
      m.kpRight = kpRight;
      m.kiRight = kiRight;
      m.maxIntegralErrorRight = maxErrorSumRight;
      ExternalInterface::MessageGameToEngine message;
      message.Set_SetWheelControllerGains(m);
      SendMessage(message);
    }

    
    void UiGameController::SendHeadControllerGains(const f32 kp, const f32 ki, const f32 kd, const f32 maxErrorSum)
    {
      ExternalInterface::SetHeadControllerGains m;
      m.kp = kp;
      m.ki = ki;
      m.kd = kd;
      m.maxIntegralError = maxErrorSum;
      ExternalInterface::MessageGameToEngine message;
      message.Set_SetHeadControllerGains(m);
      SendMessage(message);
    }
    
    void UiGameController::SendLiftControllerGains(const f32 kp, const f32 ki, const f32 kd, const f32 maxErrorSum)
    {
      ExternalInterface::SetLiftControllerGains m;
      m.kp = kp;
      m.ki = ki;
      m.kd = kd;
      m.maxIntegralError = maxErrorSum;
      ExternalInterface::MessageGameToEngine message;
      message.Set_SetLiftControllerGains(m);
      SendMessage(message);
    }
    
    void UiGameController::SendSteeringControllerGains(const f32 k1, const f32 k2)
    {
      ExternalInterface::SetSteeringControllerGains m;
      m.k1 = k1;
      m.k2 = k2;
      ExternalInterface::MessageGameToEngine message;
      message.Set_SetSteeringControllerGains(m);
      SendMessage(message);
    }
    
    void UiGameController::SendSetRobotVolume(const f32 volume)
    {
      ExternalInterface::SetRobotVolume m;
      m.volume = volume;
      ExternalInterface::MessageGameToEngine message;
      message.Set_SetRobotVolume(m);
      SendMessage(message);
    }
    
    void UiGameController::SendStartTestMode(TestMode mode, s32 p1, s32 p2, s32 p3)
    {
      ExternalInterface::StartTestMode m;
      m.robotID = 1;
      m.mode = mode;
      m.p1 = p1;
      m.p2 = p2;
      m.p3 = p3;
      ExternalInterface::MessageGameToEngine message;
      message.Set_StartTestMode(m);
      SendMessage(message);
    }
    
    void UiGameController::SendIMURequest(u32 length_ms)
    {
      ExternalInterface::IMURequest m;
      m.length_ms = length_ms;
      ExternalInterface::MessageGameToEngine message;
      message.Set_IMURequest(m);
      SendMessage(message);
    }

    void UiGameController::SendEnableRobotPickupParalysis(bool enable)
    {
      ExternalInterface::EnableRobotPickupParalysis m;
      m.enable = enable;
      ExternalInterface::MessageGameToEngine message;
      message.Set_EnableRobotPickupParalysis(m);
      SendMessage(message);
    }
    
    void UiGameController::SendAnimation(const char* animName, u32 numLoops)
    {
      static double lastSendTime_sec = -1e6;
      
      // Don't send repeated animation commands within a half second
      if(_supervisor.getTime() > lastSendTime_sec + 0.5f)
      {
        PRINT_NAMED_INFO("SendAnimation", "sending %s", animName);
        ExternalInterface::PlayAnimation m;
        //m.animationID = animId;
        m.robotID = 1;
        m.animationName = animName;
        m.numLoops = numLoops;
        ExternalInterface::MessageGameToEngine message;
        message.Set_PlayAnimation(m);
        SendMessage(message);
        lastSendTime_sec = _supervisor.getTime();
      } else {
        PRINT_NAMED_INFO("SendAnimation", "Ignoring duplicate SendAnimation keystroke.");
      }
      
    }

    void UiGameController::SendReplayLastAnimation()
    {
      ExternalInterface::ReplayLastAnimation m;
      m.numLoops = 1;
      m.robotID = 1;
      ExternalInterface::MessageGameToEngine message;
      message.Set_ReplayLastAnimation(m);
      SendMessage(message);
    }

    void UiGameController::SendReadAnimationFile()
    {
      ExternalInterface::ReadAnimationFile m;
      ExternalInterface::MessageGameToEngine message;
      message.Set_ReadAnimationFile(m);
      SendMessage(message);
    }
    
    void UiGameController::SendSetIdleAnimation(const std::string &animName) {
      ExternalInterface::SetIdleAnimation msg;
      msg.robotID = 1;
      msg.animationName = animName;
      ExternalInterface::MessageGameToEngine message;
      message.Set_SetIdleAnimation(msg);
      SendMessage(message);
    }
    
    void UiGameController::SendQueuePlayAnimAction(const std::string &animName, u32 numLoops, QueueActionPosition pos) {
      ExternalInterface::QueueSingleAction msg;
      msg.robotID = 1;
      msg.inSlot = 1;
      msg.position = pos;
      msg.action.Set_playAnimation(ExternalInterface::PlayAnimation(msg.robotID, numLoops, animName));

      ExternalInterface::MessageGameToEngine message;
      message.Set_QueueSingleAction(msg);
      SendMessage(message);
    }
    
    void UiGameController::SendCancelAction() {
      ExternalInterface::CancelAction msg;
      msg.actionType = RobotActionType::UNKNOWN;
      msg.robotID = 1;
      ExternalInterface::MessageGameToEngine message;
      message.Set_CancelAction(msg);
      SendMessage(message);
    }
    
    void UiGameController::SendEnableVisionMode(VisionMode mode, bool enable)
    {
      ExternalInterface::EnableVisionMode m;
      m.mode = mode;
      m.enable = enable;
      SendMessage(ExternalInterface::MessageGameToEngine(std::move(m)));
    }

    void UiGameController::SendForceAddRobot()
    {
      if (_root) {
        ExternalInterface::ForceAddRobot msg;
        msg.isSimulated = false;
        msg.ipAddress.fill('\0'); // ensure null-termination after copy below
        
        webots::Field* ipField = _root->getField("forceAddIP");
        webots::Field* idField = _root->getField("forceAddID");
        
        if(ipField != nullptr && idField != nullptr) {
          const std::string ipStr = ipField->getSFString();
          std::copy(ipStr.begin(), ipStr.end(), msg.ipAddress.data());
          
          msg.robotID = static_cast<u8>(idField->getSFInt32());

          PRINT_NAMED_INFO("UiGameController.SendForceAddRobot", "Sending message to force-add robot %d at %s", msg.robotID, ipStr.c_str());
          ExternalInterface::MessageGameToEngine message;
          message.Set_ForceAddRobot(msg);
          SendMessage(message);
        } else {
          PRINT_NAMED_INFO("UiGameController.SendForceAddRobot", "ERROR: No 'forceAddIP' / 'forceAddID' field(s) found!");
        }
      }
    }
    
    void UiGameController::QuitWebots(s32 status)
    {
      PRINT_NAMED_INFO("UiGameController.QuitWebots.Result", "%d", status);
      _supervisor.simulationQuit(status);
    }
    
    void UiGameController::QuitController(s32 status)
    {
      PRINT_NAMED_INFO("UiGameController.QuitController.Result", "%d", status);
      exit(status);
    }
    
    s32 UiGameController::GetStepTimeMS() const
    {
      return _stepTimeMS;
    }
    
    webots::Supervisor* UiGameController::GetSupervisor() const
    {
      return &_supervisor;
    }
    
    const Pose3d& UiGameController::GetRobotPose() const
    {
      return _robotPose;
    }
    
    const Pose3d& UiGameController::GetRobotPoseActual() const
    {
      return _robotPoseActual;
    }
    
    f32 UiGameController::GetRobotHeadAngle_rad() const
    {
      return _robotStateMsg.headAngle_rad;
    }
    
    f32 UiGameController::GetLiftHeight_mm() const
    {
      return _robotStateMsg.liftHeight_mm;
    }
    
    void UiGameController::GetWheelSpeeds_mmps(f32& left, f32& right) const
    {
      left = _robotStateMsg.leftWheelSpeed_mmps;
      right = _robotStateMsg.rightWheelSpeed_mmps;
    }
    
    s32 UiGameController::GetCarryingObjectID() const
    {
      return _robotStateMsg.carryingObjectID;
    }
    
    s32 UiGameController::GetCarryingObjectOnTopID() const
    {
      return _robotStateMsg.carryingObjectOnTopID;
    }
    
    bool UiGameController::IsRobotStatus(RobotStatusFlag mask) const
    {
      return _robotStateMsg.status & (uint16_t)mask;
    }
    
    std::vector<s32> UiGameController::GetAllObjectIDs() const
    {
      std::vector<s32> v;
      for(auto it = _objectIDToPoseMap.begin(); it != _objectIDToPoseMap.end(); ++it) {
        v.push_back(it->first);
      }
      return v;
    }
    
    std::vector<s32> UiGameController::GetAllObjectIDsByFamily(ObjectFamily family) const
    {
      std::vector<s32> v;
      auto typeToIDMapIter = _objectFamilyToTypeToIDMap.find(family);
      if (typeToIDMapIter != _objectFamilyToTypeToIDMap.end()) {
        for (auto it = typeToIDMapIter->second.begin(); it != typeToIDMapIter->second.end(); ++it) {
          v.insert(v.end(), it->second.begin(), it->second.end());
        }
      }
      return v;
    }
    
    std::vector<s32> UiGameController::GetAllObjectIDsByFamilyAndType(ObjectFamily family, ObjectType type) const
    {
      std::vector<s32> v;
      auto typeToIDMapIter = _objectFamilyToTypeToIDMap.find(family);
      if (typeToIDMapIter != _objectFamilyToTypeToIDMap.end()) {
        auto it = typeToIDMapIter->second.find(type);
        if (it != typeToIDMapIter->second.end()) {
          v.insert(v.end(), it->second.begin(), it->second.end());
        }
      }
      return v;
    }
    
    Result UiGameController::GetObjectFamily(s32 objectID, ObjectFamily& family) const
    {
      auto it = _objectIDToFamilyTypeMap.find(objectID);
      if (it != _objectIDToFamilyTypeMap.end()) {
        family = it->second.first;
        return RESULT_OK;
      }
      return RESULT_FAIL;
    }
    
    Result UiGameController::GetObjectType(s32 objectID, ObjectType& type) const
    {
      auto it = _objectIDToFamilyTypeMap.find(objectID);
      if (it != _objectIDToFamilyTypeMap.end()) {
        type = it->second.second;
        return RESULT_OK;
      }
      return RESULT_FAIL;
    }
    
    Result UiGameController::GetObjectPose(s32 objectID, Pose3d& pose) const
    {
      auto it = _objectIDToPoseMap.find(objectID);
      if (it != _objectIDToPoseMap.end()) {
        pose = it->second;
        return RESULT_OK;
      }
      return RESULT_FAIL;
    }
    
    u32 UiGameController::GetNumObjectsInFamily(ObjectFamily family) const
    {
      u32 numObjects = 0;
      auto typeToIDMapIter = _objectFamilyToTypeToIDMap.find(family);
      if (typeToIDMapIter != _objectFamilyToTypeToIDMap.end()) {
        for (auto it = typeToIDMapIter->second.begin(); it != typeToIDMapIter->second.end(); ++it) {
          numObjects += it->second.size();
        }
      }
      return numObjects;
    }
    
    u32 UiGameController::GetNumObjectsInFamilyAndType(ObjectFamily family, ObjectType type) const
    {
      auto typeToIDMapIter = _objectFamilyToTypeToIDMap.find(family);
      if (typeToIDMapIter != _objectFamilyToTypeToIDMap.end()) {
        auto it = typeToIDMapIter->second.find(type);
        return (u32)it->second.size();
      }
      return 0;
    }
    
    u32 UiGameController::GetNumObjects() const
    {
      return (u32)_objectIDToPoseMap.size();
    }
    
    void UiGameController::ClearAllKnownObjects()
    {
      _objectIDToFamilyTypeMap.clear();
      _objectFamilyToTypeToIDMap.clear();
      _objectIDToPoseMap.clear();
    }
    
    const std::map<s32, Pose3d>& UiGameController::GetObjectPoseMap() {
      return _objectIDToPoseMap;
    }
    
    const UiGameController::ObservedObject& UiGameController::GetLastObservedObject() const
    {
      return _lastObservedObject;
    }
    
    const Vision::TrackedFace::ID_t UiGameController::GetLastObservedFaceID() const
    {
      return _lastObservedFaceID;
    }
    
    const std::unordered_set<std::string>& UiGameController::GetAvailableAnimations() const
    {
      return _availableAnimations;
    }
    
    u32 UiGameController::GetNumAvailableAnimations() const
    {
      return (u32)_availableAnimations.size();
    }
    
    bool UiGameController::IsAvailableAnimation(std::string anim) const
    {
      return _availableAnimations.find(anim) != _availableAnimations.end();
    }
    
    void UiGameController::SetActualRobotPose(const Pose3d& newPose)
    {
      webots::Field* rotField = _robotNode->getField("rotation");
      assert(rotField != nullptr);
      
      webots::Field* transField = _robotNode->getField("translation");
      assert(transField != nullptr);
      
      const RotationVector3d Rvec = newPose.GetRotationVector();
      const double rotation[4] = {
        Rvec.GetAxis().x(), Rvec.GetAxis().y(), Rvec.GetAxis().z(),
        Rvec.GetAngle().ToFloat()
      };
      rotField->setSFRotation(rotation);
      
      const double translation[3] = {
        MM_TO_M(newPose.GetTranslation().x()),
        MM_TO_M(newPose.GetTranslation().y()),
        MM_TO_M(newPose.GetTranslation().z())
      };
      transField->setSFVec3f(translation);
      
    }
    
    void SetActualObjectPose(const std::string& name, const Pose3d& newPose)
    {
      // TODO: Implement!
    }

  } // namespace Cozmo
} // namespace Anki

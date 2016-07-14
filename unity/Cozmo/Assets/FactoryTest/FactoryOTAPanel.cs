﻿using UnityEngine;
using System.Collections;

public class FactoryOTAPanel : MonoBehaviour {

  public System.Action OnOTAStarted;
  public System.Action OnOTAFinished;

  [SerializeField]
  private UnityEngine.UI.Text _OTAStatus;

  [SerializeField]
  private UnityEngine.UI.Button _CloseButton;

  void Start() {
    if (OnOTAStarted != null) {
      OnOTAStarted();
    }

    RobotEngineManager.Instance.ConnectedToClient += HandleConnectedToClient;
    RobotEngineManager.Instance.DisconnectedFromClient += HandleDisconnectedFromClient;
    RobotEngineManager.Instance.RobotConnected += HandleRobotConnected;
    RobotEngineManager.Instance.RobotDisconnected += HandleRobotDisconnected;
    RobotEngineManager.Instance.OnFirmwareUpdateProgress += OnFirmwareUpdateProgress;
    RobotEngineManager.Instance.OnFirmwareUpdateComplete += OnFirmwareUpdateComplete;

    if (!RobotEngineManager.Instance.IsConnected) {
      _OTAStatus.text = "Connecting to engine";
      RobotEngineManager.Instance.Connect(FactoryIntroManager.kEngineIP);
    } 
    else {
      HandleConnectedToClient(null);
    }

    _CloseButton.onClick.AddListener(HandleCloseButton);
  }

  private void HandleConnectedToClient(string connectionIdentifier) {
    _OTAStatus.text = "Connecting to robot";
    RobotEngineManager.Instance.ConnectToRobot(FactoryIntroManager.kRobotID, FactoryIntroManager.kRobotIP, false);
  }

  private void HandleDisconnectedFromClient(DisconnectionReason obj) {
    _OTAStatus.text = "Disconnected from engine";
  }

  private void HandleRobotConnected(int robotID) {
    _OTAStatus.text = "Sending update firmware message";
    RobotEngineManager.Instance.UpdateFirmware(0);
  }

  private void HandleRobotDisconnected(int robotID) {
    _OTAStatus.text = "Disconnected from robot";
  }

  private void OnFirmwareUpdateProgress(Anki.Cozmo.ExternalInterface.FirmwareUpdateProgress message) {
    _OTAStatus.text = "InProgress: Robot " + message.robotID + " Stage: " + message.stage + ":" + message.subStage + " " + message.percentComplete + "%"
    + "\nFwSig = " + message.fwSig;
  }

  private void OnFirmwareUpdateComplete(Anki.Cozmo.ExternalInterface.FirmwareUpdateComplete message) {
    _OTAStatus.text = "Complete: Robot " + message.robotID + " Result: " + message.result + "\nFwSig = " + message.fwSig;
  }

  private void HandleCloseButton() {
    RobotEngineManager.Instance.DisconnectFromRobot(FactoryIntroManager.kRobotID);

    RobotEngineManager.Instance.ConnectedToClient -= HandleConnectedToClient;
    RobotEngineManager.Instance.DisconnectedFromClient -= HandleDisconnectedFromClient;
    RobotEngineManager.Instance.RobotConnected -= HandleRobotConnected;
    RobotEngineManager.Instance.RobotDisconnected -= HandleRobotDisconnected;
    RobotEngineManager.Instance.OnFirmwareUpdateProgress -= OnFirmwareUpdateProgress;
    RobotEngineManager.Instance.OnFirmwareUpdateComplete -= OnFirmwareUpdateComplete;

    if (OnOTAFinished != null) {
      OnOTAFinished();
    }

    GameObject.Destroy(gameObject);
  }
}

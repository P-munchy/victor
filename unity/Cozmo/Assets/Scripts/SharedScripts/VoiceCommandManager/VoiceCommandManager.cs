﻿using UnityEngine;
using Cozmo.UI;
using System;
using DataPersistence;
using Cozmo.ConnectionFlow;

using Anki.Cozmo.Audio;
using Anki.Cozmo.Audio.VolumeParameters;
using System.Collections.Generic;

namespace Anki.Cozmo.VoiceCommand {

  public enum VoiceCommandEnabledState {
    Unknown,
    Enabled,
    Disabled
  };

  public class VoiceCommandManager {

    public delegate void UserResponseToPromptHandler(bool positiveResponse);
    public event UserResponseToPromptHandler OnUserPromptResponse;

    private static VoiceCommandManager _Instance;

    public static void CreateInstance() {
      _Instance = new VoiceCommandManager();
    }

    public static VoiceCommandManager Instance {
      get {
        if (_Instance == null) {
          string stackTrace = System.Environment.StackTrace;
          DAS.Error("VoiceCommandManager.NullInstance", "Do not access VoiceCommandManager until start");
          DAS.Debug("VoiceCommandManager.NullInstance.StackTrace", DASUtil.FormatStackTrace(stackTrace));
          HockeyApp.ReportStackTrace("VoiceCommandManager.NullInstance", stackTrace);
        }
        return _Instance;
      }
      private set {
        if (_Instance != null) {
          DAS.Error("VoiceCommandManager.DuplicateInstance", "VoiceCommandManager Instance already exists");
        }
        _Instance = value;
      }
    }

    public event Action<RespondingToCommandStart> RespondingToCommandStartCallback;
    public event Action<RespondingToCommandEnd> RespondingToCommandEndCallback;
    public event Action<RespondingToCommand> RespondingToCommand;
    public event Action<Anki.Cozmo.VoiceCommand.StateData> StateDataCallback;

    private float _VolumeLevelToRestore = -1.0f;

    private const float kReducedVolumeLevel = 0.25f;

    public static void SendVoiceCommandEvent<T>(T eventSubType) {
      VoiceCommandEventUnion initializedUnion = Singleton<VoiceCommandEventUnion>.Instance.Initialize(eventSubType);
      VoiceCommandEvent initializedEvent = Singleton<VoiceCommandEvent>.Instance.Initialize(initializedUnion);
      RobotEngineManager.Instance.Message.Initialize(initializedEvent);
      RobotEngineManager.Instance.SendMessage();
    }

    private VoiceCommandManager() {
      RobotEngineManager.Instance.AddCallback<Anki.Cozmo.VoiceCommand.VoiceCommandEvent>(HandleVoiceCommandEvent);

      RespondingToCommandStartCallback += HandleRespondCommandStart;
      RespondingToCommandEndCallback += HandleRespondCommandEnd;
    }

    public void Init() {
      VoiceCommandEnabledState enabledState = DataPersistenceManager.Instance.Data.DefaultProfile.VoiceCommandEnabledState;
      if (enabledState == VoiceCommandEnabledState.Enabled) {
        SetVoiceCommandEnabled(true);
      }
    }

    private void HandleVoiceCommandEvent(Anki.Cozmo.VoiceCommand.VoiceCommandEvent voiceCommandEvent) {
      VoiceCommandEventUnion eventData = voiceCommandEvent.voiceCommandEvent;
      VoiceCommandEventUnion.Tag eventType = eventData.GetTag();
      switch (eventType) {
      case VoiceCommandEventUnion.Tag.respondingToCommandStart: {
          if (RespondingToCommandStartCallback != null) {
            RespondingToCommandStartCallback(eventData.respondingToCommandStart);
          }
          break;
        }
      case VoiceCommandEventUnion.Tag.respondingToCommandEnd: {
          if (RespondingToCommandEndCallback != null) {
            RespondingToCommandEndCallback(eventData.respondingToCommandEnd);
          }
          break;
        }
      case VoiceCommandEventUnion.Tag.respondingToCommand: {
          if (RespondingToCommand != null) {
            RespondingToCommand(eventData.respondingToCommand);
          }
          break;
        }
      case VoiceCommandEventUnion.Tag.stateData: {
          if (StateDataCallback != null) {
            StateDataCallback(eventData.stateData);
          }
          break;
        }
      case VoiceCommandEventUnion.Tag.responseToPrompt: {
          if (OnUserPromptResponse != null) {
            OnUserPromptResponse(eventData.responseToPrompt.positiveResponse);
          }
          break;
        }
      default: {
          break;
        }
      }
    }

    private void HandleRespondCommandStart(RespondingToCommandStart eventData) {
      DAS.Debug("VoiceCommandManager.RespondingToCommandStartCallback", eventData.voiceCommandType.ToString());

      if (eventData.voiceCommandType == VoiceCommandType.HeyCozmo) {
        LowerVolumeIfNeeded();
      }
    }

    private void HandleRespondCommandEnd(RespondingToCommandEnd eventData) {
      DAS.Debug("VoiceCommandManager.RespondingToCommandEndCallback", eventData.voiceCommandType.ToString());

      if (eventData.voiceCommandType == VoiceCommandType.HeyCozmo) {
        RestoreVolumeIfNeeded();
      }
    }

    private void LowerVolumeIfNeeded() {
      Dictionary<VolumeType, float> currentVolumePrefs = DataPersistenceManager.Instance.Data.DeviceSettings.VolumePreferences;

      if (!currentVolumePrefs.TryGetValue(VolumeType.Music, out _VolumeLevelToRestore) || _VolumeLevelToRestore <= kReducedVolumeLevel) {
        _VolumeLevelToRestore = -1.0f;
      }

      if (_VolumeLevelToRestore > 0.0f) {
        GameAudioClient.SetVolumeValue(VolumeType.Music, kReducedVolumeLevel);
      }
    }

    private void RestoreVolumeIfNeeded() {
      if (_VolumeLevelToRestore > 0.0f) {
        GameAudioClient.SetVolumeValue(VolumeType.Music, _VolumeLevelToRestore);
        _VolumeLevelToRestore = -1.0f;
      }
    }

    public static void SetVoiceCommandEnabled(bool enabledStatus) {
      SendVoiceCommandEvent<ChangeEnabledStatus>(Singleton<ChangeEnabledStatus>.Instance.Initialize(enabledStatus));

      if (enabledStatus) {
        DataPersistenceManager.Instance.Data.DefaultProfile.VoiceCommandEnabledState = VoiceCommandEnabledState.Enabled;
      } else {
        DataPersistenceManager.Instance.Data.DefaultProfile.VoiceCommandEnabledState = VoiceCommandEnabledState.Disabled;
      }
      DataPersistenceManager.Instance.Save();
    }
  }
}

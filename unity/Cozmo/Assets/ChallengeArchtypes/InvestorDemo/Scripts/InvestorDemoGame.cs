﻿using UnityEngine;
using System.Collections;
using System.Collections.Generic;
using UnityEngine.UI;

namespace InvestorDemo {

  public class InvestorDemoGame : GameBase {

    [SerializeField]
    private InvestorDemoPanel _GamePanelPrefab;

    private InvestorDemoPanel _GamePanel;

    private InvestorDemoConfig _DemoConfig;

    protected override void Initialize(MinigameConfigBase minigameConfig) {
      _DemoConfig = minigameConfig as InvestorDemoConfig;
      if (_DemoConfig == null) {
        DAS.Error(this, "Failed to load config InvestorDemoConfig!");
        return;
      }
      InitializeMinigameObjects();

      // Enable idle vision mode, which means "turn off" all vision processing.
      // We will rely on each demo behavior chooser to enable its required 
      // vision modes.
      CurrentRobot.SetVisionMode(Anki.Cozmo.VisionMode.Idle, true);
    }

    protected override void InitializeView(Cozmo.MinigameWidgets.SharedMinigameView newView, ChallengeData data) {
      newView.ShowQuitButton();
    }

    protected void InitializeMinigameObjects() {
      CurrentRobot.SetRobotVolume(1.0f);
      Anki.Cozmo.Audio.GameAudioClient.SetVolumeValue(Anki.Cozmo.Audio.VolumeParameters.VolumeType.Music, 1.0f);
      Anki.Cozmo.Audio.GameAudioClient.SetVolumeValue(Anki.Cozmo.Audio.VolumeParameters.VolumeType.SFX, 1.0f);

      _GamePanel = UIManager.OpenView(_GamePanelPrefab);

      CurrentRobot.SetBehaviorSystem(true);

      if (_DemoConfig.UseSequence) {
        CurrentRobot.ActivateBehaviorChooser(Anki.Cozmo.BehaviorChooserType.Selection);
        CurrentRobot.ExecuteBehavior(Anki.Cozmo.BehaviorType.NoneBehavior);

        // Waiting until the investor demo buttons are registered to ObjectTagRegistry before starting
        // the sequence.
        StartCoroutine(StartSequence());
      }
      else {
        CurrentRobot.ActivateBehaviorChooser(_DemoConfig.BehaviorChooser);
      }
    }

    protected override void Update() {
      base.Update();

      if (_DemoConfig.UseSequence) {
        ScriptedSequences.ScriptedSequence sequence = ScriptedSequences.ScriptedSequenceManager.Instance.CurrentSequence;
        if (sequence != null) {
          List<ScriptedSequences.ScriptedSequenceNode> activeNodes = sequence.GetActiveNodes();
          if (activeNodes.Count > 0) {
            _GamePanel.SetActionText(activeNodes[0].Name);
          }
        }
      }
      else {
        _GamePanel.SetActionText(_DemoConfig.BehaviorChooser.ToString());
      }
    }

    private IEnumerator StartSequence() {
      yield return new WaitForEndOfFrame();
      ScriptedSequences.ISimpleAsyncToken token = ScriptedSequences.ScriptedSequenceManager.Instance.ActivateSequence(_DemoConfig.SequenceName);
      token.Ready(HandleSequenceComplete);
    }

    private void HandleSequenceComplete(ScriptedSequences.ISimpleAsyncToken token) {
      RaiseMiniGameWin();
    }

    protected override void CleanUpOnDestroy() {
      if (_DemoConfig.UseSequence) {
        ScriptedSequences.ScriptedSequence sequence = ScriptedSequences.ScriptedSequenceManager.Instance.Sequences.Find(s => s.Name == _DemoConfig.SequenceName);
        if (sequence != null) {
          sequence.ResetSequence();
        }
      }

      if (_GamePanel != null) {
        UIManager.CloseViewImmediately(_GamePanel);
      }
    }
  }

}

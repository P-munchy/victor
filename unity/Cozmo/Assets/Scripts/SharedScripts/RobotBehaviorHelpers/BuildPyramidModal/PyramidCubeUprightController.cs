﻿using UnityEngine;
using System.Collections;
using Anki.Cozmo.ExternalInterface;

namespace Cozmo.Upgrades {

  // Lives on SparksDetailModal prefab as a second component
  public class PyramidCubeUprightController : MonoBehaviour {
    [SerializeField]
    private PyramidCubeUprightModal _PyramidCubeUprightModalPrefab;
    private PyramidCubeUprightModal _PyramidCubeUprightModalInstance;

    [SerializeField]
    private float _ReopenModalCooldown_sec;

    private float _ReopenModalCooldownStartTimestamp;
    private bool _AreCubesUpright = true;

    private void Start() {
      RobotEngineManager.Instance.AddCallback<BuildPyramidPreReqsChanged>(HandleBuildPyramidPreReqsChanged);
      RobotEngineManager.Instance.AddCallback<HardSparkEndedByEngine>(HandleSparkComplete);
      _ReopenModalCooldownStartTimestamp = -1;
    }

    private void OnDestroy() {
      CloseCubeShouldBeUprightModal();
      RobotEngineManager.Instance.RemoveCallback<BuildPyramidPreReqsChanged>(HandleBuildPyramidPreReqsChanged);
      RobotEngineManager.Instance.RemoveCallback<HardSparkEndedByEngine>(HandleSparkComplete);
    }

    private void OnApplicationPause(bool pauseStatus) {
      if (pauseStatus) {
        CloseCubeShouldBeUprightModal();
        _AreCubesUpright = true; // reset this flag since the spark ending won't necessarily set this.
      }
    }

    private void Update() {
      if (!_ReopenModalCooldownStartTimestamp.IsNear(-1, float.Epsilon)
          && (_ReopenModalCooldown_sec <= (Time.time - _ReopenModalCooldownStartTimestamp)
          && (!_AreCubesUpright))) {
        OpenCubeShouldBeUprightModal();
      }
    }

    private void HandleBuildPyramidPreReqsChanged(BuildPyramidPreReqsChanged message) {
      DAS.Debug("PyramidCubeUprightController.HandleBuildPyramidPreReqsChanged", "cubes upright: " + message.areCubesUpright);
      if (!message.areCubesUpright) {
        if (_ReopenModalCooldownStartTimestamp.IsNear(-1, float.Epsilon)) {
          OpenCubeShouldBeUprightModal();
        }
        _AreCubesUpright = false;
      }
      else {
        CloseCubeShouldBeUprightModal();
        _AreCubesUpright = true;
      }
    }

    private void HandleCubeUprightModalClosedByUser() {
      _ReopenModalCooldownStartTimestamp = Time.time;
    }

    private void OpenCubeShouldBeUprightModal() {
      DAS.Debug("PyramidCubeUprightController.OpenCubeShouldBeUprightModal", "Opening modal");
      _ReopenModalCooldownStartTimestamp = -1;
      UIManager.OpenModal(_PyramidCubeUprightModalPrefab,
                          new UI.ModalPriorityData(UI.ModalPriorityLayer.High, 0, UI.LowPriorityModalAction.CancelSelf, UI.HighPriorityModalAction.Stack),
                          HandleCubeShouldBeUprightModalCreated);
    }

    private void HandleCubeShouldBeUprightModalCreated(Cozmo.UI.BaseModal newModal) {
      _PyramidCubeUprightModalInstance = (PyramidCubeUprightModal)newModal;
      _PyramidCubeUprightModalInstance.ModalClosedWithCloseButtonOrOutsideAnimationFinished += HandleCubeUprightModalClosedByUser;
    }

    private void HandleSparkComplete(HardSparkEndedByEngine message) {
      CloseCubeShouldBeUprightModal();
    }

    private void CloseCubeShouldBeUprightModal() {
      if (_PyramidCubeUprightModalInstance != null) {
        _PyramidCubeUprightModalInstance.ModalClosedWithCloseButtonOrOutsideAnimationFinished -= HandleCubeUprightModalClosedByUser;
        UIManager.CloseModal(_PyramidCubeUprightModalInstance);
      }
    }
  }
}

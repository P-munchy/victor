﻿using UnityEngine;
using UnityEngine.UI;

namespace Anki.Debug {
  public class VarUICheckbox : ConsoleVarLine {

    [SerializeField]
    private Toggle _Checkbox;

    public override void Init(DebugConsoleData.DebugConsoleVarData singleVar, GameObject go) {
      base.Init(singleVar, go);

      _Checkbox.isOn = singleVar.ValueAsUInt64 != 0;

      _Checkbox.onValueChanged.AddListener(HandleValueChanged);
    }

    private void HandleValueChanged(bool val) {
      // If the game is fine with this value it will send a VerifyDebugConsoleVarMessage
      // otherwise it will send another Set to a valid value.
      // Empty string just means toggle.
      if (_VarData.UnityObject != null) {
        SetUnityValue(val);
        _VarData.ValueAsUInt64 = val ? 1ul : 0;
      }
      else {
        RobotEngineManager.Instance.SetDebugConsoleVar(_VarData.VarName, "");
      }
    }
  }
}

﻿using UnityEngine;
using System;
using System.Collections;
using System.Collections.Generic;
using Anki.Cozmo;
using G2U = Anki.Cozmo.ExternalInterface;
using U2G = Anki.Cozmo.ExternalInterface;

/// <summary>
/// unity representation of cozmo's LightCubes
///   adds functionality for controlling the four LEDs
///   adds awareness of accelerometer messages to detect LightCube movements
/// </summary>
public class LightCube : ObservedObject {
  public class Light : Robot.Light {
    public static new float MessageDelay = 0f;

    public override void ClearData() {
      base.ClearData();
      MessageDelay = 0f;
    }

    public void SetFlashingLED(Color onColor, uint onDurationMs = 200, uint offDurationMs = 200, uint transitionMs = 0) {
      OnColor = onColor.ToUInt();
      OffColor = 0;
      OnPeriodMs = onDurationMs;
      OffPeriodMs = offDurationMs;
      TransitionOnPeriodMs = transitionMs;
      TransitionOffPeriodMs = transitionMs;
    }
  }

  #region LightCube helpers

  public static bool TryFindCubesFurthestApart(List<LightCube> cubesToCompare, out LightCube cubeA, out LightCube cubeB) {
    bool success = false;
    cubeA = null;
    cubeB = null;
    if (cubesToCompare.Count >= 2) {
      float longestDistanceSquared = -1f;
      float distanceSquared = -1f;
      Vector3 distanceVector;
      // Check 0->1, 0->2, 0->3... 0->n then check 1->2, 1->3,...1->n all the way to (n-1)->n
      // Distance checks are communicable so there's no use checking 0->1 and 1->0
      for (int rootCube = 0; rootCube < cubesToCompare.Count - 1; rootCube++) {
        for (int otherCube = rootCube + 1; otherCube < cubesToCompare.Count; otherCube++) {
          distanceVector = cubesToCompare[rootCube].WorldPosition - cubesToCompare[otherCube].WorldPosition;
          distanceSquared = distanceVector.sqrMagnitude;
          if (distanceSquared > longestDistanceSquared) {
            longestDistanceSquared = distanceSquared;
            cubeA = cubesToCompare[rootCube];
            cubeB = cubesToCompare[otherCube];
          }
        }
      }
      success = true;
    }
    else {
      DAS.Error("CozmoUtil", string.Format("GetCubesFurthestApart: cubesToCompare has less than 2 cubes! cubesToCompare.Count: {0}", 
        cubesToCompare.Count));
      if (cubesToCompare.Count == 1) {
        cubeA = cubesToCompare[0];
      }
    }
    return success;
  }

  #endregion

  public bool IsMoving { get; private set; }

  public UpAxis UpAxis { get; private set; }

  public float XAccel { get; private set; }

  public float YAccel { get; private set; }

  public float ZAccel { get; private set; }

  private U2G.SetAllActiveObjectLEDs SetAllActiveObjectLEDsMessage;

  public Light[] Lights { get; private set; }

  public bool LightsChanged {
    get {
      if (lastRelativeMode != relativeMode || lastRelativeToX != relativeToX || lastRelativeToY != relativeToY)
        return true;

      for (int i = 0; i < Lights.Length; ++i) {
        if (Lights[i].Changed)
          return true;
      }

      return false;
    }
  }

  private MakeRelativeMode lastRelativeMode;
  public MakeRelativeMode relativeMode;

  private float lastRelativeToX;
  public float relativeToX;
  private float lastRelativeToY;
  public float relativeToY;

  public event Action<LightCube> OnAxisChange;

  /// <summary>
  /// TappedAction<ID, Tap Count, Timestamp>.
  /// </summary>
  public static Action<int, int, float> TappedAction;
  public static Action<int, float, float, float> OnMovedAction;
  public static Action<int> OnStoppedAction;

  public LightCube(int objectID, ObjectFamily objectFamily, ObjectType objectType) {
    Constructor(objectID, objectFamily, objectType);

    UpAxis = UpAxis.Unknown;
    XAccel = byte.MaxValue;
    YAccel = byte.MaxValue;
    ZAccel = byte.MaxValue;
    IsMoving = false;

    SetAllActiveObjectLEDsMessage = new U2G.SetAllActiveObjectLEDs();

    Lights = new Light[SetAllActiveObjectLEDsMessage.onColor.Length];

    for (int i = 0; i < Lights.Length; ++i) {
      Lights[i] = new Light();
    }

  }

  public void Moving(ObjectMoved message) {
    IsMoving = true;

    UpAxis = message.upAxis;
    XAccel = message.accel.x;
    YAccel = message.accel.y;
    ZAccel = message.accel.z;

    if (OnMovedAction != null) {
      OnMovedAction(ID, XAccel, YAccel, ZAccel);
    }
  }

  public void StoppedMoving(ObjectStoppedMoving message) {
    IsMoving = false;

    if (message.rolled) {
      if (OnAxisChange != null)
        OnAxisChange(this);
    }
    if (OnStoppedAction != null) {
      OnStoppedAction(ID);
    }
  }

  public void Tapped(ObjectTapped message) {
    DAS.Debug(this, "Tapped Message Received for LightCube(" + ID + "): " + message.numTaps + " taps");
    if (TappedAction != null)
      TappedAction(ID, message.numTaps, message.timestamp);
  }

  public void SetAllLEDs() { // should only be called from update loop
    SetAllActiveObjectLEDsMessage.objectID = (uint)ID;
    SetAllActiveObjectLEDsMessage.robotID = (byte)RobotID;

    for (int i = 0; i < Lights.Length; ++i) {
      SetAllActiveObjectLEDsMessage.onPeriod_ms[i] = Lights[i].OnPeriodMs;
      SetAllActiveObjectLEDsMessage.offPeriod_ms[i] = Lights[i].OffPeriodMs;
      SetAllActiveObjectLEDsMessage.transitionOnPeriod_ms[i] = Lights[i].TransitionOnPeriodMs;
      SetAllActiveObjectLEDsMessage.transitionOffPeriod_ms[i] = Lights[i].TransitionOffPeriodMs;
      SetAllActiveObjectLEDsMessage.onColor[i] = Lights[i].OnColor;
      SetAllActiveObjectLEDsMessage.offColor[i] = Lights[i].OffColor;
    }

    SetAllActiveObjectLEDsMessage.makeRelative = relativeMode;
    SetAllActiveObjectLEDsMessage.relativeToX = relativeToX;
    SetAllActiveObjectLEDsMessage.relativeToY = relativeToY;

    RobotEngineManager.Instance.Message.SetAllActiveObjectLEDs = SetAllActiveObjectLEDsMessage;
    RobotEngineManager.Instance.SendMessage();

    SetLastLEDs();
  }

  private void SetLastLEDs() {
    lastRelativeMode = relativeMode;
    lastRelativeToX = relativeToX;
    lastRelativeToY = relativeToY;

    for (int i = 0; i < Lights.Length; ++i) {
      Lights[i].SetLastInfo();
    }
  }

  public void SetLEDsOff() {
    SetLEDs((uint)LEDColor.LED_OFF);
  }

  public void SetLEDs(Color onColor) {
    SetLEDs(onColor.ToUInt());
  }

  public void SetLEDs(Color[] onColor) {
    uint[] colors = new uint[onColor.Length];
    for (int i = 0; i < onColor.Length; i++) {
      colors[i] = onColor[i].ToUInt();
    }
    SetLEDs(colors);
  }

  public void SetFlashingLEDs(Color onColor, uint onDurationMs = 100, uint offDurationMs = 100, uint transitionMs = 0) {
    SetLEDs(onColor.ToUInt(), 0, onDurationMs, offDurationMs, transitionMs, transitionMs);
  }

  public void SetFlashingLEDs(uint onColor, uint onDurationMs = 100, uint offDurationMs = 100, uint transitionMs = 0) {
    SetLEDs(onColor, 0, onDurationMs, offDurationMs, transitionMs, transitionMs);
  }

  public void SetFlashingLEDs(Color[] onColors, Color offColor, uint onDurationMs = 100, uint offDurationMs = 100, uint transitionMs = 0) {
    uint[] onColorsUint = new uint[onColors.Length];
    for (int i = 0; i < onColorsUint.Length; i++) {
      onColorsUint[i] = onColors[i].ToUInt();
    }
    SetLEDs(onColorsUint, offColor.ToUInt(), onDurationMs, offDurationMs, transitionMs, transitionMs);
  }

  public void SetLEDs(uint onColor = 0, uint offColor = 0, uint onPeriod_ms = Light.FOREVER, uint offPeriod_ms = 0, 
                      uint transitionOnPeriod_ms = 0, uint transitionOffPeriod_ms = 0) {

    Light light; 
    for (int i = 0; i < Lights.Length; ++i) {
      light = Lights[i];
      light.OnColor = onColor;
      light.OffColor = offColor;
      light.OnPeriodMs = onPeriod_ms;
      light.OffPeriodMs = offPeriod_ms;
      light.TransitionOnPeriodMs = transitionOnPeriod_ms;
      light.TransitionOffPeriodMs = transitionOffPeriod_ms;
    }

    relativeMode = 0;
    relativeToX = 0;
    relativeToY = 0;
  }

  public void SetLEDs(uint[] lightColors, uint offColor = 0, uint onPeriod_ms = Light.FOREVER, uint offPeriod_ms = 0, 
                      uint transitionOnPeriod_ms = 0, uint transitionOffPeriod_ms = 0) {

    uint onColor;
    Light light; 
    for (int i = 0; i < Lights.Length; ++i) {
      onColor = lightColors[i % lightColors.Length];
      light = Lights[i];
      light.OnColor = onColor;
      light.OffColor = offColor;
      light.OnPeriodMs = onPeriod_ms;
      light.OffPeriodMs = offPeriod_ms;
      light.TransitionOnPeriodMs = transitionOnPeriod_ms;
      light.TransitionOffPeriodMs = transitionOffPeriod_ms;
    }

    relativeMode = 0;
    relativeToX = 0;
    relativeToY = 0;
  }

  public void SetLEDsRelative(Vector2 relativeTo, uint onColor = 0, uint offColor = 0, MakeRelativeMode relativeMode = MakeRelativeMode.RELATIVE_LED_MODE_BY_CORNER,
                              uint onPeriod_ms = Light.FOREVER, uint offPeriod_ms = 0, uint transitionOnPeriod_ms = 0, uint transitionOffPeriod_ms = 0, byte turnOffUnspecifiedLEDs = 1) {  
    SetLEDsRelative(relativeTo.x, relativeTo.y, onColor, offColor, relativeMode, onPeriod_ms, offPeriod_ms, transitionOnPeriod_ms, transitionOffPeriod_ms);
  }

  public void SetLEDsRelative(float relativeToX, float relativeToY, uint onColor = 0, uint offColor = 0, MakeRelativeMode relativeMode = MakeRelativeMode.RELATIVE_LED_MODE_BY_CORNER,
                              uint onPeriod_ms = Light.FOREVER, uint offPeriod_ms = 0, uint transitionOnPeriod_ms = 0, uint transitionOffPeriod_ms = 0) {
    SetLEDs(onColor, offColor, onPeriod_ms, offPeriod_ms, transitionOnPeriod_ms, transitionOffPeriod_ms);

    this.relativeMode = relativeMode;
    this.relativeToX = relativeToX;
    this.relativeToY = relativeToY;
  }

  public Color[] GetLEDs() {
    Color[] lightColors = new Color[Lights.Length];
    for (int i = 0; i < Lights.Length; ++i) {
      lightColors[i] = Lights[i].OnColor.ToColor();
    }
    return lightColors;
  }
}

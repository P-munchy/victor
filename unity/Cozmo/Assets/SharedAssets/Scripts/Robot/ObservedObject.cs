﻿using UnityEngine;
using System;
using System.Collections;
using System.Collections.Generic;
using Anki.Cozmo;
using G2U = Anki.Cozmo.ExternalInterface;
using U2G = Anki.Cozmo.ExternalInterface;

public enum CubeType {
  Unknown = -1,
  LightCube,
  BullsEye,
  Flag,
  Face,
  Count
}

/// <summary>
/// all objects that cozmo sees are transmitted across to unity and represented here as ObservedObjects
///   so far, we only both handling three types of cubes and the occasional human head
/// </summary>
public class ObservedObject {
  public CubeType CubeType { get; private set; }

  public uint RobotID { get; private set; }

  public ObjectFamily Family { get; private set; }

  public ObjectType ObjectType { get; private set; }

  public int ID { get; private set; }

  public bool MarkersVisible { get { return Time.time - TimeLastSeen < kRemoveDelay; } }

  public Rect VizRect { get; private set; }

  public Vector3 WorldPosition { get; private set; }

  public Quaternion Rotation { get; private set; }

  public Vector3 Forward { get { return Rotation * Vector3.right; } }

  public Vector3 Right { get { return Rotation * -Vector3.up; } }

  public Vector3 Up { get { return Rotation * Vector3.forward; } }

  public Vector3 TopNorth { get { return Quaternion.AngleAxis(TopFaceNorthAngle * Mathf.Rad2Deg, Vector3.forward) * Vector2.right; } }

  public Vector3 TopEast { get { return Quaternion.AngleAxis(TopFaceNorthAngle * Mathf.Rad2Deg, Vector3.forward) * -Vector2.up; } }

  public Vector3 TopNorthEast { get { return (TopNorth + TopEast).normalized; } }

  public Vector3 TopSouthEast { get { return (-TopNorth + TopEast).normalized; } }

  public Vector3 TopSouthWest { get { return (-TopNorth - TopEast).normalized; } }

  public Vector3 TopNorthWest { get { return (TopNorth - TopEast).normalized; } }

  public float TopFaceNorthAngle { get; private set; }

  public Vector3 Size { get; private set; }

  public float TimeLastSeen { get; private set; }

  public float TimeCreated { get; private set; }

  protected Robot RobotInstance { get { return RobotEngineManager.Instance != null ? RobotEngineManager.Instance.CurrentRobot : null; } }

  public bool IsActive { get { return CubeType == CubeType.LightCube; } }

  public bool IsFace { get { return CubeType == CubeType.Face; } }

  public const float kRemoveDelay = 0.33f;

  public string InfoString { get; private set; }

  public string SelectInfoString { get; private set; }

  public ObservedObject() {
  }

  public ObservedObject(int objectID, ObjectFamily objectFamily, ObjectType objectType) {
    Constructor(objectID, objectFamily, objectType);
  }

  protected void Constructor(int objectID, ObjectFamily objectFamily, ObjectType objectType) {
    TimeCreated = Time.time;
    Family = objectFamily;
    ObjectType = objectType;
    ID = objectID;
    
    InfoString = "ID: " + ID + " Family: " + Family + " Type: " + ObjectType;
    SelectInfoString = "Select ID: " + ID + " Family: " + Family + " Type: " + ObjectType;

    if (objectFamily == ObjectFamily.LightCube) {
      CubeType = CubeType.LightCube;
    }
    else if (objectType == ObjectType.Block_BULLSEYE2 || objectType == ObjectType.Block_BULLSEYE2_INVERTED) {
      CubeType = CubeType.BullsEye;
    }
    else if (objectType == ObjectType.Block_FLAG || objectType == ObjectType.Block_FLAG2 || objectType == ObjectType.Block_FLAG_INVERTED) {
      CubeType = CubeType.Flag;
    }
    else {
      CubeType = CubeType.Unknown;
      DAS.Warn(this, "Object " + ID + " with type " + objectType + " is unsupported"); 
    }

    DAS.Debug(this, "ObservedObject cubeType(" + CubeType + ") from objectFamily(" + objectFamily + ") objectType(" + objectType + ")");

  }

  public static implicit operator uint(ObservedObject observedObject) {
    if (observedObject == null) {
      DAS.Warn(typeof(ObservedObject), "converting null ObservedObject into uint: returning uint.MaxValue");
      return uint.MaxValue;
    }
    
    return (uint)observedObject.ID;
  }

  public static implicit operator int(ObservedObject observedObject) {
    if (observedObject == null)
      return -1;

    return observedObject.ID;
  }

  public static implicit operator string(ObservedObject observedObject) {
    return ((int)observedObject).ToString();
  }

  public void UpdateInfo(G2U.RobotObservedObject message) {
    RobotID = message.robotID;
    VizRect = new Rect(message.img_topLeft_x, message.img_topLeft_y, message.img_width, message.img_height);

    Vector3 newPos = new Vector3(message.world_x, message.world_y, message.world_z);

    //dmdnote cozmo's space is Z up, keep in mind if we need to convert to unity's y up space.
    WorldPosition = newPos;
    Rotation = new Quaternion(message.quaternion_x, message.quaternion_y, message.quaternion_z, message.quaternion_w);
    Size = Vector3.one * CozmoUtil.kBlockLengthMM;

    TopFaceNorthAngle = message.topFaceOrientation_rad + Mathf.PI * 0.5f;

    if (message.markersVisible > 0)
      TimeLastSeen = Time.time;
  }

  public Vector2 GetBestFaceVector(Vector3 initialVector) {
    
    Vector2[] faces = { TopNorth, TopEast, -TopNorth, -TopEast };
    Vector2 face = faces[0];
    
    float bestDot = -float.MaxValue;
    
    for (int i = 0; i < 4; i++) {
      float dot = Vector2.Dot(initialVector, faces[i]);
      if (dot > bestDot) {
        bestDot = dot;
        face = faces[i];
      }
    }
    
    return face;
  }

}

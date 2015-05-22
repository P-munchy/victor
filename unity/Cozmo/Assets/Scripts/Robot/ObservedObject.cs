﻿using UnityEngine;
using System;
using System.Collections;
using System.Collections.Generic;
using Anki.Cozmo;
using G2U = Anki.Cozmo.G2U;
using U2G = Anki.Cozmo.U2G;

public enum CubeType {
	LIGHT_CUBE,
	BULLS_EYE,
	FLAG
}

public class ObservedObject
{
	public CubeType cubeType { get; private set; }
	public uint RobotID { get; private set; }
	public uint Family { get; private set; }
	public uint ObjectType { get; private set; }

	protected int ID;

	public bool MarkersVisible { get { return Time.time - TimeLastSeen < 0.25f; } }

	public Rect VizRect { get; private set; }
	public Vector3 WorldPosition { get; private set; }
	public Quaternion Rotation { get; private set; }
	public Vector3 Forward { get { return Rotation * Vector3.right;	} }
	public Vector3 Right { get { return Rotation * -Vector3.up;	} }
	public Vector3 TopNorth { get { return Quaternion.AngleAxis( TopFaceNorthAngle * Mathf.Rad2Deg, Vector3.forward ) * Vector2.right; } }
	public Vector3 TopEast { get { return Quaternion.AngleAxis( TopFaceNorthAngle * Mathf.Rad2Deg, Vector3.forward ) * -Vector2.up; } }
	public Vector3 TopNorthEast { get { return ( TopNorth + TopEast ).normalized; } }
	public Vector3 TopSouthEast { get { return ( -TopNorth + TopEast ).normalized; } }
	public Vector3 TopSouthWest { get { return ( -TopNorth - TopEast ).normalized; } }
	public Vector3 TopNorthWest { get { return ( TopNorth - TopEast ).normalized; } }

	public float TopFaceNorthAngle { get; private set; }

	public Vector3 Size { get; private set; }
	public float TimeLastSeen { get; private set; }
	public float TimeCreated { get; private set; }

	private Robot robot { get { return RobotEngineManager.instance != null ? RobotEngineManager.instance.current : null; } }

	public bool isActive { get { return cubeType == CubeType.LIGHT_CUBE; } }

	public event Action<ObservedObject> OnDelete;
	public static Action SignificantChangeDetected = null;

	public bool canBeStackedOn
	{
		get
		{
			if( robot.selectedObjects.Count > 1 ) return false; // if blocks stacked on each other

			//float distance = ( (Vector2)WorldPosition - (Vector2)robot.WorldPosition ).magnitude;
			float height = Mathf.Abs( WorldPosition.z - robot.WorldPosition.z );
		
			return /*distance <= CozmoUtil.BLOCK_LENGTH_MM * 4f &&*/ height < CozmoUtil.BLOCK_LENGTH_MM;
		}
	}
	
	public const float RemoveDelay = 0.15f;

	public float Distance { get { return Vector2.Distance( RobotEngineManager.instance.current.WorldPosition, WorldPosition ); } }

	public string InfoString { get; private set; }
	public string SelectInfoString { get; private set; }
	public ObservedObject() { }

	public ObservedObject( int objectID, uint objectFamily, uint objectType )
	{
		Constructor( objectID, objectFamily, objectType );
	}

	protected void Constructor( int objectID, uint objectFamily, uint objectType )
	{
		TimeCreated = Time.time;
		Family = objectFamily;
		ObjectType = objectType;
		ID = objectID;
		
		InfoString = "ID: " + ID + " Family: " + Family + " Type: " + ObjectType;
		SelectInfoString = "Select ID: " + ID + " Family: " + Family + " Type: " + ObjectType;

		if (objectFamily == 3) {
			cubeType = CubeType.LIGHT_CUBE;
		} else if (objectType == 4 || objectType == 5) {
			cubeType = CubeType.BULLS_EYE;
		}
		else if (objectType == 11 || objectType == 12) {
			cubeType = CubeType.FLAG;
		}

		if(SignificantChangeDetected != null) SignificantChangeDetected();
		//Debug.Log ("ObservedObject cubeType("+cubeType+") from objectFamily("+objectFamily+") objectType("+objectType+")" );
	}

	public static implicit operator uint( ObservedObject observedObject )
	{
		if( observedObject == null )
		{
			Debug.LogWarning( "converting null ObservedObject into uint: returning uint.MaxValue" );
			return uint.MaxValue;
		}
		
		return (uint)observedObject.ID;
	}

	public static implicit operator int( ObservedObject observedObject )
	{
		if( observedObject == null ) return -1;

		return observedObject.ID;
	}

	public static implicit operator string( ObservedObject observedObject )
	{
		return ((int)observedObject).ToString();
	}

	public void UpdateInfo( G2U.RobotObservedObject message )
	{
		RobotID = message.robotID;
		VizRect = new Rect( message.img_topLeft_x, message.img_topLeft_y, message.img_width, message.img_height );

		Vector3 newPos = new Vector3( message.world_x, message.world_y, message.world_z );

		//detect if this object has moved to a degree significant enough to warrant reValidating the build
		if( robot != null && robot.carryingObject != this && (newPos - WorldPosition).magnitude > CozmoUtil.BLOCK_LENGTH_MM ){
			if(SignificantChangeDetected != null) SignificantChangeDetected();
		}

		//dmdnote cozmo's space is Z up, keep in mind if we need to convert to unity's y up space.
		WorldPosition = newPos;
		Rotation = new Quaternion( message.quaternion1, message.quaternion2, message.quaternion3, message.quaternion0 );
		Size = Vector3.one * CozmoUtil.BLOCK_LENGTH_MM;

		TopFaceNorthAngle = message.topFaceOrientation_rad + Mathf.PI * 0.5f;

		if( message.markersVisible > 0 ) TimeLastSeen = Time.time;
	}

	public void Delete()
	{
		if( OnDelete != null ) OnDelete( this );
		if(SignificantChangeDetected != null) SignificantChangeDetected();
	}

	public Vector2 GetBestFaceVector(Vector3 initialVector) {
		
		Vector2[] faces = { TopNorth, TopEast, -TopNorth, -TopEast };
		Vector2 face = faces[0];
		
		float bestDot = -float.MaxValue;
		
		for(int i=0; i<4; i++) {
			float dot = Vector2.Dot(initialVector, faces[i]);
			if(dot > bestDot) {
				bestDot = dot;
				face = faces[i];
			}
		}
		
		return face;
	}
}

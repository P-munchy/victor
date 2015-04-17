﻿using UnityEngine;
using UnityEngine.UI;
using System.Collections;
using System.Collections.Generic;

public class CozmoVision_AutoSelect : CozmoVision
{
	public struct ActionButtonState
	{
		public bool activeSelf;
		public string text;
	}

	[SerializeField] protected float maxDistance = 200f;

	protected List<ObservedObject> observedObjects;

	protected override void Awake()
	{
		base.Awake();

		observedObjects = new List<ObservedObject>();
	}

	protected void Update()
	{
		if( actionPanel == null ) return;

		if( RobotEngineManager.instance == null || RobotEngineManager.instance.current == null || actionPanel.gameActions == null )
		{
			actionPanel.DisableButtons();
			return;
		}

		robot = RobotEngineManager.instance.current;

		ShowObservedObjects();

		if( !robot.isBusy )
		{
			robot.selectedObjects.Clear();
			observedObjects.Clear();
			observedObjects.AddRange( robot.pertinentObjects );

			/*observedObjects.Sort( ( obj1 ,obj2 ) => // sort by distance from robot
			{
				return obj1.Distance.CompareTo( obj2.Distance );   
			} );*/

			observedObjects.Sort( ( obj1 ,obj2 ) => // sort by most center of view
			{
				Vector2 center = NativeResolution * 0.5f;

				return Vector2.Distance( obj1.VizRect.center, center ).CompareTo( Vector2.Distance( obj2.VizRect.center, center ) );   
			} );

			/*for( int i = 1; i < observedObjects.Count; ++i )
			{
				float distance = observedObjects[i].Distance;
				if( distance > observedObjects[0].Distance && distance > maxDistance ) // if multiple in view and too far away
				{
					observedObjects.RemoveAt( i-- );
				}
			}*/

			for( int i = 1; i < observedObjects.Count; ++i ) // if not on top of selected block, remove
			{
				if( Vector2.Distance( observedObjects[0].WorldPosition, observedObjects[i].WorldPosition ) > observedObjects[0].Size.x * 0.5f )
				{
					observedObjects.RemoveAt( i-- );
				}
			}

			observedObjects.Sort( ( obj1, obj2 ) => { return obj1.WorldPosition.z.CompareTo( obj2.WorldPosition.z ); } );

			if( robot.Status( Robot.StatusFlag.IS_CARRYING_BLOCK ) ) // if holding a block
			{
				if( observedObjects.Count > 0 && observedObjects[0] != robot.carryingObject ) // if can see at least one block
				{
					robot.selectedObjects.Add( observedObjects[0] );
					
					if( observedObjects.Count == 1 )
					{
						RobotEngineManager.instance.current.TrackHeadToObject( robot.selectedObjects[0] );
					}
				}
				
			}
			else // if not holding a block
			{
				if( observedObjects.Count > 0 )
				{
					for( int i = 0; i < 2 && i < observedObjects.Count; ++i )
					{
						robot.selectedObjects.Add( observedObjects[i] );
					}
					
					if( observedObjects.Count == 1 )
					{
						RobotEngineManager.instance.current.TrackHeadToObject( robot.selectedObjects[0] );
					}
				}
				
			}
		}

		RefreshFade();
		if(robot.selectedObjects.Count > 0 && !robot.isBusy) {
			FadeIn();
		}
		else {
			FadeOut();
		}

		actionPanel.gameActions.SetActionButtons();
		Dings();
	}

	protected override void Dings()
	{
		if( robot != null )
		{
			if( !robot.isBusy && robot.selectedObjects.Count > 0/*robot.lastSelectedObjects.Count*/ )
			{
				Ding( true );
			}
			/*else if( robot.selectedObjects.Count < robot.lastSelectedObjects.Count )
			{
				Ding( false );
			}*/
		}
	}
}

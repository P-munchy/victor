
using UnityEngine;
using System;
using System.Reflection;

public static class CozmoUtil {

	public const float SMALL_SCREEN_MAX_HEIGHT = 3f;
	public const float MAX_WHEEL_SPEED_MM 	= 160f;
	public const float MIN_WHEEL_SPEED_MM 	= 10f;
	public const float BLOCK_LENGTH_MM 		= 44f;
	public const float LOCAL_BUSY_TIME 		= 1f;
	public const float MAX_LIFT_HEIGHT_MM 	= 95f;
	public const float MIN_LIFT_HEIGHT_MM 	= 23f;

	public static Vector3 Vector3UnityToCozmoSpace(Vector3 vector) {
		float forward = vector.z;
		float up = vector.y;

		vector.y = forward;
		vector.z = up;

		return vector;
	}

	public static Vector3 Vector3CozmoToUnitySpace(Vector3 vector) {
		float forward = vector.y;
		float up = vector.z;

		vector.z = forward;
		vector.y = up;

		return vector;
	}

	public static void CalcWheelSpeedsForTwoAxisInputs(Vector2 inputs, out float leftWheelSpeed, out float rightWheelSpeed, float maxTurnFactor) {
		
		leftWheelSpeed = 0f;
		rightWheelSpeed = 0f;
		
		if(inputs.x == 0f && inputs.y == 0f) return;

		float speed = inputs.magnitude;
		float turn = inputs.x;

		speed = Mathf.Clamp01(speed*speed) * (inputs.y >= 0f ? 1f : -1f);
		//turn = Mathf.Clamp01(turn*turn) * (turn >= 0f ? 1f : -1f);

		if(turn == 0f) { 
			//forward or backwards, scale speed from min to max
			speed = Mathf.Lerp(MIN_WHEEL_SPEED_MM, MAX_WHEEL_SPEED_MM, Mathf.Abs(speed)) * (speed >= 0f ? 1f : -1f);

			leftWheelSpeed = speed;
			rightWheelSpeed = speed;
		}
		else if(inputs.y == 0f) {
			//turn in place left or right, scale speed from min to max including maxTurnFactor
			speed = Mathf.Lerp(MIN_WHEEL_SPEED_MM, MAX_WHEEL_SPEED_MM, Mathf.Abs(speed) * maxTurnFactor) * (speed >= 0f ? 1f : -1f);

			leftWheelSpeed = turn > 0f ? speed : -speed;
			rightWheelSpeed = turn > 0f ? -speed : speed;
		}
		else {
			//drive and turn
			speed = Mathf.Lerp(MIN_WHEEL_SPEED_MM, MAX_WHEEL_SPEED_MM, Mathf.Abs(speed)) * (speed >= 0f ? 1f : -1f);

			float speedA = speed;
			float speedB = Mathf.Lerp(speed, speed*0.5f*(1f-maxTurnFactor), Mathf.Abs(turn));

			if(turn > 0f) {
				leftWheelSpeed = speedA;
				rightWheelSpeed = speedB;
			}
			else if(turn < 0f) {
				rightWheelSpeed = speedA;
				leftWheelSpeed = speedB;
			}
		}

		//Debug.Log("CalcWheelSpeedsFromBotRelativeInputsB speed("+speed+") turn("+turn+")");
	}

	public static void CalcWheelSpeedsForThumbStickInputs(Vector2 inputs, out float leftWheelSpeed, out float rightWheelSpeed, float maxAngle, float maxTurnFactor, bool reverse=false) {
		
		leftWheelSpeed = 0f;
		rightWheelSpeed = 0f;
		
		if(inputs.x == 0f && inputs.y == 0f) return;

		float speed = inputs.magnitude;
		if(speed == 0f) return;

		float turn = 0f;

		if(!reverse) {
			if(maxAngle > 0f) turn = Mathf.Clamp01(Vector2.Angle(Vector2.up, inputs) / maxAngle) * (inputs.x >= 0f ? 1f : -1f);
		}
		else {
			if(maxAngle > 0f) turn = Mathf.Clamp01(Vector2.Angle(-Vector2.up, inputs) / maxAngle) * (inputs.x >= 0f ? 1f : -1f);
			speed = -speed;
		}

		speed = speed*speed * (speed < 0f ? -1f : 1f);
		//turn = turn*turn * (turn < 0f ? -1f : 1f);

		speed = Mathf.Lerp(MIN_WHEEL_SPEED_MM, MAX_WHEEL_SPEED_MM, Mathf.Abs(speed)) * (speed >= 0f ? 1f : -1f);

		float speedA = speed;
		float speedB = speed;

		if(turn != 0f) {
			speedA = Mathf.Lerp(speed, speed*maxTurnFactor, Mathf.Abs(turn));
			speedB = Mathf.Lerp(speed, -speed*maxTurnFactor, Mathf.Abs(turn));
		}

		leftWheelSpeed = turn >= 0f ? speedA : speedB;
		rightWheelSpeed = turn < 0f ? speedA : speedB;

		//Debug.Log("CalcDriveWheelSpeedsForInputs speed("+speed+") turn("+turn+")");
	}

	public static void CalcTurnInPlaceWheelSpeeds(float x, out float leftWheelSpeed, out float rightWheelSpeed, float maxTurnFactor) {
		leftWheelSpeed = 0f;
		rightWheelSpeed = 0f;
		if(x == 0f) return;

		float speed = Mathf.Lerp(MIN_WHEEL_SPEED_MM, MAX_WHEEL_SPEED_MM, Mathf.Abs(x) * maxTurnFactor) * (x >= 0f ? 1f : -1f);

		leftWheelSpeed = speed;
		rightWheelSpeed = -speed;
	}

	public static void CalcWheelSpeedsForOldThumbStickInputs(Vector2 inputs, out float leftWheelSpeed, out float rightWheelSpeed, float maxAngle, float maxTurnFactor) {
		
		leftWheelSpeed = 0f;
		rightWheelSpeed = 0f;
		
		if(inputs.x == 0f && inputs.y == 0f) return;
		
		float speed = inputs.magnitude;
		if(speed == 0f) return;
		
		float turn = 0f;
		bool reverse = inputs.y < 0f;

		if(!reverse) {
			if(maxAngle > 0f) turn = Mathf.Clamp01(Vector2.Angle(Vector2.up, inputs) / maxAngle) * (inputs.x >= 0f ? 1f : -1f);
		}
		else {
			if(maxAngle > 0f) turn = Mathf.Clamp01(Vector2.Angle(-Vector2.up, inputs) / maxAngle) * (inputs.x >= 0f ? 1f : -1f);
			speed = -speed;
		}
		
		speed = speed*speed * (speed < 0f ? -1f : 1f);
		//turn = turn*turn * (turn < 0f ? -1f : 1f);
		
		speed = Mathf.Lerp(MIN_WHEEL_SPEED_MM, MAX_WHEEL_SPEED_MM, Mathf.Abs(speed)) * (speed >= 0f ? 1f : -1f);
		
		float speedA = speed;
		float speedB = speed;
		
		if(turn != 0f) {
			speedB = Mathf.Lerp(speed, speed*0.5f*(1f-maxTurnFactor), Mathf.Abs(turn));
		}
		
		leftWheelSpeed = turn >= 0f ? speedA : speedB;
		rightWheelSpeed = turn < 0f ? speedA : speedB;
		
		//Debug.Log("CalcDriveWheelSpeedsForInputs speed("+speed+") turn("+turn+")");
	}

	public static void CalcWheelSpeedsForPlayerRelInputs(Vector2 inputs, out float leftWheelSpeed, out float rightWheelSpeed) {
		
		leftWheelSpeed = 0f;
		rightWheelSpeed = 0f;
		
		if(inputs.x == 0f && inputs.y == 0f) return;
		
		float speed = inputs.sqrMagnitude;
		if(speed == 0f) return;

		bool clock = inputs.x >= 0f;

		speed = Mathf.Lerp(MIN_WHEEL_SPEED_MM, MAX_WHEEL_SPEED_MM, Mathf.Abs(speed));
		
		float speedA = speed;

		float angle = Vector2.Angle(Vector2.up, inputs);
		float turn = Mathf.Clamp01(angle / 90f);
		turn *= turn;
		turn *= clock ? 1f : -1f;

		float speedB = Mathf.Lerp(speed, -speed, Mathf.Abs(turn));

		leftWheelSpeed = clock ? speedA : speedB;
		rightWheelSpeed = clock ? speedB : speedA;
		
		//Debug.Log("CalcDriveWheelSpeedsForInputs speed("+speed+") turn("+turn+")");
	}

	public static T GetCopyOf<T>(Component comp, T other) where T : Component {
		Type type = comp.GetType();
		//if(type != other.GetType()) {
		//	Debug.LogError("GetCopyOf type mis-match");
		//	return null; // type mis-match
		//}
		BindingFlags flags = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Default | BindingFlags.DeclaredOnly;
		PropertyInfo[] pinfos = type.GetProperties(flags);
		foreach (var pinfo in pinfos) {
			if (pinfo.CanWrite) {
				try {
					pinfo.SetValue(comp, pinfo.GetValue(other, null), null);
				}
				catch { } // In case of NotImplementedException being thrown. For some reason specifying that exception didn't seem to catch it, so I didn't catch anything specific.
			}
		}
		FieldInfo[] finfos = type.GetFields(flags);
		foreach (var finfo in finfos) {
			finfo.SetValue(comp, finfo.GetValue(other));
		}
		return comp as T;
	}

	public static T AddComponent<T>(this GameObject go, T toAdd) where T : Component
	{
		T comp = go.AddComponent<T>();

		return GetCopyOf(comp, toAdd) as T;
	}

}

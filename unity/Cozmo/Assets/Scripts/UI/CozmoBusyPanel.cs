﻿using UnityEngine;
using UnityEngine.UI;
using System.Collections;

public class CozmoBusyPanel : MonoBehaviour {

	[SerializeField] Text text_actionDescription;
	[SerializeField] GameObject panel;
	[SerializeField] AudioClip affirmativeSound;

	Robot robot { get { return RobotEngineManager.instance != null ? RobotEngineManager.instance.current : null; } }

	public static CozmoBusyPanel instance = null;

	float timer = 0f;
	float soundDelay = 0.25f;
	bool soundPlayed = false;

	void Awake() {
		//enforce singleton
		if(instance != null && instance != this) {
			GameObject.Destroy(instance.gameObject);
			return;
		}
		instance = this;
		DontDestroyOnLoad(gameObject);
	}

	void OnEnable() {
		timer = soundDelay;
	}

	// Update is called once per frame
	void Update () {
		if(robot == null || !robot.isBusy) {
			panel.SetActive(false);
			timer = soundDelay;
			return;
		}

		if(timer > 0f) {
			timer -= Time.deltaTime;

			if(timer <= 0f && affirmativeSound != null) {
				AudioManager.PlayOneShot(affirmativeSound);
			}
		}

		panel.SetActive(true);
	}


	public void CancelCurrentActions() {
		if(robot == null || !robot.isBusy) {
			panel.SetActive(false);
			return;
		}

		robot.CancelAction();

	}

	public void SetDescription(string desc) {
		text_actionDescription.text = desc;
	}
}

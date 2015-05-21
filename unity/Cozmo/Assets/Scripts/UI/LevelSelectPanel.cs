﻿using UnityEngine;
using UnityEngine.UI;
using System;
using System.Text.RegularExpressions;
using System.Collections;
using System.Collections.Generic;

public class LevelSelectPanel : MonoBehaviour {

	[SerializeField] Text textTitle;
	[SerializeField] string gameName = "Unknown";
	[SerializeField] int numLevels = 10;
	[SerializeField] GameObject levelSelectionPrefab;
	[SerializeField] ScrollRect scrollRect;
	[SerializeField] Sprite[] previews;

	List<LevelSelectButton> levelSelectButtons = new List<LevelSelectButton>();

	void Awake() {

		textTitle.text = gameName.ToUpper();

		float width = 0f;

		for(int i=0;i<numLevels;i++) {
			GameObject buttonObj = (GameObject)GameObject.Instantiate(levelSelectionPrefab);

			RectTransform rectT = buttonObj.transform as RectTransform;

			rectT.SetParent(scrollRect.content, false);
			width += rectT.sizeDelta.x;

			LevelSelectButton selectButton = buttonObj.GetComponent<LevelSelectButton>();

			Sprite preview = null;
			if(previews != null && previews.Length > 0) {
				int spriteIndex = Mathf.Clamp(i, 0, previews.Length-1);
				preview = previews[spriteIndex];
			}

			bool interactive = i < 2;

			int level = i+1;
			selectButton.Initialize(level.ToString(), preview, interactive ? UnityEngine.Random.Range(1,3) : 0, interactive, delegate{LaunchGame(level);});

			levelSelectButtons.Add(selectButton);
		}

		RectTransform scrollRectT = scrollRect.content.transform as RectTransform;
		Vector2 size = scrollRectT.sizeDelta;
		size.x = width;
		scrollRectT.sizeDelta = size;
	}

	void LaunchGame(int level) {

		//Debug.Log("LevelSelectPanel LaunchGame gameName("+gameName+") level("+level+")");
		PlayerPrefs.SetString("CurrentGame", gameName);
		PlayerPrefs.SetInt(gameName + "_CurrentLevel", level);

		string sceneName = Regex.Replace(gameName, "[ ]", string.Empty);

		Application.LoadLevel(sceneName);
	}

}

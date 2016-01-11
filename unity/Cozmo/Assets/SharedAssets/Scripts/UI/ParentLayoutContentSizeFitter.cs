﻿using UnityEngine;
using System.Collections;
using UnityEngine.UI;

// Similar to Unity's default ContentSizefitter but modifies
// the LayoutElement instead of modifying the RectTransform
// directly. This is useful if you want a child to expand to
// its content but still have it be under a parent with a layout group.
public class ParentLayoutContentSizeFitter : MonoBehaviour {

  void OnRectTransformDimensionsChange() {
    if (transform.parent != null) {
      StartCoroutine(ResizeParent());
    }
  }

  IEnumerator ResizeParent() {
    yield return null;
    LayoutElement layoutElement = transform.parent.GetComponent<LayoutElement>();
    layoutElement.minWidth = GetComponent<RectTransform>().rect.width;
    layoutElement.minHeight = GetComponent<RectTransform>().rect.height;
  }
}

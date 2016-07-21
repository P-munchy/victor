﻿using UnityEngine;
using UnityEngine.EventSystems;
using System.Collections.Generic;
using DG.Tweening;

public struct TouchInfo {
  public TouchInfo(Vector2 position, float time) {
    _Position = position;
    _Time = time;
  }
  public Vector2 _Position;
  public float _Time;
}

public class SwipeSlides : MonoBehaviour, IPointerDownHandler, IPointerUpHandler {

  private float _ThresholdSpeed;

  [SerializeField]
  private GameObject[] _SlidePrefabs;
  private List<GameObject> _SlideInstances = new List<GameObject>();

  [SerializeField]
  private RectTransform _SwipeContainer;

  [SerializeField]
  private SwipePageIndicator _PageIndicatorPrefab;
  private SwipePageIndicator _PageIndicatorInstance;

  private Dictionary<int, TouchInfo> _StartTouchInfo = new Dictionary<int, TouchInfo>();

  private int _CurrentIndex = 0;
  private bool _Transitioning = false;
  private Vector3 _StartingPosition;

  private void Start() {

    _ThresholdSpeed = GetComponent<RectTransform>().rect.width * 0.5f;

    for (int i = 0; i < _SlidePrefabs.Length; ++i) {
      GameObject slideInstance = GameObject.Instantiate(_SlidePrefabs[i]);
      slideInstance.transform.SetParent(_SwipeContainer, false);
      slideInstance.transform.localPosition = new Vector3(i * GetComponent<RectTransform>().rect.width, 0.0f, 0.0f);
      _SlideInstances.Add(slideInstance);
    }
    _StartingPosition = _SwipeContainer.localPosition;

    _PageIndicatorInstance = GameObject.Instantiate(_PageIndicatorPrefab.gameObject).GetComponent<SwipePageIndicator>();
    _PageIndicatorInstance.transform.SetParent(transform, false);
    _PageIndicatorInstance.SetPageCount(_SlidePrefabs.Length);
    _PageIndicatorInstance.SetCurrentPage(_CurrentIndex);
  }

  public void OnPointerDown(PointerEventData eventData) {
    _StartTouchInfo[eventData.pointerId] = new TouchInfo(eventData.position, Time.time);
  }

  public void OnPointerUp(PointerEventData eventData) {
    float horizontalSpeed = ComputeHorizontalSpeed(eventData);
    if (Mathf.Abs(horizontalSpeed) > _ThresholdSpeed) {
      if (horizontalSpeed > 0.0f) {
        TransitionLeft();
      }
      else {
        TransitionRight();
      }
    }
  }

  private float ComputeHorizontalSpeed(PointerEventData eventData) {
    TouchInfo startTouch;
    if (_StartTouchInfo.TryGetValue(eventData.pointerId, out startTouch)) {
      return (eventData.position.x - startTouch._Position.x) / (Time.time - startTouch._Time);
    }
    else {
      DAS.Warn("SwipeSlides.ComputeVelocity", "No start touch found.");
      return 0f;
    }
  }

  public void GoToIndex(int index) {
    if (index < 0 || index >= _SlidePrefabs.Length) {
      DAS.Error("SwipeSlides.GoToIndex", "out of bounds: " + index);
      return;
    }
    if (_Transitioning) {
      return;
    }
    _Transitioning = true;
    _SwipeContainer.DOLocalMove(_StartingPosition - Vector3.right * GetComponent<RectTransform>().rect.width * index, 0.25f).OnComplete(() => TransitionDone());
  }

  private void TransitionDone() {
    _Transitioning = false;
    _PageIndicatorInstance.SetCurrentPage(_CurrentIndex);
  }

  private void TransitionLeft() {
    if (_CurrentIndex <= 0 || _Transitioning) {
      return;
    }
    _CurrentIndex--;
    GoToIndex(_CurrentIndex);
  }

  private void TransitionRight() {
    if (_CurrentIndex >= _SlidePrefabs.Length - 1 || _Transitioning) {
      return;
    }
    _CurrentIndex++;
    GoToIndex(_CurrentIndex);
  }

  private void OnDestroy() {
    for (int i = 0; i < _SlideInstances.Count; ++i) {
      GameObject.Destroy(_SlideInstances[i]);
    }
    _SlideInstances.Clear();
  }

}

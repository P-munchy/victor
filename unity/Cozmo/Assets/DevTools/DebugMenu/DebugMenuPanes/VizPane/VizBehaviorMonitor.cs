﻿using UnityEngine;
using System.Collections.Generic;
using Anki.Cozmo;
using Anki.Cozmo.VizInterface;

namespace Anki.Cozmo.Viz {

  public class VizBehaviorMonitor : MonoBehaviour {

    [SerializeField]
    private RectTransform _BarTray;

    [SerializeField]
    private VizBehaviorBar _BarPrefab;

    private List<VizBehaviorBar> _BehaviorBars = new List<VizBehaviorBar>();

    private List<int> _Order = new List<int>();

    const int _kMaxBars = 5;

    private Color GetColorForBehavior(string name) {
      var c = ((uint)name.GetHashCode()).ToColor();
      c.a = 1f;
      return c;
    }

    private void Awake() {
      for(int i = 0; i < _kMaxBars; i++) {
        var bar = UIManager.CreateUIElement(_BarPrefab, _BarTray).GetComponent<VizBehaviorBar>();
        _BehaviorBars.Add(bar);
      }
    }

    private void UpdateOrder(BehaviorScoreData[] behaviors) {
      while (_Order.Count < behaviors.Length) {
        _Order.Add(_Order.Count);
      }

      if (_Order.Count > behaviors.Length) {
        _Order.RemoveRange(behaviors.Length, _Order.Count - behaviors.Length);
      }

      _Order.Sort(SortOrders);
    }


    private int SortOrders(int a, int b) {
      var behaviors = VizManager.Instance.BehaviorScoreData;
      // primary sort by score descending
      int sortVal = behaviors[b].behaviorScore.CompareTo(behaviors[a].behaviorScore);

      // secondary sort by name alphabetical
      if (sortVal == 0) {
        return behaviors[a].name.CompareTo(behaviors[b].name);
      }
      return sortVal;
    }

    // Update is called once per frame
    private void Update () {
      var behaviors = VizManager.Instance.BehaviorScoreData;

      if (behaviors != null) {
        UpdateOrder(behaviors);

        for (int i = 0; i < _kMaxBars; i++) {
          if (i < behaviors.Length) {
            var index = _Order[i];
            var behavior = behaviors[index];
            _BehaviorBars[i].SetLabel(behavior.name);
            _BehaviorBars[i].SetValue(behavior.behaviorScore, behavior.totalScore);  
            var color = GetColorForBehavior(behavior.name);

            _BehaviorBars[i].SetColor(color, color - new Color(0.1f,0.1f,0.1f, 0f));
          }
          else {
            _BehaviorBars[i].SetValue(0, 0);
            _BehaviorBars[i].SetLabel("None");
          }
        }
      }
    }
  }
}
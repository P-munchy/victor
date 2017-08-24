﻿using UnityEngine;
using Anki.Assets;

namespace Cozmo {
  namespace UI {
    public class CubePalette : ScriptableObject {
      private static CubePalette _sInstance;

      public static CubePalette Instance {
        get { return _sInstance; }
        private set {
          if (_sInstance == null) {
            _sInstance = value;
          }
        }
      }

      public static void LoadCubePalette(string assetBundleName) {
        AssetBundleManager.Instance.LoadAssetAsync(
          assetBundleName, "CubePalette", (CubePalette colorHolder) => {
            Instance = colorHolder;
          });
      }

      [SerializeField]
      private CubeColor _OffColor;
      public CubeColor OffColor { get { return _OffColor; } }

      [SerializeField]
      private CubeColor _InViewColor;
      public CubeColor InViewColor { get { return _InViewColor; } }

      [SerializeField]
      private CubeColor _OutOfViewColor;
      public CubeColor OutOfViewColor { get { return _OutOfViewColor; } }

      [SerializeField]
      private CubeColor _ReadyColor;
      public CubeColor ReadyColor { get { return _ReadyColor; } }

      [SerializeField]
      private CubeColor _ErrorColor;
      public CubeColor ErrorColor { get { return _ErrorColor; } }

      [SerializeField]
      private CubeCycleColors _TapMeColor;
      public CubeCycleColors TapMeColor { get { return _TapMeColor; } }

      [SerializeField]
      private CubeColor _CubeUprightColor;
      public CubeColor CubeUprightColor { get { return _CubeUprightColor; } }

      [SerializeField]
      private LightCubeSprite _TopDownLightCubeSpritePrefab;
      public LightCubeSprite TopDownLightCubeSpritePrefab { get { return _TopDownLightCubeSpritePrefab; } }

      [SerializeField]
      private LightCubeSprite _IsometricLightCubeSpritePrefab;
      public LightCubeSprite IsometricLightCubeSpritePrefab { get { return _IsometricLightCubeSpritePrefab; } }

      [SerializeField]
      private LightCubeSprite _UpsideDownLightCubeSpritePrefab;
      public LightCubeSprite UpsideDownLightCubeSpritePrefab { get { return _UpsideDownLightCubeSpritePrefab; } }

      [System.Serializable]
      public class CubeColor {
        public Color lightColor;
        public Color uiLightColor;
      }

      [System.Serializable]
      public class CubeCycleColors {
        public Color[] lightColors;
        public float cycleIntervalSeconds;
      }
    }
  }
}
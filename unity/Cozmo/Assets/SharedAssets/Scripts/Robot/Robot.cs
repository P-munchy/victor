using UnityEngine;
using System;
using System.Collections;
using System.Collections.Generic;
using Anki.Cozmo;
using Anki.Cozmo.ExternalInterface;
using Cozmo.Util;
using G2U = Anki.Cozmo.ExternalInterface;


/// <summary>
///   our unity side representation of cozmo's current state
///   also wraps most messages related solely to him
/// </summary>
public class Robot : IRobot {

  public class Light : ILight {
    private uint _LastOnColor;

    public uint OnColor { get; set; }

    private uint _LastOffColor;

    public uint OffColor { get; set; }

    private uint _LastOnPeriodMs;

    public uint OnPeriodMs { get; set; }

    private uint _LastOffPeriodMs;

    public uint OffPeriodMs { get; set; }

    private uint _LastTransitionOnPeriodMs;

    public uint TransitionOnPeriodMs { get; set; }

    private uint _LastTransitionOffPeriodMs;

    public uint TransitionOffPeriodMs { get; set; }

    public void SetLastInfo() {
      _LastOnColor = OnColor;
      _LastOffColor = OffColor;
      _LastOnPeriodMs = OnPeriodMs;
      _LastOffPeriodMs = OffPeriodMs;
      _LastTransitionOnPeriodMs = TransitionOnPeriodMs;
      _LastTransitionOffPeriodMs = TransitionOffPeriodMs;
    }

    public bool Changed {
      get {
        return _LastOnColor != OnColor || _LastOffColor != OffColor || _LastOnPeriodMs != OnPeriodMs || _LastOffPeriodMs != OffPeriodMs ||
        _LastTransitionOnPeriodMs != TransitionOnPeriodMs || _LastTransitionOffPeriodMs != TransitionOffPeriodMs;
      }
    }

    public virtual void ClearData() {
      OnColor = 0;
      OffColor = 0;
      OnPeriodMs = 1000;
      OffPeriodMs = 0;
      TransitionOnPeriodMs = 0;
      TransitionOffPeriodMs = 0;

      _LastOnColor = 0;
      _LastOffColor = 0;
      _LastOnPeriodMs = 1000;
      _LastOffPeriodMs = 0;
      _LastTransitionOnPeriodMs = 0;
      _LastTransitionOffPeriodMs = 0;

      MessageDelay = 0f;
    }

    public static float MessageDelay = 0f;

    public const uint FOREVER = uint.MaxValue;
  }

  private struct RobotCallbackWrapper {
    public readonly uint IdTag;

    private readonly RobotCallback _Callback;

    public RobotCallbackWrapper(uint idTag, RobotCallback callback) {
      IdTag = idTag;
      _Callback = callback;
    }

    public bool IsCallback(RobotCallback callback) {
      return _Callback == callback;
    }

    public void Invoke(bool success) {
      if (_Callback != null) {
        var target = _Callback.Target;

        // Unity Overrides the == operator to return null if the object has been
        // destroyed, but the callback will still be registered.
        // By casting to UnityEngine.Object, we can test if it has been destroyed
        // and not invoke the callback.
        if (target is UnityEngine.Object) {

          if (((UnityEngine.Object)target) == null) {
            DAS.Error(this, "Tried to invoke callback on destroyed object of type " + target.GetType().Name);
            return;
          }
        }

        _Callback(success);
      }
    }
  }

  #region consts

  public const float kDefaultRadPerSec = 4.3f;
  public const float kPanAccel_radPerSec2 = 10f;
  public const float kPanTolerance_rad = 5 * Mathf.Deg2Rad;

  #endregion

  public byte ID { get; private set; }

  // in radians
  public float HeadAngle { get; private set; }

  // robot yaw in radians, from negative PI to positive PI
  public float PoseAngle { get; private set; }

  public float PitchAngle { get; private set; }

  // in mm/s
  public float LeftWheelSpeed { get; private set; }

  // in mm/s
  public float RightWheelSpeed { get; private set; }

  // in mm
  public float LiftHeight { get; private set; }

  public float LiftHeightFactor { get { return (LiftHeight - CozmoUtil.kMinLiftHeightMM) / (CozmoUtil.kMaxLiftHeightMM - CozmoUtil.kMinLiftHeightMM); } }

  public Vector3 WorldPosition { get; private set; }

  public Quaternion Rotation { get; private set; }

  public Vector3 Forward { get { return Rotation * Vector3.right; } }

  public Vector3 Right { get { return Rotation * -Vector3.up; } }

  public RobotStatusFlag RobotStatus { get; private set; }

  public GameStatusFlag GameStatus { get; private set; }

  public float BatteryVoltage { get; private set; }

  public List<ObservedObject> VisibleObjects { get; private set; }

  public List<ObservedObject> SeenObjects { get; private set; }

  public List<ObservedObject> DirtyObjects { get; private set; }

  public Dictionary<int, LightCube> LightCubes { get; private set; }

  public List<Face> Faces { get; private set; }

  public Dictionary<int, string> EnrolledFaces { get; set; }

  public int FriendshipPoints { get; private set; }

  public int FriendshipLevel { get; private set; }

  public float[] EmotionValues { get; private set; }

  public ILight[] BackpackLights { get; private set; }

  public bool IsSparked { get; private set; }

  public Anki.Cozmo.UnlockId SparkUnlockId { get; private set; }

  private bool _LightsChanged {
    get {
      for (int i = 0; i < BackpackLights.Length; ++i) {
        if (BackpackLights[i].Changed)
          return true;
      }

      return false;
    }
  }

  private ObservedObject _targetLockedObject = null;

  public ObservedObject TargetLockedObject {
    get { return _targetLockedObject; }
    set {
      _targetLockedObject = value;
    }
  }

  private int _CarryingObjectID;

  public int CarryingObjectID {
    get { return _CarryingObjectID; }
    private set {
      if (_CarryingObjectID != value) {
        _CarryingObjectID = value;
        if (OnCarryingObjectSet != null) {
          OnCarryingObjectSet(CarryingObject);
        }
      }
    }
  }

  private int _HeadTrackingObjectID;

  public int HeadTrackingObjectID {
    get { return _HeadTrackingObjectID; }
    private set {
      if (_HeadTrackingObjectID != value) {
        _HeadTrackingObjectID = value;
        if (OnHeadTrackingObjectSet != null) {
          OnHeadTrackingObjectSet(HeadTrackingObject);
        }
      }
    }
  }

  private int _LastHeadTrackingObjectID;

  public string CurrentBehaviorString { get; set; }

  public string CurrentDebugAnimationString { get; set; }

  private PathMotionProfile PathMotionProfileDefault;

  private uint _LastIdTag;

  private ObservedObject _CarryingObject;

  public ObservedObject CarryingObject {
    get {
      if (_CarryingObject != CarryingObjectID)
        _CarryingObject = SeenObjects.Find(x => x == CarryingObjectID);

      return _CarryingObject;
    }
  }

  public event Action<ObservedObject> OnCarryingObjectSet;


  private ObservedObject _HeadTrackingObject;

  public ObservedObject HeadTrackingObject {
    get {
      if (_HeadTrackingObject != HeadTrackingObjectID)
        _HeadTrackingObject = SeenObjects.Find(x => x == HeadTrackingObjectID);

      return _HeadTrackingObject;
    }
  }

  public event Action<ObservedObject> OnHeadTrackingObjectSet;

  private readonly List<RobotCallbackWrapper> _RobotCallbacks = new List<RobotCallbackWrapper>();

  private float _LocalBusyTimer = 0f;
  private bool _LocalBusyOverride = false;

  public void SetLocalBusyTimer(float localBusyTimer) {
    _LocalBusyTimer = localBusyTimer;
  }

  public bool IsBusy {
    get {
      return _LocalBusyOverride
      || _LocalBusyTimer > 0f
      || Status(RobotStatusFlag.IS_PATHING)
      || (Status(RobotStatusFlag.IS_ANIMATING) && !Status(RobotStatusFlag.IS_ANIMATING_IDLE))
      || Status(RobotStatusFlag.IS_PICKED_UP);
    }

    set {
      _LocalBusyOverride = value;

      if (value) {
        DriveWheels(0, 0);
      }
    }
  }

  public bool Status(RobotStatusFlag s) {
    return (RobotStatus & s) == s;
  }

  public bool IsLocalized() {
    return (GameStatus & GameStatusFlag.IsLocalized) == GameStatusFlag.IsLocalized;
  }


  public Robot(byte robotID) : base() {
    ID = robotID;
    const int initialSize = 8;
    SeenObjects = new List<ObservedObject>(initialSize);
    VisibleObjects = new List<ObservedObject>(initialSize);
    DirtyObjects = new List<ObservedObject>(initialSize);
    LightCubes = new Dictionary<int, LightCube>();
    Faces = new List<global::Face>();
    EnrolledFaces = new Dictionary<int, string>();

    // Defaults in clad
    PathMotionProfileDefault = new PathMotionProfile();

    BackpackLights = new ILight[Singleton<SetBackpackLEDs>.Instance.onColor.Length];

    EmotionValues = new float[(int)Anki.Cozmo.EmotionType.Count];

    for (int i = 0; i < BackpackLights.Length; ++i) {
      BackpackLights[i] = new Light();
    }

    ClearData(true);

    RobotEngineManager.Instance.DisconnectedFromClient += Reset;

    RobotEngineManager.Instance.SuccessOrFailure += RobotEngineMessages;
    RobotEngineManager.Instance.OnEmotionRecieved += UpdateEmotionFromEngineRobotManager;
    RobotEngineManager.Instance.OnObjectConnectionState += ObjectConnectionState;
    RobotEngineManager.Instance.OnSparkUnlockEnded += SparkUnlockEnded;
    RobotEngineManager.Instance.OnRobotLoadedKnownFace += HandleRobotLoadedKnownFace;
  }

  public void Dispose() {
    RobotEngineManager.Instance.DisconnectedFromClient -= Reset;
    RobotEngineManager.Instance.SuccessOrFailure -= RobotEngineMessages;
    RobotEngineManager.Instance.OnObjectConnectionState -= ObjectConnectionState;
    RobotEngineManager.Instance.OnSparkUnlockEnded -= SparkUnlockEnded;
  }

  public void CooldownTimers(float delta) {
    if (_LocalBusyTimer > 0f) {
      _LocalBusyTimer -= delta;
    }
  }

  public Vector3 WorldToCozmo(Vector3 worldSpacePosition) {
    Vector3 offset = worldSpacePosition - this.WorldPosition;
    offset = Quaternion.Inverse(this.Rotation) * offset;
    return offset;
  }

  private void RobotEngineMessages(uint idTag, bool success, RobotActionType messageType) {
    DAS.Info("Robot.ActionCallback", "Type = " + messageType + " success = " + success);

    for (int i = 0; i < _RobotCallbacks.Count; ++i) {
      if (_RobotCallbacks[i].IdTag == idTag) {
        var callback = _RobotCallbacks[i];
        _RobotCallbacks.RemoveAt(i);
        callback.Invoke(success);
        break;
      }
    }
  }

  private uint GetNextIdTag() {
    return ++_LastIdTag;
  }

  private int SortByDistance(ObservedObject a, ObservedObject b) {
    float d1 = ((Vector2)a.WorldPosition - (Vector2)WorldPosition).sqrMagnitude;
    float d2 = ((Vector2)b.WorldPosition - (Vector2)WorldPosition).sqrMagnitude;
    return d1.CompareTo(d2);
  }

  private void Reset(DisconnectionReason reason = DisconnectionReason.None) {
    ClearData();
  }

  private void HandleRobotLoadedKnownFace(int faceID, string name) {
    EnrolledFaces.Add(faceID, name);
  }

  public bool IsLightCubeInPickupRange(LightCube lightCube) {
    var bounds = new Bounds(
                   new Vector3(CozmoUtil.kOriginToLowLiftDDistMM, 0, CozmoUtil.kBlockLengthMM * 0.5f),
                   Vector3.one * CozmoUtil.kBlockLengthMM);

    return bounds.Contains(WorldToCozmo(lightCube.WorldPosition));
  }

  public void ResetRobotState(Action onComplete) {
    DriveWheels(0.0f, 0.0f);
    TrackToObject(null);
    CancelAllCallbacks();
    SetEnableFreeplayBehaviorChooser(false);
    SetIdleAnimation("NONE");
    Anki.Cozmo.LiveIdleAnimationParameter[] paramNames = { };
    float[] paramValues = { };
    SetLiveIdleAnimationParameters(paramNames, paramValues, true);
    foreach (KeyValuePair<int, LightCube> kvp in LightCubes) {
      kvp.Value.SetLEDs(Color.black);
    }
    SetBackpackLEDs(Color.black.ToUInt());

    TryResetHeadAndLift(onComplete);
  }

  public void TryResetHeadAndLift(Action onComplete) {
    // if there is a light cube right in front of cozmo, lowering
    // the lift will cause him to lift up. Instead, rotate 90 degrees
    // and try lowering lift
    foreach (var lightCube in LightCubes.Values) {
      if (IsLightCubeInPickupRange(lightCube)) {
        TurnInPlace(Mathf.PI * 0.5f, 1000, 1000, (s) => TryResetHeadAndLift(onComplete));
        return;
      }
    }

    ResetLiftAndHead((s) => {
      if (onComplete != null) {
        onComplete();
      }
    });
  }

  public void ClearData(bool initializing = false) {
    if (!initializing) {
      TurnOffAllLights(true);
      DAS.Debug(this, "Robot data cleared");
    }

    SeenObjects.Clear();
    VisibleObjects.Clear();
    DirtyObjects.Clear();
    LightCubes.Clear();
    RobotStatus = RobotStatusFlag.NoneRobotStatusFlag;
    GameStatus = GameStatusFlag.Nothing;
    WorldPosition = Vector3.zero;
    Rotation = Quaternion.identity;
    CarryingObjectID = -1;
    HeadTrackingObjectID = -1;
    _LastHeadTrackingObjectID = -1;
    TargetLockedObject = null;
    _CarryingObject = null;
    _HeadTrackingObject = null;
    HeadAngle = float.MaxValue;
    PoseAngle = float.MaxValue;
    LeftWheelSpeed = float.MaxValue;
    RightWheelSpeed = float.MaxValue;
    LiftHeight = float.MaxValue;
    BatteryVoltage = float.MaxValue;
    _LocalBusyTimer = 0f;

    for (int i = 0; i < BackpackLights.Length; ++i) {
      BackpackLights[i].ClearData();
    }

  }

  public void ClearVisibleObjects() {
    for (int i = 0; i < VisibleObjects.Count; ++i) {
      if (VisibleObjects[i].TimeLastSeen + ObservedObject.kRemoveDelay < Time.time) {
        VisibleObjects.RemoveAt(i--);
      }
    }
  }

  public void UpdateInfo(G2U.RobotState message) {
    HeadAngle = message.headAngle_rad;
    PoseAngle = message.poseAngle_rad;
    PitchAngle = message.posePitch_rad;
    LeftWheelSpeed = message.leftWheelSpeed_mmps;
    RightWheelSpeed = message.rightWheelSpeed_mmps;
    LiftHeight = message.liftHeight_mm;
    RobotStatus = (RobotStatusFlag)message.status;
    GameStatus = (GameStatusFlag)message.gameStatus;
    BatteryVoltage = message.batteryVoltage;
    CarryingObjectID = message.carryingObjectID;
    HeadTrackingObjectID = message.headTrackingObjectID;

    WorldPosition = new Vector3(message.pose_x, message.pose_y, message.pose_z);
    Rotation = new Quaternion(message.pose_qx, message.pose_qy, message.pose_qz, message.pose_qw);

  }

  // Object moved, so remove it from the seen list
  // and add it into the dirty list.
  public void UpdateDirtyList(ObservedObject dirty) {
    SeenObjects.Remove(dirty);

    if (!DirtyObjects.Contains(dirty)) {
      DirtyObjects.Add(dirty);
    }
  }

  public LightCube GetLightCubeWithFactoryID(uint factoryID) {
    foreach (LightCube lc in LightCubes.Values) {
      if (lc.FactoryID == factoryID) {
        return lc;
      }
    }

    return null;
  }

  public ObservedObject GetObservedObjectWithFactoryID(uint factoryID) {
    return SeenObjects.Find(x => x.FactoryID == factoryID);
  }

  private void SendQueueSingleAction<T>(T action, RobotCallback callback, QueueActionPosition queueActionPosition) {
    var tag = GetNextIdTag();
    RobotEngineManager.Instance.Message.QueueSingleAction =
      Singleton<QueueSingleAction>.Instance.Initialize(robotID: ID,
      idTag: tag,
      numRetries: 0,
      position: queueActionPosition,
      actionState: action);
    RobotEngineManager.Instance.SendMessage();

    _RobotCallbacks.Add(new RobotCallbackWrapper(tag, callback));
  }


  #region Mood Stats

  public void VisualizeQuad(Vector3 lowerLeft, Vector3 upperRight) {

    RobotEngineManager.Instance.Message.VisualizeQuad =
      Singleton<VisualizeQuad>.Instance.Initialize(
      quadID: 0,
      color: Color.black.ToUInt(),
      xUpperLeft: lowerLeft.x,
      yUpperLeft: upperRight.y,
      // not sure how to play z. Assume its constant between lower left and upper right?
      zUpperLeft: lowerLeft.z,

      xLowerLeft: lowerLeft.x,
      yLowerLeft: lowerLeft.y,
      zLowerLeft: lowerLeft.z,

      xUpperRight: upperRight.x,
      yUpperRight: upperRight.y,
      zUpperRight: upperRight.z,

      xLowerRight: upperRight.x,
      yLowerRight: lowerLeft.y,
      zLowerRight: upperRight.z
    );
    RobotEngineManager.Instance.SendMessage();
  }

  // source is a identifier for where the emotion is being set so it can penalize the
  // amount affected if it's coming too often from the same source.
  public void AddToEmotion(Anki.Cozmo.EmotionType type, float deltaValue, string source) {

    RobotEngineManager.Instance.Message.MoodMessage =
      Singleton<MoodMessage>.Instance.Initialize(ID,
      Singleton<AddToEmotion>.Instance.Initialize(type, deltaValue, source));
    RobotEngineManager.Instance.SendMessage();
  }

  // Only debug panels should be using set.
  // You should not be calling this from a minigame/challenge.
  public void SetEmotion(Anki.Cozmo.EmotionType type, float value) {
    RobotEngineManager.Instance.Message.MoodMessage =
      Singleton<MoodMessage>.Instance.Initialize(
      ID,
      Singleton<SetEmotion>.Instance.Initialize(type, value));
    RobotEngineManager.Instance.SendMessage();
  }

  public void SetCalibrationData(float focalLengthX, float focalLengthY, float centerX, float centerY) {
    float[] dummyDistortionCoeffs = { 0, 0, 0, 0, 0, 0, 0, 0 };
    RobotEngineManager.Instance.Message.CameraCalibration = Singleton<CameraCalibration>.Instance.Initialize(focalLengthX, focalLengthY, centerX, centerY, 0.0f, 240, 320, dummyDistortionCoeffs);
    RobotEngineManager.Instance.SendMessage();
  }

  private void UpdateEmotionFromEngineRobotManager(Anki.Cozmo.EmotionType index, float value) {
    EmotionValues[(int)index] = value;
  }

  #endregion

  #region Behavior Manager


  #endregion

  public void ObjectConnectionState(Anki.Cozmo.ObjectConnectionState message) {
    DAS.Debug(this, "ObjectConnectionState. " + (message.connected ? "Connected " : "Disconnected ") + "object of type " + message.device_type.ToString() + " with ID " + message.objectID + " and factory ID " + message.factoryID.ToString("X"));

    if (message.connected) {
      // We are only interested on cubes and chargers for connection messages. 
      bool isCube = message.device_type == ActiveObjectType.OBJECT_CUBE1 || message.device_type == ActiveObjectType.OBJECT_CUBE2 || message.device_type == ActiveObjectType.OBJECT_CUBE3;
      bool isCharger = message.device_type == ActiveObjectType.OBJECT_CHARGER;
      if (isCube || isCharger) {

        // Find the correct object type given the information in the message.
        ObjectType objectType = ObjectType.Invalid;
        ObjectFamily objectFamily = ObjectFamily.Invalid;
        switch (message.device_type) {
        case ActiveObjectType.OBJECT_CUBE1:
          objectType = ObjectType.Block_LIGHTCUBE1;
          objectFamily = ObjectFamily.LightCube;
          break;

        case ActiveObjectType.OBJECT_CUBE2:
          objectType = ObjectType.Block_LIGHTCUBE2;
          objectFamily = ObjectFamily.LightCube;
          break;

        case ActiveObjectType.OBJECT_CUBE3:
          objectType = ObjectType.Block_LIGHTCUBE3;
          objectFamily = ObjectFamily.LightCube;
          break;

        case ActiveObjectType.OBJECT_CHARGER:
          objectType = ObjectType.Charger_Basic;
          objectFamily = ObjectFamily.Charger;
          break;
        }


        int objectID = (int)message.objectID;
        if (objectFamily == ObjectFamily.LightCube) {
          AddLightCube(objectID, message.factoryID, objectType);
        }
        else {
          AddObservedObject(objectID, message.factoryID, objectFamily, objectType);
        }
      }
      else {
        // This is not a cube or a charger so let's make sure it is not in our lists of objects
        DeleteObservedObject((int)message.objectID);
      }
    }
    else {
      // For disconnects, the robot doesn't send the device type so try to remove the object from all our lists
      DeleteObservedObject((int)message.objectID);
    }
  }

  public void RobotDeletedObject(G2U.RobotDeletedObject message) {
    DAS.Debug(this, "RobotDeletedObject", "Deleted ID " + message.objectID);

    DeleteObservedObject((int)message.objectID);
  }

  public void UpdateObservedObject(G2U.RobotObservedObject message) {
    //    DAS.Debug(this, "UpdateObservedObjectInfo. ", "Updating ID " + message.objectID);
    if (message.objectFamily == Anki.Cozmo.ObjectFamily.Mat) {
      DAS.Warn(this, "UpdateObservedObjectInfo received message about the Mat!");
      return;
    }

    if (message.objectFamily == Anki.Cozmo.ObjectFamily.LightCube) {
      LightCube lightCube = AddLightCube(message.objectID, 0, message.objectType);

      UpdateObservedObject(lightCube, message);
    }
    else {
      ObservedObject observedObject = AddObservedObject(message.objectID, 0, message.objectFamily, message.objectType);

      UpdateObservedObject(observedObject, message);
    }
  }

  public void RobotMarkedObjectPoseUnknown(G2U.RobotMarkedObjectPoseUnknown message) {
    DAS.Debug(this, "RobotMarkedObjectPoseUnknown", "Object ID " + message.objectID);

    DeleteObservedObject((int)message.objectID);
  }

  private void UpdateObservedObject(ObservedObject knownObject, G2U.RobotObservedObject message) {
    UnityEngine.Assertions.Assert.IsNotNull(knownObject);

    int index = DirtyObjects.FindIndex(x => x == knownObject.ID);
    if (index != -1) {
      DirtyObjects.RemoveAt(index);
    }

    knownObject.UpdateInfo(message);

    if (SeenObjects.Find(x => x == message.objectID) == null) {
      SeenObjects.Add(knownObject);
    }

    if (knownObject.MarkersVisible && VisibleObjects.Find(x => x == message.objectID) == null) {
      VisibleObjects.Add(knownObject);
    }
  }

  private LightCube AddLightCube(int objectID, uint factoryID, ObjectType objectType) {
    LightCube lightCube = null;
    if (!LightCubes.TryGetValue(objectID, out lightCube)) {
      lightCube = new LightCube(objectID, factoryID, ObjectFamily.LightCube, objectType);

      DAS.Debug(this, "Adding cube " + objectID + " factoryID " + factoryID.ToString("X") + " Objectype " + objectType.ToString());
      LightCubes.Add(objectID, lightCube);
      SeenObjects.Add(lightCube);
    }
    if ((lightCube.FactoryID == 0) && (factoryID != 0)) {
      // So far we received messages about the cube without a factory ID. This is because it was seen 
      // before we connected to it. This message has the factory ID so we need to update its information
      // so we can find it in the future
      DAS.Debug(this, "Updating cube " + objectID + " factoryID to " + factoryID.ToString("X"));
      lightCube.FactoryID = factoryID;
    }

    return lightCube;
  }

  private ObservedObject AddObservedObject(int objectID, uint factoryID, ObjectFamily objectFamily, ObjectType objectType) {
    ObservedObject observedObject = SeenObjects.Find(x => x == objectID);
    if (observedObject == null) {
      observedObject = new ObservedObject(objectID, factoryID, objectFamily, objectType);
      SeenObjects.Add(observedObject);
    }

    return observedObject;
  }

  private void DeleteObservedObject(int objectID) {
    DAS.Debug(this, "Deleting observed object " + objectID);
    LightCubes.Remove(objectID);

    int index = SeenObjects.FindIndex(x => x.ID == objectID);
    if (index != -1) {
      SeenObjects.RemoveAt(index);
    }

    index = DirtyObjects.FindIndex(x => x.ID == objectID);
    if (index != -1) {
      DirtyObjects.RemoveAt(index);
    }

    index = VisibleObjects.FindIndex(x => x.ID == objectID);
    if (index != -1) {
      VisibleObjects.RemoveAt(index);
    }
  }

  public void UpdateObservedFaceInfo(G2U.RobotObservedFace message) {
    //DAS.Debug ("Robot", "saw a face at " + message.faceID);
    Face face = Faces.Find(x => x.ID == message.faceID);
    AddObservedFace(face != null ? face : null, message);
  }

  public void DisplayProceduralFace(float faceAngle, Vector2 faceCenter, Vector2 faceScale, float[] leftEyeParams, float[] rightEyeParams) {

    RobotEngineManager.Instance.Message.DisplayProceduralFace =
      Singleton<DisplayProceduralFace>.Instance.Initialize(
      robotID: ID,
      faceAngle: faceAngle,
      faceCenX: faceCenter.x,
      faceCenY: faceCenter.y,
      faceScaleX: faceScale.x,
      faceScaleY: faceScale.y,
      leftEye: leftEyeParams,
      rightEye: rightEyeParams
    );
    RobotEngineManager.Instance.SendMessage();

  }

  private void AddObservedFace(Face faceObject, G2U.RobotObservedFace message) {
    if (faceObject == null) {
      faceObject = new Face(message);
      Faces.Add(faceObject);
    }
    else {
      faceObject.UpdateInfo(message);
    }
  }

  public void DriveWheels(float leftWheelSpeedMmps, float rightWheelSpeedMmps) {
    RobotEngineManager.Instance.Message.DriveWheels =
      Singleton<DriveWheels>.Instance.Initialize(leftWheelSpeedMmps, rightWheelSpeedMmps, 0, 0);
    RobotEngineManager.Instance.SendMessage();
  }

  public void PlaceObjectOnGroundHere(RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW) {
    DAS.Debug(this, "Place Object " + CarryingObject + " On Ground Here");

    SendQueueSingleAction(Singleton<PlaceObjectOnGroundHere>.Instance, callback, queueActionPosition);

    _LocalBusyTimer = CozmoUtil.kLocalBusyTime;
  }

  public void PlaceObjectRel(ObservedObject target, float offsetFromMarker, float approachAngle, RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW) {
    DAS.Debug(this, "PlaceObjectRel " + target.ID);

    SendQueueSingleAction(Singleton<PlaceRelObject>.Instance.Initialize(
      approachAngle_rad: approachAngle,
      placementOffsetX_mm: offsetFromMarker,
      objectID: target.ID,
      useApproachAngle: true,
      usePreDockPose: true,
      useManualSpeed: false,
      motionProf: PathMotionProfileDefault),
      callback,
      queueActionPosition);
  }

  public void PlaceOnObject(ObservedObject target, float approachAngle, RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW) {
    DAS.Debug(this, "PlaceOnObject " + target.ID);

    SendQueueSingleAction(Singleton<PlaceOnObject>.Instance.Initialize(
      approachAngle_rad: approachAngle,
      objectID: target.ID,
      useApproachAngle: true,
      usePreDockPose: true,
      useManualSpeed: false,
      motionProf: PathMotionProfileDefault
    ),
      callback,
      queueActionPosition);
  }

  public void CancelAction(RobotActionType actionType = RobotActionType.UNKNOWN) {
    DAS.Debug(this, "CancelAction actionType(" + actionType + ")");

    RobotEngineManager.Instance.Message.CancelAction = Singleton<CancelAction>.Instance.Initialize(actionType, ID);
    RobotEngineManager.Instance.SendMessage();
  }

  public void CancelCallback(RobotCallback callback) {
    for (int i = _RobotCallbacks.Count - 1; i >= 0; i--) {
      if (_RobotCallbacks[i].IsCallback(callback)) {
        _RobotCallbacks.RemoveAt(i);
      }
    }
  }

  public void CancelAllCallbacks() {
    _RobotCallbacks.Clear();
  }

  public void EnrollNamedFace(int faceID, string name, Anki.Cozmo.FaceEnrollmentSequence seq = Anki.Cozmo.FaceEnrollmentSequence.Default, bool saveToRobot = true, RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW) {

    DAS.Debug(this, "Sending EnrollNamedFace for ID=" + faceID + " with " + name);
    SendQueueSingleAction(Singleton<EnrollNamedFace>.Instance.Initialize(faceID, name, seq, saveToRobot), callback, queueActionPosition);
  }

  public void SendAnimation(string animName, RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW) {

    DAS.Debug(this, "Sending " + animName + " with " + 1 + " loop");
    SendQueueSingleAction(Singleton<PlayAnimation>.Instance.Initialize(ID, 1, animName), callback, queueActionPosition);
  }

  public void SendAnimationGroup(string animGroupName, RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW) {

    DAS.Debug(this, "Sending Group " + animGroupName + " with " + 1 + " loop");

    SendQueueSingleAction(Singleton<PlayAnimationGroup>.Instance.Initialize(ID, 1, animGroupName), callback, queueActionPosition);
  }

  public void SetIdleAnimation(string default_anim) {

    DAS.Debug(this, "Setting idle animation to " + default_anim);

    RobotEngineManager.Instance.Message.SetIdleAnimation = Singleton<SetIdleAnimation>.Instance.Initialize(ID, default_anim);
    RobotEngineManager.Instance.SendMessage();
  }

  public void SetLiveIdleAnimationParameters(Anki.Cozmo.LiveIdleAnimationParameter[] paramNames, float[] paramValues, bool setUnspecifiedToDefault = false) {

    RobotEngineManager.Instance.Message.SetLiveIdleAnimationParameters =
      Singleton<SetLiveIdleAnimationParameters>.Instance.Initialize(paramNames, paramValues, ID, setUnspecifiedToDefault);
    RobotEngineManager.Instance.SendMessage();

  }

  public void PushDrivingAnimations(string drivingStartAnim, string drivingLoopAnim, string drivingEndAnim) {
    RobotEngineManager.Instance.Message.PushDrivingAnimations =
      Singleton<PushDrivingAnimations>.Instance.Initialize(drivingStartAnim, drivingLoopAnim, drivingEndAnim);
    RobotEngineManager.Instance.SendMessage();
  }

  public void PopDrivingAnimations() {
    RobotEngineManager.Instance.Message.PopDrivingAnimations = Singleton<PopDrivingAnimations>.Instance;
    RobotEngineManager.Instance.SendMessage();
  }

  public float GetHeadAngleFactor() {

    float angle = HeadAngle;

    if (angle >= 0f) {
      angle = Mathf.Lerp(0f, 1f, angle / (CozmoUtil.kMaxHeadAngle * Mathf.Deg2Rad));
    }
    else {
      angle = Mathf.Lerp(0f, -1f, angle / (CozmoUtil.kMinHeadAngle * Mathf.Deg2Rad));
    }

    return angle;
  }

  /// <summary>
  /// Sets the head angle.
  /// </summary>
  /// <param name="angleFactor">Angle factor.</param> usually from -1 (MIN_HEAD_ANGLE) to 1 (kMaxHeadAngle)
  /// <param name="useExactAngle">If set to <c>true</c> angleFactor is treated as an exact angle in radians.</param>
  public void SetHeadAngle(float angleFactor = -0.8f,
                           RobotCallback callback = null,
                           QueueActionPosition queueActionPosition = QueueActionPosition.NOW,
                           bool useExactAngle = false,
                           float accelRadSec = 2f,
                           float maxSpeedFactor = 1f) {

    float radians = angleFactor;

    if (!useExactAngle) {
      if (angleFactor >= 0f) {
        radians = Mathf.Lerp(0f, CozmoUtil.kMaxHeadAngle * Mathf.Deg2Rad, angleFactor);
      }
      else {
        radians = Mathf.Lerp(0f, CozmoUtil.kMinHeadAngle * Mathf.Deg2Rad, -angleFactor);
      }
    }

    if (HeadTrackingObject == -1 && Mathf.Abs(radians - HeadAngle) < 0.001f && queueActionPosition == QueueActionPosition.NOW) {
      if (callback != null) {
        callback(true);
      }
      return;
    }

    SendQueueSingleAction(
      Singleton<SetHeadAngle>.Instance.Initialize(
        radians,
        maxSpeedFactor * CozmoUtil.kMaxSpeedRadPerSec,
        accelRadSec,
        0),
      callback,
      queueActionPosition);
  }

  public void SetRobotVolume(float volume) {
    DAS.Debug(this, "Set Robot Volume " + volume);

    RobotEngineManager.Instance.Message.SetRobotVolume = Singleton<SetRobotVolume>.Instance.Initialize(ID, volume);
    RobotEngineManager.Instance.SendMessage();
  }

  public float GetRobotVolume() {
    float volume = 1f;
    Dictionary<Anki.Cozmo.Audio.VolumeParameters.VolumeType, float> currentVolumePrefs = DataPersistence.DataPersistenceManager.Instance.Data.DefaultProfile.VolumePreferences;
    currentVolumePrefs.TryGetValue(Anki.Cozmo.Audio.VolumeParameters.VolumeType.Robot, out volume);
    return volume;
  }

  public void TrackToObject(ObservedObject observedObject, bool headOnly = true) {
    if (HeadTrackingObjectID == observedObject && _LastHeadTrackingObjectID == observedObject) {
      return;
    }

    _LastHeadTrackingObjectID = observedObject;

    uint objId;
    if (observedObject != null) {
      objId = (uint)observedObject;
    }
    else {
      objId = uint.MaxValue; //cancels tracking
    }

    DAS.Debug(this, "Track Head To Object " + objId);

    RobotEngineManager.Instance.Message.TrackToObject =
      Singleton<TrackToObject>.Instance.Initialize(objId, ID, headOnly);
    RobotEngineManager.Instance.SendMessage();

  }

  public ObservedObject GetCharger() {
    return SeenObjects.Find(x => x.Family == ObjectFamily.Charger);
  }

  public void MountCharger(ObservedObject charger, RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW) {
    SendQueueSingleAction(
      Singleton<MountCharger>.Instance.Initialize(
        charger.ID, PathMotionProfileDefault, false, false),
      callback, queueActionPosition
    );
  }

  public void StopTrackToObject() {
    TrackToObject(null);
  }

  public void TurnTowardsObject(ObservedObject observedObject, bool headTrackWhenDone = true, float maxPanSpeed_radPerSec = kDefaultRadPerSec, float panAccel_radPerSec2 = kPanAccel_radPerSec2,
                                RobotCallback callback = null,
                                QueueActionPosition queueActionPosition = QueueActionPosition.NOW) {

    DAS.Debug(this, "Face Object " + observedObject);

    SendQueueSingleAction(Singleton<TurnTowardsObject>.Instance.Initialize(
      objectID: observedObject,
      robotID: ID,
      maxTurnAngle: float.MaxValue,
      panTolerance_rad: kPanTolerance_rad, // 1.7 degrees is the minimum in the engine
      headTrackWhenDone: headTrackWhenDone,
      maxPanSpeed_radPerSec: maxPanSpeed_radPerSec,
      panAccel_radPerSec2: panAccel_radPerSec2,
      maxTiltSpeed_radPerSec: 0f,
      tiltAccel_radPerSec2: 0f,
      tiltTolerance_rad: 0f,
      visuallyVerifyWhenDone: false
    ),
      callback,
      queueActionPosition);

  }

  public void TurnTowardsFacePose(Face face, float maxPanSpeed_radPerSec = kDefaultRadPerSec, float panAccel_radPerSec2 = kPanAccel_radPerSec2,
                                  RobotCallback callback = null,
                                  QueueActionPosition queueActionPosition = QueueActionPosition.NOW) {

    SendQueueSingleAction(
      Singleton<TurnTowardsPose>.Instance.Initialize(
        world_x: face.WorldPosition.x,
        world_y: face.WorldPosition.y,
        world_z: face.WorldPosition.z,
        maxTurnAngle: float.MaxValue,
        maxPanSpeed_radPerSec: maxPanSpeed_radPerSec,
        panAccel_radPerSec2: panAccel_radPerSec2,
        panTolerance_rad: kPanTolerance_rad, // 1.7 degrees is the minimum in the engine
        maxTiltSpeed_radPerSec: 0f,
        tiltAccel_radPerSec2: 0f,
        tiltTolerance_rad: 0f,
        robotID: ID
      ),
      callback,
      queueActionPosition);

    RobotEngineManager.Instance.SendMessage();
  }

  // Turns towards the last seen face, but not any more than the specified maxTurnAngle
  public void TurnTowardsLastFacePose(float maxTurnAngle, bool sayName = false, RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW) {
    DAS.Debug(this, "TurnTowardsLastFacePose with maxTurnAngle : " + maxTurnAngle);

    SendQueueSingleAction(Singleton<TurnTowardsLastFacePose>.Instance.Initialize(
      maxTurnAngle: maxTurnAngle,
      maxPanSpeed_radPerSec: kDefaultRadPerSec,
      panAccel_radPerSec2: kPanAccel_radPerSec2,
      panTolerance_rad: kPanTolerance_rad,
      maxTiltSpeed_radPerSec: 0f,
      tiltAccel_radPerSec2: 0f,
      tiltTolerance_rad: 0f,
      sayName: sayName,
      robotID: ID
    ),
      callback,
      queueActionPosition);
  }

  public void PickupObject(ObservedObject selectedObject, bool usePreDockPose = true, bool useManualSpeed = false, bool useApproachAngle = false, float approachAngleRad = 0.0f, RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW) {

    DAS.Debug(this, "Pick And Place Object " + selectedObject + " usePreDockPose " + usePreDockPose + " useManualSpeed " + useManualSpeed);

    SendQueueSingleAction(
      Singleton<PickupObject>.Instance.Initialize(
        objectID: selectedObject,
        motionProf: PathMotionProfileDefault,
        approachAngle_rad: approachAngleRad,
        useApproachAngle: useApproachAngle,
        useManualSpeed: useManualSpeed,
        usePreDockPose: usePreDockPose),
      callback,
      queueActionPosition);

    _LocalBusyTimer = CozmoUtil.kLocalBusyTime;
  }

  public void RollObject(ObservedObject selectedObject, bool usePreDockPose = true, bool useManualSpeed = false, RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW) {

    DAS.Debug(this, "Roll Object " + selectedObject + " usePreDockPose " + usePreDockPose + " useManualSpeed " + usePreDockPose);

    SendQueueSingleAction(Singleton<RollObject>.Instance.Initialize(
      selectedObject,
      PathMotionProfileDefault,
      approachAngle_rad: 0f,
      useApproachAngle: false,
      usePreDockPose: usePreDockPose,
      useManualSpeed: useManualSpeed
    ),
      callback,
      queueActionPosition);

    _LocalBusyTimer = CozmoUtil.kLocalBusyTime;
  }

  public void PlaceObjectOnGround(Vector3 position, Quaternion rotation, bool level = false, bool useManualSpeed = false, RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW) {

    DAS.Debug(this, "Drop Object At Pose " + position + " useManualSpeed " + useManualSpeed);


    SendQueueSingleAction(Singleton<PlaceObjectOnGround>.Instance.Initialize(
      x_mm: position.x,
      y_mm: position.y,
      qw: rotation.w,
      qx: rotation.x,
      qy: rotation.y,
      qz: rotation.z,
      motionProf: PathMotionProfileDefault,
      level: System.Convert.ToByte(level),
      useManualSpeed: useManualSpeed,
      useExactRotation: false,
      checkDestinationFree: false
    ),
      callback,
      queueActionPosition);

    _LocalBusyTimer = CozmoUtil.kLocalBusyTime;
  }

  public void GotoPose(Vector3 position, Quaternion rotation, bool level = false, bool useManualSpeed = false, RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW) {
    float rotationAngle = rotation.eulerAngles.z * Mathf.Deg2Rad;
    GotoPose(position.x, position.y, rotationAngle, level, useManualSpeed, callback, queueActionPosition);
  }

  public void GotoPose(float x_mm, float y_mm, float rad, bool level = false, bool useManualSpeed = false, RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW) {
    DAS.Debug(this, "Go to Pose: x: " + x_mm + " y: " + y_mm + " rad: " + rad);

    SendQueueSingleAction(Singleton<GotoPose>.Instance.Initialize(
      level: System.Convert.ToByte(level),
      useManualSpeed: useManualSpeed,
      x_mm: x_mm,
      y_mm: y_mm,
      rad: rad,
      motionProf: PathMotionProfileDefault
    ),
      callback,
      queueActionPosition);

    _LocalBusyTimer = CozmoUtil.kLocalBusyTime;
  }

  public void GotoObject(ObservedObject obj, float distance_mm, bool goToPreDockPose = false, RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW) {
    SendQueueSingleAction(Singleton<GotoObject>.Instance.Initialize(
      objectID: obj,
      distanceFromObjectOrigin_mm: distance_mm,
      useManualSpeed: false,
      usePreDockPose: goToPreDockPose,
      motionProf: PathMotionProfileDefault
    ),
      callback,
      queueActionPosition);

    _LocalBusyTimer = CozmoUtil.kLocalBusyTime;
  }

  public void AlignWithObject(ObservedObject obj, float distanceFromMarker_mm, RobotCallback callback = null, bool useApproachAngle = false, bool usePreDockPose = false, float approachAngleRad = 0.0f, AlignmentType alignmentType = AlignmentType.CUSTOM, QueueActionPosition queueActionPosition = QueueActionPosition.NOW) {
    SendQueueSingleAction(
      Singleton<AlignWithObject>.Instance.Initialize(
        objectID: obj,
        motionProf: PathMotionProfileDefault,
        distanceFromMarker_mm: distanceFromMarker_mm,
        approachAngle_rad: approachAngleRad,
        alignmentType: alignmentType,
        useApproachAngle: useApproachAngle,
        usePreDockPose: usePreDockPose,
        useManualSpeed: false
      ),
      callback,
      queueActionPosition);

    _LocalBusyTimer = CozmoUtil.kLocalBusyTime;
  }

  public void SearchForCube(LightCube cube, RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW) {
    SearchForObject(cube.Family, cube.ID, false, callback, queueActionPosition);
  }

  public void SearchForObject(ObjectFamily objectFamily, int objectId, bool matchAnyObject, RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW) {
    SendQueueSingleAction(
      Singleton<SearchForObject>.Instance.Initialize(
        desiredObjectFamily: objectFamily,
        desiredObjectID: objectId,
        matchAnyObject: matchAnyObject
      ),
      callback,
      queueActionPosition);
  }

  public void ResetLiftAndHead(RobotCallback callback = null) {
    bool headDone = false;
    bool liftDone = false;

    SetLiftHeight(0.0f, (s) => {
      liftDone = true;
      CheckLiftHeadCallback(headDone, liftDone, s, callback);
    }, QueueActionPosition.IN_PARALLEL);

    SetHeadAngle(0.0f, (s) => {
      headDone = true;
      CheckLiftHeadCallback(headDone, liftDone, s, callback);
    }, QueueActionPosition.IN_PARALLEL);
  }

  private void CheckLiftHeadCallback(bool headDone, bool liftDone, bool success, RobotCallback callback) {
    if (headDone && liftDone) {
      callback(success);
    }
  }

  // Height factor should be between 0.0f and 1.0f
  // 0.0f being lowest and 1.0f being highest.
  public void SetLiftHeight(float heightFactor, RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW) {
    DAS.Debug(this, "SetLiftHeight: " + heightFactor);
    if (LiftHeightFactor == heightFactor && queueActionPosition == QueueActionPosition.NOW) {
      if (callback != null) {
        callback(true);
      }
      return;
    }


    SendQueueSingleAction(Singleton<SetLiftHeight>.Instance.Initialize(
      height_mm: (heightFactor * (CozmoUtil.kMaxLiftHeightMM - CozmoUtil.kMinLiftHeightMM)) + CozmoUtil.kMinLiftHeightMM,
      accel_rad_per_sec2: 5f,
      max_speed_rad_per_sec: 10f,
      duration_sec: 0f
    ),
      callback,
      queueActionPosition);
  }

  public void SetRobotCarryingObject(int objectID = -1) {
    DAS.Debug(this, "Set Robot Carrying Object: " + objectID);

    RobotEngineManager.Instance.Message.SetRobotCarryingObject =
      Singleton<SetRobotCarryingObject>.Instance.Initialize(objectID, ID);
    RobotEngineManager.Instance.SendMessage();

    TargetLockedObject = null;

    SetLiftHeight(0f);
    SetHeadAngle();
  }

  public void ClearAllBlocks() {
    DAS.Debug(this, "Clear All Blocks");
    RobotEngineManager.Instance.Message.ClearAllBlocks = Singleton<ClearAllBlocks>.Instance.Initialize(ID);
    RobotEngineManager.Instance.SendMessage();
    Reset();

    SetLiftHeight(0f);
    SetHeadAngle();
  }

  public void ClearAllObjects() {
    RobotEngineManager.Instance.Message.ClearAllObjects = Singleton<ClearAllObjects>.Instance.Initialize(ID);
    RobotEngineManager.Instance.SendMessage();
    Reset();
  }

  public void VisionWhileMoving(bool enable) {
    RobotEngineManager.Instance.Message.VisionWhileMoving = Singleton<VisionWhileMoving>.Instance.Initialize(System.Convert.ToByte(enable));
    RobotEngineManager.Instance.SendMessage();
  }

  public void SetEnableCliffSensor(bool enabled) {
    RobotEngineManager.Instance.Message.EnableCliffSensor = Singleton<EnableCliffSensor>.Instance.Initialize(enabled);
    RobotEngineManager.Instance.SendMessage();
  }

  public void EnableSparkUnlock(Anki.Cozmo.UnlockId id) {
    IsSparked = (id != UnlockId.Count);
    SparkUnlockId = id;

    RobotEngineManager.Instance.Message.BehaviorManagerMessage =
      Singleton<BehaviorManagerMessage>.Instance.Initialize(
      ID,
      Singleton<SetActiveSpark>.Instance.Initialize(SparkUnlockId)
    );
    RobotEngineManager.Instance.SendMessage();
  }

  public void StopSparkUnlock() {
    EnableSparkUnlock(UnlockId.Count);
  }

  private void SparkUnlockEnded() {
    IsSparked = false;
    SparkUnlockId = UnlockId.Count;
  }

  // enable/disable games available for Cozmo to request
  public void SetAvailableGames(BehaviorGameFlag games) {
    RobotEngineManager.Instance.Message.BehaviorManagerMessage =
      Singleton<BehaviorManagerMessage>.Instance.Initialize(
      ID,
      Singleton<SetAvailableGames>.Instance.Initialize(games)
    );
    RobotEngineManager.Instance.SendMessage();
  }

  public void TurnInPlace(float angle_rad, float speed_rad_per_sec, float accel_rad_per_sec2, RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW) {

    SendQueueSingleAction(Singleton<TurnInPlace>.Instance.Initialize(
      angle_rad,
      speed_rad_per_sec,
      accel_rad_per_sec2,
      0,
      ID
    ),
      callback,
      queueActionPosition);
  }

  public void TraverseObject(int objectID, bool usePreDockPose = false, bool useManualSpeed = false) {
    DAS.Debug(this, "Traverse Object " + objectID + " useManualSpeed " + useManualSpeed + " usePreDockPose " + usePreDockPose);

    RobotEngineManager.Instance.Message.TraverseObject =
      Singleton<TraverseObject>.Instance.Initialize(PathMotionProfileDefault, usePreDockPose, useManualSpeed);
    RobotEngineManager.Instance.SendMessage();

    _LocalBusyTimer = CozmoUtil.kLocalBusyTime;
  }

  public void SetVisionMode(VisionMode mode, bool enable) {

    RobotEngineManager.Instance.Message.EnableVisionMode = Singleton<EnableVisionMode>.Instance.Initialize(mode, enable);
    RobotEngineManager.Instance.SendMessage();
  }

  public void RequestSetUnlock(Anki.Cozmo.UnlockId unlockID, bool unlocked) {
    RobotEngineManager.Instance.Message.RequestSetUnlock = Singleton<RequestSetUnlock>.Instance.Initialize(unlockID, unlocked);
    RobotEngineManager.Instance.SendMessage();
  }

  public void SetEnableSOSLogging(bool enable) {
    DAS.Debug(this, "Set enable SOS Logging: " + enable);
    RobotEngineManager.Instance.Message.SetEnableSOSLogging = Singleton<SetEnableSOSLogging>.Instance.Initialize(enable);
    RobotEngineManager.Instance.SendMessage();
  }

  public void ExecuteBehavior(BehaviorType type) {
    DAS.Debug(this, "Execute Behavior " + type);

    RobotEngineManager.Instance.Message.ExecuteBehavior = Singleton<ExecuteBehavior>.Instance.Initialize(type);
    RobotEngineManager.Instance.SendMessage();
  }

  public void SetEnableFreeplayBehaviorChooser(bool enable) {
    if (enable) {
      ActivateBehaviorChooser(Anki.Cozmo.BehaviorChooserType.Freeplay);
    }
    else {
      ActivateBehaviorChooser(Anki.Cozmo.BehaviorChooserType.Selection);
      ExecuteBehavior(Anki.Cozmo.BehaviorType.NoneBehavior);
    }
  }

  public void ActivateBehaviorChooser(BehaviorChooserType behaviorChooserType) {
    DAS.Debug(this, "ActivateBehaviorChooser: " + behaviorChooserType);

    RobotEngineManager.Instance.Message.ActivateBehaviorChooser = Singleton<ActivateBehaviorChooser>.Instance.Initialize(behaviorChooserType);
    RobotEngineManager.Instance.SendMessage();
  }

  #region Light manipulation

  #region Backpack Lights

  public void TurnOffBackpackLED(LEDId ledToChange) {
    SetBackpackLED((int)ledToChange, 0);
  }

  public void SetAllBackpackBarLED(Color color) {
    SetBackpackBarLED(LEDId.LED_BACKPACK_BACK, color);
    SetBackpackBarLED(LEDId.LED_BACKPACK_MIDDLE, color);
    SetBackpackBarLED(LEDId.LED_BACKPACK_FRONT, color);
  }

  public void SetAllBackpackBarLED(uint colorUint) {
    SetBackpackLED((int)LEDId.LED_BACKPACK_BACK, colorUint);
    SetBackpackLED((int)LEDId.LED_BACKPACK_MIDDLE, colorUint);
    SetBackpackLED((int)LEDId.LED_BACKPACK_FRONT, colorUint);
  }

  public void TurnOffAllBackpackBarLED() {
    SetBackpackLED((int)LEDId.LED_BACKPACK_BACK, 0);
    SetBackpackLED((int)LEDId.LED_BACKPACK_MIDDLE, 0);
    SetBackpackLED((int)LEDId.LED_BACKPACK_FRONT, 0);
  }

  public void SetBackpackBarLED(LEDId ledToChange, Color color) {
    if (ledToChange == LEDId.LED_BACKPACK_LEFT || ledToChange == LEDId.LED_BACKPACK_RIGHT) {
      DAS.Warn("Robot", "BackpackLighting - LEDId.LED_BACKPACK_LEFT or LEDId.LED_BACKPACK_RIGHT " +
      "does not have the full range of color. Taking the highest RGB value to use as the light intensity.");
      float highestIntensity = Mathf.Max(color.r, Mathf.Max(color.g, color.b));
      SetBackbackArrowLED(ledToChange, highestIntensity);
    }
    else {
      uint colorUint = color.ToUInt();
      SetBackpackLED((int)ledToChange, colorUint);
    }
  }

  public void SetBackbackArrowLED(LEDId ledId, float intensity) {
    intensity = Mathf.Clamp(intensity, 0f, 1f);
    Color color = new Color(intensity, intensity, intensity);
    uint colorUint = color.ToUInt();
    SetBackpackLED((int)ledId, colorUint);
  }

  public void SetFlashingBackpackLED(LEDId ledToChange, Color color, uint onDurationMs = 200, uint offDurationMs = 200, uint transitionDurationMs = 0) {
    uint colorUint = color.ToUInt();
    SetBackpackLED((int)ledToChange, colorUint, 0, onDurationMs, offDurationMs, transitionDurationMs, transitionDurationMs);
  }

  private void SetBackpackLEDs(uint onColor = 0, uint offColor = 0, uint onPeriod_ms = Light.FOREVER, uint offPeriod_ms = 0,
                               uint transitionOnPeriod_ms = 0, uint transitionOffPeriod_ms = 0) {
    for (int i = 0; i < BackpackLights.Length; ++i) {
      SetBackpackLED(i, onColor, offColor, onPeriod_ms, offPeriod_ms, transitionOnPeriod_ms, transitionOffPeriod_ms);
    }
  }

  private void SetBackpackLED(int index, uint onColor = 0, uint offColor = 0, uint onPeriod_ms = Light.FOREVER, uint offPeriod_ms = 0,
                              uint transitionOnPeriod_ms = 0, uint transitionOffPeriod_ms = 0) {
    // Special case for arrow lights; they only accept red as a color
    if (index == (int)LEDId.LED_BACKPACK_LEFT || index == (int)LEDId.LED_BACKPACK_RIGHT) {
      //uint whiteUint = Color.white.ToUInt();
      //onColor = (onColor > 0) ? whiteUint : 0;
      //offColor = (offColor > 0) ? whiteUint : 0;
    }

    BackpackLights[index].OnColor = onColor;
    BackpackLights[index].OffColor = offColor;
    BackpackLights[index].OnPeriodMs = onPeriod_ms;
    BackpackLights[index].OffPeriodMs = offPeriod_ms;
    BackpackLights[index].TransitionOnPeriodMs = transitionOnPeriod_ms;
    BackpackLights[index].TransitionOffPeriodMs = transitionOffPeriod_ms;

    if (index == (int)LEDId.LED_BACKPACK_RIGHT) {

    }
  }

  // should only be called from update loop
  private void SetAllBackpackLEDs() {

    Singleton<SetBackpackLEDs>.Instance.robotID = ID;
    for (int i = 0; i < BackpackLights.Length; i++) {
      Singleton<SetBackpackLEDs>.Instance.onColor[i] = BackpackLights[i].OnColor;
      Singleton<SetBackpackLEDs>.Instance.offColor[i] = BackpackLights[i].OffColor;
      Singleton<SetBackpackLEDs>.Instance.onPeriod_ms[i] = BackpackLights[i].OnPeriodMs;
      Singleton<SetBackpackLEDs>.Instance.offPeriod_ms[i] = BackpackLights[i].OffPeriodMs;
      Singleton<SetBackpackLEDs>.Instance.transitionOnPeriod_ms[i] = BackpackLights[i].TransitionOnPeriodMs;
      Singleton<SetBackpackLEDs>.Instance.transitionOffPeriod_ms[i] = BackpackLights[i].TransitionOffPeriodMs;
    }

    RobotEngineManager.Instance.Message.SetBackpackLEDs = Singleton<SetBackpackLEDs>.Instance;
    RobotEngineManager.Instance.SendMessage();

    SetLastLEDs();
  }

  private void SetLastLEDs() {
    for (int i = 0; i < BackpackLights.Length; ++i) {
      BackpackLights[i].SetLastInfo();
    }
  }

  #endregion

  public void TurnOffAllLights(bool now = false) {
    var enumerator = LightCubes.GetEnumerator();

    while (enumerator.MoveNext()) {
      LightCube lightCube = enumerator.Current.Value;

      lightCube.SetLEDs();
    }

    SetBackpackLEDs();

    if (now)
      UpdateLightMessages(now);
  }

  public void UpdateLightMessages(bool now = false) {
    if (RobotEngineManager.Instance == null || !RobotEngineManager.Instance.IsConnected)
      return;

    if (Time.time > LightCube.Light.MessageDelay || now) {
      var enumerator = LightCubes.GetEnumerator();

      while (enumerator.MoveNext()) {
        LightCube lightCube = enumerator.Current.Value;

        if (lightCube.LightsChanged)
          lightCube.SetAllLEDs();
      }
    }

    if (Time.time > Light.MessageDelay || now) {
      if (_LightsChanged)
        SetAllBackpackLEDs();
    }
  }

  #endregion

  #region PressDemoMessages

  public void TransitionToNextDemoState() {
    RobotEngineManager.Instance.Message.TransitionToNextDemoState = Singleton<TransitionToNextDemoState>.Instance;
    RobotEngineManager.Instance.SendMessage();
  }

  public void StartDemoWithEdge(bool demoWithEdge) {
    RobotEngineManager.Instance.Message.StartDemoWithEdge = Singleton<StartDemoWithEdge>.Instance.Initialize(demoWithEdge);
    RobotEngineManager.Instance.SendMessage();
  }

  #endregion

  public void SayTextWithEvent(string text, GameEvent playEvent, SayTextStyle style = SayTextStyle.Normal, RobotCallback callback = null, QueueActionPosition queueActionPosition = QueueActionPosition.NOW) {
    DAS.Debug(this, "Saying text: " + text);
    SendQueueSingleAction(Singleton<SayText>.Instance.Initialize(text, playEvent, style), callback, queueActionPosition);
  }

  public void SendDemoResetState() {
    RobotEngineManager.Instance.Message.DemoResetState = Singleton<DemoResetState>.Instance;
    RobotEngineManager.Instance.SendMessage();
  }

  public void EraseAllEnrolledFaces() {
    RobotEngineManager.Instance.Message.EraseAllEnrolledFaces = Singleton<EraseAllEnrolledFaces>.Instance;
    RobotEngineManager.Instance.SendMessage();
  }

  public void UpdateEnrolledFaceByID(int faceID, string oldFaceName, string newFaceName) {
    RobotEngineManager.Instance.Message.UpdateEnrolledFaceByID = Singleton<UpdateEnrolledFaceByID>.Instance.Initialize(faceID, oldFaceName, newFaceName);
    RobotEngineManager.Instance.SendMessage();
  }

  public void LoadFaceAlbumFromFile(string path, bool isPathRelative = true) {
    RobotEngineManager.Instance.Message.LoadFaceAlbumFromFile = Singleton<LoadFaceAlbumFromFile>.Instance.Initialize(path, isPathRelative);
    RobotEngineManager.Instance.SendMessage();
  }
}

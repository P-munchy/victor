using Anki.Cozmo.ExternalInterface;
using System.Collections.Generic;
using UnityEngine;
namespace CodeLab {

  public class CubeStateForCodeLab {
    public Vector3 pos;
    public bool isValid;
  }

  public class FaceStateForCodeLab {
    public Vector3 pos;
    public Vector2 camPos;
    public string name;
    public bool isVisible;
  }

  public class CozmoStateForCodeLab {
    public float poseAngle_d;
    public Vector3 pos;
    public CubeStateForCodeLab cube1 = new CubeStateForCodeLab();
    public CubeStateForCodeLab cube2 = new CubeStateForCodeLab();
    public CubeStateForCodeLab cube3 = new CubeStateForCodeLab();
    public FaceStateForCodeLab face = new FaceStateForCodeLab();
  }

  public class ScratchRequest {
    public int requestId { get; set; }
    public string command { get; set; }
    public string argString { get; set; }
    public string argUUID { get; set; }
    public int argInt { get; set; }
    public uint argUInt { get; set; }
    public float argFloat { get; set; }
    public float argFloat2 { get; set; }
    public float argFloat3 { get; set; }
  }

  public class InProgressScratchBlock {
    private int _RequestId;
    private WebViewObject _WebViewObjectComponent;

    public void Init(int requestId = -1, WebViewObject webViewObjectComponent = null) {
      _RequestId = requestId;
      _WebViewObjectComponent = webViewObjectComponent;
    }

    public void NeutralFaceThenAdvanceToNextBlock(bool success) {
      // Failure is usually because another action (e.g. animation) was requested by user clicking in Scratch
      // Therefore don't queue the neutral face animation in that case, as it will clobber the just requested animation
      if (success) {
        RobotEngineManager.Instance.CurrentRobot.SendAnimationTrigger(Anki.Cozmo.AnimationTrigger.NeutralFace, this.AdvanceToNextBlock);
      }
      else {
        AdvanceToNextBlock(success);
      }
    }

    public void OnReleased() {
      ResolveRequestPromise();
      RemoveAllCallbacks();
      Init();
    }

    public void CompletedTurn(bool success) {
      AdvanceToNextBlock(success);
    }

    public void ReleaseFromPool() {
      InProgressScratchBlockPool.ReleaseInProgressScratchBlock(this);
    }

    private void ResolveRequestPromise() {
      if (this._RequestId >= 0) {
        // Calls the JavaScript function resolving the Promise on the block
        _WebViewObjectComponent.EvaluateJS(@"window.resolveCommands[" + this._RequestId + "]();");
        this._RequestId = -1;
      }
    }

    public void AdvanceToNextBlock(bool success) {
      ResolveRequestPromise();
      ReleaseFromPool();
    }

    public void CubeTapped(int id, int tappedTimes, float timeStamp) {
      LightCube.TappedAction -= CubeTapped;
      AdvanceToNextBlock(true);
    }

    public void RemoveAllCallbacks() {
      // Just attempt to remove all callbacks rather than track the ones added
      var rEM = RobotEngineManager.Instance;
      rEM.RemoveCallback<RobotObservedFace>(RobotObservedFace);
      rEM.RemoveCallback<RobotObservedFace>(RobotObservedHappyFace);
      rEM.RemoveCallback<RobotObservedFace>(RobotObservedSadFace);
      rEM.RemoveCallback<RobotObservedObject>(RobotObservedObject);
      var robot = rEM.CurrentRobot;
      robot.CancelCallback(NeutralFaceThenAdvanceToNextBlock);
      robot.CancelCallback(CompletedTurn);
      robot.CancelCallback(AdvanceToNextBlock);
      robot.CancelCallback(DockWithCube);
      robot.CancelCallback(FinishDockWithCube);
    }

    public void RobotObservedFace(RobotObservedFace message) {
      RobotEngineManager.Instance.RemoveCallback<RobotObservedFace>(RobotObservedFace);
      AdvanceToNextBlock(true);
    }

    public void RobotObservedHappyFace(RobotObservedFace message) {
      if (message.expression == Anki.Vision.FacialExpression.Happiness) {
        RobotEngineManager.Instance.RemoveCallback<RobotObservedFace>(RobotObservedHappyFace);
        AdvanceToNextBlock(true);
      }
    }

    public void RobotObservedSadFace(RobotObservedFace message) {
      // Match on Angry or Sad, and only if score is high (minimize false positives)
      if ((message.expression == Anki.Vision.FacialExpression.Anger) ||
          (message.expression == Anki.Vision.FacialExpression.Sadness)) {
        var expressionScore = message.expressionValues[(int)message.expression];
        const int kMinSadExpressionScore = 75;
        if (expressionScore >= kMinSadExpressionScore) {
          RobotEngineManager.Instance.RemoveCallback<RobotObservedFace>(RobotObservedSadFace);
          AdvanceToNextBlock(true);
        }
      }
    }

    public void RobotObservedObject(RobotObservedObject message) {
      RobotEngineManager.Instance.RemoveCallback<RobotObservedObject>(RobotObservedObject);
      AdvanceToNextBlock(true);
    }

    public static Face GetMostRecentlySeenFace() {
      var robot = RobotEngineManager.Instance.CurrentRobot;
      Face lastFaceSeen = null;
      for (int i = 0; i < robot.Faces.Count; ++i) {
        Face face = robot.Faces[i];
        if (face.IsInFieldOfView) {
          return face;
        }
        else {
          if ((lastFaceSeen == null) || (face.NumVisionFramesSinceLastSeen < lastFaceSeen.NumVisionFramesSinceLastSeen)) {
            lastFaceSeen = face;
          }
        }
      }

      return lastFaceSeen;
    }

    private static LightCube GetMostRecentlySeenCube() {
      LightCube lastCubeSeen = null;
      foreach (KeyValuePair<int, LightCube> kvp in RobotEngineManager.Instance.CurrentRobot.LightCubes) {
        LightCube cube = kvp.Value;
        if (cube.IsInFieldOfView) {
          return cube;
        }
        else {
          if ((lastCubeSeen == null) || (cube.NumVisionFramesSinceLastSeen < lastCubeSeen.NumVisionFramesSinceLastSeen)) {
            lastCubeSeen = cube;
          }
        }
      }

      return lastCubeSeen;
    }

    // If robot currently sees a cube, try to dock with it.
    public void DockWithCube(bool headAngleActionSucceeded) {
      bool success = false;

      if (headAngleActionSucceeded) {
        LightCube cube = GetMostRecentlySeenCube();
        const int kMaxVisionFramesSinceSeeingCube = 30;
        if ((cube != null) && (cube.NumVisionFramesSinceLastSeen < kMaxVisionFramesSinceSeeingCube)) {
          success = true;
          RobotEngineManager.Instance.CurrentRobot.AlignWithObject(cube, 0.0f, callback: FinishDockWithCube, usePreDockPose: true, alignmentType: Anki.Cozmo.AlignmentType.LIFT_PLATE, numRetries: 2);
        }
        else {
          DAS.Warn("DockWithCube.NoVisibleCube", "NumVisionFramesSinceLastSeen = " + ((cube != null) ? cube.NumVisionFramesSinceLastSeen : -1));
        }
      }
      else {
        DAS.Warn("DockWithCube.HeadAngleActionFailed", "");
      }

      if (!success) {
        FinishDockWithCube(false);
      }
    }

    private void FinishDockWithCube(bool success) {
      if (!success) {
        // Play angry animation since Cozmo wasn't able to complete the task
        // As this is on failure, queue anim in parallel - failure here could be because another block was clicked, and we don't want to interrupt that one
        Anki.Cozmo.QueueActionPosition queuePos = Anki.Cozmo.QueueActionPosition.IN_PARALLEL;
        RobotEngineManager.Instance.CurrentRobot.SendAnimationTrigger(Anki.Cozmo.AnimationTrigger.FrustratedByFailureMajor, callback: AdvanceToNextBlock, queueActionPosition: queuePos);
      }
      else {
        AdvanceToNextBlock(true);
      }
    }
  }

  public static class InProgressScratchBlockPool {
    private static List<InProgressScratchBlock> _available = new List<InProgressScratchBlock>();
    private static List<InProgressScratchBlock> _inUse = new List<InProgressScratchBlock>();

    public static InProgressScratchBlock GetInProgressScratchBlock() {
      // TODO Verify the lock is required and if we can assert on threadId here
      // to make sure we're not opening ourselves to threading issues.
      lock (_available) {
        if (_available.Count != 0) {
          InProgressScratchBlock po = _available[0];
          _inUse.Add(po);
          _available.RemoveAt(0);

          return po;
        }
        else {
          InProgressScratchBlock po = new InProgressScratchBlock();
          po.Init();
          _inUse.Add(po);

          return po;
        }
      }
    }

    public static void ReleaseAllInUse() {
      lock (_available) {
        if (_inUse.Count > 0) {
          for (int i = 0; i < _inUse.Count; ++i) {
            InProgressScratchBlock po = _inUse[i];
            po.OnReleased();
            _available.Add(po);
          }
          _inUse.Clear();
        }
      }
    }

    public static void ReleaseInProgressScratchBlock(InProgressScratchBlock po) {
      po.OnReleased();

      lock (_available) {
        _available.Add(po);
        _inUse.Remove(po);
      }
    }
  }

}
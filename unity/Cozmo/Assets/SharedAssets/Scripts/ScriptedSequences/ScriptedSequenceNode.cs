using System;
using System.Collections.Generic;
using UnityEngine;
using System.Linq;
using System.ComponentModel;
using Newtonsoft.Json;

namespace ScriptedSequences
{
  [System.Serializable]
  public class ScriptedSequenceNode : IScriptedSequenceParent 
	{
    public string Name;

    public uint Id;

    [DefaultValue(true)]
    public bool Sequencial = true;

    public bool Final;

    public bool FailOnError;

    [DefaultValue(true)]
    public bool ExitOnActionsComplete = true;

    public List<ScriptedSequenceCondition> Conditions = new List<ScriptedSequenceCondition>();

    public List<ScriptedSequenceAction> Actions = new List<ScriptedSequenceAction>();

    public List<ScriptedSequenceCondition> ExitConditions = new List<ScriptedSequenceCondition>();

    private ScriptedSequence _Parent;

    private ScriptedSequenceNode _Previous;

    [JsonIgnore]
    public string DebugName { get { return _Parent.DebugName + "::" + Name; } }

    private bool _IsComplete;

    [JsonIgnore]
    public bool IsComplete { 
      get { return _IsComplete; } 
      private set { 
        if (value && !_IsComplete) {
          _IsComplete = value;
          #if DEBUG_SCRIPTED_SEQUENCES
          DAS.Debug(this, DebugName +" Is Now Complete!");
          #endif
          if (OnComplete != null) {
            OnComplete();
          }

          if (Final) {
            _Parent.Complete();
          }
        }
        else {
          _IsComplete = value;
        }
      }
    }

    private IAsyncToken _ActToken;
    [JsonIgnore]
    public bool IsActive { get { return _ActToken != null && !IsComplete; } }

    private bool _IsEnabled;

    [JsonIgnore]
    public bool IsEnabled { 
      get {
        return _IsEnabled;
      }

      private set {
        _IsEnabled = value;

        #if DEBUG_SCRIPTED_SEQUENCES
        DAS.Debug(this, DebugName +" Is "+(_IsEnabled ? "Enabled" : "Disabled")+"!");
        #endif

        if (UpdateConditions(_IsEnabled)) {
          Act();
        }
      }
    }

    [JsonIgnore]
    public bool Succeeded { get; private set; }

    public event Action OnComplete;

    public void Initialize(ScriptedSequence parent, ScriptedSequenceNode previous) {
      _Parent = parent;

      for (int i = 0; i < Conditions.Count; i++) {
        Conditions[i].Initialize(this);
        Conditions[i].OnConditionChanged += HandleConditionsChanged;
      }

      for (int i = 0; i < Actions.Count; i++) {
        Actions[i].Initialize(this);
      }

      if (Sequencial && previous != null) {
        _Previous = previous;
        previous.OnComplete += HandlePreviousNodeComplete;
      }

      for (int i = 0; i < ExitConditions.Count; i++) {
        ExitConditions[i].Initialize(this);
        ExitConditions[i].OnConditionChanged += HandleExitConditionsChanged;
      }
    }

    public void TryEnable()
    {      
      #if DEBUG_SCRIPTED_SEQUENCES
      DAS.Debug(this, "TryEnable Called on " + DebugName);
      #endif
      if (_Previous != null && !_Previous.IsComplete) {
        return;
      }

      IsEnabled = true;
    }

    public void Reset()
    {
      #if DEBUG_SCRIPTED_SEQUENCES
      DAS.Debug(this, "Reset Called on " + DebugName);
      #endif
      if (_ActToken != null) {
        _ActToken.Abort();
        _ActToken = null;
      }

      IsComplete = false;
      IsEnabled = false;

      UpdateExitConditions(false);
    }

    public ScriptedSequenceNode GetNode(uint id)
    {
      return _Parent.GetNode(id);
    }

    public ScriptedSequence GetSequence()
    {
      return _Parent;
    }

    private void HandlePreviousNodeComplete()
    {
      IsEnabled = true;
    }

    private void HandleConditionsChanged()
    {
      if (UpdateConditions(IsEnabled)) {
        Act();
      }
    }

    private void HandleExitConditionsChanged()
    {
      if (UpdateExitConditions(IsActive)) {
        if (_ActToken != null) {
          _ActToken.Abort();
          _ActToken = null;
        }
        IsComplete = true;
      }
    }

    private bool UpdateConditions(bool enabled)
    {
      if (IsComplete || IsActive) {
        return false;
      }

      bool lastMet = enabled;
      for (int i = 0; i < Conditions.Count; i++) {
        var condition = Conditions[i];
        condition.IsEnabled = lastMet;

        lastMet = condition.IsMet;
      }
      return lastMet;
    }

    private bool UpdateExitConditions(bool active)
    { 
      if (IsComplete) {
        return false;
      }

      if (ExitConditions.Count == 0) {
        return false;
      }
      
      bool lastMet = active;
      for (int i = 0; i < ExitConditions.Count; i++) {
        var condition = ExitConditions[i];
        condition.IsEnabled = lastMet;

        lastMet = condition.IsMet;
      }
      return lastMet;
    }

    public void Act() {
      #if DEBUG_SCRIPTED_SEQUENCES
      DAS.Debug(this, "Act Called on "+DebugName);
      #endif
      var actingTokens = new ISimpleAsyncToken[Actions.Count];

      for (int i = 0; i < Actions.Count; i++) {
        #if DEBUG_SCRIPTED_SEQUENCES
        DAS.Debug(this, "Calling Act on " + Actions[i].DebugName);
        #endif
        actingTokens[i] = Actions[i].Act();
      }
      if (FailOnError) {
        var token = SimpleAsyncToken.Reduce(actingTokens);
        _ActToken = token;
        token.Ready(result => {

          if (!result.Success) {
            _Parent.Fail(result.Error);
            return;
          }
          Succeeded = true;
          if(ExitOnActionsComplete)
          {
            IsComplete = true;
          }
        });
      } else {
        var token = SimpleAsyncToken.PessimisticReduce(actingTokens);
        _ActToken = token;
        token.Ready(result => {
          Succeeded = !result.Value.Any(x => !x.Success);
          if(ExitOnActionsComplete)
          {
            IsComplete = true;
          }
        });
      }

      HandleExitConditionsChanged();
    }
  }

}


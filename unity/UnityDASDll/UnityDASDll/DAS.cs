﻿using System;
using System.Runtime.InteropServices;
using System.Collections.Generic;

public interface IDAS {
  void Event(string eventValue, System.Object context = null, Dictionary<string, string> keyValues = null);

  void Error(string eventValue, System.Object context = null, Dictionary<string, string> keyValues = null);

  void Error(Exception eventValue, System.Object context = null, Dictionary<string, string> keyValues = null);

  void Warn(string eventValue, System.Object context = null, Dictionary<string, string> keyValues = null);

  void Info(string eventValue, System.Object context = null, Dictionary<string, string> keyValues = null);

  void Debug(string eventValue, System.Object context = null, Dictionary<string, string> keyValues = null);
}

public static partial class DAS {

  private static readonly List<IDASTarget> _Targets = new List<IDASTarget>();

  public static void AddTarget(IDASTarget target) {
    _Targets.Add(target);
  }

  public static void RemoveTarget(IDASTarget target) {
    _Targets.Remove(target);
  }

  public static void ClearTargets() {
    _Targets.Clear();
  }

  public static void Event(object eventObject, string eventValue, object context = null, Dictionary<string, string> keyValues = null) {
    string eventName = GetEventName(eventObject);

    for (int i = 0, len = _Targets.Count; i < len; i++) {
      _Targets[i].Event(eventName, eventValue, context, keyValues);
    }
  }

  public static void Error(object eventObject, string eventValue, object context = null, Dictionary<string, string> keyValues = null) {
    string eventName = GetEventName(eventObject);

    for (int i = 0, len = _Targets.Count; i < len; i++) {
      _Targets[i].Error(eventName, eventValue, context, keyValues);
    }
  }

  public static void Error(object eventObject, Exception eventValue, object context = null, Dictionary<string, string> keyValues = null) {
    string eventName = GetEventName(eventObject);

    for (int i = 0, len = _Targets.Count; i < len; i++) {
      _Targets[i].Error(eventName, eventValue.Message + "\n" + eventValue.StackTrace, context, keyValues);
    }
  }

  public static void Warn(object eventObject, string eventValue, object context = null, Dictionary<string, string> keyValues = null) {
    string eventName = GetEventName(eventObject);

    for (int i = 0, len = _Targets.Count; i < len; i++) {
      _Targets[i].Warn(eventName, eventValue, context, keyValues);
    }
  }

  public static void Info(object eventObject, string eventValue, object context = null, Dictionary<string, string> keyValues = null) {
    string eventName = GetEventName(eventObject);

    for (int i = 0, len = _Targets.Count; i < len; i++) {
      _Targets[i].Info(eventName, eventValue, context, keyValues);
    }
  }

  public static void Debug(object eventObject, string eventValue, object context = null, Dictionary<string, string> keyValues = null) {
    string eventName = GetEventName(eventObject);

    for (int i = 0, len = _Targets.Count; i < len; i++) {
      _Targets[i].Debug(eventName, eventValue, context, keyValues);
    }
  }

  private static string GetEventName(object eventObject) {
    if (eventObject == null) {
      return string.Empty;
    }
    var eventString = eventObject as string;
    if (eventString != null) {
      return eventString;
    }
    
    var eventType = eventObject as System.Type;
    if (eventType != null) {
      return eventType.Name;
    }

    return eventObject.GetType().Name;
  }

  public static IDAS GetInstance(System.Type type) {
    // this could be modified to do a dictionary lookup if it was a concern
    // that multiple instance for the same type would be retrieved
    return new DASInstance(type);
  }

  private sealed class DASInstance : IDAS {
    private string _EventName;

    public DASInstance(System.Type type) {
      _EventName = type.Name;
    }

    public void Event(string eventValue, System.Object context = null, Dictionary<string, string> keyValues = null) {
      DAS.Event(_EventName, eventValue, context, keyValues);
    }

    public void Error(string eventValue, System.Object context = null, Dictionary<string, string> keyValues = null) {
      DAS.Error(_EventName, eventValue, context, keyValues);      
    }

    public void Error(Exception eventValue, System.Object context = null, Dictionary<string, string> keyValues = null) {
      DAS.Error(_EventName, eventValue, context, keyValues);      
    }

    public void Warn(string eventValue, System.Object context = null, Dictionary<string, string> keyValues = null) {
      DAS.Warn(_EventName, eventValue, context, keyValues);      
    }

    public void Info(string eventValue, System.Object context = null, Dictionary<string, string> keyValues = null) {
      DAS.Info(_EventName, eventValue, context, keyValues);      
    }

    public void Debug(string eventValue, System.Object context = null, Dictionary<string, string> keyValues = null) {
      DAS.Debug(_EventName, eventValue, context, keyValues);            
    }
  }
  
}

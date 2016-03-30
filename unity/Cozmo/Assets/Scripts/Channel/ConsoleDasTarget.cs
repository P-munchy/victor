﻿using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using UnityEngine;

public class ConsoleDasTarget : IDASTarget {

  public void Event(string eventName, string eventValue, object context = null, Dictionary<string, string> keyValues = null) {
    Console.WriteLine("[Event][" + eventName + "] " + eventValue);
  }

  public void Error(string eventName, string eventValue, object context = null, Dictionary<string, string> keyValues = null) {
    Console.WriteLine("[Error][" + eventName + "] " + eventValue);
  }

  public void Warn(string eventName, string eventValue, object context = null, Dictionary<string, string> keyValues = null) {
    Console.WriteLine("[Warn][" + eventName + "] " + eventValue);
  }

  public void Info(string eventName, string eventValue, object context = null, Dictionary<string, string> keyValues = null) {
    Console.WriteLine("[Info][" + eventName + "] " + eventValue);
  }

  public void Debug(string eventName, string eventValue, object context = null, Dictionary<string, string> keyValues = null) {
    Console.WriteLine("[Debug][" + eventName + "] " + eventValue);
  }
}

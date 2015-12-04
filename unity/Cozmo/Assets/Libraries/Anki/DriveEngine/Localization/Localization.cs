﻿using UnityEngine;
using System.Collections.Generic;
using System;
using System.IO;

public static class Localization {

  private static Anki.AppResources.StringTable _st = new Anki.AppResources.StringTable();

  public static bool showDebugLocText = false;

  public static string Get(string key) {
    string value;
    if (!_st.TryGetString(key, out value)) {
      #if !UNITY_EDITOR
      DAS.Warn("LocalizedString.Get.MissingKey", "");  //disabling populating of key name for COPPA reasons (JDN)
      #else
      if (Application.isPlaying) {
        DAS.Warn("LocalizedString.Get.MissingKey", key);
      }
      #endif
      value = key;
    }
    else if (showDebugLocText) {
      value = new string('@', value.Length);
    } 

    return value;
  }

  public static bool IsSupportedLocale(string locale) {
    return _SupportedLocales.Contains(locale);
  }

  private static List<string> _SupportedLocales = 
    new List<string>() { "en-US", "en-GB", "de-DE" };

  private static string _CurrentLocale = null;
  private static System.Globalization.CultureInfo _CurrentCulture = null;

  // Return locale code for a language code.
  public static string GetStringsLocale() {

    if (string.IsNullOrEmpty(_CurrentLocale)) {
      // TODO: INGO - Need to provide functionality for GetCurrentLocale that 
      // doesn't rely on DriveEngine
      // string lang;
      // string country;
      // string deviceLocale = Anki.ApplicationServices.GetCurrentLocale(out lang, out country);
      string lang = "en";
      string deviceLocale = "en-US";

      if (_SupportedLocales.Contains(deviceLocale)) {
        // TODO: Biggest hack ever, but we don't currently have any text data for en-GB, so just hard code it back to en-US
        if (deviceLocale == "en-GB") {
          deviceLocale = "en-US";
        }
      }
      else {
        switch (lang) {
        case "en":
          deviceLocale = "en-US";
          break;
        case "de":
          deviceLocale = "de-DE";
          break;
        default:
          deviceLocale = "en-US";
          break;
        }
      }
        
      _CurrentLocale = deviceLocale;
      _CurrentCulture = new System.Globalization.CultureInfo(_CurrentLocale);
    }

    return _CurrentLocale;
  }

  private const string kLocalizationStreamingAssetsFolderPath = "/LocalizedStrings/";

  public static void LoadStrings() {

    // For each localization file in the locale's directory
    string locale = GetStringsLocale();
    foreach (var filePath in GetLocalizationJsonFilePaths(locale)) {
      
      // Load the localization into a string table so that we can query it at runtime
      JSONObject languageJson = GetJsonContentsFromLocalizationFile(locale, filePath);

      Anki.AppResources.StringTable st = Anki.AppResources.StringTable.LoadStringsFromSmartlingJSONFile(languageJson);
      _st.MergeEntriesFromStringTable(st);
    }
  }

  public static System.Globalization.CultureInfo GetCultureInfo() {
    return _CurrentCulture;
  }

  public static string[] GetLocalizationJsonFilePaths(string locale) {
    return Directory.GetFiles(Application.streamingAssetsPath + kLocalizationStreamingAssetsFolderPath + locale, "*.json");
  }

  public static JSONObject GetJsonContentsFromLocalizationFile(string locale, string localizationFilePath) {
    string languageJson = File.ReadAllText(localizationFilePath);
    return new JSONObject(languageJson);
  }
}
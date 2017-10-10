﻿using UnityEngine;
using System.Collections.Generic;

//"smartling": {
//  "translate_paths": [
//    "*/translation"
//  ],
//  "variants_enabled": true,
//  "translate_mode": "custom",
//  "placeholder_format_custom": [
//    "%%[^%]+?%%",
//    "%[^%]+?%",
//    "##[^#]+?##",
//    "__[^_]+?__",
//    "[$]\\{[^\\}\\{]+?\\}",
//    "\\{\\{[^\\}\\{]+?\\}\\}",
//    "(?<!\\{)\\{[^\\}\\{]+?\\}(?!\\})"
//  ],
//  "source_key_paths": [
//    "/{*}"
//  ]
//},
using Newtonsoft.Json;
using System.ComponentModel;
using System.Collections;
using System.IO;
using UnityEditor;
using Newtonsoft.Json.Converters;
using System;
using System.Linq;
using System.Reflection;
using Newtonsoft.Json.Linq;

public class SmartlingBlob {
  [JsonProperty("translate_paths")]
  public List<string> TranslatePaths = new List<string>();

  [JsonProperty("variants_enabled")]
  public bool VariantsEnabled;

  [JsonProperty("translate_mode")]
  public string TranslateMode;

  [JsonProperty("placeholder_format_custom")]
  public List<string> PlaceholderFormatCustom = new List<string>();

  [JsonProperty("source_key_paths")]
  public List<string> SourceKeyPaths = new List<string>();
}


[JsonConverter(typeof(LocalizationDictionaryConverter))]
public class LocalizationDictionary {
  public Dictionary<string, LocalizationDictionaryEntry> Translations { get; set; }

  [JsonProperty("smartling")]
  public SmartlingBlob Smartling { get; set; }

}

public class LocalizationDictionaryEntry {
  [JsonProperty("translation")]
  public string Translation;
}

internal class LocalizationDictionaryConverter : CustomCreationConverter<LocalizationDictionary> {
  #region Overrides of CustomCreationConverter<LocalizationDictionary>

  public override LocalizationDictionary Create(Type objectType) {
    return new LocalizationDictionary {
      Translations = new Dictionary<string, LocalizationDictionaryEntry>()
    };
  }

  public override void WriteJson(JsonWriter writer, object value, JsonSerializer serializer) {
    writer.WriteStartObject();
    var dict = (LocalizationDictionary)value;

    // Write properties.
    writer.WritePropertyName("smartling");
    serializer.Serialize(writer, dict.Smartling);

    // Write dictionary key-value pairs.
    foreach (var kvp in dict.Translations) {
      writer.WritePropertyName(kvp.Key);
      serializer.Serialize(writer, kvp.Value);
    }
    writer.WriteEndObject();
  }

  public override object ReadJson(JsonReader reader, Type objectType, object existingValue, JsonSerializer serializer) {
    JObject jsonObject = JObject.Load(reader);
    var jsonProperties = jsonObject.Properties().ToList();
    var outputObject = Create(objectType);

    foreach (var jsonProperty in jsonProperties) {
      // If such property exists - use it.
      if (jsonProperty.Name == "smartling") {
        var propertyValue = jsonProperty.Value.ToObject<SmartlingBlob>();
        outputObject.Smartling = propertyValue;
      }
      else {
        // Otherwise - use the dictionary.
        outputObject.Translations.Add(jsonProperty.Name, jsonProperty.Value.ToObject<LocalizationDictionaryEntry>());
      }
    }

    return outputObject;
  }

  public override bool CanWrite {
    get { return true; }
  }

  #endregion
}

public static class LocalizationEditorUtility {

  private static readonly Dictionary<string, LocalizationDictionary> _LocalizationDictionaries = new Dictionary<string, LocalizationDictionary>();
  private static string[] _LocalizationFiles = new string[0];
  private static string[][] _LocalizationFileNamespaces = new string[0][];

  private static string[] _LocalizationKeys = new string[0];

  private const string kLocalizationFolder = "Assets/StreamingAssets/LocalizedStrings/en-US/";

  public static string[] LocalizationFiles { get { return _LocalizationFiles; } }

  public static string[][] LocalizationFileNamespaces { get { return _LocalizationFileNamespaces; } }

  public static string[] LocalizationKeys { get { return _LocalizationKeys; } }

  public static readonly char[] kFormatMarkers = { '<', '>' };

  public static LocalizationDictionary CreateLocalizationDictionary() {
    return new LocalizationDictionary() {
      Smartling = new SmartlingBlob() {
        TranslatePaths = new List<string> { "*/translation" },
        VariantsEnabled = true,
        TranslateMode = "custom",
        PlaceholderFormatCustom = new List<string> {
          "%%[^%]+?%%",
          "%[^%]+?%",
          "##[^#]+?##",
          "__[^_]+?__",
          "[$]\\{[^\\}\\{]+?\\}",
          "\\{\\{[^\\}\\{]+?\\}\\}",
          "(?<!\\{)\\{[^\\}\\{]+?\\}(?!\\})"
        },
        SourceKeyPaths = new List<string> { "/{*}" }
      },
      Translations = new Dictionary<string, LocalizationDictionaryEntry>()
    };
  }

  public static LocalizationDictionary CreateNewLocalizationFile(string fileName) {
    var dict = CreateLocalizationDictionary();

    File.WriteAllText(kLocalizationFolder + fileName + ".json", JsonConvert.SerializeObject(dict, Formatting.Indented));

    _LocalizationDictionaries[fileName] = dict;
    return dict;
  }

  static LocalizationEditorUtility() {
    Reload();
  }

  public static string[] GetKeyNamespacesForLocalizationFile(string fileName) {
    for (int i = 0; i < _LocalizationFileNamespaces.Length; i++) {
      if (_LocalizationFiles[i] == fileName)
        return LocalizationFileNamespaces[i];
    }
    return null;
  }

  public static string GetLocalizationFileForNamespaceKey(string namespaceKey) {
    for (int i = 0; i < _LocalizationFileNamespaces.Length; i++) {
      var names = _LocalizationFileNamespaces[i];
      for (int j = 0; j < names.Length; j++) {
        if (namespaceKey == names[j])
          return _LocalizationFiles[i];
      }
    }
    return null;
  }

  public static string GetLocalizationFileForLocalizedKey(string localizedKey) {
    foreach (var kvp in _LocalizationDictionaries) {
      if (kvp.Value.Translations.ContainsKey(localizedKey)) {
        return kvp.Key;
      }
    }
    return null;
  }

  public static string[] GetAllKeysInFile(string fileName) {
    return _LocalizationDictionaries[fileName].Translations.Keys.ToArray();
  }

  public static string FindBestFitFileForKey(string localizedKey) {
    var names = localizedKey.Split('.');
    if (names.Length > 0) {
      return GetLocalizationFileForNamespaceKey(names[0]);
    }
    return null;
  }

  //Returns first 50 keys that start with the search string
  //[0] = key [1] = translation
  public static string[] FindTranslatedMatches(string searchString) {
    const int maxResults = 50;
    List<string> ret = new List<string>();
    if (!string.IsNullOrEmpty(searchString)) {
      for (int i = 0; i < LocalizationKeys.Length && ret.Count < maxResults * 2; i++) {
        string key = LocalizationKeys[i];
        if (string.IsNullOrEmpty(key))
          continue;
        string filename = GetLocalizationFileForLocalizedKey(key);
        if (string.IsNullOrEmpty(filename))
          continue;
        string translation = LocalizationEditorUtility.GetTranslation(filename, key);
        if (translation.StartsWith(searchString, StringComparison.CurrentCultureIgnoreCase)) {
          ret.Add(key);
          ret.Add(translation);
        }
      }
    }
    return ret.ToArray();
  }

  [MenuItem("Cozmo/Localization/Reload Localization Files")]
  public static void Reload() {
    _LocalizationDictionaries.Clear();

    foreach (var file in Directory.GetFiles(kLocalizationFolder, "*.json")) {
      _LocalizationDictionaries[Path.GetFileNameWithoutExtension(file)] = JsonConvert.DeserializeObject<LocalizationDictionary>(File.ReadAllText(file));
    }

    _LocalizationFiles = _LocalizationDictionaries.Keys.ToArray();

    _LocalizationFileNamespaces = new string[_LocalizationDictionaries.Count][];
    for (int i = 0; i < _LocalizationFiles.Length; i++) {
      var data = _LocalizationDictionaries[LocalizationFiles[i]];
      if (data == null || data.Translations == null || data.Translations.Count == 0)
        continue;
      HashSet<string> namespaceKeys = new HashSet<string>();
      foreach (string key in data.Translations.Keys) {
        var names = key.Split('.');
        if (names.Length > 0)
          namespaceKeys.Add(names[0]);
      }
      _LocalizationFileNamespaces[i] = namespaceKeys.ToArray();
    }

    _LocalizationKeys = new[] { string.Empty }.Concat(_LocalizationDictionaries.Values.SelectMany(x => x.Translations.Keys)).ToArray();
    // sort them to make it easier to find
    Array.Sort(_LocalizationKeys);
  }

  public static string GetTranslationSansFormatting(string key) {
    string fileName = "";
    string sansFormat = GetTranslation(key, out fileName);
    string[] splitName = sansFormat.Split();
    for (int i = 0; i < splitName.Length; i++) {
      int markerStart = splitName[i].IndexOf(kFormatMarkers[0]);
      int markerEnd = splitName[i].IndexOf(kFormatMarkers[1]);
      while (markerStart >= 0 && markerEnd > markerStart) {
        splitName[i] = splitName[i].Remove(markerStart, markerEnd - markerStart + 1);
        markerStart = splitName[i].IndexOf(kFormatMarkers[0]);
        markerEnd = splitName[i].IndexOf(kFormatMarkers[1]);
      }
    }
    sansFormat = String.Join(" ", splitName);
    return sansFormat;
  }

  public static string GetTranslation(string fileName, string key) {
    LocalizationDictionary dict;
    if (_LocalizationDictionaries.TryGetValue(fileName, out dict)) {
      LocalizationDictionaryEntry entry;
      if (dict.Translations.TryGetValue(key, out entry)) {
        return entry.Translation;
      }
    }
    return string.Empty;
  }

  // find key in any file
  public static string GetTranslation(string key, out string fileName) {
    foreach (var kvp in _LocalizationDictionaries) {
      var dict = kvp.Value;
      fileName = kvp.Key;
      LocalizationDictionaryEntry entry;
      if (dict.Translations.TryGetValue(key, out entry)) {
        return entry.Translation;
      }
    }
    fileName = string.Empty;
    return string.Empty;
  }

  public static bool KeyExists(string fileName, string key) {
    LocalizationDictionary dict;
    if (_LocalizationDictionaries.TryGetValue(fileName, out dict)) {
      return dict.Translations.ContainsKey(key);
    }
    return false;
  }

  public static bool KeyExists(string key) {
    return LocalizationKeys.Contains(key);
  }

  public static void SetTranslation(string fileName, string key, string translation) {
    LocalizationDictionary dict;
    if (!_LocalizationDictionaries.TryGetValue(fileName, out dict)) {
      dict = CreateLocalizationDictionary();
      _LocalizationDictionaries[fileName] = dict;
    }

    LocalizationDictionaryEntry entry;
    if (!dict.Translations.TryGetValue(key, out entry)) {
      entry = new LocalizationDictionaryEntry();
      dict.Translations[key] = entry;
    }

    entry.Translation = translation;

    File.WriteAllText(kLocalizationFolder + fileName + ".json", JsonConvert.SerializeObject(dict, Formatting.Indented));
  }

  private const string kGeneratedLocalizationKeysFilePath = "Assets/Plugins/Libraries/Anki/Localization/GeneratedKeys/LocalizationKeys.cs";
  private const string kGeneratedLocalizationKeysSourceLocale = "en-us";

  [MenuItem("Cozmo/Localization/Generate Localization Key Constants")]
  private static void GenerateLocalizationKeyConstFile() {
    string fileContents = "public static class LocalizationKeys {";

    // For every key in localization, generate a C# constant. Use english as the source.
    JSONObject localizationJson;
    string cSharpVariableName;
    foreach (var jsonFileName in Localization.GetLocalizationJsonFilePaths(kGeneratedLocalizationKeysSourceLocale)) {

      fileContents += "\n\n  #region " + Path.GetFileNameWithoutExtension(jsonFileName) + "\n";

      localizationJson = Localization.GetJsonContentsFromLocalizationFile(kGeneratedLocalizationKeysSourceLocale, jsonFileName);
      foreach (string localizationKey in localizationJson.keys) {
        // Skip smartling data
        if ("smartling" == localizationKey) {
          continue;
        }

        // Create a const variable using the key that follows convention
        cSharpVariableName = VariableNameFromLocalizationKey(localizationKey);
        fileContents += "\n  public const string " + cSharpVariableName + " = \"" + localizationKey + "\";";
      }

      fileContents += "\n\n  #endregion";
    }

    fileContents += "\n}";

    // Write the code to a file
    File.WriteAllText(kGeneratedLocalizationKeysFilePath, fileContents);
  }

  [MenuItem("Cozmo/Localization/Copy Boot String Files")]
  public static void CopyBootStringFiles() {
    // Bootup strings need to be in resources, so Copy them in the event something has changed.
    // TextAssets don't get called as a part of the AssetPostProcessor. So just manually copy them.
    const string _kBootStringsFileName = "/BootStrings.json";
    string[] supportedLocaleDirs = Localization.GetLocalizationAllLanguagesPaths();
    foreach (string srcPath in supportedLocaleDirs) {
      if (File.Exists(srcPath + _kBootStringsFileName)) {
        string[] splitStr = srcPath.Split('/');
        string combinedPath = Path.Combine(srcPath, "../../../Resources/bootstrap/LocalizedStrings/");
        string destPath = combinedPath + splitStr[splitStr.Length - 1] + _kBootStringsFileName;
        File.Copy(srcPath + _kBootStringsFileName, destPath, true);
      }
    }
  }

  #region Remove Unused Loc Keys

  private static string VariableNameFromLocalizationKey(string localizationKey) {
    string variableName = "k";
    char[] delimiter = { '.', ' ' };
    string[] keyParts = localizationKey.Split(delimiter, StringSplitOptions.RemoveEmptyEntries);
    foreach (string part in keyParts) {
      variableName += char.ToUpper(part[0]) + part.Substring(1);
    }
    return variableName;
  }

  private static bool StringContainedInFiles(string str, ref string[] filePaths, ref string[] fileContents, string excludePath = null) {
    // For each file...
    for (int i = 0; i < filePaths.Length; i++) {
      // Skip blacklisted files
      if (!string.IsNullOrEmpty(excludePath) && filePaths[i].Contains(excludePath)) {
        continue;
      }

      // If we haven't read this file yet cache the contents for later searches
      if (string.IsNullOrEmpty(fileContents[i])) {
        fileContents[i] = File.ReadAllText(filePaths[i]);
      }

      // Check to see if the file contains the search string
      if (fileContents[i].Contains(str)) {
        return true;
      }
    }
    return false;
  }

  [MenuItem("Cozmo/Localization/Remove Unused Loc Keys")]
  public static void RemoveUnusedLocKeys() {
    // Cache file paths
    string directoryProgressTitle = "Getting Files in Directory";
    string directoryProgressInfo = "Directory {0}/{1}";
    int totalDirectories = 6;

    string[] csFilePaths = Directory.GetFiles("Assets/", "*.cs", SearchOption.AllDirectories);
    EditorUtility.DisplayProgressBar(directoryProgressTitle, string.Format(directoryProgressInfo, 1, totalDirectories), (float)1 / totalDirectories);

    string[] prefabFilePaths = Directory.GetFiles("Assets/", "*.prefab", SearchOption.AllDirectories);
    EditorUtility.DisplayProgressBar(directoryProgressTitle, string.Format(directoryProgressInfo, 2, totalDirectories), (float)2 / totalDirectories);

    string[] assetFilePaths = Directory.GetFiles("Assets/", "*.asset", SearchOption.AllDirectories);
    EditorUtility.DisplayProgressBar(directoryProgressTitle, string.Format(directoryProgressInfo, 3, totalDirectories), (float)3 / totalDirectories);

    string[] unityJsonFilePaths = Directory.GetFiles("Assets/AssetBundles/", "*.json", SearchOption.AllDirectories);
    EditorUtility.DisplayProgressBar(directoryProgressTitle, string.Format(directoryProgressInfo, 4, totalDirectories), (float)4 / totalDirectories);

    string[] productConfigFilePaths = Directory.GetFiles(Application.dataPath + "/../../../resources/assets/",
                                                         "*.json", SearchOption.AllDirectories);
    EditorUtility.DisplayProgressBar(directoryProgressTitle, string.Format(directoryProgressInfo, 5, totalDirectories), (float)5 / totalDirectories);

    string[] basestationConfigFilePaths = Directory.GetFiles(Application.dataPath + "/../../../resources/config/engine",
                                                             "*.json", SearchOption.AllDirectories);
    EditorUtility.DisplayProgressBar(directoryProgressTitle, string.Format(directoryProgressInfo, 6, totalDirectories), (float)6 / totalDirectories);

    string[] jsFilePaths = Directory.GetFiles("Assets/StreamingAssets/Scratch", "*.js", SearchOption.AllDirectories);
    EditorUtility.DisplayProgressBar(directoryProgressTitle, string.Format(directoryProgressInfo, 7, totalDirectories), (float)7 / totalDirectories);

    // Prep caches for file contents 
    string[] csFileContents = new string[csFilePaths.Length];
    string[] prefabFileContents = new string[prefabFilePaths.Length];
    string[] assetFileContents = new string[assetFilePaths.Length];
    string[] jsonFileContents = new string[unityJsonFilePaths.Length];
    string[] productConfigFileContents = new string[productConfigFilePaths.Length];
    string[] basestationConfigFileContents = new string[basestationConfigFilePaths.Length];
    string[] jsFileContents = new string[jsFilePaths.Length];

    // Keep count for progress bar
    int numTotalFiles = _LocalizationDictionaries.Count;
    int currentFileNum = 0;

    // Iterate over each localization file
    bool anyFileChanged = false;
    foreach (var localizationDictFile in _LocalizationDictionaries) {
      currentFileNum++;

      // Skip file that has singular vs plural keys because they are constructed at runtime
      string fileName = localizationDictFile.Key;
      if (fileName == "ItemStrings") {
        continue;
      }

      // For Code Lab strings, only search js files for references
      Func<string, bool> keySearchStrategy;
      if (fileName == "CodeLabStrings") {
        keySearchStrategy = (string x) => {
          return StringContainedInFiles(x, ref jsFilePaths, ref jsFileContents);
        };
      }
      else {
        // For all other files search cs, json, prefabs, and assets
        keySearchStrategy = (string x) => {
          string csFileToExclude = "LocalizationKeys.cs";
          return (StringContainedInFiles(VariableNameFromLocalizationKey(x), ref csFilePaths, ref csFileContents, csFileToExclude)
                  || StringContainedInFiles(x, ref prefabFilePaths, ref prefabFileContents)
                  || StringContainedInFiles(x, ref assetFilePaths, ref assetFileContents)
                  || StringContainedInFiles(x, ref unityJsonFilePaths, ref jsonFileContents)
                  || StringContainedInFiles(x, ref productConfigFilePaths, ref productConfigFileContents)
                  || StringContainedInFiles(x, ref basestationConfigFilePaths, ref basestationConfigFileContents));
        };
      }

      // Deep copy to remove from source
      Dictionary<string, LocalizationDictionaryEntry> keysDict =
        new Dictionary<string, LocalizationDictionaryEntry>(localizationDictFile.Value.Translations);

      // Keep count for progress bar
      int numLocKeys = keysDict.Count;
      int currentKeyNum = 0;

      // Iterate over each localization key
      bool locFileUpdateNeeded = false;
      foreach (var locKey in keysDict) {
        currentKeyNum++;

        // Skip plural and singular keys
        if (locKey.Key.Contains(".plural") || locKey.Key.Contains(".singular")) {
          continue;
        }

        // Update editor progress bar
        EditorUtility.DisplayProgressBar(string.Format("Removing Unused Loc Keys - File {0}/{1}: {2}",
                                                       currentFileNum, numTotalFiles, fileName),
                                         string.Format("Key {0}/{1}: {2}", currentKeyNum, numLocKeys, locKey.Key),
                                         (float)currentKeyNum / numLocKeys);

        // Search files for key
        bool keyFound = keySearchStrategy.Invoke(locKey.Key);

        // Consider a key not used if it is not in C# code, prefabs, or engine configs
        if (!keyFound) {
          localizationDictFile.Value.Translations.Remove(locKey.Key);
          locFileUpdateNeeded = true;
          Debug.LogWarning("Not Found " + locKey.Key);
        }
      }

      // Write changes to file if any keys were removed
      if (locFileUpdateNeeded) {
        File.WriteAllText(kLocalizationFolder + fileName + ".json", JsonConvert.SerializeObject(localizationDictFile.Value, Formatting.Indented));
        anyFileChanged = true;
      }
    }

    // Regenerate constant file
    if (anyFileChanged) {
      GenerateLocalizationKeyConstFile();
    }

    EditorUtility.ClearProgressBar();
  }

  #endregion // Remove Unused Loc Keys

  private static List<string> AddFilesContainingStringInDir(ref List<string> filesContainingStr, string str, string dir, string searchPattern, string exclude = null) {
    foreach (var path in Directory.GetFiles(dir, searchPattern, SearchOption.AllDirectories)) {
      if (exclude != null && path.Contains(exclude)) {
        continue;
      }
      if (File.ReadAllText(path).Contains(str)) {
        string[] splitPath = path.Split(new char[] { '/' });
        filesContainingStr.Add(splitPath[splitPath.Length - 1]);
      }
    }
    return filesContainingStr;
  }
  // Can't search with multiple file extensions
  private static List<string> AddFilesContainingStringInDir(ref List<string> filesContainingStr, string str, string dir, string[] searchPatterns) {
    foreach (var path in Directory.GetFiles(dir, "*", SearchOption.AllDirectories)) {
      for (int i = 0; i < searchPatterns.Length; ++i) {
        if (path.Contains(searchPatterns[i])) {
          if (File.ReadAllText(path).Contains(str)) {
            string[] splitPath = path.Split(new char[] { '/' });
            filesContainingStr.Add(splitPath[splitPath.Length - 1]);
          }
          break; // move on to next path
        }
      }
    }
    return filesContainingStr;
  }

  // Running on a thread so needs to be volatile locked.
  private static volatile bool _LocReportRunning = false;

  public class LocReportWrapper {
    public static volatile bool _WantsContinueRunning = true;
    private string _ApplicationDataPath;
    private const int _kSleepTime_ms = 5;
    public LocReportWrapper(string applicationDataPath) {
      _ApplicationDataPath = applicationDataPath;
    }
    public void ThreadPoolCallback(System.Object threadContext) {
      _LocReportRunning = true;
      Debug.Log("LocReportWrapper started, select again to kill");
      List<LocKeyReference> locKeyReferences = new List<LocKeyReference>();

      foreach (var localizationDictFile in _LocalizationDictionaries) {
        string fileName = localizationDictFile.Key;
        // Special case file that adds plural etc
        if (fileName == "ItemStrings") {
          continue;
        }
        Debug.Log("Begin parsing " + fileName);
        // Walk through keys to find incidence
        Dictionary<string, LocalizationDictionaryEntry> keysDict = localizationDictFile.Value.Translations;
        foreach (var locKey in keysDict) {
          List<string> pathsContainingKey = new List<string>();
          AddFilesContainingStringInDir(ref pathsContainingKey, VariableNameFromLocalizationKey(locKey.Key), "Assets/", "*.cs", "LocalizationKeys.cs");
          System.Threading.Thread.Sleep(_kSleepTime_ms);
          AddFilesContainingStringInDir(ref pathsContainingKey, locKey.Key, "Assets/", new string[] { ".prefab", ".asset" });
          System.Threading.Thread.Sleep(_kSleepTime_ms);
          AddFilesContainingStringInDir(ref pathsContainingKey, locKey.Key, _ApplicationDataPath + "/../../../lib/anki/products-cozmo-assets/", "*.json");
          System.Threading.Thread.Sleep(_kSleepTime_ms);
          AddFilesContainingStringInDir(ref pathsContainingKey, locKey.Key, _ApplicationDataPath + "/../../../resources/config/engine", "*.json");

          if (pathsContainingKey.Count > 0) {
            LocKeyReference reference = new LocKeyReference();
            reference.Key = locKey.Key;
            reference.Value = locKey.Value.Translation;
            reference.FilesThatReference = pathsContainingKey;
            locKeyReferences.Add(reference);
            Debug.Log("Found: " + reference.Key);
          }
          else {
            Debug.LogWarning(locKey.Key + " is unused, consider removing");
          }
          // breaking instead of returning so function only has one exit and gets cleaned up properly
          if (!_WantsContinueRunning) {
            break;
          }
          System.Threading.Thread.Sleep(_kSleepTime_ms);
        }

        Debug.Log("Completed parsing " + fileName);
        // breaking instead of returning so function only has one exit and gets cleaned up properly
        if (!_WantsContinueRunning) {
          break;
        }
      }
      if (_WantsContinueRunning) {
        locKeyReferences.Sort((x, y) => {
          return x.FilesThatReference.Count.CompareTo(y.FilesThatReference.Count);
        });

        string fileContents = "Loc Value,Loc Key,Files\n";
        foreach (var reference in locKeyReferences) {
          fileContents += "\"" + reference.Value.Replace("\n", " ").Replace("\"", "") + "\"," + reference.Key + ",";
          fileContents += reference.FilesThatReference[0] + "\n";
          for (int i = 1; i < reference.FilesThatReference.Count; i++) {
            fileContents += ",," + reference.FilesThatReference[i] + "\n";
          }
        }

        // Write the to a file
        File.WriteAllText(kLocKeyReferenceReportFilePath, fileContents);
        Debug.Log("Report written to " + kLocKeyReferenceReportFilePath);
      }
      else {
        Debug.Log("Report was cancelled");
      }
      _LocReportRunning = false;
    }
  }

  private const string kLocKeyReferenceReportFilePath = "LocKeyReferenceReport.csv";

  [MenuItem("Cozmo/Localization/Report Loc Key References")]
  private static void ReportLocKeyReferencesThreaded() {
    if (_LocReportRunning == false) {
      // Pass in any info that needs to be on the main thread.
      LocReportWrapper reportThread = new LocReportWrapper(Application.dataPath);
      LocReportWrapper._WantsContinueRunning = true;
      // Application.DataPath can only be called on the main thead
      System.Threading.ThreadPool.QueueUserWorkItem(reportThread.ThreadPoolCallback);
    }
    else {
      Debug.Log("Report is cancelling");
      LocReportWrapper._WantsContinueRunning = false;
    }
  }

  private struct LocKeyReference {
    public string Key;
    public string Value;
    public List<string> FilesThatReference;
  }
}

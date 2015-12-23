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

  private const string kLocalizationFolder = "Assets/StreamingAssets/LocalizedStrings/en-US/";

  public static string[] LocalizationFiles { get { return _LocalizationFiles; } }

  public static LocalizationDictionary CreateLocalizationDictionary() {
    return new LocalizationDictionary() {
      Smartling = new SmartlingBlob() {
        TranslatePaths = new List<string>{ "*/translation" },
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
        SourceKeyPaths = new List<string>{ "/{*}" }
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


  [MenuItem("Cozmo/Localization/Reload Localization Files")]
  public static void Reload() {
    _LocalizationDictionaries.Clear();

    foreach (var file in Directory.GetFiles(kLocalizationFolder, "*.json")) {
      _LocalizationDictionaries[Path.GetFileNameWithoutExtension(file)] = JsonConvert.DeserializeObject<LocalizationDictionary>(File.ReadAllText(file));
    }

    _LocalizationFiles = _LocalizationDictionaries.Keys.ToArray();
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
    foreach(var kvp in _LocalizationDictionaries) {
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

  private const string kGeneratedLocalizationKeysFilePath = "Assets/Libraries/Anki/DriveEngine/Localization/GeneratedKeys/LocalizationKeys.cs";
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

  private static string VariableNameFromLocalizationKey(string localizationKey) {
    string variableName = "k";
    char[] delimiter = { '.', ' ' };
    string[] keyParts = localizationKey.Split(delimiter, StringSplitOptions.RemoveEmptyEntries);
    foreach (string part in keyParts) {
      variableName += char.ToUpper(part[0]) + part.Substring(1);
    }
    return variableName;
  }
}

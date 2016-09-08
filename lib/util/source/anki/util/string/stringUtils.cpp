/**
 * File: stringUtils
 *
 * Author: seichert
 * Created: 07/14/14
 *
 * Description: Utilities for strings
 *
 * Copyright: Anki, Inc. 2014
 *
 **/

#include "stringUtils.h"

#include "json/json.h"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <string>
#include <util/random/randomGenerator.h>
#include <util/UUID/UUID.h>


namespace Anki {
namespace Util {

bool StringCaseInsensitiveEquals(const std::string& s1, const std::string& s2)
{
  size_t sz = s1.size();

  if (s2.size() != sz) {
    return false;
  }

  for (unsigned int i = 0 ; i < sz; i++) {
    if (std::tolower(s1[i]) != std::tolower(s2[i])) {
      return false;
    }
  }

  return true;
}

std::string StringToLower(const std::string& source)
{
  std::string result;
  for (const char c : source) {
    if (c >= 'A' && c <= 'Z') {
      result += 'a' + (c - 'A');
    } else {
      result += c;
    }
  }
  return result;
}

std::string StringToUpper(const std::string& source)
{
  std::string result;
  for (const char c : source) {
    if (c >= 'a' && c <= 'z') {
      result += 'A' + (c - 'a');
    } else {
      result += c;
    }
  }
  return result;
}

std::string StringFromContentsOfFile(const std::string &filename)
{
  std::ifstream in(filename, std::ios::in | std::ios::binary);
  if (in)
  {
    std::string contents;
    in.seekg(0, std::ios::end);
    contents.resize((size_t) in.tellg());
    in.seekg(0, std::ios::beg);
    in.read(&contents[0], contents.size());
    in.close();
    return contents;
  }
  return "";
}

std::string StringMapToJson(const std::map<std::string,std::string> &stringMap)
{
  Json::Value root;

  for (auto const& kv : stringMap) {
    root[kv.first] = kv.second;
  }

  Json::FastWriter writer;
  writer.omitEndingLineFeed();
  std::string outputJson = writer.write(root);

  return outputJson;
}
  
std::map<std::string,std::string> JsonToStringMap(const std::string &jsonString)
{
  std::map<std::string,std::string> stringMap;
  
  Json::Value root;
  Json::Reader reader;
  bool parsingSuccessful = reader.parse(jsonString, root);
  if(parsingSuccessful) {
    for (auto const& id : root.getMemberNames()) {
      stringMap[id] = root[id].asString();
    }
  }
  
  return stringMap;
}

// Return the json string array as a vector of string
std::vector<std::string> JsonToStringVector(const std::string& jsonString)
{
  std::vector<std::string> stringVector;

  Json::Value root;
  Json::Reader reader;
  bool parsingSuccessful = reader.parse(jsonString, root);
  if (parsingSuccessful && root.isArray()) {
    for (Json::ArrayIndex i = 0 ; i < root.size() ; i++) {
      stringVector.push_back(root[i].asString());
    }
  }

  return stringVector;
}

// Read the Json File and convert to a vector of string
std::vector<std::string> JsonFileToStringVector(const std::string& path)
{
  return JsonToStringVector(StringFromContentsOfFile(path));
}


std::string ConvertFromByteVectorToString(const std::vector<uint8_t> &bytes)
{
  return std::string(bytes.begin(), bytes.end());
}
  
void ConvertFromStringToVector(std::vector<uint8_t> &bytes, const std::string &stringValue)
{
  copy(stringValue.begin(), stringValue.end(), back_inserter(bytes));
}

bool StringEndsWith(const std::string& fullString, const std::string& ending)
{
  if (fullString.length() >= ending.length()) {
    return (0 == fullString.compare(fullString.length() - ending.length(),
                                    ending.length(), ending));
  } else {
    return false;
  }
}

bool IsValidUTF8(const uint8_t* b, size_t length)
{
  if (!length) {
    return true;
  }
  if (!b) {
    return false;
  }
  size_t i = 0;
  bool needLowSurrogate = false;
  while (i < length) {
    uint32_t bits = 0;
    uint32_t codePoint = 0;
    uint8_t c = b[i];
    int cb = 0;
    if (c < 0x80) {
      codePoint = (uint32_t) c;
      i++;
    } else if ((c & 0xfc) == 0xfc) {
      // 6 byte UTF-8 sequences are invalid as of RFC 3629
      return false;
    } else if ((c & 0xf8) == 0xf8) {
      // 5 byte UTF-8 sequences are invalid as of RFC 3629
      return false;
    } else if ((c & 0xf0) == 0xf0) {
      if (c & 0x08) {
        return false;
      }
      bits = (uint32_t) (c & 0x07);
      codePoint = bits << 18;
      i++;
      cb = 3;
    } else if ((c & 0xe0) == 0xe0) {
      if (c & 0x10) {
        return false;
      }
      bits = (uint32_t) (c & 0x0f);
      codePoint = bits << 12;
      i++;
      cb = 2;
    } else if ((c & 0xc0) == 0xc0) {
      if (c & 0x20) {
        return false;
      }
      bits = (uint32_t) (c & 0x1f);
      codePoint = bits << 6;
      i++;
      cb = 1;
    } else if ((c & 0x80) == 0x80) {
      return false;
    }

    if (cb && i >= length) {
      return false;
    }

    while (cb) {
      c = b[i];
      if (!((c & 0x80) == 0x80)) {
        return false;
      }
      if (c & 0x40) {
        return false;
      }
      bits = (uint32_t) (c & 0x3f);
      if (cb > 1) {
        bits = bits << ((cb - 1) * 6);
      }
      codePoint |= bits;
      cb--; i++;
      if (cb && i >= length) {
        return false;
      }
    }

    if (codePoint > 0x10FFFF) {
      // RFC 3629 restricted UTF-8 to end at U+10FFFF
      return false;
    }

    bool isHighSurrogate = (codePoint >= 0xd800 && codePoint <= 0xdbff);
    bool isLowSurrogate = (codePoint >= 0xdc00 && codePoint <= 0xdfff);

    if (needLowSurrogate) {
      if (!isLowSurrogate) {
        return false;
      }
      needLowSurrogate = false;
    } else {
      if (isLowSurrogate) {
        return false;
      }
      needLowSurrogate = isHighSurrogate;
    }
  }
  if (needLowSurrogate) {
    return false;
  }
  return true;
}

bool IsValidUTF8(const std::string& s)
{
  return (bool) IsValidUTF8((uint8_t *) s.c_str(), s.length());
}

std::string TruncateUTF8String(const std::string& s, size_t maxLength, size_t minLength)
{
  std::string t = s.substr(0, maxLength);
  while (!IsValidUTF8(t) && t.length() >= minLength) {
    size_t count = t.length() - 1;
    t = t.substr(0, count);
  }
  return t;
}

std::string RemovePII(const std::string& s)
{
  std::string result = s;
  // If the uri points to the username endpoint, it includes PII in the request, must truncate that out
  std::size_t pos = s.find("username");
  
  if (pos != std::string::npos) {
    result = s.substr(0, pos);
  }
  
  return result;
}

std::string GetUUIDString()
{
  Anki::Util::RandomGenerator rand;
  union uuidTranslator {
    UUIDBytes uuidBytes;
    struct uint64s {
      uint64_t ulli1, ulli2;
    } ullis;
  } t1;
  t1.ullis.ulli1 = rand.RandT<uint64_t>();
  t1.ullis.ulli2 = rand.RandT<uint64_t>();
  
  std::string uuidString(StringFromUUIDBytes(&t1.uuidBytes));
  return uuidString;
}

std::string UrlEncodeString(const std::string &str)
{
  // Code from http://stackoverflow.com/questions/154536/encode-decode-urls-in-c
  std::ostringstream escaped;
  escaped.fill('0');
  escaped << std::hex;

  for (std::string::const_iterator i = str.begin(), n = str.end(); i != n; ++i) {
    std::string::value_type c = (*i);

    // Keep alphanumeric and other accepted characters intact
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      escaped << c;
      continue;
    }

    // Any other characters are percent-encoded
    escaped << '%' << std::setw(2) << int((unsigned char) c);
  }
  
  return escaped.str();
}

} // namespace Util
} // namespace Anki

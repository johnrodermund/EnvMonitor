#pragma once

#include <cmath>
#include <cstdio>
#include <string>

//JSON text helpers shared by the readings API upload and the web API.

//Fixed three decimals, or null for NaN and infinity
inline std::string jsonNumber(float value) {
  if (std::isnan(value) || std::isinf(value)) {
    return "null";
  }
  char text[32];
  snprintf(text, sizeof(text), "%.3f", value);
  return text;
}

inline std::string jsonBool(bool value) {
  return value ? "true" : "false";
}

//Quoted JSON string. Bytes of 0x20 and below need an escape. UTF-8 bytes (0x80 and up) pass through.
inline std::string jsonString(const std::string &value) {
  std::string text = "\"";
  for (char c : value) {
    unsigned char byte = (unsigned char)c;
    switch (c) {
      case '"': text += "\\\""; break;
      case '\\': text += "\\\\"; break;
      case '\n': text += "\\n"; break;
      case '\r': text += "\\r"; break;
      case '\t': text += "\\t"; break;
      default:
        if (byte < 0x20) {
          char escape[8];
          snprintf(escape, sizeof(escape), "\\u%04x", byte);
          text += escape;
        } else {
          text += c;
        }
    }
  }
  return text + "\"";
}

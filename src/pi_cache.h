#pragma once

#include <cstdint>
#include <string>
#include "json_text.h"

//Caches the last good reply from each Pi endpoint. No locking: the sketch guards it with a mutex.

enum class PiEndpoint { Stats = 0, Report = 1, Thresholds = 2 };

inline std::string piPath(PiEndpoint e, const std::string &device) {
  if (e == PiEndpoint::Thresholds) return "/thresholds";
  std::string encoded;
  for (char c : device) {
    bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                 c == '-' || c == '_' || c == '.' || c == '~';
    if (plain) {
      encoded += c;
    } else {
      char escape[4];
      snprintf(escape, sizeof(escape), "%%%02X", (unsigned char)c);
      encoded += escape;
    }
  }
  if (e == PiEndpoint::Stats) return "/stats?window=24h&device=" + encoded;
  return "/reports/latest?device=" + encoded;
}

class PiCache {
public:
  static const unsigned long ROUND_MS = 300000;

  //True on the first call, while the round has endpoints left, and again ROUND_MS after the round started
  bool due(unsigned long now) {
    if (!started_ || (nextIndex_ >= COUNT && now - roundStart_ >= ROUND_MS)) {
      started_ = true;
      roundStart_ = now;
      nextIndex_ = 0;
    }
    return nextIndex_ < COUNT;
  }

  //Only call after due() returned true
  PiEndpoint next() {
    PiEndpoint e = (PiEndpoint)nextIndex_;
    if (nextIndex_ < COUNT) nextIndex_++;
    return e;
  }

  //Largest body kept per endpoint, so the sketch can refuse bigger replies before reading them
  static size_t bodyCap(PiEndpoint e) { return capFor(e); }

  //A 200 reply whose body was over bodyCap and was not read. Keeps the previous body, like record().
  void recordTooLarge(PiEndpoint e, unsigned long now) {
    (void)now;
    Entry &entry = entries_[(int)e];
    entry.none = false;
    entry.error = "reply too large";
  }

  void record(PiEndpoint e, int httpCode, const std::string &body, unsigned long now) {
    Entry &entry = entries_[(int)e];
    if (httpCode == 200) {
      std::string text = trim(body);
      entry.none = false;
      if (text.size() > capFor(e)) {
        entry.error = "reply too large";
      } else if (!balancedObject(text)) {
        entry.error = "not JSON";
      } else {
        entry.body = text;
        entry.hasBody = true;
        entry.fetchedAt = now;
        entry.error.clear();
      }
    } else if (httpCode == 404 && e == PiEndpoint::Report) {
      entry.body.clear();
      entry.hasBody = false;
      entry.none = true;
      entry.error.clear();
    } else if (httpCode <= 0) {
      entry.none = false;
      entry.error = "unreachable";
    } else {
      entry.none = false;
      entry.error = "HTTP " + std::to_string(httpCode);
    }
  }

  void markUnconfigured() {
    for (Entry &entry : entries_) entry.error = "Pi not configured";
  }

  std::string json(unsigned long now) const {
    return "{\"stats\":" + entryJson(entries_[0], now) + ",\"report\":" + entryJson(entries_[1], now) +
           ",\"thresholds\":" + entryJson(entries_[2], now) + "}";
  }

  bool thresholdsBody(std::string &out) const {
    const Entry &entry = entries_[2];
    if (!entry.hasBody) return false;
    out = entry.body;
    return true;
  }

private:
  struct Entry {
    std::string body;
    bool hasBody = false;
    bool none = false;
    unsigned long fetchedAt = 0;
    std::string error;
  };
  static const int COUNT = 3;

  Entry entries_[COUNT];
  bool started_ = false;
  int nextIndex_ = 0;
  unsigned long roundStart_ = 0;

  static size_t capFor(PiEndpoint e) {
    switch (e) {
      case PiEndpoint::Stats: return 6144;
      case PiEndpoint::Report: return 4096;
      default: return 2048;
    }
  }

  static std::string trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && isSpace(s[a])) a++;
    while (b > a && isSpace(s[b - 1])) b--;
    return s.substr(a, b - a);
  }

  //One top-level object with balanced {} and [], strings closed. Not a full grammar check.
  static bool balancedObject(const std::string &s) {
    if (s.empty() || s[0] != '{') return false;
    std::string stack;
    bool inString = false;
    for (size_t i = 0; i < s.size(); i++) {
      char c = s[i];
      if (inString) {
        if (c == '\\') i++;
        else if (c == '"') inString = false;
        continue;
      }
      if (c == '"') {
        inString = true;
      } else if (c == '{' || c == '[') {
        stack += c;
        if (c == '{') {  //an object must continue with a key or close
          size_t j = i + 1;
          while (j < s.size() && isSpace(s[j])) j++;
          if (j >= s.size() || (s[j] != '"' && s[j] != '}')) return false;
        }
      } else if (c == '}' || c == ']') {
        if (stack.empty() || stack.back() != (c == '}' ? '{' : '[')) return false;
        stack.pop_back();
        if (stack.empty() && i != s.size() - 1) return false;
      }
    }
    return stack.empty() && !inString;
  }

  static bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

  //Unsigned subtraction handles the millis wrap; a negative 32-bit difference means now is behind
  static unsigned long ageSeconds(const Entry &entry, unsigned long now) {
    int32_t diff = (int32_t)(uint32_t)(now - entry.fetchedAt);
    return diff < 0 ? 0 : (unsigned long)diff / 1000;
  }

  static std::string entryJson(const Entry &entry, unsigned long now) {
    std::string age = entry.hasBody ? std::to_string(ageSeconds(entry, now)) : "null";
    std::string error = entry.error.empty() ? "null" : jsonString(entry.error);
    std::string body = entry.hasBody ? entry.body : "null";
    return "{\"age_s\":" + age + ",\"error\":" + error + ",\"none\":" + jsonBool(entry.none) + ",\"body\":" + body + "}";
  }
};

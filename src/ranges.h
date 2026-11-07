#pragma once

#include <cmath>
#include <cstdlib>
#include <string>
#include "json_text.h"

//Valid and target ranges for pH, EC and water temperature (degrees F).
//The device's own ranges win over the Pi's thresholds, which win over the built-in ones.

struct MetricRange {
  float validLo, validHi, targetLo, targetHi;
};

struct Ranges {
  MetricRange ph, ec, temp;
};

inline Ranges builtinRanges() {
  Ranges r;
  r.ph = {2.0f, 10.0f, 5.5f, 6.5f};
  r.ec = {0.0f, 5000.0f, 200.0f, 1000.0f};
  r.temp = {30.0f, 99.0f, 60.0f, 80.0f};
  return r;
}

//Values are saved with three decimals, so every rule is checked on the rounded values.
//That keeps anything accepted here valid after a save and re-read.
constexpr float RANGE_LIMIT = 100000.0f;

inline float roundTo3(float v) {
  return (float)(std::round((double)v * 1000.0) / 1000.0);
}

inline bool validateMetricRange(const MetricRange &m, const char *name, std::string &error) {
  const float values[4] = {m.validLo, m.validHi, m.targetLo, m.targetHi};
  for (float v : values) {
    if (!std::isfinite(v)) {
      error = std::string(name) + ": values must be finite numbers";
      return false;
    }
    if (v < -RANGE_LIMIT || v > RANGE_LIMIT) {
      error = std::string(name) + ": values must be between -100000 and 100000";
      return false;
    }
  }
  float validLo = roundTo3(m.validLo), validHi = roundTo3(m.validHi);
  float targetLo = roundTo3(m.targetLo), targetHi = roundTo3(m.targetHi);
  if (!(validLo < validHi)) {
    error = std::string(name) + ": valid low must be below valid high";
    return false;
  }
  if (!(targetLo < targetHi)) {
    error = std::string(name) + ": target low must be below target high";
    return false;
  }
  if (targetLo < validLo || targetHi > validHi) {
    error = std::string(name) + ": target must sit inside valid";
    return false;
  }
  return true;
}

inline bool validateRanges(const Ranges &r, std::string &error) {
  return validateMetricRange(r.ph, "ph", error) && validateMetricRange(r.ec, "ec", error) &&
         validateMetricRange(r.temp, "temp", error);
}

//Small scanner for the Pi's thresholds JSON, not a general parser. Extra keys are ignored.
namespace ranges_detail {

inline size_t skipSpace(const std::string &s, size_t i, size_t end) {
  while (i < end && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++;
  return i;
}

//Finds "key" followed by optional whitespace and a colon, within [from, end).
//Returns the index just past the colon, or npos.
inline size_t findKey(const std::string &s, const char *key, size_t from, size_t end) {
  std::string quoted = std::string("\"") + key + "\"";
  size_t pos = from;
  while (pos < end) {
    pos = s.find(quoted, pos);
    if (pos == std::string::npos || pos >= end) return std::string::npos;
    size_t after = skipSpace(s, pos + quoted.size(), end);
    if (after < end && s[after] == ':') return after + 1;
    pos += quoted.size();
  }
  return std::string::npos;
}

//Parses a JSON-style number at i: optional '-', digits, optional '.digits', optional exponent.
//Hex, '+', 'nan' and 'inf' are rejected. Moves i past it.
inline bool readNumber(const std::string &s, size_t &i, size_t end, float &value) {
  i = skipSpace(s, i, end);
  size_t p = i;
  auto digit = [&](size_t k) { return k < end && s[k] >= '0' && s[k] <= '9'; };
  if (p < end && s[p] == '-') p++;
  if (!digit(p)) return false;
  while (digit(p)) p++;
  if (p < end && s[p] == '.') {
    p++;
    if (!digit(p)) return false;
    while (digit(p)) p++;
  }
  if (p < end && (s[p] == 'e' || s[p] == 'E')) {
    p++;
    if (p < end && (s[p] == '+' || s[p] == '-')) p++;
    if (!digit(p)) return false;
    while (digit(p)) p++;
  }
  double parsed = strtod(s.substr(i, p - i).c_str(), nullptr);
  if (!std::isfinite(parsed)) return false;
  value = (float)parsed;
  i = p;
  return true;
}

//Reads "key": [a, b] inside [from, end)
inline bool readPair(const std::string &s, const char *key, size_t from, size_t end, float &lo, float &hi) {
  size_t i = findKey(s, key, from, end);
  if (i == std::string::npos) return false;
  i = skipSpace(s, i, end);
  if (i >= end || s[i] != '[') return false;
  i++;
  if (!readNumber(s, i, end, lo)) return false;
  i = skipSpace(s, i, end);
  if (i >= end || s[i] != ',') return false;
  i++;
  if (!readNumber(s, i, end, hi)) return false;
  i = skipSpace(s, i, end);
  return i < end && s[i] == ']';
}

inline bool readMetric(const std::string &s, const char *name, MetricRange &out, std::string &error) {
  size_t i = findKey(s, name, 0, s.size());
  if (i == std::string::npos) {
    error = std::string("missing \"") + name + "\" in thresholds";
    return false;
  }
  i = skipSpace(s, i, s.size());
  if (i >= s.size() || s[i] != '{') {
    error = std::string(name) + ": expected an object";
    return false;
  }
  size_t end = s.find('}', i);
  if (end == std::string::npos) {
    error = std::string(name) + ": object is not closed";
    return false;
  }
  if (!readPair(s, "valid", i + 1, end, out.validLo, out.validHi)) {
    error = std::string(name) + ": valid must be [low, high]";
    return false;
  }
  if (!readPair(s, "target", i + 1, end, out.targetLo, out.targetHi)) {
    error = std::string(name) + ": target must be [low, high]";
    return false;
  }
  return true;
}

}  // namespace ranges_detail

//Form fields for POST /api/ranges: valid lo, valid hi, target lo, target hi for ph, then ec, then temp
constexpr int RANGE_FIELD_COUNT = 12;

inline const char *rangeFieldName(int i) {
  static const char *const names[RANGE_FIELD_COUNT] = {
      "ph_valid_lo",   "ph_valid_hi",   "ph_target_lo",   "ph_target_hi",
      "ec_valid_lo",   "ec_valid_hi",   "ec_target_lo",   "ec_target_hi",
      "temp_valid_lo", "temp_valid_hi", "temp_target_lo", "temp_target_hi"};
  return names[i];
}

//Each field must be exactly one JSON-style number; a missing field arrives as "". Then validates.
inline bool rangesFromForm(const std::string fields[RANGE_FIELD_COUNT], Ranges &out, std::string &error) {
  static const char *const metrics[3] = {"ph", "ec", "temp"};
  float values[RANGE_FIELD_COUNT];
  for (int i = 0; i < RANGE_FIELD_COUNT; i++) {
    size_t pos = 0;
    if (!ranges_detail::readNumber(fields[i], pos, fields[i].size(), values[i]) || pos != fields[i].size()) {
      error = std::string(metrics[i / 4]) + ": values must be finite numbers";
      return false;
    }
  }
  Ranges parsed;
  MetricRange *targets[3] = {&parsed.ph, &parsed.ec, &parsed.temp};
  for (int m = 0; m < 3; m++) {
    *targets[m] = {values[m * 4], values[m * 4 + 1], values[m * 4 + 2], values[m * 4 + 3]};
  }
  if (!validateRanges(parsed, error)) return false;
  out = parsed;
  return true;
}

inline bool parseThresholds(const std::string &json, Ranges &out, std::string &error) {
  Ranges parsed;
  if (!ranges_detail::readMetric(json, "ph", parsed.ph, error)) return false;
  if (!ranges_detail::readMetric(json, "ec", parsed.ec, error)) return false;
  if (!ranges_detail::readMetric(json, "temp", parsed.temp, error)) return false;
  if (!validateRanges(parsed, error)) return false;
  out = parsed;
  return true;
}

inline std::string metricRangeJson(const MetricRange &m) {
  return "{\"valid\":[" + jsonNumber(m.validLo) + "," + jsonNumber(m.validHi) + "],\"target\":[" +
         jsonNumber(m.targetLo) + "," + jsonNumber(m.targetHi) + "]}";
}

inline std::string rangesJson(const Ranges &r) {
  return "{\"ph\":" + metricRangeJson(r.ph) + ",\"ec\":" + metricRangeJson(r.ec) + ",\"temp\":" +
         metricRangeJson(r.temp) + "}";
}

enum class RangeSource { Device, Pi, Builtin };

inline const char *rangeSourceName(RangeSource source) {
  switch (source) {
    case RangeSource::Device: return "device";
    case RangeSource::Pi: return "pi";
    default: return "builtin";
  }
}

inline RangeSource chooseRanges(bool hasDevice, const Ranges &device, bool hasPi, const Ranges &pi, Ranges &out) {
  if (hasDevice) {
    out = device;
    return RangeSource::Device;
  }
  if (hasPi) {
    out = pi;
    return RangeSource::Pi;
  }
  out = builtinRanges();
  return RangeSource::Builtin;
}

#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "event_detect.h"
#include "json_text.h"
#include "reading_log.h"
#include "sensor_math.h"

//JSON builders for the web API. No dependency on ReadingCycle: the sketch fills StatusView.

//"2026,11-1,04,16,33,06" (log format, month written as text "%m-1") to "2026-11-04T16:33:06", local time with no zone
inline std::string isoFromLogTimestamp(const std::string &ts) {
  int year, month, day, hour, minute, second;
  char tail;
  if (sscanf(ts.c_str(), "%d,%d-1,%d,%d,%d,%d%c", &year, &month, &day, &hour, &minute, &second, &tail) != 6) {
    return "";
  }
  char text[32];
  snprintf(text, sizeof(text), "%04d-%02d-%02dT%02d:%02d:%02d", year, month, day, hour, minute, second);
  return text;
}

//[[time|null, ph, ec, wt], ...] oldest first. The log keeps raw EC; ec here is referred to 25 C with
//ecTempCoef (% per deg C, 0 for raw), so a new coefficient applies to the whole history at once.
template <int N>
std::string readingsJson(const ReadingLog<N> &log, float ecTempCoef = 0) {
  std::string json = "[";
  for (int i = 0; i < log.size(); i++) {
    std::string iso = isoFromLogTimestamp(log.timestampAt(i));
    if (i > 0) json += ",";
    json += "[" + (iso.empty() ? std::string("null") : jsonString(iso));
    json += "," + jsonNumber(log.ph().getOrderedValue(i));
    json += "," + jsonNumber(ecAt25(log.ec().getOrderedValue(i), log.wt().getOrderedValue(i), ecTempCoef));
    json += "," + jsonNumber(log.wt().getOrderedValue(i)) + "]";
  }
  return json + "]";
}

//Mean and sample standard deviation of the log's EC referred to 25 C; 0 and 0 for an empty log
template <int N>
void compensatedEcStats(const ReadingLog<N> &log, float ecTempCoef, float &mean, float &std) {
  int n = log.size();
  double sum = 0, sumSq = 0;
  for (int i = 0; i < n; i++) {
    double ec = ecAt25(log.ec().getOrderedValue(i), log.wt().getOrderedValue(i), ecTempCoef);
    sum += ec;
    sumSq += ec * ec;
  }
  mean = n ? sum / n : 0;
  double var = n > 1 ? (sumSq - sum * sum / n) / (n - 1) : 0;
  std = var > 0 ? sqrt(var) : 0;
}

//% per deg C, 0 to 5; 0 turns EC temperature compensation off. out is unchanged on an error.
inline bool parseEcTempCoef(const std::string &text, float &out, std::string &error) {
  char *end;
  float value = strtof(text.c_str(), &end);
  if (text.empty() || *end || !(value >= 0 && value <= 5)) {
    error = "ec_temp_coef must be a number from 0 to 5 (% per deg C)";
    return false;
  }
  out = value;
  return true;
}

struct StatusView {
  std::string title;
  int intervalMin = 0;
  unsigned long uptimeS = 0;
  unsigned long freeHeap = 0;
  int count = 0;
  std::string lastTime;
  float ph = 0, phPm = 0, ec = 0, wt = 0;
  float means[3] = {0, 0, 0};  //ph, ec, wt
  float stds[3] = {0, 0, 0};
  std::string state = "idle";  //idle, busy or holding
  std::string step;
  std::string job;
  unsigned long warmupElapsedMs = 0;
  unsigned long warmupMs = 0;
  int samplesTaken = 0;
  bool ph4Ready = false;
  float pendingPh4 = 0;
  unsigned long manualSeq = 0;
  float manualPh = 0, manualEc = 0, manualWt = 0;
  unsigned long calibrationSeq = 0;
  float ph4Adc = 0, ph7Adc = 0;
  float lastPhAdc = 0;
  float phSamples[20] = {};
  int phSampleCount = 0;
  float lastEcAdc = 0;
  unsigned long lastWarmupMs = 0;
  int apiQueued = 0;
  int apiLastHttp = 0;
  unsigned long apiFailures = 0;
  unsigned long apiDelivered = 0;
  int eventsCount = 0;
  int eventsUnsynced = 0;
  unsigned eventsDropped = 0;     //unsynced events lost because the store was full
  int eventsClockAhead = 0;       //unsynced because their updated_at is over a day ahead
  int eventsRefused = 0;          //the Pi answered 400/422 to their current version
  int eventsLastHttp = 0;
  float ecTempCoef = 0;  //EC values are referred to 25 C when not 0
};

inline std::string jsonPair(float mean, float std) {
  return "[" + jsonNumber(mean) + "," + jsonNumber(std) + "]";
}

inline std::string statusJson(const StatusView &s) {
  std::string j = "{";
  j += "\"title\":" + jsonString(s.title);
  j += ",\"interval_min\":" + std::to_string(s.intervalMin);
  j += ",\"uptime_s\":" + std::to_string(s.uptimeS);
  j += ",\"free_heap\":" + std::to_string(s.freeHeap);
  j += ",\"ec_temp_coef\":" + jsonNumber(s.ecTempCoef);
  j += ",\"count\":" + std::to_string(s.count);
  if (s.count == 0) {
    j += ",\"last_reading\":null";
  } else {
    std::string iso = isoFromLogTimestamp(s.lastTime);
    j += ",\"last_reading\":{\"time\":" + (iso.empty() ? std::string("null") : jsonString(iso));
    j += ",\"ph\":" + jsonNumber(s.ph);
    j += ",\"ph_pm\":" + jsonNumber(s.phPm);
    j += ",\"ec\":" + jsonNumber(s.ec);
    j += ",\"wt\":" + jsonNumber(s.wt) + "}";
  }
  j += ",\"averages\":{\"ph\":" + jsonPair(s.means[0], s.stds[0]);
  j += ",\"ec\":" + jsonPair(s.means[1], s.stds[1]);
  j += ",\"wt\":" + jsonPair(s.means[2], s.stds[2]) + "}";
  j += ",\"cycle\":{\"state\":" + jsonString(s.state);
  j += ",\"step\":" + jsonString(s.step);
  j += ",\"job\":" + jsonString(s.job);
  j += ",\"warmup_elapsed_ms\":" + std::to_string(s.warmupElapsedMs);
  j += ",\"warmup_ms\":" + std::to_string(s.warmupMs);
  j += ",\"samples\":" + std::to_string(s.samplesTaken);
  j += ",\"sample_total\":20";
  j += ",\"ph4_ready\":" + jsonBool(s.ph4Ready);
  j += ",\"pending_ph4_adc\":" + jsonNumber(s.pendingPh4) + "}";
  j += ",\"manual\":{\"seq\":" + std::to_string(s.manualSeq);
  j += ",\"ph\":" + jsonNumber(s.manualPh);
  j += ",\"ec\":" + jsonNumber(s.manualEc);
  j += ",\"wt\":" + jsonNumber(s.manualWt) + "}";
  j += ",\"calibration\":{\"seq\":" + std::to_string(s.calibrationSeq);
  j += ",\"ph4_adc\":" + jsonNumber(s.ph4Adc);
  j += ",\"ph7_adc\":" + jsonNumber(s.ph7Adc) + "}";
  j += ",\"diag\":{\"last_ph_adc\":" + jsonNumber(s.lastPhAdc);
  j += ",\"ph_samples\":[";
  int sampleCount = s.phSampleCount < 0 ? 0 : (s.phSampleCount > 20 ? 20 : s.phSampleCount);
  for (int i = 0; i < sampleCount; i++) {
    if (i > 0) j += ",";
    j += jsonNumber(s.phSamples[i]);
  }
  j += "],\"last_ec_adc\":" + jsonNumber(s.lastEcAdc);
  j += ",\"last_warmup_ms\":" + std::to_string(s.lastWarmupMs) + "}";
  j += ",\"api\":{\"queued\":" + std::to_string(s.apiQueued);
  j += ",\"last_http\":" + std::to_string(s.apiLastHttp);
  j += ",\"failures\":" + std::to_string(s.apiFailures);
  j += ",\"delivered\":" + std::to_string(s.apiDelivered) + "}";
  j += ",\"events\":{\"count\":" + std::to_string(s.eventsCount);
  j += ",\"unsynced\":" + std::to_string(s.eventsUnsynced);
  j += ",\"dropped_unsynced\":" + std::to_string(s.eventsDropped);
  j += ",\"clock_ahead\":" + std::to_string(s.eventsClockAhead);
  j += ",\"refused\":" + std::to_string(s.eventsRefused);
  j += ",\"last_sync_http\":" + std::to_string(s.eventsLastHttp) + "}";
  return j + "}";
}

inline std::string settingsJson(int intervalMin, const std::string &title, const DetectLimits &limits, float ecTempCoef) {
  return "{\"interval_min\":" + std::to_string(intervalMin) + ",\"title\":" + jsonString(title) +
         ",\"topup_min_drop\":" + jsonNumber(limits.topupMinDrop) +
         ",\"feeding_min_rise\":" + jsonNumber(limits.feedingMinRise) +
         ",\"feeding_enabled\":" + jsonBool(limits.feedingEnabled) + ",\"ec_temp_coef\":" + jsonNumber(ecTempCoef) + "}";
}

//An empty value means "not provided"
inline bool validateSettings(const std::string &interval, const std::string &title, std::string &error) {
  if (interval.empty() && title.empty()) {
    error = "nothing to save";
    return false;
  }
  if (!interval.empty()) {
    bool digits = interval.size() <= 4;
    for (char c : interval) {
      if (c < '0' || c > '9') digits = false;
    }
    int minutes = digits ? atoi(interval.c_str()) : 0;
    if (!digits || minutes < 2 || minutes > 720) {
      error = "interval must be a whole number of minutes from 2 to 720";
      return false;
    }
  }
  if (!title.empty()) {
    bool valid = title.size() <= 40;
    for (char c : title) {
      if ((unsigned char)c < 0x20) valid = false;
    }
    if (!valid) {
      error = "title must be 1-40 characters with no control characters";
      return false;
    }
  }
  return true;
}

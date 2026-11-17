#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "event_detect.h"

//pH swing tracking (spec: docs/superpowers/specs/2026-11-16-ph-swing-design.md). The daily swing record, the
//judgement of each light change, and (Task 3) the history, suggestion rule and temperature watch. Arduino-free.

//One recorded reading, oldest first, clock-set readings only. localDay is yyyymmdd, localMinute 0-1439.
struct SwingSample {
  long long epoch;
  int localDay;
  int localMinute;
  float ph;
  float ec;
  float wtF;
};

struct LightSchedule {
  int onMinute = 420;   //07:00
  int offMinute = 1140; //19:00
};

struct DayMetrics {
  bool ok;
  int day;
  float swing, phMin, phMax, riseSize, fallSize, jitter;  //fallSize is negative
  long long riseAt, fallAt, bigAt;
  float bigBefore, bigAfter;  //the larger of the rise and the fall
  int riseMinute, fallMinute;  //local minute each starts
  int lowMinute;               //local minute of the smoothed low
};

struct ChangeJudgement {
  bool ok;        //false when no change qualifies, or its window is under 75 % covered
  bool lightsOn;
  int key;        //localDay * 2 + (lightsOn ? 1 : 0); 0 when no change qualifies
  long long changeAt, jumpAt;  //changeAt is set whenever a change qualifies, even when ok is false
  float jump, jumpBefore, jumpAfter, jitter, ecMedian;  //jump is the size (absolute) of the largest 1 h change
};

const long long SWING_HOUR_S = 3600;
const long long SWING_PRE_S = 1800;      //1 h changes may start 30 min before a light change...
const long long SWING_POST_S = 3600;     //...up to 60 min after it
const long long SWING_WINDOW_END_S = SWING_POST_S + SWING_HOUR_S;  //so the window ends 2 h after the change
const long long SWING_RECENT_S = 43200;  //jitter and EC median cover the last 12 hours

//Median of the values that aren't NAN, any count; NAN when there are none.
inline float medianMany(std::vector<float> v) {
  v.erase(std::remove_if(v.begin(), v.end(), [](float x) { return std::isnan(x); }), v.end());
  if (v.empty()) {
    return NAN;
  }
  std::sort(v.begin(), v.end());
  size_t n = v.size();
  return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

//pH median of each reading and its neighbours, against single-reading noise.
inline std::vector<float> smoothedPh(const SwingSample *s, int n) {
  std::vector<float> out(n > 0 ? n : 0);
  for (int i = 0; i < n; i++) {
    float w[3];
    int k = 0;
    for (int j = i - 1; j <= i + 1; j++) {
      if (j >= 0 && j < n) {
        w[k++] = s[j].ph;
      }
    }
    out[i] = medianOf(w, k);
  }
  return out;
}

//The first reading at least an hour after reading i, or -1 when none is, or when the reading gap makes it much
//more than an hour.
inline int swingHourLater(const SwingSample *s, int n, int i, int intervalMin) {
  long long maxSpan = SWING_HOUR_S + intervalMin * 90LL;
  for (int j = i + 1; j < n; j++) {
    long long span = s[j].epoch - s[i].epoch;
    if (span >= SWING_HOUR_S) {
      return span <= maxSpan ? j : -1;
    }
  }
  return -1;
}

inline bool swingCovered(int count, long long spanS, int intervalMin) {
  if (intervalMin <= 0) {
    return false;
  }
  return count * 4 >= 3 * (spanS / (intervalMin * 60LL));
}

//Median absolute difference between consecutive raw readings i in [from, to).
inline float swingJitter(const SwingSample *s, int from, int to) {
  std::vector<float> d;
  for (int i = from + 1; i < to; i++) {
    d.push_back(std::fabs(s[i].ph - s[i - 1].ph));
  }
  return medianMany(d);
}

inline DayMetrics dayMetrics(const SwingSample *s, int n, int day, int intervalMin) {
  DayMetrics m{};
  m.day = day;
  int first = -1, last = -1;
  for (int i = 0; i < n; i++) {
    if (s[i].localDay == day) {
      if (first < 0) {
        first = i;
      }
      last = i;
    }
  }
  if (first < 0 || !swingCovered(last - first + 1, 86400, intervalMin)) {
    return m;
  }
  std::vector<float> sm = smoothedPh(s, n);
  m.phMin = INFINITY;
  m.phMax = -INFINITY;
  int riseI = -1, riseJ = -1, fallI = -1, fallJ = -1;
  for (int i = first; i <= last; i++) {
    if (std::isnan(sm[i])) {
      continue;
    }
    if (sm[i] < m.phMin) {
      m.phMin = sm[i];
      m.lowMinute = s[i].localMinute;
    }
    m.phMax = std::max(m.phMax, sm[i]);
    int j = swingHourLater(s, n, i, intervalMin);
    if (j < 0 || std::isnan(sm[j])) {  //j may be after midnight: a change starting late in the day is the day's
      continue;
    }
    float change = sm[j] - sm[i];
    if (change > m.riseSize) {
      m.riseSize = change;
      riseI = i;
      riseJ = j;
    }
    if (change < m.fallSize) {
      m.fallSize = change;
      fallI = i;
      fallJ = j;
    }
  }
  if (std::isinf(m.phMin)) {
    return m;
  }
  m.swing = m.phMax - m.phMin;
  m.riseAt = riseI >= 0 ? s[riseI].epoch : 0;
  m.fallAt = fallI >= 0 ? s[fallI].epoch : 0;
  m.riseMinute = riseI >= 0 ? s[riseI].localMinute : 0;
  m.fallMinute = fallI >= 0 ? s[fallI].localMinute : 0;
  bool riseBigger = m.riseSize >= -m.fallSize;
  int bi = riseBigger ? riseI : fallI, bj = riseBigger ? riseJ : fallJ;
  if (bi >= 0) {
    m.bigAt = s[bi].epoch;
    m.bigBefore = sm[bi];
    m.bigAfter = sm[bj];
  } else {
    m.bigAt = s[first].epoch;
    m.bigBefore = m.bigAfter = sm[first];
  }
  m.jitter = swingJitter(s, first, last + 1);
  m.ok = true;
  return m;
}

//The day whose swing record is due, or 0: the latest day before the newest reading's, once the newest reading is
//at 01:30 or later (so changes starting before midnight have their hour), unless it is lastDay.
const int SWING_DAY_DELAY_MIN = 90;

inline int swingDayToRecord(const SwingSample *s, int n, int lastDay) {
  if (n < 2 || s[n - 1].localMinute < SWING_DAY_DELAY_MIN) {
    return 0;
  }
  for (int k = n - 2; k >= 0; k--) {
    if (s[k].localDay != s[n - 1].localDay) {
      return s[k].localDay != lastDay ? s[k].localDay : 0;
    }
  }
  return 0;
}

//The epoch of minute `minute` on reading i's local day. Local minutes start on whole UTC minutes, so this doesn't
//depend on which reading of the day is used.
inline long long swingMinuteEpoch(const SwingSample &r, int minute) {
  return r.epoch - ((r.epoch % 60) + 60) % 60 + (minute - r.localMinute) * 60LL;
}

inline ChangeJudgement judgeLatestChange(const SwingSample *s, int n, const LightSchedule &l, int intervalMin) {
  ChangeJudgement j{};
  if (n <= 0) {
    return j;
  }
  long long newest = s[n - 1].epoch;
  //The latest change, on any day in the log, whose window has ended
  for (int i = 0; i < n; i++) {
    if (i > 0 && s[i].localDay == s[i - 1].localDay) {
      continue;
    }
    for (int on = 0; on < 2; on++) {
      long long at = swingMinuteEpoch(s[i], on ? l.onMinute : l.offMinute);
      if (at + SWING_WINDOW_END_S <= newest && at > j.changeAt) {
        j.changeAt = at;
        j.lightsOn = on;
        j.key = s[i].localDay * 2 + on;
      }
    }
  }
  if (j.key == 0) {
    return j;
  }
  long long from = j.changeAt - SWING_PRE_S, to = j.changeAt + SWING_WINDOW_END_S;
  int count = 0;
  for (int i = 0; i < n; i++) {
    count += s[i].epoch >= from && s[i].epoch <= to;
  }
  if (!swingCovered(count, to - from, intervalMin)) {
    return j;
  }
  std::vector<float> sm = smoothedPh(s, n);
  bool found = false;
  for (int i = 0; i < n; i++) {
    if (s[i].epoch < from || s[i].epoch > j.changeAt + SWING_POST_S || std::isnan(sm[i])) {
      continue;
    }
    int k = swingHourLater(s, n, i, intervalMin);
    if (k < 0 || std::isnan(sm[k])) {
      continue;
    }
    float size = std::fabs(sm[k] - sm[i]);
    if (!found || size > j.jump) {
      found = true;
      j.jump = size;
      j.jumpAt = s[i].epoch;
      j.jumpBefore = sm[i];
      j.jumpAfter = sm[k];
    }
  }
  if (!found) {
    return j;
  }
  int recent = 0;
  while (recent < n && s[recent].epoch < newest - SWING_RECENT_S) {
    recent++;
  }
  j.jitter = swingJitter(s, recent, n);
  std::vector<float> ec;
  for (int i = recent; i < n; i++) {
    ec.push_back(s[i].ec);
  }
  j.ecMedian = medianMany(ec);
  j.ok = true;
  return j;
}

//The last N values (NAN ignored), oldest first
template <int N>
class ValueHistory {
 public:
  void add(float v) {
    if (std::isnan(v)) {
      return;
    }
    values_.push_back(v);
    if ((int)values_.size() > N) {
      values_.erase(values_.begin());
    }
  }
  int size() const { return (int)values_.size(); }
  float median() const { return medianMany(values_); }
  const std::vector<float> &values() const { return values_; }
  void clear() { values_.clear(); }

 private:
  std::vector<float> values_;
};

//Past light-change jumps and jitter, saved in NVS as "on:v,v;off:v,v;jit:v,v"
struct SwingHistory {
  ValueHistory<14> onJumps, offJumps, jitter;

  std::string saveText() const {
    std::string out;
    const char *names[] = {"on", "off", "jit"};
    const ValueHistory<14> *parts[] = {&onJumps, &offJumps, &jitter};
    for (int p = 0; p < 3; p++) {
      out += std::string(p ? ";" : "") + names[p] + ":";
      for (size_t i = 0; i < parts[p]->values().size(); i++) {
        char num[16];
        snprintf(num, sizeof num, "%s%.4f", i ? "," : "", parts[p]->values()[i]);
        out += num;
      }
    }
    return out;
  }

  //Replaces the history; "" means nothing saved yet. On garbage it returns false and leaves the history empty.
  bool loadText(const std::string &text) {
    onJumps.clear();
    offJumps.clear();
    jitter.clear();
    if (text.empty()) {
      return true;
    }
    const char *names[] = {"on:", "off:", "jit:"};
    ValueHistory<14> *parts[] = {&onJumps, &offJumps, &jitter};
    size_t pos = 0;
    for (int p = 0; p < 3; p++) {
      size_t end = text.find(';', pos);
      std::string part = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
      std::string name = names[p];
      if (part.compare(0, name.size(), name) != 0 || (p < 2 && end == std::string::npos) ||
          (p == 2 && end != std::string::npos)) {
        loadText("");
        return false;
      }
      std::string list = part.substr(name.size());
      size_t at = 0;
      while (at < list.size()) {
        size_t comma = list.find(',', at);
        std::string token = list.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
        char *stop;
        float v = strtof(token.c_str(), &stop);
        if (token.empty() || *stop) {
          loadText("");
          return false;
        }
        parts[p]->add(v);
        at = comma == std::string::npos ? list.size() : comma + 1;
      }
      pos = end + 1;
    }
    return true;
  }
};

//Values as "v,v,v" (4 decimals), and back; parse returns false on garbage
template <int N>
std::string valuesText(const ValueHistory<N> &h) {
  std::string out;
  for (size_t i = 0; i < h.values().size(); i++) {
    char num[16];
    snprintf(num, sizeof num, "%s%.4f", i ? "," : "", h.values()[i]);
    out += num;
  }
  return out;
}

template <int N>
bool parseValues(const std::string &list, ValueHistory<N> &h) {
  size_t at = 0;
  while (at < list.size()) {
    size_t comma = list.find(',', at);
    std::string token = list.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
    char *stop;
    float v = strtof(token.c_str(), &stop);
    if (token.empty() || *stop) {
      return false;
    }
    h.add(v);
    at = comma == std::string::npos ? list.size() : comma + 1;
  }
  return true;
}

inline std::string swingClock(int minute);  //"HH:MM", defined below

//Minutes from the lights-on setting, -720 to 719
inline int minutesFromLightsOn(int minute, const LightSchedule &l) {
  return ((minute - l.onMinute + 720) % 1440 + 1440) % 1440 - 720;
}

const int SHIFT_DAYS = 10;        //7 days of baseline, then 3 recent days
const int SHIFT_RECENT = 3;
const float SHIFT_MIN_MINUTES = 60;

//When each day's biggest pH fall started and when its low was, as minutes from the lights-on setting at the time.
//A lasting move of both, with the setting unchanged, suggests the light timer changed. Saved in NVS "shift" as
//"fall:v,v;low:v,v". Changing the light setting clears it, and so does a warning (the baseline starts over).
struct LightShiftHistory {
  ValueHistory<SHIFT_DAYS> fallOffsets, lowOffsets;

  void add(const DayMetrics &m, const LightSchedule &l) {
    if (!m.ok || !m.fallAt) {
      return;
    }
    fallOffsets.add(minutesFromLightsOn(m.fallMinute, l));
    lowOffsets.add(minutesFromLightsOn(m.lowMinute, l));
  }

  void clear() {
    fallOffsets.clear();
    lowOffsets.clear();
  }

  std::string saveText() const { return "fall:" + valuesText(fallOffsets) + ";low:" + valuesText(lowOffsets); }

  bool loadText(const std::string &text) {
    clear();
    if (text.empty()) {
      return true;
    }
    size_t semi = text.find(';');
    if (text.compare(0, 5, "fall:") != 0 || semi == std::string::npos || text.compare(semi, 5, ";low:") != 0 ||
        !parseValues(text.substr(5, semi - 5), fallOffsets) || !parseValues(text.substr(semi + 5), lowOffsets)) {
      clear();
      return false;
    }
    return true;
  }
};

//Medians of the 7 older and 3 newer offsets. Offsets are minutes on a 24-hour circle, so each is first moved by
//whole days to lie within 12 hours of the oldest: times either side of the -720/+720 wrap stay together.
inline void shiftMedians(const ValueHistory<SHIFT_DAYS> &h, float &before, float &recent) {
  std::vector<float> v = h.values();
  for (float &x : v) {
    while (x - v[0] > 720) {
      x -= 1440;
    }
    while (x - v[0] < -720) {
      x += 1440;
    }
  }
  before = medianMany(std::vector<float>(v.begin(), v.end() - SHIFT_RECENT));
  recent = medianMany(std::vector<float>(v.end() - SHIFT_RECENT, v.end()));
}

//True, with the note asking whether the lights were adjusted, when the last 3 days' fall and low both moved 60
//minutes or more the same way from the 7 days before
inline bool judgeLightShift(const LightShiftHistory &h, const LightSchedule &l, std::string &note) {
  if (h.fallOffsets.size() < SHIFT_DAYS || h.lowOffsets.size() < SHIFT_DAYS) {
    return false;
  }
  float fallBefore, fallRecent, lowBefore, lowRecent;
  shiftMedians(h.fallOffsets, fallBefore, fallRecent);
  shiftMedians(h.lowOffsets, lowBefore, lowRecent);
  float dFall = fallRecent - fallBefore, dLow = lowRecent - lowBefore;
  if (std::fabs(dFall) < SHIFT_MIN_MINUTES || std::fabs(dLow) < SHIFT_MIN_MINUTES || (dFall > 0) != (dLow > 0)) {
    return false;
  }
  auto minuteAt = [&](float offset) { return (((l.onMinute + (int)std::lround(offset)) % 1440) + 1440) % 1440; };
  auto clockAt = [&](float offset) { return swingClock(minuteAt(offset)); };
  //Name the light change nearest the usual fall time
  int usual = minuteAt(fallBefore);
  auto distance = [&](int m) { int d = std::abs(usual - m) % 1440; return std::min(d, 1440 - d); };
  bool nearOn = distance(l.onMinute) <= distance(l.offMinute);
  int move = (int)std::lround(std::fabs(dFall));
  char text[224];
  snprintf(text, sizeof text,
           "pH has been falling about %d:%02d %s than usual for 3 days (around %s instead of %s). Lights %s is still "
           "set to %s. Did the lights get adjusted? If so, update the light times in Settings.",
           move / 60, move % 60, dFall < 0 ? "earlier" : "later", clockAt(fallRecent).c_str(),
           clockAt(fallBefore).c_str(), nearOn ? "on" : "off", swingClock(nearOn ? l.onMinute : l.offMinute).c_str());
  note = text;
  return true;
}

struct SwingLimits {
  float nutrientsOkEc = 275;
  float alertFactor = 1.5f;
};

struct SwingSuggestion {
  bool found = false;
  std::string kind;
  std::string note;
  long long at = 0;
};

const int SWING_MIN_HISTORY = 5;
const long long SWING_SUGGEST_GAP_S = 86400;  //at most one buffer/nutrients suggestion a day, rejected or not

inline bool swingSuggestionAllowed(long long now, long long lastSuggestAt) {
  return now - lastSuggestAt >= SWING_SUGGEST_GAP_S;
}

//Whether value is over factor x the median of past (with enough past values to judge); ratio is value / median
inline bool swingAbove(const ValueHistory<14> &past, float value, float factor, float &ratio) {
  if (past.size() < SWING_MIN_HISTORY) {
    return false;
  }
  float median = past.median();
  if (!(median > 0) || std::isnan(value)) {
    return false;
  }
  ratio = value / median;
  return value > factor * median;
}

//Compares a judged change with the history before it (the caller records it afterwards with recordJudgement)
inline SwingSuggestion judgeSuggestion(const ChangeJudgement &j, const SwingHistory &before, const SwingLimits &lim) {
  SwingSuggestion out;
  if (!j.ok) {
    return out;
  }
  float jumpRatio = 0, jitterRatio = 0;
  bool jump = swingAbove(j.lightsOn ? before.onJumps : before.offJumps, j.jump, lim.alertFactor, jumpRatio);
  bool erratic = swingAbove(before.jitter, j.jitter, lim.alertFactor, jitterRatio);
  if (!jump && !erratic) {
    return out;
  }
  char what[96];
  if (jump) {
    snprintf(what, sizeof what, "pH jumped %.2f at lights %s, %.1fx usual.", j.jump, j.lightsOn ? "on" : "off",
             jumpRatio);
  } else {
    snprintf(what, sizeof what, "pH is erratic: jitter %.3f, %.1fx usual.", j.jitter, jitterRatio);
  }
  char fix[112];
  if (j.ecMedian > lim.nutrientsOkEc) {
    out.kind = "buffer";
    snprintf(fix, sizeof fix, " EC %.0f is above %.0f, so nutrients look fine: add buffer or do a water change.",
             j.ecMedian, lim.nutrientsOkEc);
  } else {
    out.kind = "nutrients";
    if (std::isnan(j.ecMedian)) {
      snprintf(fix, sizeof fix, " EC unknown: add nutrients, buffer, or do a water change.");
    } else {
      snprintf(fix, sizeof fix, " EC %.0f is below %.0f: add nutrients, buffer, or do a water change.", j.ecMedian,
               lim.nutrientsOkEc);
    }
  }
  out.found = true;
  out.note = std::string(what) + fix;
  out.at = j.jumpAt;
  return out;
}

//Adds a judged change's values to the history, whether or not it triggered
inline void recordJudgement(SwingHistory &h, const ChangeJudgement &j) {
  if (!j.ok) {
    return;
  }
  (j.lightsOn ? h.onJumps : h.offJumps).add(j.jump);
  h.jitter.add(j.jitter);
}

inline std::string swingClock(int minute) {
  char text[8];
  snprintf(text, sizeof text, "%02d:%02d", minute / 60, minute % 60);
  return text;
}

//"1:43 after lights on": the offset from local minute t to the nearest light change
inline std::string swingOffset(int t, const LightSchedule &l) {
  auto wrap = [](int d) { return ((d + 720) % 1440 + 1440) % 1440 - 720; };
  int dOn = wrap(t - l.onMinute), dOff = wrap(t - l.offMinute);
  bool on = std::abs(dOn) <= std::abs(dOff);
  int d = on ? dOn : dOff;
  const char *which = on ? "lights on" : "lights off";
  if (d == 0) {
    return std::string("at ") + which;
  }
  char text[40];
  snprintf(text, sizeof text, "%d:%02d %s %s", std::abs(d) / 60, std::abs(d) % 60, d > 0 ? "after" : "before", which);
  return text;
}

//The daily record's note, e.g. "Swing 1.38 (4.96-6.34). Fall -0.52 at 08:43, 1:43 after lights on. ..."
inline std::string swingNote(const DayMetrics &m, const LightSchedule &l) {
  char text[64];
  snprintf(text, sizeof text, "Swing %.2f (%.2f-%.2f).", m.swing, m.phMin, m.phMax);
  std::string out = text;
  if (m.fallAt) {
    snprintf(text, sizeof text, " Fall %.2f at ", m.fallSize);
    out += text + swingClock(m.fallMinute) + ", " + swingOffset(m.fallMinute, l) + ".";
  }
  if (m.riseAt) {
    snprintf(text, sizeof text, " Rise +%.2f at ", m.riseSize);
    out += text + swingClock(m.riseMinute) + ", " + swingOffset(m.riseMinute, l) + ".";
  }
  snprintf(text, sizeof text, " Jitter %.3f.", m.jitter);
  return out + text;
}

//"HH:MM" to a minute of the day
inline bool parseSwingClock(const std::string &text, int &minute) {
  if (text.size() != 5 || text[2] != ':') {
    return false;
  }
  for (int i : {0, 1, 3, 4}) {
    if (text[i] < '0' || text[i] > '9') {
      return false;
    }
  }
  int h = (text[0] - '0') * 10 + text[1] - '0', m = (text[3] - '0') * 10 + text[4] - '0';
  if (h > 23 || m > 59) {
    return false;
  }
  minute = h * 60 + m;
  return true;
}

inline bool parseSwingNumber(const std::string &text, float lo, float hi, float &out) {
  char *end;
  float v = strtof(text.c_str(), &end);
  if (text.empty() || *end || !(v >= lo && v <= hi)) {
    return false;
  }
  out = v;
  return true;
}

//Applies the given swing settings (empty means "not given"), or none of them on an error
inline bool applySwingSettings(const std::string &on, const std::string &off, const std::string &okEc,
                               const std::string &factor, LightSchedule &l, SwingLimits &lim, std::string &error) {
  LightSchedule nl = l;
  SwingLimits nlim = lim;
  if (!on.empty() && !parseSwingClock(on, nl.onMinute)) {
    error = "lights_on must be HH:MM";
    return false;
  }
  if (!off.empty() && !parseSwingClock(off, nl.offMinute)) {
    error = "lights_off must be HH:MM";
    return false;
  }
  if (nl.onMinute == nl.offMinute) {
    error = "lights_on and lights_off must differ";
    return false;
  }
  if (!okEc.empty() && !parseSwingNumber(okEc, 0, 5000, nlim.nutrientsOkEc)) {
    error = "nutrients_ok_ec must be a number from 0 to 5000";
    return false;
  }
  if (!factor.empty() && !parseSwingNumber(factor, 1.1f, 5, nlim.alertFactor)) {
    error = "swing_alert_factor must be a number from 1.1 to 5";
    return false;
  }
  l = nl;
  lim = nlim;
  return true;
}

inline std::string swingSettingsJsonFields(const LightSchedule &l, const SwingLimits &lim) {
  char text[128];
  snprintf(text, sizeof text,
           ",\"lights_on\":\"%s\",\"lights_off\":\"%s\",\"nutrients_ok_ec\":%.3f,\"swing_alert_factor\":%.3f",
           swingClock(l.onMinute).c_str(), swingClock(l.offMinute).c_str(), lim.nutrientsOkEc, lim.alertFactor);
  return text;
}

const long long TEMP_WARN_S = 7200;   //every reading out of range for 2 h warns...
const long long TEMP_REARM_S = 3600;  //...once, until every reading is back in range for 1 h

//Warns once per excursion of water temperature outside [lo, hi] (°F). The state is in RAM only.
class TempWatch {
 public:
  //intervalMin is the reading interval: a window needs 75 % of its expected readings (and at least 2)
  bool check(const long long *epochs, const float *tempF, int n, float lo, float hi, int intervalMin,
             std::string &note, long long &at) {
    if (n <= 0 || std::isnan(lo) || std::isnan(hi)) {
      return false;
    }
    long long newest = epochs[n - 1];
    if (!armed_) {
      if (epochs[0] <= newest - TEMP_REARM_S && covered(epochs, tempF, n, newest - TEMP_REARM_S, intervalMin) &&
          allSide(epochs, tempF, n, lo, hi, newest - TEMP_REARM_S) == 2) {
        armed_ = true;
      }
      return false;
    }
    if (epochs[0] > newest - TEMP_WARN_S || !covered(epochs, tempF, n, newest - TEMP_WARN_S, intervalMin)) {
      return false;
    }
    int side = allSide(epochs, tempF, n, lo, hi, newest - TEMP_WARN_S);
    if (side != 1 && side != -1) {
      return false;
    }
    int start = n - 1;
    while (start > 0 && (std::isnan(tempF[start - 1]) || sideOf(tempF[start - 1], lo, hi) == side)) {
      start--;
    }
    while (std::isnan(tempF[start])) {
      start++;
    }
    char text[128];
    snprintf(text, sizeof text, "Water %.1f \xC2\xB0" "F has been %s target (%.0f \xC2\xB0" "F) for 2 h: check the heater.",
             tempF[n - 1], side > 0 ? "above" : "below", side > 0 ? hi : lo);
    note = text;
    at = epochs[start];
    armed_ = false;
    return true;
  }

 private:
  bool armed_ = true;

  static bool covered(const long long *epochs, const float *tempF, int n, long long from, int intervalMin) {
    int count = 0;
    for (int i = n - 1; i >= 0 && epochs[i] >= from; i--) {
      count += !std::isnan(tempF[i]);
    }
    return count >= 2 && swingCovered(count, epochs[n - 1] - from, intervalMin);
  }

  static int sideOf(float t, float lo, float hi) { return t > hi ? 1 : t < lo ? -1 : 0; }

  //1 or -1 when every reading since `from` is above or below the range, 2 when every one is inside, else 0
  static int allSide(const long long *epochs, const float *tempF, int n, float lo, float hi, long long from) {
    int side = 3;
    for (int i = n - 1; i >= 0 && epochs[i] >= from; i--) {
      if (std::isnan(tempF[i])) {
        continue;
      }
      int s = sideOf(tempF[i], lo, hi);
      s = s == 0 ? 2 : s;
      if (side != 3 && s != side) {
        return 0;
      }
      side = s;
    }
    return side == 3 ? 0 : side;
  }
};

#pragma once

#include <algorithm>
#include <cmath>
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
    m.phMin = std::min(m.phMin, sm[i]);
    m.phMax = std::max(m.phMax, sm[i]);
    int j = swingHourLater(s, n, i, intervalMin);
    if (j < 0 || j > last || std::isnan(sm[j])) {
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

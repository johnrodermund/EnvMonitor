#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>

//Top-up and feeding detection over the 12 most recent readings. The same rule as PiServer
//agent/events.py, which backfills history: compare the median EC of readings 1-6 with 7-12.

struct DetectLimits {
  float topupMinDrop = 3.0f;
  float feedingMinRise = 5.0f;
  bool feedingEnabled = false;
};

//One reading as the detector sees it. NAN means missing.
struct DetectSample {
  long long epoch;  //UTC seconds
  float ec;
  float ph;
};

struct Candidate {
  bool found = false;
  bool topup = false;
  long long occurredAt = 0;  //the first reading after the step
  float ecBefore = NAN, ecAfter = NAN, phBefore = NAN, phAfter = NAN;
  float size = 0;      //absolute change of the median EC
  float meanSize = 0;  //absolute change of the mean EC, breaks exact ties: it peaks at the step itself
};

const int DETECT_SIDE = 6;
const int DETECT_WINDOW = 2 * DETECT_SIDE;
const long long DETECT_MAX_GAP_S = 1500;  //25 minutes

//Median of the values that aren't NAN, or NAN when there are none. Takes at most 16 values.
inline float medianOf(const float *values, int count) {
  float present[16];
  int n = 0;
  for (int i = 0; i < count && n < 16; i++) {
    if (!std::isnan(values[i])) {
      present[n++] = values[i];
    }
  }
  if (n == 0) {
    return NAN;
  }
  std::sort(present, present + n);
  return n % 2 ? present[n / 2] : (present[n / 2 - 1] + present[n / 2]) / 2;
}

//Medians of EC and pH over DETECT_SIDE samples before and DETECT_SIDE after the step
inline void sideMedians(const DetectSample *before, const DetectSample *after, Candidate &c) {
  float ecB[DETECT_SIDE], ecA[DETECT_SIDE], phB[DETECT_SIDE], phA[DETECT_SIDE];
  for (int i = 0; i < DETECT_SIDE; i++) {
    ecB[i] = before[i].ec;
    ecA[i] = after[i].ec;
    phB[i] = before[i].ph;
    phA[i] = after[i].ph;
  }
  c.ecBefore = medianOf(ecB, DETECT_SIDE);
  c.ecAfter = medianOf(ecA, DETECT_SIDE);
  c.phBefore = medianOf(phB, DETECT_SIDE);
  c.phAfter = medianOf(phA, DETECT_SIDE);
}

//last12 holds the 12 most recent samples, oldest first
inline Candidate detectAt(const DetectSample *last12, const DetectLimits &limits) {
  Candidate c;
  for (int i = 0; i < DETECT_WINDOW; i++) {
    if (std::isnan(last12[i].ec)) {
      return c;
    }
    if (i > 0 && last12[i].epoch - last12[i - 1].epoch > DETECT_MAX_GAP_S) {
      return c;
    }
  }
  sideMedians(last12, last12 + DETECT_SIDE, c);
  float meanBefore = 0, meanAfter = 0;
  for (int i = 0; i < DETECT_SIDE; i++) {
    meanBefore += last12[i].ec;
    meanAfter += last12[DETECT_SIDE + i].ec;
  }
  meanBefore /= DETECT_SIDE;
  meanAfter /= DETECT_SIDE;
  float drop = c.ecBefore - c.ecAfter;
  if (drop >= limits.topupMinDrop) {
    c.topup = true;
    c.size = drop;
    c.meanSize = meanBefore - meanAfter;
  } else if (limits.feedingEnabled && -drop >= limits.feedingMinRise) {
    c.topup = false;
    c.size = -drop;
    c.meanSize = meanAfter - meanBefore;
  } else {
    return c;
  }
  c.found = true;
  c.occurredAt = last12[DETECT_SIDE].epoch;
  return c;
}

//Local time as the reading log writes it: "YYYY,MM-1,DD,HH,MM,SS" (month is 1-12, "-1" is literal)
struct LogTime {
  int year, month, day, hour, minute, second;
};

inline bool parseLogTimestamp(const char *text, LogTime &t) {
  return sscanf(text, "%d,%d-1,%d,%d,%d,%d", &t.year, &t.month, &t.day, &t.hour, &t.minute, &t.second) == 6;
}

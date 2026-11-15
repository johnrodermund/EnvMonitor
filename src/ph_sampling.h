#pragma once

#include <algorithm>
#include <cmath>

//A water pump in the tank puts mains hum on the high-impedance pH electrode. Reads exactly 100 ms
//apart all land on the same phase of 60 Hz (6 whole cycles), so the hum became a random offset per
//reading. 20 reads 105 ms apart step 0.3 of a 60 Hz cycle (0.25 of 50 Hz) each, covering whole
//cycles evenly, so the hum and its low harmonics average out of the window's mean.
const unsigned long HUM_SPACING_MS = 105;
const int HUM_WINDOW = 20;

//Paces reads on a fixed grid from the first read, so a late read doesn't shift the ones after it.
//If reads fall a full spacing behind it restarts the grid instead of reading back to back, which
//drains the ADC's sample capacitor and reads low.
class SampleSchedule {

public:

  void reset() {
    started = false;
  }

  //True when a read is due now; counts that read as taken
  bool due(unsigned long now) {
    if (!started) {
      started = true;
      next = now + HUM_SPACING_MS;
      return true;
    }
    if ((long)(now - next) < 0) {
      return false;
    }
    next += HUM_SPACING_MS;
    if ((long)(now - next) >= 0) {
      next = now + HUM_SPACING_MS;
    }
    return true;
  }

private:

  bool started = false;
  unsigned long next = 0;
};

//Mean without the `trim` lowest and `trim` highest values, so a single glitch read can't move it.
//Uses every value when there are too few to trim. Leaves `values` untouched. n is at most 64.
inline float trimmedMean(const float *values, int n, int trim) {
  float sorted[64];
  if (n > 64) {
    n = 64;
  }
  if (n <= 0) {
    return 0;
  }
  std::copy(values, values + n, sorted);
  std::sort(sorted, sorted + n);
  if (n - 2 * trim < 1) {
    trim = 0;
  }
  float total = 0;
  for (int i = trim; i < n - trim; i++) {
    total += sorted[i];
  }
  return total / (n - 2 * trim);
}

//Decides when a freshly powered pH board has settled: once the minimum time has passed and a
//window's hum-free mean is within maxStep ADC counts of the window before it.
class WarmupSettle {

public:

  WarmupSettle(unsigned long minMs_in, float maxStep_in) : minMs(minMs_in), maxStep(maxStep_in) {}

  void reset() {
    windows = 0;
  }

  //Adds a finished window's mean, taken elapsedMs after power-on. True when settled.
  bool add(float mean, unsigned long elapsedMs) {
    bool settled = windows > 0 && elapsedMs >= minMs && std::fabs(mean - previous) <= maxStep;
    previous = mean;
    windows++;
    return settled;
  }

private:

  unsigned long minMs;
  float maxStep;
  int windows = 0;
  float previous = 0;
};

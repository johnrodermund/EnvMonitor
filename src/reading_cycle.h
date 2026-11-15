#pragma once

#include <atomic>
#include <cmath>
#include <string>
#include "ph_sampling.h"
#include "sensor_math.h"

struct Reading {
  std::string timestamp;
  float ph = 0;
  float phUncertainty = 0;
  float ec = 0;
  float wt = 0;
};

//Hardware and persistence the cycle drives, all called from the task running tick()
class CycleHost {

public:

  virtual ~CycleHost() {}
  virtual float readTemperature() = 0;
  virtual float readEc() = 0;
  virtual float readPhAdc() = 0;
  virtual void setPhPower(bool on) = 0;
  virtual std::string timestamp() = 0;
  virtual void recordReading(const Reading &reading) = 0;
  virtual void saveCalibration(float ph4, float ph7) = 0;
  virtual void wipeReadings() = 0;
  virtual void restart() = 0;
};

enum class CycleCommand { None, TakeReading, Hold, MeasurePh4, MeasurePh7, MeasureManual, Restart, WipeAndRestart, Resume };
enum class CycleStatus { Idle, Busy, Holding };
enum class CycleStep { Waiting, Holding, Warmup, Sampling, Restarting };
enum class CycleJob { Reading, Ph4, Ph7, Manual };

//Owns every reading, calibration and restart transition. request(), status() and
//setInterval() are safe to call from web handlers, everything else runs in tick().
//A command interrupts whatever is running, and an interrupted reading records nothing.
class ReadingCycle {

public:

  ReadingCycle(CycleHost &host_in, float ph4_in, float ph7_in, unsigned long interval_in, unsigned long holdTimeout_in)
    : host(host_in), ph4Adc(ph4_in), ph7Adc(ph7_in), candidatePh4(ph4_in), interval(interval_in), holdTimeout(holdTimeout_in) {}

  void configure(float ph4_in, float ph7_in, unsigned long interval_in) {
    ph4Adc = ph4_in;
    ph7Adc = ph7_in;
    candidatePh4 = ph4_in;
    interval = interval_in;
  }

  void request(CycleCommand command) {
    pending = command;
  }

  CycleCommand pendingCommand() const {
    return pending.load();
  }

  CycleStatus status() const {
    if (pending != CycleCommand::None) {
      return CycleStatus::Busy;
    }
    if (phase == Phase::Waiting) {
      return CycleStatus::Idle;
    }
    return phase == Phase::Holding ? CycleStatus::Holding : CycleStatus::Busy;
  }

  void setInterval(unsigned long interval_in) {
    interval = interval_in;
  }

  float ph4() const {
    return ph4Adc;
  }

  float ph7() const {
    return ph7Adc;
  }

  const Reading &manualReading() const {
    return manual;
  }

  //Raw ADC from the last finished pH measurement of any kind, for diagnostics
  float lastPhAdc() const {
    return lastAdc;
  }

  int lastPhSampleCount() const {
    return lastCount;
  }

  float lastPhSample(int index) const {
    return lastSamples[index];
  }

  CycleStep step() const {
    switch (phase) {
      case Phase::Waiting: return CycleStep::Waiting;
      case Phase::Holding: return CycleStep::Holding;
      case Phase::PowerOn:
      case Phase::Warmup: return CycleStep::Warmup;
      case Phase::Sampling: return CycleStep::Sampling;
      case Phase::Restarting: return CycleStep::Restarting;
    }
    return CycleStep::Waiting;
  }

  CycleJob job() const {
    switch (currentJob) {
      case Job::Reading: return CycleJob::Reading;
      case Job::Ph4: return CycleJob::Ph4;
      case Job::Ph7: return CycleJob::Ph7;
      case Job::Manual: return CycleJob::Manual;
    }
    return CycleJob::Reading;
  }

  //Milliseconds of probe warmup done, 0 outside Warmup
  unsigned long warmupElapsed(unsigned long now) const {
    if (phase != Phase::Warmup) {
      return 0;
    }
    //A handler's now can be a little earlier than the tick that set warmupStart
    long diff = (long)(now - warmupStart);
    if (diff <= 0) {
      return 0;
    }
    unsigned long limit = warmupMax;
    unsigned long elapsed = (unsigned long)diff;
    return elapsed > limit ? limit : elapsed;
  }

  //Longest the pH board stays powered waiting to settle
  static unsigned long warmupMs() {
    return warmupMax;
  }

  static unsigned long warmupMinMs() {
    return warmupMin;
  }

  //Largest change in ADC counts between two warmup windows that still counts as settled
  static float settleStepCounts() {
    return settleCounts;
  }

  //How long the last pH warmup lasted from power-on, and whether it settled or hit warmupMax
  unsigned long lastWarmupMs() const {
    return lastWarmup;
  }

  bool lastWarmupSettled() const {
    return lastSettled;
  }

  //Hum-free means of the last warmup's 2.1 s windows, oldest first, to show how the board settled
  int lastWarmupWindowCount() const {
    return lastWindowCount;
  }

  float lastWarmupWindowMean(int index) const {
    return lastWindowMeans[index];
  }

  int samplesTaken() const {
    return phase == Phase::Sampling ? sampleCount : 0;
  }

  static int sampleTotalCount() {
    return sampleTotal;
  }

  bool ph4Ready() const {
    return ph4Measured;
  }

  float pendingPh4() const {
    return candidatePh4;
  }

  unsigned manualSeq() const {
    return manualDone;
  }

  unsigned calibrationSeq() const {
    return calibrationDone;
  }

  void tick(unsigned long now) {
    //Commands can spend seconds on hardware, the next tick brings a fresh now
    if (applyCommand(now)) {
      return;
    }

    switch (phase) {
      case Phase::Waiting:
        if (now - lastReadingStart > interval) {
          startReading(now);
        }
        break;

      case Phase::PowerOn:
        warmupStart = now;
        host.setPhPower(true);
        schedule.reset();
        settle.reset();
        windowSum = 0;
        windowReads = 0;
        windowCount = 0;
        phase = Phase::Warmup;
        break;

      case Phase::Holding:
        if (now - holdStart > holdTimeout) {
          candidatePh4 = ph4Adc;
          ph4Measured = false;
          phase = Phase::Waiting;
        }
        break;

      //The pH board drifts after power-on. Average hum-free windows and start sampling once one
      //matches the window before it, but never before warmupMin, and give up waiting at warmupMax
      //so the probe isn't powered for long.
      case Phase::Warmup:
        if (now - warmupStart >= warmupMax) {
          beginSampling(now, false);
        } else if (schedule.due(now)) {
          windowSum += host.readPhAdc();
          windowReads++;
          if (windowReads == HUM_WINDOW) {
            float mean = windowSum / HUM_WINDOW;
            windowSum = 0;
            windowReads = 0;
            if (windowCount < maxWindows) {
              windowMeans[windowCount++] = mean;
            }
            if (settle.add(mean, now - warmupStart)) {
              beginSampling(now, true);
            }
          }
        }
        break;

      //Same grid as the warmup, so reads never come back to back (that drains the ADC's sample
      //capacitor and reads low) and the window spans whole mains cycles
      case Phase::Sampling:
        if (sampleCount < sampleTotal) {
          if (schedule.due(now)) {
            samples[sampleCount] = host.readPhAdc();
            sampleCount = sampleCount + 1;
          }
        } else {
          host.setPhPower(false);
          finishMeasurement(now);
        }
        break;

      case Phase::Restarting:
        if (now - restartStart > restartDelay) {
          host.restart();
        }
        break;
    }
  }

private:

  enum class Phase { Waiting, Holding, PowerOn, Warmup, Sampling, Restarting };
  enum class Job { Reading, Ph4, Ph7, Manual };

  static const int sampleTotal = HUM_WINDOW;
  static const int sampleTrim = 2;  //lowest and highest reads dropped from the average
  //Starting values until the settle test on the device measures the board: settled means two
  //window means within settleCounts ADC counts (about 0.025 pH)
  static const unsigned long warmupMin = 15000;
  static const unsigned long warmupMax = 60000;
  static constexpr float settleCounts = 4;
  static const int maxWindows = warmupMax / (HUM_WINDOW * HUM_SPACING_MS) + 2;
  static const unsigned long restartDelay = 2000;

  CycleHost &host;
  float ph4Adc;
  float ph7Adc;
  float candidatePh4;
  volatile unsigned long interval;
  unsigned long holdTimeout;

  std::atomic<CycleCommand> pending{ CycleCommand::TakeReading };
  volatile Phase phase = Phase::Waiting;
  volatile Job currentJob = Job::Reading;
  volatile bool ph4Measured = false;
  volatile unsigned manualDone = 0;
  volatile unsigned calibrationDone = 0;
  Reading current;
  Reading manual;
  float samples[sampleTotal];
  volatile int sampleCount = 0;
  SampleSchedule schedule;
  WarmupSettle settle{ warmupMin, settleCounts };
  float windowSum = 0;
  int windowReads = 0;
  float windowMeans[maxWindows] = {};
  int windowCount = 0;
  float lastWindowMeans[maxWindows] = {};
  volatile int lastWindowCount = 0;
  volatile unsigned long lastWarmup = 0;
  volatile bool lastSettled = false;
  float lastSamples[sampleTotal] = {};
  int lastCount = 0;
  float lastAdc = 0;
  unsigned long lastReadingStart = 0;
  volatile unsigned long warmupStart = 0;
  unsigned long holdStart = 0;
  unsigned long restartStart = 0;

  //The command stays pending until applied so status() reads Busy throughout
  bool applyCommand(unsigned long now) {
    CycleCommand command = pending.load();
    if (command == CycleCommand::None) {
      return false;
    }

    //A pH 7 measurement without a fresh pH 4 would pair a stale value, so ignore it untouched
    if (command == CycleCommand::MeasurePh7 && !ph4Measured) {
      pending.compare_exchange_strong(command, CycleCommand::None);
      return true;
    }

    if (phase == Phase::Warmup || phase == Phase::Sampling) {
      host.setPhPower(false);
    }

    switch (command) {
      case CycleCommand::TakeReading:
        startReading(now);
        break;

      case CycleCommand::Resume:
        candidatePh4 = ph4Adc;
        ph4Measured = false;
        lastReadingStart = now;
        phase = Phase::Waiting;
        break;

      case CycleCommand::Hold:
        candidatePh4 = ph4Adc;
        ph4Measured = false;
        lastReadingStart = now;
        enterHold(now);
        break;

      case CycleCommand::MeasurePh4:
        ph4Measured = false;
        startMeasurement(Job::Ph4);
        break;

      case CycleCommand::MeasurePh7:
        startMeasurement(Job::Ph7);
        break;

      case CycleCommand::MeasureManual:
        startMeasurement(Job::Manual);
        break;

      case CycleCommand::WipeAndRestart:
        host.wipeReadings();
        startRestart(now);
        break;

      case CycleCommand::Restart:
        startRestart(now);
        break;

      case CycleCommand::None:
        break;
    }

    pending.compare_exchange_strong(command, CycleCommand::None);
    return true;
  }

  void startReading(unsigned long now) {
    lastReadingStart = now;
    startMeasurement(Job::Reading);
    current = Reading();
    current.wt = host.readTemperature();
    current.ec = host.readEc();
  }

  void startMeasurement(Job newJob) {
    currentJob = newJob;
    phase = Phase::PowerOn;
  }

  void startRestart(unsigned long now) {
    restartStart = now;
    phase = Phase::Restarting;
  }

  void enterHold(unsigned long now) {
    holdStart = now;
    phase = Phase::Holding;
  }

  void beginSampling(unsigned long now, bool settled) {
    lastWarmup = now - warmupStart;
    lastSettled = settled;
    for (int i = 0; i < windowCount; i++) {
      lastWindowMeans[i] = windowMeans[i];
    }
    lastWindowCount = windowCount;
    sampleCount = 0;
    phase = Phase::Sampling;
  }

  void finishMeasurement(unsigned long now) {
    float phAdc = trimmedMean(samples, sampleTotal, sampleTrim);
    for (int i = 0; i < sampleTotal; i++) {
      lastSamples[i] = samples[i];
    }
    lastCount = sampleTotal;
    lastAdc = phAdc;

    switch (currentJob) {
      case Job::Reading:
        current.timestamp = host.timestamp();
        current.ph = phFromAdc(phAdc, ph4Adc, ph7Adc);
        current.phUncertainty = current.ph - phFromAdc(phAdc + sampleSpread(phAdc), ph4Adc, ph7Adc);
        host.recordReading(current);
        phase = Phase::Waiting;
        break;

      case Job::Ph4:
        candidatePh4 = phAdc;
        ph4Measured = true;
        enterHold(now);
        break;

      case Job::Ph7:
        ph4Adc = candidatePh4;
        ph7Adc = phAdc;
        host.saveCalibration(ph4Adc, ph7Adc);
        ph4Measured = false;
        calibrationDone = calibrationDone + 1;
        phase = Phase::Waiting;
        break;

      case Job::Manual:
        manual = Reading();
        manual.ph = phFromAdc(phAdc, ph4Adc, ph7Adc);
        manual.ec = host.readEc();
        manual.wt = host.readTemperature();
        manualDone = manualDone + 1;
        phase = Phase::Waiting;
        break;
    }
  }

  float sampleSpread(float mean) const {
    float total = 0;
    for (int i = 0; i < sampleTotal; i++) {
      total += (mean - samples[i]) * (mean - samples[i]);
    }
    return sqrt(total / sampleTotal);
  }
};

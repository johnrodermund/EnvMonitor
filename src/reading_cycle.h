#pragma once

#include <atomic>
#include <cmath>
#include <string>
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

enum class CycleCommand { None, TakeReading, Hold, MeasurePh4, MeasurePh7, MeasureManual, Restart, WipeAndRestart };
enum class CycleStatus { Idle, Busy, Holding };

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

  void tick(unsigned long now) {
    applyCommand(now);

    switch (phase) {
      case Phase::Waiting:
        if (now - lastReadingStart > interval) {
          startReading(now);
        }
        break;

      case Phase::Holding:
        if (now - holdStart > holdTimeout) {
          candidatePh4 = ph4Adc;
          phase = Phase::Waiting;
        }
        break;

      case Phase::Warmup:
        if (now - warmupStart > warmupTime) {
          sampleCount = 0;
          phase = Phase::Sampling;
        }
        break;

      case Phase::Sampling:
        if (sampleCount < sampleTotal) {
          samples[sampleCount] = host.readPhAdc();
          sampleCount++;
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

  enum class Phase { Waiting, Holding, Warmup, Sampling, Restarting };
  enum class Job { Reading, Ph4, Ph7, Manual };

  static const int sampleTotal = 20;
  static const unsigned long warmupTime = 20000;
  static const unsigned long restartDelay = 2000;

  CycleHost &host;
  float ph4Adc;
  float ph7Adc;
  float candidatePh4;
  volatile unsigned long interval;
  unsigned long holdTimeout;

  std::atomic<CycleCommand> pending{ CycleCommand::TakeReading };
  volatile Phase phase = Phase::Waiting;
  Job job = Job::Reading;
  Reading current;
  Reading manual;
  float samples[sampleTotal];
  int sampleCount = 0;
  unsigned long lastReadingStart = 0;
  unsigned long warmupStart = 0;
  unsigned long holdStart = 0;
  unsigned long restartStart = 0;

  void applyCommand(unsigned long now) {
    CycleCommand command = pending.exchange(CycleCommand::None);
    if (command == CycleCommand::None) {
      return;
    }

    if (phase == Phase::Warmup || phase == Phase::Sampling) {
      host.setPhPower(false);
    }

    switch (command) {
      case CycleCommand::TakeReading:
        startReading(now);
        break;

      case CycleCommand::Hold:
        candidatePh4 = ph4Adc;
        lastReadingStart = now;
        enterHold(now);
        break;

      case CycleCommand::MeasurePh4:
        startMeasurement(Job::Ph4, now);
        break;

      case CycleCommand::MeasurePh7:
        startMeasurement(Job::Ph7, now);
        break;

      case CycleCommand::MeasureManual:
        startMeasurement(Job::Manual, now);
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
  }

  void startReading(unsigned long now) {
    lastReadingStart = now;
    current = Reading();
    current.wt = host.readTemperature();
    current.ec = host.readEc();
    startMeasurement(Job::Reading, now);
  }

  void startMeasurement(Job newJob, unsigned long now) {
    job = newJob;
    host.setPhPower(true);
    warmupStart = now;
    phase = Phase::Warmup;
  }

  void startRestart(unsigned long now) {
    restartStart = now;
    phase = Phase::Restarting;
  }

  void enterHold(unsigned long now) {
    holdStart = now;
    phase = Phase::Holding;
  }

  void finishMeasurement(unsigned long now) {
    float phAdc = 0;
    for (int i = 0; i < sampleTotal; i++) {
      phAdc += samples[i];
    }
    phAdc = phAdc / sampleTotal;

    switch (job) {
      case Job::Reading:
        current.timestamp = host.timestamp();
        current.ph = phFromAdc(phAdc, ph4Adc, ph7Adc);
        current.phUncertainty = current.ph - phFromAdc(phAdc + sampleSpread(phAdc), ph4Adc, ph7Adc);
        host.recordReading(current);
        phase = Phase::Waiting;
        break;

      case Job::Ph4:
        candidatePh4 = phAdc;
        enterHold(now);
        break;

      case Job::Ph7:
        ph4Adc = candidatePh4;
        ph7Adc = phAdc;
        host.saveCalibration(ph4Adc, ph7Adc);
        phase = Phase::Waiting;
        break;

      case Job::Manual:
        manual = Reading();
        manual.ph = phFromAdc(phAdc, ph4Adc, ph7Adc);
        manual.ec = host.readEc();
        manual.wt = host.readTemperature();
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

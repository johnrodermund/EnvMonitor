#pragma once

#include <string>
#include <vector>
#include "reading_cycle.h"

struct FakeHost : CycleHost {
  std::vector<Reading> recorded;
  std::vector<float> adcSamples = {2124};
  float (*adcByIndex)(size_t index) = nullptr;  //when set, replaces adcSamples
  size_t nextSample = 0;
  bool phPower = false;
  float savedPh4 = 0;
  float savedPh7 = 0;
  bool wiped = false;
  bool restarted = false;
  unsigned long *clock = nullptr;  //when set, EC and relay calls take as long as real hardware
  ReadingCycle *watched = nullptr;
  std::vector<CycleStatus> statusDuringHardware;
  unsigned long powerOnAt = 0;
  unsigned long firstSampleAt = 0;
  std::vector<unsigned long> sampleTimes;

  void spendTime(unsigned long duration) {
    if (clock) {
      *clock += duration;
    }
    if (watched) {
      statusDuringHardware.push_back(watched->status());
    }
  }

  float readTemperature() override { return 68; }
  float readEc() override {
    spendTime(2000);
    return 400;
  }
  float readPhAdc() override {
    if (nextSample == 0 && clock) {
      firstSampleAt = *clock;
    }
    if (clock) {
      sampleTimes.push_back(*clock);
    }
    size_t index = nextSample++;
    if (adcByIndex) {
      return adcByIndex(index);
    }
    return adcSamples[index % adcSamples.size()];
  }
  void setPhPower(bool on) override {
    if (on && clock) {
      powerOnAt = *clock;
    }
    phPower = on;
    spendTime(500);
  }
  std::string timestamp() override { return "t"; }
  void recordReading(const Reading &reading) override { recorded.push_back(reading); }
  void saveCalibration(float ph4, float ph7) override {
    savedPh4 = ph4;
    savedPh7 = ph7;
  }
  void wipeReadings() override { wiped = true; }
  void restart() override { restarted = true; }
};

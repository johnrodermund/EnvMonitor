#include <vector>
#include <unity.h>
#include "reading_cycle.h"

const unsigned long interval = 600000;
const unsigned long holdTimeout = 900000;
const unsigned long oneMeasurement = 23000;

struct FakeHost : CycleHost {
  std::vector<Reading> recorded;
  std::vector<float> adcSamples = {2124};
  size_t nextSample = 0;
  bool phPower = false;
  float savedPh4 = 0;
  float savedPh7 = 0;
  bool wiped = false;
  bool restarted = false;

  float readTemperature() override { return 68; }
  float readEc() override { return 400; }
  float readPhAdc() override { return adcSamples[nextSample++ % adcSamples.size()]; }
  void setPhPower(bool on) override { phPower = on; }
  std::string timestamp() override { return "t"; }
  void recordReading(const Reading &reading) override { recorded.push_back(reading); }
  void saveCalibration(float ph4, float ph7) override {
    savedPh4 = ph4;
    savedPh7 = ph7;
  }
  void wipeReadings() override { wiped = true; }
  void restart() override { restarted = true; }
};

void run(ReadingCycle &cycle, unsigned long &now, unsigned long duration) {
  unsigned long end = now + duration;
  while (now < end) {
    cycle.tick(now);
    now += 100;
  }
}

void setUp() {}
void tearDown() {}

void boot_takes_a_first_reading_and_records_it_complete() {
  FakeHost host;
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);
  unsigned long now = 0;

  run(cycle, now, oneMeasurement);

  TEST_ASSERT_EQUAL_INT(1, host.recorded.size());
  TEST_ASSERT_FLOAT_WITHIN(0.001, 5.5, host.recorded[0].ph);
  TEST_ASSERT_EQUAL_FLOAT(400, host.recorded[0].ec);
  TEST_ASSERT_EQUAL_FLOAT(68, host.recorded[0].wt);
  TEST_ASSERT_EQUAL_STRING("t", host.recorded[0].timestamp.c_str());
}

void ph_probe_warms_up_before_sampling_and_powers_off_after() {
  FakeHost host;
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);
  unsigned long now = 0;

  run(cycle, now, 19000);
  TEST_ASSERT_TRUE(host.phPower);
  TEST_ASSERT_EQUAL_INT(0, host.nextSample);

  run(cycle, now, 4000);
  TEST_ASSERT_FALSE(host.phPower);
  TEST_ASSERT_EQUAL_INT(20, host.nextSample);
}

void reading_reports_ph_uncertainty_from_sample_spread() {
  FakeHost host;
  host.adcSamples = {2100, 2148};
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);
  unsigned long now = 0;

  run(cycle, now, oneMeasurement);

  TEST_ASSERT_FLOAT_WITHIN(0.001, 5.5, host.recorded[0].ph);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.1475, host.recorded[0].phUncertainty);
}

void next_reading_waits_for_the_interval() {
  FakeHost host;
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);
  unsigned long now = 0;

  run(cycle, now, 590000);
  TEST_ASSERT_EQUAL_INT(1, host.recorded.size());

  run(cycle, now, 40000);
  TEST_ASSERT_EQUAL_INT(2, host.recorded.size());
}

void interrupted_reading_records_nothing_and_powers_off() {
  FakeHost host;
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);
  unsigned long now = 0;

  run(cycle, now, 5000);
  cycle.request(CycleCommand::Hold);
  run(cycle, now, oneMeasurement);

  TEST_ASSERT_EQUAL_INT(0, host.recorded.size());
  TEST_ASSERT_FALSE(host.phPower);
}

void status_turns_busy_as_soon_as_a_command_is_requested() {
  FakeHost host;
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);
  unsigned long now = 0;
  run(cycle, now, oneMeasurement);
  TEST_ASSERT_TRUE(cycle.status() == CycleStatus::Idle);

  cycle.request(CycleCommand::TakeReading);

  TEST_ASSERT_TRUE(cycle.status() == CycleStatus::Busy);
}

void requested_reading_runs_without_waiting_for_the_interval() {
  FakeHost host;
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);
  unsigned long now = 0;
  run(cycle, now, oneMeasurement);

  cycle.request(CycleCommand::TakeReading);
  run(cycle, now, oneMeasurement);

  TEST_ASSERT_EQUAL_INT(2, host.recorded.size());
}

void abandoned_hold_resumes_sampling() {
  FakeHost host;
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);
  unsigned long now = 0;
  run(cycle, now, oneMeasurement);
  cycle.request(CycleCommand::Hold);

  run(cycle, now, holdTimeout - 1000);
  TEST_ASSERT_TRUE(cycle.status() == CycleStatus::Holding);

  run(cycle, now, 2000 + oneMeasurement);
  TEST_ASSERT_TRUE(cycle.status() != CycleStatus::Holding);
  TEST_ASSERT_EQUAL_INT(2, host.recorded.size());
}

void calibration_measures_both_buffers_then_saves_them() {
  FakeHost host;
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);
  unsigned long now = 0;
  run(cycle, now, oneMeasurement);

  cycle.request(CycleCommand::Hold);
  host.adcSamples = {2400};
  cycle.request(CycleCommand::MeasurePh4);
  run(cycle, now, oneMeasurement);
  TEST_ASSERT_TRUE(cycle.status() == CycleStatus::Holding);

  host.adcSamples = {1900};
  cycle.request(CycleCommand::MeasurePh7);
  run(cycle, now, oneMeasurement);

  TEST_ASSERT_EQUAL_FLOAT(2400, host.savedPh4);
  TEST_ASSERT_EQUAL_FLOAT(1900, host.savedPh7);
  TEST_ASSERT_EQUAL_FLOAT(2400, cycle.ph4());
  TEST_ASSERT_EQUAL_FLOAT(1900, cycle.ph7());
  TEST_ASSERT_TRUE(cycle.status() == CycleStatus::Idle);
}

void abandoned_calibration_keeps_the_old_calibration() {
  FakeHost host;
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);
  unsigned long now = 0;
  run(cycle, now, oneMeasurement);

  host.adcSamples = {2400};
  cycle.request(CycleCommand::MeasurePh4);
  run(cycle, now, oneMeasurement + holdTimeout + 1000);

  TEST_ASSERT_EQUAL_FLOAT(2368, cycle.ph4());
  TEST_ASSERT_EQUAL_FLOAT(0, host.savedPh4);
}

void manual_measurement_is_reported_but_not_recorded() {
  FakeHost host;
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);
  unsigned long now = 0;
  run(cycle, now, oneMeasurement);

  cycle.request(CycleCommand::MeasureManual);
  run(cycle, now, oneMeasurement);

  TEST_ASSERT_EQUAL_INT(1, host.recorded.size());
  TEST_ASSERT_FLOAT_WITHIN(0.001, 5.5, cycle.manualReading().ph);
  TEST_ASSERT_EQUAL_FLOAT(400, cycle.manualReading().ec);
  TEST_ASSERT_EQUAL_FLOAT(68, cycle.manualReading().wt);
  TEST_ASSERT_TRUE(cycle.status() == CycleStatus::Idle);
}

void restart_waits_two_seconds_for_the_response() {
  FakeHost host;
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);
  unsigned long now = 0;
  run(cycle, now, oneMeasurement);

  cycle.request(CycleCommand::Restart);
  run(cycle, now, 1500);
  TEST_ASSERT_FALSE(host.restarted);

  run(cycle, now, 1000);
  TEST_ASSERT_TRUE(host.restarted);
}

void wipe_clears_readings_before_restarting() {
  FakeHost host;
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);
  unsigned long now = 0;
  run(cycle, now, oneMeasurement);

  cycle.request(CycleCommand::WipeAndRestart);
  run(cycle, now, 2500);

  TEST_ASSERT_TRUE(host.wiped);
  TEST_ASSERT_TRUE(host.restarted);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(boot_takes_a_first_reading_and_records_it_complete);
  RUN_TEST(ph_probe_warms_up_before_sampling_and_powers_off_after);
  RUN_TEST(reading_reports_ph_uncertainty_from_sample_spread);
  RUN_TEST(next_reading_waits_for_the_interval);
  RUN_TEST(interrupted_reading_records_nothing_and_powers_off);
  RUN_TEST(status_turns_busy_as_soon_as_a_command_is_requested);
  RUN_TEST(requested_reading_runs_without_waiting_for_the_interval);
  RUN_TEST(abandoned_hold_resumes_sampling);
  RUN_TEST(calibration_measures_both_buffers_then_saves_them);
  RUN_TEST(abandoned_calibration_keeps_the_old_calibration);
  RUN_TEST(manual_measurement_is_reported_but_not_recorded);
  RUN_TEST(restart_waits_two_seconds_for_the_response);
  RUN_TEST(wipe_clears_readings_before_restarting);
  return UNITY_END();
}

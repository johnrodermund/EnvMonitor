#include <vector>
#include <unity.h>
#include "reading_cycle.h"
#include "../fake_host.h"

const unsigned long interval = 600000;
const unsigned long holdTimeout = 900000;
const unsigned long oneMeasurement = 23000;

void run(ReadingCycle &cycle, unsigned long &now, unsigned long duration, unsigned long step = 100) {
  unsigned long end = now + duration;
  while (now < end) {
    cycle.tick(now);
    now += step;
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

void scheduled_reading_gets_the_full_warmup_after_slow_ec_read() {
  FakeHost host;
  unsigned long now = 0;
  host.clock = &now;
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);

  run(cycle, now, oneMeasurement + 3000);

  TEST_ASSERT_EQUAL_INT(1, host.recorded.size());
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(20000, host.firstSampleAt - host.powerOnAt);
}

void status_stays_busy_while_a_requested_reading_starts() {
  FakeHost host;
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);
  unsigned long now = 0;
  run(cycle, now, oneMeasurement);
  host.watched = &cycle;

  cycle.request(CycleCommand::TakeReading);
  run(cycle, now, 100);

  TEST_ASSERT_FALSE(host.statusDuringHardware.empty());
  for (CycleStatus status : host.statusDuringHardware) {
    TEST_ASSERT_TRUE(status == CycleStatus::Busy);
  }
}

void status_stays_busy_while_a_held_measurement_starts() {
  FakeHost host;
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);
  unsigned long now = 0;
  run(cycle, now, oneMeasurement);
  cycle.request(CycleCommand::Hold);
  run(cycle, now, 100);
  host.watched = &cycle;

  cycle.request(CycleCommand::MeasureManual);
  run(cycle, now, 300);

  TEST_ASSERT_FALSE(host.statusDuringHardware.empty());
  for (CycleStatus status : host.statusDuringHardware) {
    TEST_ASSERT_TRUE(status == CycleStatus::Busy);
  }
}

void last_ph_samples_and_average_are_kept_for_diagnostics() {
  FakeHost host;
  host.adcSamples = {2100, 2148};
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);
  unsigned long now = 0;

  run(cycle, now, oneMeasurement);

  TEST_ASSERT_EQUAL_FLOAT(2124, cycle.lastPhAdc());
  TEST_ASSERT_EQUAL_INT(20, cycle.lastPhSampleCount());
  TEST_ASSERT_EQUAL_FLOAT(2100, cycle.lastPhSample(0));
  TEST_ASSERT_EQUAL_FLOAT(2148, cycle.lastPhSample(1));
}

void ph_samples_are_spaced_so_the_adc_can_recover() {
  FakeHost host;
  unsigned long now = 0;
  host.clock = &now;
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);

  run(cycle, now, oneMeasurement + 3000, 10);

  TEST_ASSERT_EQUAL_INT(20, host.sampleTimes.size());
  for (size_t i = 1; i < host.sampleTimes.size(); i++) {
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(100, host.sampleTimes[i] - host.sampleTimes[i - 1]);
  }
}

void resume_ends_hold_without_a_reading() {
  FakeHost host;
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);
  unsigned long now = 0;
  run(cycle, now, oneMeasurement);
  cycle.request(CycleCommand::Hold);
  run(cycle, now, 200);
  TEST_ASSERT_TRUE(cycle.status() == CycleStatus::Holding);
  size_t before = host.recorded.size();

  cycle.request(CycleCommand::Resume);
  run(cycle, now, 200);

  TEST_ASSERT_TRUE(cycle.status() == CycleStatus::Idle);
  TEST_ASSERT_TRUE(cycle.step() == CycleStep::Waiting);
  TEST_ASSERT_EQUAL_INT(before, host.recorded.size());
}

void resume_during_sampling_powers_off_and_discards() {
  FakeHost host;
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);
  unsigned long now = 0;
  run(cycle, now, 21000);
  TEST_ASSERT_TRUE(host.phPower);

  cycle.request(CycleCommand::Resume);
  run(cycle, now, oneMeasurement);

  TEST_ASSERT_FALSE(host.phPower);
  TEST_ASSERT_EQUAL_INT(0, host.recorded.size());
}

void step_and_progress_follow_the_reading() {
  FakeHost host;
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);
  unsigned long now = 0;
  TEST_ASSERT_TRUE(cycle.step() == CycleStep::Waiting);
  TEST_ASSERT_TRUE(cycle.job() == CycleJob::Reading);

  cycle.tick(now);  //applies the pending reading, entering PowerOn
  now += 100;
  cycle.tick(now);  //PowerOn becomes Warmup
  unsigned long warmupBegan = now;
  TEST_ASSERT_TRUE(cycle.step() == CycleStep::Warmup);
  TEST_ASSERT_EQUAL_UINT32(0, cycle.warmupElapsed(now));

  now = warmupBegan + 12000;
  cycle.tick(now);
  TEST_ASSERT_EQUAL_UINT32(12000, cycle.warmupElapsed(now));
  TEST_ASSERT_EQUAL_UINT32(20000, ReadingCycle::warmupMs());
  TEST_ASSERT_EQUAL_INT(20, ReadingCycle::sampleTotalCount());

  now = warmupBegan + 20100;
  cycle.tick(now);
  TEST_ASSERT_TRUE(cycle.step() == CycleStep::Sampling);
  TEST_ASSERT_EQUAL_UINT32(0, cycle.warmupElapsed(now));

  int last = 0;
  for (int i = 0; i < 40 && cycle.step() == CycleStep::Sampling; i++) {
    now += 100;
    cycle.tick(now);
    if (cycle.step() != CycleStep::Sampling) {
      break;
    }
    TEST_ASSERT_TRUE(cycle.samplesTaken() >= last);
    last = cycle.samplesTaken();
  }
  TEST_ASSERT_EQUAL_INT(20, last);
  now += 100;
  cycle.tick(now);
  TEST_ASSERT_EQUAL_INT(0, cycle.samplesTaken());
}

void ph4_ready_only_after_ph4_measurement() {
  FakeHost host;
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);
  unsigned long now = 0;
  run(cycle, now, oneMeasurement);

  cycle.request(CycleCommand::Hold);
  run(cycle, now, 200);
  TEST_ASSERT_FALSE(cycle.ph4Ready());

  host.adcSamples = {2400};
  cycle.request(CycleCommand::MeasurePh4);
  run(cycle, now, oneMeasurement);
  TEST_ASSERT_TRUE(cycle.ph4Ready());
  TEST_ASSERT_EQUAL_FLOAT(2400, cycle.pendingPh4());

  cycle.request(CycleCommand::Resume);
  run(cycle, now, 200);
  TEST_ASSERT_FALSE(cycle.ph4Ready());
  TEST_ASSERT_EQUAL_FLOAT(2368, cycle.pendingPh4());
}

void manual_and_calibration_seq_increment() {
  FakeHost host;
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);
  unsigned long now = 0;
  run(cycle, now, oneMeasurement);
  unsigned manual0 = cycle.manualSeq();
  unsigned cal0 = cycle.calibrationSeq();

  cycle.request(CycleCommand::MeasureManual);
  run(cycle, now, oneMeasurement);
  TEST_ASSERT_EQUAL_UINT32(manual0 + 1, cycle.manualSeq());
  TEST_ASSERT_EQUAL_UINT32(cal0, cycle.calibrationSeq());

  cycle.request(CycleCommand::Hold);
  cycle.request(CycleCommand::MeasurePh4);
  run(cycle, now, oneMeasurement);
  cycle.request(CycleCommand::MeasurePh7);
  run(cycle, now, oneMeasurement);
  TEST_ASSERT_EQUAL_UINT32(cal0 + 1, cycle.calibrationSeq());
  TEST_ASSERT_EQUAL_UINT32(manual0 + 1, cycle.manualSeq());
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
  RUN_TEST(scheduled_reading_gets_the_full_warmup_after_slow_ec_read);
  RUN_TEST(status_stays_busy_while_a_requested_reading_starts);
  RUN_TEST(status_stays_busy_while_a_held_measurement_starts);
  RUN_TEST(last_ph_samples_and_average_are_kept_for_diagnostics);
  RUN_TEST(ph_samples_are_spaced_so_the_adc_can_recover);
  RUN_TEST(resume_ends_hold_without_a_reading);
  RUN_TEST(resume_during_sampling_powers_off_and_discards);
  RUN_TEST(step_and_progress_follow_the_reading);
  RUN_TEST(ph4_ready_only_after_ph4_measurement);
  RUN_TEST(manual_and_calibration_seq_increment);
  return UNITY_END();
}

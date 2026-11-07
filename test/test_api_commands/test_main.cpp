#include <string>
#include <unity.h>
#include "api_commands.h"
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

void unknown_is_400() {
  FakeHost host;
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);
  CommandResult r = dispatchCommand("bogus", cycle);
  TEST_ASSERT_EQUAL_INT(400, r.httpCode);
  TEST_ASSERT_EQUAL_STRING("unknown command", r.message.c_str());
}

void cancel_always_accepted_even_when_busy() {
  FakeHost host;
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);
  unsigned long now = 0;
  run(cycle, now, 5000);
  TEST_ASSERT_TRUE(cycle.status() == CycleStatus::Busy);

  CommandResult r = dispatchCommand("cancel", cycle);
  TEST_ASSERT_EQUAL_INT(202, r.httpCode);
  TEST_ASSERT_EQUAL_STRING("accepted", r.message.c_str());

  TEST_ASSERT_EQUAL_INT(202, dispatchCommand("restart", cycle).httpCode);
  TEST_ASSERT_EQUAL_INT(202, dispatchCommand("reset", cycle).httpCode);
}

void cancel_rejected_while_restarting() {
  FakeHost host;
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);
  unsigned long now = 0;
  run(cycle, now, oneMeasurement);
  TEST_ASSERT_EQUAL_INT(202, dispatchCommand("restart", cycle).httpCode);
  run(cycle, now, 1000);
  TEST_ASSERT_TRUE(cycle.step() == CycleStep::Restarting);

  CommandResult r = dispatchCommand("cancel", cycle);
  TEST_ASSERT_EQUAL_INT(409, r.httpCode);
  TEST_ASSERT_EQUAL_STRING("restarting", r.message.c_str());
  TEST_ASSERT_EQUAL_INT(202, dispatchCommand("restart", cycle).httpCode);
  TEST_ASSERT_EQUAL_INT(202, dispatchCommand("reset", cycle).httpCode);
}

void busy_rejects_reading_with_step() {
  FakeHost host;
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);
  unsigned long now = 0;
  run(cycle, now, 5000);
  TEST_ASSERT_TRUE(cycle.step() == CycleStep::Warmup);
  CommandResult r = dispatchCommand("reading", cycle);
  TEST_ASSERT_EQUAL_INT(409, r.httpCode);
  TEST_ASSERT_EQUAL_STRING("busy: warmup", r.message.c_str());

  run(cycle, now, 16000);
  TEST_ASSERT_TRUE(cycle.step() == CycleStep::Sampling);
  TEST_ASSERT_EQUAL_STRING("busy: sampling", dispatchCommand("reading", cycle).message.c_str());
}

void busy_while_command_pending() {
  FakeHost host;
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);
  unsigned long now = 0;
  run(cycle, now, oneMeasurement);
  TEST_ASSERT_EQUAL_INT(202, dispatchCommand("measure_start", cycle).httpCode);
  CommandResult r = dispatchCommand("reading", cycle);
  TEST_ASSERT_EQUAL_INT(409, r.httpCode);
  TEST_ASSERT_EQUAL_STRING("busy: pending", r.message.c_str());
}

void reading_accepted_when_idle() {
  FakeHost host;
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);
  unsigned long now = 0;
  run(cycle, now, oneMeasurement);
  CommandResult r = dispatchCommand("reading", cycle);
  TEST_ASSERT_EQUAL_INT(202, r.httpCode);
  run(cycle, now, oneMeasurement);
  TEST_ASSERT_EQUAL_INT(2, host.recorded.size());
}

void reading_rejected_while_holding() {
  FakeHost host;
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);
  unsigned long now = 0;
  run(cycle, now, oneMeasurement);
  dispatchCommand("measure_start", cycle);
  run(cycle, now, 1000);
  TEST_ASSERT_TRUE(cycle.status() == CycleStatus::Holding);
  CommandResult r = dispatchCommand("reading", cycle);
  TEST_ASSERT_EQUAL_INT(409, r.httpCode);
  TEST_ASSERT_EQUAL_STRING("finish or cancel the current step first", r.message.c_str());
}

void measure_needs_hold() {
  FakeHost host;
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);
  unsigned long now = 0;
  run(cycle, now, oneMeasurement);
  CommandResult r = dispatchCommand("measure", cycle);
  TEST_ASSERT_EQUAL_INT(409, r.httpCode);
  TEST_ASSERT_EQUAL_STRING("start first", r.message.c_str());
  TEST_ASSERT_EQUAL_STRING("start first", dispatchCommand("calibrate_ph4", cycle).message.c_str());

  dispatchCommand("measure_start", cycle);
  run(cycle, now, 1000);
  unsigned manual0 = cycle.manualSeq();
  TEST_ASSERT_EQUAL_INT(202, dispatchCommand("measure", cycle).httpCode);
  run(cycle, now, oneMeasurement);
  TEST_ASSERT_EQUAL_UINT32(manual0 + 1, cycle.manualSeq());
}

void calibrate_ph7_needs_ph4() {
  FakeHost host;
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);
  unsigned long now = 0;
  run(cycle, now, oneMeasurement);
  TEST_ASSERT_EQUAL_STRING("start first", dispatchCommand("calibrate_ph7", cycle).message.c_str());

  dispatchCommand("calibrate_start", cycle);
  run(cycle, now, 1000);
  CommandResult r = dispatchCommand("calibrate_ph7", cycle);
  TEST_ASSERT_EQUAL_INT(409, r.httpCode);
  TEST_ASSERT_EQUAL_STRING("measure pH 4 first", r.message.c_str());
}

void calibrate_flow_end_to_end() {
  FakeHost host;
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);
  unsigned long now = 0;
  run(cycle, now, oneMeasurement);
  unsigned cal0 = cycle.calibrationSeq();

  TEST_ASSERT_EQUAL_INT(202, dispatchCommand("calibrate_start", cycle).httpCode);
  run(cycle, now, 1000);
  TEST_ASSERT_TRUE(cycle.status() == CycleStatus::Holding);

  host.adcSamples = {2400};
  TEST_ASSERT_EQUAL_INT(202, dispatchCommand("calibrate_ph4", cycle).httpCode);
  run(cycle, now, oneMeasurement);
  TEST_ASSERT_TRUE(cycle.ph4Ready());

  host.adcSamples = {1900};
  TEST_ASSERT_EQUAL_INT(202, dispatchCommand("calibrate_ph7", cycle).httpCode);
  run(cycle, now, oneMeasurement);

  TEST_ASSERT_EQUAL_UINT32(cal0 + 1, cycle.calibrationSeq());
  TEST_ASSERT_EQUAL_FLOAT(2400, cycle.ph4());
  TEST_ASSERT_EQUAL_FLOAT(1900, cycle.ph7());
}

void cancel_cannot_overwrite_pending_reset_or_restart() {
  FakeHost host;
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);
  unsigned long now = 0;
  run(cycle, now, oneMeasurement);
  TEST_ASSERT_EQUAL_INT(202, dispatchCommand("reset", cycle).httpCode);
  CommandResult r = dispatchCommand("cancel", cycle);
  TEST_ASSERT_EQUAL_INT(409, r.httpCode);
  TEST_ASSERT_EQUAL_STRING("restarting", r.message.c_str());
  run(cycle, now, 1000);
  TEST_ASSERT_TRUE(host.wiped);

  FakeHost host2;
  ReadingCycle cycle2(host2, 2368, 1880, interval, holdTimeout);
  now = 0;
  run(cycle2, now, oneMeasurement);
  dispatchCommand("restart", cycle2);
  TEST_ASSERT_EQUAL_INT(409, dispatchCommand("cancel", cycle2).httpCode);
  run(cycle2, now, 1000);
  TEST_ASSERT_TRUE(cycle2.step() == CycleStep::Restarting);
  run(cycle2, now, 3000);
  TEST_ASSERT_TRUE(host2.restarted);
}

void cancel_from_holding_returns_to_idle() {
  FakeHost host;
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);
  unsigned long now = 0;
  run(cycle, now, oneMeasurement);
  dispatchCommand("measure_start", cycle);
  run(cycle, now, 1000);
  TEST_ASSERT_TRUE(cycle.status() == CycleStatus::Holding);
  TEST_ASSERT_EQUAL_INT(202, dispatchCommand("cancel", cycle).httpCode);
  run(cycle, now, 1000);
  TEST_ASSERT_TRUE(cycle.status() == CycleStatus::Idle);
}

void reset_wipes_readings_and_restart_does_not() {
  FakeHost host;
  ReadingCycle cycle(host, 2368, 1880, interval, holdTimeout);
  unsigned long now = 0;
  run(cycle, now, oneMeasurement);
  dispatchCommand("restart", cycle);
  run(cycle, now, 4000);
  TEST_ASSERT_TRUE(host.restarted);
  TEST_ASSERT_FALSE(host.wiped);

  FakeHost host2;
  ReadingCycle cycle2(host2, 2368, 1880, interval, holdTimeout);
  now = 0;
  run(cycle2, now, oneMeasurement);
  dispatchCommand("reset", cycle2);
  run(cycle2, now, 4000);
  TEST_ASSERT_TRUE(host2.wiped);
  TEST_ASSERT_TRUE(host2.restarted);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(unknown_is_400);
  RUN_TEST(cancel_always_accepted_even_when_busy);
  RUN_TEST(cancel_rejected_while_restarting);
  RUN_TEST(cancel_cannot_overwrite_pending_reset_or_restart);
  RUN_TEST(cancel_from_holding_returns_to_idle);
  RUN_TEST(reset_wipes_readings_and_restart_does_not);
  RUN_TEST(busy_rejects_reading_with_step);
  RUN_TEST(busy_while_command_pending);
  RUN_TEST(reading_accepted_when_idle);
  RUN_TEST(reading_rejected_while_holding);
  RUN_TEST(measure_needs_hold);
  RUN_TEST(calibrate_ph7_needs_ph4);
  RUN_TEST(calibrate_flow_end_to_end);
  return UNITY_END();
}

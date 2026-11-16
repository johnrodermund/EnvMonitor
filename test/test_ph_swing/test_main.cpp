#include <cmath>
#include <unity.h>
#include "ph_swing.h"

//Local time is UTC in these tests: DAY0 is a midnight, D0 its day, D1 the next
const long long DAY0 = 1800000000LL - 1800000000LL % 86400;
const long long DAY1 = DAY0 + 86400;
const int D0 = 20270114;
const int D1 = 20270115;

SwingSample buf[400];
int nbuf = 0;

void setUp() { nbuf = 0; }
void tearDown() {}

//Readings every `every` minutes from fromMinute (inclusive) to toMinute (exclusive), EC 250, 70 °F
template <typename F>
void addDay(long long start, int day, F phAt, int fromMinute = 0, int toMinute = 1440, int every = 10) {
  for (int m = fromMinute; m < toMinute; m += every) {
    SwingSample s;
    s.epoch = start + m * 60LL;
    s.localDay = day;
    s.localMinute = m;
    s.ph = phAt(m);
    s.ec = 250;
    s.wtF = 70;
    buf[nbuf++] = s;
  }
}

bool between(long long v, long long lo, long long hi) { return v >= lo && v <= hi; }

void day_swing_rise_fall_and_times() {
  addDay(DAY0, D0, [](int m) { return (m >= 540 && m < 1260) ? 6.0f : 5.0f; });
  DayMetrics d = dayMetrics(buf, nbuf, D0, 10);
  TEST_ASSERT_TRUE(d.ok);
  TEST_ASSERT_EQUAL_INT(D0, d.day);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 1.0, d.swing);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 5.0, d.phMin);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 6.0, d.phMax);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 1.0, d.riseSize);
  TEST_ASSERT_FLOAT_WITHIN(0.001, -1.0, d.fallSize);
  TEST_ASSERT_TRUE(between(d.riseAt, DAY0 + 480 * 60, DAY0 + 540 * 60));
  TEST_ASSERT_TRUE(between(d.fallAt, DAY0 + 1200 * 60, DAY0 + 1260 * 60));
  TEST_ASSERT_FLOAT_WITHIN(0.001, 1.0, std::fabs(d.bigAfter - d.bigBefore));
}

void single_spike_is_smoothed_away() {
  addDay(DAY0, D0, [](int m) { return m == 600 ? 9.0f : 5.0f; });
  DayMetrics d = dayMetrics(buf, nbuf, D0, 10);
  TEST_ASSERT_TRUE(d.ok);
  TEST_ASSERT_TRUE(d.swing < 0.05f);
}

void day_under_coverage_is_skipped() {
  addDay(DAY0, D0, [](int) { return 5.0f; }, 0, 1000);  //100 of 144 readings
  TEST_ASSERT_FALSE(dayMetrics(buf, nbuf, D0, 10).ok);
}

void jitter_is_median_successive_difference() {
  addDay(DAY0, D0, [](int m) { return 5.0f + ((m / 10) % 2) * 0.02f + (m >= 720 ? 0.5f : 0.0f); });
  DayMetrics d = dayMetrics(buf, nbuf, D0, 10);
  TEST_ASSERT_TRUE(d.ok);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.02, d.jitter);
}

void change_judged_after_window_ends() {
  addDay(DAY1, D1, [](int m) { return m >= 430 ? 5.8f : 5.0f; }, 0, 541);  //newest at 09:00
  ChangeJudgement j = judgeLatestChange(buf, nbuf, LightSchedule(), 10);
  TEST_ASSERT_TRUE(j.ok);
  TEST_ASSERT_TRUE(j.lightsOn);
  TEST_ASSERT_EQUAL_INT(D1 * 2 + 1, j.key);
  TEST_ASSERT_EQUAL_INT64(DAY1 + 420 * 60, j.changeAt);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 0.8, j.jump);
  TEST_ASSERT_TRUE(between(j.jumpAt, DAY1 + 390 * 60, DAY1 + 430 * 60));
  TEST_ASSERT_FLOAT_WITHIN(0.001, 5.0, j.jumpBefore);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 5.8, j.jumpAfter);
}

void change_not_judged_before_its_window_ends() {
  addDay(DAY0, D0, [](int) { return 5.0f; });
  addDay(DAY1, D1, [](int) { return 5.0f; }, 0, 531);  //newest at 08:50
  ChangeJudgement j = judgeLatestChange(buf, nbuf, LightSchedule(), 10);
  TEST_ASSERT_TRUE(j.ok);
  TEST_ASSERT_FALSE(j.lightsOn);
  TEST_ASSERT_EQUAL_INT(D0 * 2, j.key);
  TEST_ASSERT_EQUAL_INT64(DAY0 + 1140 * 60, j.changeAt);
}

void window_under_coverage_is_skipped() {
  addDay(DAY1, D1, [](int) { return 5.0f; }, 0, 420);
  addDay(DAY1, D1, [](int) { return 5.0f; }, 500, 541);
  ChangeJudgement j = judgeLatestChange(buf, nbuf, LightSchedule(), 10);
  TEST_ASSERT_FALSE(j.ok);
  TEST_ASSERT_EQUAL_INT(D1 * 2 + 1, j.key);  //still named, so the caller marks it judged
  TEST_ASSERT_EQUAL_INT64(DAY1 + 420 * 60, j.changeAt);
}

void change_window_may_cross_midnight() {
  LightSchedule l;
  l.onMinute = 450;
  l.offMinute = 1410;  //16/8: 07:30 to 23:30
  addDay(DAY0, D0, [](int) { return 6.0f; });
  addDay(DAY1, D1, [](int m) { return m >= 20 ? 6.6f : 6.0f; }, 0, 101);  //newest at 01:40
  ChangeJudgement j = judgeLatestChange(buf, nbuf, l, 10);
  TEST_ASSERT_TRUE(j.ok);
  TEST_ASSERT_FALSE(j.lightsOn);
  TEST_ASSERT_EQUAL_INT(D0 * 2, j.key);
  TEST_ASSERT_EQUAL_INT64(DAY0 + 1410 * 60, j.changeAt);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 0.6, j.jump);
}

void same_change_has_same_key() {
  addDay(DAY0, D0, [](int) { return 5.0f; }, 1, 1440);  //odd seconds don't move the change time
  for (int i = 0; i < nbuf; i++) {
    buf[i].epoch += 17;
  }
  addDay(DAY1, D1, [](int) { return 5.0f; }, 0, 541);
  ChangeJudgement a = judgeLatestChange(buf, nbuf, LightSchedule(), 10);
  addDay(DAY1, D1, [](int) { return 5.0f; }, 550, 551);
  ChangeJudgement b = judgeLatestChange(buf, nbuf, LightSchedule(), 10);
  TEST_ASSERT_TRUE(a.ok);
  TEST_ASSERT_EQUAL_INT(a.key, b.key);
  TEST_ASSERT_EQUAL_INT64(a.changeAt, b.changeAt);
  ChangeJudgement c = judgeLatestChange(buf + 1, nbuf - 1, LightSchedule(), 10);  //oldest reading dropped
  TEST_ASSERT_EQUAL_INT64(a.changeAt, c.changeAt);
}

void ec_median_covers_last_12_hours() {
  addDay(DAY0, D0, [](int) { return 5.0f; });
  addDay(DAY1, D1, [](int m) { return 5.0f + ((m / 10) % 2) * 0.03f; }, 0, 541);
  long long newest = buf[nbuf - 1].epoch;
  for (int i = 0; i < nbuf; i++) {
    buf[i].ec = buf[i].epoch >= newest - 43200 ? 300 : 200;
  }
  ChangeJudgement j = judgeLatestChange(buf, nbuf, LightSchedule(), 10);
  TEST_ASSERT_TRUE(j.ok);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 300, j.ecMedian);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.03, j.jitter);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(day_swing_rise_fall_and_times);
  RUN_TEST(single_spike_is_smoothed_away);
  RUN_TEST(day_under_coverage_is_skipped);
  RUN_TEST(jitter_is_median_successive_difference);
  RUN_TEST(change_judged_after_window_ends);
  RUN_TEST(change_not_judged_before_its_window_ends);
  RUN_TEST(window_under_coverage_is_skipped);
  RUN_TEST(change_window_may_cross_midnight);
  RUN_TEST(same_change_has_same_key);
  RUN_TEST(ec_median_covers_last_12_hours);
  return UNITY_END();
}

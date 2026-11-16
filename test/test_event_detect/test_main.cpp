#include <cmath>
#include <unity.h>
#include "event_detect.h"

void setUp() {}
void tearDown() {}

//12 samples 600 s apart: ec `before` for the first 6, `after` for the last 6, pH 6.5 then 6.6
void fillStep(DetectSample *s, float before, float after) {
  for (int i = 0; i < 12; i++) {
    s[i].epoch = 1800000000LL + i * 600;
    s[i].ec = i < 6 ? before : after;
    s[i].ph = i < 6 ? 6.5f : 6.6f;
  }
}

void test_step_down_is_topup_at_seventh_sample() {
  DetectSample s[12];
  fillStep(s, 250, 244);
  Candidate c = detectAt(s, DetectLimits());
  TEST_ASSERT_TRUE(c.found);
  TEST_ASSERT_TRUE(c.topup);
  TEST_ASSERT_EQUAL_INT64(s[6].epoch, c.occurredAt);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 250, c.ecBefore);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 244, c.ecAfter);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 6.5, c.phBefore);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 6.6, c.phAfter);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 6, c.size);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 6, c.meanSize);
}

void test_noise_below_threshold_not_found() {
  DetectSample s[12];
  fillStep(s, 250, 247.5f);
  s[2].ec = 240;  //one outlier moves the mean, not the median
  TEST_ASSERT_FALSE(detectAt(s, DetectLimits()).found);
}

void test_rise_ignored_when_feeding_disabled() {
  DetectSample s[12];
  fillStep(s, 244, 252);
  TEST_ASSERT_FALSE(detectAt(s, DetectLimits()).found);
}

void test_rise_is_feeding_when_enabled() {
  DetectSample s[12];
  fillStep(s, 244, 252);
  DetectLimits limits;
  limits.feedingEnabled = true;
  Candidate c = detectAt(s, limits);
  TEST_ASSERT_TRUE(c.found);
  TEST_ASSERT_FALSE(c.topup);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 8, c.size);
  limits.feedingMinRise = 9;
  TEST_ASSERT_FALSE(detectAt(s, limits).found);
}

void test_nan_ec_or_gap_not_found() {
  DetectSample s[12];
  fillStep(s, 250, 244);
  s[3].ec = NAN;
  TEST_ASSERT_FALSE(detectAt(s, DetectLimits()).found);
  fillStep(s, 250, 244);
  for (int i = 9; i < 12; i++) s[i].epoch += 901;  //a 1501 s gap between samples 8 and 9
  TEST_ASSERT_FALSE(detectAt(s, DetectLimits()).found);
  fillStep(s, 250, 244);
  for (int i = 9; i < 12; i++) s[i].epoch += 900;  //exactly 1500 s is allowed
  TEST_ASSERT_TRUE(detectAt(s, DetectLimits()).found);
}

void test_ph_median_ignores_nan() {
  DetectSample s[12];
  fillStep(s, 250, 244);
  s[0].ph = NAN;
  s[1].ph = 9;  //median of 9, 6.5 x4 = 6.5
  for (int i = 6; i < 12; i++) s[i].ph = NAN;
  Candidate c = detectAt(s, DetectLimits());
  TEST_ASSERT_TRUE(c.found);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 6.5, c.phBefore);
  TEST_ASSERT_TRUE(std::isnan(c.phAfter));
}

void test_even_count_median_averages_middle_pair() {
  float v[6] = {1, 5, 3, 4, 2, 6};
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 3.5, medianOf(v, 6));
}

void test_parses_log_timestamp() {
  LogTime t;
  TEST_ASSERT_TRUE(parseLogTimestamp("2026,11-1,05,21,16,08", t));
  TEST_ASSERT_EQUAL_INT(2026, t.year);
  TEST_ASSERT_EQUAL_INT(11, t.month);
  TEST_ASSERT_EQUAL_INT(5, t.day);
  TEST_ASSERT_EQUAL_INT(21, t.hour);
  TEST_ASSERT_EQUAL_INT(16, t.minute);
  TEST_ASSERT_EQUAL_INT(8, t.second);
  TEST_ASSERT_FALSE(parseLogTimestamp("", t));
  TEST_ASSERT_FALSE(parseLogTimestamp("2026,11-1,05", t));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_step_down_is_topup_at_seventh_sample);
  RUN_TEST(test_noise_below_threshold_not_found);
  RUN_TEST(test_rise_ignored_when_feeding_disabled);
  RUN_TEST(test_rise_is_feeding_when_enabled);
  RUN_TEST(test_nan_ec_or_gap_not_found);
  RUN_TEST(test_ph_median_ignores_nan);
  RUN_TEST(test_even_count_median_averages_middle_pair);
  RUN_TEST(test_parses_log_timestamp);
  return UNITY_END();
}

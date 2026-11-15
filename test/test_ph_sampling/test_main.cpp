#include <cmath>
#include <vector>
#include <unity.h>
#include "ph_sampling.h"

void setUp() {}
void tearDown() {}

void schedule_is_due_at_once_then_on_a_fixed_grid() {
  SampleSchedule schedule;
  schedule.reset();

  TEST_ASSERT_TRUE(schedule.due(1000));
  TEST_ASSERT_FALSE(schedule.due(1104));
  TEST_ASSERT_TRUE(schedule.due(1105));
  //A late read doesn't push the grid back
  TEST_ASSERT_FALSE(schedule.due(1209));
  TEST_ASSERT_TRUE(schedule.due(1212));
  TEST_ASSERT_FALSE(schedule.due(1314));
  TEST_ASSERT_TRUE(schedule.due(1315));
}

void schedule_skips_ahead_rather_than_reading_back_to_back() {
  SampleSchedule schedule;
  schedule.reset();
  TEST_ASSERT_TRUE(schedule.due(0));

  TEST_ASSERT_TRUE(schedule.due(500));  //starved for a while
  TEST_ASSERT_FALSE(schedule.due(501));
  TEST_ASSERT_FALSE(schedule.due(604));
  TEST_ASSERT_TRUE(schedule.due(605));
}

//The point of the 105 ms grid: 20 reads cancel steady 60 Hz and 50 Hz hum and its low harmonics
void window_on_the_grid_cancels_mains_hum() {
  const double pi = 3.14159265358979;
  double hz[] = { 50, 60, 120, 150, 180, 300 };
  for (double f : hz) {
    for (double phase = 0; phase < 6.28; phase += 0.7) {
      double sum = 0;
      for (int i = 0; i < HUM_WINDOW; i++) {
        double t = i * HUM_SPACING_MS / 1000.0;
        sum += 2000 + 100 * sin(2 * pi * f * t + phase);
      }
      TEST_ASSERT_FLOAT_WITHIN(0.01, 2000, (float)(sum / HUM_WINDOW));
    }
  }
}

//The old 100 ms spacing locked onto one phase of 60 Hz, so the hum became the reading's offset
void old_100ms_spacing_aliases_60hz_to_an_offset() {
  const double pi = 3.14159265358979;
  double sum = 0;
  for (int i = 0; i < 20; i++) {
    sum += 2000 + 100 * sin(2 * pi * 60 * i * 0.1 + 1.0);
  }
  TEST_ASSERT_FLOAT_WITHIN(0.5, 2000 + 100 * sin(1.0), (float)(sum / 20));
}

void trimmed_mean_drops_the_extremes() {
  float values[] = { 10, 10, 10, 10, 10, 10, 500, 10, 10, -400 };
  TEST_ASSERT_EQUAL_FLOAT(10, trimmedMean(values, 10, 1));
  float plain[] = { 1, 2, 3, 4 };
  TEST_ASSERT_EQUAL_FLOAT(2.5, trimmedMean(plain, 4, 0));
}

void trimmed_mean_leaves_the_input_unsorted() {
  float values[] = { 3, 1, 2 };
  trimmedMean(values, 3, 1);
  TEST_ASSERT_EQUAL_FLOAT(3, values[0]);
  TEST_ASSERT_EQUAL_FLOAT(1, values[1]);
}

void trimmed_mean_with_too_much_trim_uses_all_values() {
  float values[] = { 1, 3 };
  TEST_ASSERT_EQUAL_FLOAT(2, trimmedMean(values, 2, 1));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(schedule_is_due_at_once_then_on_a_fixed_grid);
  RUN_TEST(schedule_skips_ahead_rather_than_reading_back_to_back);
  RUN_TEST(window_on_the_grid_cancels_mains_hum);
  RUN_TEST(old_100ms_spacing_aliases_60hz_to_an_offset);
  RUN_TEST(trimmed_mean_drops_the_extremes);
  RUN_TEST(trimmed_mean_leaves_the_input_unsorted);
  RUN_TEST(trimmed_mean_with_too_much_trim_uses_all_values);
  return UNITY_END();
}

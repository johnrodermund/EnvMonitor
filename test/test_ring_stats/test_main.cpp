#include <unity.h>
#include "ring_stats.h"

void setUp() {}
void tearDown() {}

void first_reading_is_its_own_mean_with_zero_variance() {
  RingStats<5> stats;
  stats.updatedata(7.5);

  TEST_ASSERT_EQUAL_INT(1, stats.size());
  TEST_ASSERT_EQUAL_FLOAT(7.5, stats.currentValue);
  TEST_ASSERT_EQUAL_FLOAT(7.5, stats.mean);
  TEST_ASSERT_EQUAL_FLOAT(0, stats.sVar);
}

void partial_buffer_reports_sample_mean_and_variance() {
  RingStats<10> stats;
  float readings[] = {2, 4, 4, 4, 5, 5, 7, 9};
  for (float reading : readings) {
    stats.updatedata(reading);
  }

  TEST_ASSERT_EQUAL_INT(8, stats.size());
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 5.0, stats.mean);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 4.571429, stats.sVar);
}

void full_buffer_forgets_oldest_reading() {
  RingStats<3> stats;
  float readings[] = {1, 2, 3, 4};
  for (float reading : readings) {
    stats.updatedata(reading);
  }

  TEST_ASSERT_EQUAL_INT(3, stats.size());
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 3.0, stats.mean);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 1.0, stats.sVar);
}

void wrapped_buffer_reads_oldest_first() {
  RingStats<3> stats;
  float readings[] = {1, 2, 3, 4, 5};
  for (float reading : readings) {
    stats.updatedata(reading);
  }

  TEST_ASSERT_EQUAL_FLOAT(3, stats.getOrderedValue(0));
  TEST_ASSERT_EQUAL_FLOAT(4, stats.getOrderedValue(1));
  TEST_ASSERT_EQUAL_FLOAT(5, stats.getOrderedValue(2));
}

void reset_starts_a_fresh_series() {
  RingStats<3> stats;
  float readings[] = {1, 2, 3, 4};
  for (float reading : readings) {
    stats.updatedata(reading);
  }

  stats.resetDataset();
  stats.updatedata(10);

  TEST_ASSERT_EQUAL_INT(1, stats.size());
  TEST_ASSERT_EQUAL_FLOAT(10, stats.mean);
  TEST_ASSERT_EQUAL_FLOAT(0, stats.sVar);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(first_reading_is_its_own_mean_with_zero_variance);
  RUN_TEST(partial_buffer_reports_sample_mean_and_variance);
  RUN_TEST(full_buffer_forgets_oldest_reading);
  RUN_TEST(wrapped_buffer_reads_oldest_first);
  RUN_TEST(reset_starts_a_fresh_series);
  return UNITY_END();
}

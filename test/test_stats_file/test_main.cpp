#include <unity.h>
#include "stats_file.h"

void setUp() {}
void tearDown() {}

void saved_partial_series_restores_unchanged() {
  RingStats<5> saved;
  saved.updatedata(4);
  saved.updatedata(6);

  RingStats<5> restored;
  TEST_ASSERT_TRUE(restoreStats(serializeStats("phdata", saved).c_str(), restored));

  TEST_ASSERT_EQUAL_INT(2, restored.size());
  TEST_ASSERT_EQUAL_FLOAT(5, restored.mean);
  TEST_ASSERT_EQUAL_FLOAT(2, restored.sVar);
  TEST_ASSERT_EQUAL_FLOAT(6, restored.currentValue);
  TEST_ASSERT_EQUAL_FLOAT(4, restored.getOrderedValue(0));
  TEST_ASSERT_EQUAL_FLOAT(6, restored.getOrderedValue(1));
}

void variance_stays_accurate_across_a_restart() {
  RingStats<5> saved;
  saved.updatedata(7.01);
  saved.updatedata(7.03);
  saved.updatedata(7.02);

  RingStats<5> restored;
  TEST_ASSERT_TRUE(restoreStats(serializeStats("phdata", saved).c_str(), restored));
  restored.updatedata(7.04);

  TEST_ASSERT_FLOAT_WITHIN(0.00002, 0.00016667, restored.sVar);
}

void empty_file_is_rejected_and_leaves_series_alone() {
  RingStats<5> stats;
  stats.updatedata(4);

  TEST_ASSERT_FALSE(restoreStats("", stats));

  TEST_ASSERT_EQUAL_INT(1, stats.size());
  TEST_ASSERT_EQUAL_FLOAT(4, stats.mean);
}

void file_cut_short_by_power_loss_is_rejected() {
  RingStats<5> stats;

  TEST_ASSERT_FALSE(restoreStats("phdata 3 5 2 2 6 0 4,6", stats));

  TEST_ASSERT_EQUAL_INT(0, stats.size());
}

void file_written_by_old_firmware_still_restores() {
  RingStats<5> stats;

  TEST_ASSERT_TRUE(restoreStats("phdata 2 5.00 2.00 2.00 6.00 0 4.00,6.00", stats));

  TEST_ASSERT_EQUAL_INT(2, stats.size());
  TEST_ASSERT_EQUAL_FLOAT(5, stats.mean);
  TEST_ASSERT_EQUAL_FLOAT(6, stats.getOrderedValue(1));
}

void wrapped_series_restores_and_keeps_rolling() {
  RingStats<3> saved;
  float readings[] = {1, 2, 3, 4};
  for (float reading : readings) {
    saved.updatedata(reading);
  }

  RingStats<3> restored;
  TEST_ASSERT_TRUE(restoreStats(serializeStats("wtdata", saved).c_str(), restored));
  restored.updatedata(5);

  TEST_ASSERT_EQUAL_INT(3, restored.size());
  TEST_ASSERT_EQUAL_FLOAT(3, restored.getOrderedValue(0));
  TEST_ASSERT_EQUAL_FLOAT(5, restored.getOrderedValue(2));
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 4.0, restored.mean);
}

void file_with_more_values_than_capacity_is_rejected() {
  RingStats<3> stats;

  TEST_ASSERT_FALSE(restoreStats("phdata 1 2 0 0 2 1 1,2,3,4,5,6", stats));
}

void full_series_with_out_of_range_index_is_rejected() {
  RingStats<3> stats;

  TEST_ASSERT_FALSE(restoreStats("phdata 5 2 2 1 3 1 1,2,3", stats));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(saved_partial_series_restores_unchanged);
  RUN_TEST(variance_stays_accurate_across_a_restart);
  RUN_TEST(empty_file_is_rejected_and_leaves_series_alone);
  RUN_TEST(file_cut_short_by_power_loss_is_rejected);
  RUN_TEST(file_written_by_old_firmware_still_restores);
  RUN_TEST(wrapped_series_restores_and_keeps_rolling);
  RUN_TEST(file_with_more_values_than_capacity_is_rejected);
  RUN_TEST(full_series_with_out_of_range_index_is_rejected);
  return UNITY_END();
}

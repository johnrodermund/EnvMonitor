#include <unity.h>
#include "sensor_math.h"

void setUp() {}
void tearDown() {}

void calibration_readings_map_to_their_buffer_ph() {
  TEST_ASSERT_FLOAT_WITHIN(0.001, 4.0, phFromAdc(2368, 2368, 1880));
  TEST_ASSERT_FLOAT_WITHIN(0.001, 7.0, phFromAdc(1880, 2368, 1880));
}

void reading_between_calibration_points_interpolates() {
  TEST_ASSERT_FLOAT_WITHIN(0.001, 5.5, phFromAdc(2124, 2368, 1880));
}

void ec_follows_probe_curve_across_the_adc_range() {
  TEST_ASSERT_FLOAT_WITHIN(0.01, 0.0, ecFromAdc(0));
  TEST_ASSERT_FLOAT_WITHIN(0.05, 367.475, ecFromAdc(1240.9091));  // 1.0 V
  TEST_ASSERT_FLOAT_WITHIN(0.05, 2418.89, ecFromAdc(4095));       // 3.3 V
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(calibration_readings_map_to_their_buffer_ph);
  RUN_TEST(reading_between_calibration_points_interpolates);
  RUN_TEST(ec_follows_probe_curve_across_the_adc_range);
  return UNITY_END();
}

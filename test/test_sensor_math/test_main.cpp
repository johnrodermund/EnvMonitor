#include <cmath>
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

//EC(T) = EC25 * (1 + a * (T - 25)), with T converted from the DS18B20's deg F
void ec_compensates_to_25_c() {
  TEST_ASSERT_FLOAT_WITHIN(0.001, 250, ecAt25(250, 77, 2.0f));                 //25 C: unchanged
  TEST_ASSERT_FLOAT_WITHIN(0.01, 250 / (1 - 0.06f), ecAt25(250, 71.6f, 2.0f));  //22 C, 2 %/C
  TEST_ASSERT_FLOAT_WITHIN(0.01, 250 / (1 + 0.0835f), ecAt25(250, 86, 1.67f));  //30 C, 1.67 %/C
}

void ec_compensation_off_or_bad_temperature_leaves_ec_raw() {
  TEST_ASSERT_FLOAT_WITHIN(0.001, 250, ecAt25(250, 71.6f, 0));
  TEST_ASSERT_FLOAT_WITHIN(0.001, 250, ecAt25(250, NAN, 2));
  TEST_ASSERT_FLOAT_WITHIN(0.001, 250, ecAt25(250, -196.6f, 2));  //DS18B20 "disconnected" (-127 C)
  TEST_ASSERT_FLOAT_WITHIN(0.001, 250, ecAt25(250, 185, 2));      //DS18B20 power-on value (85 C)
  TEST_ASSERT_TRUE(std::isnan(ecAt25(NAN, 71.6f, 2)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(ec_compensates_to_25_c);
  RUN_TEST(ec_compensation_off_or_bad_temperature_leaves_ec_raw);
  RUN_TEST(calibration_readings_map_to_their_buffer_ph);
  RUN_TEST(reading_between_calibration_points_interpolates);
  RUN_TEST(ec_follows_probe_curve_across_the_adc_range);
  return UNITY_END();
}

#include <cmath>
#include <string>
#include <unity.h>
#include "reading_queue.h"

void setUp() {}
void tearDown() {}

QueuedReading readingAt(long long epoch, float ph = 7) {
  QueuedReading reading;
  reading.epoch = epoch;
  reading.ph = ph;
  return reading;
}

void queue_hands_back_readings_oldest_first() {
  ReadingQueue<3> queue;
  queue.push(readingAt(1000));
  queue.push(readingAt(2000));

  TEST_ASSERT_EQUAL_INT(2, queue.size());
  TEST_ASSERT_EQUAL_INT64(1000, queue.front().epoch);
  queue.pop();
  TEST_ASSERT_EQUAL_INT64(2000, queue.front().epoch);
}

void full_queue_drops_the_oldest_reading() {
  ReadingQueue<2> queue;
  queue.push(readingAt(1000));
  queue.push(readingAt(2000));
  queue.push(readingAt(3000));

  TEST_ASSERT_EQUAL_INT(2, queue.size());
  TEST_ASSERT_EQUAL_INT(1, queue.dropped());
  TEST_ASSERT_EQUAL_INT64(2000, queue.front().epoch);
}

void reading_becomes_the_api_json_body() {
  QueuedReading reading;
  reading.epoch = 1762300800;  // 2025-11-05T00:00:00Z
  reading.ph = 6.5;
  reading.phUncertainty = 0.25;
  reading.phAdc = 2124;
  reading.ec = 261.5;
  reading.ecAdc = 836;
  reading.wt = 74.5;

  TEST_ASSERT_EQUAL_STRING(
      "{\"device\":\"envmonitor-1\",\"measured_at\":\"2025-11-05T00:00:00Z\",\"ph\":6.500,\"ph_uncertainty\":0.250,"
      "\"ph_adc\":2124.000,\"ec\":261.500,\"ec_adc\":836.000,\"water_temp_f\":74.500}",
      readingJson("envmonitor-1", reading).c_str());
}

void missing_sensor_values_are_sent_as_null() {
  QueuedReading reading = readingAt(1762300800);
  reading.wt = NAN;

  std::string body = readingJson("box", reading);

  TEST_ASSERT_TRUE(body.find("\"water_temp_f\":null") != std::string::npos);
  TEST_ASSERT_TRUE(body.find("\"ec\":null") != std::string::npos);
}

void device_name_is_escaped_in_json() {
  TEST_ASSERT_TRUE(readingJson("a\"b\\c", readingAt(1762300800)).find("\"device\":\"a\\\"b\\\\c\"") != std::string::npos);
}

void timestamps_convert_to_utc_across_leap_years() {
  TEST_ASSERT_EQUAL_STRING("2024-02-29T23:59:59Z", utcIso(1709251199).c_str());
  TEST_ASSERT_EQUAL_STRING("2026-11-04T23:30:00Z", utcIso(1793835000).c_str());
}

void clock_counts_as_set_only_after_ntp_sync() {
  TEST_ASSERT_FALSE(clockIsSet(5000));  // seconds since boot before NTP
  TEST_ASSERT_TRUE(clockIsSet(1762300800));
}

void http_results_decide_what_happens_to_a_reading() {
  TEST_ASSERT_TRUE(outcomeFor(201) == SendOutcome::Delivered);
  TEST_ASSERT_TRUE(outcomeFor(200) == SendOutcome::Delivered);  // duplicate already stored
  TEST_ASSERT_TRUE(outcomeFor(422) == SendOutcome::Rejected);   // would never be accepted
  TEST_ASSERT_TRUE(outcomeFor(401) == SendOutcome::RetryLater); // fix the token and it still gets through
  TEST_ASSERT_TRUE(outcomeFor(500) == SendOutcome::RetryLater);
  TEST_ASSERT_TRUE(outcomeFor(-1) == SendOutcome::RetryLater);  // connection failed
}

void retries_back_off_up_to_five_minutes() {
  TEST_ASSERT_EQUAL_UINT32(10000, retryDelayMs(1));
  TEST_ASSERT_EQUAL_UINT32(20000, retryDelayMs(2));
  TEST_ASSERT_EQUAL_UINT32(160000, retryDelayMs(5));
  TEST_ASSERT_EQUAL_UINT32(300000, retryDelayMs(6));
  TEST_ASSERT_EQUAL_UINT32(300000, retryDelayMs(40));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(queue_hands_back_readings_oldest_first);
  RUN_TEST(full_queue_drops_the_oldest_reading);
  RUN_TEST(reading_becomes_the_api_json_body);
  RUN_TEST(missing_sensor_values_are_sent_as_null);
  RUN_TEST(device_name_is_escaped_in_json);
  RUN_TEST(timestamps_convert_to_utc_across_leap_years);
  RUN_TEST(clock_counts_as_set_only_after_ntp_sync);
  RUN_TEST(http_results_decide_what_happens_to_a_reading);
  RUN_TEST(retries_back_off_up_to_five_minutes);
  return UNITY_END();
}

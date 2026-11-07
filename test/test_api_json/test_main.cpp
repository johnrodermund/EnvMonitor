#include <cmath>
#include <string>
#include <unity.h>
#include "api_json.h"
#include "reading_log.h"

void setUp() {}
void tearDown() {}

void iso_from_log_timestamp(void) {
  TEST_ASSERT_EQUAL_STRING("2026-11-04T16:33:06", isoFromLogTimestamp("2026,11-1,04,16,33,06").c_str());
  TEST_ASSERT_EQUAL_STRING("", isoFromLogTimestamp("").c_str());
  TEST_ASSERT_EQUAL_STRING("", isoFromLogTimestamp("garbage").c_str());
}

void readings_json_empty_log_is_empty_array(void) {
  ReadingLog<200> log;
  TEST_ASSERT_EQUAL_STRING("[]", readingsJson(log).c_str());
}

void readings_json_writes_values_oldest_first(void) {
  ReadingLog<200> log;
  log.append("2026,11-1,04,16,33,06", 6.2f, 244.73f, 75.2f);
  log.append("2026,11-1,04,16,43,06", 6.3f, 245.0f, 75.0f);
  TEST_ASSERT_EQUAL_STRING("[[\"2026-11-04T16:33:06\",6.200,244.730,75.200],[\"2026-11-04T16:43:06\",6.300,245.000,75.000]]",
                           readingsJson(log).c_str());
}

void readings_json_writes_null_for_nan_and_empty_time(void) {
  ReadingLog<200> log;
  log.append("", NAN, 1, 2);
  TEST_ASSERT_EQUAL_STRING("[[null,null,1.000,2.000]]", readingsJson(log).c_str());
}

void status_json_last_reading_null_when_empty(void) {
  StatusView s;
  s.count = 0;
  TEST_ASSERT_NOT_EQUAL(std::string::npos, statusJson(s).find("\"last_reading\":null"));
}

void status_json_escapes_title(void) {
  StatusView s;
  s.title = "a\"b";
  TEST_ASSERT_NOT_EQUAL(std::string::npos, statusJson(s).find("\"title\":\"a\\\"b\""));
}

void status_json_lists_only_taken_ph_samples(void) {
  StatusView s;
  s.phSampleCount = 2;
  s.phSamples[0] = 1;
  s.phSamples[1] = 2;
  s.phSamples[2] = 99;
  std::string json = statusJson(s);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, json.find("\"ph_samples\":[1.000,2.000]"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, json.find("\"warmup_ms\":20000,\"samples\":0,\"sample_total\":20"));
}

void settings_json_shape(void) {
  TEST_ASSERT_EQUAL_STRING("{\"interval_min\":10,\"title\":\"T\"}", settingsJson(10, "T").c_str());
}

void validate_settings_rules(void) {
  std::string error;
  TEST_ASSERT_FALSE(validateSettings("10.5", "", error));
  TEST_ASSERT_FALSE(validateSettings("1", "", error));
  TEST_ASSERT_FALSE(validateSettings("721", "", error));
  TEST_ASSERT_FALSE(validateSettings("abc", "", error));
  TEST_ASSERT_EQUAL_STRING("interval must be a whole number of minutes from 2 to 720", error.c_str());
  TEST_ASSERT_FALSE(validateSettings("", "a\nb", error));
  TEST_ASSERT_FALSE(validateSettings("", std::string(41, 'x'), error));
  TEST_ASSERT_EQUAL_STRING("title must be 1-40 characters with no control characters", error.c_str());
  TEST_ASSERT_FALSE(validateSettings("", "", error));
  TEST_ASSERT_EQUAL_STRING("nothing to save", error.c_str());
  TEST_ASSERT_TRUE(validateSettings("2", "", error));
  TEST_ASSERT_TRUE(validateSettings("720", "", error));
  TEST_ASSERT_TRUE(validateSettings("", "Tank", error));
  TEST_ASSERT_TRUE(validateSettings("10", std::string(40, 'x'), error));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(iso_from_log_timestamp);
  RUN_TEST(readings_json_empty_log_is_empty_array);
  RUN_TEST(readings_json_writes_values_oldest_first);
  RUN_TEST(readings_json_writes_null_for_nan_and_empty_time);
  RUN_TEST(status_json_last_reading_null_when_empty);
  RUN_TEST(status_json_escapes_title);
  RUN_TEST(status_json_lists_only_taken_ph_samples);
  RUN_TEST(settings_json_shape);
  RUN_TEST(validate_settings_rules);
  return UNITY_END();
}

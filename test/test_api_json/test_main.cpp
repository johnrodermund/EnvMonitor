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
  s.warmupMs = 30000;
  s.lastWarmupMs = 30045;
  std::string json = statusJson(s);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, json.find("\"ph_samples\":[1.000,2.000]"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, json.find("\"warmup_ms\":30000,\"samples\":0,\"sample_total\":20"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, json.find("\"last_warmup_ms\":30045}"));
}

void settings_json_shape(void) {
  DetectLimits limits;
  limits.feedingEnabled = true;
  TEST_ASSERT_EQUAL_STRING(
      "{\"interval_min\":10,\"title\":\"T\",\"topup_min_drop\":3.000,\"feeding_min_rise\":5.000,"
      "\"feeding_enabled\":true,\"ec_temp_coef\":1.670}",
      settingsJson(10, "T", limits, 1.67f).c_str());
}

//The log keeps raw EC; every reply applies the current coefficient, so a change re-applies to all history
void readings_json_compensates_ec(void) {
  ReadingLog<200> log;
  log.append("2026,11-1,04,16,33,06", 6.2f, 235.0f, 71.6f);  //22 C
  TEST_ASSERT_EQUAL_STRING("[[\"2026-11-04T16:33:06\",6.200,250.000,71.600]]", readingsJson(log, 2.0f).c_str());
}

void ec_stats_use_compensated_values(void) {
  ReadingLog<200> log;
  log.append("", 6, 235.0f, 71.6f);  //250 at 25 C
  log.append("", 6, 245.0f, 77.0f);  //245 at 25 C
  log.append("", 6, 248.0f, 77.0f);  //248
  float mean, std;
  compensatedEcStats(log, 2.0f, mean, std);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 247.667, mean);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 2.517, std);
  ReadingLog<200> empty;
  compensatedEcStats(empty, 2.0f, mean, std);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0, mean);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0, std);
}

void ec_coef_parse_rules(void) {
  float coef = 9;
  std::string error;
  TEST_ASSERT_TRUE(parseEcTempCoef("1.67", coef, error));
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 1.67, coef);
  TEST_ASSERT_TRUE(parseEcTempCoef("0", coef, error));  //0 turns compensation off
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 0, coef);
  TEST_ASSERT_FALSE(parseEcTempCoef("-1", coef, error));
  TEST_ASSERT_EQUAL_STRING("ec_temp_coef must be a number from 0 to 5 (% per deg C)", error.c_str());
  TEST_ASSERT_FALSE(parseEcTempCoef("5.5", coef, error));
  TEST_ASSERT_FALSE(parseEcTempCoef("abc", coef, error));
  TEST_ASSERT_FALSE(parseEcTempCoef("", coef, error));
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 0, coef);  //unchanged on an error
}

void status_json_reports_ec_temp_coef(void) {
  StatusView s;
  s.ecTempCoef = 1.67f;
  TEST_ASSERT_NOT_EQUAL(std::string::npos, statusJson(s).find("\"ec_temp_coef\":1.670"));
}

void status_json_reports_events(void) {
  StatusView s;
  s.eventsCount = 12;
  s.eventsUnsynced = 3;
  s.eventsDropped = 1;
  s.eventsClockAhead = 2;
  s.eventsRefused = 4;
  s.eventsLastHttp = 201;
  TEST_ASSERT_NOT_EQUAL(std::string::npos,
                        statusJson(s).find("\"events\":{\"count\":12,\"unsynced\":3,\"dropped_unsynced\":1,"
                                           "\"clock_ahead\":2,\"refused\":4,\"last_sync_http\":201}}"));
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
  RUN_TEST(readings_json_compensates_ec);
  RUN_TEST(ec_stats_use_compensated_values);
  RUN_TEST(ec_coef_parse_rules);
  RUN_TEST(status_json_reports_ec_temp_coef);
  RUN_TEST(status_json_reports_events);
  RUN_TEST(validate_settings_rules);
  return UNITY_END();
}

#include <cmath>
#include <string>
#include <unity.h>
#include "ranges.h"

void setUp() {}
void tearDown() {}

static const char *PI_EXAMPLE = R"JSON({
  "ph":   {"valid": [2, 10],   "target": [5.5, 6.5],  "drift_per_hour": 0.05, "noisy_std": 0.3},
  "ec":   {"valid": [0, 5000], "target": [200, 1000], "drift_per_hour": 10,   "noisy_std": 50},
  "temp": {"valid": [30, 99],  "target": [60, 80],    "drift_per_hour": 1.0,  "noisy_std": 2.0},
  "stuck_min_count": 6,
  "silent_intervals": 3,
  "default_interval_s": 600
}
)JSON";

static bool sameMetric(const MetricRange &a, const MetricRange &b) {
  return a.validLo == b.validLo && a.validHi == b.validHi && a.targetLo == b.targetLo && a.targetHi == b.targetHi;
}

static bool sameRanges(const Ranges &a, const Ranges &b) {
  return sameMetric(a.ph, b.ph) && sameMetric(a.ec, b.ec) && sameMetric(a.temp, b.temp);
}

void parses_pi_thresholds_with_extra_keys(void) {
  Ranges r = {};
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(parseThresholds(PI_EXAMPLE, r, error), error.c_str());
  TEST_ASSERT_EQUAL_FLOAT(5.5f, r.ph.targetLo);
  TEST_ASSERT_EQUAL_FLOAT(6.5f, r.ph.targetHi);
  TEST_ASSERT_EQUAL_FLOAT(2.0f, r.ph.validLo);
  TEST_ASSERT_EQUAL_FLOAT(5000.0f, r.ec.validHi);
  TEST_ASSERT_EQUAL_FLOAT(200.0f, r.ec.targetLo);
  TEST_ASSERT_EQUAL_FLOAT(80.0f, r.temp.targetHi);
  TEST_ASSERT_EQUAL_FLOAT(30.0f, r.temp.validLo);
}

void rejects_missing_metric(void) {
  Ranges r = {};
  std::string error;
  std::string json = R"({"ph":{"valid":[2,10],"target":[5.5,6.5]},"ec":{"valid":[0,5000],"target":[200,1000]}})";
  TEST_ASSERT_FALSE(parseThresholds(json, r, error));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, error.find("temp"));
}

void does_not_match_metric_inside_longer_key(void) {
  Ranges r = {};
  std::string error;
  std::string json = R"({"ph":{"valid":[2,10],"target":[5.5,6.5]},"ec":{"valid":[0,5000],"target":[200,1000]},"water_temp":{"valid":[30,99],"target":[60,80]}})";
  TEST_ASSERT_FALSE(parseThresholds(json, r, error));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, error.find("temp"));
}

void rejects_missing_array_and_malformed_numbers(void) {
  Ranges r = {};
  std::string error;
  std::string noTarget = R"({"ph":{"valid":[2,10]},"ec":{"valid":[0,5000],"target":[200,1000]},"temp":{"valid":[30,99],"target":[60,80]}})";
  TEST_ASSERT_FALSE(parseThresholds(noTarget, r, error));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, error.find("ph"));
  std::string badNumber = R"({"ph":{"valid":[2,"x"],"target":[5.5,6.5]},"ec":{"valid":[0,5000],"target":[200,1000]},"temp":{"valid":[30,99],"target":[60,80]}})";
  TEST_ASSERT_FALSE(parseThresholds(badNumber, r, error));
  TEST_ASSERT_FALSE(parseThresholds("", r, error));
}

void accepts_negatives_and_decimals(void) {
  Ranges r = {};
  std::string error;
  std::string json = R"({"ph":{"valid":[ 2 , 10 ],"target":[5.5,6.5]},"ec":{"valid":[0,5000],"target":[200,1000]},"temp":{"valid" : [-10.5, 99],"target" : [ -5, 8e1 ]}})";
  TEST_ASSERT_TRUE_MESSAGE(parseThresholds(json, r, error), error.c_str());
  TEST_ASSERT_EQUAL_FLOAT(-10.5f, r.temp.validLo);
  TEST_ASSERT_EQUAL_FLOAT(-5.0f, r.temp.targetLo);
  TEST_ASSERT_EQUAL_FLOAT(80.0f, r.temp.targetHi);
}

void rejects_target_outside_valid(void) {
  Ranges r = builtinRanges();
  r.ph.targetLo = 1;
  r.ph.targetHi = 6;
  std::string error;
  TEST_ASSERT_FALSE(validateRanges(r, error));
  TEST_ASSERT_EQUAL_STRING("ph: target must sit inside valid", error.c_str());
}

void parse_applies_validation(void) {
  Ranges r = {};
  std::string error;
  std::string json = R"({"ph":{"valid":[2,10],"target":[1,6]},"ec":{"valid":[0,5000],"target":[200,1000]},"temp":{"valid":[30,99],"target":[60,80]}})";
  TEST_ASSERT_FALSE(parseThresholds(json, r, error));
  TEST_ASSERT_EQUAL_STRING("ph: target must sit inside valid", error.c_str());
}

void rejects_reversed_range(void) {
  Ranges r = builtinRanges();
  r.ec.validLo = 6000;
  std::string error;
  TEST_ASSERT_FALSE(validateRanges(r, error));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, error.find("ec"));
  r = builtinRanges();
  r.temp.targetLo = 90;
  r.temp.targetHi = 70;
  TEST_ASSERT_FALSE(validateRanges(r, error));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, error.find("temp"));
}

void rejects_non_finite(void) {
  Ranges r = builtinRanges();
  r.ph.validHi = NAN;
  std::string error;
  TEST_ASSERT_FALSE(validateRanges(r, error));
  r = builtinRanges();
  r.ph.validHi = INFINITY;
  TEST_ASSERT_FALSE(validateRanges(r, error));
}

void builtin_is_valid(void) {
  std::string error;
  TEST_ASSERT_TRUE(validateRanges(builtinRanges(), error));
}

void choose_prefers_device_then_pi_then_builtin(void) {
  Ranges device = builtinRanges();
  device.ph.targetLo = 5.8f;
  Ranges pi = builtinRanges();
  pi.ph.targetLo = 5.9f;
  Ranges out = {};

  TEST_ASSERT_EQUAL((int)RangeSource::Device, (int)chooseRanges(true, device, true, pi, out));
  TEST_ASSERT_TRUE(sameRanges(device, out));
  TEST_ASSERT_EQUAL((int)RangeSource::Device, (int)chooseRanges(true, device, false, pi, out));
  TEST_ASSERT_TRUE(sameRanges(device, out));
  TEST_ASSERT_EQUAL((int)RangeSource::Pi, (int)chooseRanges(false, device, true, pi, out));
  TEST_ASSERT_TRUE(sameRanges(pi, out));
  TEST_ASSERT_EQUAL((int)RangeSource::Builtin, (int)chooseRanges(false, device, false, pi, out));
  TEST_ASSERT_TRUE(sameRanges(builtinRanges(), out));
}

void source_names(void) {
  TEST_ASSERT_EQUAL_STRING("device", rangeSourceName(RangeSource::Device));
  TEST_ASSERT_EQUAL_STRING("pi", rangeSourceName(RangeSource::Pi));
  TEST_ASSERT_EQUAL_STRING("builtin", rangeSourceName(RangeSource::Builtin));
}

void ranges_json_shape(void) {
  TEST_ASSERT_EQUAL_STRING(
    "{\"ph\":{\"valid\":[2.000,10.000],\"target\":[5.500,6.500]},"
    "\"ec\":{\"valid\":[0.000,5000.000],\"target\":[200.000,1000.000]},"
    "\"temp\":{\"valid\":[30.000,99.000],\"target\":[60.000,80.000]}}",
    rangesJson(builtinRanges()).c_str());
}

void ranges_json_round_trips(void) {
  Ranges parsed = {};
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(parseThresholds(rangesJson(builtinRanges()), parsed, error), error.c_str());
  TEST_ASSERT_TRUE(sameRanges(builtinRanges(), parsed));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(parses_pi_thresholds_with_extra_keys);
  RUN_TEST(rejects_missing_metric);
  RUN_TEST(does_not_match_metric_inside_longer_key);
  RUN_TEST(rejects_missing_array_and_malformed_numbers);
  RUN_TEST(accepts_negatives_and_decimals);
  RUN_TEST(rejects_target_outside_valid);
  RUN_TEST(parse_applies_validation);
  RUN_TEST(rejects_reversed_range);
  RUN_TEST(rejects_non_finite);
  RUN_TEST(builtin_is_valid);
  RUN_TEST(choose_prefers_device_then_pi_then_builtin);
  RUN_TEST(source_names);
  RUN_TEST(ranges_json_shape);
  RUN_TEST(ranges_json_round_trips);
  return UNITY_END();
}

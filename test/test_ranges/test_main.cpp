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

static bool near3(float a, float b) {
  return std::fabs(a - b) <= 0.0005f;
}

void round_trip_keeps_fractional_values(void) {
  Ranges r = builtinRanges();
  r.ph = {2.5f, 9.5f, 5.25f, 6.75f};
  r.ec.targetLo = 150.125f;
  r.ec.targetHi = 999.875f;
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(validateRanges(r, error), error.c_str());
  Ranges back = {};
  TEST_ASSERT_TRUE_MESSAGE(parseThresholds(rangesJson(r), back, error), error.c_str());
  const float *a = &r.ph.validLo, *b = &back.ph.validLo;
  for (int i = 0; i < 12; i++) TEST_ASSERT_TRUE(near3(a[i], b[i]));
}

void rejects_values_that_collapse_when_saved(void) {
  Ranges r = builtinRanges();
  r.ec.targetLo = 0.0001f;
  r.ec.targetHi = 0.0004f;
  std::string error;
  TEST_ASSERT_FALSE(validateRanges(r, error));
}

void rejects_huge_values_with_magnitude_error(void) {
  Ranges r = builtinRanges();
  r.ec.validHi = 1e30f;
  std::string error;
  TEST_ASSERT_FALSE(validateRanges(r, error));
  TEST_ASSERT_EQUAL_STRING("ec: values must be between -100000 and 100000", error.c_str());
  r = builtinRanges();
  r.temp.validLo = -100001.0f;
  TEST_ASSERT_FALSE(validateRanges(r, error));
  TEST_ASSERT_EQUAL_STRING("temp: values must be between -100000 and 100000", error.c_str());
}

void parse_leaves_out_untouched_on_failure(void) {
  Ranges out = builtinRanges();
  out.ph.targetLo = 5.75f;
  Ranges before = out;
  std::string error;
  std::string json = R"({"ph":{"valid":[2,10],"target":[1,6]},"ec":{"valid":[0,5000],"target":[200,1000]},"temp":{"valid":[30,99],"target":[60,80]}})";
  TEST_ASSERT_FALSE(parseThresholds(json, out, error));
  TEST_ASSERT_TRUE(sameRanges(before, out));
  TEST_ASSERT_FALSE(parseThresholds("{}", out, error));
  TEST_ASSERT_TRUE(sameRanges(before, out));
}

void rejects_hex_and_other_non_json_numbers(void) {
  const char *bad[] = {"0x10", "0x1p3", "+5", ".5", "nan", "inf", "5.", "1e"};
  for (const char *n : bad) {
    std::string json = std::string("{\"ph\":{\"valid\":[2,") + n + "],\"target\":[5.5,6.5]},\"ec\":{\"valid\":[0,5000],\"target\":[200,1000]},\"temp\":{\"valid\":[30,99],\"target\":[60,80]}}";
    Ranges r = {};
    std::string error;
    TEST_ASSERT_FALSE_MESSAGE(parseThresholds(json, r, error), n);
  }
}

static void builtinFields(std::string fields[RANGE_FIELD_COUNT]) {
  const char *values[RANGE_FIELD_COUNT] = {"2", "10", "5.5", "6.5", "0", "5000", "200", "1000", "30", "99", "60", "80"};
  for (int i = 0; i < RANGE_FIELD_COUNT; i++) fields[i] = values[i];
}

void range_field_names_follow_the_form_contract(void) {
  const char *expected[RANGE_FIELD_COUNT] = {
      "ph_valid_lo",   "ph_valid_hi",   "ph_target_lo",   "ph_target_hi",
      "ec_valid_lo",   "ec_valid_hi",   "ec_target_lo",   "ec_target_hi",
      "temp_valid_lo", "temp_valid_hi", "temp_target_lo", "temp_target_hi"};
  for (int i = 0; i < RANGE_FIELD_COUNT; i++) {
    TEST_ASSERT_EQUAL_STRING(expected[i], rangeFieldName(i));
  }
}

void form_fields_parse_into_ranges(void) {
  std::string fields[RANGE_FIELD_COUNT];
  builtinFields(fields);
  fields[9] = "1e2";   //temp valid hi, exponent form as JS String() can produce
  fields[4] = "-0.5";  //ec valid lo
  Ranges r = {};
  std::string error;
  TEST_ASSERT_TRUE_MESSAGE(rangesFromForm(fields, r, error), error.c_str());
  Ranges expected = builtinRanges();
  expected.temp.validHi = 100.0f;
  expected.ec.validLo = -0.5f;
  TEST_ASSERT_TRUE(sameRanges(expected, r));
}

void form_rejects_missing_or_garbage_fields_by_metric(void) {
  const char *bad[] = {"", "abc", "7x", "0x10", "nan", "Infinity", "1.", "+3", "1 2"};
  for (const char *value : bad) {
    for (int field : {0, 6, 11}) {
      std::string fields[RANGE_FIELD_COUNT];
      builtinFields(fields);
      fields[field] = value;
      Ranges r = builtinRanges();
      std::string error;
      TEST_ASSERT_FALSE_MESSAGE(rangesFromForm(fields, r, error), value);
      const char *metric = field < 4 ? "ph" : (field < 8 ? "ec" : "temp");
      TEST_ASSERT_EQUAL_STRING((std::string(metric) + ": values must be finite numbers").c_str(), error.c_str());
    }
  }
}

void form_applies_range_validation(void) {
  std::string fields[RANGE_FIELD_COUNT];
  builtinFields(fields);
  fields[6] = "6000";  //ec target lo above valid hi
  Ranges r = {};
  std::string error;
  TEST_ASSERT_FALSE(rangesFromForm(fields, r, error));
  TEST_ASSERT_EQUAL_STRING("ec: target low must be below target high", error.c_str());
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
  RUN_TEST(round_trip_keeps_fractional_values);
  RUN_TEST(rejects_values_that_collapse_when_saved);
  RUN_TEST(rejects_huge_values_with_magnitude_error);
  RUN_TEST(parse_leaves_out_untouched_on_failure);
  RUN_TEST(rejects_hex_and_other_non_json_numbers);
  RUN_TEST(range_field_names_follow_the_form_contract);
  RUN_TEST(form_fields_parse_into_ranges);
  RUN_TEST(form_rejects_missing_or_garbage_fields_by_metric);
  RUN_TEST(form_applies_range_validation);
  return UNITY_END();
}

#include <string>
#include <unity.h>
#include "pi_cache.h"

//Valid object of exactly `total` bytes: {"a":"xxx"}
static std::string objectOfSize(size_t total) { return "{\"a\":\"" + std::string(total - 8, 'x') + "\"}"; }

void setUp() {}
void tearDown() {}

void first_call_is_due_and_rounds_cycle_three_endpoints(void) {
  PiCache cache;
  TEST_ASSERT_TRUE(cache.due(1000));
  TEST_ASSERT_TRUE(cache.next() == PiEndpoint::Stats);
  TEST_ASSERT_TRUE(cache.due(1001));
  TEST_ASSERT_TRUE(cache.next() == PiEndpoint::Report);
  TEST_ASSERT_TRUE(cache.due(1002));
  TEST_ASSERT_TRUE(cache.next() == PiEndpoint::Thresholds);
  TEST_ASSERT_FALSE(cache.due(1003));
  TEST_ASSERT_FALSE(cache.due(1000 + 299999));
  TEST_ASSERT_TRUE(cache.due(1000 + 300000));
  TEST_ASSERT_TRUE(cache.next() == PiEndpoint::Stats);
}

void round_timing_survives_millis_wrap(void) {
  PiCache cache;
  unsigned long start = 0xFFFFFFFFUL - 1000UL;
  TEST_ASSERT_TRUE(cache.due(start));
  cache.next(); cache.next(); cache.next();
  TEST_ASSERT_FALSE(cache.due(start + 299999UL));
  TEST_ASSERT_TRUE(cache.due(start + 300000UL));
}

void stores_valid_json_and_reports_age(void) {
  PiCache cache;
  cache.record(PiEndpoint::Stats, 200, "  {\"a\":1}\n", 5000);
  std::string text = cache.json(35000);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, text.find("\"stats\":{\"age_s\":30,\"error\":null,\"none\":false,\"body\":{\"a\":1}}"));
  std::string out;
  TEST_ASSERT_FALSE(cache.thresholdsBody(out));
  cache.record(PiEndpoint::Thresholds, 200, "{\"t\":2}", 5000);
  TEST_ASSERT_TRUE(cache.thresholdsBody(out));
  TEST_ASSERT_EQUAL_STRING("{\"t\":2}", out.c_str());
}

void rejects_html_body_and_keeps_previous(void) {
  PiCache cache;
  cache.record(PiEndpoint::Stats, 200, "{\"a\":1}", 0);
  cache.record(PiEndpoint::Stats, 200, "<html>502</html>", 1000);
  std::string text = cache.json(2000);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, text.find("\"error\":\"not JSON\""));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, text.find("\"body\":{\"a\":1}"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, text.find("\"age_s\":2,"));
}

void rejects_truncated_and_oversized(void) {
  PiCache cache;
  std::string big = objectOfSize(6145);
  TEST_ASSERT_EQUAL(6145, (int)big.size());
  cache.record(PiEndpoint::Stats, 200, big, 0);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, cache.json(0).find("\"stats\":{\"age_s\":null,\"error\":\"reply too large\""));
  cache.record(PiEndpoint::Stats, 200, "{\"a\":1", 0);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, cache.json(0).find("\"stats\":{\"age_s\":null,\"error\":\"not JSON\""));
  std::string ok = objectOfSize(6144);
  cache.record(PiEndpoint::Stats, 200, ok, 0);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, cache.json(0).find("\"error\":null"));
  std::string report = objectOfSize(4096);
  cache.record(PiEndpoint::Report, 200, report, 0);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, cache.json(0).find("\"report\":{\"age_s\":0,\"error\":null"));
  cache.record(PiEndpoint::Report, 200, objectOfSize(4097), 0);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, cache.json(0).find("\"report\":{\"age_s\":0,\"error\":\"reply too large\""));
  std::string thr = objectOfSize(2048);
  cache.record(PiEndpoint::Thresholds, 200, thr, 0);
  std::string out;
  TEST_ASSERT_TRUE(cache.thresholdsBody(out));
  cache.record(PiEndpoint::Thresholds, 200, objectOfSize(2049), 0);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, cache.json(0).find("\"thresholds\":{\"age_s\":0,\"error\":\"reply too large\""));
}

void report_404_means_none(void) {
  PiCache cache;
  cache.record(PiEndpoint::Report, 200, "{\"r\":1}", 0);
  cache.record(PiEndpoint::Report, 404, "", 1000);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, cache.json(1000).find("\"report\":{\"age_s\":null,\"error\":null,\"none\":true,\"body\":null}"));
  cache.record(PiEndpoint::Stats, 404, "", 1000);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, cache.json(1000).find("\"error\":\"HTTP 404\""));
}

void unreachable_code(void) {
  PiCache cache;
  cache.record(PiEndpoint::Stats, -1, "", 0);
  cache.record(PiEndpoint::Report, 503, "", 0);
  std::string text = cache.json(0);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, text.find("\"error\":\"unreachable\""));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, text.find("\"error\":\"HTTP 503\""));
}

void unconfigured_sets_all_errors(void) {
  PiCache cache;
  cache.markUnconfigured();
  std::string text = cache.json(0);
  size_t pos = 0;
  int count = 0;
  while ((pos = text.find("\"error\":\"Pi not configured\"", pos)) != std::string::npos) { count++; pos++; }
  TEST_ASSERT_EQUAL(3, count);
}

void json_is_valid_with_empty_cache(void) {
  PiCache cache;
  const char *e = "{\"age_s\":null,\"error\":null,\"none\":false,\"body\":null}";
  std::string expected = std::string("{\"stats\":") + e + ",\"report\":" + e + ",\"thresholds\":" + e + "}";
  TEST_ASSERT_EQUAL_STRING(expected.c_str(), cache.json(0).c_str());
}

void paths_encode_device_name(void) {
  TEST_ASSERT_EQUAL_STRING("/stats?window=24h&device=res%20box%26%C3%A9-_.~A9", piPath(PiEndpoint::Stats, "res box&\xC3\xA9-_.~A9").c_str());
  TEST_ASSERT_EQUAL_STRING("/reports/latest?device=a", piPath(PiEndpoint::Report, "a").c_str());
  TEST_ASSERT_EQUAL_STRING("/thresholds", piPath(PiEndpoint::Thresholds, "a").c_str());
}

void rejects_unbalanced_and_multi_value_bodies(void) {
  const char *bad[] = {"{\"a\":[1,2}", "{oops}", "{\"a\":1}{\"b\":2}", "{\"a\":\"x}", "{\"a\":[1]]}"};
  for (const char *body : bad) {
    PiCache cache;
    cache.record(PiEndpoint::Stats, 200, "{\"keep\":1}", 0);
    cache.record(PiEndpoint::Stats, 200, body, 1000);
    std::string text = cache.json(1000);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, text.find("\"error\":\"not JSON\""));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, text.find("\"body\":{\"keep\":1}"));
  }
}

void accepts_nested_body_with_braces_and_quotes_in_strings(void) {
  PiCache cache;
  cache.record(PiEndpoint::Stats, 200, "{\"s\":\"a}b\\\"c\",\"a\":[{\"x\":1}]}", 0);
  std::string text = cache.json(0);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, text.find("\"error\":null"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, text.find("\"body\":{\"s\":\"a}b\\\"c\",\"a\":[{\"x\":1}]}"));
}

void none_clears_after_later_failure(void) {
  PiCache cache;
  cache.record(PiEndpoint::Report, 404, "", 0);
  cache.record(PiEndpoint::Report, 500, "", 1);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, cache.json(1).find("\"report\":{\"age_s\":null,\"error\":\"HTTP 500\",\"none\":false"));
  cache.record(PiEndpoint::Report, 404, "", 2);
  cache.record(PiEndpoint::Report, 200, "<html>", 3);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, cache.json(3).find("\"none\":false"));
}

void age_clamps_to_zero_when_now_is_behind(void) {
  PiCache cache;
  cache.record(PiEndpoint::Stats, 200, "{\"a\":1}", 5000);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, cache.json(4000).find("\"age_s\":0,"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(first_call_is_due_and_rounds_cycle_three_endpoints);
  RUN_TEST(round_timing_survives_millis_wrap);
  RUN_TEST(stores_valid_json_and_reports_age);
  RUN_TEST(rejects_html_body_and_keeps_previous);
  RUN_TEST(rejects_truncated_and_oversized);
  RUN_TEST(report_404_means_none);
  RUN_TEST(unreachable_code);
  RUN_TEST(unconfigured_sets_all_errors);
  RUN_TEST(json_is_valid_with_empty_cache);
  RUN_TEST(paths_encode_device_name);
  RUN_TEST(rejects_unbalanced_and_multi_value_bodies);
  RUN_TEST(accepts_nested_body_with_braces_and_quotes_in_strings);
  RUN_TEST(none_clears_after_later_failure);
  RUN_TEST(age_clamps_to_zero_when_now_is_behind);
  return UNITY_END();
}

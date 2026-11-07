#include <cmath>
#include <string>
#include <unity.h>
#include "json_text.h"

void setUp() {}
void tearDown() {}

void escapes_quotes_backslash_and_controls(void) {
  TEST_ASSERT_EQUAL_STRING("\"a\\\"b\\\\c\\nd\\u0001\"", jsonString(std::string("a\"b\\c\nd\x01")).c_str());
}

void escapes_carriage_return_and_tab(void) {
  TEST_ASSERT_EQUAL_STRING("\"x\\ry\\tz\"", jsonString(std::string("x\ry\tz")).c_str());
}

void passes_utf8_through(void) {
  TEST_ASSERT_EQUAL_STRING("\"Tank \xC2\xB0" "F\"", jsonString("Tank \xC2\xB0" "F").c_str());
}

void number_is_null_for_nan_and_inf(void) {
  TEST_ASSERT_EQUAL_STRING("null", jsonNumber(NAN).c_str());
  TEST_ASSERT_EQUAL_STRING("null", jsonNumber(INFINITY).c_str());
  TEST_ASSERT_EQUAL_STRING("6.200", jsonNumber(6.2f).c_str());
}

void bool_is_true_or_false(void) {
  TEST_ASSERT_EQUAL_STRING("true", jsonBool(true).c_str());
  TEST_ASSERT_EQUAL_STRING("false", jsonBool(false).c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(escapes_quotes_backslash_and_controls);
  RUN_TEST(escapes_carriage_return_and_tab);
  RUN_TEST(passes_utf8_through);
  RUN_TEST(number_is_null_for_nan_and_inf);
  RUN_TEST(bool_is_true_or_false);
  return UNITY_END();
}

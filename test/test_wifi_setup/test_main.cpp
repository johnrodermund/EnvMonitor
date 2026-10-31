#include <unity.h>
#include "wifi_setup.h"

void setUp() {}
void tearDown() {}

void network_needs_a_name() {
  TEST_ASSERT_FALSE(validWifiCredentials("", "longenough"));
  TEST_ASSERT_TRUE(validWifiCredentials("home", "longenough"));
}

void network_name_is_limited_to_32_bytes() {
  TEST_ASSERT_TRUE(validWifiCredentials("12345678901234567890123456789012", ""));
  TEST_ASSERT_FALSE(validWifiCredentials("123456789012345678901234567890123", ""));
}

void password_is_empty_or_a_valid_wpa_length() {
  TEST_ASSERT_TRUE(validWifiCredentials("home", ""));
  TEST_ASSERT_FALSE(validWifiCredentials("home", "1234567"));
  TEST_ASSERT_TRUE(validWifiCredentials("home", "12345678"));
  TEST_ASSERT_TRUE(validWifiCredentials("home", "123456789012345678901234567890123456789012345678901234567890123"));
  TEST_ASSERT_FALSE(validWifiCredentials("home", "1234567890123456789012345678901234567890123456789012345678901234"));
}

void portal_ends_once_credentials_are_saved() {
  TEST_ASSERT_TRUE(setupPortalFinished(true, false, 1000, 300000));
}

void portal_retries_a_saved_network_after_timeout() {
  TEST_ASSERT_FALSE(setupPortalFinished(false, true, 299999, 300000));
  TEST_ASSERT_TRUE(setupPortalFinished(false, true, 300001, 300000));
}

void portal_waits_indefinitely_with_no_saved_network() {
  TEST_ASSERT_FALSE(setupPortalFinished(false, false, 4000000000UL, 300000));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(network_needs_a_name);
  RUN_TEST(network_name_is_limited_to_32_bytes);
  RUN_TEST(password_is_empty_or_a_valid_wpa_length);
  RUN_TEST(portal_ends_once_credentials_are_saved);
  RUN_TEST(portal_retries_a_saved_network_after_timeout);
  RUN_TEST(portal_waits_indefinitely_with_no_saved_network);
  return UNITY_END();
}

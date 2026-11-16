#include <cmath>
#include <string>
#include <unity.h>
#include "event_sync.h"

void setUp() {}
void tearDown() {}

TankEvent manualEvent() {
  TankEvent e;
  e.seq = 7;
  e.occurredAt = 1800000000LL;  //2027-01-15T08:00:00Z
  e.updatedAt = 1800000060LL;
  e.kind = "other";
  e.source = "manual";
  e.status = "confirmed";
  return e;
}

void test_uid_and_path_encode_device() {
  TEST_ASSERT_EQUAL_STRING("envmonitor-1:7", eventUid("envmonitor-1", 7).c_str());
  //The Pi only takes [A-Za-z0-9._:-] in a uid, so other characters become '-'
  TEST_ASSERT_EQUAL_STRING("tank-1:7", eventUid("tank 1", 7).c_str());
  TEST_ASSERT_EQUAL_STRING("t--nk:7", eventUid("t\xC3\xA4nk", 7).c_str());
  TEST_ASSERT_EQUAL_STRING("/events/tank-1%3A7", eventSyncPath("tank 1", manualEvent()).c_str());
}

void test_json_nulls_and_times() {
  TankEvent e = manualEvent();
  TEST_ASSERT_EQUAL_STRING(
      "{\"device\":\"tank 1\",\"occurred_at\":\"2027-01-15T08:00:00Z\",\"origin_at\":null,"
      "\"kind\":\"other\",\"detail\":null,\"source\":\"manual\",\"status\":\"confirmed\","
      "\"ec_before\":null,\"ec_after\":null,\"ph_before\":null,\"ph_after\":null,\"note\":\"\","
      "\"updated_at\":\"2027-01-15T08:01:00Z\"}",
      eventSyncJson("tank 1", e).c_str());

  e.kind = "topup";
  e.detail = "unknown";
  e.source = "detected";
  e.originAt = e.occurredAt;
  e.ecBefore = 250;
  e.ecAfter = 244.5f;
  e.phBefore = 6.5f;
  e.phAfter = NAN;
  e.deleted = true;
  std::string json = eventSyncJson("d", e);
  TEST_ASSERT_TRUE(json.find("\"origin_at\":\"2027-01-15T08:00:00Z\"") != std::string::npos);
  TEST_ASSERT_TRUE(json.find("\"detail\":\"unknown\"") != std::string::npos);
  TEST_ASSERT_TRUE(json.find("\"ec_before\":250.000,\"ec_after\":244.500,\"ph_before\":6.500,\"ph_after\":null") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(json.find(",\"deleted\":true}") != std::string::npos);
}

void test_json_escapes_note() {
  TankEvent e = manualEvent();
  e.note = "a | \"b\"\n\\ \xC3\xA9";
  std::string json = eventSyncJson("d", e);
  TEST_ASSERT_TRUE(json.find("\"note\":\"a | \\\"b\\\"\\n\\\\ \xC3\xA9\"") != std::string::npos);
}

void test_outcomes() {
  TEST_ASSERT_TRUE(eventSyncOutcome(200) == SendOutcome::Delivered);
  TEST_ASSERT_TRUE(eventSyncOutcome(201) == SendOutcome::Delivered);
  TEST_ASSERT_TRUE(eventSyncOutcome(400) == SendOutcome::Rejected);
  TEST_ASSERT_TRUE(eventSyncOutcome(422) == SendOutcome::Rejected);
  TEST_ASSERT_TRUE(eventSyncOutcome(401) == SendOutcome::RetryLater);
  TEST_ASSERT_TRUE(eventSyncOutcome(404) == SendOutcome::RetryLater);
  TEST_ASSERT_TRUE(eventSyncOutcome(500) == SendOutcome::RetryLater);
  TEST_ASSERT_TRUE(eventSyncOutcome(-1) == SendOutcome::RetryLater);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_uid_and_path_encode_device);
  RUN_TEST(test_json_nulls_and_times);
  RUN_TEST(test_json_escapes_note);
  RUN_TEST(test_outcomes);
  return UNITY_END();
}

#include <string>
#include <unity.h>
#include "event_api.h"

void setUp() {}
void tearDown() {}

const long long NOW = 1800000000LL;  //2027-01-15T08:00:00Z

EventForm form(const char *op) {
  EventForm f;
  f.op = op;
  return f;
}

void test_parses_iso_time_with_offset() {
  long long epoch = 0;
  TEST_ASSERT_TRUE(parseIsoTime("2027-01-15T08:00:00Z", epoch));
  TEST_ASSERT_EQUAL_INT64(NOW, epoch);
  TEST_ASSERT_TRUE(parseIsoTime("2027-01-15T03:00:00-05:00", epoch));
  TEST_ASSERT_EQUAL_INT64(NOW, epoch);
  TEST_ASSERT_TRUE(parseIsoTime("2027-01-15T13:30+05:30", epoch));  //seconds are optional
  TEST_ASSERT_EQUAL_INT64(NOW, epoch);
  TEST_ASSERT_TRUE(parseIsoTime("2024-02-29T00:00:00Z", epoch));
  TEST_ASSERT_EQUAL_INT64(1709164800LL, epoch);
  TEST_ASSERT_FALSE(parseIsoTime("2027-01-15T08:00:00", epoch));  //no zone: ambiguous
  TEST_ASSERT_FALSE(parseIsoTime("2027-13-15T08:00:00Z", epoch));
  TEST_ASSERT_FALSE(parseIsoTime("2027-01-15T08:00:00Zjunk", epoch));
  TEST_ASSERT_FALSE(parseIsoTime("", epoch));
}

void test_create_returns_seq_and_defaults_time_to_now() {
  EventLog<10> log;
  EventForm f = form("create");
  f.kind = "topup";
  f.detail = "di";
  f.note = "a b";
  EventApiResult r = applyEventForm(log, f, NOW);
  TEST_ASSERT_EQUAL_INT(200, r.httpCode);
  TEST_ASSERT_EQUAL_STRING("{\"seq\":1}", r.json.c_str());
  TEST_ASSERT_TRUE(r.changed);
  TEST_ASSERT_EQUAL_INT64(NOW, log.find(1)->occurredAt);
  TEST_ASSERT_EQUAL_STRING("di", log.find(1)->detail.c_str());
  TEST_ASSERT_EQUAL_STRING("a b", log.find(1)->note.c_str());
}

void test_create_takes_time_and_refuses_bad_fields() {
  EventLog<10> log;
  EventForm f = form("create");
  f.kind = "other";
  f.occurredAt = "2027-01-15T02:00:00-05:00";
  TEST_ASSERT_EQUAL_INT(200, applyEventForm(log, f, NOW).httpCode);
  TEST_ASSERT_EQUAL_INT64(NOW - 3600, log.find(1)->occurredAt);

  f.occurredAt = "yesterday";
  EventApiResult r = applyEventForm(log, f, NOW);
  TEST_ASSERT_EQUAL_INT(400, r.httpCode);
  TEST_ASSERT_EQUAL_STRING("{\"error\":\"bad occurred_at\"}", r.json.c_str());
  TEST_ASSERT_FALSE(r.changed);

  f.occurredAt = "2027-01-17T08:00:00Z";  //two days ahead
  TEST_ASSERT_EQUAL_INT(400, applyEventForm(log, f, NOW).httpCode);

  f.occurredAt = "";
  f.kind = "";
  TEST_ASSERT_EQUAL_INT(400, applyEventForm(log, f, NOW).httpCode);
  f.kind = "party";
  TEST_ASSERT_EQUAL_INT(400, applyEventForm(log, f, NOW).httpCode);
  TEST_ASSERT_EQUAL_INT(1, log.size());
}

void test_update_changes_only_given_fields() {
  EventLog<10> log;
  log.addManual("topup", "di", NOW - 600, "keep", NOW - 60);
  EventForm f = form("update");
  f.seq = "1";
  f.hasDetail = true;
  f.detail = "tap";
  EventApiResult r = applyEventForm(log, f, NOW);
  TEST_ASSERT_EQUAL_INT(200, r.httpCode);
  TEST_ASSERT_EQUAL_STRING("{\"seq\":1}", r.json.c_str());
  TEST_ASSERT_EQUAL_STRING("tap", log.find(1)->detail.c_str());
  TEST_ASSERT_EQUAL_STRING("keep", log.find(1)->note.c_str());
  TEST_ASSERT_EQUAL_INT64(NOW - 600, log.find(1)->occurredAt);

  f.hasNote = true;
  f.note = "";  //an empty note given on purpose clears it
  applyEventForm(log, f, NOW);
  TEST_ASSERT_EQUAL_STRING("", log.find(1)->note.c_str());
}

void test_update_unknown_seq_is_404() {
  EventLog<10> log;
  EventForm f = form("update");
  f.seq = "5";
  TEST_ASSERT_EQUAL_INT(404, applyEventForm(log, f, NOW).httpCode);
  f.seq = "x";
  TEST_ASSERT_EQUAL_INT(400, applyEventForm(log, f, NOW).httpCode);
}

void test_confirm_and_reject_detected() {
  EventLog<10> log;
  Candidate c;
  c.found = true;
  c.topup = true;
  c.occurredAt = NOW - 3600;
  c.size = 6;
  log.offerDetected(c, NOW - 60);
  EventForm f = form("confirm");
  f.seq = "1";
  f.hasDetail = true;
  f.detail = "di";
  TEST_ASSERT_EQUAL_INT(200, applyEventForm(log, f, NOW).httpCode);
  TEST_ASSERT_EQUAL_STRING("confirmed", log.find(1)->status.c_str());
  TEST_ASSERT_EQUAL_STRING("di", log.find(1)->detail.c_str());
  f = form("reject");
  f.seq = "1";
  TEST_ASSERT_EQUAL_INT(200, applyEventForm(log, f, NOW).httpCode);
  TEST_ASSERT_EQUAL_STRING("rejected", log.find(1)->status.c_str());
}

void test_delete_manual_ok_detected_409() {
  EventLog<10> log;
  Candidate c;
  c.found = true;
  c.topup = true;
  c.occurredAt = NOW - 3600;
  c.size = 6;
  log.offerDetected(c, NOW - 60);
  log.addManual("other", "", NOW, "", NOW);
  EventForm f = form("delete");
  f.seq = "1";
  EventApiResult r = applyEventForm(log, f, NOW);
  TEST_ASSERT_EQUAL_INT(409, r.httpCode);
  TEST_ASSERT_EQUAL_STRING("{\"error\":\"detected events can be rejected, not deleted\"}", r.json.c_str());
  f.seq = "2";
  TEST_ASSERT_EQUAL_INT(200, applyEventForm(log, f, NOW).httpCode);
  TEST_ASSERT_TRUE(log.find(2)->deleted);
  TEST_ASSERT_EQUAL_INT(404, applyEventForm(log, f, NOW).httpCode);  //already deleted
}

void test_unset_clock_is_503_and_unknown_op_400() {
  EventLog<10> log;
  EventForm f = form("create");
  f.kind = "other";
  EventApiResult r = applyEventForm(log, f, 5000);
  TEST_ASSERT_EQUAL_INT(503, r.httpCode);
  TEST_ASSERT_EQUAL_STRING("{\"error\":\"clock not set yet\"}", r.json.c_str());
  TEST_ASSERT_EQUAL_INT(400, applyEventForm(log, form("explode"), NOW).httpCode);
}

void test_detect_settings_apply_and_validate() {
  DetectLimits limits;
  std::string error;
  TEST_ASSERT_TRUE(applyDetectSettings("4.5", "", "1", limits, error));
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 4.5, limits.topupMinDrop);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 5.0, limits.feedingMinRise);
  TEST_ASSERT_TRUE(limits.feedingEnabled);
  TEST_ASSERT_TRUE(applyDetectSettings("", "7", "false", limits, error));
  TEST_ASSERT_FALSE(limits.feedingEnabled);
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 7, limits.feedingMinRise);

  DetectLimits before = limits;
  TEST_ASSERT_FALSE(applyDetectSettings("0.1", "", "", limits, error));
  TEST_ASSERT_EQUAL_STRING("topup_min_drop must be a number from 0.5 to 100", error.c_str());
  TEST_ASSERT_FALSE(applyDetectSettings("abc", "", "", limits, error));
  TEST_ASSERT_FALSE(applyDetectSettings("", "101", "", limits, error));
  TEST_ASSERT_FALSE(applyDetectSettings("", "", "maybe", limits, error));
  TEST_ASSERT_FALSE(applyDetectSettings("5", "", "maybe", limits, error));  //nothing applies when one field is bad
  TEST_ASSERT_FLOAT_WITHIN(0.0001, before.topupMinDrop, limits.topupMinDrop);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parses_iso_time_with_offset);
  RUN_TEST(test_create_returns_seq_and_defaults_time_to_now);
  RUN_TEST(test_create_takes_time_and_refuses_bad_fields);
  RUN_TEST(test_update_changes_only_given_fields);
  RUN_TEST(test_update_unknown_seq_is_404);
  RUN_TEST(test_confirm_and_reject_detected);
  RUN_TEST(test_delete_manual_ok_detected_409);
  RUN_TEST(test_unset_clock_is_503_and_unknown_op_400);
  RUN_TEST(test_detect_settings_apply_and_validate);
  return UNITY_END();
}

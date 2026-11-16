#include <cmath>
#include <string>
#include <unity.h>
#include "event_log.h"
#include "nov_readings.h"

void setUp() {}
void tearDown() {}

const long long T0 = 1800000000LL;  //2027-01-15, any NTP-synced time
const long long NOW = T0 + 86400;

Candidate topupAt(long long at, float size, float meanSize = 0) {
  Candidate c;
  c.found = true;
  c.topup = true;
  c.occurredAt = at;
  c.ecBefore = 250;
  c.ecAfter = 250 - size;
  c.phBefore = 6.5f;
  c.phAfter = 6.6f;
  c.size = size;
  c.meanSize = meanSize ? meanSize : size;
  return c;
}

EventEdit statusEdit(const char *status) {
  EventEdit edit;
  edit.hasStatus = true;
  edit.status = status;
  return edit;
}

void test_detected_topup_added_suggested_unknown() {
  EventLog<10> log;
  TEST_ASSERT_TRUE(log.offerDetected(topupAt(T0, 6), NOW));
  TEST_ASSERT_EQUAL_INT(1, log.size());
  const TankEvent *e = log.find(1);
  TEST_ASSERT_NOT_NULL(e);
  TEST_ASSERT_EQUAL_STRING("topup", e->kind.c_str());
  TEST_ASSERT_EQUAL_STRING("unknown", e->detail.c_str());
  TEST_ASSERT_EQUAL_STRING("detected", e->source.c_str());
  TEST_ASSERT_EQUAL_STRING("suggested", e->status.c_str());
  TEST_ASSERT_EQUAL_INT64(T0, e->occurredAt);
  TEST_ASSERT_EQUAL_INT64(T0, e->originAt);
  TEST_ASSERT_EQUAL_INT64(NOW, e->updatedAt);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 244, e->ecAfter);
  TEST_ASSERT_EQUAL_INT(1, log.dirtyCount());
}

void test_feeding_candidate_is_feeding_without_detail() {
  EventLog<10> log;
  Candidate c = topupAt(T0, 6);
  c.topup = false;
  log.offerDetected(c, NOW);
  TEST_ASSERT_EQUAL_STRING("feeding", log.find(1)->kind.c_str());
  TEST_ASSERT_EQUAL_STRING("", log.find(1)->detail.c_str());
  //A feeding doesn't block a top-up at the same time
  TEST_ASSERT_TRUE(log.offerDetected(topupAt(T0, 6), NOW));
}

void test_rejected_blocks_redetection_within_2h() {
  EventLog<10> log;
  log.offerDetected(topupAt(T0, 6), NOW);
  TEST_ASSERT_TRUE(log.update(1, statusEdit("rejected"), NOW));
  TEST_ASSERT_FALSE(log.offerDetected(topupAt(T0 + 7200, 9), NOW));
  TEST_ASSERT_EQUAL_STRING("rejected", log.find(1)->status.c_str());
  TEST_ASSERT_TRUE(log.offerDetected(topupAt(T0 + 7201, 9), NOW));
  TEST_ASSERT_EQUAL_INT(2, log.size());
}

void test_manual_event_blocks_detection() {
  EventLog<10> log;
  log.addManual("topup", "di", T0, "", NOW);
  TEST_ASSERT_FALSE(log.offerDetected(topupAt(T0 + 600, 6), NOW));
}

void test_moved_event_blocks_at_origin() {
  EventLog<10> log;
  log.offerDetected(topupAt(T0, 6), NOW);
  EventEdit edit;
  edit.hasOccurredAt = true;
  edit.occurredAt = T0 + 3 * 3600;
  TEST_ASSERT_TRUE(log.update(1, edit, NOW));
  TEST_ASSERT_EQUAL_INT64(T0, log.find(1)->originAt);
  TEST_ASSERT_FALSE(log.offerDetected(topupAt(T0, 6), NOW));
  TEST_ASSERT_EQUAL_INT(1, log.size());
}

void test_larger_candidate_replaces_unedited_suggested() {
  EventLog<10> log;
  log.offerDetected(topupAt(T0, 4), NOW);
  TEST_ASSERT_FALSE(log.offerDetected(topupAt(T0 + 600, 3.5f), NOW));
  TEST_ASSERT_TRUE(log.offerDetected(topupAt(T0 + 600, 6), NOW + 1));
  TEST_ASSERT_EQUAL_INT(1, log.size());
  const TankEvent *e = log.find(1);  //replaced in place: same seq, so the same uid on the Pi
  TEST_ASSERT_EQUAL_INT64(T0 + 600, e->occurredAt);
  TEST_ASSERT_EQUAL_INT64(T0 + 600, e->originAt);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 244, e->ecAfter);
  TEST_ASSERT_EQUAL_INT64(NOW + 1, e->updatedAt);
}

void test_exact_tie_goes_to_larger_mean_drop() {
  EventLog<10> log;
  log.offerDetected(topupAt(T0, 6, 4), NOW);
  TEST_ASSERT_FALSE(log.offerDetected(topupAt(T0 + 600, 6, 4), NOW));
  TEST_ASSERT_TRUE(log.offerDetected(topupAt(T0 + 600, 6, 5), NOW));
  TEST_ASSERT_EQUAL_INT64(T0 + 600, log.find(1)->occurredAt);
}

void test_edited_suggested_is_not_replaced() {
  EventLog<10> log;
  log.offerDetected(topupAt(T0, 4), NOW);
  EventEdit edit = statusEdit("suggested");  //an edit that keeps the status still counts as an edit
  edit.hasNote = true;
  edit.note = "hm";
  log.update(1, edit, NOW);
  TEST_ASSERT_EQUAL_STRING("suggested", log.find(1)->status.c_str());
  TEST_ASSERT_FALSE(log.offerDetected(topupAt(T0 + 600, 9), NOW));
}

void test_edit_confirms_detected() {
  EventLog<10> log;
  log.offerDetected(topupAt(T0, 6), NOW);
  EventEdit edit;
  edit.hasDetail = true;
  edit.detail = "di";
  TEST_ASSERT_TRUE(log.update(1, edit, NOW + 5));
  TEST_ASSERT_EQUAL_STRING("confirmed", log.find(1)->status.c_str());
  TEST_ASSERT_EQUAL_STRING("di", log.find(1)->detail.c_str());
  TEST_ASSERT_EQUAL_INT64(NOW + 5, log.find(1)->updatedAt);
  TEST_ASSERT_FALSE(log.update(99, edit, NOW));
}

void test_kind_change_clears_detail() {
  EventLog<10> log;
  unsigned long seq = log.addManual("topup", "tap", T0, "", NOW);
  EventEdit edit;
  edit.hasKind = true;
  edit.kind = "buffer";
  log.update(seq, edit, NOW);
  TEST_ASSERT_EQUAL_STRING("buffer", log.find(seq)->kind.c_str());
  TEST_ASSERT_EQUAL_STRING("", log.find(seq)->detail.c_str());
}

void test_manual_topup_without_detail_is_unknown() {
  EventLog<10> log;
  unsigned long seq = log.addManual("topup", "", T0, "", NOW);
  TEST_ASSERT_EQUAL_STRING("unknown", log.find(seq)->detail.c_str());
  TEST_ASSERT_EQUAL_STRING("manual", log.find(seq)->source.c_str());
  TEST_ASSERT_EQUAL_STRING("confirmed", log.find(seq)->status.c_str());
  TEST_ASSERT_EQUAL_INT64(0, log.find(seq)->originAt);
}

void test_remove_detected_refused_manual_tombstoned() {
  EventLog<10> log;
  log.offerDetected(topupAt(T0, 6), NOW);
  unsigned long manual = log.addManual("other", "", T0 + 9000, "x", NOW);
  TEST_ASSERT_FALSE(log.remove(1, NOW));
  TEST_ASSERT_TRUE(log.remove(manual, NOW + 2));
  const TankEvent *e = log.find(manual);
  TEST_ASSERT_TRUE(e->deleted);
  TEST_ASSERT_EQUAL_INT64(NOW + 2, e->updatedAt);
  TEST_ASSERT_FALSE(log.update(manual, statusEdit("confirmed"), NOW));  //a tombstone can't be edited
  TEST_ASSERT_FALSE(log.remove(manual, NOW));
}

void test_unsynced_clock_changes_nothing() {
  EventLog<10> log;
  long long bootClock = 5000;
  TEST_ASSERT_FALSE(log.offerDetected(topupAt(T0, 6), bootClock));
  TEST_ASSERT_EQUAL_UINT32(0, log.addManual("other", "", T0, "", bootClock));
  TEST_ASSERT_EQUAL_INT(0, log.size());
  unsigned long seq = log.addManual("other", "", T0, "", NOW);
  TEST_ASSERT_FALSE(log.update(seq, statusEdit("rejected"), bootClock));
  TEST_ASSERT_FALSE(log.remove(seq, bootClock));
  TEST_ASSERT_EQUAL_INT64(NOW, log.find(seq)->updatedAt);
}

void test_updated_at_never_goes_backwards() {
  EventLog<10> log;
  unsigned long seq = log.addManual("other", "", T0, "", NOW);
  log.update(seq, statusEdit("rejected"), NOW - 100);
  TEST_ASSERT_EQUAL_INT64(NOW, log.find(seq)->updatedAt);
}

void test_edit_during_sync_stays_dirty() {
  EventLog<10> log;
  log.offerDetected(topupAt(T0, 6), NOW);
  const TankEvent *e = log.nextDirty(NOW);
  TEST_ASSERT_NOT_NULL(e);
  unsigned gen = e->dirtyGen;
  log.update(1, statusEdit("confirmed"), NOW);
  log.markSynced(1, gen);
  TEST_ASSERT_EQUAL_INT(1, log.dirtyCount());
  e = log.nextDirty(NOW);
  TEST_ASSERT_EQUAL_STRING("confirmed", e->status.c_str());
  log.markSynced(1, e->dirtyGen);
  TEST_ASSERT_EQUAL_INT(0, log.dirtyCount());
  TEST_ASSERT_NULL(log.nextDirty(NOW));
}

void test_far_future_updated_at_is_not_synced() {
  EventLog<10> log;
  log.addManual("other", "", T0, "", NOW + 2 * 86400);  //stamped by a clock two days fast
  log.addManual("other", "", T0, "", NOW);
  const TankEvent *e = log.nextDirty(NOW);
  TEST_ASSERT_EQUAL_UINT32(2, e->seq);
  log.markSynced(2, e->dirtyGen);
  TEST_ASSERT_NULL(log.nextDirty(NOW));
  TEST_ASSERT_EQUAL_INT(1, log.dirtyCount());
  TEST_ASSERT_EQUAL_INT(1, log.futureCount(NOW));
  TEST_ASSERT_NOT_NULL(log.nextDirty(NOW + 2 * 86400 - 86400));
}

void test_tombstone_dropped_after_sync() {
  EventLog<10> log;
  unsigned long seq = log.addManual("other", "", T0, "", NOW);
  log.remove(seq, NOW);
  const TankEvent *e = log.nextDirty(NOW);
  TEST_ASSERT_TRUE(e->deleted);
  log.markSynced(seq, e->dirtyGen);
  TEST_ASSERT_NULL(log.find(seq));
  TEST_ASSERT_EQUAL_INT(0, log.size());
}

void test_full_drops_synced_first_then_counts() {
  EventLog<3> log;
  for (int i = 0; i < 3; i++) {
    log.addManual("other", "", T0 + i * 100, "", NOW);
  }
  log.markSynced(2, log.find(2)->dirtyGen);
  log.addManual("other", "", T0 + 900, "", NOW);  //drops seq 2, the only synced one
  TEST_ASSERT_NULL(log.find(2));
  TEST_ASSERT_NOT_NULL(log.find(1));
  TEST_ASSERT_EQUAL_UINT32(0, log.droppedUnsynced());
  log.addManual("other", "", T0 + 1000, "", NOW);  //all unsynced: drops the oldest, seq 1
  TEST_ASSERT_NULL(log.find(1));
  TEST_ASSERT_EQUAL_INT(3, log.size());
  TEST_ASSERT_EQUAL_UINT32(1, log.droppedUnsynced());
}

void test_save_load_round_trip_with_awkward_note() {
  EventLog<10> log;
  log.offerDetected(topupAt(T0, 6, 5.25f), NOW);
  const std::string note = "a | b\\c\n\"\xC3\xA9\" -  \\s";
  unsigned long manual = log.addManual("water_change", "", T0 + 9000, note, NOW);
  log.markSynced(1, log.find(1)->dirtyGen);
  unsigned long gone = log.addManual("other", "", T0, "", NOW);
  log.remove(gone, NOW);
  log.markSynced(gone, log.find(gone)->dirtyGen);  //seq 3 is used up and gone
  log.addManual("buffer", "", T0, "", NOW);  //seq 4, empty note

  EventLog<10> loaded;
  TEST_ASSERT_TRUE(loaded.loadText(log.saveText()));
  TEST_ASSERT_EQUAL_STRING(log.saveText().c_str(), loaded.saveText().c_str());
  TEST_ASSERT_EQUAL_INT(3, loaded.size());
  TEST_ASSERT_EQUAL_STRING(note.c_str(), loaded.find(manual)->note.c_str());
  TEST_ASSERT_EQUAL_STRING("", loaded.find(4)->note.c_str());
  const TankEvent *detected = loaded.find(1);
  TEST_ASSERT_EQUAL_STRING("suggested", detected->status.c_str());
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 5.25, detected->meanSize);
  TEST_ASSERT_TRUE(std::isnan(loaded.find(manual)->ecBefore));
  //Synced stays synced, unsynced stays dirty
  TEST_ASSERT_EQUAL_INT(2, loaded.dirtyCount());
  TEST_ASSERT_EQUAL_UINT32(manual, loaded.nextDirty(NOW)->seq);
  //seq continues past every number ever used
  TEST_ASSERT_EQUAL_UINT32(5, loaded.addManual("other", "", T0, "", NOW));
}

void test_load_refuses_bad_header_and_skips_bad_lines() {
  EventLog<10> log;
  TEST_ASSERT_FALSE(log.loadText(""));
  TEST_ASSERT_FALSE(log.loadText("readings v1\n"));
  TEST_ASSERT_TRUE(log.loadText("events v1 7\ngarbage line\n"));
  TEST_ASSERT_EQUAL_INT(0, log.size());
  TEST_ASSERT_EQUAL_UINT32(7, log.addManual("other", "", T0, "", NOW));
}

void test_fill_deltas_for_manual_event() {
  EventLog<10> log;
  DetectSample s[14];
  for (int i = 0; i < 14; i++) {
    s[i].epoch = T0 + i * 600;
    s[i].ec = i < 7 ? 250 : 244;
    s[i].ph = i < 7 ? 6.5f : 6.7f;
  }
  unsigned long seq = log.addManual("topup", "di", s[7].epoch - 30, "", NOW);
  log.markSynced(seq, log.find(seq)->dirtyGen);
  TEST_ASSERT_FALSE(log.fillDeltas(s, 12, NOW + 1));  //only 5 samples after it yet
  TEST_ASSERT_TRUE(std::isnan(log.find(seq)->ecBefore));
  TEST_ASSERT_TRUE(log.fillDeltas(s, 13, NOW + 1));
  const TankEvent *e = log.find(seq);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 250, e->ecBefore);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 244, e->ecAfter);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 6.5, e->phBefore);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 6.7, e->phAfter);
  TEST_ASSERT_EQUAL_INT(1, log.dirtyCount());
  TEST_ASSERT_FALSE(log.fillDeltas(s, 14, NOW + 2));  //filled once only
}

void test_moving_an_event_refills_its_deltas() {
  EventLog<10> log;
  log.offerDetected(topupAt(T0, 6), NOW);
  EventEdit edit;
  edit.hasOccurredAt = true;
  edit.occurredAt = T0 + 600;
  log.update(1, edit, NOW);
  TEST_ASSERT_TRUE(std::isnan(log.find(1)->ecBefore));
}

void test_list_json_newest_first_without_tombstones() {
  EventLog<10> log;
  log.addManual("other", "", T0, "first \"one\"", NOW);
  log.offerDetected(topupAt(T0 + 9000, 6), NOW);
  unsigned long gone = log.addManual("buffer", "", T0 + 20000, "", NOW);
  log.remove(gone, NOW);
  log.markSynced(1, log.find(1)->dirtyGen);
  std::string json = log.listJson();
  TEST_ASSERT_EQUAL_STRING(
      "{\"events\":["
      "{\"seq\":2,\"occurred_at\":\"2027-01-15T10:30:00Z\",\"kind\":\"topup\",\"detail\":\"unknown\","
      "\"source\":\"detected\",\"status\":\"suggested\",\"ec_before\":250.000,\"ec_after\":244.000,"
      "\"ph_before\":6.500,\"ph_after\":6.600,\"note\":\"\",\"backed_up\":false},"
      "{\"seq\":1,\"occurred_at\":\"2027-01-15T08:00:00Z\",\"kind\":\"other\",\"detail\":null,"
      "\"source\":\"manual\",\"status\":\"confirmed\",\"ec_before\":null,\"ec_after\":null,"
      "\"ph_before\":null,\"ph_after\":null,\"note\":\"first \\\"one\\\"\",\"backed_up\":true}"
      "]}",
      json.c_str());
}

void test_validates_fields() {
  std::string error;
  TEST_ASSERT_TRUE(validEventFields("topup", "di", "confirmed", "ok", error));
  TEST_ASSERT_TRUE(validEventFields("other", "", "", "", error));
  TEST_ASSERT_FALSE(validEventFields("party", "", "", "", error));
  TEST_ASSERT_EQUAL_STRING("bad kind", error.c_str());
  TEST_ASSERT_FALSE(validEventFields("topup", "salt", "", "", error));
  TEST_ASSERT_FALSE(validEventFields("topup", "", "maybe", "", error));
  //200 characters is fine even when some take two bytes; 201 isn't
  std::string note(198, 'a');
  note += "\xC3\xA9\xC3\xA9";
  TEST_ASSERT_TRUE(validEventFields("other", "", "", note, error));
  note += "b";
  TEST_ASSERT_FALSE(validEventFields("other", "", "", note, error));
  TEST_ASSERT_FALSE(validEventFields("other", "", "", "a\x01" "b", error));  //control bytes other than newline
}

//The device's online rule must find the same top-ups as PiServer agent/events.py on the same data
void test_replay_of_november_finds_the_reference_topups() {
  EventLog<100> log;
  int count = sizeof(novReadings) / sizeof(novReadings[0]);
  for (int i = DETECT_WINDOW - 1; i < count; i++) {
    Candidate c = detectAt(novReadings + i - (DETECT_WINDOW - 1), DetectLimits());
    if (c.found) {
      log.offerDetected(c, novReadings[i].epoch);
    }
  }
  const char *expected[] = {"2026-11-05T21:16:08Z", "2026-11-06T12:56:08Z", "2026-11-07T15:56:09Z",
                            "2026-11-08T14:33:06Z", "2026-11-10T01:23:07Z", "2026-11-10T19:43:08Z",
                            "2026-11-11T18:23:08Z", "2026-11-13T01:53:10Z", "2026-11-13T21:23:10Z"};
  TEST_ASSERT_EQUAL_INT(9, log.size());
  for (int i = 0; i < 9; i++) {
    TEST_ASSERT_EQUAL_STRING(expected[i], utcIso(log.find(i + 1)->occurredAt).c_str());
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_detected_topup_added_suggested_unknown);
  RUN_TEST(test_feeding_candidate_is_feeding_without_detail);
  RUN_TEST(test_rejected_blocks_redetection_within_2h);
  RUN_TEST(test_manual_event_blocks_detection);
  RUN_TEST(test_moved_event_blocks_at_origin);
  RUN_TEST(test_larger_candidate_replaces_unedited_suggested);
  RUN_TEST(test_exact_tie_goes_to_larger_mean_drop);
  RUN_TEST(test_edited_suggested_is_not_replaced);
  RUN_TEST(test_edit_confirms_detected);
  RUN_TEST(test_kind_change_clears_detail);
  RUN_TEST(test_manual_topup_without_detail_is_unknown);
  RUN_TEST(test_remove_detected_refused_manual_tombstoned);
  RUN_TEST(test_unsynced_clock_changes_nothing);
  RUN_TEST(test_updated_at_never_goes_backwards);
  RUN_TEST(test_edit_during_sync_stays_dirty);
  RUN_TEST(test_far_future_updated_at_is_not_synced);
  RUN_TEST(test_tombstone_dropped_after_sync);
  RUN_TEST(test_full_drops_synced_first_then_counts);
  RUN_TEST(test_save_load_round_trip_with_awkward_note);
  RUN_TEST(test_load_refuses_bad_header_and_skips_bad_lines);
  RUN_TEST(test_fill_deltas_for_manual_event);
  RUN_TEST(test_moving_an_event_refills_its_deltas);
  RUN_TEST(test_list_json_newest_first_without_tombstones);
  RUN_TEST(test_validates_fields);
  RUN_TEST(test_replay_of_november_finds_the_reference_topups);
  return UNITY_END();
}

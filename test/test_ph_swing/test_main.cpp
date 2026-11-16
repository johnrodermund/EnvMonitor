#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <unity.h>
#include "ph_swing.h"

//Local time is UTC in these tests: DAY0 is a midnight, D0 its day, D1 the next
const long long DAY0 = 1800000000LL - 1800000000LL % 86400;
const long long DAY1 = DAY0 + 86400;
const int D0 = 20270114;
const int D1 = 20270115;

SwingSample buf[400];
int nbuf = 0;

void setUp() { nbuf = 0; }
void tearDown() {}

//Readings every `every` minutes from fromMinute (inclusive) to toMinute (exclusive), EC 250, 70 °F
template <typename F>
void addDay(long long start, int day, F phAt, int fromMinute = 0, int toMinute = 1440, int every = 10) {
  for (int m = fromMinute; m < toMinute; m += every) {
    SwingSample s;
    s.epoch = start + m * 60LL;
    s.localDay = day;
    s.localMinute = m;
    s.ph = phAt(m);
    s.ec = 250;
    s.wtF = 70;
    buf[nbuf++] = s;
  }
}

bool between(long long v, long long lo, long long hi) { return v >= lo && v <= hi; }

void day_swing_rise_fall_and_times() {
  addDay(DAY0, D0, [](int m) { return (m >= 540 && m < 1260) ? 6.0f : 5.0f; });
  DayMetrics d = dayMetrics(buf, nbuf, D0, 10);
  TEST_ASSERT_TRUE(d.ok);
  TEST_ASSERT_EQUAL_INT(D0, d.day);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 1.0, d.swing);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 5.0, d.phMin);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 6.0, d.phMax);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 1.0, d.riseSize);
  TEST_ASSERT_FLOAT_WITHIN(0.001, -1.0, d.fallSize);
  TEST_ASSERT_TRUE(between(d.riseAt, DAY0 + 480 * 60, DAY0 + 540 * 60));
  TEST_ASSERT_TRUE(between(d.fallAt, DAY0 + 1200 * 60, DAY0 + 1260 * 60));
  TEST_ASSERT_FLOAT_WITHIN(0.001, 1.0, std::fabs(d.bigAfter - d.bigBefore));
}

void single_spike_is_smoothed_away() {
  addDay(DAY0, D0, [](int m) { return m == 600 ? 9.0f : 5.0f; });
  DayMetrics d = dayMetrics(buf, nbuf, D0, 10);
  TEST_ASSERT_TRUE(d.ok);
  TEST_ASSERT_TRUE(d.swing < 0.05f);
}

void day_under_coverage_is_skipped() {
  addDay(DAY0, D0, [](int) { return 5.0f; }, 0, 1000);  //100 of 144 readings
  TEST_ASSERT_FALSE(dayMetrics(buf, nbuf, D0, 10).ok);
}

void jitter_is_median_successive_difference() {
  addDay(DAY0, D0, [](int m) { return 5.0f + ((m / 10) % 2) * 0.02f + (m >= 720 ? 0.5f : 0.0f); });
  DayMetrics d = dayMetrics(buf, nbuf, D0, 10);
  TEST_ASSERT_TRUE(d.ok);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.02, d.jitter);
}

void change_judged_after_window_ends() {
  addDay(DAY1, D1, [](int m) { return m >= 430 ? 5.8f : 5.0f; }, 0, 541);  //newest at 09:00
  ChangeJudgement j = judgeLatestChange(buf, nbuf, LightSchedule(), 10);
  TEST_ASSERT_TRUE(j.ok);
  TEST_ASSERT_TRUE(j.lightsOn);
  TEST_ASSERT_EQUAL_INT(D1 * 2 + 1, j.key);
  TEST_ASSERT_EQUAL_INT64(DAY1 + 420 * 60, j.changeAt);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 0.8, j.jump);
  TEST_ASSERT_TRUE(between(j.jumpAt, DAY1 + 390 * 60, DAY1 + 430 * 60));
  TEST_ASSERT_FLOAT_WITHIN(0.001, 5.0, j.jumpBefore);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 5.8, j.jumpAfter);
}

void change_not_judged_before_its_window_ends() {
  addDay(DAY0, D0, [](int) { return 5.0f; });
  addDay(DAY1, D1, [](int) { return 5.0f; }, 0, 531);  //newest at 08:50
  ChangeJudgement j = judgeLatestChange(buf, nbuf, LightSchedule(), 10);
  TEST_ASSERT_TRUE(j.ok);
  TEST_ASSERT_FALSE(j.lightsOn);
  TEST_ASSERT_EQUAL_INT(D0 * 2, j.key);
  TEST_ASSERT_EQUAL_INT64(DAY0 + 1140 * 60, j.changeAt);
}

void window_under_coverage_is_skipped() {
  addDay(DAY1, D1, [](int) { return 5.0f; }, 0, 420);
  addDay(DAY1, D1, [](int) { return 5.0f; }, 500, 541);
  ChangeJudgement j = judgeLatestChange(buf, nbuf, LightSchedule(), 10);
  TEST_ASSERT_FALSE(j.ok);
  TEST_ASSERT_EQUAL_INT(D1 * 2 + 1, j.key);  //still named, so the caller marks it judged
  TEST_ASSERT_EQUAL_INT64(DAY1 + 420 * 60, j.changeAt);
}

void change_window_may_cross_midnight() {
  LightSchedule l;
  l.onMinute = 450;
  l.offMinute = 1410;  //16/8: 07:30 to 23:30
  addDay(DAY0, D0, [](int) { return 6.0f; });
  addDay(DAY1, D1, [](int m) { return m >= 20 ? 6.6f : 6.0f; }, 0, 101);  //newest at 01:40
  ChangeJudgement j = judgeLatestChange(buf, nbuf, l, 10);
  TEST_ASSERT_TRUE(j.ok);
  TEST_ASSERT_FALSE(j.lightsOn);
  TEST_ASSERT_EQUAL_INT(D0 * 2, j.key);
  TEST_ASSERT_EQUAL_INT64(DAY0 + 1410 * 60, j.changeAt);
  TEST_ASSERT_FLOAT_WITHIN(0.01, 0.6, j.jump);
}

void same_change_has_same_key() {
  addDay(DAY0, D0, [](int) { return 5.0f; }, 1, 1440);  //odd seconds don't move the change time
  for (int i = 0; i < nbuf; i++) {
    buf[i].epoch += 17;
  }
  addDay(DAY1, D1, [](int) { return 5.0f; }, 0, 541);
  ChangeJudgement a = judgeLatestChange(buf, nbuf, LightSchedule(), 10);
  addDay(DAY1, D1, [](int) { return 5.0f; }, 550, 551);
  ChangeJudgement b = judgeLatestChange(buf, nbuf, LightSchedule(), 10);
  TEST_ASSERT_TRUE(a.ok);
  TEST_ASSERT_EQUAL_INT(a.key, b.key);
  TEST_ASSERT_EQUAL_INT64(a.changeAt, b.changeAt);
  ChangeJudgement c = judgeLatestChange(buf + 1, nbuf - 1, LightSchedule(), 10);  //oldest reading dropped
  TEST_ASSERT_EQUAL_INT64(a.changeAt, c.changeAt);
}

void ec_median_covers_last_12_hours() {
  addDay(DAY0, D0, [](int) { return 5.0f; });
  addDay(DAY1, D1, [](int m) { return 5.0f + ((m / 10) % 2) * 0.03f; }, 0, 541);
  long long newest = buf[nbuf - 1].epoch;
  for (int i = 0; i < nbuf; i++) {
    buf[i].ec = buf[i].epoch >= newest - 43200 ? 300 : 200;
  }
  ChangeJudgement j = judgeLatestChange(buf, nbuf, LightSchedule(), 10);
  TEST_ASSERT_TRUE(j.ok);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 300, j.ecMedian);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.03, j.jitter);
}

size_t chars(const std::string &t) {
  size_t n = 0;
  for (char c : t) {
    n += ((unsigned char)c & 0xC0) != 0x80;
  }
  return n;
}

ChangeJudgement judged(bool lightsOn, float jump, float jitter, float ec) {
  ChangeJudgement j{};
  j.ok = true;
  j.lightsOn = lightsOn;
  j.key = D1 * 2 + lightsOn;
  j.changeAt = DAY1 + 420 * 60;
  j.jumpAt = DAY1 + 400 * 60;
  j.jump = jump;
  j.jumpBefore = 5.0f;
  j.jumpAfter = 5.0f + jump;
  j.jitter = jitter;
  j.ecMedian = ec;
  return j;
}

SwingHistory steadyHistory(int count = 5) {
  SwingHistory h;
  for (int i = 0; i < count; i++) {
    h.onJumps.add(0.3f);
    h.offJumps.add(1.0f);
    h.jitter.add(0.02f);
  }
  return h;
}

void history_median_and_cap() {
  ValueHistory<14> h;
  for (int i = 1; i <= 5; i++) {
    h.add(i);
  }
  TEST_ASSERT_FLOAT_WITHIN(0.001, 3, h.median());
  for (int i = 6; i <= 15; i++) {
    h.add(i);
  }
  TEST_ASSERT_EQUAL_INT(14, h.size());
  TEST_ASSERT_FLOAT_WITHIN(0.001, 8.5, h.median());  //2..15
  h.add(NAN);
  TEST_ASSERT_EQUAL_INT(14, h.size());
}

void history_save_load_round_trip() {
  SwingHistory h = steadyHistory();
  h.onJumps.add(0.45f);
  SwingHistory back;
  TEST_ASSERT_TRUE(back.loadText(h.saveText()));
  TEST_ASSERT_EQUAL_INT(6, back.onJumps.size());
  TEST_ASSERT_EQUAL_INT(5, back.offJumps.size());
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.3, back.onJumps.median());
  TEST_ASSERT_FLOAT_WITHIN(0.001, 1.0, back.offJumps.median());
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 0.02, back.jitter.median());
  TEST_ASSERT_EQUAL_STRING(h.saveText().c_str(), back.saveText().c_str());
  SwingHistory bad;
  TEST_ASSERT_FALSE(bad.loadText("garbage"));
  TEST_ASSERT_TRUE(bad.loadText(""));  //nothing saved yet
  TEST_ASSERT_EQUAL_INT(0, bad.onJumps.size());
}

void no_suggestion_before_five_values() {
  SwingHistory h = steadyHistory(4);
  TEST_ASSERT_FALSE(judgeSuggestion(judged(true, 2.0f, 0.5f, 281), h, SwingLimits()).found);
}

void big_jump_with_high_ec_suggests_buffer() {
  SwingSuggestion s = judgeSuggestion(judged(true, 0.6f, 0.02f, 281), steadyHistory(), SwingLimits());
  TEST_ASSERT_TRUE(s.found);
  TEST_ASSERT_EQUAL_STRING("buffer", s.kind.c_str());
  TEST_ASSERT_EQUAL_INT64(DAY1 + 400 * 60, s.at);
  TEST_ASSERT_NOT_NULL(strstr(s.note.c_str(), "lights on"));
  TEST_ASSERT_NOT_NULL(strstr(s.note.c_str(), "281"));
  TEST_ASSERT_NOT_NULL(strstr(s.note.c_str(), "add buffer or do a water change"));
}

void big_jump_with_low_ec_suggests_nutrients() {
  SwingSuggestion s = judgeSuggestion(judged(true, 0.6f, 0.02f, 248), steadyHistory(), SwingLimits());
  TEST_ASSERT_TRUE(s.found);
  TEST_ASSERT_EQUAL_STRING("nutrients", s.kind.c_str());
  TEST_ASSERT_NOT_NULL(strstr(s.note.c_str(), "add nutrients"));
}

void erratic_jitter_triggers_too() {
  SwingSuggestion s = judgeSuggestion(judged(true, 0.3f, 0.05f, 281), steadyHistory(), SwingLimits());
  TEST_ASSERT_TRUE(s.found);
  TEST_ASSERT_NOT_NULL(strstr(s.note.c_str(), "erratic"));
  TEST_ASSERT_FALSE(judgeSuggestion(judged(true, 0.3f, 0.025f, 281), steadyHistory(), SwingLimits()).found);
}

void on_and_off_jumps_have_separate_baselines() {
  SwingHistory h = steadyHistory();
  TEST_ASSERT_FALSE(judgeSuggestion(judged(false, 0.6f, 0.02f, 281), h, SwingLimits()).found);
  TEST_ASSERT_TRUE(judgeSuggestion(judged(true, 0.6f, 0.02f, 281), h, SwingLimits()).found);
  recordJudgement(h, judged(false, 0.6f, 0.02f, 281));
  TEST_ASSERT_EQUAL_INT(6, h.offJumps.size());
  TEST_ASSERT_EQUAL_INT(5, h.onJumps.size());
  TEST_ASSERT_EQUAL_INT(6, h.jitter.size());
  ChangeJudgement skipped{};
  recordJudgement(h, skipped);  //not ok: nothing added
  TEST_ASSERT_EQUAL_INT(6, h.jitter.size());
}

void notes_fit_200_characters() {
  DayMetrics m{};
  m.ok = true;
  m.swing = 9.99f;
  m.phMin = 4.0f;
  m.phMax = 13.99f;
  m.riseSize = 9.99f;
  m.fallSize = -9.99f;
  m.riseAt = m.fallAt = m.bigAt = DAY0 + 1439 * 60;
  m.riseMinute = m.fallMinute = 1439;
  m.jitter = 9.999f;
  LightSchedule l;
  l.onMinute = 719;
  l.offMinute = 721;
  std::string day = swingNote(m, l);
  TEST_ASSERT_TRUE(chars(day) <= 200);
  TEST_ASSERT_NOT_NULL(strstr(day.c_str(), "Swing 9.99"));
  TEST_ASSERT_NOT_NULL(strstr(day.c_str(), "23:59"));
  SwingHistory tiny;
  for (int i = 0; i < 5; i++) {
    tiny.onJumps.add(0.001f);
    tiny.jitter.add(0.001f);
  }
  for (float ec : {9999.0f, 1.0f, NAN}) {
    SwingSuggestion s = judgeSuggestion(judged(true, 9.99f, 9.999f, ec), tiny, SwingLimits());
    TEST_ASSERT_TRUE(s.found);
    TEST_ASSERT_TRUE(chars(s.note) <= 200);
  }
  TempWatch w;
  long long e[] = {0, 3600, 7200};
  float t[] = {999.9f, 999.9f, 999.9f};
  std::string note;
  long long at;
  TEST_ASSERT_TRUE(w.check(e, t, 3, -999.9f, 999.8f, note, at));
  TEST_ASSERT_TRUE(chars(note) <= 200);
}

void day_note_names_times_and_offsets() {
  DayMetrics m{};
  m.ok = true;
  m.swing = 1.38f;
  m.phMin = 4.96f;
  m.phMax = 6.34f;
  m.fallSize = -0.52f;
  m.fallAt = 1;
  m.fallMinute = 8 * 60 + 43;
  m.riseSize = 0.32f;
  m.riseAt = 1;
  m.riseMinute = 17 * 60 + 23;
  m.jitter = 0.014f;
  TEST_ASSERT_EQUAL_STRING(
      "Swing 1.38 (4.96-6.34). Fall -0.52 at 08:43, 1:43 after lights on. "
      "Rise +0.32 at 17:23, 1:37 before lights off. Jitter 0.014.",
      swingNote(m, LightSchedule()).c_str());
}

void swing_settings_parse_and_json() {
  LightSchedule l;
  SwingLimits lim;
  std::string error;
  TEST_ASSERT_EQUAL_STRING(",\"lights_on\":\"07:00\",\"lights_off\":\"19:00\",\"nutrients_ok_ec\":275.000,"
                           "\"swing_alert_factor\":1.500",
                           swingSettingsJsonFields(l, lim).c_str());
  TEST_ASSERT_TRUE(applySwingSettings("", "", "", "", l, lim, error));
  TEST_ASSERT_EQUAL_INT(420, l.onMinute);
  TEST_ASSERT_TRUE(applySwingSettings("06:30", "22:30", "300", "2", l, lim, error));
  TEST_ASSERT_EQUAL_INT(390, l.onMinute);
  TEST_ASSERT_EQUAL_INT(1350, l.offMinute);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 300, lim.nutrientsOkEc);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 2, lim.alertFactor);
  const char *bad[][4] = {{"24:00", "", "", ""}, {"7:5", "", "", ""}, {"", "19:60", "", ""}, {"", "", "6000", ""},
                          {"", "", "-1", ""},   {"", "", "", "1.0"}, {"", "", "", "5.5"},   {"", "", "", "x"},
                          {"08:00", "08:00", "", ""}};
  for (auto &b : bad) {
    error.clear();
    TEST_ASSERT_FALSE(applySwingSettings(b[0], b[1], b[2], b[3], l, lim, error));
    TEST_ASSERT_FALSE(error.empty());
  }
  TEST_ASSERT_FALSE(applySwingSettings("05:00", "", "", "9", l, lim, error));  //all or nothing
  TEST_ASSERT_EQUAL_INT(390, l.onMinute);
  TEST_ASSERT_EQUAL_STRING(",\"lights_on\":\"06:30\",\"lights_off\":\"22:30\",\"nutrients_ok_ec\":300.000,"
                           "\"swing_alert_factor\":2.000",
                           swingSettingsJsonFields(l, lim).c_str());
}

//Feeds readings one at a time, as the device does, and counts warnings
struct TempFeed {
  std::vector<long long> e;
  std::vector<float> t;
  TempWatch w;
  int warnings = 0;
  std::string lastNote;
  long long lastAt = 0;
  void add(int count, float temp) {
    for (int i = 0; i < count; i++) {
      e.push_back(DAY0 + (long long)e.size() * 600);
      t.push_back(temp);
      std::string note;
      long long at;
      if (w.check(e.data(), t.data(), (int)e.size(), 60, 80, note, at)) {
        warnings++;
        lastNote = note;
        lastAt = at;
      }
    }
  }
};

void temp_warns_once_and_rearms() {
  TempFeed f;
  f.add(13, 70);
  f.add(12, 82.1f);  //110 minutes above
  TEST_ASSERT_EQUAL_INT(0, f.warnings);
  f.add(1, 82.1f);   //2 h above
  TEST_ASSERT_EQUAL_INT(1, f.warnings);
  TEST_ASSERT_EQUAL_INT64(DAY0 + 13 * 600, f.lastAt);
  TEST_ASSERT_NOT_NULL(strstr(f.lastNote.c_str(), "82.1"));
  TEST_ASSERT_NOT_NULL(strstr(f.lastNote.c_str(), "above"));
  TEST_ASSERT_NOT_NULL(strstr(f.lastNote.c_str(), "80"));
  f.add(12, 82.1f);  //still above
  TEST_ASSERT_EQUAL_INT(1, f.warnings);
  f.add(5, 70);      //back inside 40 minutes, then out again: not re-armed
  f.add(13, 55);
  TEST_ASSERT_EQUAL_INT(1, f.warnings);
  f.add(7, 70);      //an hour inside re-arms
  f.add(13, 55);
  TEST_ASSERT_EQUAL_INT(2, f.warnings);
  TEST_ASSERT_NOT_NULL(strstr(f.lastNote.c_str(), "below"));
}

void temp_mixed_inside_does_not_warn() {
  TempFeed f;
  for (int i = 0; i < 20; i++) {
    f.add(1, 82);
    f.add(1, 70);
  }
  TEST_ASSERT_EQUAL_INT(0, f.warnings);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(day_swing_rise_fall_and_times);
  RUN_TEST(single_spike_is_smoothed_away);
  RUN_TEST(day_under_coverage_is_skipped);
  RUN_TEST(jitter_is_median_successive_difference);
  RUN_TEST(change_judged_after_window_ends);
  RUN_TEST(change_not_judged_before_its_window_ends);
  RUN_TEST(window_under_coverage_is_skipped);
  RUN_TEST(change_window_may_cross_midnight);
  RUN_TEST(same_change_has_same_key);
  RUN_TEST(ec_median_covers_last_12_hours);
  RUN_TEST(history_median_and_cap);
  RUN_TEST(history_save_load_round_trip);
  RUN_TEST(no_suggestion_before_five_values);
  RUN_TEST(big_jump_with_high_ec_suggests_buffer);
  RUN_TEST(big_jump_with_low_ec_suggests_nutrients);
  RUN_TEST(erratic_jitter_triggers_too);
  RUN_TEST(on_and_off_jumps_have_separate_baselines);
  RUN_TEST(notes_fit_200_characters);
  RUN_TEST(day_note_names_times_and_offsets);
  RUN_TEST(swing_settings_parse_and_json);
  RUN_TEST(temp_warns_once_and_rearms);
  RUN_TEST(temp_mixed_inside_does_not_warn);
  return UNITY_END();
}

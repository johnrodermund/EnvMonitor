#include <string>
#include <unity.h>
#include "appearance.h"

void setUp() {}
void tearDown() {}

void defaults_follow_the_system_in_green() {
  Appearance a;
  TEST_ASSERT_EQUAL_STRING(
      ",\"theme\":\"system\",\"accent\":\"green\",\"text_size\":\"normal\",\"temp_unit\":\"F\","
      "\"time_format\":\"24\",\"chart_window\":\"24\",\"tile_details\":true",
      appearanceJsonFields(a).c_str());
}

void form_applies_only_given_fields() {
  Appearance a;
  AppearanceForm f;
  f.theme = "dark";
  f.tempUnit = "C";
  f.tileDetails = "false";
  std::string error;
  TEST_ASSERT_TRUE(f.any());
  TEST_ASSERT_TRUE(applyAppearanceForm(f, a, error));
  TEST_ASSERT_EQUAL_STRING("dark", a.theme.c_str());
  TEST_ASSERT_EQUAL_STRING("C", a.tempUnit.c_str());
  TEST_ASSERT_FALSE(a.tileDetails);
  TEST_ASSERT_EQUAL_STRING("green", a.accent.c_str());
  TEST_ASSERT_FALSE(AppearanceForm().any());
}

void form_refuses_bad_values_and_changes_nothing() {
  Appearance a;
  std::string error;
  AppearanceForm f;
  f.theme = "light";
  f.accent = "pink";
  TEST_ASSERT_FALSE(applyAppearanceForm(f, a, error));
  TEST_ASSERT_EQUAL_STRING("accent must be green, blue, teal, purple or orange", error.c_str());
  TEST_ASSERT_EQUAL_STRING("system", a.theme.c_str());
  const char *bad[][2] = {{"theme", "neon"}, {"text_size", "huge"}, {"temp_unit", "K"},
                          {"time_format", "13"}, {"chart_window", "48"}, {"tile_details", "maybe"}};
  for (auto &b : bad) {
    AppearanceForm g;
    std::string field = b[0];
    if (field == "theme") g.theme = b[1];
    if (field == "text_size") g.textSize = b[1];
    if (field == "temp_unit") g.tempUnit = b[1];
    if (field == "time_format") g.timeFormat = b[1];
    if (field == "chart_window") g.chartWindow = b[1];
    if (field == "tile_details") g.tileDetails = b[1];
    TEST_ASSERT_FALSE_MESSAGE(applyAppearanceForm(g, a, error), b[0]);
  }
}

void every_allowed_value_is_accepted() {
  const char *accents[] = {"green", "blue", "teal", "purple", "orange"};
  for (const char *accent : accents) {
    Appearance a;
    AppearanceForm f;
    f.accent = accent;
    std::string error;
    TEST_ASSERT_TRUE(applyAppearanceForm(f, a, error));
  }
  Appearance a;
  AppearanceForm f;
  f.theme = "system";
  f.textSize = "xlarge";
  f.timeFormat = "12";
  f.chartWindow = "all";
  f.tileDetails = "1";
  std::string error;
  TEST_ASSERT_TRUE(applyAppearanceForm(f, a, error));
  TEST_ASSERT_TRUE(a.tileDetails);
  TEST_ASSERT_EQUAL_STRING("all", a.chartWindow.c_str());
}

void save_text_round_trips() {
  Appearance a;
  a.theme = "light";
  a.accent = "teal";
  a.textSize = "large";
  a.tempUnit = "C";
  a.timeFormat = "12";
  a.chartWindow = "6";
  a.tileDetails = false;
  Appearance b;
  TEST_ASSERT_TRUE(appearanceLoadText(appearanceSaveText(a), b));
  TEST_ASSERT_EQUAL_STRING(appearanceJsonFields(a).c_str(), appearanceJsonFields(b).c_str());
}

void load_keeps_defaults_for_missing_or_bad_entries() {
  Appearance a;
  TEST_ASSERT_TRUE(appearanceLoadText("theme=dark;accent=pink;junk;text_size=large", a));
  TEST_ASSERT_EQUAL_STRING("dark", a.theme.c_str());
  TEST_ASSERT_EQUAL_STRING("green", a.accent.c_str());
  TEST_ASSERT_EQUAL_STRING("large", a.textSize.c_str());
  Appearance b;
  TEST_ASSERT_FALSE(appearanceLoadText("", b));
  TEST_ASSERT_EQUAL_STRING("system", b.theme.c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(defaults_follow_the_system_in_green);
  RUN_TEST(form_applies_only_given_fields);
  RUN_TEST(form_refuses_bad_values_and_changes_nothing);
  RUN_TEST(every_allowed_value_is_accepted);
  RUN_TEST(save_text_round_trips);
  RUN_TEST(load_keeps_defaults_for_missing_or_bad_entries);
  return UNITY_END();
}

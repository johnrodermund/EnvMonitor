#pragma once

#include <string>
#include <utility>

#include "json_text.h"

//Look of the web app, kept on the device so every browser shows the same: saved in NVS as one
//"key=value;..." string, read and written through /api/settings. Display only: nothing here changes
//what the device measures, logs or uploads (temperatures stay deg F).

struct Appearance {
  std::string theme = "system";    //system, light or dark
  std::string accent = "green";    //green, blue, teal, purple or orange
  std::string textSize = "normal"; //normal, large or xlarge
  std::string tempUnit = "F";      //F or C
  std::string timeFormat = "24";   //24 or 12
  std::string chartWindow = "24";  //6, 24 or all: the Charts page's starting window, in hours
  bool tileDetails = true;         //show the average and 24 h trend lines on Dashboard tiles
};

//POST /api/settings fields; an empty string means "not given"
struct AppearanceForm {
  std::string theme, accent, textSize, tempUnit, timeFormat, chartWindow, tileDetails;

  bool any() const {
    return !(theme.empty() && accent.empty() && textSize.empty() && tempUnit.empty() && timeFormat.empty() &&
             chartWindow.empty() && tileDetails.empty());
  }
};

inline bool appearanceOneOf(const std::string &value, const char *const *allowed, int count) {
  for (int i = 0; i < count; i++) {
    if (value == allowed[i]) {
      return true;
    }
  }
  return false;
}

//Checks and applies one named field; false (out unchanged) when the value isn't allowed
inline bool applyAppearanceField(const std::string &name, const std::string &value, Appearance &out, std::string &error) {
  static const char *const themes[] = {"system", "light", "dark"};
  static const char *const accents[] = {"green", "blue", "teal", "purple", "orange"};
  static const char *const sizes[] = {"normal", "large", "xlarge"};
  static const char *const units[] = {"F", "C"};
  static const char *const formats[] = {"24", "12"};
  static const char *const windows[] = {"6", "24", "all"};
  if (name == "theme") {
    if (!appearanceOneOf(value, themes, 3)) return error = "theme must be system, light or dark", false;
    out.theme = value;
  } else if (name == "accent") {
    if (!appearanceOneOf(value, accents, 5)) return error = "accent must be green, blue, teal, purple or orange", false;
    out.accent = value;
  } else if (name == "text_size") {
    if (!appearanceOneOf(value, sizes, 3)) return error = "text_size must be normal, large or xlarge", false;
    out.textSize = value;
  } else if (name == "temp_unit") {
    if (!appearanceOneOf(value, units, 2)) return error = "temp_unit must be F or C", false;
    out.tempUnit = value;
  } else if (name == "time_format") {
    if (!appearanceOneOf(value, formats, 2)) return error = "time_format must be 24 or 12", false;
    out.timeFormat = value;
  } else if (name == "chart_window") {
    if (!appearanceOneOf(value, windows, 3)) return error = "chart_window must be 6, 24 or all", false;
    out.chartWindow = value;
  } else if (name == "tile_details") {
    if (value == "true" || value == "1") {
      out.tileDetails = true;
    } else if (value == "false" || value == "0") {
      out.tileDetails = false;
    } else {
      return error = "tile_details must be true or false", false;
    }
  } else {
    return error = "unknown appearance setting", false;
  }
  return true;
}

//Applies the given fields, or none of them when any is invalid
inline bool applyAppearanceForm(const AppearanceForm &f, Appearance &out, std::string &error) {
  Appearance next = out;
  const std::pair<const char *, const std::string *> fields[] = {
      {"theme", &f.theme},           {"accent", &f.accent},           {"text_size", &f.textSize},
      {"temp_unit", &f.tempUnit},    {"time_format", &f.timeFormat}, {"chart_window", &f.chartWindow},
      {"tile_details", &f.tileDetails}};
  for (const auto &field : fields) {
    if (!field.second->empty() && !applyAppearanceField(field.first, *field.second, next, error)) {
      return false;
    }
  }
  out = next;
  return true;
}

//The fields /api/settings adds, each starting with a comma
inline std::string appearanceJsonFields(const Appearance &a) {
  return ",\"theme\":" + jsonString(a.theme) + ",\"accent\":" + jsonString(a.accent) +
         ",\"text_size\":" + jsonString(a.textSize) + ",\"temp_unit\":" + jsonString(a.tempUnit) +
         ",\"time_format\":" + jsonString(a.timeFormat) + ",\"chart_window\":" + jsonString(a.chartWindow) +
         ",\"tile_details\":" + jsonBool(a.tileDetails);
}

inline std::string appearanceSaveText(const Appearance &a) {
  return "theme=" + a.theme + ";accent=" + a.accent + ";text_size=" + a.textSize + ";temp_unit=" + a.tempUnit +
         ";time_format=" + a.timeFormat + ";chart_window=" + a.chartWindow +
         ";tile_details=" + (a.tileDetails ? "true" : "false");
}

//Applies each valid saved entry; a missing or bad one keeps its default. False when the text is empty.
inline bool appearanceLoadText(const std::string &text, Appearance &out) {
  if (text.empty()) {
    return false;
  }
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find(';', start);
    if (end == std::string::npos) {
      end = text.size();
    }
    std::string entry = text.substr(start, end - start);
    size_t eq = entry.find('=');
    if (eq != std::string::npos) {
      std::string error;
      applyAppearanceField(entry.substr(0, eq), entry.substr(eq + 1), out, error);
    }
    start = end + 1;
  }
  return true;
}

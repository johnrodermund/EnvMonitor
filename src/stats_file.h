#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>
#include "ring_stats.h"

inline std::string statsNumber(float value) {
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%.9g", value);  //9 significant digits round-trip a float exactly
  return buffer;
}

//File format: id index mean varSum sVar currentValue full v0,v1,...
template <int capacity>
std::string serializeStats(const char *id, RingStats<capacity> &stats) {
  std::string text = id;
  text += " " + std::to_string(stats.index);
  text += " " + statsNumber(stats.mean);
  text += " " + statsNumber(stats.varSum);
  text += " " + statsNumber(stats.sVar);
  text += " " + statsNumber(stats.currentValue);
  text += stats.full ? " 1 " : " 0 ";
  for (int i = 0; i < stats.size(); i++) {
    if (i > 0) {
      text += ",";
    }
    text += statsNumber(stats.arr[i]);
  }
  return text;
}

template <int capacity>
bool restoreStats(const char *text, RingStats<capacity> &stats) {
  RingStats<capacity> restored;
  int full = 0;
  int consumed = 0;
  int fields = sscanf(text, "%*s %d %f %f %f %f %d %n", &restored.index, &restored.mean, &restored.varSum, &restored.sVar, &restored.currentValue, &full, &consumed);
  if (fields != 6) {
    return false;
  }
  restored.full = full;
  if (restored.index < 0 || restored.index >= capacity) {
    return false;
  }

  const char *cursor = text + consumed;
  int count = 0;
  while (count <= capacity) {
    char *end;
    float value = strtof(cursor, &end);
    if (end == cursor) {
      break;
    }
    if (count < capacity) {
      restored.arr[count] = value;
    }
    count++;
    cursor = (*end == ',') ? end + 1 : end;
  }

  //a short or overlong value list means the file was truncated or corrupted
  if (count != restored.size()) {
    return false;
  }

  stats = restored;
  return true;
}

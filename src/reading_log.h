#pragma once

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "ring_stats.h"
#include "stats_file.h"

//History of complete readings: every series advances together, so they can't drift apart.
//Store needs read(path) returning std::string and write(path, text), and write
//must replace the file in one step so a power cut leaves the last good save.
template <int capacity>
class ReadingLog {

public:

  const RingStats<capacity> &ph() const {
    return phStats;
  }
  const RingStats<capacity> &ec() const {
    return ecStats;
  }
  const RingStats<capacity> &wt() const {
    return wtStats;
  }

  int size() const {
    return phStats.size();
  }

  //Timestamp text of the i-th reading, oldest first
  const char *timestampAt(int i) const {
    return orderedTimestamp(i);
  }

  void append(const std::string &timestamp, float ph, float ec, float wt) {
    snprintf(timestamps[phStats.index], timestampSize, "%s", timestamp.c_str());
    phStats.updatedata(ph);
    ecStats.updatedata(ec);
    wtStats.updatedata(wt);
  }

  void clear() {
    phStats.resetDataset();
    ecStats.resetDataset();
    wtStats.resetDataset();
  }

  //JS array literal of [new Date(...), ph, ec, wt] rows, oldest first
  std::string chartRows() const {
    std::string rows = "[";
    for (int i = 0; i < size(); i++) {
      char values[64];
      snprintf(values, sizeof(values), "),%.2f,%.2f,%.2f],\n", phStats.getOrderedValue(i), ecStats.getOrderedValue(i), wtStats.getOrderedValue(i));
      rows += "[new Date(";
      rows += orderedTimestamp(i);
      rows += values;
    }
    return rows + "]";
  }

  template <typename Store>
  void save(Store &store) const {
    std::string text = fileHeader;
    for (int i = 0; i < size(); i++) {
      const char *timestamp = orderedTimestamp(i);
      text += *timestamp ? timestamp : noTimestamp;
      text += " " + statsNumber(phStats.getOrderedValue(i));
      text += " " + statsNumber(ecStats.getOrderedValue(i));
      text += " " + statsNumber(wtStats.getOrderedValue(i));
      text += "\n";
    }
    store.write(readingsPath, text);
  }

  template <typename Store>
  void restore(Store &store) {
    std::string text = store.read(readingsPath);
    if (text.compare(0, strlen(fileHeader), fileHeader) == 0) {
      restoreRows(text.c_str() + strlen(fileHeader));
    } else {
      restoreOldFormat(store);
    }
  }

private:

  static const int timestampSize = 24;
  static constexpr const char *fileHeader = "readings v1\n";
  static constexpr const char *noTimestamp = "-";
  static constexpr const char *readingsPath = "/readings.txt";

  RingStats<capacity> phStats;
  RingStats<capacity> ecStats;
  RingStats<capacity> wtStats;
  char timestamps[capacity][timestampSize] = {};

  const char *orderedTimestamp(int count) const {
    return timestamps[phStats.full ? (phStats.index + count) % capacity : count];
  }

  //Replaying rows rebuilds statistics exactly, an unterminated last row is dropped
  void restoreRows(const char *cursor) {
    const char *lineEnd;
    while ((lineEnd = strchr(cursor, '\n')) != nullptr) {
      char timestamp[timestampSize];
      float ph, ec, wt;
      if (sscanf(cursor, "%23s %f %f %f", timestamp, &ph, &ec, &wt) != 4) {
        return;
      }
      append(strcmp(timestamp, noTimestamp) == 0 ? "" : timestamp, ph, ec, wt);
      cursor = lineEnd + 1;
    }
  }

  //Files written before /readings.txt: one per series, plus timestamps in ring order
  template <typename Store>
  void restoreOldFormat(Store &store) {
    RingStats<capacity> oldPh, oldEc, oldWt;
    restoreStats(store.read("/phdata.txt").c_str(), oldPh);
    restoreStats(store.read("/ecdata.txt").c_str(), oldEc);
    restoreStats(store.read("/wtdata.txt").c_str(), oldWt);
    std::vector<std::string> oldTimestamps = splitOldTimestamps(store.read("/datetime.txt"));

    //An interrupted cycle could leave ec and wt ahead of ph, newest entries line up
    int count = std::min(oldPh.size(), std::min(oldEc.size(), oldWt.size()));
    for (int i = 0; i < count; i++) {
      int phOffset = oldPh.size() - count + i;
      size_t slot = oldPh.full ? (oldPh.index + phOffset) % capacity : phOffset;
      append(slot < oldTimestamps.size() ? oldTimestamps[slot] : "",
             oldPh.getOrderedValue(phOffset),
             oldEc.getOrderedValue(oldEc.size() - count + i),
             oldWt.getOrderedValue(oldWt.size() - count + i));
    }
  }

  static std::vector<std::string> splitOldTimestamps(const std::string &text) {
    std::vector<std::string> result;
    size_t start = 0;
    size_t end;
    while ((end = text.find('|', start)) != std::string::npos && result.size() < capacity) {
      std::string entry = text.substr(start, end - start);
      size_t space = entry.find(' ');
      result.push_back(entry.substr(0, space));
      start = end + 1;
    }
    return result;
  }
};

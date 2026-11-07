#pragma once

#include <cmath>
#include <cstdio>
#include <string>

#include "json_text.h"

//A finished reading waiting to be sent to the readings API. NaN means the sensor gave no value.
struct QueuedReading {
  long long epoch = 0;  //UTC seconds
  float ph = NAN;
  float phUncertainty = NAN;
  float phAdc = NAN;
  float ec = NAN;
  float ecAdc = NAN;
  float wt = NAN;
};

//Readings waiting to be sent, oldest first. When full, the oldest reading is dropped so the
//newest data survives a long API outage. Not thread-safe: callers lock around it.
template <int capacity>
class ReadingQueue {

public:

  void push(const QueuedReading &reading) {
    if (count == capacity) {
      pop();
      droppedCount++;
    }
    items[(head + count) % capacity] = reading;
    count++;
  }

  const QueuedReading &front() const {
    return items[head];
  }

  void pop() {
    head = (head + 1) % capacity;
    count--;
  }

  int size() const {
    return count;
  }

  int dropped() const {
    return droppedCount;
  }

private:

  QueuedReading items[capacity];
  int head = 0;
  int count = 0;
  int droppedCount = 0;
};

//Before NTP sync the clock counts from boot, which would store readings in 1970
inline bool clockIsSet(long long epoch) {
  return epoch >= 1700000000;
}

//UTC text the API accepts, e.g. 2026-11-04T23:30:00Z
inline std::string utcIso(long long epoch) {
  long long days = epoch / 86400;
  long long seconds = epoch % 86400;
  //Civil date from days since 1970-01-01 (Howard Hinnant's days_from_civil, inverted)
  days += 719468;
  long long era = days / 146097;
  long long dayOfEra = days - era * 146097;
  long long yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
  long long dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
  long long monthIndex = (5 * dayOfYear + 2) / 153;
  int day = dayOfYear - (153 * monthIndex + 2) / 5 + 1;
  int month = monthIndex < 10 ? monthIndex + 3 : monthIndex - 9;
  long long year = yearOfEra + era * 400 + (month <= 2);
  char text[24];
  snprintf(text, sizeof(text), "%04lld-%02d-%02dT%02lld:%02lld:%02lldZ", year, month, day,
           seconds / 3600, (seconds % 3600) / 60, seconds % 60);
  return text;
}

//Body for POST /readings on the readings API
inline std::string readingJson(const char *device, const QueuedReading &reading) {
  return "{\"device\":" + jsonString(device) +
         ",\"measured_at\":\"" + utcIso(reading.epoch) + "\"" +
         ",\"ph\":" + jsonNumber(reading.ph) +
         ",\"ph_uncertainty\":" + jsonNumber(reading.phUncertainty) +
         ",\"ph_adc\":" + jsonNumber(reading.phAdc) +
         ",\"ec\":" + jsonNumber(reading.ec) +
         ",\"ec_adc\":" + jsonNumber(reading.ecAdc) +
         ",\"water_temp_f\":" + jsonNumber(reading.wt) + "}";
}

enum class SendOutcome { Delivered, Rejected, RetryLater };

//200 means the API already had this reading. 400 and 422 mean it never will accept it.
//Anything else, including a wrong token or no connection, keeps the reading for a retry.
inline SendOutcome outcomeFor(int httpCode) {
  if (httpCode == 200 || httpCode == 201) {
    return SendOutcome::Delivered;
  }
  if (httpCode == 400 || httpCode == 422) {
    return SendOutcome::Rejected;
  }
  return SendOutcome::RetryLater;
}

//Wait before the next attempt after this many failures in a row: 10 s, doubling, capped at 5 min
inline unsigned long retryDelayMs(int failures) {
  unsigned long delay = 10000;
  for (int i = 1; i < failures && delay < 300000; i++) {
    delay *= 2;
  }
  return delay < 300000 ? delay : 300000;
}

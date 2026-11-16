#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>

#include "event_log.h"
#include "json_text.h"
#include "reading_queue.h"

//POST /api/events and the detection settings: form fields in, HTTP code and JSON out. The sketch
//calls these under eventsLock (events) or settingsLock (limits), so they only touch RAM.

//"YYYY-MM-DDTHH:MM[:SS]" followed by "Z" or "+HH:MM"/"-HH:MM", to UTC seconds. A time with no zone is refused.
inline bool parseIsoTime(const std::string &text, long long &epoch) {
  int year, month, day, hour, minute, second = 0, used = 0;
  const char *s = text.c_str();
  if (sscanf(s, "%4d-%2d-%2dT%2d:%2d%n", &year, &month, &day, &hour, &minute, &used) != 5) {
    return false;
  }
  s += used;
  if (*s == ':') {
    if (sscanf(s, ":%2d%n", &second, &used) != 1) {
      return false;
    }
    s += used;
  }
  long long offset = 0;
  if (*s == 'Z') {
    s++;
  } else if (*s == '+' || *s == '-') {
    int offHour, offMinute;
    if (sscanf(s + 1, "%2d:%2d%n", &offHour, &offMinute, &used) != 2 || offHour > 23 || offMinute > 59) {
      return false;
    }
    offset = (offHour * 3600LL + offMinute * 60) * (*s == '-' ? -1 : 1);
    s += 1 + used;
  } else {
    return false;
  }
  if (*s || month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60) {
    return false;
  }
  //Days since 1970-01-01 (Howard Hinnant's days_from_civil)
  long long y = year - (month <= 2);
  long long era = (y >= 0 ? y : y - 399) / 400;
  long long yearOfEra = y - era * 400;
  long long dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  long long dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  long long days = era * 146097 + dayOfEra - 719468;
  epoch = days * 86400 + hour * 3600LL + minute * 60 + second - offset;
  return true;
}

//The form of POST /api/events. has* is true when the field was in the body, even if empty.
struct EventForm {
  std::string op, seq, kind, detail, occurredAt, note, status;
  bool hasKind = false, hasDetail = false, hasOccurredAt = false, hasNote = false, hasStatus = false;
};

struct EventApiResult {
  int httpCode;
  std::string json;
  bool changed;  //the log changed, so it needs saving
};

inline EventApiResult eventError(int code, const std::string &message) {
  return {code, "{\"error\":" + jsonString(message) + "}", false};
}

inline EventApiResult eventSaved(unsigned long seq) {
  return {200, "{\"seq\":" + std::to_string(seq) + "}", true};
}

//op is create, update, delete, reject or confirm. Times are ISO with a zone; create defaults to now.
template <int capacity>
EventApiResult applyEventForm(EventLog<capacity> &log, const EventForm &f, long long now) {
  if (f.op != "create" && f.op != "update" && f.op != "delete" && f.op != "reject" && f.op != "confirm") {
    return eventError(400, "op must be create, update, delete, reject or confirm");
  }
  if (!clockIsSet(now)) {
    return eventError(503, "clock not set yet");
  }
  std::string error;
  if (!validEventFields(f.kind, f.detail, f.status, f.note, error)) {
    return eventError(400, error);
  }
  long long occurredAt = now;
  if (!f.occurredAt.empty()) {
    if (!parseIsoTime(f.occurredAt, occurredAt) || !clockIsSet(occurredAt)) {
      return eventError(400, "bad occurred_at");
    }
    if (occurredAt - now > EVENT_MAX_AHEAD_S) {
      return eventError(400, "occurred_at is in the future");
    }
  }

  if (f.op == "create") {
    if (f.kind.empty()) {
      return eventError(400, "kind is required");
    }
    return eventSaved(log.addManual(f.kind, f.detail, occurredAt, f.note, now));
  }

  char *end;
  unsigned long seq = strtoul(f.seq.c_str(), &end, 10);
  if (f.seq.empty() || *end || seq == 0) {
    return eventError(400, "bad seq");
  }
  const TankEvent *e = log.find(seq);
  if (!e || e->deleted) {
    return eventError(404, "no such event");
  }
  if (f.op == "delete") {
    if (e->source != "manual") {
      return eventError(409, "detected events can be rejected, not deleted");
    }
    log.remove(seq, now);
    return eventSaved(seq);
  }

  EventEdit edit;
  edit.hasKind = f.hasKind && !f.kind.empty();
  edit.kind = f.kind;
  edit.hasDetail = f.hasDetail;
  edit.detail = f.detail;
  edit.hasNote = f.hasNote;
  edit.note = f.note;
  edit.hasOccurredAt = f.hasOccurredAt && !f.occurredAt.empty();
  edit.occurredAt = occurredAt;
  if (f.op == "update") {
    edit.hasStatus = f.hasStatus && !f.status.empty();
    edit.status = f.status;
  } else {
    edit.hasStatus = true;
    edit.status = f.op == "reject" ? "rejected" : "confirmed";
  }
  log.update(seq, edit, now);
  return eventSaved(seq);
}

inline bool parseLimit(const std::string &text, const char *name, float &out, std::string &error) {
  char *end;
  float value = strtof(text.c_str(), &end);
  if (*end || !(value >= 0.5f && value <= 100)) {
    error = std::string(name) + " must be a number from 0.5 to 100";
    return false;
  }
  out = value;
  return true;
}

//Applies the given detection settings (empty means "not given") to limits, or none of them on an error
inline bool applyDetectSettings(const std::string &topupMinDrop, const std::string &feedingMinRise,
                                const std::string &feedingEnabled, DetectLimits &limits, std::string &error) {
  DetectLimits next = limits;
  if (!topupMinDrop.empty() && !parseLimit(topupMinDrop, "topup_min_drop", next.topupMinDrop, error)) {
    return false;
  }
  if (!feedingMinRise.empty() && !parseLimit(feedingMinRise, "feeding_min_rise", next.feedingMinRise, error)) {
    return false;
  }
  if (!feedingEnabled.empty()) {
    if (feedingEnabled == "1" || feedingEnabled == "true") {
      next.feedingEnabled = true;
    } else if (feedingEnabled == "0" || feedingEnabled == "false") {
      next.feedingEnabled = false;
    } else {
      error = "feeding_enabled must be true or false";
      return false;
    }
  }
  limits = next;
  return true;
}

#pragma once

#include <string>

#include "event_log.h"
#include "json_text.h"
#include "pi_cache.h"
#include "reading_queue.h"

//PUT <url>/events/<uid> on the readings API: one event per request, the newest updated_at wins there.

//"<device>:<seq>". The Pi takes only [A-Za-z0-9._:-] in a uid, so any other byte of the device name becomes '-'.
inline std::string eventUid(const char *device, unsigned long seq) {
  std::string uid;
  for (const char *c = device; *c; c++) {
    bool plain = (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') ||
                 *c == '-' || *c == '_' || *c == '.';
    uid += plain ? *c : '-';
  }
  return uid + ":" + std::to_string(seq);
}

inline std::string eventSyncPath(const char *device, const TankEvent &e) {
  return "/events/" + urlEncode(eventUid(device, e.seq));
}

inline std::string optionalTime(long long epoch) {
  return epoch ? "\"" + utcIso(epoch) + "\"" : std::string("null");
}

//Every field PUT /events/{uid} takes. deleted is sent only for a tombstone.
inline std::string eventSyncJson(const char *device, const TankEvent &e) {
  return "{\"device\":" + jsonString(device) + ",\"occurred_at\":\"" + utcIso(e.occurredAt) +
         "\",\"origin_at\":" + optionalTime(e.originAt) + ",\"kind\":" + jsonString(e.kind) +
         ",\"detail\":" + (e.detail.empty() ? std::string("null") : jsonString(e.detail)) +
         ",\"source\":" + jsonString(e.source) + ",\"status\":" + jsonString(e.status) +
         ",\"ec_before\":" + jsonNumber(e.ecBefore) + ",\"ec_after\":" + jsonNumber(e.ecAfter) +
         ",\"ph_before\":" + jsonNumber(e.phBefore) + ",\"ph_after\":" + jsonNumber(e.phAfter) +
         ",\"note\":" + jsonString(e.note) + ",\"updated_at\":\"" + utcIso(e.updatedAt) + "\"" +
         (e.deleted ? ",\"deleted\":true" : "") + "}";
}

//200 and 201 mean stored (or already newer there). 400 and 422 mean the Pi will never take it.
inline SendOutcome eventSyncOutcome(int httpCode) {
  return outcomeFor(httpCode);
}

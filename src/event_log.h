#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "event_detect.h"
#include "json_text.h"
#include "reading_queue.h"

//Tank events kept on the device: detected top-ups and feedings, and events the owner logs.
//Every change marks an event dirty until it has synced to the readings API's PUT /events/<uid>.
//Not thread-safe: the sketch guards it with eventsLock.

struct TankEvent {
  unsigned long seq = 0;
  long long occurredAt = 0;  //UTC seconds
  long long originAt = 0;    //when a detected event was first found, 0 for manual events
  long long updatedAt = 0;
  std::string kind, detail, source, status, note;
  float ecBefore = NAN, ecAfter = NAN, phBefore = NAN, phAfter = NAN;
  float size = 0, meanSize = 0;  //detection strength, compared when a stronger candidate comes along
  bool edited = false;           //the owner changed it, so the detector never replaces it
  bool deleted = false;          //a tombstone, kept until its delete has synced
  unsigned dirtyGen = 0;         //counts changes
  unsigned syncedGen = 0;        //dirtyGen as of the last delivered sync

  bool dirty() const {
    return dirtyGen != syncedGen;
  }
};

//An edit from POST /api/events: only fields marked has* change
struct EventEdit {
  bool hasKind = false, hasDetail = false, hasStatus = false, hasNote = false, hasOccurredAt = false;
  std::string kind, detail, status, note;
  long long occurredAt = 0;
};

const long long EVENT_NEAR_S = 2 * 3600;       //a candidate this close to an event of its kind is the same event
const long long EVENT_MAX_AHEAD_S = 86400;     //an updated_at further ahead than this came from a wrong clock
const size_t EVENT_NOTE_MAX_CHARS = 200;

inline bool validEventKind(const std::string &kind) {
  return kind == "topup" || kind == "feeding" || kind == "buffer" || kind == "water_change" || kind == "other";
}

//Empty strings mean "not given". Notes are counted in characters, not UTF-8 bytes, as the Pi counts them.
inline bool validEventFields(const std::string &kind, const std::string &detail, const std::string &status,
                             const std::string &note, std::string &error) {
  if (!kind.empty() && !validEventKind(kind)) {
    error = "bad kind";
    return false;
  }
  if (!detail.empty() && detail != "di" && detail != "tap" && detail != "unknown") {
    error = "bad detail";
    return false;
  }
  if (!status.empty() && status != "suggested" && status != "confirmed" && status != "rejected") {
    error = "bad status";
    return false;
  }
  size_t chars = 0;
  for (char c : note) {
    unsigned char byte = (unsigned char)c;
    if (byte < 0x20 && c != '\n' && c != '\r' && c != '\t') {
      error = "bad character in note";
      return false;
    }
    if ((byte & 0xC0) != 0x80) {
      chars++;
    }
  }
  if (chars > EVENT_NOTE_MAX_CHARS) {
    error = "note is over 200 characters";
    return false;
  }
  return true;
}

template <int capacity>
class EventLog {

public:

  int size() const {
    return count;
  }

  const TankEvent *find(unsigned long seq) const {
    int i = indexOf(seq);
    return i < 0 ? nullptr : &items[i];
  }

  //Applies the detection rule's 2-hour rules. Returns true when it added or replaced an event.
  bool offerDetected(const Candidate &c, long long now) {
    if (!c.found || !clockIsSet(now)) {
      return false;
    }
    const char *kind = c.topup ? "topup" : "feeding";
    int nearest = -1;
    for (int i = 0; i < count; i++) {
      const TankEvent &e = items[i];
      if (e.deleted || e.kind != kind) {
        continue;
      }
      bool near = llabs(c.occurredAt - e.occurredAt) <= EVENT_NEAR_S ||
                  (e.originAt != 0 && llabs(c.occurredAt - e.originAt) <= EVENT_NEAR_S);
      if (!near) {
        continue;
      }
      if (!replaceable(e) || !stronger(c, e)) {
        return false;  //the owner's event, a rejection or a stronger suggestion already covers it
      }
      if (nearest < 0 || llabs(c.occurredAt - e.occurredAt) < llabs(c.occurredAt - items[nearest].occurredAt)) {
        nearest = i;
      }
    }
    TankEvent *e;
    if (nearest >= 0) {
      e = &items[nearest];  //same seq, so the Pi gets the stronger version under the same uid
    } else {
      e = &addSlot();
      e->kind = kind;
      e->detail = c.topup ? "unknown" : "";
      e->source = "detected";
      e->status = "suggested";
    }
    e->occurredAt = c.occurredAt;
    e->originAt = c.occurredAt;
    e->ecBefore = c.ecBefore;
    e->ecAfter = c.ecAfter;
    e->phBefore = c.phBefore;
    e->phAfter = c.phAfter;
    e->size = c.size;
    e->meanSize = c.meanSize;
    touch(*e, now);
    return true;
  }

  //Returns the new event's seq, or 0 when the clock isn't set. Fields must already be valid.
  unsigned long addManual(const std::string &kind, const std::string &detail, long long occurredAt,
                          const std::string &note, long long now) {
    if (!clockIsSet(now)) {
      return 0;
    }
    TankEvent &e = addSlot();
    e.kind = kind;
    e.detail = detailFor(kind, detail);
    e.source = "manual";
    e.status = "confirmed";
    e.occurredAt = occurredAt;
    e.note = note;
    touch(e, now);
    return e.seq;
  }

  //Editing a detected event confirms it, unless the edit sets the status itself
  bool update(unsigned long seq, const EventEdit &edit, long long now) {
    int i = indexOf(seq);
    if (i < 0 || items[i].deleted || !clockIsSet(now)) {
      return false;
    }
    TankEvent &e = items[i];
    if (edit.hasKind) {
      e.kind = edit.kind;
    }
    if (edit.hasDetail || edit.hasKind) {
      e.detail = detailFor(e.kind, edit.hasDetail ? edit.detail : e.detail);
    }
    if (edit.hasNote) {
      e.note = edit.note;
    }
    if (edit.hasOccurredAt && edit.occurredAt != e.occurredAt) {
      e.occurredAt = edit.occurredAt;
      e.ecBefore = e.ecAfter = e.phBefore = e.phAfter = NAN;  //refilled around the new time
    }
    if (edit.hasStatus) {
      e.status = edit.status;
    } else if (e.source == "detected") {
      e.status = "confirmed";
    }
    if (e.source == "detected") {
      e.edited = true;
    }
    touch(e, now);
    return true;
  }

  //Deletes a manual event, keeping a tombstone until the delete syncs. Detected events can only be rejected.
  bool remove(unsigned long seq, long long now) {
    int i = indexOf(seq);
    if (i < 0 || items[i].deleted || items[i].source != "manual" || !clockIsSet(now)) {
      return false;
    }
    items[i].deleted = true;
    touch(items[i], now);
    return true;
  }

  //Fills before/after medians for events that lack them, once 6 readings on each side exist.
  //recent holds readings oldest first. Returns true when it changed anything.
  bool fillDeltas(const DetectSample *recent, int sampleCount, long long now) {
    if (!clockIsSet(now)) {
      return false;
    }
    bool changed = false;
    for (int i = 0; i < count; i++) {
      TankEvent &e = items[i];
      if (e.deleted || !std::isnan(e.ecBefore)) {
        continue;
      }
      int split = 0;
      while (split < sampleCount && recent[split].epoch < e.occurredAt) {
        split++;
      }
      if (split < DETECT_SIDE || split + DETECT_SIDE > sampleCount) {
        continue;
      }
      Candidate medians;
      sideMedians(recent + split - DETECT_SIDE, recent + split, medians);
      if (std::isnan(medians.ecBefore)) {
        continue;
      }
      e.ecBefore = medians.ecBefore;
      e.ecAfter = medians.ecAfter;
      e.phBefore = medians.phBefore;
      e.phAfter = medians.phAfter;
      touch(e, now);
      changed = true;
    }
    return changed;
  }

  //The dirty event to sync next (lowest seq), skipping any stamped more than a day ahead of now
  const TankEvent *nextDirty(long long now) const {
    const TankEvent *next = nullptr;
    for (int i = 0; i < count; i++) {
      const TankEvent &e = items[i];
      if (e.dirty() && e.updatedAt - now <= EVENT_MAX_AHEAD_S && (!next || e.seq < next->seq)) {
        next = &e;
      }
    }
    return next;
  }

  //Dirty events nextDirty() skips because their updated_at is too far ahead
  int futureCount(long long now) const {
    int future = 0;
    for (int i = 0; i < count; i++) {
      if (items[i].dirty() && items[i].updatedAt - now > EVENT_MAX_AHEAD_S) {
        future++;
      }
    }
    return future;
  }

  //gen is the event's dirtyGen when its sync was built. A change since then keeps it dirty.
  void markSynced(unsigned long seq, unsigned gen) {
    int i = indexOf(seq);
    if (i < 0) {
      return;
    }
    items[i].syncedGen = gen;
    if (items[i].deleted && !items[i].dirty()) {
      removeAt(i);
    }
  }

  int dirtyCount() const {
    int dirty = 0;
    for (int i = 0; i < count; i++) {
      dirty += items[i].dirty();
    }
    return dirty;
  }

  unsigned droppedUnsynced() const {
    return dropped;
  }

  static const char *path() {
    return "/events.txt";
  }

  //"events v1 <nextSeq>", then one line per event with the note last, escaped so it holds no space or newline
  std::string saveText() const {
    std::string text = "events v1 " + std::to_string(nextSeq) + "\n";
    char line[256];
    for (int i = 0; i < count; i++) {
      const TankEvent &e = items[i];
      snprintf(line, sizeof(line), "%lu %lld %lld %lld %s %s %s %s ", e.seq, e.occurredAt, e.originAt, e.updatedAt,
               e.kind.c_str(), e.detail.empty() ? "-" : e.detail.c_str(), e.source.c_str(), e.status.c_str());
      text += line;
      text += floatToken(e.ecBefore) + " " + floatToken(e.ecAfter) + " " + floatToken(e.phBefore) + " " +
              floatToken(e.phAfter) + " " + floatToken(e.size) + " " + floatToken(e.meanSize);
      text += e.edited ? " 1" : " 0";
      text += e.deleted ? " 1" : " 0";
      text += e.dirty() ? " 1" : " 0";
      text += " =" + escapeNote(e.note) + "\n";
    }
    return text;
  }

  //Replaces the log with saved text. Returns false, changing nothing, when the header is wrong.
  //A line that doesn't parse is skipped.
  bool loadText(const std::string &text) {
    unsigned long savedNextSeq = 0;
    if (sscanf(text.c_str(), "events v1 %lu", &savedNextSeq) != 1) {
      return false;
    }
    count = 0;
    nextSeq = savedNextSeq > 0 ? savedNextSeq : 1;
    size_t start = text.find('\n');
    while (start != std::string::npos && start + 1 < text.size()) {
      size_t end = text.find('\n', start + 1);
      if (end == std::string::npos) {
        break;  //an unterminated last line is a cut-off write
      }
      TankEvent e;
      if (count < capacity && parseLine(text.substr(start + 1, end - start - 1), e)) {
        items[count++] = e;
        if (e.seq >= nextSeq) {
          nextSeq = e.seq + 1;
        }
      }
      start = end;
    }
    return true;
  }

  //{"events":[...]} newest first, without tombstones. backed_up is false while an event is dirty.
  std::string listJson() const {
    int order[capacity];
    int shown = 0;
    for (int i = 0; i < count; i++) {
      if (!items[i].deleted) {
        order[shown++] = i;
      }
    }
    //Insertion sort, newest first; equal times keep the higher seq first
    for (int i = 1; i < shown; i++) {
      int value = order[i];
      int j = i - 1;
      while (j >= 0 && newer(items[value], items[order[j]])) {
        order[j + 1] = order[j];
        j--;
      }
      order[j + 1] = value;
    }
    std::string json = "{\"events\":[";
    for (int k = 0; k < shown; k++) {
      const TankEvent &e = items[order[k]];
      if (k) {
        json += ",";
      }
      json += "{\"seq\":" + std::to_string(e.seq) + ",\"occurred_at\":\"" + utcIso(e.occurredAt) +
              "\",\"kind\":" + jsonString(e.kind) +
              ",\"detail\":" + (e.detail.empty() ? std::string("null") : jsonString(e.detail)) +
              ",\"source\":" + jsonString(e.source) + ",\"status\":" + jsonString(e.status) +
              ",\"ec_before\":" + jsonNumber(e.ecBefore) + ",\"ec_after\":" + jsonNumber(e.ecAfter) +
              ",\"ph_before\":" + jsonNumber(e.phBefore) + ",\"ph_after\":" + jsonNumber(e.phAfter) +
              ",\"note\":" + jsonString(e.note) + ",\"backed_up\":" + jsonBool(!e.dirty()) + "}";
    }
    return json + "]}";
  }

private:

  TankEvent items[capacity];
  int count = 0;
  unsigned long nextSeq = 1;
  unsigned dropped = 0;

  int indexOf(unsigned long seq) const {
    for (int i = 0; i < count; i++) {
      if (items[i].seq == seq) {
        return i;
      }
    }
    return -1;
  }

  static bool replaceable(const TankEvent &e) {
    return e.source == "detected" && e.status == "suggested" && !e.edited;
  }

  //Larger median change wins; an exact tie goes to the larger mean change
  static bool stronger(const Candidate &c, const TankEvent &e) {
    return c.size > e.size || (c.size == e.size && c.meanSize > e.meanSize);
  }

  static bool newer(const TankEvent &a, const TankEvent &b) {
    return a.occurredAt > b.occurredAt || (a.occurredAt == b.occurredAt && a.seq > b.seq);
  }

  static std::string detailFor(const std::string &kind, const std::string &detail) {
    if (kind != "topup") {
      return "";
    }
    return detail.empty() ? "unknown" : detail;
  }

  static void touch(TankEvent &e, long long now) {
    if (now > e.updatedAt) {
      e.updatedAt = now;  //never backwards: the Pi keeps the newest updated_at
    }
    e.dirtyGen++;
  }

  //A fresh event in a free slot. When full, the oldest synced event goes first; only when every
  //event is unsynced does the oldest one go, and that loss is counted.
  TankEvent &addSlot() {
    if (count == capacity) {
      int victim = -1;
      for (int i = 0; i < count; i++) {
        if (!items[i].dirty() && (victim < 0 || older(items[i], items[victim]))) {
          victim = i;
        }
      }
      if (victim < 0) {
        for (int i = 0; i < count; i++) {
          if (victim < 0 || older(items[i], items[victim])) {
            victim = i;
          }
        }
        dropped++;
      }
      removeAt(victim);
    }
    TankEvent &e = items[count++];
    e = TankEvent();
    e.seq = nextSeq++;
    return e;
  }

  static bool older(const TankEvent &a, const TankEvent &b) {
    return a.occurredAt < b.occurredAt || (a.occurredAt == b.occurredAt && a.seq < b.seq);
  }

  void removeAt(int i) {
    for (int j = i; j < count - 1; j++) {
      items[j] = items[j + 1];
    }
    count--;
  }

  static std::string floatToken(float value) {
    if (std::isnan(value)) {
      return "-";
    }
    char text[24];
    snprintf(text, sizeof(text), "%.9g", value);
    return text;
  }

  static bool parseFloatToken(const std::string &token, float &value) {
    if (token == "-") {
      value = NAN;
      return true;
    }
    char *end;
    value = strtof(token.c_str(), &end);
    return !token.empty() && *end == '\0';
  }

  static std::string escapeNote(const std::string &note) {
    std::string out;
    for (char c : note) {
      switch (c) {
        case '\\': out += "\\\\"; break;
        case ' ': out += "\\s"; break;
        case '|': out += "\\p"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c;
      }
    }
    return out;
  }

  static std::string unescapeNote(const std::string &text) {
    std::string out;
    for (size_t i = 0; i < text.size(); i++) {
      if (text[i] != '\\' || i + 1 == text.size()) {
        out += text[i];
        continue;
      }
      switch (text[++i]) {
        case 's': out += ' '; break;
        case 'p': out += '|'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        default: out += text[i];
      }
    }
    return out;
  }

  static bool parseLine(const std::string &line, TankEvent &e) {
    const int fieldCount = 18;
    std::string tokens[fieldCount];
    size_t start = 0;
    for (int f = 0; f < fieldCount; f++) {
      size_t end = f == fieldCount - 1 ? line.size() : line.find(' ', start);
      if (end == std::string::npos || start > line.size()) {
        return false;
      }
      tokens[f] = line.substr(start, end - start);
      start = end + 1;
    }
    char *end;
    e.seq = strtoul(tokens[0].c_str(), &end, 10);
    if (*end || e.seq == 0) {
      return false;
    }
    e.occurredAt = strtoll(tokens[1].c_str(), &end, 10);
    e.originAt = strtoll(tokens[2].c_str(), &end, 10);
    e.updatedAt = strtoll(tokens[3].c_str(), &end, 10);
    e.kind = tokens[4];
    e.detail = tokens[5] == "-" ? "" : tokens[5];
    e.source = tokens[6];
    e.status = tokens[7];
    std::string error;
    if (!validEventKind(e.kind) || !validEventFields(e.kind, e.detail, e.status, "", error) ||
        (e.source != "detected" && e.source != "manual")) {
      return false;
    }
    float *floats[] = {&e.ecBefore, &e.ecAfter, &e.phBefore, &e.phAfter, &e.size, &e.meanSize};
    for (int f = 0; f < 6; f++) {
      if (!parseFloatToken(tokens[8 + f], *floats[f])) {
        return false;
      }
    }
    e.edited = tokens[14] == "1";
    e.deleted = tokens[15] == "1";
    e.dirtyGen = tokens[16] == "1" ? 1 : 0;
    e.syncedGen = 0;
    if (tokens[17].empty() || tokens[17][0] != '=') {
      return false;
    }
    e.note = unescapeNote(tokens[17].substr(1));
    return true;
  }
};

# Tank Events (device-first) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The device detects top-ups and feedings, keeps its recent events, lets the owner log and correct them in the web app with no Pi needed, and syncs every event to the Pi's long-term store.

**Architecture:** Three Arduino-free, host-tested headers: `event_detect.h` (the rule), `event_log.h` (a 100-event store with dirty flags, tombstones and save/load text) and `event_sync.h` (PUT body and outcome). The sketch runs detection after each recorded reading, changes the log in RAM under `eventsLock` from handlers, and saves and syncs from `loop()` as slow steps. The web app reads and writes `/api/events` on the device.

**Tech Stack:** ESP32-C3 Arduino (core 2.0.14), FreeRTOS, LittleFS, NVS `Preferences`, PlatformIO Unity host tests, Preact + uPlot + Vitest.

**Spec:** `C:\Users\doubt\ClaudeProjects\PiServer\docs\superpowers\specs\2026-11-15-tank-events-design.md` (device-first revision). The Pi API is built by `PiServer/docs/superpowers/plans/2026-11-15-tank-events-pi.md`, Task 2.

## Gate

Branch from `ph-hum-sampling` (commit `821dc93`, the pH sampling fix), or from main once that's merged, and only after the owner has run that build on the device and accepted it. Tasks 1–3 are Arduino-free and can start before that if needed. Task 4 onward touch the sketch.

## Global Constraints

- Follow `CLAUDE.md`: take locks with `portMAX_DELAY`, hold them only to copy data, never do I/O under a lock, and run at most one slow step per `loop()` pass. Keep logic in `src/` with host tests.
- Slow-step order in `loop()`: readings upload, then event save, then event sync, then pending settings write, then one Pi fetch.
- Event store: 100 events. `uid` is `<device>:<seq>`, and `seq` is never reused (saved with the events).
- Detection defaults, stored in NVS namespace `events`: `topup_min_drop` 3.0, `feeding_min_rise` 5.0, `feeding_enabled` false.
- Detected top-ups get `detail = "unknown"` and `status = "suggested"`. Manual events start `confirmed`. Detected events can be rejected, never deleted.
- Enums: `kind` is `topup`, `feeding`, `buffer`, `water_change` or `other`. `detail` is `di`, `tap`, `unknown` or empty (top-up only). `status` is `suggested`, `confirmed` or `rejected`.
- Note: 200 characters or fewer.
- Sync request: `PUT <url>/events/<uid>`, with a JSON body of every record field plus `deleted` when it's a tombstone, and `Authorization: Bearer <token>`. 200 and 201 mean delivered. 400 and 422 mean drop and record an error. Anything else means retry with the existing backoff.
- Events made before NTP sync aren't recorded, because their timestamp would be wrong (the same rule as readings).
- The Pi keeps the newest `updated_at` forever, so a clock far in the future would make later edits "stale". Never stamp `updated_at` from a clock that isn't NTP-synced, and refuse to sync an event whose `updated_at` is more than a day ahead of the current NTP time (keep it dirty and record an error).
- Run `npm run build && npm run embed` before a firmware build, and commit `src/web_assets.h`, which must stay under 61440 bytes. Never upload `data/` to the live device.

## Review Focus

1. **An event is edited while its sync PUT is in flight.** Expect: it stays dirty after the 200, and the newer version is sent next (Task 2: a dirty generation counter).
2. **The store is full and the Pi has been down for days.** Expect: synced events are dropped first, unsynced ones only when every event is unsynced, and the count shows in `/api/status` (Task 2).
3. **A note containing spaces, `|`, quotes, newlines or UTF-8.** Expect: it survives save and load on flash and the JSON to the Pi unchanged (Tasks 2 and 3).
4. **The owner moves a detected event's time by more than 2 hours.** Expect: the detector doesn't recreate it at the original time, because the check uses `origin_at` too (Task 1).
5. **A reboot between an edit and its save, or between a save and its sync.** Expect: after boot, unsaved edits are lost (documented), saved-but-unsynced events are still dirty, and `seq` continues without reusing a number (Task 2).

---

### Task 1: Detector

**Files:**
- Create: `src/event_detect.h`
- Test: `test/test_event_detect/test_main.cpp`

**Interfaces:**
- Produces:
  - `struct DetectLimits { float topupMinDrop = 3.0f; float feedingMinRise = 5.0f; bool feedingEnabled = false; };`
  - `struct DetectSample { long long epoch; float ec; float ph; };` where NAN means missing.
  - `struct Candidate { bool found; bool topup; long long occurredAt; float ecBefore, ecAfter, phBefore, phAfter; float size; };`
  - `Candidate detectAt(const DetectSample *last12, const DetectLimits &limits)`. It takes the 12 most recent samples, oldest first. "Before" is samples 0–5 and "after" is 6–11. It returns not-found if any EC is NAN or any gap is over 1500 s. `occurredAt` is `last12[6].epoch`, and `size` is the absolute median difference. pH medians ignore NAN and give NAN if all are missing.

- [ ] **Step 1: Write the failing tests**

```cpp
void test_step_down_is_topup_at_seventh_sample() {}  // ec 250 x6 then 244 x6 -> found, topup, occurredAt = s[6].epoch, before 250, after 244
void test_noise_below_threshold_not_found() {}
void test_rise_ignored_when_feeding_disabled() {}
void test_rise_is_feeding_when_enabled() {}
void test_nan_ec_or_gap_not_found() {}
void test_ph_median_ignores_nan() {}
```

- [ ] **Step 2: Run them and check they fail**

Run: `pio test -e native -f test_event_detect`. Expected: FAIL to compile.

- [ ] **Step 3: Implement**

- [ ] **Step 4: Run them and check they pass**

Run: `pio test -e native`. Expected: all PASS.

- [ ] **Step 5: Commit**

`git add src/event_detect.h test/test_event_detect && git commit -m "Add top-up and feeding detection rule"`

---

### Task 2: Event log

**Files:**
- Create: `src/event_log.h`
- Test: `test/test_event_log/test_main.cpp`

**Interfaces:**
- Consumes: `Candidate` from Task 1.
- Produces:
  - `struct TankEvent { unsigned long seq; long long occurredAt, originAt, updatedAt; std::string kind, detail, source, status, note; float ecBefore, ecAfter, phBefore, phAfter; bool deleted; unsigned dirtyGen; unsigned syncedGen; };` An event is dirty when `dirtyGen != syncedGen`.
  - `template <int N> class EventLog` with:
    - `bool offerDetected(const Candidate &c, long long now)`. It applies the 2-hour rules from the spec (Device, Detection): drop if within 2 hours, by `occurredAt` or `originAt`, of a same-kind event that's anything other than unedited and suggested. Replace an unedited suggested event within 2 hours if `c.size` is larger. Otherwise add. It returns true if anything changed.
    - `unsigned long addManual(kind, detail, occurredAt, note, now)` returns the new `seq`.
    - `bool update(seq, fields…, now)`. Editing a detected event confirms it, unless the edit sets the status itself.
    - `bool remove(seq, now)`: manual events only. It marks a tombstone, and returns false for detected ones.
    - `void fillDeltas(const DetectSample *recent, int count, long long now)` fills the before/after values for manual events once 6 samples after them exist.
    - `const TankEvent *nextDirty() const`
    - `void markSynced(unsigned long seq, unsigned gen)`, which also drops a tombstone once it has synced.
    - `int size() const`, `int dirtyCount() const`, `unsigned droppedUnsynced() const`.
    - `std::string saveText() const` and `bool loadText(const std::string&)`. The first line is `events v1 <nextSeq>`, then one line per event. Escape spaces, `|`, `\` and newlines in the note so it round-trips.
    - `std::string listJson(const char *device) const` returns `{"events":[...]}`, newest first, without tombstones, with a `"backed_up": bool` on each event.
  - When full, a new event first drops the oldest synced non-dirty event, otherwise the oldest event, and increments `droppedUnsynced`.

- [ ] **Step 1: Write the failing tests**

```cpp
void test_detected_topup_added_suggested_unknown() {}
void test_rejected_blocks_redetection_within_2h() {}
void test_moved_event_blocks_at_origin() {}            // update occurredAt +3h, offer the same candidate -> no change
void test_larger_candidate_replaces_unedited_suggested() {}
void test_edit_confirms_detected() {}
void test_remove_detected_refused_manual_tombstoned() {}
void test_edit_during_sync_stays_dirty() {}           // gen = e.dirtyGen; update(); markSynced(seq, gen) -> still dirty
void test_tombstone_dropped_after_sync() {}
void test_full_drops_synced_first_then_counts() {}
void test_save_load_round_trip_with_awkward_note() {} // note "a | b\\c\n\"é\"" and nextSeq preserved
void test_fill_deltas_for_manual_event() {}
void test_list_json_newest_first_without_tombstones() {}
```

- [ ] **Step 2: Run them and check they fail**

Run: `pio test -e native -f test_event_log`. Expected: FAIL to compile.

- [ ] **Step 3: Implement**

- [ ] **Step 4: Run them and check they pass**

Run: `pio test -e native`. Expected: PASS.

- [ ] **Step 5: Commit**

`git add src/event_log.h test/test_event_log && git commit -m "Add on-device event log with sync state"`

---

### Task 3: Sync body and outcomes

**Files:**
- Create: `src/event_sync.h`
- Test: `test/test_event_sync/test_main.cpp`

**Interfaces:**
- Consumes: `TankEvent` (Task 2), and `utcIso`, `jsonString` and `jsonNumber` (`reading_queue.h`, `json_text.h`).
- Produces:
  - `std::string eventUid(const char *device, unsigned long seq)` returns `"<device>:<seq>"`.
  - `std::string eventSyncPath(const char *device, const TankEvent &e)` returns `"/events/<uid>"`, URL-encoded with the `piPath` encoding.
  - `std::string eventSyncJson(const char *device, const TankEvent &e)` builds every field the Pi's `PUT /events/{uid}` takes. Times use `utcIso`. NAN deltas become `null` and an empty detail becomes `null`. `deleted` is included only when true.
  - `SendOutcome eventSyncOutcome(int httpCode)`: 200 and 201 are Delivered, 400 and 422 are Rejected, anything else is RetryLater.

- [ ] **Step 1: Write the failing tests**

```cpp
void test_uid_and_path_encode_device() {}     // "tank 1", 7 -> "/events/tank%201%3A7"
void test_json_nulls_and_times() {}
void test_json_escapes_note() {}
void test_outcomes() {}
```

- [ ] **Step 2: Run them and check they fail**

Run: `pio test -e native -f test_event_sync`. Expected: FAIL.

- [ ] **Step 3: Implement**

- [ ] **Step 4: Run them and check they pass**

Run: `pio test -e native`. Expected: PASS.

- [ ] **Step 5: Commit**

`git add src/event_sync.h test/test_event_sync && git commit -m "Add event sync request builder"`

---

### Task 4: Sketch wiring

**Files:**
- Modify: `esp32_res_copy_largebox.ino`, `src/api_json.h` (status fields)
- Modify: `web/mock/plugin.ts`, plus new `web/mock/fixtures/events.json`
- Modify: `CLAUDE.md` (Architecture: events, locks and slow-step order)

**Interfaces:**
- Consumes: Tasks 1–3.
- Produces:
  - Globals: `EventLog<100> events`, `SemaphoreHandle_t eventsLock`, `bool eventsSavePending`, `DetectLimits detectLimits` (loaded from NVS `events` at boot).
  - `DeviceHost::recordReading()` builds 12 `DetectSample`s from the reading log under `readingsLock`. Outside that lock, it calls `events.offerDetected` and `events.fillDeltas` under `eventsLock`, and sets `eventsSavePending` if anything changed.
  - The boot sequence calls `events.loadText(LittleFS /events.txt)`.
  - `loop()` slow steps: the event save writes `saveText()`, built under the lock, through `LittleFsStore`. The event sync PUTs `nextDirty()` with 3 s timeouts and the shared backoff, then calls `markSynced(seq, gen)` on Delivered or Rejected (recording the error on Rejected).
  - Routes: `GET /api/events` returns `listJson`. `POST /api/events` takes form `op=create|update|delete|reject|confirm`, plus `seq`, `kind`, `detail`, `occurred_at` (ISO with offset), `note`. It replies `200 {"seq":n}`, or `400 {"error":...}`, or `409` for deleting a detected event. `GET /api/settings` and `POST /api/settings` gain `topup_min_drop`, `feeding_min_rise` and `feeding_enabled`. `/api/status` gains `events: {count, unsynced, dropped_unsynced, last_sync_http}`.
  - The mock serves `/api/events`, GET and POST, from the fixture.

- [ ] **Step 1: Implement the wiring, routes, settings fields and mock**

- [ ] **Step 2: Check it builds**

Run: `pio test -e native` (all PASS), then compile the sketch for ESP32-C3 with the flags in `CLAUDE.md`. Expected: it builds.

- [ ] **Step 3: Commit**

`git commit -am "Detect, store and sync tank events on the device"`

---

### Task 5: Web logic and chart markers

**Files:**
- Modify: `web/src/types.ts` (`TankEvent`, `EventsResponse`), `web/src/api.ts` (`getEvents`, `postEvent`)
- Create: `web/src/logic/events.ts`, `web/src/logic/events.test.ts`
- Modify: `web/src/components/MetricChart.tsx` (an optional `events` prop drawn in a `draw` hook, without changing the scales)

**Interfaces:**
- Produces: `eventsInWindow(events, startSec, endSec)`, `eventLabel(e)` (for example "Top-up (DI)", "Top-up (unknown)" or "Feeding"), `eventDelta(e, 'ec'|'ph'): number | null` and `markerStyle(e): { color: string; dashed: boolean }`, where `dashed` is true when `status === 'suggested'`.

- [ ] **Step 1: Write the failing tests**

```ts
test('keeps only events inside the window', ...)
test('labels top-up detail and feeding', ...)
test('delta is null when a side is missing', ...)
test('suggested events are dashed', ...)
```

- [ ] **Step 2: Run them and check they fail**

Run: `npm test` in `web/`. Expected: FAIL.

- [ ] **Step 3: Implement the logic, types, api functions and the chart hook**

- [ ] **Step 4: Run them and check they pass**

Run: `npm test`. Expected: PASS.

- [ ] **Step 5: Commit**

`git commit -am "Add event logic and chart markers"`

---

### Task 6: Events table, log form and settings

**Files:**
- Create: `web/src/components/EventsTable.tsx`
- Modify: `web/src/screens/Charts.tsx`, `web/src/screens/Settings.tsx`, `web/src/screens/Diagnostics.tsx`, `src/web_assets.h` (regenerated)

**Behaviour:**
- **Charts:** pass `getEvents` data (polled at 60 s and after every write) to the pH and EC charts.
- **Events table:** below the charts, with local time, label, ΔEC (1 decimal), ΔpH (2 decimals), source/status, and a "not backed up" mark while `backed_up` is false. Suggested rows get Confirm and Reject, manual rows get Delete, and every row gets Edit (kind, detail, time, note).
- **Log event:** a form with kind, detail (top-up only), time (defaults to now) and note.
- **Settings:** the three detection fields.
- **Diagnostics:** unsynced and dropped counts, and the last sync HTTP result.

- [ ] **Step 1: Build it and check it against the mock (`npm run dev`)**

Check: markers line up with the fixture times, Confirm posts `op=confirm&seq=…`, Log posts `op=create`, and the table is usable at phone width.

- [ ] **Step 2: Run the tests and size check**

Run `npm test && npm run build && npm run embed`. Expected: tests PASS and the app is under 61440 bytes.

- [ ] **Step 3: Commit**

`git add web src/web_assets.h && git commit -m "Show, log and edit tank events in the web app"`

---

### Task 7: Hardware check (owner, after the Pi plan is deployed)

- [ ] **Step 1: The owner OTA-flashes the build**

- [ ] **Step 2: Log a test event with the Pi reachable**

Log an "other" event with the note `test | "x"`. Expected: it appears at once on the device. Within a minute it shows as backed up, and `GET /events` on the Pi shows it with the note unchanged.

- [ ] **Step 3: Stop the Pi API and log another event**

Run `systemctl --user stop envmonitor-api` on the Pi, then log another event. Expected: it shows as not backed up, and the charts and table still work. Start the API again; within the backoff time the event syncs.

- [ ] **Step 4: Delete the test events**

Expected: both are gone from the device and from the Pi.

- [ ] **Step 5: Wait for a real top-up**

After the next top-up, a suggested top-up should appear about an hour later. Confirm it as DI and check that the Pi shows it confirmed.

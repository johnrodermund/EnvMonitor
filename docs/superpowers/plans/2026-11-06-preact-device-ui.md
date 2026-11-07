# Preact Device Web UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the ESP32-C3's templated pages with a Preact single-page app built into the firmware. It is backed by JSON endpoints, a cache of the Pi's stats, report and thresholds, and device ranges that can override the Pi's.

**Architecture:** Arduino-free logic (JSON building, range parsing and choice, command dispatch, Pi cache) goes in new `src/*.h` headers with native Unity tests. The sketch only wires these to AsyncWebServer handlers and `loop()`. The frontend in `web/` is Vite + Preact + TypeScript + uPlot. Its pure logic gets Vitest tests and its screens are checked against a mock dev server. A Node script gzips the build into `src/web_assets.h`, which the firmware serves.

**Tech Stack:** C++ (Arduino-ESP32 core 2.0.14, ESPAsyncWebServer, HTTPClient, Preferences), Unity via `pio test -e native`, Node 26, Vite 5, Preact 10, TypeScript 5, uPlot 1.6, Vitest 2.

**Spec:** `docs/superpowers/specs/2026-11-06-preact-device-ui-design.md`

## Global Constraints

- Pins: DS18B20 on GPIO 8, relay on GPIO 1, pH and EC on `sensorPin` (GPIO 3). Never use `A0`.
- Web handlers never block: no `writeFile()`, no HTTP calls, no `delay()`. They read state, copy data under a lock, or set pending work for `loop()`.
- `loop()` must keep its `delay(2)` yield and do at most one Pi HTTP request per pass, with 3 s connect and read timeouts.
- The API token is never sent to the browser, logged or committed. The repo is public: grep the diff for the token before each commit.
- Commit as the repo's local identity (the noreply address). End each commit message with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Deploy only by OTA firmware upload to 192.168.12.119. Never upload `data/`, because it wipes readings and calibration.
- Interval range: 2–720 min, stored in ms. The boot guard is 120000–43200000 ms.
- Built-in ranges: pH valid 2–10, target 5.5–6.5; EC valid 0–5000, target 200–1000; temperature valid 30–99, target 60–80 (°F).
- Pi body caps: stats 6144 bytes, report 4096, thresholds 2048. Pi refresh every 300000 ms, one endpoint per `loop()` pass, starting right after boot.
- Polling: `/api/status` every 15 s (2 s while busy); `/api/pi` and `/api/ranges` every 60 s.
- Frontend target: 40 KB gzipped total. Stop and ask the user if the firmware would leave under 60 KB free in the 1,310,720-byte app slot.
- **Deviation from the spec, decided here:** request bodies are `application/x-www-form-urlencoded`, not JSON, so the device needs no JSON parser for input (the existing `/apiconfig` already works this way). Device ranges are set with `POST /api/ranges` instead of `PUT`, because form bodies on PUT aren't reliably parsed by AsyncWebServer. `DELETE /api/ranges` stays. Responses are JSON.

## Review Focus

1. **A title with quotes, a backslash, a newline or non-ASCII text.** The expected behaviour is that status JSON stays valid. Control characters are rejected by settings validation, and UTF-8 passes through. Tests: Task 2 (`jsonString` escapes control characters) and Task 5 (`validateSettings` rejects a newline title).
2. **A NaN or infinite reading**, for example pH when `ph4 == ph7`. The expected behaviour is that the JSON says `null` and the card shows "—" with the fault colour. Tests: Task 5 (`readingsJson` with NaN) and Task 8 (`statusFor(null)`).
3. **An empty log after a reset or first boot.** The expected behaviour is that `/api/readings` returns `[]`, `last_reading` is `null`, and the Dashboard and Charts say "No readings yet" without errors. Tests: Task 5 (empty log JSON) and Task 10 (mock scenario `empty`).
4. **A Pi reply that isn't JSON**, for example an HTML error page from a proxy, or a body cut off at the cap. The expected behaviour is that the cache stores the error and never embeds the body, so `/api/pi` stays valid JSON. Test: Task 6 (`record` with HTML and truncated bodies).
5. **The device restarting while a wizard is open** (uptime goes down). The expected behaviour is that the wizard resets to its first step with "Calibration cancelled", or "Measurement cancelled". Test: Task 12 (wizard reducer on an uptime drop).

---

### Task 1: Measure firmware headroom

**Files:**
- Create: none in the repo. Uses a scratch PlatformIO project at `$FW` (any folder outside the repo).

- [ ] **Step 1: Create the scratch project.** Write `$FW/platformio.ini`:

```ini
[env:check]
platform = espressif32
board = lolin_c3_mini
framework = arduino
upload_protocol = espota
upload_port = 192.168.12.119
build_flags = -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1
lib_deps =
  mathieucarbou/ESPAsyncWebServer
  matmunk/DS18B20
  paulstoffregen/OneWire
```

- [ ] **Step 2: Copy the sources and build.** This copy-and-build sequence is the "firmware build" every later task refers to:

```bash
mkdir -p $FW/src/src && cp esp32_res_copy_largebox.ino $FW/src/ && cp src/*.h $FW/src/src/ && (cd $FW && pio run -e check)
```

Expected: `SUCCESS`, and a `Flash:` line giving bytes used out of 1310720.

- [ ] **Step 3: Record the headroom.** Free = 1310720 − firmware size. If free is under 100 KB (60 KB margin plus about 40 KB for the site), stop and report to the user before Task 2. Otherwise note the number for Task 14.

### Task 2: Shared JSON text helpers

**Files:**
- Create: `src/json_text.h`
- Modify: `src/reading_queue.h` (remove `jsonNumber` and `jsonString`, and include `json_text.h`)
- Test: `test/test_json_text/test_main.cpp`

**Interfaces:**
- Produces:
  - `std::string jsonNumber(float value)`: `"%.3f"`, or `null` for NaN or infinity. Behaviour unchanged.
  - `std::string jsonString(const std::string &value)`: quotes the value. Escapes `"`, `\`, `\n`, `\r` and `\t`, writes other bytes below 0x20 as `\u00XX`, and passes UTF-8 through.
  - `std::string jsonBool(bool value)`.

- [ ] **Step 1: Write failing tests**

```cpp
void escapes_quotes_backslash_and_controls(void) {
  TEST_ASSERT_EQUAL_STRING("\"a\\\"b\\\\c\\nd\\u0001\"", jsonString(std::string("a\"b\\c\nd\x01")).c_str());
}
void passes_utf8_through(void) {
  TEST_ASSERT_EQUAL_STRING("\"Tank \xC2\xB0" "F\"", jsonString("Tank \xC2\xB0" "F").c_str());
}
void number_is_null_for_nan_and_inf(void) {
  TEST_ASSERT_EQUAL_STRING("null", jsonNumber(NAN).c_str());
  TEST_ASSERT_EQUAL_STRING("null", jsonNumber(INFINITY).c_str());
  TEST_ASSERT_EQUAL_STRING("6.200", jsonNumber(6.2f).c_str());
}
```

- [ ] **Step 2: Run them.** `pio test -e native -f test_json_text` should fail to compile, because `json_text.h` doesn't exist yet.
- [ ] **Step 3: Implement `src/json_text.h`.** Move the two helpers there and extend the escaping. Change `reading_queue.h` to include it.
- [ ] **Step 4: Run all suites.** `pio test -e native` should pass, including the existing `test_reading_queue`.
- [ ] **Step 5: Commit** with the message "Move JSON helpers to json_text.h and escape control characters".

### Task 3: ReadingCycle additions for the UI

**Files:**
- Create: `test/fake_host.h`, moving `FakeHost` out of `test/test_reading_cycle/test_main.cpp` unchanged. Both test files include it as `"../fake_host.h"`.
- Modify: `src/reading_cycle.h`
- Test: `test/test_reading_cycle/test_main.cpp`

**Interfaces:**
- Produces, in `src/reading_cycle.h`:
  - A new `CycleCommand::Resume`. It powers the pH probe off if needed, sets `candidatePh4 = ph4Adc`, clears `ph4Ready`, and goes to Waiting with `lastReadingStart = now` (no reading is taken).
  - `enum class CycleStep { Waiting, Holding, Warmup, Sampling, Restarting }` and `CycleStep step() const`. PowerOn reports as Warmup.
  - `enum class CycleJob { Reading, Ph4, Ph7, Manual }` and `CycleJob job() const`.
  - `unsigned long warmupElapsed(unsigned long now) const`, which is 0 outside Warmup and capped at `warmupTime`. Also `static unsigned long warmupMs()` (20000), `int samplesTaken() const` and `static int sampleTotalCount()` (20).
  - `bool ph4Ready() const`: true after a `MeasurePh4` finishes, false after `Hold`, `Resume`, the hold timeout or `MeasurePh7`. `float pendingPh4() const` returns `candidatePh4`.
  - `unsigned manualSeq() const` and `unsigned calibrationSeq() const`, incremented when a Manual or Ph7 job finishes.

- [ ] **Step 1: Write failing tests**, with the names and assertions below:
  - `resume_ends_hold_without_a_reading`: after `Hold`, then `Resume`, then a tick, `status()` is Idle and `recorded.size()` is unchanged.
  - `resume_during_sampling_powers_off_and_discards`: power is false and nothing is recorded.
  - `step_and_progress_follow_the_reading`: `step()` is Warmup after the PowerOn tick, `warmupElapsed` is 12000 at 12 s, `step()` is Sampling after 20 s, and `samplesTaken()` counts up to 20.
  - `ph4_ready_only_after_ph4_measurement`: false after `Hold`, true after `MeasurePh4` finishes, false after `Resume`.
  - `manual_and_calibration_seq_increment`: each goes up by exactly 1 per finished job.
- [ ] **Step 2: Run them.** `pio test -e native -f test_reading_cycle` should fail on the missing members.
- [ ] **Step 3: Implement** the members above in `src/reading_cycle.h`. Make `ph4Ready` and the sequence counters `volatile`, because handlers read them.
- [ ] **Step 4: Run all suites.** `pio test -e native` should pass.
- [ ] **Step 5: Commit** with the message "Add Resume, step progress and calibration state to ReadingCycle".

### Task 4: Ranges

**Files:**
- Create: `src/ranges.h`
- Test: `test/test_ranges/test_main.cpp`

**Interfaces:**
- Consumes: `jsonNumber`, `jsonString` (Task 2).
- Produces:
  - `struct MetricRange { float validLo, validHi, targetLo, targetHi; };`
  - `struct Ranges { MetricRange ph, ec, temp; };`
  - `Ranges builtinRanges()`, holding the Global Constraints values.
  - `bool validateRanges(const Ranges &r, std::string &error)`: each metric needs `validLo < validHi`, `targetLo < targetHi`, and `validLo <= targetLo` and `targetHi <= validHi`, all finite. The error names the metric, for example `"ph: target must sit inside valid"`.
  - `bool parseThresholds(const std::string &json, Ranges &out, std::string &error)`: reads `ph`, `ec` and `temp`, each with `valid:[a,b]` and `target:[a,b]`, from the Pi's thresholds JSON. Extra keys are ignored. It's a small scanner, not a general JSON parser: find `"<metric>"`, then the next `"valid"` and `"target"` arrays inside that object's braces. It accepts integers, decimals and negatives.
  - `std::string rangesJson(const Ranges &r)` gives `{"ph":{"valid":[2.000,10.000],"target":[5.500,6.500]},"ec":{...},"temp":{...}}`.
  - `enum class RangeSource { Device, Pi, Builtin }` and `const char *rangeSourceName(RangeSource)`, giving `"device"`, `"pi"` or `"builtin"`.
  - `RangeSource chooseRanges(bool hasDevice, const Ranges &device, bool hasPi, const Ranges &pi, Ranges &out)`: device, then Pi, then built-in.

- [ ] **Step 1: Write failing tests**
  - `parses_pi_thresholds_with_extra_keys`: input is the full `thresholds.example.json` from PiServer, including `drift_per_hour`, `stuck_min_count` and `default_interval_s`. Expect `ph.targetLo == 5.5f`, `ec.validHi == 5000`, and `temp.targetHi == 80`.
  - `rejects_missing_metric`: JSON without `"temp"` gives false, with an error containing `"temp"`.
  - `rejects_target_outside_valid`: ph target 1–6 with valid 2–10 gives false, `"ph: target must sit inside valid"`.
  - `rejects_reversed_range`: `validLo > validHi` gives false.
  - `choose_prefers_device_then_pi_then_builtin`: covers all three sources and checks the returned `out`.
  - `ranges_json_round_trips`: `parseThresholds(rangesJson(builtinRanges()))` equals `builtinRanges()`.
- [ ] **Step 2: Run them.** `pio test -e native -f test_ranges` should fail to compile.
- [ ] **Step 3: Implement `src/ranges.h`.**
- [ ] **Step 4: Run them again.** The suite should pass.
- [ ] **Step 5: Commit** with the message "Add range parsing, validation and source choice".

### Task 5: Status, readings and settings JSON

**Files:**
- Create: `src/api_json.h`
- Test: `test/test_api_json/test_main.cpp`

**Interfaces:**
- Consumes: `json_text.h`, `ReadingLog<N>` (its `size()`, `ph()/ec()/wt()` RingStats with `mean`, `sVar` and `currentValue`, and the ordered timestamps), and the `ReadingCycle` accessors from Task 3.
- Produces:
  - `std::string isoFromLogTimestamp(const std::string &ts)`: `"2026,11-1,04,16,33,06"` becomes `"2026-11-04T16:33:06"`, a local time with no zone. Empty or unparseable input gives `""`.
  - `template <int N> std::string readingsJson(const ReadingLog<N> &log)` gives `[["2026-11-04T16:33:06",6.200,244.730,75.200],...]`, oldest first. An empty timestamp becomes `null`. Add a public `const char *timestampAt(int i) const` to `ReadingLog` if the ordered timestamp isn't reachable yet.
  - `struct StatusView` has these plain fields, filled by the sketch: `title`, `intervalMin`, `uptimeS`, `freeHeap`, `count`, `lastTime`, `ph`, `phPm`, `ec`, `wt`, `means[3]`, `stds[3]`, `state` (`"idle"`, `"busy"` or `"holding"`), `step`, `job`, `warmupElapsedMs`, `samplesTaken`, `ph4Ready`, `pendingPh4`, `manualSeq`, `manualPh`, `manualEc`, `manualWt`, `calibrationSeq`, `ph4Adc`, `ph7Adc`, `lastPhAdc`, `phSamples[20]`, `phSampleCount`, `lastEcAdc`, `apiQueued`, `apiLastHttp`, `apiFailures`, `apiDelivered`.
  - `std::string statusJson(const StatusView &s)` uses exactly these keys:
    `{"title","interval_min","uptime_s","free_heap","count","last_reading":{"time","ph","ph_pm","ec","wt"}|null,"averages":{"ph":[m,s],"ec":[m,s],"wt":[m,s]},"cycle":{"state","step","job","warmup_elapsed_ms","warmup_ms":20000,"samples":n,"sample_total":20,"ph4_ready","pending_ph4_adc"},"manual":{"seq","ph","ec","wt"},"calibration":{"seq","ph4_adc","ph7_adc"},"diag":{"last_ph_adc","ph_samples":[...],"last_ec_adc"},"api":{"queued","last_http","failures","delivered"}}`.
    `last_reading` is `null` when `count == 0`.
  - `std::string settingsJson(int intervalMin, const std::string &title)` gives `{"interval_min":10,"title":"..."}`.
  - `bool validateSettings(const std::string &interval, const std::string &title, std::string &error)`:
    - The interval must be all digits, from 2 to 720. Otherwise the error is `"interval must be a whole number of minutes from 2 to 720"`.
    - The title must be 1–40 bytes with no byte below 0x20. Otherwise the error is `"title must be 1-40 characters with no control characters"`.
    - An empty string means "not provided" and is skipped. Both empty is invalid, with the error `"nothing to save"`.

- [ ] **Step 1: Write failing tests**
  - `iso_from_log_timestamp`: the example above gives `"2026-11-04T16:33:06"`, `""` gives `""`, and `"garbage"` gives `""`.
  - `readings_json_empty_log_is_empty_array`: gives `"[]"`.
  - `readings_json_writes_null_for_nan_and_empty_time`: appending `("", NAN, 1, 2)` gives `[[null,null,1.000,2.000]]`.
  - `status_json_last_reading_null_when_empty`: the output contains `"last_reading":null`.
  - `status_json_escapes_title`: title `a"b` gives output containing `"title":"a\"b"`.
  - `validate_settings_rules`: `"10.5"`, `"1"`, `"721"` and `"abc"` are rejected. A title containing `\n`, or 41 bytes long, is rejected. `"2"`, `"720"` and the title `"Tank"` are accepted.
- [ ] **Step 2: Run them.** `pio test -e native -f test_api_json` should fail.
- [ ] **Step 3: Implement `src/api_json.h`.**
- [ ] **Step 4: Run all suites.** `pio test -e native` should pass.
- [ ] **Step 5: Commit** with the message "Add JSON builders for status, readings and settings".

### Task 6: Pi cache

**Files:**
- Create: `src/pi_cache.h`
- Test: `test/test_pi_cache/test_main.cpp`

**Interfaces:**
- Consumes: `json_text.h`.
- Produces:
  - `enum class PiEndpoint { Stats, Report, Thresholds }`.
  - `std::string piPath(PiEndpoint e, const std::string &device)` gives `/stats?window=24h&device=<device>`, `/reports/latest?device=<device>` or `/thresholds`. The device name is URL-encoded.
  - The `class PiCache` methods:
    - `bool due(unsigned long now)`: true at the first call after boot. After that, true while the current round still has endpoints left, and again 300000 ms after the last round started.
    - `PiEndpoint next()`: cycles Stats, then Report, then Thresholds. `due()` turns false after Thresholds, until the next round.
    - `void record(PiEndpoint e, int httpCode, const std::string &body, unsigned long now)`. The checks apply in this order:
      - 200: the body is trimmed. It must start with `{` and end with `}`, and stay within that endpoint's cap. If so, the body is stored, the fetch time set and the error cleared. Otherwise the old body is kept and the error is set to `"reply too large"` or `"not JSON"`.
      - 404 on Report: the body is cleared, and `none = true` with no error.
      - Any other code: the old body is kept, and the error is set to `"HTTP <code>"`, or `"unreachable"` when the code is ≤ 0.
    - `void markUnconfigured()`: sets the error `"Pi not configured"` on all three entries.
    - `std::string json(unsigned long now) const` gives `{"stats":E,"report":E,"thresholds":E}`, where each E is `{"age_s":n|null,"error":"..."|null,"none":bool,"body":<stored JSON>|null}`.
    - `bool thresholdsBody(std::string &out) const`.

- [ ] **Step 1: Write failing tests**
  - `first_call_is_due_and_rounds_cycle_three_endpoints`: covers the round order, `due()` false after Thresholds, and true again at +300000.
  - `stores_valid_json_and_reports_age`: `age_s` is 30 after 30000 ms.
  - `rejects_html_body_and_keeps_previous`: after recording a valid body, recording `"<html>502</html>"` keeps the old body, and the error is `"not JSON"`.
  - `rejects_truncated_and_oversized`: a body of 6145 bytes gives `"reply too large"`, and `"{\"a\":1"` gives `"not JSON"`.
  - `report_404_means_none`: gives `"none":true,"error":null,"body":null`.
  - `unreachable_code`: code −1 gives the error `"unreachable"`.
  - `json_is_valid_with_empty_cache`: gives `{"stats":{"age_s":null,"error":null,"none":false,"body":null},...}`.
- [ ] **Step 2: Run them.** `pio test -e native -f test_pi_cache` should fail.
- [ ] **Step 3: Implement `src/pi_cache.h`.**
- [ ] **Step 4: Run all suites.** `pio test -e native` should pass.
- [ ] **Step 5: Commit** with the message "Add Pi cache with size caps and JSON checks".

### Task 7: Command dispatch

**Files:**
- Create: `src/api_commands.h`
- Test: `test/test_api_commands/test_main.cpp` (uses `test/fake_host.h`)

**Interfaces:**
- Consumes: `ReadingCycle` and the Task 3 accessors.
- Produces:
  - `struct CommandResult { int httpCode; std::string message; };`
  - `CommandResult dispatchCommand(const std::string &cmd, ReadingCycle &cycle)`. The rules apply in order:
    1. An unknown command gives 400, `"unknown command"`.
    2. `cancel`, `restart` and `reset` are always accepted, giving 202. They request `Resume`, `Restart` and `WipeAndRestart`.
    3. If `status()` is Busy, give 409 with `"busy: <step>"`, where the step is `warmup`, `sampling` or `restarting`.
    4. `reading` needs Idle, otherwise 409 `"finish or cancel the current step first"`. It requests `TakeReading`.
    5. `measure_start` and `calibrate_start` need Idle or Holding. They request `Hold`.
    6. `measure` and `calibrate_ph4` need Holding, otherwise 409 `"start first"`. They request `MeasureManual` and `MeasurePh4`.
    7. `calibrate_ph7` needs Holding and `ph4Ready()`, otherwise 409 `"measure pH 4 first"`. It requests `MeasurePh7`.

    Accepted commands give 202, `"accepted"`.

- [ ] **Step 1: Write failing tests**, one per rule:
  - `unknown_is_400`
  - `cancel_always_accepted_even_when_busy`
  - `busy_rejects_reading_with_step`
  - `reading_rejected_while_holding`
  - `measure_needs_hold`
  - `calibrate_ph7_needs_ph4`
  - `calibrate_flow_end_to_end`: drives `FakeHost` through start, ph4, ph7, and asserts `calibrationSeq() == 1` and that `ph4()` and `ph7()` changed.
- [ ] **Step 2: Run them.** `pio test -e native -f test_api_commands` should fail.
- [ ] **Step 3: Implement `src/api_commands.h`.**
- [ ] **Step 4: Run all suites.** `pio test -e native` should pass.
- [ ] **Step 5: Commit** with the message "Add command dispatch for the web API".

### Task 8: Frontend scaffold and pure logic

**Files:**
- Create: `web/package.json`, `web/tsconfig.json`, `web/vite.config.ts`, `web/index.html`, `web/src/main.tsx`, `web/src/types.ts`, `web/src/logic/status.ts`, `web/src/logic/format.ts`, `web/src/logic/csv.ts`
- Test: `web/src/logic/*.test.ts`
- Modify: `.gitignore` (add `web/node_modules/` and `web/dist/`)

**Interfaces:**
- Produces:
  - `types.ts`: TypeScript types that mirror `statusJson`, `readingsJson` (`Row = [string|null, number|null, number|null, number|null]`), `/api/pi` (`PiEntry<T> = {age_s: number|null; error: string|null; none: boolean; body: T|null}`), the Pi `/stats` body (`stats` keyed by column `ph`, `ec` and `water_temp_f`, each with `latest`, `mean`, `std`, `min`, `max`, `slope_per_hour` and `count`; `flags: {id, evidence}[]`; `severity`), the Pi report body (from the PiServer `reports` table: `created_at`, `severity`, `flags`, `headline`, `summary`, `source`, `model`, `duration_s`), and `RangesResponse = {source: 'device'|'pi'|'builtin'; ranges: Ranges; pi: Ranges|null}`.
  - `status.ts`:
    - `type Level = 'ok'|'warn'|'fault'`.
    - `levelFor(value: number|null, r: MetricRange): Level`: null or NaN, or outside valid, gives fault; outside target gives warn; otherwise ok. The bounds are inclusive.
    - `tagsFor(metric: 'ph'|'ec'|'temp', flags: {id:string}[]): ('drifting'|'noisy')[]`, matching the Pi flag ids `<metric>_drift` and `<metric>_noisy` exactly (from PiServer `agent/stats.py`).
  - `format.ts`:
    - `fmt(value: number|null, digits: number): string` gives `'—'` for null.
    - `ago(seconds: number|null): string` gives `'just now'` under 60 s, then `'5 min ago'`, `'3 h ago'`, or `'—'`.
    - `slope(perHour: number|null, unit: string): string` gives, for example, `'+0.02 pH/h ↑'`, `'−1.5 µS/cm/h ↓'` or `'0.00 pH/h →'` (the arrow is → when |x| < 0.005).
    - `localTime(iso: string|null): string`.
  - `csv.ts`: `toCsv(rows: Row[]): string` with the header `time,ph,ec_uS_cm,water_temp_F`. Null values become empty cells, and lines end with `\n`.

- [ ] **Step 1: Scaffold.** Pin exact versions in `web/package.json`: `preact@10.24.3` and `uplot@1.6.31`; dev dependencies `vite@5.4.11`, `@preact/preset-vite@2.9.1`, `typescript@5.6.3` and `vitest@2.1.5`. Scripts: `dev`, `build` (`tsc --noEmit && vite build`), `test` (`vitest run`) and `embed` (Task 14). In `vite.config.ts`, set `build.assetsInlineLimit: 0`, `build.cssCodeSplit: false`, and output names without hashes (`assets/app.js`, `assets/app.css`). Run `npm install` in `web/` and commit `package-lock.json`.
- [ ] **Step 2: Write failing tests**
  - `levelFor`: `(6.0, ph)` gives ok, `(7.0)` warn, `(15.6)` fault, `(null)` fault, `(NaN)` fault, `(5.5)` ok.
  - `tagsFor('ph', [{id:'ph_drift'}, {id:'temp_noisy'}])` gives `['drifting']`, and `tagsFor('temp', ...)` on the same flags gives `['noisy']`.
  - `slope(0.0234,'pH')` gives `'+0.02 pH/h ↑'`, and `slope(0.001,'pH')` gives `'0.00 pH/h →'`.
  - `ago(30)` gives `'just now'`, `ago(300)` gives `'5 min ago'`, and `ago(null)` gives `'—'`.
  - `toCsv([["2026-11-04T16:33:06",6.2,null,75.2]])` gives `'time,ph,ec_uS_cm,water_temp_F\n2026-11-04T16:33:06,6.2,,75.2\n'`.
- [ ] **Step 3: Run them.** `npm test` should fail.
- [ ] **Step 4: Implement** the three logic modules.
- [ ] **Step 5: Run them again.** `npm test` should pass, and `npm run build` should succeed.
- [ ] **Step 6: Commit** with the message "Scaffold Preact web app with status, format and CSV logic".

### Task 9: API client and mock dev server

**Files:**
- Create: `web/src/api.ts`, `web/src/usePoll.ts`, `web/mock/plugin.ts`, and fixtures `web/mock/fixtures/{status-idle,status-busy,status-holding,status-fault,status-empty,readings,readings-empty,pi,pi-none,pi-stale,ranges}.json`
- Modify: `web/vite.config.ts` (register the plugin in `serve` mode only)

**Interfaces:**
- Produces:
  - `api.ts`, where every function throws `ApiError {status: number; message: string}` for any non-2xx response or network failure:
    - `getStatus(): Promise<Status>`
    - `getReadings(): Promise<Row[]>`
    - `getPi(): Promise<PiResponse>`
    - `getRanges(): Promise<RangesResponse>`
    - `getSettings(): Promise<{interval_min:number; title:string}>`
    - `saveSettings(p: {interval?: number; title?: string}): Promise<void>`
    - `command(cmd: CommandName): Promise<void>`
    - `saveRanges(r: Ranges): Promise<void>`, which posts form fields `ph_valid_lo`, `ph_valid_hi`, `ph_target_lo`, `ph_target_hi`, and the same for `ec_` and `temp_`
    - `resetRanges(): Promise<void>`
    - `getApiConfig(): Promise<{url:string; device:string; token_set:boolean}>`
    - `saveApiConfig(p: {url?: string; device?: string; token?: string}): Promise<void>`
  - `usePoll<T>(fn: () => Promise<T>, intervalMs: number): {data: T|null; error: ApiError|null; at: number|null; refresh(): void}`. It keeps the last good data when an error happens.
  - The mock plugin serves every `/api/*` route from the fixtures. A `?scenario=` value on the page URL (`idle`, `busy`, `holding`, `fault`, `empty`, `offline`, `nopi`, `stale`), saved in a cookie, chooses the fixtures. `offline` returns 503 for everything. In the `idle` scenario, `POST /api/command` returns 202 and switches to `busy` for 5 s.
  - Record fixtures from the real device where possible. `curl http://192.168.12.119/diag` gives realistic values for `status-*`. Pi bodies come from `curl -H "Authorization: Bearer $TOKEN" http://192.168.12.230:8000/stats`, with the token entered by the user and never written to a file. Hand-edit the variants.

- [ ] **Step 1: Write failing tests** in `web/src/api.test.ts`, with `fetch` stubbed:
  - `command('reading')` posts `cmd=reading` as form data to `/api/command`.
  - A 409 with the body `{"error":"busy: warmup"}` throws an `ApiError` with status 409 and message `'busy: warmup'`.
  - A network failure throws an `ApiError` with status 0.
- [ ] **Step 2: Run them.** `npm test` should fail.
- [ ] **Step 3: Implement** `api.ts`, `usePoll.ts` and the mock plugin with its fixtures.
- [ ] **Step 4: Check.** `npm test` should pass. Then run `npm run dev` and confirm that `curl localhost:5173/api/status` returns the idle fixture.
- [ ] **Step 5: Commit** with the message "Add API client, polling hook and mock dev server".

### Task 10: App shell and Dashboard

**Files:**
- Create: `web/src/App.tsx` (hash router, nav, offline banner), `web/src/styles.css` (light and dark tokens), `web/src/screens/Dashboard.tsx`, `web/src/components/{MetricCard,ReviewPanel,Progress}.tsx`

**Interfaces:**
- Consumes: `api.ts`, `usePoll`, `levelFor`, `tagsFor`, `fmt`, `ago` and `slope`.
- Produces:
  - Routes `#/`, `#/charts`, `#/measure`, `#/calibrate`, `#/settings` and `#/diag`.
  - `<Progress status={Status} />`, reused by Measure and Calibrate. It reads "Warming pH probe 12/20 s" or "Sampling 7/20".
  - App-level polling of status: 15000 ms, or 2000 ms while `cycle.state === 'busy'`.

Behaviour, from the spec's Screens section:
- An offline banner, "Device not responding, retrying", appears when status polling errors. The last data stays on screen, greyed.
- The cards show "Sensor fault" plus the raw value when `levelFor` gives fault.
- The review panel shows the severity badge, headline, summary, the flags with their evidence, the time and the source. The source reads "qwen2.5:1.5b · 52 s", or "template" when no model wrote it.
- The Pi age line reads "Pi data from 14:20 · unreachable" when there's an error, and "No Pi data yet" when the body is null.
- "Take reading now" is disabled unless the device is idle.
- With no readings, the page says "No readings yet".

- [ ] **Step 1: Build the shell and Dashboard.**
- [ ] **Step 2: Check each mock scenario in the browser pane** at mobile (375×812) and desktop widths, taking a screenshot of each:
  - `idle`: three cards, green or amber.
  - `fault`: a red pH card reading "Sensor fault 15.62".
  - `busy`: the progress bar.
  - `empty`: "No readings yet".
  - `offline`: the banner, with greyed values.
  - `nopi`: "No Pi data yet".
  - `stale`: the age and error line.
- [ ] **Step 3: Check the build.** `npm run build` should succeed, and `npm test` should pass.
- [ ] **Step 4: Commit** with the message "Add app shell and dashboard".

### Task 11: Charts screen

**Files:**
- Create: `web/src/screens/Charts.tsx`, `web/src/components/MetricChart.tsx` (a uPlot wrapper), `web/src/logic/window.ts`
- Test: `web/src/logic/window.test.ts`

**Interfaces:**
- Produces: `windowRows(rows: Row[], hours: number|null, nowMs: number): Row[]`. It drops rows with a null time and keeps those within `hours` of the newest row; `null` means all rows.

- [ ] **Step 1: Write failing tests.** `windowRows` with 6 h keeps the rows from the last 6 h relative to the newest row (not the browser clock, since device time has no zone), and drops rows with a null time.
- [ ] **Step 2: Implement** `window.ts`, then the screen:
  - Three uPlot charts with a shared cursor (uPlot `cursor.sync`).
  - The target band shaded from `ranges`, and points outside it drawn in the warn colour.
  - 6 h, 24 h and All buttons.
  - A Download CSV button using `toCsv`, saved as `envmonitor-<date>.csv`.
  - "No readings yet" when the log is empty.
- [ ] **Step 3: Check.** `npm test` should pass. In the browser pane with the `idle` and `empty` scenarios at both widths: drag-zoom works, the CSV downloads, and there's no horizontal scroll at 375 px.
- [ ] **Step 4: Commit** with the message "Add charts screen with ranges and CSV export".

### Task 12: Measure and Calibrate wizards

**Files:**
- Create: `web/src/logic/wizard.ts`, `web/src/screens/Measure.tsx`, `web/src/screens/Calibrate.tsx`
- Test: `web/src/logic/wizard.test.ts`

**Interfaces:**
- Produces:
  - `type Kind = 'measure'|'calibrate'`.
  - `type WizardState = {kind: Kind; step: 'intro'|'starting'|'ready'|'ph4'|'ph4done'|'ph7'|'done'|'cancelled'; startSeq: number; startUptime: number; message?: string}`.
  - `wizardStart(kind: Kind, s: Status): WizardState`.
  - `wizardOnStatus(w: WizardState, s: Status): WizardState`:
    - `s.uptime_s < w.startUptime` (the device restarted) gives `cancelled`, with the message "Calibration cancelled" or "Measurement cancelled".
    - In `ph4`, state holding with `ph4_ready` gives `ph4done`.
    - In `ph7`, or in a measure's `ready` after Start, `calibration.seq` or `manual.seq` going above `startSeq` gives `done`.
    - State idle before done (the hold timed out or was cancelled elsewhere) gives `cancelled`.

- [ ] **Step 1: Write failing tests**
  - `restart_during_calibration_cancels`: uptime 500 then 3 gives `cancelled` with "Calibration cancelled".
  - `ph4_completes_when_ready`.
  - `calibration_done_when_seq_increments`.
  - `measure_done_when_manual_seq_increments`.
  - `idle_mid_wizard_cancels`.
- [ ] **Step 2: Run them.** `npm test` should fail.
- [ ] **Step 3: Implement** `wizard.ts`, then both screens. They show the instructions from the spec's Screens section, `<Progress>`, the results (old and new pH 4 and pH 7 raw values; the manual pH, EC and temperature), and a Cancel button that sends `cancel`. A 409 shows its message inline.
- [ ] **Step 4: Check.** `npm test` should pass. In the browser pane with the `holding` and `busy` scenarios, step through both wizards and Cancel.
- [ ] **Step 5: Commit** with the message "Add measure and calibrate wizards".

### Task 13: Settings and Diagnostics

**Files:**
- Create: `web/src/screens/Settings.tsx`, `web/src/screens/Diagnostics.tsx`, `web/src/components/RangesTable.tsx`, `web/src/logic/rangesForm.ts`
- Test: `web/src/logic/rangesForm.test.ts`

**Interfaces:**
- Produces: `validateRangesForm(r: Ranges): string|null`, with the same rules and messages as `validateRanges` in Task 4.

- [ ] **Step 1: Write failing tests** for `validateRangesForm`: a target outside valid gives `'ph: target must sit inside valid'`, and the built-in ranges give null.
- [ ] **Step 2: Implement the screens.**
  - Settings:
    - Interval (2–720) and title, each with Save.
    - A ranges table with the source label (Device, Pi or Built-in), the Pi values in a second column, Save as device ranges, and Use Pi ranges (disabled unless the source is device).
    - Pi connection: URL, device and token (a password field that's never filled; the label reads "Token: set" or "not set"), plus the upload status.
    - Restart behind `confirm()`, and Reset data behind a dialog that needs `RESET` typed.
    - 400 messages are shown under the matching field.
  - Diagnostics:
    - The calibration ADC values, the last pH ADC and a small uPlot of `ph_samples`, and the last EC ADC.
    - Free heap and uptime.
    - The Pi cache fetch ages and errors.
    - A link to `/diag`.
- [ ] **Step 3: Check.** `npm test` should pass. In the browser pane, try a ranges save with an invalid target to see the inline error, Use Pi ranges, and the Reset dialog (cancel it).
- [ ] **Step 4: Commit** with the message "Add settings and diagnostics screens".

### Task 14: Firmware wiring, embedded site and removal of old pages

**Files:**
- Create: `web/scripts/embed.mjs`, `src/web_assets.h` (generated, and committed so firmware builds don't need Node)
- Modify: `esp32_res_copy_largebox.ino`, `data/` (delete `index.html`, `index.js`, `index.css`, `update.html`, `calibrate0.html`, `calibrate2.html` and `waiting.html`)

**Interfaces:**
- Consumes: everything from Tasks 2–7, plus `web/dist/`.
- Produces:
  - `embed.mjs` reads `web/dist/index.html`, `assets/app.js` and `assets/app.css`, and gzips each at level 9. It writes `src/web_assets.h` with `struct WebAsset { const char *path; const char *mime; const char *etag; const uint8_t *data; size_t length; };` and `const WebAsset webAssets[]`, plus a count. The ETag is the first 16 hex characters of the SHA-256 of the gzipped bytes. It prints each file's size and the total, and exits 1 if the total is over 61440 bytes.
  - Sketch routes, as specified in the spec's endpoint table, with the form-body deviation from Global Constraints:
    - `GET /api/status`, `/api/readings`, `/api/settings`, `/api/pi`, `/api/ranges` and `/api/apiconfig`.
    - `POST /api/settings`, `/api/command`, `/api/ranges` and `/api/apiconfig`.
    - `DELETE /api/ranges`.
    - Errors are `{"error":"<message>"}` with the matching status code.
  - Assets: `/` serves `index.html`; `/assets/app.js` and `/assets/app.css` are served with `Content-Encoding: gzip`. `If-None-Match` gets a 304. Assets have `Cache-Control: max-age=31536000`, and `index.html` has `no-cache`. Asset names stay fixed and ETags handle cache busting, so with the long max-age, `index.html` must reference the assets as `assets/app.js?v=<etag>`. `embed.mjs` rewrites the reference.

Firmware changes:
- **Readings lock.** A new `SemaphoreHandle_t readingsLock` is taken around `readings.append`, `clear` and `save` in `DeviceHost`, and around building `readingsJson` and `StatusView` in handlers.
- **Settings.** `POST /api/settings` validates with `validateSettings`, then sets `pendingInterval` and `pendingTitle`. `loop()` writes them with `writeFile` and calls `cycle.setInterval`. The boot guard becomes 120000–43200000 ms.
- **Device ranges.** These are saved in NVS namespace `ranges`, as key `json`, holding the `rangesJson` text. `POST` validates the form into `Ranges` with `validateRanges`. `DELETE` removes the key. `GET` builds `{"source":..., "ranges":..., "pi":...|null}` using `chooseRanges`, with the Pi ranges parsed from `piCache.thresholdsBody`.
- **Pi fetch** in `loop()`, after `sendQueuedReading()`:
  - If the URL or token is empty, call `markUnconfigured()`.
  - Otherwise, when `piCache.due(millis())`, do one GET of `piPath(piCache.next(), apiDevice)` with the Bearer header and 3 s timeouts, then call `record`.
  - The cache is guarded by `piCacheLock`.
- **Commands.** `POST /api/command` calls `dispatchCommand` and returns `{"ok":true}` with 202, or the error.
- **API config.** `/api/apiconfig` reuses the existing `/apiconfig` save code (moved into a function), and returns `token_set`, never the token.
- **Removed:** `processor()`, the `/get`, `/update.html`, `/index.js` and `/index.css` routes, the `title`, `interval0` and `ph`-template code used only by `processor`, and the `%m-1` in `getDateTime()`. The log keeps its existing `"%Y,%m-1,..."` text format, because `/readings.txt` rows depend on it, and `isoFromLogTimestamp` converts it.
- **Kept:** `/diag`, the GET `/apiconfig` text page, the setup portal and OTA.

- [ ] **Step 1: Write `embed.mjs`.** Run `npm run build && npm run embed`. Expected: it prints the sizes, the total is ≤ 61440, and `src/web_assets.h` is generated.
- [ ] **Step 2: Wire the routes and `loop()` changes** in the sketch, and delete the old pages and their code.
- [ ] **Step 3: Check.** `pio test -e native` should pass. Run the firmware build (Task 1, Step 2): it should report SUCCESS and leave at least 61440 bytes free in the app slot. If it doesn't, stop and ask the user (larger partition or the LittleFS approach).
- [ ] **Step 4: Grep the diff for the token and the Pi address in source:** `git diff --cached | grep -n -i -E 'bearer [a-z0-9]{8}|192\.168\.12\.230'` should print nothing.
- [ ] **Step 5: Commit** with the message "Serve the embedded Preact app and JSON API; remove templated pages".

### Task 15: Deploy, verify on the device and update docs

**Files:**
- Modify: `CLAUDE.md` (the Architecture, Templating and Gotchas sections: describe `/api/*`, `web/`, `npm run build && npm run embed` before a firmware build, the Pi cache and device ranges; remove the `/get`, `processor` and `%%` notes), `README.md` (Building: Node step, and that the site lives in the firmware), `docs/superpowers/specs/2026-11-06-preact-device-ui-design.md` (note the form-body and `POST /api/ranges` deviation)

- [ ] **Step 1: OTA upload.** Run `(cd $FW && pio run -e check -t upload)`. Expected: `Success`, then the device answers `curl -s http://192.168.12.119/api/status` with valid JSON within 30 s.
- [ ] **Step 2: Check the device in the browser pane** at `http://192.168.12.119/`, at mobile and desktop widths:
  - The Dashboard values match `/diag` and the last row of `/api/readings`.
  - The review panel shows the Pi's latest report.
  - Charts show the logged history.
  - "Take reading now" runs through the progress bar, and a new row appears.
  - Measure completes once.
  - Calibrate is exercised up to Cancel only, unless the user has buffers ready.
  - A device ranges save shows the source "Device", and Use Pi ranges brings back "Pi".
- [ ] **Step 3: Check behaviour with the Pi down.** Ask the user before stopping `envmonitor-api` on the Pi. With it stopped, the review panel shows the age and "unreachable" within 5 min, and a reading still completes on schedule. Restart the service afterwards.
- [ ] **Step 4: Confirm readings and calibration survived.** `/api/readings` count is at least the pre-upload count, and `/diag` calibration still reads ph4=2368, ph7=1880.
- [ ] **Step 5: Update the docs** listed above.
- [ ] **Step 6: Commit** with the message "Document the device web app and API". Push only if the user asks.

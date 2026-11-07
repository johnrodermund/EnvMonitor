# Device web UI rebuild (Preact): design

Date: 2026-11-06. Status: approved in conversation, awaiting written-spec review.

## Goal

Replace the device's web pages at the monitor's address with an easier single-page app, built with Preact and served from the ESP32-C3. It shows the device's live readings, plus the Pi's trend statistics and the latest LLM review, and it replaces the `/get` form wizards with guided screens.

## Understanding

What the user asked for:
- A more user-friendly site on the device, built with Preact and hosted on the device.
- Include the Pi's computed metrics (drift and the rest) and the most recent LLM review: what triggered it, and its response.
- Ranges for the colour status can be edited on the device. Device ranges override the Pi's, and there must be a way to go back to the Pi's ranges.

Assumptions (not contradicted in review):
- Used mostly from a phone on the home network, sometimes from a desktop.
- The new site covers everything the current pages do. The old pages and `/get` can be removed.
- Done means every screen works on phone and desktop widths, the device still samples on schedule, and an OTA upload keeps readings, calibration and settings.

## Decisions

| Question | Decision |
|---|---|
| Framework | Preact + TypeScript, built with Vite. uPlot for charts, so they work without internet. |
| Delivery | Built into the firmware as gzipped byte arrays in `src/web_assets.h`. An OTA firmware upload updates the site. LittleFS isn't used for the site, so `data/` is never uploaded again. |
| Pi data | The device fetches it with the API token and caches it. The browser never sees the token and never calls the Pi. |
| Ranges | Pi ranges by default. Device ranges in NVS override them for the page only. "Use Pi ranges" deletes the device ranges. Built-in defaults apply when neither exists. Device ranges never change the Pi's ranges or the reviewer. |
| Routing | Hash routes (`#/settings`), so the device serves one HTML file. |

Alternatives rejected:
- **Site in LittleFS with an upload endpoint:** the site and firmware can get out of step, deploys take two steps, and the endpoint would let anyone on the network write files.
- **Clean up the current templated pages:** fixes none of the problems (no refresh, no wizard progress, broken redirects).
- **Full React:** about 45 KB gzipped more, with no benefit for six screens.
- **Browser calls the Pi directly:** would expose the API token in the page.

## Architecture

### Firmware JSON endpoints

The functions that build each JSON body live in `src/` with no Arduino dependencies, so they get native tests. Handlers only read state, copy data under a lock, or queue work. They never block.

| Endpoint | Purpose |
|---|---|
| `GET /api/status` | Latest pH, EC and temperature with the pH ± value, the device averages and spread, cycle state (`idle`, `busy` or `holding`) with the current step and its progress (`warmup` ms elapsed of 20000, `sampling` count of 20), the last-reading time, the interval, the title, the Pi upload status (last result, queue length), uptime and free heap |
| `GET /api/readings` | The logged rows as `[[iso_time, ph, ec, wt], ...]`, oldest first, copied under a lock |
| `GET /api/settings`, `POST /api/settings` | Interval in minutes (2–720) and title. POST validates, then queues the save for `loop()`. It returns 400 with a reason on bad input |
| `POST /api/command` | Body `{"cmd": ...}`, one of `reading`, `measure_start`, `measure`, `calibrate_start`, `calibrate_ph4`, `calibrate_ph7`, `cancel`, `restart`, `reset`. Returns 202 when accepted, 409 with the busy step when not allowed now, 400 for an unknown command. Results come back through `/api/status` (manual reading, calibration values) |
| `GET /api/pi` | The cached Pi `/stats?window=24h`, `/reports/latest` and `/thresholds` bodies, each with its fetch time and last error |
| `GET /api/ranges`, `PUT /api/ranges`, `DELETE /api/ranges` | GET returns the ranges in use, their source (`device`, `pi` or `builtin`), and the Pi's ranges for comparison. PUT validates and saves device ranges in NVS (namespace `ranges`). DELETE removes them |
| `GET /api/apiconfig`, `POST /api/apiconfig` | Upload settings (URL, device name) as JSON, plus whether a token is set. The token can be written but is never returned |

Range shape, matching the Pi's `thresholds.json` subset:
`{"ph": {"valid": [lo, hi], "target": [lo, hi]}, "ec": {...}, "temp": {...}}`. The target range must sit inside the valid range, and lo < hi. The built-in defaults match `thresholds.example.json` in PiServer: pH valid 2–10, target 5.5–6.5; EC valid 0–5000, target 200–1000; temperature valid 30–99 °F, target 60–80 °F. Drift and noise limits come only from the Pi.

Command mapping to `ReadingCycle`:
- `reading` → `TakeReading`
- `measure_start` → `Hold`
- `measure` → `MeasureManual`
- `calibrate_start` → `Hold`
- `calibrate_ph4` → `MeasurePh4`
- `calibrate_ph7` → `MeasurePh7` (only while holding, after `calibrate_ph4`)
- `cancel` → a new `CycleCommand::Resume`. It stops any measurement, discards an unfinished calibration (as the hold timeout does), and returns the cycle to waiting without taking a reading.
- `restart` → `Restart`
- `reset` → `WipeAndRestart`

`ReadingCycle` also gets new read-only accessors for `/api/status`: the current step and its progress, and the pending pH 4 raw value during calibration. The saved values already come from `ph4()` and `ph7()`.

### Pi cache

`loop()` already sends queued readings to the Pi. Every 5 minutes, and once right after boot, it also fetches `/stats?window=24h&device=<device>`, `/reports/latest?device=<device>` and `/thresholds` with the same URL and token and 3 s timeouts. It fetches one endpoint per pass, so `loop()` never blocks for more than one request.

- Each body is kept as text with a cap: 6 KB for stats, 4 KB for the report, 2 KB for thresholds. A larger body is dropped, with the error "reply too large".
- The cache is swapped under a lock. Handlers copy the text out.
- With no URL or token configured, the cache stays empty with the error "Pi not configured".
- A 404 from `/reports/latest` means "no reports yet", not an error.

### Settings save

`writeFile()` blocks for 2 s, so POST handlers set pending values and `loop()` writes them. The boot guard on `interval0` changes from 12000–720000 ms to 120000–43200000 ms (2–720 min), so saved intervals survive a restart.

### Frontend (`web/`)

- Vite + Preact + TypeScript + uPlot. No CSS framework: one hand-written stylesheet with light and dark colour tokens.
- Polling: `/api/status` every 15 s, every 2 s while busy; `/api/pi` and `/api/ranges` every 60 s; `/api/readings` on load and after each new reading.
- `npm run build` writes `dist/`. `npm run embed`, a Node script, gzips each file in `dist/` into `src/web_assets.h` as PROGMEM byte arrays with path, MIME type and an ETag from the content hash.
- The firmware serves `/` and each asset with `Content-Encoding: gzip`. It answers `If-None-Match` with 304. Assets get a long cache lifetime and `index.html` gets `no-cache`.
- Target size: 40 KB gzipped for the whole app.

### Removed

`/get`, `processor()` and its `%placeholder%` templating, `getDateTime()`'s `%m-1` format, and the HTML, JS and CSS in `data/`. The seed `.txt` files in `data/` stay. The plain-text `/diag` and `/apiconfig` GET pages stay for troubleshooting, and the `/savewifi` setup portal is unchanged.

## Screens

Navigation is a bottom tab bar on phones and a side bar on desktop. Dark mode follows the system setting.

1. **Dashboard**
   - A status line with the last-reading and next-reading times. While busy, a progress bar names the step ("Warming pH probe 12/20 s").
   - Three cards: pH, EC (µS/cm) and temperature (°F). Each shows:
     - The latest value, the device average ± spread, and the Pi's 24 h drift per hour with an arrow.
     - Its colour: green inside target, amber outside target, red outside valid. A red card reads "Sensor fault" with the raw value small.
     - Tags "drifting" and "noisy" when the Pi's flags or limits say so.
   - A latest-review panel: severity badge, headline, summary, trigger flags with evidence, time, and source (model name and duration, or template). It shows the cache age and the error when the Pi data is stale.
   - A "Take reading now" button.
2. **Charts**
   - pH, EC and temperature stacked on a shared time axis from the device log.
   - The target range is shaded, and points outside it are marked.
   - 6 h, 24 h and all-data buttons, drag to zoom, a value on hover or tap, and CSV download.
3. **Measure**: a check reading that isn't saved. Probe-in-solution prompt, Start, progress, then the result and Done.
4. **Calibrate**
   - pH 4 step with progress, showing the pH 4 raw value.
   - pH 7 step with progress.
   - Old and new pH 4 and pH 7 values, then Done.
   - Cancel at every step. Leaving the page ends the hold through the existing 15-minute timeout.
5. **Settings**
   - Interval (2–720 min) and title.
   - The ranges table with its source label, the Pi's values beside it, and Save (device ranges) and Use Pi ranges.
   - Pi connection: URL, device name, token (write-only, shown as set or not set), last upload result and queue length.
   - Restart, with confirmation. Reset data, which requires typing `RESET`.
6. **Diagnostics**
   - Calibration values, the raw pH ADC average and the 20 samples as a small chart, and the last EC raw value.
   - Free heap and uptime.
   - Pi cache fetch times and errors.
   - A link to `/diag`.

## Error handling

| Situation | Behaviour |
|---|---|
| Device stops responding | Banner "Device not responding, retrying". The last values stay on screen, greyed, with their time. Polling continues. |
| Pi unreachable or erroring | Device data unaffected. The review panel and drift show the cache age and the error, or "No Pi data yet" with an empty cache. |
| Command while busy | 409. The page shows the busy step and keeps the button disabled until idle. |
| Invalid settings or ranges | 400 with the reason, shown next to the field. |
| Sensor fault value | Red card with "Sensor fault". The value is still logged. |
| Restart during calibration | The calibration is discarded (existing behaviour). The page sees the cycle back at idle and says "Calibration cancelled". |
| Oversized Pi reply | Dropped, with the error "reply too large". |

## Testing

- **Native (`pio test -e native`)**:
  - JSON builders for status, readings, settings, ranges and the Pi cache view.
  - Range validation and the device, then Pi, then built-in order.
  - Command handling (202, 409, 400) through `FakeHost`.
  - The new `Resume` command and the step/progress accessors.
- **Frontend (Vitest)**:
  - Colour and fault rules, number and time formatting, and CSV output.
  - The measure and calibrate step logic against a fake device client.
- **Local preview**: the Vite dev server with mock endpoints replaying recorded device and Pi replies, including busy, offline, fault and no-Pi cases. Checked in the browser pane at phone and desktop widths.
- **On the device**:
  - OTA upload, then each screen against `/diag`.
  - One saved reading and one measure.
  - Calibrate exercised up to Cancel, unless buffers are at hand.
  - Stop the Pi API and confirm the device keeps sampling and the page shows the stale state.
  - Readings keep arriving on the interval.

## Deployment and size

- Before building the UI, build the current firmware and record its size against the 1,310,720-byte app slot. The 2026-11-04 build was 1,124,400 bytes, before the Pi upload code.
- If the firmware with the embedded site would leave under 60 KB free, stop and choose between a larger app partition (one USB upload) and the LittleFS approach.
- Deploy by OTA only. The old HTML files left in LittleFS are unused and harmless.

## Implementation notes

Deviations from the endpoint table above, made while building:

- `POST /api/settings`, `POST /api/command` and `POST /api/apiconfig` take form-encoded bodies (`interval`, `title`; `cmd`; `url`, `token`, `device`), not JSON, because the async web server reads form fields without buffering a body.
- Device ranges are saved with `POST /api/ranges`, not `PUT`, with twelve flat form fields (`ph_valid_lo` through `temp_target_hi`). `DELETE /api/ranges` is unchanged. Replies stay JSON.
- Four mutexes guard the shared state: `settingsLock`, `readingsLock`, `piCacheLock` and `apiQueueLock`.
- Deployed to the device by OTA on 2026-11-07; the firmware is 1,138,674 bytes of the 1,310,720-byte slot.

## Out of scope

- Device commands from the Pi or the Android app.
- A login on the device page.
- History beyond the device's 200 readings.
- Editing the Pi's ranges from the device page.

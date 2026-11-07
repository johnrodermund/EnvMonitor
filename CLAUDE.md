# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

ESP32 Arduino sketch that logs pH, electrical conductivity and water temperature, and serves a Preact web app over Wi-Fi. All firmware is in `esp32_res_copy_largebox.ino`; the web app source is in `web/` and is embedded in the firmware as `src/web_assets.h`. `data/` is the LittleFS image, which now holds only seed state files (no web pages).

## Build and verify

Run tests on the host with `pio test -e native` (add `-f test_ring_stats` for one suite, `-v` for per-test results). This needs a C++ compiler on PATH. Tests in `test/` cover Arduino-free code in `src/`; keep logic there to make it testable. Frontend tests run with `npm test` in `web/`.

Before every firmware build run `npm run build && npm run embed` in `web/` (Node 26). It gzips `web/dist/` into `src/web_assets.h`, which the sketch serves, and commit the regenerated header. The embed script fails if the gzipped app passes 60 KB (61440 bytes); the current app is about 41 KB. `npm run dev` serves the app against `web/mock/`.

There's no firmware build config or linter, and the board and library versions aren't recorded. Compile with the Arduino toolchain for ESP32. The device has run a build for `lolin_c3_mini` (Arduino core 2.0.14; the board itself is an unconfirmed guess, only the ESP32-C3 chip is certain) with flags `-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1`, and libraries `mathieucarbou/ESPAsyncWebServer`, `matmunk/DS18B20` and `paulstoffregen/OneWire`.

Pinout: DS18B20 on GPIO 8, relay on GPIO 1, pH and EC on GPIO 3 (`sensorPin`). The box was laid out for a D1 mini, so GPIO 3 sits in its A0 position; GPIO 0 is unconnected. Code must not use `A0`, which means GPIO 0 on this board.

- The sketch name doesn't match its folder (`EnvMonitor`). Arduino tools require them to match, so copy or rename before compiling.
- The site is part of the firmware, so a firmware upload (OTA works) updates it. A LittleFS upload of `data/` is needed only for first-time seeding; never do it on a live device. Uploading `data/` replaces the device filesystem, which wipes logged readings, calibration, title and interval with the seed files in `data/`.
- Real verification needs the hardware. If it isn't available, say the change is untested.

## Architecture

Three execution contexts share globals. Four FreeRTOS mutexes guard the shared state the handlers touch (see Locks below):

- `loop()` services OTA, the settle test and the readings API upload, then yields with `delay(2)`. The ESP32-C3 has one core and `loop()` runs at a higher priority than `loop2()`, so without that yield sampling starves and a reading takes minutes.
- `loop2()`, a FreeRTOS task, calls `ReadingCycle::tick()`, which does all sensor work.
- `AsyncWebServer` handlers use only `request()`, `status()` and read accessors on the cycle. Keep slow work out of handlers: they copy data under a lock or queue work for `loop()`, and `loop()` runs at most one slow step per pass (a Pi fetch, an API upload or a settings write).

`ReadingCycle` in `src/reading_cycle.h` owns every reading, calibration and restart transition. Its `CycleHost` interface has two adapters: `DeviceHost` in the sketch and `FakeHost` in the tests. A new command interrupts whatever is running, and an interrupted reading records nothing. pH and EC share one analog input (GPIO 3), and the relay selects which sensor is powered: on for pH during warmup and sampling, off for EC. Calibrate and Measure hold sampling, and a hold abandoned for 15 minutes resumes it and discards any unfinished calibration.

Web app and API: `web/` is a Preact single-page app with hash routes (Dashboard, Charts, Measure, Calibrate, Settings, Diagnostics). `/` and `/assets/*` are served gzipped from `src/web_assets.h` with an ETag (304 on `If-None-Match`; `index.html` is `no-cache`). The JSON API: `GET /api/status`, `/api/readings`, `/api/settings`, `/api/pi`, `/api/ranges`, `/api/apiconfig`, plus `POST /api/settings`, `/api/command` (`cmd=reading|measure_start|measure|calibrate_start|calibrate_ph4|calibrate_ph7|cancel|restart|reset`), `/api/ranges`, `/api/apiconfig` and `DELETE /api/ranges`. POST bodies are form-encoded, not JSON, and ranges are saved by `POST` (not `PUT`) with flat fields such as `ph_valid_lo`. `/api/apiconfig` reports `token_set`, never the token. The plain-text `/diag` and `/apiconfig` pages remain for troubleshooting. JSON builders live in `src/api_json.h`, command dispatch in `src/api_commands.h`.

Pi cache: `PiCache` (`src/pi_cache.h`) holds the last replies from the Pi's `/stats?window=24h`, `/reports/latest` and `/thresholds`. `loop()` fetches one endpoint per pass, one round every 5 minutes, and `/api/pi` serves the cache with each entry's age and last error. Replies over the per-endpoint body cap are refused unread. The dashboard review panel and trend lines come from this cache, so they work with the Pi down but show age and "unreachable".

Device ranges: colour ranges come from the Pi's thresholds by default. `POST /api/ranges` stores device ranges in NVS (namespace `ranges`, key `json`), which override the Pi's for the page only; `DELETE /api/ranges` ("Use Pi ranges") removes them. `/api/ranges` reports the source (`device`, `pi` or `builtin`). Device ranges never reach the Pi.

Locks: `settingsLock` (title, interval and their pending-save flags), `readingsLock` (`readings` and `lastPhUncertainty`), `piCacheLock` and `apiQueueLock`. Take them with `portMAX_DELAY`, hold them only to copy data, and never do I/O while holding one.

`ReadingLog` in `src/reading_log.h` takes one complete reading at a time, so its series stay aligned, and it saves all 200 entries to `/readings.txt` through a store adapter. Boot replays that file's rows to rebuild statistics. If the file is missing, it migrates the old `/phdata.txt`, `/ecdata.txt`, `/wtdata.txt` and `/datetime.txt` files, which is the only remaining use of `src/stats_file.h`.

Readings API upload: `DeviceHost::recordReading()` only queues each finished reading in `apiQueue` (`ReadingQueue` in `src/reading_queue.h`, 24 entries, under `apiQueueLock`). `sendQueuedReading()` in `loop()` POSTs the oldest one as JSON to `<url>/readings`, with 3 s timeouts and a 10 s to 5 min backoff. A 200 or 201 removes it, a 400 or 422 drops it as never acceptable, and anything else keeps it for a retry. When full, the queue drops its oldest reading. Readings taken before NTP sync aren't queued, because their timestamp would be wrong. The URL, token and device name live in NVS (`Preferences` namespace `api`), set by a POST to `/api/apiconfig` or the legacy `/apiconfig` (form fields `url`, `token`, `device`). A GET of either, or `/diag`, shows the status but never the token. The API itself runs on the Raspberry Pi and isn't in this repo.

Charts use uPlot, bundled into the app, so the viewing browser needs no internet access.

## Gotchas

- `setup()` resets `interval0` to 10 minutes if it's outside 120000–43200000 ms (2 minutes to 12 hours), matching the 2–720 minute range of `POST /api/settings`.
- `writeFile()` blocks for 2 s per file. It's used for settings, always from `loop()`; readings go through `LittleFsStore`.
- If `LittleFS.begin()` fails, `setup()` returns early, leaving no web server, OTA or sampling task.
- `ph4` and `ph7` hold raw ADC readings for the two-point calibration, not pH values.
- NTP settings and UTC offset are hardcoded at the top of the sketch.
- Wi-Fi credentials live in NVS (`Preferences` namespace `wifi`), not in source or `data/`. With none saved, or if the saved network fails at boot, `runWifiSetupPortal()` opens the `EnvMonitor-Setup` access point at 192.168.4.1. It restarts after a save, or after 5 minutes if a network is saved. No sampling or OTA runs during the portal.

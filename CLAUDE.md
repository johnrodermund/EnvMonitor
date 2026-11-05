# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

ESP32 Arduino sketch that logs pH, electrical conductivity and water temperature, and serves a dashboard over Wi-Fi. All firmware is in `esp32_res_copy_largebox.ino`; `data/` is the LittleFS image (web UI plus persisted state).

## Build and verify

Run tests on the host with `pio test -e native` (add `-f test_ring_stats` for one suite, `-v` for per-test results). This needs a C++ compiler on PATH. Tests in `test/` cover Arduino-free code in `src/`; keep logic there to make it testable.

There's no firmware build config or linter, and the board and library versions aren't recorded. Compile with the Arduino toolchain for ESP32. The device has run a build for `lolin_c3_mini` (Arduino core 2.0.14; the board itself is an unconfirmed guess, only the ESP32-C3 chip is certain) with flags `-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1`, and libraries `mathieucarbou/ESPAsyncWebServer`, `matmunk/DS18B20` and `paulstoffregen/OneWire`.

Pinout: DS18B20 on GPIO 8, relay on GPIO 1, pH and EC on GPIO 3 (`sensorPin`). The box was laid out for a D1 mini, so GPIO 3 sits in its A0 position; GPIO 0 is unconnected. Code must not use `A0`, which means GPIO 0 on this board.

- The sketch name doesn't match its folder (`EnvMonitor`). Arduino tools require them to match, so copy or rename before compiling.
- A firmware upload and a LittleFS upload of `data/` are separate steps. Both are also possible over ArduinoOTA.
- Uploading `data/` replaces the device filesystem, which wipes logged readings, calibration, title and interval with the seed files in `data/`.
- Real verification needs the hardware. If it isn't available, say the change is untested.

## Architecture

Three execution contexts share globals with no locking:

- `loop()` services OTA, the settle test and the readings API upload, then yields with `delay(2)`. The ESP32-C3 has one core and `loop()` runs at a higher priority than `loop2()`, so without that yield sampling starves and a reading takes minutes.
- `loop2()`, a FreeRTOS task, calls `ReadingCycle::tick()`, which does all sensor work.
- `AsyncWebServer` handlers use only `request()`, `status()` and `setInterval()` on the cycle. Keep slow work out of handlers. The dashboard still reads `ReadingLog` from a handler with no lock while the task appends.

`ReadingCycle` in `src/reading_cycle.h` owns every reading, calibration and restart transition. Its `CycleHost` interface has two adapters: `DeviceHost` in the sketch and `FakeHost` in the tests. A new command interrupts whatever is running, and an interrupted reading records nothing. pH and EC share one analog input (GPIO 3), and the relay selects which sensor is powered: on for pH during warmup and sampling, off for EC. Wizards hold sampling, and a hold abandoned for 15 minutes resumes it and discards any unfinished calibration.

`/get` is the single endpoint for settings and multi-page wizards, dispatched on which query parameter is present. Wizard pages (`calibrate*.html`, `waiting.html`) move forward by form submits to `/get`, and `waiting.html` polls until the cycle stops being busy.

`ReadingLog` in `src/reading_log.h` takes one complete reading at a time, so its series stay aligned, and it saves all 200 entries to `/readings.txt` through a store adapter. Boot replays that file's rows to rebuild statistics. If the file is missing, it migrates the old `/phdata.txt`, `/ecdata.txt`, `/wtdata.txt` and `/datetime.txt` files, which is the only remaining use of `src/stats_file.h`.

Readings API upload: `DeviceHost::recordReading()` only queues each finished reading in `apiQueue` (`ReadingQueue` in `src/reading_queue.h`, 24 entries, under `apiQueueLock`). `sendQueuedReading()` in `loop()` POSTs the oldest one as JSON to `<url>/readings`, with 3 s timeouts and a 10 s to 5 min backoff. A 200 or 201 removes it, a 400 or 422 drops it as never acceptable, and anything else keeps it for a retry. When full, the queue drops its oldest reading. Readings taken before NTP sync aren't queued, because their timestamp would be wrong. The URL, token and device name live in NVS (`Preferences` namespace `api`), set by a POST to `/apiconfig` (form fields `url`, `token`, `device`). A GET of `/apiconfig`, or `/diag`, shows the status but never the token. The API itself runs on the Raspberry Pi and isn't in this repo.

Templating: `processor()` fills `%name_html%` placeholders in `index.html`, `index.js` and `update.html`, so a literal `%` in those files must be written `%%`. `%datatable0_html%` expands to a JS array literal of `new Date(...)` rows. That is why `getDateTime()` formats the month as the text `%m-1`, which the browser evaluates. Charts load from gstatic, so the viewing browser needs internet access.

## Gotchas

- Every `/get` request reaches the final `request->redirect("/")`, even after an earlier `send()`. The trailing `else` covers only `takenewreading1`. Check this when changing wizard responses.
- `setup()` resets `interval0` to 10 minutes if it's outside 12000–720000 ms, so a setting above 12 minutes is lost on reboot.
- `writeFile()` blocks for 2 s per file. It's used for settings; readings go through `LittleFsStore`.
- If `LittleFS.begin()` fails, `setup()` returns early, leaving no web server, OTA or sampling task.
- `ph4` and `ph7` hold raw ADC readings for the two-point calibration, not pH values.
- NTP settings and UTC offset are hardcoded at the top of the sketch.
- Wi-Fi credentials live in NVS (`Preferences` namespace `wifi`), not in source or `data/`. With none saved, or if the saved network fails at boot, `runWifiSetupPortal()` opens the `EnvMonitor-Setup` access point at 192.168.4.1. It restarts after a save, or after 5 minutes if a network is saved. No sampling or OTA runs during the portal.

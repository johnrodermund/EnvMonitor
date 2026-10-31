# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

ESP32 Arduino sketch that logs pH, electrical conductivity and water temperature, and serves a dashboard over Wi-Fi. All firmware is in `esp32_res_copy_largebox.ino`; `data/` is the LittleFS image (web UI plus persisted state).

## Build and verify

Run tests on the host with `pio test -e native` (add `-f test_ring_stats` for one suite, `-v` for per-test results). This needs a C++ compiler on PATH. Tests in `test/` cover Arduino-free code in `src/`; keep logic there to make it testable.

There's no firmware build config or linter, and the board and library versions aren't recorded. Compile with the Arduino toolchain for ESP32. It has compiled for `esp32-c3-devkitm-1` (Arduino core 2.0.14, a guess from the pin use) with `mathieucarbou/ESPAsyncWebServer`, `matmunk/DS18B20` and `paulstoffregen/OneWire`.

- The sketch name doesn't match its folder (`EnvMonitor`). Arduino tools require them to match, so copy or rename before compiling.
- A firmware upload and a LittleFS upload of `data/` are separate steps. Both are also possible over ArduinoOTA.
- Uploading `data/` replaces the device filesystem, which wipes logged readings, calibration, title and interval with the seed files in `data/`.
- Real verification needs the hardware. If it isn't available, say the change is untested.

## Architecture

Three execution contexts share globals with no locking:

- `loop()` only services OTA.
- `loop2()`, a FreeRTOS task on core 0, runs the `STATE` machine and does all sensor work.
- `AsyncWebServer` handlers change behaviour by writing `STATE` and `PHSTATE`. `loop2` then acts on them. Keep slow work out of handlers.

`STATE` values: `A` idle until `interval0`, `B` read temperature and EC, `C` read pH then save, `F`/`G` calibrate pH 4 / pH 7, `H` manual pH, `R` restart, `E` wipe data then restart, `W` hold with no sampling. `PHSTATE` is the nested pH sequence: `A` relay on, `B` 20 s warmup, `C` take 20 ADC samples, `D` done. The relay is on only during pH sampling, and EC is read with it off.

`/get` is the single endpoint for settings and multi-page wizards, dispatched on which query parameter is present. Wizard pages (`calibrate*.html`, `waiting.html`) move forward by form submits to `/get`, and `waiting.html` polls until `STATE` changes.

Data flow: `RingStats` in `src/ring_stats.h` is a ring buffer with running mean and variance, and `src/stats_file.h` defines its file format, rejecting empty or corrupt files so boot starts empty. The sketch's `dataset` class ties these together for 200 entries. `phdata`, `ecdata`, `wtdata` and `datetimearray` are parallel, and timestamps are indexed by `phdata.index`, so all of them must advance together. `saveAllData()` writes `/<id>.txt` and `/datetime.txt`, and `restoreAllData()` reads them plus the settings files at boot.

Templating: `processor()` fills `%name_html%` placeholders in `index.html`, `index.js` and `update.html`, so a literal `%` in those files must be written `%%`. `%datatable0_html%` expands to a JS array literal of `new Date(...)` rows. That is why `getDateTime()` formats the month as the text `%m-1`, which the browser evaluates. Charts load from gstatic, so the viewing browser needs internet access.

## Gotchas

- Every `/get` request reaches the final `request->redirect("/")`, even after an earlier `send()`. The trailing `else` covers only `takenewreading1`. Check this when changing wizard responses.
- `W` stops sampling. Abandoning a calibrate or `takeph` wizard leaves the device idle until something else sets `STATE`.
- `setup()` resets `interval0` to 10 minutes if it's outside 12000–720000 ms, so a setting above 12 minutes is lost on reboot.
- `writeFile()` blocks for 2 s per file, so `saveAllData()` stalls `loop2` for at least 8 s.
- If `LittleFS.begin()` fails, `setup()` returns early, leaving no web server, OTA or sampling task.
- `ph4` and `ph7` hold raw ADC readings for the two-point calibration, not pH values.
- `restoreDateTime()` has no bounds check, and a reading taken before NTP sync saves an empty timestamp that shifts every later entry on restore.
- NTP settings and UTC offset are hardcoded at the top of the sketch.
- Wi-Fi credentials live in NVS (`Preferences` namespace `wifi`), not in source or `data/`. With none saved, or if the saved network fails at boot, `runWifiSetupPortal()` opens the `EnvMonitor-Setup` access point at 192.168.4.1. It restarts after a save, or after 5 minutes if a network is saved. No sampling or OTA runs during the portal.

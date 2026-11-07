# EnvMonitor

ESP32-C3 firmware that logs pH, electrical conductivity (EC) and water temperature, and serves a web app with a dashboard, charts, measure and calibrate screens and settings over Wi-Fi.

## Hardware

| Signal | GPIO |
|---|---|
| pH and EC analog output (shared), in the D1 mini A0 position | 3 |
| Relay: on powers the pH sensor, off powers the EC sensor | 1 |
| DS18B20 temperature sensor | 8 |

The firmware has run on an ESP32-C3 built as `lolin_c3_mini`. Other ESP32-C3 boards should work with the same pins.

## Building

The firmware is `esp32_res_copy_largebox.ino`, built with the Arduino framework for ESP32 (core 2.0.14 tested). Arduino IDE needs the sketch in a folder of the same name, so copy or rename it first.

Libraries:

- `mathieucarbou/ESPAsyncWebServer`
- `matmunk/DS18B20`
- `paulstoffregen/OneWire`

For USB Serial output on boards with native USB, build with `-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1`.

The web app is a Preact single-page app in `web/` and is built into the firmware. Before building the firmware, install Node (26 tested) and run:

```bash
cd web
npm install
npm run build && npm run embed
```

`npm run embed` writes `src/web_assets.h`, which holds the gzipped app. Uploading the firmware, including over the network, updates the site, so there is no separate web upload. `npm run dev` serves the app with mock data for working on the UI.

`data/` is the LittleFS image with the seed state files (readings, calibration, title and interval). Upload it once with a LittleFS upload tool on a new device. Uploading it again replaces the logged readings, calibration and settings on the device.

## Tests

The logic in `src/` has no Arduino dependencies and runs on your computer with PlatformIO and a C++ compiler:

```bash
pio test -e native
```

## First run

1. With no saved network, the device opens a Wi-Fi access point called `EnvMonitor-Setup`.
2. Join it, open `http://192.168.4.1` and enter your network name and password.
3. The device restarts, joins your network and serves the dashboard on port 80.

Firmware updates can then go over the network with ArduinoOTA.

Calibrate pH from the Calibrate screen with pH 4 and pH 7 buffer solutions. A reading takes about 25 seconds because the pH sensor warms up for 20.

## Sending readings to a server

The device can also send each reading as JSON to an HTTP API, for long-term storage. Readings wait in a small queue (24 readings) and are retried with backoff while the server is unreachable. To turn it on, save the server address, an API token and a device name:

```bash
curl --data-urlencode "url=http://<server>:8000" --data-urlencode "token=<token>" --data-urlencode "device=envmonitor-1" http://<device-ip>/apiconfig
```

Open `http://<device-ip>/apiconfig` (or the Settings and Diagnostics screens) to see the queue and the last HTTP result. The token is kept in the device's NVS storage and is never shown.

## Security

This is built for a trusted home network:

- The dashboard and settings, including restart and data reset, need no login.
- ArduinoOTA has no password, so anyone on the network can replace the firmware.
- The `EnvMonitor-Setup` access point is open while waiting for Wi-Fi details.
- Anyone on the network can change the API settings at `/api/apiconfig`, and the API token travels over plain HTTP.

Don't expose the device to the internet.

## License

MIT, see [LICENSE](LICENSE).

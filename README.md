# EnvMonitor

ESP32-C3 firmware that logs pH, electrical conductivity (EC) and water temperature, and serves a dashboard with charts over Wi-Fi.

## Hardware

| Signal | GPIO |
|---|---|
| pH and EC analog output (shared) | 0 |
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

`data/` is the LittleFS image with the web pages. Upload it once with a LittleFS upload tool. Uploading it again replaces the logged readings, calibration and settings on the device.

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

Calibrate pH from Settings with pH 4 and pH 7 buffer solutions. A reading takes about 25 seconds because the pH sensor warms up for 20.

## Security

This is built for a trusted home network:

- The dashboard and settings, including restart and data reset, need no login.
- ArduinoOTA has no password, so anyone on the network can replace the firmware.
- The `EnvMonitor-Setup` access point is open while waiting for Wi-Fi details.

Don't expose the device to the internet.

## License

MIT, see [LICENSE](LICENSE).

# SAFE ROAD firmware

ESP32-S3 N16R8 firmware for the OV5640 camera, SH1106 OLED and passive buzzer.
Face monitoring and buzzer alerts run on the ESP32. The dashboard provides
camera preview, calibration, Wi-Fi setup, email and webhook settings.

From an ESP-IDF 5.5 terminal in the repository root:

```powershell
idf.py -p COM11 flash monitor
```

Use your board's serial port. To view serial logs only, use
`idf.py -p COM11 monitor`. Press Ctrl+] to exit the monitor.

Connect to the device hotspot and open http://192.168.4.1. The default hotspot
is SAFE_ROAD_xxxxxx with password 12345678. Calibrate from the dashboard if
needed. Saved calibration and settings use the same flash partitions as the
previous smaller firmware. Use Wi-Fi & notifications to configure the hotspot,
email and webhooks, and the events and hold times that trigger alerts.

| Module pin | ESP32-S3 pin |
| --- | --- |
| OLED SDA | GPIO 41 |
| OLED SCK/SCL | GPIO 42 |
| Buzzer I/O | GPIO 21 |
| Hotspot reset button | GPIO 14 to GND |
| Both VCC | 3V3 |
| Both GND | GND |

This prototype provides experimental camera-based alerts. Firmware source and
runtime model files live in main/, bundled ESP-IDF components in
components/, and host checks in tests/. Model conversion experiments
are available on the main branch.

Use a normally open momentary button between GPIO 14 and GND. After boot,
hold it for five seconds to restore SAFE_ROAD_xxxxxx / 12345678 and restart.
This resets only the hotspot credentials.

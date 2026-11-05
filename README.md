# SAFE ROAD firmware

[Full setup, wiring, troubleshooting, and source code guide](docs/GUIDE.md)

[Gmail SMTP and MacroDroid webhook setup](docs/NOTIFICATIONS.md)

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

```mermaid
flowchart LR
  subgraph ESP["ESP32-S3"]
    P["3V3"]
    G["GND"]
    D["GPIO 41"]
    C["GPIO 42"]
    B["GPIO 21"]
    R["GPIO 14"]
  end
  subgraph OLED["SH1106 OLED"]
    OV["VCC"]
    OG["GND"]
    OD["SDA"]
    OC["SCK / SCL"]
  end
  subgraph BUZZ["Passive buzzer module"]
    BV["VCC"]
    BG["GND"]
    BI["I/O / IN"]
  end
  subgraph RESET["Normally open reset button"]
    RT["Terminal 1"]
    RG["Terminal 2"]
  end
  P --- OV
  P --- BV
  G --- OG
  G --- BG
  G --- RG
  D --- OD
  C --- OC
  B --- BI
  R --- RT
```

This prototype provides experimental camera-based alerts. Firmware source and
runtime model files live in main/, bundled ESP-IDF components in
components/, and host checks in tests/. Model conversion experiments
are available on the main branch.

Use a normally open momentary button between GPIO 14 and GND. After boot,
hold it for five seconds to restore SAFE_ROAD_xxxxxx / 12345678 and restart.
This resets only the hotspot credentials.

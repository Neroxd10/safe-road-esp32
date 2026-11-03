# S.A.F.E. R.O.A.D. — ESP32-S3 offline monitoring

The current tested experiment is **[the smaller 468-landmark firmware](experiments/v7_small/README.md)**.
It runs camera capture and inference on an ESP32-S3 N16R8 with OV5640, without a
phone, PC browser or internet connection. The browser provides setup, calibration
and status. This is an experimental eye/mouth/head monitoring prototype.

From an ESP-IDF 5.5 terminal at the repository root:

```powershell
idf.py -C experiments/v7_small -p COM11 flash monitor
```

Use your serial port. Connect to the device hotspot (`SAFE_ROAD_xxxxxx`, default
setup key `12345678`), open `http://192.168.4.1`, then complete all nine calibration
poses. Calibration waits for a tap and saves across restart. The smaller model
uses its own calibration, separate from V7. It uses eye/mouth geometry rather
than expression blendshapes; real-world accuracy is not established.

Reported board timing: approximately **503–536 ms** per tracked reading, with
normal updates about every 0.6 seconds. Frames that rerun the detector take longer
(the supplied example was 1,253 ms). This is not a validated drowsiness alarm.

The previous full V7 conversion lives in `experiments/v7_live/`; restore it with
`idf.py -C experiments/v7_live -p COM11 flash monitor`. Conversion tooling and
validation reports are included. Root `managed_components/` is vendored to retain
the tested runtime and dependency versions; original dependency licenses remain
in their directories. Build directories, Python environments, personal training
samples and local firmware backups are excluded from Git.

The earlier standalone firmware and its instructions are retained below.

---

# S.A.F.E. R.O.A.D. â€” Standalone Stage 4 Wi-Fi Dashboard

**ESP32-S3 N16R8 (16 MB flash + 8 MB octal PSRAM), OV5640, ESP-IDF 5.5.x.** This project builds on the real Stage 3 experimental face/eye/mouth/head monitoring firmware. The ESP32 still performs all inference on-device. The browser is only a display and configuration interface; detection works without any browser or Wi-Fi client connected.

## Included
- Firmware-hosted, mobile-friendly website. No SD card, internet, CDN, or external JS required.
- Private, per-device 2.4 GHz setup hotspot `SAFE_ROAD_xxxxxx` with WPA2 password `12345678` shown on the **serial monitor** each boot.
- Wi-Fi connection page: enter the setup access key (identical to the device hotspot password), choose a scanned network or type SSID, enter password, press **Save & connect**. STA credentials are stored in ESP32 NVS across reboot, via `esp_wifi_set_config`.
- Setup AP remains available at `http://192.168.4.1` even if home Wi-Fi doesn't connect; once connected the dashboard is also reachable at the **station IP** shown on serial and in the website.
- Actual Stage 3 statuses: eyes, mouth, head down, left/right away, left/right tilt, face presence, hold timers, pose metrics, nine-stage calibration progress. Restart calibration from website using setup key.
- Latest camera JPEG image updated roughly once per second, copied from the **same** camera frame used for on-device AI. It is a low-rate preview, **not** continuous MJPEG video.

## Build and run on Windows

1. Back up V7, Stage 2, and Stage 3. Extract to a **short** project path, e.g. `C:\ESP32\Stage4` (put CMakeLists.txt directly inside Stage4).
2. Open **ESP-IDF 5.5 PowerShell**:

   ```powershell
   cd C:\ESP32\Stage4
   idf.py set-target esp32s3
   idf.py menuconfig
   idf.py build
   ```

3. Confirm **16 MB flash**, **Octal 80 MHz PSRAM**, **custom `partitions.csv`**. Only after the camera's previous overheating issue has been resolved:

   ```powershell
   idf.py -p COM5 flash monitor
   ```

   Replace COM5 with your real USB serial port.

4. In Serial Monitor, find:

   ```text
   Wi-Fi name: SAFE_ROAD_......
   Wi-Fi password + dashboard setup key: ............
   Dashboard: http://192.168.4.1
   ```

5. On a phone or PC, connect to the listed hotspot; open **http://192.168.4.1** in a browser. Password and setup key are the same. You may see a **no internet connection** warning; stay connected.
6. In Wi-Fi Connection, supply setup access key and select or enter your **2.4 GHz** home Wi-Fi details. After association, monitor the serial log (or the dashboard) for the new station IP. Open `http://<station-ip>` on devices on the same trusted LAN.
7. Calibration waits for you: enter the setup key and tap **Calibrate**. After each pose, tap **Ready** for the next capture; each starts with a 4-second countdown. Completed calibration saves automatically and monitoring resumes after reboot without Wi-Fi. Complete all **9 guided stages**, preparing and holding each posture. You may restart all stages in the dashboard. After all nine finish, individual Recalibrate buttons repeat only the selected pose, keeping other captures. Each allows 4 seconds to prepare and collects 12 valid samples; monitoring then resumes and quality is checked again.

## Safety & security

- **Bench-test research only.** Eye patch and mouth patch matching plus sparse 2D pose geometry are experimental. No validated driver-sleepiness detection, no buzzer, no SMS, and no automatic safety action.
- **Hardware:** The OV5640 previously became dangerously hot. Resolve the hardware problem before operating the board/camera. Build without powering it.
- **Local network only.** The dashboard uses unencrypted HTTP, not HTTPS; setup keys and Wi-Fi credentials transit HTTP. Configure on the protected device AP or trusted private LAN only. Never port-forward or expose it on the internet. The hotspot password and dashboard setup key are fixed to `12345678`.
- The station password is kept in ESP32 NVS (not shown in the dashboard); default NVS is not at-rest encrypted unless flash encryption is separately configured. For high-assurance deployments, add HTTPS, access controls for read-only endpoints, encrypted NVS/flash, secure boot and lifecycle provisioning.
- Data on status endpoints and camera images is **not authenticated** on the local network; anyone who can access the device can view them. Setup access key is required for changing Wi-Fi or restarting calibration, but GET endpoints are read-only/public within the network.
- **No SD card required.** Firmware includes the HTML and model weights. Wi-Fi station details and setup hotspot pass survive reboot. Completed driver calibration is saved as a versioned NVS blob and restored automatically after reboot. An interrupted recalibration keeps the previously saved set. Erasing NVS removes calibration.
- The camera and Wi-Fi share CPU/RAM and RF bandwidth; JPEG snapshots may reduce frame rate. Status polling is ~1.3s; preview ~1.8s.

## Diagnostics

- `idf.py build` fails: send the **first** `error:` or `FAILED:` output plus 15 lines above/below. This code has not been compiled against your Windows ESP-IDF or run on the board. The API usage is based on ESP-IDF 5.5.x.
- CMake path-length warning: extract at `C:\ESP32\Stage4`, delete `build/`, rebuild.
- Cannot connect to AP: verify you used password displayed on **this** boot; this firmware overrides older saved hotspot passwords with `12345678`. Some devices may prefer 2.4 GHz / disable auto cellular internet switching.
- Can open setup hotspot but not home Wi-Fi IP: ensure client is on same local network and router isolation is off. Check station IP on serial and make sure the home router is 2.4 GHz compatible.
- Calibration quality OFF/UNRELIABLE: redo guided calibration while camera remains stable, face well lit and clearly visible.

## Source project structure

```text
Stage4/
  CMakeLists.txt
  sdkconfig.defaults
  partitions.csv
  README.md
  main/
    CMakeLists.txt
    idf_component.yml
    main.cpp
    index.html
```

## Experimental tiny eye/mouth AI

See [tools/TINY_AI.md](tools/TINY_AI.md) for sample collection, held-out training checks and flashing trained weights. No pretrained eye/mouth weights are included; firmware uses the existing matching engine until your captured examples pass validation. The dashboard shows the active engine. This is a separate compact classifier experiment, not a converted V7 model.

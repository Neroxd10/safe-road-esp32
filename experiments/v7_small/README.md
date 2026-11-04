# Smaller landmark experiment

This separate firmware retains the working V7 app in `../v7_live/`.
It uses the official MediaPipe non-attention face mesh model: 192x192 RGB [0,1]
input, 468 XYZ landmarks and a sigmoid face-presence logit. The existing V7
128x128 detector, camera orientation, Wi-Fi hotspot, dashboard and nine-step
manual calibration flow are retained. Inference continues without a browser.

The model omits iris landmarks and the expression model is removed. Eye closure
uses both calibrated eye-aspect ratios; mouth opening uses calibrated lip
geometry. Blink/jaw blendshape scores are unavailable and are not fabricated.
These geometry labels are experimental, and closed-eye, mouth-open and pose
performance must be evaluated on the board. This is a different pretrained model,
not a lossless compression of V7 or a validated drowsiness monitor.

## Checks completed

- Original TFLite vs converted ONNX on 19 inputs: max difference <0.000153.
- INT8 export failed equivalence checks and is not selected.
- Selected INT16 export: max coordinate difference <0.136 crop pixels over 19
  inputs; eye/mouth geometry and face-presence decisions matched the float model.
- Inputs comprise 17 crops/augmentations of one public portrait, plus all-black
  and all-white images. This tests conversion/quantization, not diverse real-world
  eye-closure or yawn accuracy. Constant images were rejected by the landmark
  presence head in both float and INT16 models.
- Conv workload: 34,979,040 MACs vs V7's 112,863,488 (about 31%). This excludes
  other operations and is not a board latency measurement.
- Landmark ESP-DL artifact: 1,536,256 bytes vs V7's 3,169,040.
- Geometry decisions and dashboard state tests pass on the host.
- Firmware startup still requires real-face embedded output checks on both cores.

## Flash and compare

In the ESP-IDF terminal at the project root, exit monitor with Ctrl+] then:

```powershell
idf.py -C experiments/v7_small -p COM11 flash monitor
```

No fullclean or NVS erase is needed. Check `Real-face landmark self-test: ESP_OK`,
then compare `PERF total=...` with the previous 1,590 ms baseline. Hardware startup
checks and speed remain pending until the board is flashed.

Tap Calibrate and complete all nine poses. This experiment saves to `small_cal`
in NVS, so it starts without a prior calibration and saves its own after capture.
The working V7 `v7_cal` calibration is preserved. Calibration is never automatic.

Restore the working V7 firmware with:

```powershell
idf.py -C experiments/v7_live -p COM11 flash monitor
```

## Reproduce

Use the existing conversion venv in `../v7_direct/.venv/`:
`python experiments/v7_small/convert_model.py` then
`python experiments/v7_small/validate_export.py` from the root. Keep the official
model in `models/face_landmark.tflite`; validation reads the portrait crop produced
by the V7 experiment. See validation.json for results and build provenance.

Official references:
- https://storage.googleapis.com/mediapipe-assets/face_landmark.tflite
- https://github.com/google-ai-edge/mediapipe/blob/master/mediapipe/modules/face_landmark/face_landmark_cpu.pbtxt
- https://github.com/google-ai-edge/mediapipe/blob/master/mediapipe/modules/face_landmark/tensors_to_face_landmarks.pbtxt


## Gmail email and webhook notifications

The simpler dashboard keeps per-pose recalibration and raw measurements collapsed.
Open **Wi-Fi & notifications** to connect the device to internet Wi-Fi. Choose **Load saved settings**; no dashboard access key is required.

For Gmail, enable Gmail email, enter the sender address and one recipient, and
enter a 16-character Google app password. Enable 2-Step Verification on the Google
account first, then generate the app password at
https://myaccount.google.com/apppasswords . Ordinary account passwords are not the
SMTP app-password credential. Gmail is fixed to smtp.gmail.com:465 with verified
TLS. Workspace accounts may have administrator restrictions on app passwords.

Save, wait for internet clock sync, and press **Test email**. Watch the delivery
status: **Email accepted by Gmail** confirms SMTP acceptance, not delivery to the
recipient inbox. Check the inbox/spam folder. Real Gmail delivery has not been
verified in development because no real account credentials were supplied.

Webhook delivery is optional and can be enabled later with an HTTPS URL. POST
JSON contains `device`, `event`, `duration_ms`, `uptime_ms`, `experimental`, `text`
and `content` fields; text/content carry the same human-readable description.
Event names are `eyes_closed`, `mouth_open`, `head_down`, `looking_away`,
`head_tilt`, or `test`. No camera images or credentials are sent in the payload.
Only HTTP 2xx responses count as accepted; redirects are not followed.

Choose event types and a 30–3600 second cooldown (default 60) per type. A sustained
event sends at most once and must clear before another notification. Automatic
thresholds are 1.5 s for eyes, 3 s for mouth and 2.5 s for head/away/tilt. Missing
or invalid faces are not sleeping events. Calibration does not emit detections.

Delivery runs on a lower-priority task with a six-item queue and timeouts; it
never performs SMTP/HTTPS in the inference task. Offline observations are skipped
and queued items older than 30 seconds are dropped. Failed sends are not retried
because a lost SMTP acknowledgement can leave delivery ambiguous. Internet loss
does not stop camera monitoring; delivery status reports failures separately.

Settings persist in the separate `sr_alerts` NVS namespace, with channels disabled
until configured. Passwords and webhook URLs are never returned to the browser;
blank fields keep saved secrets. Loading, saving and testing do not require an access key. The local settings page is HTTP, so use the protected hotspot or a trusted
LAN. NVS stores these credentials; this prototype does not enable flash encryption.

TLS verifies server certificates against the ESP-IDF certificate bundle. SNTP
sets the clock for certificate validity. TLS buffers use PSRAM to preserve
internal camera/network memory. The existing `small_cal` calibration and model
weights are unchanged. Board delivery, persistence across reboot and concurrent
inference timing still need a hardware test with the configured account.

Host checks:
`tests/alerts_test.cpp` exercises event rearming/cooldowns, address validation,
SMTP multiline responses, authentication failures, body escaping and acceptance;
`tests/notifications_ui_test.cjs` exercises keyless settings, preserving secrets,
password normalization, errors and offline status. Desktop/mobile layouts were
also rendered in a headless browser. A successful build does not prove external
delivery.

References:
- https://support.google.com/accounts/answer/185833
- https://knowledge.workspace.google.com/admin/gmail/send-email-from-a-printer-scanner-or-app
- https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32/api-reference/protocols/esp_http_client.html

Calibration, Wi-Fi and notification controls do not require a dashboard access key. The hotspot Wi-Fi password remains 12345678.


## Hotspot internet sharing

IPv4 NAT now shares the station's Wi-Fi uplink with clients on SAFE_ROAD. The
hotspot remains 192.168.4.1 and keeps its Wi-Fi password. Forwarding is enabled
only after the station obtains an IPv4 address, and disabled on disconnect or
lost IP. DHCP advertises upstream DNS, with 1.1.1.1 as a fallback if no IPv4 DNS
was supplied. No inbound port mappings are enabled.

Flash the smaller firmware normally. Connect it to your internet Wi-Fi, wait for
`HOTSPOT SHARING READY` in serial or the sharing-ready dashboard message, then
turn your phone's Wi-Fi off/on or disconnect/reconnect SAFE_ROAD once to renew
its DNS information. Open a normal website to test. This uses IPv4 NAT and does
not bypass a router's captive portal, unavailable uplink, or access restrictions.
A sharing-ready status reports the configured route, not a verified internet test.

If home Wi-Fi also uses the 192.168.4.x subnet, sharing stays disabled and the
page reports the conflict; use a different upstream network/subnet. The local
calibration/dashboard and offline inference remain available. Phone browsing
shares radio/CPU resources with inference, so hardware latency/throughput need
measurement. The user confirmed phone browsing works through the hotspot. Throughput and inference impact have not been measured.

`/api/status` identifies this version as `small-notify-nat-1` and includes
`internet_sharing` and `sharing_status`. The notification POST route remains
`/api/notifications`; Gmail uses the station connection independently of phone
internet sharing. A 404 from that local route is not a Gmail/internet failure.

Host tests cover overlapping IPv4 subnets and the dashboard sharing messages.


MacroDroid webhook setup: use the complete Webhook (URL) trigger address,
including its identifier, for example
`https://trigger.macrodroid.com/<device-id>/safe_road`. The base device URL alone
returns 404. Save and enable the macro with the same identifier and a notification
action, then save the complete URL in the dashboard and test. The user confirmed
this works. No actual device webhook URL or Gmail credential is stored here.


## SH1106 OLED and buzzer

The selected 1.3-inch 128x64 I2C display is SH1106. The selected low-trigger
buzzer module is passive. Firmware defaults are below; verify these GPIOs are
exposed and unused on your actual board before wiring. Disconnect power first.

| Module pin | ESP32-S3 connection |
| --- | --- |
| OLED GND | GND |
| OLED VCC | 3V3 |
| OLED SDA | GPIO 1 |
| OLED SCK / SCL | GPIO 2 |
| Passive buzzer GND | GND |
| Passive buzzer VCC | 3V3, only if the actual module is rated to operate at 3.3 V |
| Passive buzzer I/O / IN | GPIO 21 |

Match the printed labels; OLED header order varies. Do not connect 5 V to GPIO.
The screenshot does not establish the buzzer board's supply rating: confirm the
actual module's voltage before powering it. A 5 V-only module may require a
transistor/level interface; its I/O must not pull the ESP32 signal above 3.3 V.
These instructions apply to the transistor module, not a bare two-wire buzzer.

OLED uses I2C controller 0 at 100 kHz and probes 0x3c then 0x3d. Camera SCCB uses
controller 1 (GPIO 4/5). SH1106 page addressing includes the two-column offset.
Power the display at 3.3 V so its pullups do not raise SDA/SCL above GPIO voltage.
The display shows Wi-Fi, recently verified internet, monitoring/no-face status,
calibration stage, eye/mouth state and sustained alert type. It refreshes roughly
every 750 ms. The internet check uses a certificate-verified HTTPS HEAD request
to Google's generate_204 endpoint every 30 seconds; a failed/blocked probe means
unverified, not proof that every internet service is unavailable.

The buzzer uses LEDC timer 1/channel 1 at 2400 Hz for the selected passive module;
the camera retains timer 0/channel 0. The idle signal is HIGH for low-trigger
modules. Active-module mode is selectable in `idf.py menuconfig` under SAFE ROAD
OLED and buzzer, with the GPIO settings. Do not flash GPIO assignments that are
already used by another peripheral or reserved by your board.

Local sound is independent of notification settings/internet. It repeats short
bursts while a valid calibrated detection exceeds 1.5 s eyes closed, 3 s mouth
open, or 2.5 s head down/away/tilt. Calibration, missing faces, and telemetry older
than 2.5 s silence it. Local hardware uses a separate task; network checks run
in another low-priority task, so SMTP/HTTP never delays buzzer pulse scheduling.
The OLED is optional: a missing display logs a warning and monitoring continues.
If the display is unplugged while running, reboot after reconnecting to retry.

Build and policy tests do not confirm physical wiring, display orientation,
volume, or module supply compatibility. Hardware operation and concurrent timing
still require bench testing. No audible startup test is performed automatically.


## Editable detection hold times

Under Wi-Fi & notifications, load saved settings, choose event types, set each
hold time in seconds (0.5–60), and save notifications. These event selections and
hold times now control the local buzzer/OLED alert indication and email/webhook
notifications together. Unchecking all events disables detection alerts while
monitoring continues. The remote-notification cooldown remains 30–3600 seconds;
it does not change the local buzzer pulse pattern.

Times persist in `sr_alerts/hold_ms`. Existing saved credentials and channel
settings use the existing unchanged config blob, and old installations without
hold_ms retain 1.5/3/2.5/2.5/2.5-second defaults. Calibration is unchanged.
Settings-load returns hold_ms; save requires five integer millisecond values.
The inference cadence limits timing precision: a 0.5-second value cannot ensure
that every brief blink will be captured. Restart persistence still needs board
verification; host tests cover custom boundaries, disabled events and form values.


## Configure the device hotspot

Wi-Fi & notifications now includes Device hotspot. Enter a name (1–32 UTF-8
bytes), and optionally a new password (8–63 printable ASCII characters). Blank
password retains the current password. Save & apply hotspot persists one versioned
NVS blob at safe_road/ap_settings and restarts the device after about 2.5 seconds,
allowing the HTTP confirmation to arrive first. Rejoin the new SSID using its
password and reopen http://192.168.4.1. Monitoring pauses during the restart and
resumes with saved calibration. Home Wi-Fi and notification settings are retained.
The default remains SAFE_ROAD_xxxxxx / 12345678 until a custom configuration is
saved. Firmware no longer overwrites a saved custom password at boot or prints it
to serial. Hotspot configuration does not require an access key, matching the
other dashboard controls. Saved passwords are never returned by the API.

POST /api/hotspot accepts ssid and password. SSID/password boundary and form tests
pass; actual reconnection and power-cycle persistence still require board testing.

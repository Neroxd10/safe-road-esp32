# Tiny eye/mouth classifiers — ESP32-S3 experiment

This is a **new personalized classifier experiment**, not a compressed copy of
MediaPipe/V7. The original V7 bundle is unchanged. No trained weights are shipped:
until real capture data passes the training gate, firmware keeps its existing
calibration template matching and the dashboard says so explicitly.

## What runs where

- ESP32: existing face detector, aligned/normalized eye and mouth crops, pose
  monitoring, optional compact classifier inference, saved pose calibration.
- PC, during setup only: collect labeled crops over the local network, train
  three binary linear classifiers (logistic regression), export a C++ header.
- After flashing the trained header, detection needs no browser or PC.

The three models use 515 float parameters: **2,060 bytes** of weights and biases
and 512 weighted feature operations per inference, plus three sigmoid functions.
These numbers describe the small classifiers only; the existing face detector,
JPEG decoding, networking and image buffers still dominate resource use. They
are not measured frame-rate or accuracy claims. No new runtime dependencies.

## 1. Flash collection support

From the project folder in ESP-IDF PowerShell:

```powershell
idf.py build
idf.py -p COM5 flash monitor
```

Replace COM5 with the board's actual port. Exit monitor with Ctrl+] when needed.
Connect the PC to the device hotspot. You do not need to run the calibration
wizard to collect samples. Keep the camera fixed and your full face in frame.

## 2. Collect your own labeled examples

```powershell
python tools/tiny_ai.py collect
```

Enter setup key `12345678` when prompted. For a home-LAN connection use
`--url http://DEVICE_IP`. Follow the terminal instructions: neutral, both eyes
closed, mouth wide open, repeated over three rounds. Each capture starts only
after Enter and a four-second countdown, then gathers 24 unique frames. Do this
seated and stationary. Labels come from your deliberate poses, not from the old
detector's predictions. More varied independent rounds improve the experiment.

The endpoint is `POST /api/training/sample` with JSON `{ "key": "..." }`.
It rejects missing/incorrect keys and unavailable/stale face crops. It returns
feature schema, boot ID, frame ID and 160/160/192 normalized crop values.
These are face-derived data; files stay on your PC. No cloud service is used.

## 3. Train and check

```powershell
python tools/tiny_ai.py train
```

The last full round is reserved for testing; the preceding round is validation.
All earlier rounds train the models. Frames from one round are never split
between train/validation/test. The tool checks for duplicate frames and requires
both classes in every split. It exports only if each class/model has at least
12 held-out examples, >=80% classification accuracy and >=70% confidently correct
predictions in **both** validation and test. Otherwise the existing header stays
unchanged. See `training/report.json` for every class's results. These are an
experimental screening gate, not proof of reliable drowsiness detection; nearby
rounds with the same person are still correlated.

The tool writes `main/tiny_ai_weights.h` only on success. A failed later attempt
leaves the previous header in place. To disable trained inference, restore the
untrained stub header (set `tiny_ai::trained` to false).

## 4. Flash the trained models

```powershell
idf.py build
idf.py -p COM5 flash monitor
```

The dashboard must now say **trained tiny classifiers running on ESP32**.
Complete/load the existing nine-pose calibration for head-position monitoring.
Eye closure requires confident predictions from both eyes; uncertain predictions
and off-axis head poses suppress eye/mouth hold timers. Open/closed cutoffs are
0.2/0.8; these scores are model outputs, not calibrated real-world probabilities.
The head detector is unchanged. Recalibration buttons update pose/template data;
they do not retrain the compiled classifiers. Retrain if person, camera mounting,
orientation or lighting changes materially. Trained weights survive power cycles
as part of firmware; pose calibration remains in NVS.

## V7 model audit

```powershell
python tools/audit_v7.py "PATH/face_landmarker.task" --out training/v7_audit.json
```

The audit reads model shapes, tensor types and operator counts using Python's
standard library. It does not run, convert or quantize the V7 models and does
not measure ESP32 runtime/activation memory. Model file size alone cannot prove
either compatibility or incompatibility. A direct V7 port would additionally
need supported operators, correct preprocessing/postprocessing, activation-memory
measurement and device benchmarks. That port is not implemented by this experiment.

References: [ESP-DL quantization](https://docs.espressif.com/projects/esp-dl/en/latest/tutorials/how_to_quantize_model.html),
[MediaPipe Face Landmarker](https://ai.google.dev/edge/mediapipe/solutions/vision/face_landmarker).

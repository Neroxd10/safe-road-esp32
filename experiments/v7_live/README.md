# Live V7-style AI on ESP32-S3 â€” experimental integration

This separate project uses the directly converted V7 face detector, 478-point
landmark model and expression model on the ESP32-S3. It connects them to the
existing OV5640 camera pins, local Wi-Fi dashboard, guided calibration and NVS
storage. The original project at the repository root and the standalone model
benchmark are unchanged.

## Flash and first run

From the main `adruino` folder, in the ESP-IDF terminal:

```powershell
idf.py -C experiments/v7_live -p COM11 flash monitor
```

Replace COM11 if the port changes. Exit another serial monitor with Ctrl+] first.
Look for **V7 models ready** in the serial output. If loading fails, the dashboard
also reports a model-loading error; preserve the boot log for diagnosis.

Connect to `SAFE_ROAD_xxxxxx`, password **12345678**, and open
`http://192.168.4.1`. Enter the same setup key in Calibration. Face the camera in
good light and check that face presence, landmark markers, EAR, blink and jaw
measurements update. They are visible before calibration.

Tap **Calibrate** when ready. Follow the nine pose instructions, tapping **Ready**
for each next pose. Each begins with a four-second countdown and captures 12
valid frames. Individual pose recalibration buttons remain available afterward.

Completed V7 calibration saves automatically in NVS using the separate key
`v7_cal`. Your root project's older calibration format is not loaded or erased.
The first run of this new project requires its own calibration. After saving,
restarts restore monitoring automatically, with no phone, PC, Wi-Fi client or
browser required. Calibration and thresholds are personal to the operator and
camera placement; recapture after moving the camera or changing users.

To restore the previous firmware, exit monitor and run from the main folder:

```powershell
idf.py -p COM11 flash monitor
```

The old benchmark can still be flashed using its separate project path.

## Runtime behavior

- VGA RGB decoding from JPEG, two camera buffers in PSRAM, latest-frame capture.
- Same sensor vertical flip as the current working camera project.
- Detector: 128x128 aspect-preserving letterbox, [-1,1] RGB, 896 anchors and
  weighted overlap suppression. A strong second face is rejected.
- Landmark model: rotated square face ROI, 256x256 RGB [0,1], border replication,
  478 landmark decoding and sigmoid face-presence gating.
- Expression model: exact 146-index V7 subset, XY coordinates in original-image
  pixels, original normalization kept in FP32, quantized neural-network layers.
- Tracking: next face ROI comes from the previous valid landmarks. The detector
  runs after loss or every 12 successful tracked frames, rather than every frame.
- Eye monitoring: both-eye EAR relative to calibration plus blink scores. Mouth
  monitoring: lip-opening ratio plus jaw score. Head yaw/down/tilt retain calibrated
  2D geometry proxies; the MediaPipe 3D transformation-matrix solver is not ported.
- Face loss or invalid measurements pause sample collection and reset hold timers.
- Guided calibration does not start automatically on an uncalibrated board.
- Calibrations contain landmark-derived measurements, not eye/mouth image templates.
- Camera preview and selected landmarks, raw metrics and processed-frame timing
  appear on the dashboard. This is local HTTP, not a cloud service.
- CPU configured for 240 MHz and performance compilation; the earlier benchmark
  was 160 MHz/debug. No live-device FPS is claimed before testing this project.

## Memory placement

All three model instances stay loaded. Tensor arenas use PSRAM (zero forced
internal arena), avoiding the 48 KB internal-arena allocation failure observed in
the benchmark. During model creation, standard heap allocations are directed to
PSRAM so model metadata does not consume camera/network internal memory. The
standard-allocation threshold is restored to 4096 bytes after model loading.
Embedded models are 16-byte aligned. Null input/output buffers and expected input
shapes are checked before processing camera frames. Combined camera/Wi-Fi/model
memory behavior still needs the physical-board test.

## Validation completed

- ESP-IDF build and partition-size check passed.
- Native C++ tests passed: rotated ROI inverse, anchor indexing, box decoding,
  eye aspect ratio, bilinear sampling and border handling.
- An end-to-end PC smoke test used the actual C++ pixel sampler and the original
  full-precision ONNX models on Google's public MediaPipe portrait. Face detection
  score was ~0.913 and landmark presence was >0.999. Projected landmarks were
  visually checked. This checks preprocessing and coordinates, not accuracy of
  the quantized models on real camera frames.
- Dashboard DOM/runtime tests passed for idle, ready, saved calibration, landmark
  drawing and model-load error feedback.
- The separate benchmark already established that all three converted models can
  pass their embedded reference tests on this ESP32-S3. It did not test the live
  camera pipeline.

Re-run host checks using the isolated conversion Python environment:

```powershell
experiments/v7_direct/.venv/Scripts/python.exe -m ziglang c++ -std=c++17 experiments/v7_live/tests/geometry_test.cpp -o experiments/v7_live/tests/geometry_test.exe
experiments/v7_live/tests/geometry_test.exe
experiments/v7_direct/.venv/Scripts/python.exe experiments/v7_live/tests/check_preprocessing.py
node experiments/v7_live/tests/dashboard_test.cjs
```

## Current limits

This is ready for a **stationary integration test**, not a validated fatigue
warning system. Converted-model quantization still uses synthetic calibration;
we need representative camera face/landmark samples and independent sequences
to assess and improve accuracy. Passing synthetic reference vectors and one PC
portrait does not establish robustness, drowsiness accuracy, or a live frame rate.
Sparse frame sampling can miss brief eye closures. No buzzer, OLED, GSM or SMS
hardware integration is added here.

## References and image attribution

- MediaPipe detector graph and normalization: https://github.com/google-ai-edge/mediapipe/blob/master/mediapipe/modules/face_detection/face_detection.pbtxt
- Short-range anchors and decoding: https://github.com/google-ai-edge/mediapipe/blob/master/mediapipe/modules/face_detection/face_detection_short_range.pbtxt
- Landmark ROI and presence processing: https://github.com/google-ai-edge/mediapipe/blob/master/mediapipe/tasks/cc/vision/face_landmarker/face_landmarks_detector_graph.cc
- Expression subset: https://github.com/google-ai-edge/mediapipe/blob/master/mediapipe/tasks/cc/vision/face_landmarker/face_blendshapes_graph.cc
- Landmark-to-pixel inputs: https://github.com/google-ai-edge/mediapipe/blob/master/mediapipe/calculators/tensor/landmarks_to_tensor_calculator.cc
- Public portrait fixture: https://storage.googleapis.com/mediapipe-assets/portrait.jpg

## Face rejection fix: real-image landmark regression

The first live-device log showed Calibrate was accepted and started; sample
capture stalled because no usable face reached calibration. A subsequent PC
check reproduced a model problem: the random-calibrated INT8 landmark model
gave the public portrait only 0.018 face presence, despite the original model's
near-1.0 result. Face-image calibration alone still left INT8 coordinate errors
of roughly 10–21 pixels on this small test set. Neither threshold was lowered.

The live project now uses a 16-bit landmark model calibrated on 32 brightness,
contrast and alignment augmentations of the public portrait crop. Evaluation
on the original plus eight different augmentations produced >0.999 presence and
mean XY errors about 0.09–0.11 crop pixels (maximum below 0.4 pixel). These are
limited same-person PC checks, not evidence of broad accuracy. The detector and
expression model artifacts are unchanged. The earlier synthetic INT8 artifact
is preserved in the direct-conversion experiment.

A real-face embedded reference-vector self-test runs at startup and must print
`Real-face landmark self-test: ESP_OK` before the engine is enabled. The larger
16-bit tensors use more PSRAM and may run slower; live resource use must be
checked on the board. New calibration format version 3 ignores earlier V7
calibrations without erasing them. Complete calibration again with this model.

Face failures now show the exact reason plus detector and landmark scores in
serial, and the dashboard tells you why calibration is waiting. The old page
could show a zero-second preparation countdown while no valid face was present;
that feedback has been corrected. Request authentication and thresholds remain
in place. Watch for lines beginning `FACE WAIT:` if a visible face is rejected.

## INT16 board self-test mismatch and project-local activation fix

The next device test failed its presence-head output: expected 16124, observed
-32768. The engine remained disabled as intended. Inspection found the installed
S3 INT16 PReLU SIMD implementation copies the positive branch without applying
input-to-output exponent rescaling. Nine PReLU layers in this exported model
have different input/output exponents. The C reference path accounts for this,
but the optimized S16 assembly does not consume output_scale/output_shift.

`v7_prelu16.hpp` now registers a project-local INT16 PReLU module before model
loading. It uses per-channel learned slopes, rescales both branches, applies
HALF_UP rounding and saturates to INT16. INT8 and lookup-table PReLU modules keep
the vendor implementation. The shared managed component sources were not edited.
The scalar implementation may cost performance. Its core arithmetic passed
1,638,400 host cases against a floating reference, including positive exponent
rescaling, negative inputs, several slopes, rounding and saturation. The rebuilt
firmware passed ESP-IDF compilation and partition size checks. The complete
landmark self-test still requires device confirmation; its threshold is unchanged.

```powershell
idf.py -C experiments/v7_live -p COM11 flash monitor
```

The relevant startup result remains `Real-face landmark self-test: ESP_OK`.
To repeat the new arithmetic test with the isolated portable compiler:

```powershell
experiments/v7_direct/.venv/Scripts/python.exe -m ziglang c++ -std=c++17 experiments/v7_live/tests/prelu_test.cpp -o experiments/v7_live/tests/prelu_test.exe
experiments/v7_live/tests/prelu_test.exe
```

## Decoded-output startup validation

After the local activation correction, the board's presence output changed from
-32768 to 16134 against a reference of 16124. Its exponent is -10, so the residual
error is 10/1024 = 0.009765625 in logit units. Presence agreement alone does not
establish correct landmark coordinates. The library's raw integer tolerance of
one unit also ignores that the three output heads have different scales.

Startup now assigns the same embedded face input, executes the complete model,
and compares every output against the embedded reference in decoded units:

- All 956 XY coordinate values: mean absolute error <=0.10 crop pixel and maximum
  absolute error <=0.50 crop pixel, at 256x256 model resolution.
- All 478 Z coordinates: maximum error <=1.0 model pixel unit.
- Presence logit: absolute error <=0.025 and actual face confidence >=0.9.
- Auxiliary score: absolute error <=0.002.
- All three heads must exist with matching element counts; non-finite values fail.

This replaces raw integer equality with explicit subpixel/logit bounds; it does
not simply ignore the failed presence output or globally increase raw epsilon.
The live face-detection/landmark-presence thresholds remain unchanged. Host
regression cases verify rejection of the previous saturated output, pixel drift,
excessive mean drift, bad scores and NaNs. The device must still pass the complete
new comparison. The log prints `Landmark reference errors:` with all measurements
and then `Real-face landmark self-test: ESP_OK` or ESP_FAIL.

## Speed pass after the first working live run

The user log showed ~3.0–3.2 seconds between valid readings, including ~2.9 seconds
on frames with landmark rejection while the detector was skipped. The corrected
PReLU implementation contained variable 64-bit divisions per activation value
and reloaded exponents/slopes metadata inside its pixel loop.

The speed build makes these changes without changing model weights, thresholds,
calibration format, face-detection frequency or frame-skipping policy:

- Exact power-of-two HALF_UP shifts replace 64-bit division; normal INT16-product
  cases use safe 32-bit arithmetic, with a wide path for extreme shifts.
- Per-layer exponents and pointers are cached, and nested row/channel loops remove
  per-element modulo and repeated metadata access.
- PReLU layers >=8192 values split disjoint rows across the existing two-core
  ESP-DL worker runtime. Small layers stay on the caller core.
- Image input generation caches trigonometry and quantization scale once per crop
  and dispatches tensor element type outside the channel loop.
- Startup now checks decoded landmark outputs in both single-core and dual-core
  modes with the same physical-unit limits before live operation is enabled.

Host tests with assertions enabled passed 1,638,400 activation cases against the
reference, including the cached fast path. Preprocessing parity checks compared
110,592 tensor elements across types/rotations/border modes with exact equality.
Dashboard tests passed. The build is verified; the speed increase still needs
measurement on the ESP32 and no FPS improvement is claimed yet.

Serial prints one timing line per processed frame:

```text
PERF total=...ms detect=... prep=... landmarks=... (prelu=...) expressions=...
```

The PReLU time is included in landmark time. Zero detector time means that frame
reused tracking. Timings exclude camera capture/JPEG decode. A dashboard breakdown
is available under calibration details. Flash normally, without fullclean:

```powershell
idf.py -C experiments/v7_live -p COM11 flash monitor
```

The previously working application binary was preserved in
`tests/before_speed/safe_road_v7_live.bin` before applying these optimizations.


Second speed pass: the first optimized board logs show tracked readings around
1.9 seconds, with preparation around 357 ms and PReLU around 430 ms. This pass
shares RGB source addresses/border work, prepares disjoint tensor rows on both
runtime workers, and specializes activation loops outside the element loop.
The arithmetic order, weights, thresholds and saved calibration format remain
unchanged. Host checks passed 3,145,728 specialized activation cases, the existing
1,638,400 reference cases, 120,000 exact camera-color sample comparisons, and
split-row tensor parity. Firmware builds successfully. Actual second-pass speed
and dual-worker behavior still require a board run; compare the PERF lines.


Third speed candidate: project-local ESP32-S3 SIMD PReLU processes eight INT16
values per vector. It applies only to aligned, channel-multiple-of-eight layers
with identical input/output exponents. Rescaling layers retain exact scalar code.
Each negative shift is enabled only after an on-device check of every INT16 input
against eight slope values (524,288 comparisons per shift); any mismatch disables
that vector path. The existing full-model single/multi-core reference checks
remain mandatory. These hardware checks have not run on the developer machine.

Aligned slope caches use at most 16 KiB total internal RAM, allocated only while
at least 64 KiB remains free at allocation time. Aligned original per-channel
slopes can also use SIMD if the cache is unavailable. The V7_SIMD startup logs
report probe results, vector/scalar layer counts, cache bytes and internal heap.
Startup will take longer for the instruction checks. The candidate builds, but
actual speed improvement from the measured 1.59-second baseline is pending a
board test. The previous working app binary is saved under tests/before_vector/.
Weights, calibration format and detection thresholds remain unchanged.

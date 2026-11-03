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

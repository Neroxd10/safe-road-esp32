# Direct V7 conversion experiment

The original V7 model bundle and active dashboard firmware are unchanged. This
folder contains direct conversions of the original pretrained V7 models, not
new eye/mouth classifiers. It is an experiment, not live monitoring firmware.

## Results obtained

- All three TFLite models converted to ONNX and passed ONNX structural checks.
- Each ONNX model was compared with its original TFLite model on five identical
  synthetic inputs. Maximum absolute output errors were under 0.001, against a
  0.01 numerical-conversion gate. See `all_float_equivalence.json`.
- Espressif ESP-PPQ 1.3.14 exported ESP32-S3 `.espdl` models using w8a8 quantization.
  Some expression-model operations remain floating point, so this is mixed
  execution rather than an entirely INT8 graph.
- Landmark PReLU slopes were reshaped from [1,C,1,1] to [C,1,1] to match ESP-DL
  3.3.14's runtime contract. Equivalent floating-point outputs were rechecked.
- Expression-model Reciprocal nodes were replaced with equivalent Div(1,x),
  because the installed ESP-DL registry lacks Reciprocal.
- Exported operation names were checked against the installed runtime's
  `spec/ops.yml`; no missing operation names remained. This is not a check of
  every runtime shape/dtype restriction. Actual board tests are still required.
- Calibration for these exports was SYNTHETIC, not representative face images.
  File conversion success does not establish usable accuracy or speed.

See `results.json` for artifact sizes/hashes and `runtime_operator_check.json`
for operation checks. The desktop PyTorch/ONNX/TensorFlow environment lives in
`.venv`; it is separate from ESP-IDF's Python environment. See requirements-lock.txt.

## Next: board-only structural benchmark

A standalone ESP-IDF app is in `benchmark/`. It uses the same 16 MB flash/8 MB
OPI PSRAM settings and partition table as the main project. It loads each model
one at a time, compares an embedded expected test vector, and reports memory and
single/dual-core model-only latency. Loading models one at a time does NOT prove
that the complete camera/face/landmark/expression pipeline fits simultaneously.
It does not start the camera or provide a Wi-Fi dashboard. It is not drowsiness
monitoring. It uses the experimental synthetic-calibrated models.

From the main project folder, in the ESP-IDF terminal:

```powershell
idf.py -C experiments/v7_direct/benchmark -p COM5 flash monitor
```

Replace COM5 with your ESP32 port. Close any other serial monitor first. Flashing
this app temporarily replaces the dashboard app. Preserve the output beginning
with `V7_BENCH`, plus any assertion or error output. A crash/output mismatch is
valuable evidence of a runtime incompatibility, not a successful deployment.
The build has been verified; execution on your board has NOT been verified.

To restore your normal firmware, exit monitor (Ctrl+]) and run from project root:

```powershell
idf.py -p COM5 flash monitor
```

The normal project files and NVS calibration have not been deliberately erased.

## Reproduce conversion

From the main project folder:

```powershell
experiments/v7_direct/.venv/Scripts/python.exe experiments/v7_direct/run_conversion.py --quantize-smoke
```

Override `--bundle` to use a different location for the same original task file.
This extracts model copies, converts and numerically validates all three, exports
synthetic-calibrated structural test models and updates benchmark assets. It
never flashes the ESP32 or changes the main firmware's detector. Failed earlier
converter attempts are retained in logs; the working expression conversion uses
`tf2onnx`, followed by `normalize_blendshapes.py`.

## Work remaining before live V7-style monitoring

1. Verify exported test vectors, memory use and latency on the actual ESP32-S3.
2. Calibrate quantization with representative aligned RGB face crops, and with
   representative landmark inputs for the expression model; the previous tiny-AI
   normalized eye/mouth patches cannot substitute for this data.
3. Compare floating and quantized landmarks/blendshapes on real images and
   independent sequences, including closed eyes, yawns, turns and poor lighting.
4. Port V7's face ROI rotation/scale, landmark decoding, selected landmark mapping
   for expressions, face presence handling and temporal decisions. A converted
   neural network does not include the MediaPipe task's full graph processing.
5. Integrate with the camera/dashboard only after the tests above establish that
   speed, memory use and output quality are adequate.

References:
- https://docs.espressif.com/projects/esp-dl/en/latest/tutorials/how_to_quantize_model.html
- https://github.com/espressif/esp-ppq
- https://github.com/google-ai-edge/mediapipe/blob/master/mediapipe/tasks/cc/vision/face_landmarker/face_landmarks_detector_graph.cc

## First board result and allocation fix

The first board run found all 8 MB PSRAM and passed its boot memory test, but
failed to allocate a 48 KB contiguous internal tensor arena (largest available
internal block: 22.5 KB). The original test app continued into Model::test() with
a null input tensor and crashed. This did not measure inference speed.

The revised benchmark requests zero internal tensor arena bytes (PSRAM placement),
checks input/output tensor pointers before testing, and aligns embedded models to
16 bytes. Rebuild/reflash the benchmark to test this correction. It has not yet
been verified on the physical board.

## Second board result: landmark test passed, expression layout corrected

The next board log shows the landmark model's embedded test-vector comparison
passing (ESP_OK). The subsequent assertion points to the third model's loading
call, the expression/blendshape model. A PC audit reproduced its shape failure:
exporter layout propagation/fusion mixed NCHW and NHWC, broadcasting an intended
[1,1,97,64] tensor into [1,64,97,64] before a reshape.

`conservative_layout.py` is now used only for expression export. It retains the
original ONNX layouts for non-convolution operations and wraps each convolution
in explicit NCHW/NHWC conversions. It does not disable the runtime assertion or
change target sizes to force a match. The corrected export passes all module
shape checks in `audit_runtime_shapes.py`. The previous export fails that audit
at the reproduced reshape; see `shape_audit_before.json`. Golden exported test
inputs and outputs were confirmed unchanged, byte for byte. Extra transposes
may cost inference time. The corrected model still needs the physical board's
test-vector comparison and latency measurements.

## Third board result and expression precision diagnostic

User log (COM11, CPU 160 MHz, debug compiler settings):

| Model | Exported test vector | Single core | Dual core |
| --- | --- | --- | --- |
| Landmarks | ESP_OK | 1053.3 ms | 924.1 ms |
| Face detector | ESP_OK | 970.9 ms | 890.2 ms |
| Expression | ESP_FAIL | not measured | not measured |

The expression output differed by 12 integer units at output index 1. The test
correctly stopped. These times exclude camera capture/cropping/networking and
the models were loaded individually. They do not establish a real monitoring FPS.

A PC check found that the INT8 coordinate-normalization frontend rounded squared
values to zero for the test input, producing a zero normalization denominator.
The eleven operations before the first Conv are now explicitly dispatched to
FP32. The model still uses quantized neural-network layers; this is mixed
precision. The corrected frontend's PC arithmetic is finite and agrees with its
reference within 0.000002 on this input. That is not a physical-board pass.

The current benchmark defaults to EXPRESSION-ONLY DIAGNOSTIC mode and compares
179 intermediate reference tensors, stopping at the first mismatch. It skips
the two already-passing models. Flash from project root:

```powershell
idf.py -C experiments/v7_direct/benchmark -p COM11 flash monitor
```

Keep the `Testing output ...` line immediately before any mismatch, the error
and the `V7_BENCH` lines. To rebuild the normal three-model benchmark later:

```powershell
idf.py -C experiments/v7_direct/benchmark -D V7_EXPRESSION_DIAGNOSTIC=OFF build
```

To regenerate diagnostic model assets with the isolated Python environment:

```powershell
experiments/v7_direct/.venv/Scripts/python.exe experiments/v7_direct/quantize_smoke.py --model face_blendshapes --diagnostic
```

Normal smoke exports retain their names; the diagnostic export has its own
`face_blendshapes_s3_int8_diagnostic.espdl` file. The diagnostic model's additional
intermediate-reference data increases flash usage. No test tolerance was relaxed.

## Fourth board result: expression reference checks passed

The corrected expression diagnostic passed all 179 intermediate checkpoints and
its final output on the ESP32-S3 (ESP_OK). Model-only averages were 160.2 ms
single-core and 154.8 ms dual-core. No tolerance was relaxed. The coordinate
normalization FP32 fix and explicit convolution layout boundaries were active.

All three converted V7 models have now passed their embedded reference vectors
in separate board runs. This establishes runtime compatibility for these inputs,
not equivalence on all images or real-world detection accuracy. Calibration
remains synthetic. Landmarks 924.1 ms + face detector 890.2 ms + expressions
154.8 ms sum to 1969.1 ms for separate dual-core model-only runs at 160 MHz/debug
settings; this is not a live pipeline FPS measurement. Models were loaded one
at a time, so combined camera/Wi-Fi/model RAM use is still unverified.

The current flashed diagnostic is a benchmark without camera or dashboard.
Next work for live monitoring is representative face/landmark calibration,
image/ROI preprocessing, tracking, landmark/expression decoding and pipeline
scheduling, followed by accuracy and latency tests on real sequences. Normal
three-model mode can be selected with V7_EXPRESSION_DIAGNOSTIC=OFF; the working
original dashboard firmware can be restored from the project root as documented.

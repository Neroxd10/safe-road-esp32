// No trained model supplied. Existing calibration matching stays active.
// tools/tiny_ai.py train replaces this only after held-out validation passes.
#pragma once
namespace tiny_ai {
inline constexpr bool trained = false;
inline constexpr int schema = 1;
inline constexpr float left_eye_bias=0, right_eye_bias=0, mouth_bias=0;
inline constexpr float left_eye_weights[160]={};
inline constexpr float right_eye_weights[160]={};
inline constexpr float mouth_weights[192]={};
}

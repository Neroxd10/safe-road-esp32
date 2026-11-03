#pragma once
#include <cmath>
#include "tiny_ai_weights.h"

namespace tiny_ai {
static_assert(schema == 1, "Incompatible feature schema");
static_assert(sizeof(left_eye_weights)/sizeof(float) == 160, "Bad left eye model");
static_assert(sizeof(right_eye_weights)/sizeof(float) == 160, "Bad right eye model");
static_assert(sizeof(mouth_weights)/sizeof(float) == 192, "Bad mouth model");
template <int N>
inline float probability(const float *x, const float (&weights)[N], float bias) {
    float z=bias;
    for (int i=0;i<N;++i) z+=x[i]*weights[i];
    if (!std::isfinite(z)) return 0.5f;
    z=z < -40 ? -40 : z > 40 ? 40 : z;
    return 1.0f/(1.0f+std::exp(-z));
}
}

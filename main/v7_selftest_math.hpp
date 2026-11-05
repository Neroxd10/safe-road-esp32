#pragma once
#include <cmath>
namespace v7 {
struct LandmarkTestErrors {
    float mean_xy=0,max_xy=0,max_z=0,presence_logit=0,auxiliary=0,face_confidence=0;
};
inline bool valid_landmark_test(const LandmarkTestErrors &e) {
    // At 192x192 model resolution, require subpixel XY agreement and a
    // small decoded-logit difference. Never use a global raw-integer epsilon:
    // coordinates, logits and auxiliary output have different exponents.
    return std::isfinite(e.mean_xy)&&std::isfinite(e.max_xy)&&std::isfinite(e.max_z)&&
      std::isfinite(e.presence_logit)&&std::isfinite(e.auxiliary)&&std::isfinite(e.face_confidence)&&
      e.mean_xy<=.10f&&e.max_xy<=.50f&&e.max_z<=1.f&&e.presence_logit<=.025f&&
      e.auxiliary<=.002f&&e.face_confidence>=.9f;
}
}

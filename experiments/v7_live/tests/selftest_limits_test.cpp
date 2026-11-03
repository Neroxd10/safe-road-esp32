#include "../main/v7_selftest_math.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
int main(){
    using namespace v7;
    LandmarkTestErrors e{.03f,.12f,.2f,10.f/1024.f,.0002f,.999f};assert(valid_landmark_test(e));
    auto bad=e;bad.presence_logit=(16124.f+32768.f)/1024.f;assert(!valid_landmark_test(bad));
    bad=e;bad.max_xy=1.f;assert(!valid_landmark_test(bad));
    bad=e;bad.mean_xy=.11f;assert(!valid_landmark_test(bad));
    bad=e;bad.max_z=1.1f;assert(!valid_landmark_test(bad));
    bad=e;bad.auxiliary=.003f;assert(!valid_landmark_test(bad));
    bad=e;bad.face_confidence=.7f;assert(!valid_landmark_test(bad));
    bad=e;bad.max_xy=NAN;assert(!valid_landmark_test(bad));
    std::cout<<"PASS: decoded-output checks accept small rounding but reject saturation, pixel drift, invalid scores and non-finite values\n";
}

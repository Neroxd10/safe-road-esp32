#include "../main/small_features.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <limits>
#include <iostream>
int main(){
 using namespace small_features;
 assert(eyes(.30f,.32f,.30f,.32f,.10f,.11f,true)==Eye::Open);
 assert(eyes(.10f,.11f,.30f,.32f,.10f,.11f,true)==Eye::Closed);
 assert(eyes(.10f,.32f,.30f,.32f,.10f,.11f,true)!=Eye::Closed);
 assert(eyes(.23f,.24f,.30f,.32f,.10f,.11f,true)==Eye::Uncertain);
 assert(eyes(.10f,.11f,.30f,.32f,.10f,.11f,false)==Eye::Unknown);
 assert(eyes(.10f,.11f,.30f,.32f,.29f,.30f,true)==Eye::Unknown);
 assert(eyes(std::numeric_limits<float>::quiet_NaN(),.1f,.3f,.32f,.1f,.11f,true)==Eye::Unknown);
 assert(eyes(-.1f,.11f,.3f,.32f,.1f,.11f,true)==Eye::Unknown);
 std::cout<<"PASS: calibrated open/closed/uncertain, unilateral closure, bad calibration, missing face and invalid geometry\n";
}

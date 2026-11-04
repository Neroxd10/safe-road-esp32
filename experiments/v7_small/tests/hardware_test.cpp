#include "../main/hardware_policy.hpp"
#include "../main/oled_font.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
int main(){
 int64_t d[5]={1499,0,0,0,0};assert(hardware::alert_event(true,true,500,d)==-1);
 d[0]=1500;assert(hardware::alert_event(true,true,500,d)==0);
 assert(hardware::alert_event(false,true,500,d)==-1);assert(hardware::alert_event(true,false,500,d)==-1);
 assert(hardware::alert_event(true,true,2501,d)==-1);assert(hardware::alert_event(true,true,-1,d)==-1);
 assert(hardware::pulse(0,0));assert(!hardware::pulse(0,200));assert(hardware::pulse(0,700));assert(!hardware::pulse(1,700));assert(!hardware::pulse(-1,0));
 d[0]=0;d[1]=3000;assert(hardware::alert_event(true,true,500,d)==1);
 for(int i=0;i<5;i++){int64_t one[5]={};one[i]=alerts::thresholds[i];assert(hardware::alert_event(true,true,500,one)==i);}
 int limits[5]={5000,4000,3000,3000,3000};int64_t configured[5]={4999,0,0,0,0};
 assert(hardware::alert_event(true,true,0,configured,limits,31)==-1);configured[0]=5000;assert(hardware::alert_event(true,true,0,configured,limits,31)==0);assert(hardware::alert_event(true,true,0,configured,limits,30)==-1);
 unsigned char glyph[5];hardware::glyph('A',glyph);assert(glyph[0]==126);hardware::glyph(' ',glyph);for(auto v:glyph)assert(!v);
 std::cout<<"PASS: OLED glyphs; buzzer thresholds, pulse timing, calibration/missing/stale face silencing\n";
}

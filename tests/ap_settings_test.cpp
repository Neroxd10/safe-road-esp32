#include "../main/ap_settings.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
int main(){hotspot::Settings s;strcpy(s.ssid,"SAFE ROAD");strcpy(s.password,"12345678");assert(hotspot::valid(s));s.password[7]=0;assert(!hotspot::valid(s));memset(s.password,'a',63);s.password[63]=0;assert(hotspot::valid(s));s.ssid[0]=0;assert(!hotspot::valid(s));memset(s.ssid,'b',32);s.ssid[32]=0;assert(hotspot::valid(s));s.ssid[0]='\n';assert(!hotspot::valid(s));memset(s.ssid,'a',33);assert(!hotspot::valid(s));std::cout<<"PASS: SSID/password boundaries, controls and missing terminators\n";}

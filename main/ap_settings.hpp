#pragma once
#include <cstring>
namespace hotspot {
struct Settings {unsigned version=1;char ssid[33]={};char password[64]={};};
inline bool valid(const Settings &s){
 if(s.version!=1||!std::memchr(s.ssid,0,sizeof(s.ssid))||!std::memchr(s.password,0,sizeof(s.password)))return false;
 const auto n=std::strlen(s.ssid),p=std::strlen(s.password);if(!n||n>32||p<8||p>63)return false;
 for(const unsigned char *v=(const unsigned char *)s.ssid;*v;v++)if(*v<32||*v==127)return false;
 for(const unsigned char *v=(const unsigned char *)s.password;*v;v++)if(*v<32||*v>126)return false;
 return true;
}
}

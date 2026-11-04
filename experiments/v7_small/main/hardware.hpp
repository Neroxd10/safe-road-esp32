#pragma once
#include <cstdint>
namespace hardware {
struct Snapshot {
 bool face=false,monitoring=false,calibrating=false,waiting=false,wifi=false,internet=false;
 int stage=0,samples=0;int64_t age_ms=0;
 int64_t durations[5]={};
 char eye[20]={},mouth[20]={},away[16]={},tilt[16]={};bool down=false;
};
using ReadSnapshot=bool (*)(Snapshot &);
void init(ReadSnapshot reader);
bool internet_recent();
bool oled_ready();
}

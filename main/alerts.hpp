#pragma once
#include <atomic>
#include <cstdint>
#include "cJSON.h"
namespace alerts {
bool init(const char *device,std::atomic<bool> *connected);
void observe(const int64_t durations[5]);
uint32_t take_display_milestones();
void reset_episode();
bool local_policy(int *hold_ms,uint32_t &events);
cJSON *request(cJSON *doc);
void status(cJSON *doc);
bool enabled_channels(bool &email,bool &webhook);
}

#pragma once
#include <atomic>
#include <cstdint>
#include "cJSON.h"
namespace alerts {
bool init(const char *device,std::atomic<bool> *connected);
void observe(const int64_t durations[5]);
void reset_episode();
cJSON *request(cJSON *doc);
void status(cJSON *doc);
}

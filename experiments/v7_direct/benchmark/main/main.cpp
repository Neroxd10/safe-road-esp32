#include "dl_model_base.hpp"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <new>
extern const uint8_t landmark_start[] asm("_binary_landmark_espdl_start");
extern const uint8_t detector_start[] asm("_binary_detector_espdl_start");
extern const uint8_t blendshapes_start[] asm("_binary_blendshapes_espdl_start");
extern const uint8_t blendshapes_diagnostic_start[] asm("_binary_blendshapes_diagnostic_espdl_start");
#ifndef V7_EXPRESSION_DIAGNOSTIC
#define V7_EXPRESSION_DIAGNOSTIC 0
#endif
static const char *TAG="V7_BENCH";
static bool run_one(const char *name,const uint8_t *data) {
    ESP_LOGI(TAG,"BEGIN %s: free internal=%u PSRAM=%u largest PSRAM block=%u",name,
      unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
      unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
      unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
    const int64_t start=esp_timer_get_time();
    auto *model=new(std::nothrow) dl::Model(reinterpret_cast<const char *>(data),
       // No contiguous internal tensor arena: use the board's 8 MB PSRAM.
       fbs::MODEL_LOCATION_IN_FLASH_RODATA,0,dl::MEMORY_MANAGER_GREEDY,nullptr,true);
    if (!model) {ESP_LOGE(TAG,"%s allocation failed",name);return false;}
    ESP_LOGI(TAG,"%s load=%lld ms",name,(long long)((esp_timer_get_time()-start)/1000));
    model->profile_memory();
    // Model construction can return an object after arena allocation fails.
    // Never call test()/run() on the null input tensor seen in the crash log.
    const auto ready=[](const auto &tensors) {
        if (tensors.empty()) return false;
        for (const auto &entry:tensors) {
            if (!entry.second || !entry.second->get_element_ptr()) return false;
        }
        return true;
    };
    if (!ready(model->get_inputs()) || !ready(model->get_outputs())) {
        ESP_LOGE(TAG,"%s tensor allocation failed; stopping before inference",name);
        delete model;return false;
    }
    ESP_LOGI(TAG,"%s input/output buffers allocated; starting test",name);
    const esp_err_t test=model->test();
    ESP_LOGI(TAG,"%s exported test-vector comparison: %s",name,esp_err_to_name(test));
    if (test!=ESP_OK) {ESP_LOGE(TAG,"STOP: runtime output mismatch");delete model;return false;}
    for (auto mode:{dl::RUNTIME_MODE_SINGLE_CORE,dl::RUNTIME_MODE_MULTI_CORE}) {
        int64_t total=0;
        for (int i=0;i<3;i++) {
            const int64_t before=esp_timer_get_time();model->run(mode);
            total+=esp_timer_get_time()-before;vTaskDelay(pdMS_TO_TICKS(100));
        }
        ESP_LOGI(TAG,"%s %s average model-only latency=%.1f ms",name,
           mode==dl::RUNTIME_MODE_SINGLE_CORE ? "single-core" : "dual-core",double(total)/3000.0);
    }
    ESP_LOGI(TAG,"%s done; PSRAM free=%u, stack watermark=%u",name,
       unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),unsigned(uxTaskGetStackHighWaterMark(nullptr)));
    delete model;return true;
}
static void benchmark(void *) {
    ESP_LOGI(TAG,"V7 STRUCTURAL SMOKE TEST: synthetic calibration, NOT detection accuracy");
    // Load one at a time: this measures per-model memory, not combined pipeline memory.
#if V7_EXPRESSION_DIAGNOSTIC
    ESP_LOGI(TAG,"EXPRESSION-ONLY DIAGNOSTIC: compare 179 intermediate checkpoints; first two models skipped");
    bool ok=run_one("blendshapes-diagnostic",blendshapes_diagnostic_start);
#else
    bool ok=run_one("landmarks",landmark_start);
    if (ok) ok=run_one("face-detector",detector_start);
    if (ok) ok=run_one("blendshapes",blendshapes_start);
#endif
    ESP_LOGI(TAG,"BENCHMARK %s; camera, cropping, Wi-Fi and monitoring are not implemented in this test app",ok?"COMPLETE":"FAILED");
    vTaskDelete(nullptr);
}
extern "C" void app_main() {
    if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM)<6*1024*1024) {ESP_LOGE(TAG,"Expected 8 MB PSRAM configuration");return;}
    if (xTaskCreatePinnedToCore(benchmark,"v7_bench",16384,nullptr,4,nullptr,1)!=pdPASS)
        ESP_LOGE(TAG,"Benchmark task allocation failed");
}

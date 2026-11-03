#pragma once
#include "v7_prelu_math.hpp"
#include "dl_module_creator.hpp"
#include "dl_module_prelu.hpp"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
extern "C" void v7_prelu_simd_row(int16_t *,const int16_t *,const int16_t *,int,int);
namespace v7 {
// Probe the actual instruction on this chip before enabling it for a shift.
// Every INT16 input is checked against eight slopes, including saturation
// and halfway rounding cases. A mismatch keeps the exact scalar path.
inline bool prelu_simd_shift_verified(int shift){
    static uint32_t tested=0,passed=0;
    if(shift<1||shift>30)return false;
    const uint32_t bit=uint32_t(1)<<shift;
    if(tested&bit)return (passed&bit)!=0;
    tested|=bit;
    alignas(16) int16_t input[8],output[8];
    alignas(16) const int16_t slopes[8]={-32768,-8192,-1,0,1,8192,16384,32767};
    for(int x=-32768;x<=32767;x++){
        for(auto &v:input)v=int16_t(x);
        v7_prelu_simd_row(output,input,slopes,1,shift);
        for(int c=0;c<8;c++)if(output[c]!=prelu_s16_cached(int16_t(x),slopes[c],0,shift)){
            ESP_LOGW("V7_SIMD","shift=%d mismatch x=%d slope=%d; exact scalar fallback",shift,x,int(slopes[c]));
            return false;
        }
        if((x&4095)==0)vTaskDelay(1);
    }
    passed|=bit;
    ESP_LOGI("V7_SIMD","shift=%d passed 524288 hardware/reference comparisons",shift);
    return true;
}

// Project-local INT16 PReLU. Requantizes both branches with HALF_UP rounding.
// The vendor S3 S16 assembly only rescales the negative branch.
class ExactPRelu16 final:public dl::module::Module {
    dl::TensorBase *alpha;
    int16_t *internal_slopes=nullptr;
    inline static size_t internal_slope_bytes=0;
    inline static unsigned vector_layers=0,scalar_layers=0;
    bool recorded_path=false;
    size_t slope_bytes=0;
    struct Work {
        const int16_t *src,*slopes;int16_t *dst;
        int first_row,last_row,channels,alpha_count,positive_shift,negative_shift;
        bool vector=false;
    };
public:
    inline static int64_t elapsed_us=0;
private:
public:
    ExactPRelu16(const char *name,dl::TensorBase *a):
      Module(name,dl::MODULE_INPLACE_CHANGED_BUFFER,dl::QUANT_TYPE_SYMM_16BIT),alpha(a){}
    ~ExactPRelu16(){
        if(internal_slopes){internal_slope_bytes-=slope_bytes;heap_caps_free(internal_slopes);}
        delete alpha;
    }
    std::vector<std::vector<int>> get_output_shape(std::vector<std::vector<int>> &shapes)override {
        assert(!shapes.empty()&&!shapes[0].empty());
        assert(alpha->get_dtype()==dl::DATA_TYPE_INT16);
        assert(alpha->get_size()==1||alpha->get_size()==shapes[0].back());
        const int channels=shapes[0].back();
        const size_t bytes=channels*sizeof(int16_t);
        // Cap the total cache and preserve internal heap for camera/network tasks.
        if(!internal_slopes&&channels%8==0&&internal_slope_bytes+bytes<=16384&&
           heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)>65536+bytes){
            internal_slopes=static_cast<int16_t *>(heap_caps_aligned_alloc(16,bytes,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
            if(internal_slopes){
                const auto *source=alpha->get_element_ptr<int16_t>();
                for(int c=0;c<channels;c++)internal_slopes[c]=source[alpha->get_size()==1?0:c];
                slope_bytes=bytes;internal_slope_bytes+=bytes;
            }
        }
        return {shapes[0]};
    }
    void forward_args(void *arg)override {
        const auto &w=*static_cast<Work *>(arg);
        if(w.vector){
            for(int row=w.first_row;row<w.last_row;row++){
                const int offset=row*w.channels;
                v7_prelu_simd_row(w.dst+offset,w.src+offset,w.slopes,w.channels/8,w.negative_shift);
            }
            return;
        }
        if(w.positive_shift==0){
            if(w.alpha_count==1)prelu_dispatch_shift<true,true>(w);else prelu_dispatch_shift<true,false>(w);
        }else{
            if(w.alpha_count==1)prelu_dispatch_shift<false,true>(w);else prelu_dispatch_shift<false,false>(w);
        }
    }
    void forward(dl::ModelContext *context,dl::runtime_mode_t mode)override {
        const int64_t start=esp_timer_get_time();
        auto *input=context->get_tensor(m_inputs_index[0]);auto *output=context->get_tensor(m_outputs_index[0]);
        const int channels=input->shape.back(),rows=input->get_size()/channels;
        const int positive_shift=output->exponent-input->exponent;
        Work work{input->get_element_ptr<int16_t>(),alpha->get_element_ptr<int16_t>(),output->get_element_ptr<int16_t>(),
          0,rows,channels,alpha->get_size(),positive_shift,positive_shift-alpha->exponent};
        // Aligned original per-channel slopes also work when the RAM cache
        // cannot be allocated. SIMD never depends on reducing the heap reserve.
        const int16_t *vector_slopes=internal_slopes?internal_slopes:
            (alpha->get_size()==channels?alpha->get_element_ptr<int16_t>():nullptr);
        if(vector_slopes&&!(reinterpret_cast<uintptr_t>(vector_slopes)&15)&&positive_shift==0&&channels%8==0&&
           !(reinterpret_cast<uintptr_t>(work.src)&15)&&!(reinterpret_cast<uintptr_t>(work.dst)&15)&&
           prelu_simd_shift_verified(work.negative_shift)){
            work.vector=true;work.slopes=vector_slopes;work.alpha_count=channels;
        }
        if(!recorded_path){if(work.vector)vector_layers++;else scalar_layers++;recorded_path=true;}
        if(mode!=dl::RUNTIME_MODE_SINGLE_CORE&&input->get_size()>=8192&&rows>=2){
            Work other=work;work.last_row=rows/2;other.first_row=rows/2;
            dl::module::module_forward_dual_core(this,&work,&other);
        }else forward_args(&work);
        elapsed_us+=esp_timer_get_time()-start;
    }
    static void log_paths(){
        ESP_LOGI("V7_SIMD","vector layers=%u scalar layers=%u internal slope cache=%u bytes; internal free=%u largest=%u",
            vector_layers,scalar_layers,unsigned(internal_slope_bytes),
            unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),
            unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));
    }
    static dl::module::Module *deserialize(fbs::FbsModel *model,std::string name) {
        dl::quant_type_t quant;model->get_operation_attribute(name,"quant_type",quant);
        if(quant!=dl::QUANT_TYPE_SYMM_16BIT||dl::module::LUT::has_lut(model,name))
            return dl::module::PRelu::deserialize(model,name);
        return new ExactPRelu16(name.c_str(),model->get_operation_parameter(name,1));
    }
};
inline void register_exact_prelu16() {
    auto *creator=dl::module::ModuleCreator::get_instance();
    creator->register_dl_modules();
    creator->register_module("PRelu",ExactPRelu16::deserialize);
}
}

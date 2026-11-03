#pragma once
#include "v7_prelu_math.hpp"
#include "dl_module_creator.hpp"
#include "dl_module_prelu.hpp"
#include "esp_timer.h"
namespace v7 {
// Project-local INT16 PReLU. Requantizes both branches with HALF_UP rounding.
// The vendor S3 S16 assembly only rescales the negative branch.
class ExactPRelu16 final:public dl::module::Module {
    dl::TensorBase *alpha;
    struct Work {
        const int16_t *src,*slopes;int16_t *dst;
        int first_row,last_row,channels,alpha_count,positive_shift,negative_shift;
    };
public:
    inline static int64_t elapsed_us=0;
private:
public:
    ExactPRelu16(const char *name,dl::TensorBase *a):
      Module(name,dl::MODULE_INPLACE_CHANGED_BUFFER,dl::QUANT_TYPE_SYMM_16BIT),alpha(a){}
    ~ExactPRelu16(){delete alpha;}
    std::vector<std::vector<int>> get_output_shape(std::vector<std::vector<int>> &shapes)override {
        assert(!shapes.empty()&&!shapes[0].empty());
        assert(alpha->get_dtype()==dl::DATA_TYPE_INT16);
        assert(alpha->get_size()==1||alpha->get_size()==shapes[0].back());
        return {shapes[0]};
    }
    void forward_args(void *arg)override {
        const auto &w=*static_cast<Work *>(arg);
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
        if(mode!=dl::RUNTIME_MODE_SINGLE_CORE&&input->get_size()>=8192&&rows>=2){
            Work other=work;work.last_row=rows/2;other.first_row=rows/2;
            dl::module::module_forward_dual_core(this,&work,&other);
        }else forward_args(&work);
        elapsed_us+=esp_timer_get_time()-start;
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

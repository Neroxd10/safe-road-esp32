#pragma once
#include <algorithm>
#include <cstdint>
namespace v7 {
inline int16_t requantize_s16(int64_t value,int right_shift) {
    if(!value)return 0;
    if(right_shift>0) {
        if(right_shift>=63)return 0;
        const int64_t denominator=int64_t(1)<<right_shift;
        const int64_t adjusted=value+(denominator>>1);
        // Xtensa GCC and our host compiler use arithmetic signed right shifts.
        // This is floor division by a power of two, including negative values.
        value=adjusted>>right_shift;
    } else if(right_shift<0) {
        if(right_shift<=-16)return value>0?32767:-32768;
        value*=int64_t(1)<<(-right_shift);
    }
    return int16_t(std::clamp(value,int64_t(-32768),int64_t(32767)));
}
inline int16_t requantize_product_s16(int32_t value,int shift) {
    // An INT16*INT16 product is bounded by +/-2^30, so adding at most
    // 2^29 is safe in int32. Keep rarer large/left shifts in the wide path.
    if(shift>0&&shift<=30){
        int32_t q=(value+(int32_t(1)<<(shift-1)))>>shift;
        return int16_t(std::clamp(q,int32_t(-32768),int32_t(32767)));
    }
    return requantize_s16(value,shift);
}
inline int16_t prelu_s16_cached(int16_t x,int16_t alpha,int positive_shift,int negative_shift) {
    if(x<0)return requantize_product_s16(int32_t(x)*alpha,negative_shift);
    if(positive_shift==0)return x;
    return requantize_product_s16(x,positive_shift);
}
inline int16_t prelu_s16_value(int16_t x,int16_t alpha,int input_exp,int alpha_exp,int output_exp) {
    return x<0?requantize_s16(int64_t(x)*alpha,output_exp-input_exp-alpha_exp):
               requantize_s16(x,output_exp-input_exp);
}
    template<bool identity_positive,bool scalar_alpha,bool fast_negative,typename Work>
    inline void prelu_process(const Work &w) {
        const int shift=w.negative_shift;
        const int32_t rounding=fast_negative?(int32_t(1)<<(shift-1)):0;
        for(int row=w.first_row;row<w.last_row;row++){
            const int offset=row*w.channels;
            for(int c=0;c<w.channels;c++){
                const int16_t x=w.src[offset+c];
                int16_t value;
                if(x<0){
                    const int32_t product=int32_t(x)*w.slopes[scalar_alpha?0:c];
                    if constexpr(fast_negative){
                        const int32_t q=(product+rounding)>>shift;
                        value=int16_t(std::clamp(q,int32_t(-32768),int32_t(32767)));
                    }else value=requantize_product_s16(product,shift);
                }else if constexpr(identity_positive)value=x;
                else value=requantize_product_s16(x,w.positive_shift);
                w.dst[offset+c]=value;
            }
        }
    }
    template<bool identity_positive,bool scalar_alpha,typename Work>
    inline void prelu_dispatch_shift(const Work &w) {
        if(w.negative_shift>0&&w.negative_shift<=30)prelu_process<identity_positive,scalar_alpha,true>(w);
        else prelu_process<identity_positive,scalar_alpha,false>(w);
    }
}

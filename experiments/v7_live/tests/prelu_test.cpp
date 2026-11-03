#include "../main/v7_prelu_math.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>
int main(){
    assert(v7::prelu_s16_value(1000,8192,-10,-15,-11)==2000);
    assert(v7::prelu_s16_value(-1000,8192,-10,-15,-11)==-500);
    long long checks=0;
    for(int alpha:{0,1,8192,32767,-8192})for(int delta:{-4,-1,0,1,4})for(int x=-32768;x<=32767;x++){
        int aexp=-15,iexp=-10,oexp=iexp+delta;
        double value=x<0?std::ldexp(double(x)*alpha,iexp+aexp-oexp):std::ldexp(double(x),iexp-oexp);
        double expected=std::clamp(std::floor(value+.5),-32768.,32767.);
        assert(v7::prelu_s16_value(int16_t(x),int16_t(alpha),iexp,aexp,oexp)==int16_t(expected));
        assert(v7::prelu_s16_cached(int16_t(x),int16_t(alpha),oexp-iexp,oexp-iexp-aexp)==int16_t(expected));checks++;
    }
    struct Work {const int16_t *src,*slopes;int16_t *dst;int first_row,last_row,channels,alpha_count,positive_shift,negative_shift;};
    int16_t source[256],slopes[256],output[256];
    long long kernel_checks=0;
    for(int positive:{-4,0,1,4})for(int negative:{-2,0,1,15,30,31})for(bool scalar:{false,true}){
        for(int base=-32768;base<=32512;base+=256){
            for(int c=0;c<256;c++){source[c]=int16_t(base+c);slopes[c]=int16_t((c*257)%65536-32768);}
            Work work{source,slopes,output,0,1,256,scalar?1:256,positive,negative};
            if(positive==0){if(scalar)v7::prelu_dispatch_shift<true,true>(work);else v7::prelu_dispatch_shift<true,false>(work);}
            else{if(scalar)v7::prelu_dispatch_shift<false,true>(work);else v7::prelu_dispatch_shift<false,false>(work);}
            for(int c=0;c<256;c++){
                double value=source[c]<0?std::ldexp(double(source[c])*slopes[scalar?0:c],-negative):std::ldexp(double(source[c]),-positive);
                assert(output[c]==int16_t(std::clamp(std::floor(value+.5),-32768.,32767.)));kernel_checks++;
            }
        }
    }
    std::cout<<"PASS: "<<kernel_checks<<" specialized worker cases\n";
    assert(v7::requantize_s16(1,-40)==32767);assert(v7::requantize_s16(-1,-40)==-32768);
    assert(v7::requantize_s16(100,63)==0);
    std::cout<<"PASS: "<<checks<<" INT16 PReLU cases against floating HALF_UP reference, including rescaling and saturation\n";
}

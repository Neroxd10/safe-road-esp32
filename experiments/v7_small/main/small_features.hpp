#pragma once
#include <algorithm>
#include <cmath>
namespace small_features {
enum class Eye { Unknown,Open,Closed,Uncertain };
inline bool calibrated_eyes(float ol,float oright,float cl,float cr){
    return std::isfinite(ol)&&std::isfinite(oright)&&std::isfinite(cl)&&std::isfinite(cr)&&
        ol>.08f&&oright>.08f&&cl>=0&&cr>=0&&cl<ol*.90f&&cr<oright*.90f;
}
inline Eye eyes(float l,float r,float ol,float oright,float cl,float cr,bool usable){
    if(!usable||!calibrated_eyes(ol,oright,cl,cr)||!std::isfinite(l)||!std::isfinite(r)||l<0||r<0)return Eye::Unknown;
    const float ratio=std::max(l/ol,r/oright);
    const float closed_ratio=std::max(cl/ol,cr/oright);
    const float threshold=std::clamp((1.f+closed_ratio)*.5f,.45f,.82f);
    if(ratio<threshold)return Eye::Closed;
    if(ratio>std::max(.9f,threshold+.1f))return Eye::Open;
    return Eye::Uncertain;
}
}

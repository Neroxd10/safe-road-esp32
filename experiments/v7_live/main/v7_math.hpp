#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <type_traits>

namespace v7 {
constexpr float pi=3.14159265358979323846f;
struct Point {float x=0,y=0;};
struct Roi {float cx=0,cy=0,side=0,rotation=0;};
struct Detection {float cx=0,cy=0,w=0,h=0,score=0;Point eye0,eye1;};
inline float distance(Point a,Point b){return std::hypot(a.x-b.x,a.y-b.y);}
inline float sigmoid(float x){return 1.f/(1.f+std::exp(-std::clamp(x,-40.f,40.f)));}
inline Point project(const Roi &r,float u,float v) {
    float x=(u-.5f)*r.side,y=(v-.5f)*r.side;
    return {r.cx+std::cos(r.rotation)*x-std::sin(r.rotation)*y,
            r.cy+std::sin(r.rotation)*x+std::cos(r.rotation)*y};
}
inline Point unproject(const Roi &r,Point p) {
    float x=p.x-r.cx,y=p.y-r.cy;
    return {.5f+(std::cos(r.rotation)*x+std::sin(r.rotation)*y)/r.side,
            .5f+(-std::sin(r.rotation)*x+std::cos(r.rotation)*y)/r.side};
}
inline Point anchor(int index) {
    const int grid=index<512?16:8, repeats=index<512?2:6;
    int cell=(index<512?index:index-512)/repeats;
    return {(cell%grid+.5f)/grid,(cell/grid+.5f)/grid};
}
inline Detection decode(int index,const float *box,float score,int width,int height) {
    const Point a=anchor(index);const float side=float(std::max(width,height));
    const float ox=(width-side)*.5f,oy=(height-side)*.5f;
    Detection d;
    d.cx=(box[0]/128.f+a.x)*side+ox;d.cy=(box[1]/128.f+a.y)*side+oy;
    d.w=box[2]/128.f*side;d.h=box[3]/128.f*side;d.score=score;
    d.eye0={(box[4]/128.f+a.x)*side+ox,(box[5]/128.f+a.y)*side+oy};
    d.eye1={(box[6]/128.f+a.x)*side+ox,(box[7]/128.f+a.y)*side+oy};
    return d;
}
inline float iou(const Detection &a,const Detection &b) {
    float w=std::max(0.f,std::min(a.cx+a.w*.5f,b.cx+b.w*.5f)-std::max(a.cx-a.w*.5f,b.cx-b.w*.5f));
    float h=std::max(0.f,std::min(a.cy+a.h*.5f,b.cy+b.h*.5f)-std::max(a.cy-a.h*.5f,b.cy-b.h*.5f));
    return w*h/std::max(1e-6f,a.w*a.h+b.w*b.h-w*h);
}
inline Roi detection_roi(Detection d) {
    if(d.eye0.x>d.eye1.x)std::swap(d.eye0,d.eye1);
    return {d.cx,d.cy,1.5f*std::max(d.w,d.h),std::atan2(d.eye1.y-d.eye0.y,d.eye1.x-d.eye0.x)};
}
inline float ear(const Point *p,const int *ids) {
    return (distance(p[ids[1]],p[ids[5]])+distance(p[ids[2]],p[ids[4]])) /
           std::max(1e-5f,2*distance(p[ids[0]],p[ids[3]]));
}
inline void pixel(const uint8_t *rgb,int w,int h,float x,float y,bool replicate,float *out) {
    int ix=int(std::floor(x)),iy=int(std::floor(y));float fx=x-ix,fy=y-iy;
    for(int c=0;c<3;c++) {
        float value=0;
        for(int dy=0;dy<2;dy++)for(int dx=0;dx<2;dx++) {
            int px=ix+dx,py=iy+dy;
            float v=0;
            if(replicate){px=std::clamp(px,0,w-1);py=std::clamp(py,0,h-1);}
            if(px>=0&&py>=0&&px<w&&py<h)v=rgb[(py*w+px)*3+c];
            value+=v*(dx?fx:1-fx)*(dy?fy:1-fy);
        }
        out[c]=value;
    }
}
// Resolve border coordinates and source addresses once for all RGB channels.
// Keep the reference multiply/add order to preserve quantized model inputs.
inline void pixel_shared(const uint8_t *rgb,int w,int h,float x,float y,bool replicate,float *out) {
    int ix=int(std::floor(x)),iy=int(std::floor(y));float fx=x-ix,fy=y-iy;
    out[0]=out[1]=out[2]=0;
    for(int dy=0;dy<2;dy++)for(int dx=0;dx<2;dx++){
        int px=ix+dx,py=iy+dy;
        if(replicate){px=std::clamp(px,0,w-1);py=std::clamp(py,0,h-1);}
        const uint8_t *source=(px>=0&&py>=0&&px<w&&py<h)?rgb+(py*w+px)*3:nullptr;
        const float wx=dx?fx:1-fx,wy=dy?fy:1-fy;
        for(int k=0;k<3;k++)out[k]+=float(source?source[k]:0)*wx*wy;
    }
}
// Row ranges allow two workers to prepare disjoint portions of one tensor.
template<typename T>
inline void image_tensor_rows(T *dst,int exponent,const uint8_t *rgb,int w,int h,const Roi &r,int size,bool detection,int first,int last) {
    const float c=std::cos(r.rotation),sn=std::sin(r.rotation);
    const float quant_scale=std::ldexp(1.f,-exponent);
    for(int y=first;y<last;y++)for(int x=0;x<size;x++){
        float hx=((x+.5f)/size-.5f)*r.side,vy=((y+.5f)/size-.5f)*r.side;
        Point p{r.cx+c*hx-sn*vy,r.cy+sn*hx+c*vy};float channels[3];
        pixel_shared(rgb,w,h,p.x-.5f,p.y-.5f,!detection,channels);
        for(int k=0;k<3;k++){
            float value=detection?channels[k]/127.5f-1.f:channels[k]/255.f;
            if constexpr(std::is_same_v<T,float>)dst[(y*size+x)*3+k]=value;
            else {
                float q=std::floor(value*quant_scale+.5f);
                constexpr float lo=std::is_same_v<T,int8_t>?-128.f:-32768.f;
                constexpr float hi=std::is_same_v<T,int8_t>?127.f:32767.f;
                dst[(y*size+x)*3+k]=T(std::clamp(q,lo,hi));
            }
        }
    }
}
template<typename T>
inline void image_tensor(T *dst,int exponent,const uint8_t *rgb,int w,int h,const Roi &r,int size,bool detection) {
    image_tensor_rows(dst,exponent,rgb,w,h,r,size,detection,0,size);
}
// MediaPipe face_blendshapes_graph.cc: 146 selected landmarks, original-image XY pixels.
constexpr int subset[146]={
0,1,4,5,6,7,8,10,13,14,17,21,33,37,39,40,46,52,53,54,55,58,61,63,65,66,67,70,78,80,
81,82,84,87,88,91,93,95,103,105,107,109,127,132,133,136,144,145,146,148,149,150,152,153,
154,155,157,158,159,160,161,162,163,168,172,173,176,178,181,185,191,195,197,234,246,249,
251,263,267,269,270,276,282,283,284,285,288,291,293,295,296,297,300,308,310,311,312,314,
317,318,321,323,324,332,334,336,338,356,361,362,365,373,374,375,377,378,379,380,381,382,
384,385,386,387,388,389,390,397,398,400,402,405,409,415,454,466,468,469,470,471,472,473,
474,475,476,477};
}

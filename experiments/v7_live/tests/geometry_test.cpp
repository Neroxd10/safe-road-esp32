#include "../main/v7_math.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstdlib>
static bool near(float a,float b,float e=1e-4f){return std::fabs(a-b)<e;}
template<typename T>static void tensor_parity(const uint8_t *rgb,int exponent,bool detection){
    for(float rotation:{-.75f,0.f,.37f})for(float side:{1.f,2.5f}){
        v7::Roi r{.9f,.8f,side,rotation};constexpr int size=32;
        std::vector<T> actual(size*size*3),split(size*size*3);
        v7::image_tensor(actual.data(),exponent,rgb,2,2,r,size,detection);
        v7::image_tensor_rows(split.data(),exponent,rgb,2,2,r,size,detection,0,13);
        v7::image_tensor_rows(split.data(),exponent,rgb,2,2,r,size,detection,13,size);
        assert(actual==split);
        for(int y=0;y<size;y++)for(int x=0;x<size;x++){
            auto p=v7::project(r,(x+.5f)/size,(y+.5f)/size);float channels[3];v7::pixel(rgb,2,2,p.x-.5f,p.y-.5f,!detection,channels);
            for(int c=0;c<3;c++){
                float v=detection?channels[c]/127.5f-1.f:channels[c]/255.f;T expected;
                if constexpr(std::is_same_v<T,float>)expected=v;
                else{float q=std::floor(std::ldexp(v,-exponent)+.5f);expected=T(std::clamp(q,std::is_same_v<T,int8_t>?-128.f:-32768.f,std::is_same_v<T,int8_t>?127.f:32767.f));}
                assert(actual[(y*size+x)*3+c]==expected);
            }
        }
    }
}
int main(int argc,char **argv){
    using namespace v7;
    Roi r{320,240,200,.3f};
    for(int i=0;i<100;i++){float u=(i%10)/9.f,v=(i/10)/9.f;auto p=project(r,u,v);auto q=unproject(r,p);assert(near(u,q.x)&&near(v,q.y));}
    assert(near(anchor(0).x,.5f/16));assert(near(anchor(511).y,15.5f/16));assert(near(anchor(512).x,.5f/8));assert(near(anchor(895).y,7.5f/8));
    float b[16]={};b[2]=b[3]=32;b[4]=-8;b[6]=8;auto d=decode(256,b,.9f,640,480);assert(near(d.w,160)&&near(d.eye1.x-d.eye0.x,80));assert(near(iou(d,d),1));
    Point eye[6]={{0,0},{1,-.3f},{2,-.3f},{3,0},{2,.3f},{1,.3f}};int ids[]={0,1,2,3,4,5};assert(near(ear(eye,ids),.2f));
    uint8_t image[12]={0,0,0,100,100,100,200,200,200,240,240,240};float color[3];pixel(image,2,2,.5f,.5f,true,color);assert(near(color[0],135));pixel(image,2,2,-1,0,false,color);assert(near(color[0],0));
    for(bool detector:{false,true}){
        tensor_parity<int8_t>(image,-7,detector);tensor_parity<int16_t>(image,-14,detector);tensor_parity<float>(image,0,detector);
    }
    // Color channels and borders on a full camera-sized frame.
    std::vector<uint8_t> camera(640*480*3);
    uint32_t state=1234567;
    for(auto &v:camera){state=1664525u*state+1013904223u;v=uint8_t(state>>24);}
    for(int i=0;i<20000;i++){
        state=1664525u*state+1013904223u;float x=float(state%80000)/100.f-80.f;
        state=1664525u*state+1013904223u;float y=float(state%64000)/100.f-80.f;
        for(bool replicate:{false,true}){
            float reference[3],actual[3];
            pixel(camera.data(),640,480,x,y,replicate,reference);
            pixel_shared(camera.data(),640,480,x,y,replicate,actual);
            for(int c=0;c<3;c++)assert(actual[c]==reference[c]);
        }
    }
    std::cout<<"PASS: 120000 camera-color sampling values match exactly\n";
    if(argc==3||argc==7){
        std::ifstream input(argv[1],std::ios::binary);std::string magic;int w,h,maxval;input>>magic>>w>>h>>maxval;input.get();assert(magic=="P6"&&maxval==255);std::vector<uint8_t> rgb(w*h*3);input.read((char*)rgb.data(),rgb.size());assert(input.gcount()==(std::streamsize)rgb.size());
        Roi roi=argc==7?Roi{float(std::atof(argv[3])),float(std::atof(argv[4])),float(std::atof(argv[5])),float(std::atof(argv[6]))}:Roi{w*.5f,h*.5f,float(std::max(w,h)),0};
        int size=argc==7?256:128;std::ofstream output(argv[2],std::ios::binary);
        for(int y=0;y<size;y++)for(int x=0;x<size;x++){auto p=project(roi,(x+.5f)/size,(y+.5f)/size);float c[3];pixel(rgb.data(),w,h,p.x-.5f,p.y-.5f,argc==7,c);for(float &v:c)v=argc==7?v/255.f:v/127.5f-1;output.write((char*)c,sizeof(c));}
    }
    std::cout<<"PASS: ROI inverse, 896 anchor layout, box decoding, EAR and bilinear border tests\n";
}

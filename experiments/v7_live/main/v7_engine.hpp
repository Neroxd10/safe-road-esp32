#pragma once
#include "v7_math.hpp"
#include "v7_prelu16.hpp"
#include "v7_selftest_math.hpp"
#include "dl_model_base.hpp"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <new>

extern const uint8_t v7_detector_data[] asm("_binary_detector_espdl_start");
extern const uint8_t v7_landmark_data[] asm("_binary_landmark_espdl_start");
extern const uint8_t v7_expression_data[] asm("_binary_expression_espdl_start");

namespace v7 {
struct Result {
    Point points[478];
    float left_ear=0,right_ear=0,mouth_ratio=0,blink_left=0,blink_right=0,jaw=0;
    float confidence=0,detector_score=0,box[4]={};
    const char *reason="Waiting for a usable face";
    float inference_ms=0,detector_ms=0,preprocess_ms=0,landmark_ms=0,expression_ms=0,prelu_ms=0;
    bool reacquired=false;
};
inline float read(dl::TensorBase *t,int index) {
    float value=0;
    switch(t->get_dtype()){
    case dl::DATA_TYPE_INT8:value=t->get_element_ptr<int8_t>()[index];break;
    case dl::DATA_TYPE_INT16:value=t->get_element_ptr<int16_t>()[index];break;
    case dl::DATA_TYPE_FLOAT:return t->get_element_ptr<float>()[index];
    default:return NAN;
    }
    return std::ldexp(value,t->get_exponent());
}
inline void write(dl::TensorBase *t,int index,float value) {
    if(t->get_dtype()==dl::DATA_TYPE_FLOAT){t->get_element_ptr<float>()[index]=value;return;}
    float q=std::floor(std::ldexp(value,-t->get_exponent())+.5f);
    if(t->get_dtype()==dl::DATA_TYPE_INT8)t->get_element_ptr<int8_t>()[index]=int8_t(std::clamp(q,-128.f,127.f));
    else if(t->get_dtype()==dl::DATA_TYPE_INT16)t->get_element_ptr<int16_t>()[index]=int16_t(std::clamp(q,-32768.f,32767.f));
}
class Engine {
    dl::Model *detector=nullptr,*landmark=nullptr,*expression=nullptr;
    Roi roi{};
    bool roi_valid=false;
    int tracked=0;
    static bool landmark_self_test(dl::Model *model,dl::runtime_mode_t mode) {
        auto *fbs=model->get_fbs_model();if(!fbs)return false;
        fbs->load_map();bool valid=true;
        for(const auto &entry:model->get_inputs()){
            auto *reference=fbs->get_test_input_tensor(entry.first);
            if(!reference||!entry.second||!entry.second->assign(reference))valid=false;
            delete reference;
        }
        if(!valid){fbs->clear_map();return false;}
        model->run(mode);
        LandmarkTestErrors errors;float total_xy=0;int xy_count=0;int checked=0;
        for(const auto &entry:model->get_outputs()){
            auto *reference=fbs->get_test_output_tensor(entry.first);auto *actual=entry.second;
            if(!reference||!actual||reference->get_size()!=actual->get_size()){delete reference;valid=false;continue;}
            for(int i=0;i<actual->get_size();i++){
                float a=read(actual,i),b=read(reference,i),delta=std::fabs(a-b);
                if(!std::isfinite(a)||!std::isfinite(b)){valid=false;continue;}
                if(entry.first=="Identity"&&actual->get_size()==1434){
                    if(i%3==2)errors.max_z=std::max(errors.max_z,delta);
                    else{total_xy+=delta;xy_count++;errors.max_xy=std::max(errors.max_xy,delta);}
                }else if(entry.first=="Identity_1"){
                    errors.presence_logit=std::max(errors.presence_logit,delta);errors.face_confidence=sigmoid(a);
                }else if(entry.first=="Identity_2")errors.auxiliary=std::max(errors.auxiliary,delta);
                else valid=false;
            }
            checked++;delete reference;
        }
        fbs->clear_map();errors.mean_xy=xy_count?total_xy/xy_count:INFINITY;
        valid=valid&&checked==3&&xy_count==956&&valid_landmark_test(errors);
        ESP_LOGI("V7_ENGINE","Landmark reference errors: meanXY=%.4fpx maxXY=%.4fpx maxZ=%.4fpx logit=%.6f aux=%.6f face=%.6f",
           errors.mean_xy,errors.max_xy,errors.max_z,errors.presence_logit,errors.auxiliary,errors.face_confidence);
        if(!valid)ESP_LOGE("V7_ENGINE","Reference limits: meanXY<=0.10px maxXY<=0.50px maxZ<=1px logit<=0.025 aux<=0.002; all heads required");
        return valid;
    }
    static dl::Model *load(const uint8_t *data) {
        auto *m=new(std::nothrow) dl::Model(reinterpret_cast<const char *>(data),
          fbs::MODEL_LOCATION_IN_FLASH_RODATA,0,dl::MEMORY_MANAGER_GREEDY,nullptr,true);
        if(!m)return nullptr;
        for(auto *t:{&m->get_inputs(),&m->get_outputs()}) {
            if(t->empty()){delete m;return nullptr;}
            for(const auto &entry:*t)if(!entry.second||!entry.second->get_element_ptr()){delete m;return nullptr;}
        }
        return m;
    }
    static dl::TensorBase *output(dl::Model *model,int count) {
        for(auto &entry:model->get_outputs())if(entry.second->get_size()==count)return entry.second;
        return nullptr;
    }
    // Reuse the runtime's synchronized workers; inference starts after both finish.
    class ImagePreparation final:public dl::module::Module {
        struct Work {dl::TensorBase *input;const uint8_t *rgb;int w,h;Roi roi;int size;bool detection;int first,last;};
    public:
        ImagePreparation():Module("image_preparation",dl::MODULE_NON_INPLACE,dl::QUANT_TYPE_NONE){}
        std::vector<std::vector<int>> get_output_shape(std::vector<std::vector<int>> &shapes)override{return shapes;}
        void forward(dl::ModelContext *,dl::runtime_mode_t)override{}
        void forward_args(void *arg)override {
            const auto &w=*static_cast<Work *>(arg);auto *input=w.input;
            switch(input->get_dtype()){
            case dl::DATA_TYPE_INT8:image_tensor_rows(input->get_element_ptr<int8_t>(),input->exponent,w.rgb,w.w,w.h,w.roi,w.size,w.detection,w.first,w.last);break;
            case dl::DATA_TYPE_INT16:image_tensor_rows(input->get_element_ptr<int16_t>(),input->exponent,w.rgb,w.w,w.h,w.roi,w.size,w.detection,w.first,w.last);break;
            case dl::DATA_TYPE_FLOAT:image_tensor_rows(input->get_element_ptr<float>(),input->exponent,w.rgb,w.w,w.h,w.roi,w.size,w.detection,w.first,w.last);break;
            default:break;
            }
        }
        void prepare(dl::TensorBase *input,const uint8_t *rgb,int w,int h,const Roi &roi,int size,bool detection){
            Work first{input,rgb,w,h,roi,size,detection,0,size/2};
            Work second=first;second.first=size/2;second.last=size;
            dl::module::module_forward_dual_core(this,&first,&second);
        }
    };
    static void image_input(dl::TensorBase *input,const uint8_t *rgb,int w,int h,const Roi &r,int size,bool detection) {
        ImagePreparation preparation;
        preparation.prepare(input,rgb,w,h,r,size,detection);
    }

    bool detect(const uint8_t *rgb,int w,int h,Result &r) {
        const int64_t detector_start=esp_timer_get_time();
        auto *input=detector->get_inputs().begin()->second;
        image_input(input,rgb,w,h,{w*.5f,h*.5f,float(std::max(w,h)),0},128,true);
        detector->run(dl::RUNTIME_MODE_MULTI_CORE);
        r.detector_ms=(esp_timer_get_time()-detector_start)/1000.f;
        auto *boxes=output(detector,896*16),*scores=output(detector,896);
        if(!boxes||!scores){r.reason="Detector output layout invalid";return false;}
        Detection candidates[32];int n=0;
        for(int i=0;i<896;i++) {
            float score=sigmoid(read(scores,i));if(std::isfinite(score))r.detector_score=std::max(r.detector_score,score);if(score<.65f)continue;
            float b[16];for(int k=0;k<16;k++)b[k]=read(boxes,i*16+k);
            Detection d=decode(i,b,score,w,h);
            if(d.w<24||d.h<24||!std::isfinite(d.cx)||!std::isfinite(d.cy))continue;
            if(n<32)candidates[n++]=d;
            else if(d.score>candidates[n-1].score)candidates[n-1]=d;
            std::sort(candidates,candidates+n,[](const Detection &a,const Detection &b){return a.score>b.score;});
        }
        if(!n){r.reason=r.detector_score<.65f?"Detector confidence too low":"Detector face boxes invalid";return false;}
        // Weighted suppression of overlapping detections. Reject a second face.
        const Detection best=candidates[0];Detection sum{};float total=0;
        for(int i=0;i<n;i++) {
            const auto &d=candidates[i];if(iou(best,d)<.3f){if(d.score>.75f){r.reason="More than one face candidate";return false;}continue;}
            float k=d.score;total+=k;sum.cx+=d.cx*k;sum.cy+=d.cy*k;sum.w+=d.w*k;sum.h+=d.h*k;
            sum.eye0.x+=d.eye0.x*k;sum.eye0.y+=d.eye0.y*k;sum.eye1.x+=d.eye1.x*k;sum.eye1.y+=d.eye1.y*k;
        }
        if(total<=0){r.reason="Face detection merge failed";return false;}
        sum.cx/=total;sum.cy/=total;sum.w/=total;sum.h/=total;
        sum.eye0.x/=total;sum.eye0.y/=total;sum.eye1.x/=total;sum.eye1.y/=total;
        roi=detection_roi(sum);if(roi.side<48||roi.side>std::max(w,h)*1.6f){r.reason="Detected face crop size invalid";return false;}return true;
    }
public:
    ~Engine(){delete expression;delete landmark;delete detector;}
    bool init() {
        register_exact_prelu16();
        // Model metadata and STL allocations go to PSRAM too, preserving internal
        // memory for camera DMA, networking and FreeRTOS stacks.
        heap_caps_malloc_extmem_enable(0);
        detector=load(v7_detector_data);landmark=load(v7_landmark_data);expression=load(v7_expression_data);
        heap_caps_malloc_extmem_enable(4096);
        if(!detector||!landmark||!expression)return false;
        auto *d=detector->get_inputs().begin()->second,*l=landmark->get_inputs().begin()->second,*e=expression->get_inputs().begin()->second;
        if(!(d->shape==std::vector<int>({1,128,128,3})&&l->shape==std::vector<int>({1,256,256,3})&&
          e->get_size()==292&&e->get_dtype()==dl::DATA_TYPE_FLOAT))return false;
        // The new landmark artifact uses a real face crop as its embedded test.
        // Validate it once before trusting live camera predictions.
        const esp_err_t check=(landmark_self_test(landmark,dl::RUNTIME_MODE_SINGLE_CORE)&&landmark_self_test(landmark,dl::RUNTIME_MODE_MULTI_CORE))?ESP_OK:ESP_FAIL;
        ExactPRelu16::log_paths();
        ESP_LOGI("V7_ENGINE","Real-face landmark self-test: %s",esp_err_to_name(check));
        return check==ESP_OK;
    }
    bool run(const uint8_t *rgb,int w,int h,Result &r) {
        const int64_t start=esp_timer_get_time();
        if(!roi_valid||tracked>=12) {
            roi_valid=detect(rgb,w,h,r);tracked=0;r.reacquired=true;
            if(!roi_valid)return false;
        }
        const int64_t prep_start=esp_timer_get_time();
        image_input(landmark->get_inputs().begin()->second,rgb,w,h,roi,256,false);
        r.preprocess_ms=(esp_timer_get_time()-prep_start)/1000.f;
        ExactPRelu16::elapsed_us=0;const int64_t landmark_start=esp_timer_get_time();
        landmark->run(dl::RUNTIME_MODE_MULTI_CORE);
        r.landmark_ms=(esp_timer_get_time()-landmark_start)/1000.f;
        r.prelu_ms=ExactPRelu16::elapsed_us/1000.f;
        auto *coords=output(landmark,1434);
        auto it=landmark->get_outputs().find("Identity_1");
        if(!coords||it==landmark->get_outputs().end()){r.reason="Landmark output layout invalid";roi_valid=false;return false;}
        r.confidence=sigmoid(read(it->second,0));
        if(!std::isfinite(r.confidence)||r.confidence<.6f){r.reason="Landmark face confidence too low";roi_valid=false;return false;}
        float x0=float(w),y0=float(h),x1=0,y1=0;
        for(int i=0;i<478;i++) {
            float u=read(coords,3*i)/256.f,v=read(coords,3*i+1)/256.f;
            if(!std::isfinite(u)||!std::isfinite(v)||u<-.3f||u>1.3f||v<-.3f||v>1.3f){r.reason="Landmarks outside face crop";roi_valid=false;return false;}
            r.points[i]=project(roi,u,v);x0=std::min(x0,r.points[i].x);x1=std::max(x1,r.points[i].x);
            y0=std::min(y0,r.points[i].y);y1=std::max(y1,r.points[i].y);
        }
        if(x1-x0<w*.16f||y1-y0<h*.20f){r.reason="Face too small; move closer";roi_valid=false;return false;}
        if(x0<0||y0<0||x1>=w||y1>=h){r.reason="Face reaches image edge; center your face";roi_valid=false;return false;}
        auto *input=expression->get_inputs().begin()->second;
        for(int i=0;i<146;i++){write(input,2*i,r.points[subset[i]].x);write(input,2*i+1,r.points[subset[i]].y);}
        const int64_t expression_start=esp_timer_get_time();
        expression->run(dl::RUNTIME_MODE_MULTI_CORE);
        r.expression_ms=(esp_timer_get_time()-expression_start)/1000.f;
        auto *blend=output(expression,52);if(!blend){r.reason="Expression output layout invalid";roi_valid=false;return false;}
        r.blink_left=std::clamp(read(blend,9),0.f,1.f);r.blink_right=std::clamp(read(blend,10),0.f,1.f);r.jaw=std::clamp(read(blend,25),0.f,1.f);
        const int left[6]={33,160,158,133,153,144},right[6]={362,385,387,263,373,380};
        r.left_ear=ear(r.points,left);r.right_ear=ear(r.points,right);
        r.mouth_ratio=distance(r.points[13],r.points[14])/std::max(1e-5f,distance(r.points[78],r.points[308]));
        if(!std::isfinite(r.left_ear)||!std::isfinite(r.right_ear)||!std::isfinite(r.mouth_ratio)||!std::isfinite(r.jaw)||!std::isfinite(r.blink_left)||!std::isfinite(r.blink_right)){r.reason="Invalid eye or mouth measurements";roi_valid=false;return false;}
        r.box[0]=x0;r.box[1]=y0;r.box[2]=x1;r.box[3]=y1;
        Point a=r.points[33],b=r.points[263];if(a.x>b.x)std::swap(a,b);
        roi={(x0+x1)*.5f,(y0+y1)*.5f,1.5f*std::max(x1-x0,y1-y0),std::atan2(b.y-a.y,b.x-a.x)};
        r.reason="Face measurements valid";tracked++;r.inference_ms=(esp_timer_get_time()-start)/1000.f;return true;
    }
};
}

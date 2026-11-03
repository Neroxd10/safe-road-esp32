#pragma once
#include <cstdint>
#include <cctype>
#include <cstring>
namespace alerts {
constexpr int event_count=5;
constexpr int thresholds[event_count]={1500,3000,2500,2500,2500};
constexpr const char *event_names[event_count]={"eyes_closed","mouth_open","head_down","looking_away","head_tilt"};
constexpr const char *event_labels[event_count]={"Eyes closed-like","Mouth open-like","Head down-like","Looking away-like","Head tilt-like"};
struct Gate {
    bool fired[event_count]={};
    int64_t last[event_count]={};
    bool take(int event,int64_t duration,int64_t now,int cooldown_seconds){
        if(duration<=0){fired[event]=false;return false;}
        if(duration<thresholds[event]||fired[event])return false;
        fired[event]=true;
        if(last[event]&&now-last[event]<int64_t(cooldown_seconds)*1000)return false;
        last[event]=now;return true;
    }
    void reset(){for(auto &v:fired)v=false;}
};
inline bool email_address(const char *s){
    size_t n=std::strlen(s);if(n<5||n>95)return false;
    const char *at=std::strchr(s,'@');if(!at||at==s||!at[1]||std::strchr(at+1,'@')||!std::strchr(at+1,'.'))return false;
    for(const unsigned char *p=(const unsigned char *)s;*p;p++)
        if(!std::isalnum(*p)&&!std::strchr("@._%+-",*p))return false;
    return true;
}
inline bool https_url(const char *s){
    if(std::strncmp(s,"https://",8)||std::strlen(s)>511)return false;
    const char *host=s+8;if(!*host||*host=='/'||*host=='?'||*host=='#')return false;
    for(const unsigned char *p=(const unsigned char *)s;*p;p++)if(*p<=32||*p>=127||*p=='@'||*p=='#'||*p=='\\')return false;
    return true;
}
}

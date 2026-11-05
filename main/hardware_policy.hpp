#pragma once
#include "alert_policy.hpp"
namespace hardware {
inline int alert_event(bool monitoring,bool face,int64_t age,const int64_t *durations,const int *limits=alerts::thresholds,uint32_t events=31){
 if(!monitoring||!face||age<0||age>2500)return -1;
 for(int i=0;i<alerts::event_count;i++)if((events&(1u<<i))&&durations[i]>=limits[i])return i;
 return -1;
}
inline bool pulse(int event,int64_t now_ms){
 if(event<0)return false;
 const int phase=int(now_ms%1600);
 return phase<180||(phase>=340&&phase<520)||(event==0&&phase>=680&&phase<860);
}
}

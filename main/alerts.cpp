#include "alerts.hpp"
#include "alert_policy.hpp"
#include "smtp_protocol.hpp"
#include <cstring>
#include <ctime>
#include <string>
#include "esp_http_client.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
namespace alerts {
struct Config {
    uint32_t version=1;
    bool email=false,webhook=false;
    uint32_t events=31,cooldown=60;
    char user[96]={},recipient[96]={},password[33]={},url[512]={};
};
struct Item {uint32_t revision;int event;int64_t duration,created;uint8_t channels;};
static Config config;
static int hold_ms[5]={1500,3000,2500,2500,2500};
static uint32_t revision=1;
static SemaphoreHandle_t mutex=nullptr;
static QueueHandle_t queue=nullptr;
static std::atomic<bool> *online=nullptr;
static char device[32]={},email_result[96]="Not sent yet",webhook_result[96]="Not sent yet";
static Gate gate;
static std::atomic<bool> started{false};
static std::atomic<uint32_t> reset_generation{0};
static uint32_t seen_generation=0;
static bool lock(){return mutex&&xSemaphoreTake(mutex,pdMS_TO_TICKS(30))==pdTRUE;}
static void unlock(){xSemaphoreGive(mutex);}
static cJSON *error(const char *s){auto *d=cJSON_CreateObject();cJSON_AddStringToObject(d,"error",s);return d;}
static bool ready(){return online&&online->load();}
static void result(bool email,const char *text){if(lock()){snprintf(email?email_result:webhook_result,96,"%s",text);unlock();}}
struct TlsTransport {
    esp_tls_t *tls=nullptr;
    int64_t deadline=0;
    TlsTransport(){tls=esp_tls_init();}
    ~TlsTransport(){if(tls)esp_tls_conn_destroy(tls);}
    bool connect(){
        esp_tls_cfg_t cfg={};cfg.crt_bundle_attach=esp_crt_bundle_attach;cfg.timeout_ms=8000;
        return tls&&esp_tls_conn_new_sync("smtp.gmail.com",14,465,&cfg,tls)==1;
    }
    bool write_all(const std::string &s){
        const int64_t until=esp_timer_get_time()+10000000;size_t offset=0;
        while(offset<s.size()&&esp_timer_get_time()<until){
            int n=esp_tls_conn_write(tls,s.data()+offset,s.size()-offset);
            if(n==ESP_TLS_ERR_SSL_WANT_READ||n==ESP_TLS_ERR_SSL_WANT_WRITE){vTaskDelay(1);continue;}
            if(n<=0)return false;
            offset+=n;
        }
        deadline=esp_timer_get_time()+10000000;return offset==s.size();
    }
    bool read_char(char &c){
        if(!deadline)deadline=esp_timer_get_time()+10000000;
        while(esp_timer_get_time()<deadline){
            int n=esp_tls_conn_read(tls,&c,1);
            if(n==1)return true;
            if(n!=ESP_TLS_ERR_SSL_WANT_READ&&n!=ESP_TLS_ERR_SSL_WANT_WRITE)return false;
            vTaskDelay(1);
        }
        return false;
    }
};
static std::string event_body(const Item &item){
    char body[512];
    const char *label=item.event<0?"Test notification":event_labels[item.event];
    snprintf(body,sizeof(body),"SAFE ROAD\nDevice: %s\nEvent: %s\nObserved duration: %.3f seconds\nDevice uptime: %.3f seconds\n\nExperimental camera-based monitoring event; not a confirmed drowsiness or yawn diagnosis.\n",
        device,label,double(item.duration)/1000.0,double(item.created)/1000.0);
    return body;
}
static void send_email(const Config &c,const Item &item){
    TlsTransport transport;
    if(!transport.connect()){result(true,"Gmail TLS connection failed; check internet and clock");return;}
    const char *err=smtp_send(transport,c.user,c.password,c.recipient,event_body(item));
    result(true,err?err:"Email accepted by Gmail");
}
static void send_webhook(const Config &c,const Item &item){
    cJSON *doc=cJSON_CreateObject();if(!doc){result(false,"Out of memory");return;}
    cJSON_AddStringToObject(doc,"device",device);
    cJSON_AddStringToObject(doc,"event",item.event<0?"test":event_names[item.event]);
    cJSON_AddNumberToObject(doc,"duration_ms",double(item.duration));
    cJSON_AddNumberToObject(doc,"uptime_ms",double(item.created));
    cJSON_AddBoolToObject(doc,"experimental",true);
    const auto body=event_body(item);cJSON_AddStringToObject(doc,"text",body.c_str());cJSON_AddStringToObject(doc,"content",body.c_str());
    char *json=cJSON_PrintUnformatted(doc);cJSON_Delete(doc);
    if(!json){result(false,"Out of memory");return;}
    esp_http_client_config_t cfg={};cfg.url=c.url;cfg.method=HTTP_METHOD_POST;cfg.timeout_ms=8000;
    cfg.crt_bundle_attach=esp_crt_bundle_attach;cfg.disable_auto_redirect=true;
    auto client=esp_http_client_init(&cfg);
    if(!client){free(json);result(false,"Could not create HTTPS client");return;}
    esp_http_client_set_header(client,"Content-Type","application/json");
    esp_http_client_set_post_field(client,json,strlen(json));
    const esp_err_t err=esp_http_client_perform(client);const int status=esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);free(json);
    char message[96];
    if(err!=ESP_OK)snprintf(message,sizeof(message),"Webhook connection failed; check URL and internet");
    else snprintf(message,sizeof(message),"Webhook %s (HTTP %d)",status>=200&&status<300?"accepted":"rejected",status);
    result(false,message);
}
static void worker(void *){
    Item item;
    while(true){
        if(xQueueReceive(queue,&item,portMAX_DELAY)!=pdTRUE)continue;
        Config c;uint32_t current;
        if(!lock())continue;
        c=config;current=revision;unlock();
        if(item.revision!=current)continue;
        auto fail=[&](const char *s){if(item.channels&1)result(true,s);if(item.channels&2)result(false,s);};
        if(esp_timer_get_time()/1000-item.created>30000){fail("Expired; no old notification sent");continue;}
        if(!ready()){fail("Offline; notification skipped");continue;}
        if(time(nullptr)<1700000000){fail("Waiting for internet time sync; try again shortly");continue;}
        if(item.channels&1)send_email(c,item);
        if((item.channels&2)&&ready())send_webhook(c,item);
    }
}
bool init(const char *id,std::atomic<bool> *connected){
    online=connected;snprintf(device,sizeof(device),"%s",id);
    mutex=xSemaphoreCreateMutex();queue=xQueueCreate(6,sizeof(Item));
    if(!mutex||!queue)return false;
    nvs_handle_t handle;
    if(nvs_open("sr_alerts",NVS_READONLY,&handle)==ESP_OK){
        Config saved;size_t size=sizeof(saved);
        if(nvs_get_blob(handle,"config",&saved,&size)==ESP_OK&&size==sizeof(saved)&&saved.version==1){
            saved.user[95]=saved.recipient[95]=saved.password[32]=saved.url[511]=0;
            if(saved.cooldown>=30&&saved.cooldown<=3600&&saved.events<=31&&
               (!saved.email||(email_address(saved.user)&&email_address(saved.recipient)&&strlen(saved.password)==16))&&
               (!saved.webhook||https_url(saved.url)))config=saved;
        }
        int saved_hold[5];size_t hold_size=sizeof(saved_hold);
        if(nvs_get_blob(handle,"hold_ms",saved_hold,&hold_size)==ESP_OK&&hold_size==sizeof(saved_hold)){
            bool valid=true;for(int value:saved_hold)valid=valid&&valid_hold_ms(value);
            if(valid)memcpy(hold_ms,saved_hold,sizeof(hold_ms));
        }
        nvs_close(handle);
    }
    const bool ok=xTaskCreate(worker,"notifications",10240,nullptr,2,nullptr)==pdPASS;
    started.store(ok);return ok;
}
bool local_policy(int *values,uint32_t &events){
    if(!lock())return false;
    memcpy(values,hold_ms,sizeof(hold_ms));events=config.events;unlock();return true;
}
void reset_episode(){reset_generation.fetch_add(1);}
void observe(const int64_t durations[5]){
    if(!started.load())return;
    const auto generation=reset_generation.load();if(generation!=seen_generation){gate.reset();seen_generation=generation;}
    Config c;uint32_t rev;int limits[5];
    if(!lock())return;
    c=config;rev=revision;memcpy(limits,hold_ms,sizeof(limits));unlock();
    const int64_t now=esp_timer_get_time()/1000;
    const uint8_t channels=(c.email?1:0)|(c.webhook?2:0);
    for(int i=0;i<event_count;i++)if(gate.take(i,durations[i],now,c.cooldown,limits[i])&&(c.events&(1u<<i))&&channels){
        if(!ready()){if(c.email)result(true,"Offline; monitoring continues");if(c.webhook)result(false,"Offline; monitoring continues");continue;}
        Item item{rev,i,durations[i],now,channels};
        if(xQueueSend(queue,&item,0)!=pdTRUE){if(c.email)result(true,"Notification queue full");if(c.webhook)result(false,"Notification queue full");}
    }
}
static bool boolean(cJSON *d,const char *name,bool &dest){auto *v=cJSON_GetObjectItemCaseSensitive(d,name);if(!cJSON_IsBool(v))return false;dest=cJSON_IsTrue(v);return true;}
static bool string(cJSON *d,const char *name,char *dest,size_t size,bool keep_empty=false){
    auto *v=cJSON_GetObjectItemCaseSensitive(d,name);if(!cJSON_IsString(v)||strlen(v->valuestring)>=size)return false;
    if(!keep_empty||v->valuestring[0])snprintf(dest,size,"%s",v->valuestring);
    return true;
}
cJSON *request(cJSON *doc){
    if(!started.load())return error("Notifications unavailable");
    auto *action=cJSON_GetObjectItemCaseSensitive(doc,"action");if(!cJSON_IsString(action))return error("Action required");
    if(!lock())return error("Settings busy; retry");
    Config next=config;uint32_t rev=revision;int next_hold[5];memcpy(next_hold,hold_ms,sizeof(next_hold));unlock();
    if(!strcmp(action->valuestring,"load")){
        auto *out=cJSON_CreateObject();cJSON_AddBoolToObject(out,"email_enabled",next.email);cJSON_AddBoolToObject(out,"webhook_enabled",next.webhook);
        cJSON_AddStringToObject(out,"email",next.user);cJSON_AddStringToObject(out,"recipient",next.recipient);
        cJSON_AddBoolToObject(out,"password_saved",next.password[0]);cJSON_AddBoolToObject(out,"webhook_saved",next.url[0]);
        cJSON_AddNumberToObject(out,"events",next.events);cJSON_AddNumberToObject(out,"cooldown",next.cooldown);cJSON_AddItemToObject(out,"hold_ms",cJSON_CreateIntArray(next_hold,5));return out;
    }
    if(!strcmp(action->valuestring,"test_email")||!strcmp(action->valuestring,"test_webhook")){
        const bool email=!strcmp(action->valuestring,"test_email");
        if(!ready())return error("Connect the device to internet Wi-Fi first");
        if((email&&!next.email)||(!email&&!next.webhook))return error("Enable and save this channel first");
        Item item{rev,-1,0,esp_timer_get_time()/1000,uint8_t(email?1:2)};
        if(xQueueSend(queue,&item,0)!=pdTRUE)return error("Notification queue full; retry shortly");
        result(email,"Test queued");auto *out=cJSON_CreateObject();cJSON_AddStringToObject(out,"message","Test queued; check delivery status below");return out;
    }
    if(strcmp(action->valuestring,"save"))return error("Unknown action");
    if(!boolean(doc,"email_enabled",next.email)||!boolean(doc,"webhook_enabled",next.webhook)||
       !string(doc,"email",next.user,sizeof(next.user))||!string(doc,"recipient",next.recipient,sizeof(next.recipient))||
       !string(doc,"app_password",next.password,sizeof(next.password),true)||!string(doc,"webhook_url",next.url,sizeof(next.url),true))return error("Invalid settings fields");
    auto *events=cJSON_GetObjectItemCaseSensitive(doc,"events"),*cooldown=cJSON_GetObjectItemCaseSensitive(doc,"cooldown");
    if(!cJSON_IsNumber(events)||events->valuedouble!=events->valueint||events->valueint<0||events->valueint>31||
       !cJSON_IsNumber(cooldown)||cooldown->valuedouble!=cooldown->valueint||cooldown->valueint<30||cooldown->valueint>3600)return error("Choose events and a cooldown from 30 to 3600 seconds");
    next.events=events->valueint;next.cooldown=cooldown->valueint;
    auto *holds=cJSON_GetObjectItemCaseSensitive(doc,"hold_ms");
    if(!cJSON_IsArray(holds)||cJSON_GetArraySize(holds)!=5)return error("Set a hold time for each detection");
    for(int i=0;i<5;i++){
        auto *value=cJSON_GetArrayItem(holds,i);
        if(!cJSON_IsNumber(value)||value->valuedouble!=value->valueint||!valid_hold_ms(value->valueint))return error("Detection hold times must be 0.5 to 60 seconds");
        next_hold[i]=value->valueint;
    }
    // Google displays the 16-character app password in groups separated by spaces.
    {size_t n=0;for(size_t i=0;next.password[i];i++)if(next.password[i]!=' ')next.password[n++]=next.password[i];next.password[n]=0;}
    if(next.email&&(!email_address(next.user)||!email_address(next.recipient)||strlen(next.password)!=16))return error("Gmail needs sender, one recipient, and a 16-character app password");
    if(next.webhook&&!https_url(next.url))return error("Webhook must be an HTTPS URL without user info or fragment");
    nvs_handle_t handle;esp_err_t err=nvs_open("sr_alerts",NVS_READWRITE,&handle);
    if(err!=ESP_OK)return error("Could not open saved settings");
    err=nvs_set_blob(handle,"config",&next,sizeof(next));if(err==ESP_OK)err=nvs_set_blob(handle,"hold_ms",next_hold,sizeof(next_hold));if(err==ESP_OK)err=nvs_commit(handle);nvs_close(handle);
    if(err!=ESP_OK)return error("Could not save notification settings");
    if(!lock())return error("Settings saved; reload to confirm");
    config=next;memcpy(hold_ms,next_hold,sizeof(hold_ms));++revision;unlock();reset_episode();
    auto *out=cJSON_CreateObject();cJSON_AddStringToObject(out,"message","Notification settings saved on device");return out;
}
bool enabled_channels(bool &email,bool &webhook){
    if(!started.load()||!lock())return false;
    email=config.email;
    webhook=config.webhook;
    unlock();
    return true;
}
void status(cJSON *doc){
    if(!started.load()||!lock())return;
    auto *out=cJSON_AddObjectToObject(doc,"notifications");
    cJSON_AddBoolToObject(out,"email_enabled",config.email);cJSON_AddBoolToObject(out,"webhook_enabled",config.webhook);
    cJSON_AddStringToObject(out,"email_status",email_result);cJSON_AddStringToObject(out,"webhook_status",webhook_result);
    cJSON_AddBoolToObject(out,"clock_ready",time(nullptr)>=1700000000);unlock();
}
}

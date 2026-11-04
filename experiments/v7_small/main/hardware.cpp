#include "hardware.hpp"
#include "alerts.hpp"
#include "hardware_policy.hpp"
#include "oled_font.hpp"
#include "sdkconfig.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <atomic>
#include <initializer_list>
#include <cstring>
#include <cstdio>
namespace hardware {
static ReadSnapshot read_snapshot=nullptr;
static i2c_master_bus_handle_t bus=nullptr;
static i2c_master_dev_handle_t oled=nullptr;
static std::atomic<bool> display_ok{false};
static std::atomic<int64_t> internet_at{0};
static bool buzzer_ok=false;
static constexpr int pin=CONFIG_SAFE_ROAD_BUZZER_GPIO;
static_assert(CONFIG_SAFE_ROAD_OLED_SDA!=CONFIG_SAFE_ROAD_OLED_SCL);
static_assert(pin!=CONFIG_SAFE_ROAD_OLED_SDA&&pin!=CONFIG_SAFE_ROAD_OLED_SCL);
static bool transfer(const uint8_t *data,size_t length){return oled&&i2c_master_transmit(oled,data,length,40)==ESP_OK;}
static bool command(const uint8_t *data,size_t count){
 uint8_t packet[32]={};if(count>31)return false;memcpy(packet+1,data,count);return transfer(packet,count+1);
}
static void init_oled(){
 i2c_master_bus_config_t cfg={};
 // Camera SCCB uses port 1 in this build. OLED uses the other controller.
 cfg.i2c_port=I2C_NUM_0;cfg.sda_io_num=gpio_num_t(CONFIG_SAFE_ROAD_OLED_SDA);cfg.scl_io_num=gpio_num_t(CONFIG_SAFE_ROAD_OLED_SCL);
 cfg.clk_source=I2C_CLK_SRC_DEFAULT;cfg.glitch_ignore_cnt=7;cfg.flags.enable_internal_pullup=true;
 if(i2c_new_master_bus(&cfg,&bus)!=ESP_OK){ESP_LOGW("HARDWARE","OLED bus unavailable");return;}
 for(uint8_t address:{uint8_t(0x3c),uint8_t(0x3d)}){
  if(i2c_master_probe(bus,address,40)!=ESP_OK)continue;
  i2c_device_config_t device={};device.dev_addr_length=I2C_ADDR_BIT_LEN_7;device.device_address=address;device.scl_speed_hz=100000;
  if(i2c_master_bus_add_device(bus,&device,&oled)==ESP_OK)break;
 }
 const uint8_t setup[]={0xae,0xd5,0x80,0xa8,0x3f,0xd3,0,0x40,0xad,0x8b,0xa1,0xc8,0xda,0x12,0x81,0x7f,0xd9,0x22,0xdb,0x35,0xa4,0xa6,0xaf};
 display_ok.store(command(setup,sizeof(setup)));
 ESP_LOGI("HARDWARE","SH1106 %s; SDA=%d SCL=%d",display_ok.load()?"ready":"not found",CONFIG_SAFE_ROAD_OLED_SDA,CONFIG_SAFE_ROAD_OLED_SCL);
}
static void line(uint8_t *buffer,int row,const char *s){
 for(int x=0;s[x]&&x<21;x++)glyph(s[x],buffer+row*128+x*6);
}
static const char *eye(const char *s){return !strcmp(s,"OPEN-LIKE")?"OPEN":!strcmp(s,"CLOSED-LIKE")?"CLOSED":"UNKNOWN";}
static void draw(const Snapshot &s,int event){
 uint8_t frame[1024]={};char text[32];
 line(frame,0,"SAFE ROAD");line(frame,1,s.wifi?"WIFI: CONNECTED":"WIFI: OFFLINE");
 line(frame,2,s.wifi&&internet_recent()?"INTERNET: VERIFIED":"INTERNET: UNVERIFIED");
 if(s.calibrating){snprintf(text,sizeof(text),"CALIBRATE %d/9 %d/12",s.stage+1,s.samples);line(frame,3,text);line(frame,4,s.waiting?"TAP READY ON PHONE":"HOLD POSE");}
 else line(frame,3,s.monitoring?(s.face&&s.age_ms<2500?"MONITORING ACTIVE":"MONITOR: NO FACE"):"TAP CALIBRATE");
 if(!s.calibrating){snprintf(text,sizeof(text),"EYES: %s",eye(s.eye));line(frame,4,text);snprintf(text,sizeof(text),"MOUTH: %s",!strcmp(s.mouth,"OPEN-LIKE")?"OPEN":!strcmp(s.mouth,"SHUT-LIKE")?"CLOSED":"UNKNOWN");line(frame,5,text);}
 constexpr const char *labels[]={"ALERT: EYES CLOSED","ALERT: MOUTH OPEN","ALERT: HEAD DOWN","ALERT: LOOKING AWAY","ALERT: HEAD TILT"};
 line(frame,6,event>=0?labels[event]:"192.168.4.1");line(frame,7,"EXPERIMENTAL MONITOR");
 for(int page=0;page<8;page++){
  const uint8_t position[]={uint8_t(0xb0+page),0x02,0x10}; // SH1106 visible columns 2..129.
  uint8_t data[129];data[0]=0x40;memcpy(data+1,frame+page*128,128);
  if(!command(position,3)||!transfer(data,sizeof(data))){display_ok.store(false);ESP_LOGW("HARDWARE","OLED disconnected; monitoring continues");break;}
 }
}
static void set_buzzer(bool on){
 if(!buzzer_ok)return;
#ifdef CONFIG_SAFE_ROAD_BUZZER_ACTIVE
 gpio_set_level(gpio_num_t(pin),on?0:1);
#else
 if(on){ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_1,512);ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_1);}
 else ledc_stop(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_1,1); // Low-trigger module: HIGH is silent.
#endif
}
static void init_buzzer(){
 gpio_set_level(gpio_num_t(pin),1);gpio_set_direction(gpio_num_t(pin),GPIO_MODE_OUTPUT);
#ifdef CONFIG_SAFE_ROAD_BUZZER_ACTIVE
 buzzer_ok=true;
#else
 ledc_timer_config_t timer={};timer.speed_mode=LEDC_LOW_SPEED_MODE;timer.timer_num=LEDC_TIMER_1;timer.duty_resolution=LEDC_TIMER_10_BIT;timer.freq_hz=2400;timer.clk_cfg=LEDC_AUTO_CLK;
 ledc_channel_config_t channel={};channel.gpio_num=pin;channel.speed_mode=LEDC_LOW_SPEED_MODE;channel.channel=LEDC_CHANNEL_1;channel.timer_sel=LEDC_TIMER_1;channel.duty=1023;
 buzzer_ok=ledc_timer_config(&timer)==ESP_OK&&ledc_channel_config(&channel)==ESP_OK;
#endif
 set_buzzer(false);ESP_LOGI("HARDWARE","Buzzer GPIO=%d %s; camera LEDC timer/channel 0 preserved",pin,buzzer_ok?"ready":"unavailable");
}
static void task(void *){
 init_buzzer();init_oled();int64_t next_draw=0;bool previous=false;
 while(true){
  Snapshot s;const bool valid=read_snapshot&&read_snapshot(s);const int64_t now=esp_timer_get_time()/1000;
  int limits[5];uint32_t events=0;
  const bool policy=alerts::local_policy(limits,events);
  int event=valid&&policy?alert_event(s.monitoring,s.face,s.age_ms,s.durations,limits,events):-1;
  bool on=pulse(event,now);if(on!=previous){set_buzzer(on);previous=on;}
  if(display_ok.load()&&now>=next_draw){if(valid)draw(s,event);next_draw=now+750;}
  vTaskDelay(pdMS_TO_TICKS(30));
 }
}
static void internet_task(void *){
 while(true){
  Snapshot s;
  if(read_snapshot&&read_snapshot(s)&&s.wifi){
   esp_http_client_config_t cfg={};cfg.url="https://connectivitycheck.gstatic.com/generate_204";cfg.method=HTTP_METHOD_HEAD;cfg.timeout_ms=5000;cfg.crt_bundle_attach=esp_crt_bundle_attach;cfg.disable_auto_redirect=true;
   auto client=esp_http_client_init(&cfg);
   if(client){if(esp_http_client_perform(client)==ESP_OK&&esp_http_client_get_status_code(client)==204)internet_at.store(esp_timer_get_time()/1000);else internet_at.store(0);esp_http_client_cleanup(client);}
  }else internet_at.store(0);
  vTaskDelay(pdMS_TO_TICKS(30000));
 }
}
bool internet_recent(){const auto at=internet_at.load();return at>0&&esp_timer_get_time()/1000-at<65000;}
bool oled_ready(){return display_ok.load();}
void init(ReadSnapshot reader){
 read_snapshot=reader;
 if(xTaskCreate(task,"oled_buzzer",4096,nullptr,3,nullptr)!=pdPASS)ESP_LOGE("HARDWARE","Cannot start OLED/buzzer task");
 if(xTaskCreate(internet_task,"internet_probe",8192,nullptr,1,nullptr)!=pdPASS)ESP_LOGW("HARDWARE","Cannot start internet check");
}
}

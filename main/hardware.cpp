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
#include <ctime>
namespace hardware {
static ReadSnapshot read_snapshot=nullptr;
static i2c_master_bus_handle_t bus=nullptr;
static i2c_master_dev_handle_t oled=nullptr;
static std::atomic<bool> display_ok{false};
static std::atomic<int64_t> internet_at{0};
static std::atomic<unsigned> internet_generation{0};
static std::atomic<unsigned> verified_generation{0};
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
 const esp_err_t bus_result=i2c_new_master_bus(&cfg,&bus);
 if(bus_result!=ESP_OK){
  ESP_LOGW("HARDWARE","OLED bus unavailable (SDA=%d SCL=%d): %s",cfg.sda_io_num,cfg.scl_io_num,esp_err_to_name(bus_result));
  return;
 }
 vTaskDelay(pdMS_TO_TICKS(100));
 ESP_LOGI("HARDWARE","OLED diagnostics: I2C0 SDA=%d SCL=%d, configured driver SH1106",cfg.sda_io_num,cfg.scl_io_num);
 for(uint8_t address:{uint8_t(0x3c),uint8_t(0x3d)}){
  const esp_err_t probe=i2c_master_probe(bus,address,40);
  ESP_LOGI("HARDWARE","OLED probe 0x%02X: %s",unsigned(address),probe==ESP_OK?"ACK":esp_err_to_name(probe));
  if(probe!=ESP_OK)continue;
  i2c_device_config_t device={};device.dev_addr_length=I2C_ADDR_BIT_LEN_7;device.device_address=address;device.scl_speed_hz=100000;
  const esp_err_t added=i2c_master_bus_add_device(bus,&device,&oled);
  if(added!=ESP_OK){
   ESP_LOGW("HARDWARE","OLED address responded but device setup failed: %s",esp_err_to_name(added));
   return;
  }
  ESP_LOGI("HARDWARE","OLED address 0x%02X responds; address does not identify controller",unsigned(address));
  break;
 }
 if(!oled){
  unsigned found=0;
  for(unsigned address=0x08;address<=0x77;address++){
   if(address==0x3c||address==0x3d)continue;
   const esp_err_t probe=i2c_master_probe(bus,address,20);
   if(probe==ESP_ERR_TIMEOUT){
    ESP_LOGW("HARDWARE","OLED bus scan timed out; check SDA/SCL and pull-ups");
    return;
   }
   if(probe==ESP_OK){
    ESP_LOGI("HARDWARE","Other I2C device responds at 0x%02X",address);
    found++;
   }
  }
  ESP_LOGW("HARDWARE","No response at OLED addresses 0x3C/0x3D; %u other addresses responded",found);
  return;
 }
 const uint8_t setup[]={0,0xae,0xd5,0x80,0xa8,0x3f,0xd3,0,0x40,0xad,0x8b,0xa1,0xc8,0xda,0x12,0x81,0x7f,0xd9,0x22,0xdb,0x35,0xa4,0xa6,0xaf};
 const esp_err_t initialized=i2c_master_transmit(oled,setup,sizeof(setup),40);
 display_ok.store(initialized==ESP_OK);
 if(initialized==ESP_OK)ESP_LOGI("HARDWARE","SH1106 setup commands sent successfully");
 else ESP_LOGW("HARDWARE","OLED responded to probe but SH1106 setup transfer failed: %s",esp_err_to_name(initialized));
}
static void line(uint8_t *buffer,int row,const char *s,bool centered=false){
 int length=0;
 while(length<21&&s[length])++length;
 const int start=centered?(128-length*6+1)/2:0;
 for(int x=0;x<length;x++)glyph(s[x],buffer+row*128+start+x*6);
}
static void draw_caution(uint8_t *frame){
 constexpr int center=64,top=7,height=27,half_base=19;
 auto pixel=[frame](int x,int y){frame[(y/8)*128+x]|=uint8_t(1u<<(y%8));};
 for(int y=0;y<height;y++){
  const int half_width=y*half_base/(height-1);
  pixel(center-half_width,top+y);
  pixel(center+half_width,top+y);
 }
 for(int x=center-half_base;x<=center+half_base;x++)pixel(x,top+height-1);
 for(int y=top+9;y<=top+17;y++)pixel(center,y);
 pixel(center,top+21);
}
static void draw(const Snapshot &s,int event){
 uint8_t frame[1024]={};
 const bool camera_ready=!s.calibrating&&s.monitoring&&s.age_ms>=0&&s.age_ms<2500;
 if(event>=0){
  constexpr const char *labels[]={"EYES CLOSED","MOUTH OPEN","HEAD DOWN","LOOKING AWAY","HEAD TILT"};
  draw_caution(frame);
  line(frame,5,"ALERT!",true);
  line(frame,6,labels[event],true);
 }else if(camera_ready&&s.face){
  char text[32];
  line(frame,0,"MONITORING",true);
  line(frame,1,!s.wifi?"WIFI: OFFLINE":internet_recent()?"WIFI: VERIFIED":"WIFI: UNVERIFIED");
  line(frame,2,"FACE: DETECTED");
  const char *eyes=!strcmp(s.eye,"OPEN-LIKE")?"OPEN":!strcmp(s.eye,"CLOSED-LIKE")?"CLOSED":"-";
  const char *mouth=!strcmp(s.mouth,"OPEN-LIKE")?"OPEN":!strcmp(s.mouth,"SHUT-LIKE")?"CLOSED":"-";
  snprintf(text,sizeof(text),"EYES:%s M:%s",eyes,mouth);line(frame,3,text);
  snprintf(text,sizeof(text),"HEAD DOWN: %s",s.down?"YES":"NO");line(frame,4,text);
  snprintf(text,sizeof(text),"HEAD TILT: %s",s.tilt);line(frame,5,text);
  snprintf(text,sizeof(text),"LOOKING AWAY: %s",s.away);line(frame,6,text);
  line(frame,7,"192.168.4.1");
 }else if(camera_ready){
  constexpr uint8_t face_icon[]={0xe7,0x81,0x81,0x28,0x10,0x10,0x10,0x28,0x81,0x81,0xe7};
  constexpr uint8_t wifi_icon[]={4,2,9,5,21,69,21,5,9,2,4};
  memcpy(frame+4,face_icon,sizeof(face_icon));
  memcpy(frame+113,wifi_icon,sizeof(wifi_icon));
  if(!s.wifi)for(int x=0;x<8;x++)frame[114+x]|=uint8_t(1u<<(7-x));
  line(frame,3,"SAFE ROAD",true);
  line(frame,7,"192.168.4.1",true);
 }else{
  line(frame,0,"SAFE ROAD");
  if(s.calibrating){
   char text[32];
   snprintf(text,sizeof(text),"CALIBRATE %d/9 %d/12",s.stage+1,s.samples);
   line(frame,2,text);
   line(frame,3,s.waiting?"TAP READY ON PHONE":"HOLD POSE");
  }else line(frame,2,s.monitoring?"WAITING FOR CAMERA":"TAP CALIBRATE");
  line(frame,7,"192.168.4.1");
 }
 for(int page=0;page<8;page++){
  // Reposition each chunk so a retry can redraw the entire frame.
  constexpr int chunk_size=32;
  for(int x=0;x<128;x+=chunk_size){
   const int column=x+2; // SH1106 visible columns 2..129.
   const uint8_t position[]={uint8_t(0xb0+page),uint8_t(column&0x0f),uint8_t(0x10|(column>>4))};
   uint8_t data[chunk_size+1];data[0]=0x40;memcpy(data+1,frame+page*128+x,chunk_size);
   if(!command(position,sizeof(position))||!transfer(data,sizeof(data))){
    if(display_ok.exchange(false))ESP_LOGW("HARDWARE","OLED refresh failed at page %d column %d; retrying",page,x);
    return;
   }
  }
 }
 if(!display_ok.exchange(true))ESP_LOGI("HARDWARE","OLED refresh recovered");
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
 ledc_timer_config_t timer={};timer.speed_mode=LEDC_LOW_SPEED_MODE;timer.timer_num=LEDC_TIMER_1;timer.duty_resolution=LEDC_TIMER_10_BIT;timer.freq_hz=CONFIG_SAFE_ROAD_BUZZER_FREQUENCY_HZ;timer.clk_cfg=LEDC_AUTO_CLK;
 ledc_channel_config_t channel={};channel.gpio_num=pin;channel.speed_mode=LEDC_LOW_SPEED_MODE;channel.channel=LEDC_CHANNEL_1;channel.timer_sel=LEDC_TIMER_1;channel.duty=1023;
 buzzer_ok=ledc_timer_config(&timer)==ESP_OK&&ledc_channel_config(&channel)==ESP_OK;
#endif
 set_buzzer(false);ESP_LOGI("HARDWARE","Buzzer GPIO=%d %s; camera LEDC timer/channel 0 preserved",pin,buzzer_ok?"ready":"unavailable");
}
static void task(void *){
 init_buzzer();init_oled();int64_t next_draw=0;bool previous=false;
 uint32_t pending_milestones=0;
 int display_event=-1;
 int64_t display_until=0;
 while(true){
  Snapshot s;const bool valid=read_snapshot&&read_snapshot(s);const int64_t now=esp_timer_get_time()/1000;
  int limits[5];uint32_t events=0;
  const bool policy=alerts::local_policy(limits,events);
  int event=valid&&policy?alert_event(s.monitoring,s.face,s.age_ms,s.durations,limits,events):-1;
  bool on=pulse(event,now);if(on!=previous){set_buzzer(on);previous=on;}
  if(oled&&valid&&now>=next_draw){
   pending_milestones|=alerts::take_display_milestones();
   if(now>=display_until){
    display_event=-1;
    for(int i=0;i<alerts::event_count;i++){
     const uint32_t bit=1u<<i;
     if(!(pending_milestones&bit))continue;
     pending_milestones&=~bit;
     display_event=i;
     display_until=now+2250;
     break;
    }
   }
   draw(s,display_event);
   next_draw=now+750;
  }
  vTaskDelay(pdMS_TO_TICKS(30));
 }
}
void internet_connection_changed(){
 internet_generation.fetch_add(1);
 internet_at.store(0);
}
static void internet_task(void *){
 unsigned generation=internet_generation.load();
 int64_t next_check=0;
 while(true){
  const unsigned current=internet_generation.load();
  if(current!=generation){generation=current;next_check=0;}
  Snapshot s;
  const auto now=esp_timer_get_time()/1000;
  if(read_snapshot&&read_snapshot(s)&&s.wifi&&time(nullptr)>=1700000000&&now>=next_check){
   esp_http_client_config_t cfg={};cfg.url="https://connectivitycheck.gstatic.com/generate_204";
   cfg.method=HTTP_METHOD_GET;cfg.timeout_ms=5000;cfg.crt_bundle_attach=esp_crt_bundle_attach;
   cfg.disable_auto_redirect=true;
   auto client=esp_http_client_init(&cfg);
   esp_err_t result=ESP_ERR_NO_MEM;
   int status=0;
   if(client){
    result=esp_http_client_perform(client);
    status=esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
   }
   if(internet_generation.load()==generation){
    const bool verified=result==ESP_OK&&status==204;
    verified_generation.store(generation);
    internet_at.store(verified?esp_timer_get_time()/1000:0);
    if(!verified)ESP_LOGW("HARDWARE","Internet probe failed: %s HTTP %d",esp_err_to_name(result),status);
    next_check=esp_timer_get_time()/1000+(verified?30000:10000);
   }
  }
  vTaskDelay(pdMS_TO_TICKS(1000));
 }
}
bool internet_recent(){
 const auto at=internet_at.load();
 return at>0&&verified_generation.load()==internet_generation.load()&&esp_timer_get_time()/1000-at<65000;
}
bool oled_ready(){return display_ok.load();}
void init(ReadSnapshot reader){
 read_snapshot=reader;
 if(xTaskCreate(task,"oled_buzzer",4096,nullptr,3,nullptr)!=pdPASS)ESP_LOGE("HARDWARE","Cannot start OLED/buzzer task");
 if(xTaskCreate(internet_task,"internet_probe",8192,nullptr,1,nullptr)!=pdPASS)ESP_LOGW("HARDWARE","Cannot start internet check");
}
}

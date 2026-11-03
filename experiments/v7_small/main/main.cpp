// S.A.F.E. R.O.A.D. / Standalone Stage 4 (Wi-Fi + local browser dashboard)
// ESP32-S3 N16R8 + OV5640, ESP-IDF 5.5.x / human_face_detect 0.5.2
// MediaPipe detector and smaller 468-landmark geometry experiment.
// Head orientation remains a calibrated 2D proxy, not MediaPipe's 3D transform.
// Experimental local monitoring; no buzzer, SMS or driving decisions.


#include <algorithm>
#include <cmath>
#include <cstdint>
#include <new>
#include <atomic>
#include <cstdio>
#include <cstring>
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "bootloader_random.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "freertos/semphr.h"
#include "esp_camera.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "v7_engine.hpp"
#include "small_features.hpp"
#include "dl_image_jpeg.hpp"


static constexpr char TAG[] = "SAFE_ROAD";
static constexpr float PI_F = 3.14159265358979323846f;
static constexpr int EYE_W = 16, EYE_H = 10, EYE_N = 2 * EYE_W * EYE_H;
static constexpr int MOUTH_W = 16, MOUTH_H = 12, MOUTH_N = MOUTH_W * MOUTH_H;
static constexpr int CAPTURE_SAMPLES = 12;
static v7::Result g_v7_result;
static constexpr int64_t READY_US = 4000000;

// Web requests only read a protected snapshot; neural inference always runs on ESP32.
struct Telemetry {
    bool face = false;
    float ear_l=0,ear_r=0,jaw=0,blink_l=0,blink_r=0,mouth_ratio=0,confidence=0,inference_ms=0;
    float overlay[32]={};
    char engine_error[80]={};
    char face_reason[96]="Waiting for first face measurements";
    float detector_score=0,landmark_ms=0,prelu_ms=0,preprocess_ms=0,expression_ms=0,detector_ms=0;
    bool calibrated = false;
    bool calibration_running = false, calibration_waiting = false, calibration_saved = false;
    bool preparing = false;
    int stage = 0;
    int samples = 0;
    int wait_s = 0;
    int64_t update_us = 0;
    char eye[20] = "UNKNOWN";
    char mouth[20] = "UNKNOWN";
    char away[16] = "NO";
    char tilt[16] = "NO";
    bool head_down = false;
    float down_score = 0, yaw_delta = 0, roll_delta = 0;
    uint32_t eye_ms = 0, mouth_ms = 0, down_ms = 0, away_ms = 0, tilt_ms = 0;
    bool eye_ok = false, mouth_ok = false, down_ok = false;
    bool yaw_left_ok = false, yaw_right_ok = false, tilt_left_ok = false, tilt_right_ok = false;
};
static Telemetry g_telemetry;
static SemaphoreHandle_t g_telemetry_mutex = nullptr;
static SemaphoreHandle_t g_frame_mutex = nullptr;
static uint8_t *g_frame = nullptr;
static size_t g_frame_size = 0, g_frame_capacity = 0;
static int64_t g_frame_at_us = 0;
// -2: no request; -1: all stages; 0..8: one stage.
static std::atomic<int> g_calibration_request(-2);
static std::atomic<bool> g_wifi_connected(false);
static std::atomic<bool> g_wifi_connecting(false);
static std::atomic<bool> g_wifi_pending(false);
static esp_netif_t *g_sta_netif = nullptr;
static char g_ap_ssid[33] = {};
static char g_ap_password[20] = {};
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

static void capture_preview(const camera_fb_t *fb) {
    const int64_t now = esp_timer_get_time();
    if (!fb || !fb->buf || fb->len == 0 || fb->len > 180000 || now - g_frame_at_us < 900000) return;
    if (xSemaphoreTake(g_frame_mutex, 0) != pdTRUE) return;
    if (fb->len > g_frame_capacity) {
        const size_t cap = fb->len + 4096;
        auto *p = static_cast<uint8_t *>(heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (p) { if (g_frame) heap_caps_free(g_frame); g_frame = p; g_frame_capacity = cap; }
    }
    if (g_frame && fb->len <= g_frame_capacity) {
        memcpy(g_frame, fb->buf, fb->len);
        g_frame_size = fb->len;
        g_frame_at_us = now;
    }
    xSemaphoreGive(g_frame_mutex);
}


struct Pose {
    float ear_l=0,ear_r=0,mouth_ratio=0,jaw=0,blink_l=0,blink_r=0;
    float roll = 0;     // degrees: angle of eye-to-eye line
    float yaw = 0;      // nose deviation along eye-to-eye line / interocular distance
    float pitch = 0;    // nose distance along downward eye-axis / interocular distance
    float mouth_y = 0;  // mouth midpoint distance below eye line / interocular distance
    float face_y = 0;   // face bounding-box vertical center / image height
};

struct PoseSum {
    Pose total = {};
    int n = 0;
    void add(const Pose &p) {
        total.ear_l += p.ear_l;
        total.ear_r += p.ear_r;
        total.mouth_ratio += p.mouth_ratio;
        total.jaw += p.jaw;
        total.blink_l += p.blink_l;
        total.blink_r += p.blink_r;
        total.roll += p.roll;
        total.yaw += p.yaw;
        total.pitch += p.pitch;
        total.mouth_y += p.mouth_y;
        total.face_y += p.face_y;
        ++n;
    }
    Pose mean() const {
        Pose a = {};
        if (!n) return a;
        float k = 1.0f / n;
        a.ear_l = total.ear_l * k;
        a.ear_r = total.ear_r * k;
        a.mouth_ratio = total.mouth_ratio * k;
        a.jaw = total.jaw * k;
        a.blink_l = total.blink_l * k;
        a.blink_r = total.blink_r * k;
        a.roll = total.roll * k;
        a.yaw = total.yaw * k;
        a.pitch = total.pitch * k;
        a.mouth_y = total.mouth_y * k;
        a.face_y = total.face_y * k;
        return a;
    }
};

// Guided capture is started with dashboard buttons; saved monitoring runs without a client.
enum Cal : int {
    EYES_OPEN = 0, EYES_CLOSED, NEUTRAL, MOUTH_OPEN, HEAD_DOWN,
    LOOK_LEFT, LOOK_RIGHT, TILT_LEFT, TILT_RIGHT, STAGE_COUNT
};

static const char *const directions[STAGE_COUNT] = {
    "FACE FORWARD, EYES OPEN, MOUTH CLOSED",
    "FACE FORWARD, BOTH EYES CLOSED",
    "NORMAL NEUTRAL FACE, EYES OPEN, MOUTH CLOSED",
    "FACE FORWARD, OPEN MOUTH VERY WIDE (like a yawn)",
    "FACE FORWARD, LOWER HEAD / LOOK DOWN (NOT just eyes)",
    "TURN HEAD TO YOUR LEFT (not a shoulder tilt)",
    "TURN HEAD TO YOUR RIGHT (not a shoulder tilt)",
    "TILT LEFT EAR TOWARD LEFT SHOULDER (not a head turn)",
    "TILT RIGHT EAR TOWARD RIGHT SHOULDER (not a head turn)"
};

static PoseSum poses[STAGE_COUNT];
static int stage = 0, count = 0;
static bool preparing = false, tracking = false;
static bool single_stage = false;
static bool calibration_running = false, calibration_waiting = false, calibration_saved = false;
static int64_t ready_start = 0;
static int last_ready_second = 99;
static bool eye_good = false, mouth_good = false;
static bool yaw_good[2] = {false, false}, tilt_good[2] = {false, false}, down_good = false;


static void publish_progress(bool valid) {
    if (xSemaphoreTake(g_telemetry_mutex, pdMS_TO_TICKS(15)) != pdTRUE) return;
    g_telemetry.face = valid;
    g_telemetry.calibrated = tracking;
    g_telemetry.calibration_running = calibration_running;
    g_telemetry.calibration_waiting = calibration_waiting;
    g_telemetry.calibration_saved = calibration_saved;
    g_telemetry.preparing = preparing;
    g_telemetry.stage = stage;
    g_telemetry.samples = count;
    const int64_t left = READY_US - (esp_timer_get_time() - ready_start);
    g_telemetry.preparing=preparing && left>0;
    g_telemetry.wait_s = (preparing && left > 0) ? int((left + 999999) / 1000000) : 0;
    g_telemetry.eye_ok = eye_good;
    g_telemetry.mouth_ok = mouth_good;
    g_telemetry.down_ok = down_good;
    g_telemetry.yaw_left_ok = yaw_good[0];
    g_telemetry.yaw_right_ok = yaw_good[1];
    g_telemetry.tilt_left_ok = tilt_good[0];
    g_telemetry.tilt_right_ok = tilt_good[1];
    g_telemetry.update_us = esp_timer_get_time();
    if (!valid || !tracking) {
        strcpy(g_telemetry.eye,"UNKNOWN");
        strcpy(g_telemetry.mouth,"UNKNOWN");
        strcpy(g_telemetry.away,"NO");
        strcpy(g_telemetry.tilt,"NO");
        g_telemetry.head_down = false;
        g_telemetry.eye_ms = g_telemetry.mouth_ms = g_telemetry.down_ms = 0;
        g_telemetry.away_ms = g_telemetry.tilt_ms = 0;
    }
    xSemaphoreGive(g_telemetry_mutex);
}

static esp_err_t start_camera()
{
    camera_config_t c = {};
    c.pin_pwdn=-1; c.pin_reset=-1; c.pin_xclk=15;
    c.pin_sccb_sda=4; c.pin_sccb_scl=5;
    c.pin_d0=11; c.pin_d1=9; c.pin_d2=8; c.pin_d3=10;
    c.pin_d4=12; c.pin_d5=18; c.pin_d6=17; c.pin_d7=16;
    c.pin_vsync=6; c.pin_href=7; c.pin_pclk=13;
    c.ledc_timer=LEDC_TIMER_0; c.ledc_channel=LEDC_CHANNEL_0;
    c.xclk_freq_hz=20000000;
    c.pixel_format=PIXFORMAT_JPEG;
    c.frame_size=FRAMESIZE_VGA;
    c.jpeg_quality=14;
    c.fb_count=2;
    c.fb_location=CAMERA_FB_IN_PSRAM;
    c.grab_mode=CAMERA_GRAB_LATEST;
    const esp_err_t err = esp_camera_init(&c);
    if (err != ESP_OK) return err;
    sensor_t *s = esp_camera_sensor_get();
    if (!s) return ESP_FAIL;
    ESP_LOGI(TAG, "Camera PID: 0x%04x", s->id.PID);
    // Flip at the sensor so detection and dashboard use the same orientation.
    if (s->set_vflip(s, 1) != 0) return ESP_FAIL;
    return ESP_OK;
}

// Keypoint layout for espressif/human_face_detect MSR+MNP model:
// [0,1] first eye, [2,3] first mouth corner, [4,5] nose,
// [6,7] second eye, [8,9] second mouth corner.
static bool measure(const dl::image::img_t &img, const int *kp, const int *box,
                    Pose &pose, bool &have_eyes, bool &have_mouth)
{
    const float lx=float(kp[0]), ly=float(kp[1]);
    const float rx=float(kp[6]), ry=float(kp[7]);
    const float vx=rx-lx, vy=ry-ly;
    const float iod=std::sqrt(vx*vx+vy*vy);
    if (iod < 32.0f || iod > img.width*0.65f || vx < 8.0f) return false;
    const float ux=vx/iod, uy=vy/iod;
    const float ex=(lx+rx)*0.5f, ey=(ly+ry)*0.5f;
    const float nx=float(kp[4]), ny=float(kp[5]);
    const float mx=(float(kp[2])+float(kp[8]))*0.5f;
    const float my=(float(kp[3])+float(kp[9]))*0.5f;
    const float mouth_dx=float(kp[8]-kp[2]);
    const float mouth_dy=float(kp[9]-kp[3]);
    const float mouth_span=std::sqrt(mouth_dx*mouth_dx+mouth_dy*mouth_dy);
    if (mouth_span < 10.0f || mouth_span > iod*1.2f) return false;
    if (box[0]<0 || box[1]<0 || box[2]>=img.width || box[3]>=img.height) return false;
    if (box[2]<=box[0] || box[3]<=box[1]) return false;
    pose.roll=std::atan2(vy, vx)*(180.0f/PI_F);
    pose.yaw=((nx-ex)*ux + (ny-ey)*uy)/iod;
    pose.pitch=(-(nx-ex)*uy + (ny-ey)*ux)/iod;
    pose.mouth_y=(-(mx-ex)*uy+(my-ey)*ux)/iod;
    pose.face_y=float(box[1]+box[3])/(2.0f*img.height);
    have_eyes=have_mouth=true;
    return true;
}

static void begin_next_stage()
{
    ++stage;
    count=0;
    if (stage == STAGE_COUNT) { tracking=true; preparing=false; calibration_running=false; return; }
    preparing=false; calibration_waiting=true;
    ESP_LOGW(TAG, "NEXT: %s. Tap Ready on the dashboard when ready.", directions[stage]);
}

static void check_calibration()
{
    const Pose n=poses[NEUTRAL].mean();
    const Pose down=poses[HEAD_DOWN].mean();
    const Pose left=poses[LOOK_LEFT].mean();
    const Pose right=poses[LOOK_RIGHT].mean();
    const Pose tl=poses[TILT_LEFT].mean();
    const Pose tr=poses[TILT_RIGHT].mean();
    const Pose opened=poses[EYES_OPEN].mean(),closed=poses[EYES_CLOSED].mean(),wide=poses[MOUTH_OPEN].mean();
    eye_good=small_features::calibrated_eyes(opened.ear_l,opened.ear_r,closed.ear_l,closed.ear_r);
    mouth_good=wide.mouth_ratio>n.mouth_ratio+.08f;
    const float dyaw_l=left.yaw-n.yaw, dyaw_r=right.yaw-n.yaw;
    yaw_good[0]=std::fabs(dyaw_l)>=0.045f;
    yaw_good[1]=std::fabs(dyaw_r)>=0.045f;
    // Disallow contradictory/inconsistent turn directions.
    if (dyaw_l*dyaw_r >= 0.0f) { yaw_good[0]=false; yaw_good[1]=false; }
    const float droll_l=tl.roll-n.roll, droll_r=tr.roll-n.roll;
    tilt_good[0]=std::fabs(droll_l)>=8.0f;
    tilt_good[1]=std::fabs(droll_r)>=8.0f;
    if (droll_l*droll_r >= 0.0f) { tilt_good[0]=false; tilt_good[1]=false; }
    const float dp=down.pitch-n.pitch, dm=down.mouth_y-n.mouth_y;
    const float df=(down.face_y-n.face_y)*2.5f;
    down_good=(dp*dp + 0.6f*dm*dm + df*df)>=0.007f;
    ESP_LOGI(TAG, "CALIBRATION eye=%s mouth=%s head-down=%s yaw[L/R]=%s/%s tilt[L/R]=%s/%s",
        eye_good?"OK":"UNRELIABLE",mouth_good?"OK":"UNRELIABLE",
        down_good?"OK":"UNRELIABLE",yaw_good[0]?"OK":"OFF",yaw_good[1]?"OK":"OFF",
        tilt_good[0]?"OK":"OFF",tilt_good[1]?"OK":"OFF");
    ESP_LOGI(TAG, "Calibration geometry neutral: yaw=%.3f pitch=%.3f roll=%.1f deg mouthY=%.3f faceY=%.3f",
             n.yaw,n.pitch,n.roll,n.mouth_y,n.face_y);
    ESP_LOGW(TAG, "STATIONARY EXPERIMENT ONLY. Indicators are NOT proven driver safety or sleepiness detection.");
}

// One versioned NVS blob: incomplete recaptures never overwrite the last saved set.
// Bump version when feature extraction, camera orientation, or this layout changes.
struct SavedCalibration {
    uint32_t version;
    PoseSum pose[STAGE_COUNT];
};
static SavedCalibration saved_data;
static constexpr uint32_t CALIBRATION_VERSION=1; // sensor vflip=1

static bool save_calibration() {
    saved_data.version=CALIBRATION_VERSION;
    memcpy(saved_data.pose,poses,sizeof(poses));
    nvs_handle_t handle;
    esp_err_t err=nvs_open("safe_road",NVS_READWRITE,&handle);
    if (err==ESP_OK) {
        err=nvs_set_blob(handle,"small_cal",&saved_data,sizeof(saved_data));
        if (err==ESP_OK) err=nvs_commit(handle);
        nvs_close(handle);
    }
    if (err!=ESP_OK) ESP_LOGE(TAG,"Calibration save failed: %s",esp_err_to_name(err));
    return err==ESP_OK;
}

static bool load_calibration() {
    nvs_handle_t handle;
    if (nvs_open("safe_road",NVS_READONLY,&handle)!=ESP_OK) return false;
    size_t size=sizeof(saved_data);
    const esp_err_t err=nvs_get_blob(handle,"small_cal",&saved_data,&size);
    nvs_close(handle);
    if (err!=ESP_OK || size!=sizeof(saved_data) || saved_data.version!=CALIBRATION_VERSION) return false;
    for (const auto &pose:saved_data.pose) {
        if (pose.n!=CAPTURE_SAMPLES || !std::isfinite(pose.total.roll) ||
            !std::isfinite(pose.total.yaw) || !std::isfinite(pose.total.pitch) ||
            !std::isfinite(pose.total.mouth_y) || !std::isfinite(pose.total.face_y) ||
            !std::isfinite(pose.total.ear_l) || !std::isfinite(pose.total.ear_r) ||
            !std::isfinite(pose.total.jaw) || !std::isfinite(pose.total.mouth_ratio) ||
            !std::isfinite(pose.total.blink_l) || !std::isfinite(pose.total.blink_r)) return false;
    }
    memcpy(poses,saved_data.pose,sizeof(poses));
    stage=STAGE_COUNT; tracking=true; calibration_saved=true;
    check_calibration();
    ESP_LOGI(TAG,"Saved calibration restored; monitoring active.");
    return true;
}

struct Duration {
    int64_t start=0;
    void reset(){start=0;}
    int64_t update(bool active,int64_t now) {
        if (!active) {start=0;return 0;}
        if (!start) start=now;
        return (now-start)/1000;
    }
};
static Duration hold_eye,hold_mouth,hold_down,hold_away,hold_tilt;
static int64_t last_report_us=0;

static float score_down(const Pose &p)
{
    if (!down_good) return 0;
    const Pose n=poses[NEUTRAL].mean(), d=poses[HEAD_DOWN].mean();
    const float dp=d.pitch-n.pitch, dm=d.mouth_y-n.mouth_y;
    const float df=(d.face_y-n.face_y)*2.5f;
    const float pp=p.pitch-n.pitch, pm=p.mouth_y-n.mouth_y;
    const float pf=(p.face_y-n.face_y)*2.5f;
    const float denominator=dp*dp + 0.6f*dm*dm + df*df;
    if (denominator < 0.0001f) return 0;
    return (pp*dp + 0.6f*pm*dm + pf*df)/denominator;
}

static void track_frame(const Pose &p, bool have_eyes, bool have_mouth)
{
    const Pose n=poses[NEUTRAL].mean();
    const int64_t now=esp_timer_get_time();
    const float droll=p.roll-n.roll;
    const float dyaw=p.yaw-n.yaw;

    bool look_left=false,look_right=false,tilt_left=false,tilt_right=false;
    const float left_delta=poses[LOOK_LEFT].mean().yaw-n.yaw;
    const float right_delta=poses[LOOK_RIGHT].mean().yaw-n.yaw;
    if (yaw_good[0] && dyaw*left_delta>0 && dyaw/left_delta>0.62f) look_left=true;
    if (yaw_good[1] && dyaw*right_delta>0 && dyaw/right_delta>0.62f) look_right=true;
    const float tilt_l_delta=poses[TILT_LEFT].mean().roll-n.roll;
    const float tilt_r_delta=poses[TILT_RIGHT].mean().roll-n.roll;
    if (tilt_good[0] && droll*tilt_l_delta>0 && droll/tilt_l_delta>0.60f && std::fabs(droll)>8.0f) tilt_left=true;
    if (tilt_good[1] && droll*tilt_r_delta>0 && droll/tilt_r_delta>0.60f && std::fabs(droll)>8.0f) tilt_right=true;

    const float down_ratio=score_down(p);
    // Reject sharp sideways or turned poses: the 2D pitch proxy is unreliable there.
    const bool down_like=down_good && down_ratio>0.65f && !(look_left||look_right||tilt_left||tilt_right);
    const bool away_like=look_left||look_right;
    const bool tilt_like=tilt_left||tilt_right;
    const bool geometry_forward=!away_like && !tilt_like && !down_like;

    const Pose opened=poses[EYES_OPEN].mean(),closed=poses[EYES_CLOSED].mean(),wide=poses[MOUTH_OPEN].mean();
    const auto eye_state=small_features::eyes(p.ear_l,p.ear_r,opened.ear_l,opened.ear_r,closed.ear_l,closed.ear_r,
        eye_good&&have_eyes&&geometry_forward);
    const bool eye_closed_like=eye_state==small_features::Eye::Closed;
    const char *eye=eye_state==small_features::Eye::Unknown?"UNKNOWN":eye_closed_like?"CLOSED-LIKE":
        eye_state==small_features::Eye::Open?"OPEN-LIKE":"UNCERTAIN";
    const float mouth_threshold=std::max(.18f,(n.mouth_ratio+wide.mouth_ratio)*.5f);
    const bool mouth_open_like=mouth_good && have_mouth && geometry_forward && p.mouth_ratio>mouth_threshold;
    const char *mouth=!mouth_good||!have_mouth||!geometry_forward?"UNKNOWN":mouth_open_like?"OPEN-LIKE":p.mouth_ratio<mouth_threshold*.8f?"SHUT-LIKE":"UNCERTAIN";

    const int64_t eye_ms=hold_eye.update(eye_closed_like,now);
    const int64_t mouth_ms=hold_mouth.update(mouth_open_like,now);
    const int64_t down_ms=hold_down.update(down_like,now);
    const int64_t away_ms=hold_away.update(away_like,now);
    const int64_t tilt_ms=hold_tilt.update(tilt_like,now);

    if (xSemaphoreTake(g_telemetry_mutex, pdMS_TO_TICKS(15)) == pdTRUE) {
        snprintf(g_telemetry.eye,sizeof(g_telemetry.eye),"%s",eye);
        snprintf(g_telemetry.mouth,sizeof(g_telemetry.mouth),"%s",mouth);
        snprintf(g_telemetry.away,sizeof(g_telemetry.away),"%s",look_left?"LEFT":look_right?"RIGHT":"NO");
        snprintf(g_telemetry.tilt,sizeof(g_telemetry.tilt),"%s",tilt_left?"LEFT":tilt_right?"RIGHT":"NO");
        g_telemetry.head_down = down_like;
        g_telemetry.down_score = down_ratio;
        g_telemetry.yaw_delta = dyaw;
        g_telemetry.roll_delta = droll;
        g_telemetry.eye_ms = static_cast<uint32_t>(eye_ms);
        g_telemetry.mouth_ms = static_cast<uint32_t>(mouth_ms);
        g_telemetry.down_ms = static_cast<uint32_t>(down_ms);
        g_telemetry.away_ms = static_cast<uint32_t>(away_ms);
        g_telemetry.tilt_ms = static_cast<uint32_t>(tilt_ms);
        xSemaphoreGive(g_telemetry_mutex);
    }

    // Print metrics about once a second, plus the state duration. No physical alarms.
    if (now-last_report_us < 1000000) return;
    last_report_us=now;
    ESP_LOGI(TAG,"eye=%s (%lldms) mouth=%s (%lldms) headDown~=%s (%.2f,%lldms)",
        eye,(long long)eye_ms,mouth,(long long)mouth_ms,down_like?"YES":"no",down_ratio,(long long)down_ms);
    ESP_LOGI(TAG,"lookingAway~=%s (%lldms) headTilt~=%s (%lldms,rollDelta=%.1f deg) yawDelta=%.3f",
        look_left?"LEFT":(look_right?"RIGHT":"no"),(long long)away_ms,
        tilt_left?"LEFT":(tilt_right?"RIGHT":"no"),(long long)tilt_ms,droll,dyaw);
    if (mouth_ms>=3000) ESP_LOGW(TAG,"MOUTH OPEN-LIKE >=3s: may be yawning, speaking, etc. NOT validated.");
    if (down_ms>=2500) ESP_LOGW(TAG,"HEAD DOWN-LIKE >=2.5s: unverified 2D head proxy.");
    if (away_ms>=2500) ESP_LOGW(TAG,"LOOKING AWAY-LIKE >=2.5s: unverified yaw proxy.");
    if (tilt_ms>=2500) ESP_LOGW(TAG,"SIDE TILT-LIKE >=2.5s: unverified roll proxy.");
    if (eye_ms>=1500) ESP_LOGW(TAG,"EYES CLOSED-LIKE >=1.5s: experimental only, no alarm.");
}

static void accept_sample(const Pose &p, bool have_eyes, bool have_mouth)
{
    if (!calibration_running || calibration_waiting) return;
    if (preparing) {
        const int64_t remain=READY_US-(esp_timer_get_time()-ready_start);
        if (remain>0) {
            const int secs=int((remain+999999)/1000000);
            if (secs != last_ready_second) {
                ESP_LOGW(TAG,"%s in %ds",directions[stage],secs);
                last_ready_second=secs;
            }
            return;
        }
        preparing=false;
        ESP_LOGW(TAG,"NOW HOLD: %s",directions[stage]);
    }
    // If feature isn't available, skip this frame (never use garbage data).
    if ((stage==EYES_OPEN || stage==EYES_CLOSED) && !have_eyes) return;
    if ((stage==NEUTRAL || stage==MOUTH_OPEN) && !have_mouth) return;
    poses[stage].add(p);
    ++count;
    if (count%3==0) ESP_LOGI(TAG,"CALIBRATION %d/9: %d/%d valid samples",stage+1,count,CAPTURE_SAMPLES);
    if (count < CAPTURE_SAMPLES) return;
    ESP_LOGI(TAG,"Captured %s",directions[stage]);
    if (single_stage) {
        single_stage=false; stage=STAGE_COUNT; count=0; tracking=true; preparing=false; calibration_running=false;
    } else begin_next_stage();
    if (tracking) {
        check_calibration();
        calibration_saved=save_calibration();
        ESP_LOGW(TAG,"CALIBRATION COMPLETE. Return to NEUTRAL and try each motion SEPARATELY.");
        ESP_LOGW(TAG,"Prototypes only: no buzzer, SMS, or safety decisions. Use dashboard buttons to recalibrate.");
    }
}

static void reset_timers() {
    hold_eye.reset();hold_mouth.reset();hold_down.reset();hold_away.reset();hold_tilt.reset();
}


static void recalibrate_stage(int selected) {
    // Runs only in the inference task, preserving every other captured stage.
    if (!tracking) return;
    poses[selected]=PoseSum{};
    stage=selected; count=0; tracking=false; preparing=true; single_stage=true;
    calibration_running=true; calibration_waiting=false; calibration_saved=false;
    ready_start=esp_timer_get_time(); last_ready_second=99;
    reset_timers();
    ESP_LOGW(TAG,"Recalibrating one stage. Prepare: %s",directions[stage]);
}

static void restart_calibration() {
    single_stage=false; calibration_running=true; calibration_waiting=false; calibration_saved=false;
    for (auto &p : poses) p = PoseSum{};
    eye_good = mouth_good = down_good = false;
    yaw_good[0] = yaw_good[1] = tilt_good[0] = tilt_good[1] = false;
    stage=0; count=0; tracking=false; preparing=true;
    ready_start = esp_timer_get_time(); last_ready_second=99;
    reset_timers();
    ESP_LOGW(TAG, "Calibration restarted by dashboard. Prepare: %s", directions[0]);
}

static void face_task(void *)
{
    auto *engine=new(std::nothrow) v7::Engine();
    if (!engine || !engine->init()) {
        ESP_LOGE(TAG,"Small-model loading failed; check memory and model shapes");
        if (xSemaphoreTake(g_telemetry_mutex,pdMS_TO_TICKS(40))==pdTRUE) {
            strcpy(g_telemetry.engine_error,"Small model could not load; inspect serial log");
            xSemaphoreGive(g_telemetry_mutex);
        }
        delete engine;vTaskDelete(nullptr);return;
    }
    ESP_LOGI(TAG,"Small models ready: internal free=%u PSRAM free=%u",unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    ESP_LOGI(TAG,"SMALL LIVE: 192x192 / 468 landmarks; calibrated geometry; no blendshapes");
    ESP_LOGW(TAG,"STATIONARY TEST ONLY. Keep face toward camera, eyes OPEN, mouth CLOSED.");
    ESP_LOGI(TAG,"Eye/mouth: calibrated landmark geometry, entirely on ESP32; fresh calibration required");
    if (!load_calibration()) ESP_LOGI(TAG,"No saved calibration. Tap Calibrate on the dashboard to begin.");
    publish_progress(false);

    while (true) {
        const int request=g_calibration_request.exchange(-2);
        if (request==-3 && calibration_running && calibration_waiting) {
            calibration_waiting=false; preparing=true;
            ready_start=esp_timer_get_time(); last_ready_second=99;
        }
        else if (request==-1) restart_calibration();
        else if (request>=0 && request<STAGE_COUNT) recalibrate_stage(request);
        camera_fb_t *fb=esp_camera_fb_get();
        if (!fb) { reset_timers(); publish_progress(false); ESP_LOGW(TAG,"Camera frame unavailable"); vTaskDelay(pdMS_TO_TICKS(280));continue; }
        capture_preview(fb);
        dl::image::jpeg_img_t jpg={}; jpg.data=(void *)fb->buf; jpg.data_len=fb->len;
        auto img=dl::image::sw_decode_jpeg(jpg,dl::image::DL_IMAGE_PIX_TYPE_RGB888);
        esp_camera_fb_return(fb);
        if (!img.data) {reset_timers();publish_progress(false);ESP_LOGW(TAG,"JPEG decode failed");vTaskDelay(pdMS_TO_TICKS(280));continue;}
        bool valid=false,have_eyes=false,have_mouth=false;
        Pose pose={};
        g_v7_result=v7::Result{};
        const int64_t inference_start=esp_timer_get_time();
        if (engine->run(static_cast<const uint8_t *>(img.data),img.width,img.height,g_v7_result)) {
            const auto &r=g_v7_result;
            const int ids[5]={33,61,1,263,291};int kp[10],box[4];
            for(int i=0;i<5;i++){kp[2*i]=int(std::lround(r.points[ids[i]].x));kp[2*i+1]=int(std::lround(r.points[ids[i]].y));}
            for(int i=0;i<4;i++)box[i]=int(r.box[i]);
            valid=measure(img,kp,box,pose,have_eyes,have_mouth);
            if(!valid)g_v7_result.reason="Face geometry rejected by pose checks";
            if(valid){
                pose.ear_l=r.left_ear;pose.ear_r=r.right_ear;pose.mouth_ratio=r.mouth_ratio;
                pose.jaw=r.jaw;pose.blink_l=r.blink_left;pose.blink_r=r.blink_right;
                have_eyes=have_mouth=true;
                if(xSemaphoreTake(g_telemetry_mutex,pdMS_TO_TICKS(15))==pdTRUE){
                    g_telemetry.ear_l=r.left_ear;g_telemetry.ear_r=r.right_ear;g_telemetry.jaw=r.jaw;
                    g_telemetry.blink_l=r.blink_left;g_telemetry.blink_r=r.blink_right;g_telemetry.mouth_ratio=r.mouth_ratio;
                    g_telemetry.confidence=r.confidence;g_telemetry.inference_ms=r.inference_ms;
                    const int overlay_ids[16]={33,160,158,133,153,144,362,385,387,263,373,380,1,13,14,291};
                    for(int i=0;i<16;i++){g_telemetry.overlay[2*i]=r.points[overlay_ids[i]].x/img.width;g_telemetry.overlay[2*i+1]=r.points[overlay_ids[i]].y/img.height;}
                    xSemaphoreGive(g_telemetry_mutex);
                }
            }
        }
        g_v7_result.inference_ms=(esp_timer_get_time()-inference_start)/1000.f;
        if(xSemaphoreTake(g_telemetry_mutex,pdMS_TO_TICKS(15))==pdTRUE){
            snprintf(g_telemetry.face_reason,sizeof(g_telemetry.face_reason),"%s",g_v7_result.reason);
            g_telemetry.detector_score=g_v7_result.detector_score;
            g_telemetry.confidence=g_v7_result.confidence;
            g_telemetry.inference_ms=g_v7_result.inference_ms;
            g_telemetry.landmark_ms=g_v7_result.landmark_ms;g_telemetry.prelu_ms=g_v7_result.prelu_ms;
            g_telemetry.preprocess_ms=g_v7_result.preprocess_ms;g_telemetry.expression_ms=g_v7_result.expression_ms;
            g_telemetry.detector_ms=g_v7_result.detector_ms;
            xSemaphoreGive(g_telemetry_mutex);
        }
        ESP_LOGI(TAG,"PERF total=%.0fms detect=%.0f prep=%.0f landmarks=%.0f (prelu=%.0f) expressions=%.0f",
            g_v7_result.inference_ms,g_v7_result.detector_ms,g_v7_result.preprocess_ms,
            g_v7_result.landmark_ms,g_v7_result.prelu_ms,g_v7_result.expression_ms);
        heap_caps_free(img.data);
        if (!valid) {
            reset_timers();
            publish_progress(false);
            static int64_t last_warn_us=0;
            const int64_t now=esp_timer_get_time();
            if (now-last_warn_us>2000000) {
                ESP_LOGW(TAG,"FACE WAIT: %s | detector=%.3f landmarks=%.3f time=%.0fms",g_v7_result.reason,g_v7_result.detector_score,g_v7_result.confidence,g_v7_result.inference_ms);
                last_warn_us=now;
            }
            vTaskDelay(pdMS_TO_TICKS(150));
            continue;
        }
        if (tracking) track_frame(pose,have_eyes,have_mouth);
        else accept_sample(pose,have_eyes,have_mouth);
        publish_progress(true);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}


static bool admin_key_valid(const cJSON *request) {
    const cJSON *key = cJSON_GetObjectItemCaseSensitive(request,"key");
    return cJSON_IsString(key) && key->valuestring && strcmp(key->valuestring,g_ap_password)==0;
}
static esp_err_t json_reply(httpd_req_t *req, cJSON *doc, int code = 200) {
    if (!doc) { httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Out of memory"); return ESP_FAIL; }
    char *json = cJSON_PrintUnformatted(doc);
    cJSON_Delete(doc);
    if (!json) { httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Out of memory"); return ESP_FAIL; }
    if (code == 400) httpd_resp_set_status(req,"400 Bad Request");
    if (code == 409) httpd_resp_set_status(req,"409 Conflict");
    if (code == 403) httpd_resp_set_status(req,"403 Forbidden");
    if (code == 503) httpd_resp_set_status(req,"503 Service Unavailable");
    httpd_resp_set_type(req,"application/json");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");
    const esp_err_t result = httpd_resp_send(req,json,HTTPD_RESP_USE_STRLEN);
    free(json);
    return result;
}
static esp_err_t error_json(httpd_req_t *req, const char *message, int code) {
    cJSON *doc=cJSON_CreateObject();
    if (doc) cJSON_AddStringToObject(doc,"error",message);
    return json_reply(req,doc,code);
}
static cJSON *post_json(httpd_req_t *req) {
    if (req->content_len <= 0 || req->content_len > 384) return nullptr;
    char data[385]; size_t offset=0;
    while (offset < static_cast<size_t>(req->content_len)) {
        int n=httpd_req_recv(req,data+offset,req->content_len-offset);
        if (n<=0) return nullptr;
        offset+=n;
    }
    data[offset]='\0';
    return cJSON_Parse(data);
}
static esp_err_t index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");
    httpd_resp_set_hdr(req,"Content-Security-Policy","default-src 'self'; img-src 'self' data:; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; object-src 'none'; frame-ancestors 'none'");
    return httpd_resp_send(req,reinterpret_cast<const char *>(index_html_start),index_html_end-index_html_start);
}
static esp_err_t status_handler(httpd_req_t *req) {
    Telemetry t;
    if (xSemaphoreTake(g_telemetry_mutex,pdMS_TO_TICKS(40)) != pdTRUE)
        return error_json(req,"Telemetry busy",503);
    t=g_telemetry;
    xSemaphoreGive(g_telemetry_mutex);
    cJSON *doc=cJSON_CreateObject();
    if (!doc) return error_json(req,"Memory unavailable",503);
    const char *const steps[]={
      "Eyes open", "Eyes closed", "Neutral / mouth closed", "Mouth wide open", "Head down",
      "Turn head left", "Turn head right", "Tilt head left", "Tilt head right"};
    cJSON_AddBoolToObject(doc,"face",t.face);
    cJSON_AddStringToObject(doc,"engine_error",t.engine_error);
    cJSON_AddStringToObject(doc,"face_reason",t.face_reason);
    cJSON_AddNumberToObject(doc,"detector_score",t.detector_score);
    cJSON_AddNumberToObject(doc,"landmark_ms",t.landmark_ms);cJSON_AddNumberToObject(doc,"prelu_ms",t.prelu_ms);
    cJSON_AddNumberToObject(doc,"preprocess_ms",t.preprocess_ms);cJSON_AddNumberToObject(doc,"expression_ms",t.expression_ms);
    cJSON_AddNumberToObject(doc,"detector_ms",t.detector_ms);
    cJSON_AddNumberToObject(doc,"ear_left",t.ear_l);cJSON_AddNumberToObject(doc,"ear_right",t.ear_r);
    cJSON_AddBoolToObject(doc,"blendshapes_available",false);
    cJSON_AddNumberToObject(doc,"mouth_ratio",t.mouth_ratio);
    cJSON_AddNumberToObject(doc,"face_confidence",t.confidence);cJSON_AddNumberToObject(doc,"inference_ms",t.inference_ms);
    cJSON_AddItemToObject(doc,"overlay",cJSON_CreateFloatArray(t.overlay,32));
    cJSON_AddBoolToObject(doc,"calibrated",t.calibrated);
    cJSON_AddStringToObject(doc,"eye_mouth_engine","mediapipe-468-geometry-esp32");
    cJSON_AddBoolToObject(doc,"calibration_running",t.calibration_running);
    cJSON_AddBoolToObject(doc,"calibration_waiting",t.calibration_waiting);
    cJSON_AddBoolToObject(doc,"calibration_saved",t.calibration_saved);
    cJSON_AddNumberToObject(doc,"calibration_step",t.stage < STAGE_COUNT ? t.stage+1 : STAGE_COUNT);
    cJSON_AddNumberToObject(doc,"calibration_samples",t.samples);
    cJSON_AddNumberToObject(doc,"calibration_target",CAPTURE_SAMPLES);
    cJSON_AddStringToObject(doc,"calibration_instruction",t.stage < STAGE_COUNT ? steps[t.stage] : "Complete");
    cJSON_AddBoolToObject(doc,"calibration_preparing",t.preparing);
    cJSON_AddNumberToObject(doc,"calibration_wait_s",t.wait_s);
    cJSON_AddStringToObject(doc,"eye",t.eye);
    cJSON_AddStringToObject(doc,"mouth",t.mouth);
    cJSON_AddStringToObject(doc,"away",t.away);
    cJSON_AddStringToObject(doc,"tilt",t.tilt);
    cJSON_AddBoolToObject(doc,"down",t.head_down);
    cJSON_AddNumberToObject(doc,"eye_ms",t.eye_ms);
    cJSON_AddNumberToObject(doc,"mouth_ms",t.mouth_ms);
    cJSON_AddNumberToObject(doc,"down_ms",t.down_ms);
    cJSON_AddNumberToObject(doc,"away_ms",t.away_ms);
    cJSON_AddNumberToObject(doc,"tilt_ms",t.tilt_ms);
    cJSON_AddNumberToObject(doc,"down_score",t.down_score);
    cJSON_AddNumberToObject(doc,"yaw_delta",t.yaw_delta);
    cJSON_AddNumberToObject(doc,"roll_delta",t.roll_delta);
    cJSON *quality=cJSON_AddObjectToObject(doc,"calibration_quality");
    if (quality) {
        cJSON_AddBoolToObject(quality,"eye",t.eye_ok);
        cJSON_AddBoolToObject(quality,"mouth",t.mouth_ok);
        cJSON_AddBoolToObject(quality,"down",t.down_ok);
        cJSON_AddBoolToObject(quality,"yaw_left",t.yaw_left_ok);
        cJSON_AddBoolToObject(quality,"yaw_right",t.yaw_right_ok);
        cJSON_AddBoolToObject(quality,"tilt_left",t.tilt_left_ok);
        cJSON_AddBoolToObject(quality,"tilt_right",t.tilt_right_ok);
    }
    cJSON_AddNumberToObject(doc,"age_ms",t.update_us ? (esp_timer_get_time()-t.update_us)/1000 : -1);
    cJSON_AddBoolToObject(doc,"wifi_connected",g_wifi_connected.load());
    wifi_config_t conf={};
    esp_wifi_get_config(WIFI_IF_STA,&conf);
    char saved_ssid[33] = {};
    memcpy(saved_ssid,conf.sta.ssid,32);
    cJSON_AddStringToObject(doc,"wifi_ssid",saved_ssid);
    if (g_wifi_connected && g_sta_netif) {
        esp_netif_ip_info_t ip={};
        if (esp_netif_get_ip_info(g_sta_netif,&ip)==ESP_OK) {
            char str[16]; snprintf(str,sizeof(str),IPSTR,IP2STR(&ip.ip));
            cJSON_AddStringToObject(doc,"wifi_ip",str);
        }
    }
    cJSON_AddStringToObject(doc,"ap_ssid",g_ap_ssid);
    cJSON_AddStringToObject(doc,"ap_ip","192.168.4.1");
    return json_reply(req,doc);
}
static esp_err_t frame_handler(httpd_req_t *req) {
    if (xSemaphoreTake(g_frame_mutex,pdMS_TO_TICKS(50))!=pdTRUE)
        return error_json(req,"Preview busy",503);
    uint8_t *buf=nullptr; size_t n=g_frame_size;
    if (g_frame && n>0) {
        buf=static_cast<uint8_t *>(heap_caps_malloc(n,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (buf) memcpy(buf,g_frame,n);
    }
    xSemaphoreGive(g_frame_mutex);
    if (!buf) return error_json(req,"Preview unavailable",503);
    httpd_resp_set_type(req,"image/jpeg");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");
    const esp_err_t err=httpd_resp_send(req,reinterpret_cast<const char *>(buf),n);
    heap_caps_free(buf);
    return err;
}
static esp_err_t wifi_handler(httpd_req_t *req) {
    cJSON *doc=post_json(req);
    if (!doc) return error_json(req,"Invalid JSON",400);
    if (!admin_key_valid(doc)) {cJSON_Delete(doc);return error_json(req,"Wrong setup access key",403);}
    const cJSON *ssid=cJSON_GetObjectItemCaseSensitive(doc,"ssid");
    const cJSON *pass=cJSON_GetObjectItemCaseSensitive(doc,"password");
    if (!cJSON_IsString(ssid) || !ssid->valuestring || !cJSON_IsString(pass) || !pass->valuestring) {
        cJSON_Delete(doc);return error_json(req,"SSID and password required",400);
    }
    const size_t sl=strlen(ssid->valuestring),pl=strlen(pass->valuestring);
    if (sl==0 || sl>32 || pl>63 || (pl>0 && pl<8)) {
        cJSON_Delete(doc);return error_json(req,"SSID max 32 bytes; password 8-63 bytes (or empty for open Wi-Fi)",400);
    }
    wifi_config_t cfg={};
    memcpy(cfg.sta.ssid,ssid->valuestring,sl);
    memcpy(cfg.sta.password,pass->valuestring,pl);
    cfg.sta.threshold.authmode=WIFI_AUTH_OPEN;
    cfg.sta.pmf_cfg.capable=true;
    g_wifi_pending.store(true);
    esp_wifi_disconnect();
    esp_err_t err=esp_wifi_set_config(WIFI_IF_STA,&cfg);
    g_wifi_connected.store(false);
    g_wifi_connecting.store(false);
    if (err==ESP_OK) err=esp_wifi_connect();
    if (err==ESP_OK) g_wifi_connecting.store(true);
    g_wifi_pending.store(false);
    cJSON_Delete(doc);
    if (err!=ESP_OK) return error_json(req,"Could not start Wi-Fi connection; check serial log",503);
    ESP_LOGI(TAG,"Station Wi-Fi connection requested (password not logged)");
    cJSON *response=cJSON_CreateObject(); cJSON_AddBoolToObject(response,"ok",true);
    cJSON_AddStringToObject(response,"message","Credentials saved, attempting Wi-Fi connection");
    return json_reply(req,response);
}
static esp_err_t forget_wifi_handler(httpd_req_t *req) {
    cJSON *doc=post_json(req);
    if (!doc) return error_json(req,"Invalid JSON",400);
    if (!admin_key_valid(doc)) {cJSON_Delete(doc);return error_json(req,"Wrong setup access key",403);}
    cJSON_Delete(doc);
    g_wifi_pending.store(true);
    esp_wifi_disconnect();
    wifi_config_t cfg={};
    const esp_err_t err=esp_wifi_set_config(WIFI_IF_STA,&cfg);
    g_wifi_connected.store(false);g_wifi_connecting.store(false);
    g_wifi_pending.store(false);
    if (err!=ESP_OK) return error_json(req,"Could not forget network",503);
    cJSON *response=cJSON_CreateObject(); cJSON_AddBoolToObject(response,"ok",true);
    return json_reply(req,response);
}
static esp_err_t restart_handler(httpd_req_t *req) {
    cJSON *doc=post_json(req);
    if (!doc) return error_json(req,"Invalid JSON",400);
    if (!admin_key_valid(doc)) {cJSON_Delete(doc);return error_json(req,"Wrong setup access key",403);}
    const cJSON *selected=cJSON_GetObjectItemCaseSensitive(doc,"stage");
    int requested=-1;
    const cJSON *action=cJSON_GetObjectItemCaseSensitive(doc,"action");
    if (action && (!cJSON_IsString(action) || strcmp(action->valuestring,"ready")!=0 || selected)) {
        cJSON_Delete(doc);return error_json(req,"Invalid calibration action",400);
    }
    if (action) {
        bool waiting=false;
        if (xSemaphoreTake(g_telemetry_mutex,pdMS_TO_TICKS(15))==pdTRUE) {
            waiting=g_telemetry.calibration_running && g_telemetry.calibration_waiting;
            xSemaphoreGive(g_telemetry_mutex);
        }
        if (!waiting) {cJSON_Delete(doc);return error_json(req,"No calibration step waiting",409);}
        requested=-3;
    }
    if (selected) {
        if (!cJSON_IsNumber(selected) || selected->valuedouble<1 ||
            selected->valuedouble>static_cast<int>(STAGE_COUNT) || selected->valuedouble!=selected->valueint) {
            cJSON_Delete(doc); return error_json(req,"Stage must be an integer from 1 to 9",400);
        }
        requested=selected->valueint-1;
        bool complete=false;
        if (xSemaphoreTake(g_telemetry_mutex,pdMS_TO_TICKS(15))==pdTRUE) {
            complete=g_telemetry.calibrated;
            xSemaphoreGive(g_telemetry_mutex);
        }
        if (!complete) {cJSON_Delete(doc);return error_json(req,"Finish the current calibration first",409);}
    }
    cJSON_Delete(doc);
    int expected=-2;
    if (!g_calibration_request.compare_exchange_strong(expected,requested))
        return error_json(req,"Calibration request already pending",409);
    ESP_LOGI(TAG,"Dashboard calibration request accepted: %d",requested);
    cJSON *response=cJSON_CreateObject(); cJSON_AddBoolToObject(response,"ok",true);
    cJSON_AddStringToObject(response,"message","Calibration request accepted. Follow the pose instructions above.");
    return json_reply(req,response);
}
static esp_err_t scan_handler(httpd_req_t *req) {
    if (g_wifi_connecting.load() || g_wifi_pending.load()) return error_json(req,"Wi-Fi is connecting; retry scan later",503);
    const esp_err_t err=esp_wifi_scan_start(nullptr,true);
    if (err != ESP_OK) return error_json(req,"Wi-Fi scan unavailable (try again)",503);
    uint16_t found=0; esp_wifi_scan_get_ap_num(&found);
    const uint16_t count=found>16 ? 16 : found;
    wifi_ap_record_t *aps=static_cast<wifi_ap_record_t *>(calloc(count ? count : 1,sizeof(wifi_ap_record_t)));
    if (!aps) {esp_wifi_clear_ap_list();return error_json(req,"Out of memory",503);}
    uint16_t n=count;
    esp_err_t result=esp_wifi_scan_get_ap_records(&n,aps);
    if (result!=ESP_OK) {free(aps);esp_wifi_clear_ap_list();return error_json(req,"Scan results unavailable",503);}
    cJSON *doc=cJSON_CreateObject();cJSON *arr=cJSON_AddArrayToObject(doc,"networks");
    if (!doc || !arr) {free(aps);if(doc)cJSON_Delete(doc);return error_json(req,"Out of memory",503);}
    for (uint16_t i=0;i<n;++i) {
        cJSON *item=cJSON_CreateObject(); if (!item) continue;
        char name[33]={};memcpy(name,aps[i].ssid,32);
        cJSON_AddStringToObject(item,"ssid",name);
        cJSON_AddNumberToObject(item,"rssi",aps[i].rssi);
        cJSON_AddBoolToObject(item,"secured",aps[i].authmode!=WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(arr,item);
    }
    free(aps);return json_reply(req,doc);
}
static void wifi_event(void *,esp_event_base_t base,int32_t id,void *data) {
    if (base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) {
        g_wifi_connected.store(false);
        g_wifi_connecting.store(false);
        if (data) {
            auto *event=static_cast<wifi_event_sta_disconnected_t *>(data);
            ESP_LOGW(TAG,"Wi-Fi disconnected; reason=%u",(unsigned)event->reason);
        }
    } else if (base==WIFI_EVENT && id==WIFI_EVENT_STA_CONNECTED) {
        g_wifi_connecting.store(false);
    } else if (base==IP_EVENT && id==IP_EVENT_STA_GOT_IP) {
        g_wifi_connected.store(true);
        g_wifi_connecting.store(false);
        auto *event=static_cast<ip_event_got_ip_t *>(data);
        ESP_LOGI(TAG,"Wi-Fi connected: " IPSTR,IP2STR(&event->ip_info.ip));
    }
}
static void connection_task(void *) {
    int64_t next_attempt_us = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        if (g_wifi_connected.load() || g_wifi_connecting.load() || g_wifi_pending.load()) continue;
        wifi_config_t cfg={};
        if (esp_wifi_get_config(WIFI_IF_STA,&cfg)!=ESP_OK || !cfg.sta.ssid[0]) continue;
        if (esp_timer_get_time()<next_attempt_us) continue;
        next_attempt_us=esp_timer_get_time()+12000000;
        if (esp_wifi_connect()==ESP_OK) g_wifi_connecting.store(true);
    }
}
static void start_http() {
    httpd_config_t config=HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers=8;
    config.stack_size=8192;
    config.recv_wait_timeout=7;
    config.send_wait_timeout=7;
    httpd_handle_t srv=nullptr;
    ESP_ERROR_CHECK(httpd_start(&srv,&config));
    const httpd_uri_t routes[]={
        {.uri="/",.method=HTTP_GET,.handler=index_handler,.user_ctx=nullptr},
        {.uri="/api/status",.method=HTTP_GET,.handler=status_handler,.user_ctx=nullptr},
        {.uri="/api/frame",.method=HTTP_GET,.handler=frame_handler,.user_ctx=nullptr},
        {.uri="/api/scan",.method=HTTP_GET,.handler=scan_handler,.user_ctx=nullptr},
        {.uri="/api/wifi",.method=HTTP_POST,.handler=wifi_handler,.user_ctx=nullptr},
        {.uri="/api/wifi/forget",.method=HTTP_POST,.handler=forget_wifi_handler,.user_ctx=nullptr},
        {.uri="/api/calibration",.method=HTTP_POST,.handler=restart_handler,.user_ctx=nullptr},
    };
    for (const auto &uri:routes) ESP_ERROR_CHECK(httpd_register_uri_handler(srv,&uri));
}
static void load_or_create_ap_password() {
    // Fixed hotspot password and dashboard key requested for this prototype.
    strcpy(g_ap_password,"12345678");
    nvs_handle_t handle;
    ESP_ERROR_CHECK(nvs_open("safe_road",NVS_READWRITE,&handle));
    ESP_ERROR_CHECK(nvs_set_str(handle,"ap_pass",g_ap_password));
    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);
}
static void start_network() {
    esp_err_t err=nvs_flash_init();
    if (err==ESP_ERR_NVS_NO_FREE_PAGES || err==ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG,"NVS incompatible/full, erasing NVS settings");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err=nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    load_or_create_ap_password();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    g_sta_netif=esp_netif_create_default_wifi_sta();
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,&wifi_event,nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,&wifi_event,nullptr));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    uint8_t mac[6]; ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_AP,mac));
    snprintf(g_ap_ssid,sizeof(g_ap_ssid),"SAFE_ROAD_%02X%02X%02X",mac[3],mac[4],mac[5]);
    wifi_config_t ap={};
    strcpy(reinterpret_cast<char *>(ap.ap.ssid),g_ap_ssid);
    strcpy(reinterpret_cast<char *>(ap.ap.password),g_ap_password);
    ap.ap.ssid_len=strlen(g_ap_ssid);
    ap.ap.channel=1; ap.ap.authmode=WIFI_AUTH_WPA2_PSK; ap.ap.max_connection=3;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP,&ap));
    ESP_ERROR_CHECK(esp_wifi_start());
    wifi_config_t sta={};
    if (esp_wifi_get_config(WIFI_IF_STA,&sta)==ESP_OK && sta.sta.ssid[0]) {
        if (esp_wifi_connect()==ESP_OK) g_wifi_connecting.store(true);
    }
    start_http();
    ESP_LOGW(TAG,"===== SETUP ACCESS =====");
    ESP_LOGW(TAG,"Wi-Fi name: %s",g_ap_ssid);
    ESP_LOGW(TAG,"Wi-Fi password + dashboard setup key: %s",g_ap_password);
    ESP_LOGW(TAG,"Dashboard: http://192.168.4.1");
    ESP_LOGW(TAG,"Access key is needed to change Wi-Fi or restart calibration; do not share it.");
    xTaskCreate(connection_task,"wifi_retry",3072,nullptr,3,nullptr);
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG,"S.A.F.E. R.O.A.D. / SMALL 468-point camera monitoring EXPERIMENT");
    const size_t psram=heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG,"PSRAM available: %u MB",(unsigned)(psram/1048576));
    if (psram<2*1024*1024) {ESP_LOGE(TAG,"PSRAM not configured");return;}
    g_telemetry_mutex=xSemaphoreCreateMutex();
    g_frame_mutex=xSemaphoreCreateMutex();
    if (!g_telemetry_mutex || !g_frame_mutex) {ESP_LOGE(TAG,"Mutex allocation failed");return;}
    if (start_camera()!=ESP_OK) {ESP_LOGE(TAG,"Camera initialization failed");return;}
    start_network();
    if (xTaskCreatePinnedToCore(face_task,"safe_road_s3",20480,nullptr,5,nullptr,1)!=pdPASS)
        ESP_LOGE(TAG,"Cannot create face monitoring task");
}
